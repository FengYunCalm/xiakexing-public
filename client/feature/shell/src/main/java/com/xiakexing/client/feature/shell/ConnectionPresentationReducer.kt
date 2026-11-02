package com.xiakexing.client.feature.shell

import com.xiakexing.client.core.network.GameConnectionRepository
import com.xiakexing.client.core.network.GatewayEndpoint

internal data class ConnectionConnectedPresentation(
    val transcriptLine: String,
)

internal data class ConnectionResetRuntimeState(
    val clearInventoryItems: Boolean = true,
    val clearQuickActionGroups: Boolean = true,
    val clearMapGrid: Boolean = true,
    val clearSkillSummaries: Boolean = true,
    val clearUnhandledPacketTypes: Boolean = true,
)

internal data class ConnectionSuspensionPresentation(
    val detail: String,
    val transcriptLine: String,
)

internal data class ConnectionResetUiState(
    val appScene: ShellScene,
    val dialog: ShellPopupState? = null,
    val sheet: ShellSheetState? = null,
    val commandPalette: CommandPaletteState? = null,
    val exitOverlay: ExitOverlayState? = null,
    val inventoryItemsCleared: Boolean = true,
)

internal data class ConnectionDisconnectedPresentation(
    val reason: String,
    val transcriptLine: String,
    val sessionEvent: ShellEvent,
    val uiState: ConnectionResetUiState,
    val runtimeState: ConnectionResetRuntimeState = ConnectionResetRuntimeState(),
    val shouldShowLoginError: Boolean,
    val loginErrorMessage: String,
)

internal data class ConnectionErrorPresentation(
    val sessionEvent: ShellEvent,
    val uiState: ConnectionResetUiState,
    val runtimeState: ConnectionResetRuntimeState = ConnectionResetRuntimeState(),
    val shouldShowLoginError: Boolean,
    val loginErrorMessage: String,
)

internal fun presentConnectedState(
    endpoint: GatewayEndpoint,
    resumed: Boolean,
): ConnectionConnectedPresentation {
    return if (resumed) {
        ConnectionConnectedPresentation(transcriptLine = "[恢复] 已重新连接到 ${endpoint.toWebSocketUrl()}")
    } else {
        ConnectionConnectedPresentation(transcriptLine = "[连接] 已连接到 ${endpoint.toWebSocketUrl()}")
    }
}

internal fun backgroundSuspendPresentation(
    detail: String,
    transcriptLine: String,
): ConnectionSuspensionPresentation {
    return ConnectionSuspensionPresentation(
        detail = detail,
        transcriptLine = transcriptLine,
    )
}

internal fun disconnectedPresentation(
    reason: String,
    shouldShowLoginError: Boolean,
): ConnectionDisconnectedPresentation {
    return ConnectionDisconnectedPresentation(
        reason = reason,
        transcriptLine = "[连接] $reason",
        sessionEvent = ShellEvent.Disconnected(reason),
        uiState = ConnectionResetUiState(appScene = ShellScene.LOGIN),
        shouldShowLoginError = shouldShowLoginError,
        loginErrorMessage = reason,
    )
}

internal fun errorPresentation(
    currentScene: ShellScene,
    message: String,
    shouldShowLoginError: Boolean,
): ConnectionErrorPresentation {
    val nextScene = if (currentScene == ShellScene.REGISTER) ShellScene.REGISTER else ShellScene.LOGIN
    return ConnectionErrorPresentation(
        sessionEvent = ShellEvent.ConnectionFailed(message),
        uiState = ConnectionResetUiState(appScene = nextScene),
        shouldShowLoginError = shouldShowLoginError,
        loginErrorMessage = message,
    )
}

internal fun ShellUiState.applyConnectedPresentation(
    preserveSurfaces: Boolean = false,
): ShellUiState {
    return if (preserveSurfaces) {
        copy(loginErrorMessage = "")
    } else {
        copy(
            loginErrorMessage = "",
            commandPalette = null,
            exitOverlay = null,
        )
    }
}

