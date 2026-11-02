package com.xiakexing.client.feature.shell

import com.xiakexing.client.core.network.ConnectionState
import com.xiakexing.client.core.network.GameConnectionRepository
import com.xiakexing.client.core.network.GatewayEndpoint
import com.xiakexing.client.core.protocol.XkStreamDecoder
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

/**
 * 连接呈现协调器（ShellConnectionLifecycle + ShellConnectionEvents 合并）。
 * 职责：连接生命周期状态机事件、前后台切换、重连覆盖层、连接重置瞬态清理。
 * 纯呈现逻辑在 ConnectionPresentationReducer.kt。
 */
internal class ConnectionPresentationCoordinator(
    private val connectionRepository: GameConnectionRepository,
    private val connectionSession: ShellConnectionSessionState,
    private val streamDecoder: XkStreamDecoder,
    private val scope: CoroutineScope,
    private val sink: ConnectionPresentationSink,
) {
    private var reconnectTimeoutJob: Job? = null
    private var firstScreenTxJob: Job? = null
    private var firstScreenTxResynced: Boolean = false
    private var diagnosticEndpoint: String = ""
    private var diagnosticStage: String = "未开始"
    private var diagnosticLastFrame: String = "无"

    // ===== 会话状态 =====

    fun applySessionEvent(event: ShellEvent) {
        sink.updateState {
            copy(session = ShellSessionReducer.reduce(session, event))
        }
    }

    fun updateStateWithSessionEvent(
        event: ShellEvent,
        transform: ShellUiState.() -> ShellUiState = { this },
    ) {
        sink.updateState {
            val next = transform()
            next.copy(session = ShellSessionReducer.reduce(next.session, event))
        }
    }

    // ===== app 前后台 =====

    fun onForegrounded() {
        connectionSession.markForegrounded()
        if (sink.currentState().appScene == ShellScene.LOGIN) {
            sink.maybeStartAutomaticUpdateCheck()
        }
    }

    fun onBackgrounded(): Boolean {
        connectionSession.markBackgrounded()
        return shouldKeepRecoverableSessionAlive(sink.currentState().session.phase)
    }

    fun shouldKeepRecoverableSessionAlive(phase: ShellPhase): Boolean {
        return connectionSession.shouldKeepRecoverableSessionAlive(phase)
    }

    fun shouldSuspendForForegroundResume(): Boolean {
        return connectionSession.shouldSuspendForForegroundResume()
    }

    fun prepareResumeConnection(): GatewayEndpoint? {
        return connectionSession.prepareResumeConnection()
    }

    // ===== 连接重置瞬态清理 =====

    fun resetConnectionTransientState() {
        sink.clearPendingRoomRefresh()
        streamDecoder.reset()
        sink.resetProtocolPacketGate()
        sink.clearPendingMoveAutoScroll()
        sink.clearPendingInteractionRequest()
        sink.clearPendingProtocolSheetRequest()
        sink.clearPendingProtocolPopupRequest()
        sink.clearPendingInventoryInspectItem()
        sink.clearTrackedQuest()
    }

    // ===== 方案 R-20（S14）：主壳重连覆盖层 =====

    /** 重连覆盖层自动重连等待：超过后展示「重试 / 返回登录」 */
    fun retryReconnect() {
        cancelReconnectTimeout()
        sink.updateState { reconnectOverlayState(overlay = true, timedOut = false) }
        val endpoint = connectionSession.lastEndpoint()
        if (endpoint == null) {
            leaveReconnectToLogin()
            return
        }
        startConnection(endpoint, resumeSession = true)
        scheduleReconnectTimeout()
    }

    /** 重连覆盖层「返回登录」：按现状手动断开语义回到登录页 */
    fun leaveReconnectToLogin() {
        cancelReconnectTimeout()
        sink.updateState { reconnectOverlayState(overlay = false, timedOut = false) }
        disconnect()
    }

    private fun beginReconnectOverlay(reason: String) {
        cancelReconnectTimeout()
        sink.updateState { reconnectOverlayState(overlay = true, timedOut = false) }
        sink.appendTranscript("[恢复] $reason，正在重连…")
        val endpoint = connectionSession.lastEndpoint()
        if (endpoint == null) {
            leaveReconnectToLogin()
            return
        }
        startConnection(endpoint, resumeSession = true)
        scheduleReconnectTimeout()
    }

    private fun scheduleReconnectTimeout() {
        reconnectTimeoutJob?.cancel()
        reconnectTimeoutJob =
            scope.launch {
                delay(RECONNECT_TIMEOUT_MS)
                if (sink.currentState().reconnectOverlay) {
                    sink.updateState { copy(reconnectTimedOut = true) }
                }
            }
    }

    private fun cancelReconnectTimeout() {
        reconnectTimeoutJob?.cancel()
        reconnectTimeoutJob = null
    }

    fun connect() {
        val current = sink.currentState()
        if (current.clientUpdateRequired) {
            return
        }
        val port = current.portText.toIntOrNull()
        if (port == null) {
            sink.appendErrorFeedback("端口格式无效")
            return
        }

        resetForConnect(sink)
        startConnection(
            GatewayEndpoint(
                host = current.host.trim(),
                port = port,
                path = current.path.trim(),
                secure = current.secure,
            ),
            resumeSession = false,
        )
    }

    fun startConnection(
        endpoint: GatewayEndpoint,
        resumeSession: Boolean,
    ) {
        if (sink.currentState().clientUpdateRequired) {
            return
        }
        diagnosticEndpoint = endpoint.toWebSocketUrl()
        diagnosticStage = "WebSocket 握手"
        diagnosticLastFrame = "无"
        connectionSession.prepareConnection(resumeSession)
        sink.clearPendingRoomRefresh()
        sink.resetIncomingStream()

        sink.resetMessageForConnection(
            connectionEndpointLine(resumeSession, endpoint),
            preserveNoticeFeed = resumeSession,
        )

        if (resumeSession) {
            sink.updateState { resumeConnectionState() }
            connectionRepository.connectResume(endpoint)
            return
        }

        sink.updateState {
            freshConnectionState(
                visibleTranscript = sink.visibleTranscript(),
                topNoticeLines = sink.recentNoticeFeed(),
            )
        }

        connectionRepository.connectFresh(endpoint)
    }

    fun disconnect() {
        cancelReconnectTimeout()
        connectionSession.prepareManualDisconnect()
        sink.clearPendingRoomRefresh()
        sink.clearPendingMoveAutoScroll()
        sink.clearPendingInteractionRequest()
        sink.resetIncomingStream()
        val hadActiveConnection = connectionRepository.hasActiveConnection()
        connectionRepository.manualDisconnect()
        if (!hadActiveConnection) {
            applyManualDisconnect(sink)
            connectionSession.clearManualDisconnectRequest()
        }
    }

    /** 消费进程级 owner 的窄口相位事件，映射为壳侧呈现。 */
    fun handlePhaseEvent(event: GameConnectionRepository.PhaseEvent) {
        when (event) {
            is GameConnectionRepository.PhaseEvent.Connecting -> onPhaseConnecting(event.resuming)
            is GameConnectionRepository.PhaseEvent.Connected -> {
                diagnosticEndpoint = event.endpoint.toWebSocketUrl()
                diagnosticStage = "等待登录提示"
                diagnosticLastFrame = "WebSocket 已打开"
                handleConnected(event)
            }
            is GameConnectionRepository.PhaseEvent.Disconnected ->
                when (
                    handleDisconnected(
                        ConnectionState.Disconnected(event.reason),
                        connectionRepository,
                        connectionSession,
                        sink,
                    )
                ) {
                    // 保活语义：非用户意图断线一律自动重连覆盖层 + 自动重连（不再有后台挂起分支）。
                    DisconnectOutcome.ReconnectOverlay -> beginReconnectOverlay(event.reason)
                    DisconnectOutcome.Reset -> Unit
                }
            is GameConnectionRepository.PhaseEvent.Failed ->
                handleError(
                    ConnectionState.Error(event.message),
                    connectionSession,
                    diagnosticState(),
                    sink,
                )
            is GameConnectionRepository.PhaseEvent.LoginWaitTimedOut ->
                failLoginWait(
                    waitingPhases = LOGIN_WAIT_PHASES,
                    message = event.message,
                    detail = loginWaitTimeoutDetail(event.message),
                    connectionSession = connectionSession,
                    diagnostic = diagnosticState(),
                    sink = sink,
                )
            GameConnectionRepository.PhaseEvent.LoginPromptReady -> handleLoginPromptReceived()
            GameConnectionRepository.PhaseEvent.LoginBootstrapCompleted -> handleLoginBootstrapComplete()
            is GameConnectionRepository.PhaseEvent.ResumeTokenIssued -> Unit // token 已由到达点直存（LoginCoordinator），事件通道不重复（同 LoginPromptReady 先例）
        }
    }

    private fun onPhaseConnecting(resuming: Boolean) {
        if (!resuming) {
            // fresh 连接的 UI 全量重置（原 startConnection 内嵌段）保留在 shell 侧。
            resetForFreshConnection(sink)
        }
    }

    private fun diagnosticState() =
        ConnectionDiagnostic(diagnosticEndpoint, diagnosticStage, diagnosticLastFrame)

    fun cancel() {
        // 登录等待看门狗已归 owner；重连覆盖层超时仍属壳侧呈现。
        cancelReconnectTimeout()
        cancelFirstScreenTransactionWatchdog()
    }

    private fun markLoginPromptFrameReceived() {
        diagnosticLastFrame = "登录提示"
    }

    fun handleLoginPromptReceived() {
        // 登录超时策略唯一在 owner（GameConnectionRepository）；
        // UI 只消费 LoginWaitTimedOut 事件渲染终态。
        markLoginPromptFrameReceived()
        diagnosticStage = "等待进入世界"
    }

    fun handleLoginBootstrapComplete() {
        diagnosticStage = "已进入江湖"
        diagnosticLastFrame = "启动同步完成"
        sink.updateStateWithSessionEvent(ShellEvent.LoginBootstrapCompleted) {
            loginBootstrapCompleteState()
        }
        armFirstScreenTransactionWatchdog()
    }

    /**
     * 首屏不变量（方案 2.4-3）：进入 Playing 后限定时间内必须提交同代完整房间事务
     * （REAL+TITL+EXIT+NPCS/ITEM 权威收敛）。超时先执行一次受控 resync，
     * 仍失败则进入明确错误态，不再用空标题伪装房间名。
     */
    private fun armFirstScreenTransactionWatchdog() {
        firstScreenTxJob?.cancel()
        firstScreenTxJob =
            scope.launch {
                delay(FIRST_SCREEN_TX_TIMEOUT_MS)
                val projection = sink.currentState().projectionRuntime.projection?.projection
                val committed =
                    if (projection?.isV2 == true) {
                        // v2 投影：强制同代比对，防止跨代拼包
                        sink.hasCommittedCompleteRoomSnapshot(
                            projection.projectionId,
                            projection.projectionRevision,
                            projection.projectionGeneration,
                        )
                    } else {
                        false
                    }
                if (committed) {
                    firstScreenTxResynced = false
                    return@launch
                }
                if (!firstScreenTxResynced) {
                    firstScreenTxResynced = true
                    sink.appendTranscript("[同步] 首屏房间事务超时，正在重新同步…")
                    sink.requestAuthoritativeResync()
                    armFirstScreenTransactionWatchdog()
                    return@launch
                }
                firstScreenTxResynced = false
                sink.appendErrorFeedback("世界同步失败")
                sink.updateState {
                    copy(
                        session =
                            ShellSessionReducer.reduce(
                                session,
                                ShellEvent.DetailUpdated("世界同步失败"),
                            ),
                        floatingMessages =
                            floatingMessages.withFloatingMessage(
                                FloatingMessageState(
                                    id = nextFloatingMessageId(),
                                    text = "世界同步失败，请重连或重新同步",
                                    level = "error",
                                    ttlMs = 4_000L.coerceFloatingMessageTtl(),
                                    dedupeKey = "world_sync_failed",
                                ),
                            ),
                    )
                }
            }
    }

    private fun cancelFirstScreenTransactionWatchdog() {
        firstScreenTxJob?.cancel()
        firstScreenTxJob = null
        firstScreenTxResynced = false
    }

    private fun handleConnected(event: GameConnectionRepository.PhaseEvent.Connected) {
        val wasResume = event.resuming || connectionSession.connectedWasResume()
        val presentation = presentConnectedState(event.endpoint, wasResume)

        if (wasResume) {
            cancelReconnectTimeout()
            // 方案 R-20：后台恢复/重连成功给出顶部信息细条（2.8s 自消，去重刷新），并收起 S14 覆盖层
            sink.updateState { resumeConnectedState() }
        } else {
            sink.updateStateWithSessionEvent(ShellEvent.ConnectionEstablished) {
                applyConnectedPresentation()
            }
        }
        sink.appendTranscript(presentation.transcriptLine)
    }
}

// 超时消息常量唯一权威在 GameConnectionRepository companion（owner 发射与壳侧呈现共用同一值）。
internal const val MANUAL_DISCONNECT_REASON = GameConnectionRepository.MANUAL_DISCONNECT_REASON
internal const val MANUAL_DISCONNECT_LABEL = "手动断开连接"

/** 方案 R-20：S14 重连覆盖层等待时长，超时展示「重试 / 返回登录」 */
internal const val RECONNECT_TIMEOUT_MS = 5_000L

/** 首屏不变量：进入 Playing 后等待完整房间事务的时长（方案 2.4-3）。 */
internal const val FIRST_SCREEN_TX_TIMEOUT_MS = 5_000L

/** R-19：登录等待超时覆盖的相位集合。 */
private val LOGIN_WAIT_PHASES =
    setOf(
        ShellPhase.WAITING_LOGIN_PROMPT,
        ShellPhase.AUTHENTICATING,
        ShellPhase.WAITING_WORLD,
    )