internal fun ShellUiState.applyConnectionResetUi(
    resetUiState: ConnectionResetUiState,
): ShellUiState {
    val requiredUpdateDialog =
        dialog?.takeIf { popup ->
            clientUpdateRequired && popup.meta.contains(CLIENT_UPDATE_POPUP_META)
        }
    return copy(
        appScene = resetUiState.appScene,
        dialog = resetUiState.dialog ?: requiredUpdateDialog,
        sheet = resetUiState.sheet,
        commandPalette = resetUiState.commandPalette,
        exitOverlay = resetUiState.exitOverlay,
        inventoryItems = if (resetUiState.inventoryItemsCleared) emptyList() else inventoryItems,
    )
}

/** 连接失败诊断消息（纯呈现，下沉自 ConnectionPresentationCoordinator）。 */
internal fun diagnosticConnectionFailureMessage(
    headline: String,
    detail: String,
    endpoint: String,
    stage: String,
    lastFrame: String,
): String {
    return buildList {
        add(headline)
        detail.takeIf { it.isNotBlank() && it != headline }?.let { add(it) }
        endpoint.takeIf { it.isNotBlank() }?.let { add("端点：$it") }
        add("阶段：$stage")
        add("最近帧：$lastFrame")
    }.joinToString("\n")
}

// ===== state 变换纯函数（下沉自 ConnectionPresentationCoordinator） =====

/** 运行时重置执行（纯呈现，下沉自 ConnectionPresentationCoordinator）。 */
internal fun applyRuntimeReset(
    runtimeState: ConnectionResetRuntimeState,
    sink: ConnectionPresentationSink,
) {
    if (runtimeState.clearQuickActionGroups || runtimeState.clearMapGrid) {
        sink.resetRoomCaches()
    }
    if (runtimeState.clearSkillSummaries) {
        sink.clearSkillSummaries()
    }
    if (runtimeState.clearUnhandledPacketTypes) {
        sink.clearUnhandledPacketTypes()
    }
}

/** 重连覆盖层开关（retry/leave/begin 三处共用）。 */
internal fun ShellUiState.reconnectOverlayState(
    overlay: Boolean,
    timedOut: Boolean,
): ShellUiState = copy(reconnectOverlay = overlay, reconnectTimedOut = timedOut)

/** fresh 连接的全量 UI 重置（startConnection 非 resume 分支）。 */
internal fun ShellUiState.freshConnectionState(
    visibleTranscript: List<TranscriptLine>,
    topNoticeLines: List<String>,
): ShellUiState =
    copy(
        appScene = ShellScene.LOGIN,
        roomTitle = "",
        roomZone = "",
        exits = emptyList(),
        specialExits = emptyList(),
        gridExits = emptyList(),
        npcs = emptyList(),
        items = emptyList(),
        inventoryItems = emptyList(),
        statusSnapshot = null,
        latestPrompt = "",
        dialog = null,
        sheet = null,
        commandPalette = null,
        exitOverlay = null,
        commandInput = "",
        session =
            ShellSessionReducer.reduce(
                ShellSessionState(),
                ShellEvent.ConnectRequested,
            ),
        visibleTranscript = visibleTranscript,
        topNoticeLines = topNoticeLines,
        loginErrorMessage = "",
    )

/** 手动断开（无活动连接时）的 UI 重置。 */
internal fun ShellUiState.manualDisconnectState(): ShellUiState =
    copy(
        appScene = ShellScene.LOGIN,
        dialog = null,
        sheet = null,
        commandPalette = null,
        exitOverlay = null,
        reconnectOverlay = false,
        reconnectTimedOut = false,
        inventoryItems = emptyList(),
    )

/** resume 连接成功的 UI 呈现（覆盖层收起 + 顶部信息细条）。 */
internal fun ShellUiState.resumeConnectedState(): ShellUiState =
    applyConnectedPresentation(preserveSurfaces = true)
        .copy(
            reconnectOverlay = false,
            reconnectTimedOut = false,
            floatingMessages =
                floatingMessages.withFloatingMessage(
                    FloatingMessageState(
                        id = nextFloatingMessageId(),
                        text = "已重新连接",
                        level = "info",
                        ttlMs = 2_800L.coerceFloatingMessageTtl(),
                        dedupeKey = "connection_resumed",
                    ),
                ),
        )

/** 登录等待超时终态（R-19：世界加载阶段留在加载页）。 */
internal fun ShellUiState.loginWaitTimeoutState(
    staysOnWorldLoading: Boolean,
    diagnosticMessage: String,
): ShellUiState {
    return if (staysOnWorldLoading) {
        applyConnectionResetUi(
            ConnectionResetUiState(
                appScene = ShellScene.WORLD_LOADING,
                dialog = null,
                sheet = null,
                commandPalette = null,
                exitOverlay = null,
            ),
        )
    } else {
        applyConnectionResetUi(ConnectionResetUiState(appScene = ShellScene.LOGIN))
            .copy(loginErrorMessage = diagnosticMessage)
    }
}

/** fresh 连接的瞬态重置（onPhaseConnecting 非 resume 分支）。 */

internal fun ShellUiState.loginBootstrapCompleteState(): ShellUiState =
    copy(
        appScene = ShellScene.MAIN_SHELL,
        loginErrorMessage = "",
    )

/** resume 连接的 UI 重置（startConnection resume 分支）。 */
internal fun ShellUiState.resumeConnectionState(): ShellUiState =
    copy(
        latestPrompt = "",
        commandInput = "",
        loginErrorMessage = "",
        session = ShellSessionReducer.reduce(session, ShellEvent.ResumeRequested),
    )

/** 登录等待超时详情（R-19 分支文案）。 */
internal fun loginWaitTimeoutDetail(message: String): String =
    if (message == GameConnectionRepository.LOGIN_RESULT_TIMEOUT_MESSAGE) {
        "已收到登录提示并发送登录信息，但未进入世界"
    } else {
        "WebSocket 已打开，但未收到登录提示"
    }

/** 重连覆盖层中再次失败的 UI 呈现。 */
internal fun ShellUiState.reconnectOverlayErrorState(): ShellUiState =
    copy(reconnectTimedOut = true)

/** 登录等待超时 guard（REGISTER 或非等待相位不呈现）。 */
internal fun shouldFailLoginWait(
    appScene: ShellScene,
    phase: ShellPhase,
    waitingPhases: Set<ShellPhase>,
): Boolean = appScene != ShellScene.REGISTER && phase in waitingPhases

/** 连接前瞬态重置（connect 入口）。 */

internal fun disconnectFlowDecision(
    reason: String,
    isManualRequested: Boolean,
    appScene: ShellScene,
): DisconnectFlow {
    if (isManualRequested && reason == MANUAL_DISCONNECT_REASON) {
        return DisconnectFlow.MANUAL_RESET
    }
    if (appScene != ShellScene.LOGIN && reason != MANUAL_DISCONNECT_REASON) {
        return DisconnectFlow.RECONNECT_OVERLAY
    }
    // 方案 R-20（S14）：主壳前台断线不丢场景——保留主壳，显示重连覆盖层并自动重连
    if (appScene == ShellScene.MAIN_SHELL && reason != MANUAL_DISCONNECT_REASON) {
        return DisconnectFlow.RECONNECT_OVERLAY
    }
    return DisconnectFlow.RESET
}

/** 连接端点行文案。 */
internal fun connectionEndpointLine(
    resumeSession: Boolean,
    endpoint: GatewayEndpoint,
): String =
    if (resumeSession) {
        "[恢复] 正在恢复到 ${endpoint.toWebSocketUrl()}"
    } else {
        "[连接] 正在连接到 ${endpoint.toWebSocketUrl()}"
    }

/** owner 策略性断连（登录超时终态单归 LoginWaitTimedOut 分支，R-19）。 */
internal fun isOwnerPolicyDisconnect(reason: String): Boolean =
    reason == GameConnectionRepository.LOGIN_PROMPT_TIMEOUT_MESSAGE ||
        reason == GameConnectionRepository.LOGIN_RESULT_TIMEOUT_MESSAGE
