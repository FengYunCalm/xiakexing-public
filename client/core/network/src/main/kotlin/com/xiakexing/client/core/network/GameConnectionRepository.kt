package com.xiakexing.client.core.network

import com.xiakexing.client.core.protocol.AnsiSanitizer
import com.xiakexing.client.core.protocol.IncomingFrame
import com.xiakexing.client.core.protocol.XkAuthSignal
import com.xiakexing.client.core.protocol.XkPacketParser
import com.xiakexing.client.core.protocol.XkStreamDecoder
import com.xiakexing.client.core.protocol.authSignalOrNull
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicLong

/**
 * 进程级连接 owner（原 feature/shell 连接域下沉端口）。
 *
 * - 全进程唯一持有 GatewaySessionPort（单 socket）；UI 侧经 [port] 只读复用，
 *   发送走 [sendLine]，断连仅由用户登出（[manualDisconnect]）或本类策略触发。
 * - messages 单一权威收集者：[startCollecting] 由宿主调用一次，收到的原始行经
 *   [rawLines] SharedFlow 扇出，并保留最近 [TRANSCRIPT_CAPACITY] 行供重建后回放；
 *   三层 drop（port/repository/shell 观测）各自计数可观测，结构帧不静默丢弃。
 * - 重连/登录等待超时跑在自有 [scope]（不随 Activity/ViewModel 死亡）；
 *   无 UI 订阅者时自动重连退避（headless 自举链）。
 * - resume token 仅内存（服务端 TTL 180s、单次消费，持久化无收益）。
 *
 * 连接生命周期经单一 actor 串行化——公开函数只向 [lifecycleIntents]
 * 通道 trySend 意图，endpoint/resume/attachment/generation/snapshot/reconnect
 * job 状态只在 actor 协程中修改；reconnect timer 到期回发意图，actor 再次
 * 验证 generation/manual/attachment 后才连接，杜绝旧 Job 抢连与复合 RMW。
 */
class GameConnectionRepository(
    private val scope: CoroutineScope,
    val credentialsStore: GameCredentialStore,
    initialPort: GatewaySessionPort = MudGatewayClientAdapter(MudGatewayClient()),
) {
    /** 唯一 socket 端口：字段处即建（进程级 owner 生命周期 = 进程生命周期，无惰性必要）。 */
    @Volatile
    var port: GatewaySessionPort = initialPort
        private set

    /** 测试/特殊环境注入端口（如 RecordingGatewayClient）。 */
    fun overridePortForTest(override: GatewaySessionPort) {
        check(!collectStarted) { "Cannot override port after collecting started" }
        port = override
    }

    // ===== 连接会话状态（owner 态，原 ShellConnectionSessionState 迁入） =====

    data class SessionMeta(
        val lastEndpoint: GatewayEndpoint? = null,
        val resumeToken: String = "",
        val manualDisconnectRequested: Boolean = false,
    )

    private val _session = MutableStateFlow(SessionMeta())
    val session: StateFlow<SessionMeta> = _session.asStateFlow()

    // ===== 原始帧扇出 + transcript 环形缓冲 =====

    data class ReceivedFrame(
        val text: String,
        val connectionId: Long,
        val generation: Long,
    )

    /** One transport message after the owner-held stream decoder has framed it. */
    data class ReceivedFrameBatch(
        val frames: List<IncomingFrame>,
        val connectionId: Long,
        val generation: Long,
    )

    private val _rawLines = MutableSharedFlow<ReceivedFrame>(extraBufferCapacity = 256)
    val rawLines: SharedFlow<ReceivedFrame> = _rawLines

    private val _decodedFrameBatches =
        MutableSharedFlow<ReceivedFrameBatch>(extraBufferCapacity = 256)
    val decodedFrameBatches: SharedFlow<ReceivedFrameBatch> = _decodedFrameBatches

    /** The sole stream decoder for transport ingress; UI receives its typed batches. */
    private val ingressDecoder = XkStreamDecoder()
    private var ingressDecoderConnectionId = 0L

    // repository 层降级计数：_rawLines 扇出失败（慢消费者/缓冲满）次数。
    // port 层降级（MudGatewayClient.droppedFrameCount）与此独立——三层计数
    // （port/repository/shell 观测）各自可观测，结构帧不静默丢弃。
    private var repositoryDroppedFrameCount: Long = 0L

    // 三层 drop 观测快照：port 层计数（含结构帧）+ repository 层计数，随帧消费更新。
    // shell 侧 collect 此流喂给诊断 coordinator（不轮询，避免虚拟时间循环）。
    private val _dropCounters = MutableStateFlow(DropCounters(0L, 0L, 0L))
    val dropCounters: StateFlow<DropCounters> = _dropCounters.asStateFlow()

    data class DropCounters(
        val portDropped: Long,
        val portProtocolDropped: Long,
        val repositoryDropped: Long,
    )

    private fun refreshDropCounters() {
        val next =
            DropCounters(
                portDropped = port.droppedFrameCount(),
                portProtocolDropped = port.droppedProtocolFrameCount(),
                repositoryDropped = repositoryDroppedFrameCount,
            )
        if (next != _dropCounters.value) {
            _dropCounters.value = next
        }
    }

    private val transcriptBuffer = ArrayDeque<String>()
    private val transcriptLock = Any()

    fun recentTranscriptLines(): List<String> =
        synchronized(transcriptLock) {
            transcriptBuffer.toList()
        }

    /** 测试直写环形缓冲（绕过 socket 收集）。 */
    fun recentTranscriptLinesForTest(line: String) = recordRawLine(line)

    private fun recordRawLine(line: String) =
        synchronized(transcriptLock) {
            if (transcriptBuffer.size >= TRANSCRIPT_CAPACITY) {
                transcriptBuffer.removeFirst()
            }
            transcriptBuffer.addLast(line)
        }

    private fun classifyLoginPhase(frame: IncomingFrame): LifecycleIntent? {
        val signal = (frame as? IncomingFrame.Packet)?.authSignalOrNull()
        return when (signal) {
            XkAuthSignal.LOGIN_PROMPT -> LifecycleIntent.LoginPromptSeen
            XkAuthSignal.BOOTSTRAP_COMPLETE -> LifecycleIntent.BootstrapCompleted
            null -> null
        }
    }

    private fun decodeIngressChunk(raw: String): List<IncomingFrame> {
        return ingressDecoder.feed(raw).mapNotNull { segment ->
            when (segment) {
                is XkStreamDecoder.Segment.RawText ->
                    IncomingFrame.PlainText(AnsiSanitizer.sanitize(segment.text))
                is XkStreamDecoder.Segment.XkPacket -> XkPacketParser.parse(segment.text)
                is XkStreamDecoder.Segment.InvalidPacket ->
                    IncomingFrame.InvalidPacket(rawType = "stream", reason = segment.reason)
            }
        }
    }

    // ===== 连接相位事件（窄口：UI 侧 reducer 消费） =====

    sealed interface PhaseEvent {
        data class Connecting(val endpoint: GatewayEndpoint, val resuming: Boolean) : PhaseEvent

        data class Connected(val endpoint: GatewayEndpoint, val resuming: Boolean) : PhaseEvent

        data class Disconnected(val reason: String, val manual: Boolean) : PhaseEvent

        data class Failed(val message: String) : PhaseEvent

        /** 登录引导完成（进入世界）。 */
        data object LoginBootstrapCompleted : PhaseEvent

        /** 登录等待超时。 */
        data class LoginWaitTimedOut(val message: String) : PhaseEvent

        /** 服务端下发 resume token。 */
        data class ResumeTokenIssued(val token: String) : PhaseEvent

        /** 登录提示到达，需要发送凭据（resume 优先、失败回退账密）。 */
        data object LoginPromptReady : PhaseEvent
    }

    private val _phaseEvents = MutableSharedFlow<PhaseEvent>(extraBufferCapacity = 64)
    val phaseEvents: SharedFlow<PhaseEvent> = _phaseEvents

    // ===== 生命周期意图通道（唯一修改入口） =====

    private sealed interface LifecycleIntent {
        data class Connect(val endpoint: GatewayEndpoint, val resume: Boolean) : LifecycleIntent

        data object UiAttached : LifecycleIntent

        data object UiDetached : LifecycleIntent

        data class TransportStateChanged(val state: ConnectionState) : LifecycleIntent

        data object BootstrapCompleted : LifecycleIntent

        data class RoomSnapshotCommitted(val generation: Long?) : LifecycleIntent

        data class BootstrapSnapshotTimeout(val generation: Long) : LifecycleIntent

        data object ManualDisconnect : LifecycleIntent

        data object ClearManualDisconnectRequest : LifecycleIntent

        data object ResyncRequested : LifecycleIntent

        data object LoginPromptSeen : LifecycleIntent

        data object LoginResultPending : LifecycleIntent

        data object LoginRegistrationProgress : LifecycleIntent

        data class ReconnectTimerFired(
            val endpoint: GatewayEndpoint,
            val attempt: Int,
            val generation: Long,
        ) : LifecycleIntent

        data class StrategyDisconnect(
            val reason: String,
            val expectedGeneration: Long? = null,
            val timedOut: Boolean = false,
        ) : LifecycleIntent

        data class ResumeTokenStored(val token: String) : LifecycleIntent

        data object ProtocolBackpressureFailure : LifecycleIntent
    }

    private val lifecycleIntents = Channel<LifecycleIntent>(Channel.UNLIMITED)
    private var lifecycleActorJob: Job? = null

    /** 公开入口只 trySend 意图；返回是否投递成功（通道 UNLIMITED 恒真）。 */
    private fun sendIntent(intent: LifecycleIntent): Boolean =
        lifecycleIntents.trySend(intent).isSuccess

    // ===== 内部状态（仅 actor 协程内修改） =====

    private var connectionAttemptSerial: Long = 0L
    private var loginPromptTimeoutJob: Job? = null
    private var loginResultTimeoutJob: Job? = null
    private var bootstrapSnapshotTimeoutJob: Job? = null
    private var stateCollectJob: Job? = null
    private var framesCollectJob: Job? = null
    private var headlessReconnectJob: Job? = null
    private var headlessReconnectAttempts: Int = 0
    private var uiAttachmentCount = 0

    /** headless 决策单一 owner（无 UI attachment）。 */
    private val isHeadless: Boolean get() = uiAttachmentCount == 0

    /** 断线/溢出后是否需要 attach 重放（非手动 + headless）。 */
    private fun requiresResyncOnDrop(manual: Boolean) = !manual && isHeadless

    private var pendingResumeAttempt = false
    private var bootstrapObserved = false
    private var roomSnapshotObserved = false

    @Volatile
    private var collectStarted = false

    @Volatile
    private var activeTransportConnectionId = 0L

    /** 连接生命周期快照（UI attach 先读快照再订阅事件）。 */
    data class ConnectionLifecycleSnapshot(
        val generation: Long,
        val endpoint: GatewayEndpoint?,
        val loginStage: String,
        val bootstrapCompleted: Boolean,
        val requiresAuthoritativeResync: Boolean,
        val resyncInFlight: Boolean,
    )

    private val _connectionLifecycle =
        MutableStateFlow(
            ConnectionLifecycleSnapshot(
                generation = 0L,
                endpoint = null,
                loginStage = "IDLE",
                bootstrapCompleted = false,
                requiresAuthoritativeResync = false,
                resyncInFlight = false,
            ),
        )
    val connectionLifecycle: StateFlow<ConnectionLifecycleSnapshot> = _connectionLifecycle.asStateFlow()

    /** /headless 断线重连完成 bootstrap 后，UI attach 请求权威重同步。
     * 仅当 bootstrap 已完成且存在未消费的 resync 需求（非手动断线）时执行；
     * 执行后清 required，resyncInFlight 防止执行中重复触发。 */
    fun requestAuthoritativeResync() {
        sendIntent(LifecycleIntent.ResyncRequested)
    }

    fun droppedFrameCount(): Long = port.droppedFrameCount()

    private val commandRequestSerial = AtomicLong(0L)

    @Volatile
    private var latestCommandRequest: CommandRequestIdentity? = null

    /** Immutable requests are published before the transport write. */
    private val pendingCommandRequests = ConcurrentHashMap<String, CommandRequestIdentity>()

    /** Allocates a request identity before a command is written to the transport. */
    fun reserveCommandRequest(): CommandRequestIdentity {
        val generation = connectionLifecycle.value.generation
        val serial = commandRequestSerial.incrementAndGet()
        return CommandRequestIdentity(
            id = "g$generation-r$serial",
            generation = generation,
        )
    }

    /** Refuses a request prepared for an older connection generation. */
    fun sendCommand(
        request: CommandRequestIdentity,
        payload: String,
    ): Boolean {
        if (request.generation != connectionLifecycle.value.generation) {
            return false
        }
        pendingCommandRequests[request.id] = request
        latestCommandRequest = request
        val sent = port.sendCommand(request.id, payload)
        if (!sent) {
            pendingCommandRequests.remove(request.id, request)
            if (latestCommandRequest == request) {
                latestCommandRequest = null
            }
        }
        return sent
    }

    /** Exact correlation gate for a pending interaction capture. */
    fun acceptsCommandResponse(commandResponseId: String): Boolean {
        if (commandResponseId.isBlank()) {
            return true
        }
        val latest = latestCommandRequest ?: return false
        return latest.generation == connectionLifecycle.value.generation &&
            latest.id == commandResponseId
    }

    /**
     * Drops only responses from an older connection generation at ingress. A
     * same-generation response may be a late authoritative fact; individual
     * capture owners decide whether it may mutate their pending surface.
     */
    fun acceptsCommandResponseFromCurrentGeneration(commandResponseId: String): Boolean {
        if (commandResponseId.isBlank()) {
            return true
        }
        val separator = commandResponseId.indexOf("-r")
        if (separator <= 1 || separator == commandResponseId.lastIndex) {
            return false
        }
        val generation =
            commandResponseId.substring(1, separator).toLongOrNull()
                ?: return false
        val serial =
            commandResponseId.substring(separator + 2).toLongOrNull()
                ?: return false
        if (commandResponseId[0] != 'g' || serial <= 0L) {
            return false
        }
        return generation == connectionLifecycle.value.generation
    }

    // ===== 收集启动（宿主 Service onCreate / VM 兜底调用一次） =====

    fun startCollecting() {
        if (collectStarted) {
            return
        }
        collectStarted = true
        // 单 actor job 消费全部生命周期意图。
        lifecycleActorJob =
            scope.launch {
                for (intent in lifecycleIntents) {
                    handleLifecycleIntent(intent)
                }
            }
        stateCollectJob =
            scope.launch {
                port.connectionState.collect { state ->
                    sendIntent(LifecycleIntent.TransportStateChanged(state))
                }
            }
        framesCollectJob =
            scope.launch {
                port.messages.collect { message ->
                    // Transport owns socket identity; reject callbacks queued by an older socket
                    // before stamping them with the current lifecycle generation.
                    if (message.connectionId != activeTransportConnectionId) {
                        return@collect
                    }
                    if (message.connectionId != ingressDecoderConnectionId) {
                        ingressDecoder.reset()
                        ingressDecoderConnectionId = message.connectionId
                    }
                    val decodedFrames = decodeIngressChunk(message.text)
                    val received =
                        ReceivedFrame(
                            text = message.text,
                            connectionId = message.connectionId,
                            generation = connectionLifecycle.value.generation,
                        )
                    // transcript 权威环形缓冲：无论有无 UI 订阅者都记录（保活回放用）。
                    recordRawLine(message.text)

                    // 先完整校验本次解码结果，再一次性发布；非法/失步帧不能降级成正文，
                    // 同一 transport chunk 的前半段也不能先污染 reducer。
                    if (decodedFrames.any { it is IncomingFrame.InvalidPacket }) {
                        sendIntent(LifecycleIntent.ProtocolBackpressureFailure)
                        return@collect
                    }
                    val generation = connectionLifecycle.value.generation
                    if (decodedFrames.isNotEmpty()) {
                        val batch =
                            ReceivedFrameBatch(
                                frames = decodedFrames,
                                connectionId = message.connectionId,
                                generation = generation,
                            )
                        // UI 是该 typed ingress 的消费者；拒收任何一批权威帧都使当前代失效。
                        if (!_decodedFrameBatches.tryEmit(batch)) {
                            sendIntent(LifecycleIntent.ProtocolBackpressureFailure)
                            return@collect
                        }
                        decodedFrames.forEach { frame ->
                            classifyLoginPhase(frame)?.let(::sendIntent)
                        }
                    }

                    // rawLines 仅是诊断/回放兼容出口；它的观察者变慢不能改变
                    // 已由 owner 解码并发布的权威 typed ingress。
                    if (!_rawLines.tryEmit(received)) {
                        repositoryDroppedFrameCount += 1
                    }
                    refreshDropCounters()
                }
            }
    }

    /**
     * 协议帧背压溢出 = 权威投影已丢失，会话不可信。
     * 清 resume token 并按下最后 endpoint 发起 fresh 重连；
     * UI 经 [PhaseEvent.Connecting(resuming=true)] 保持重连态，
     * 重连 bootstrap 后服务端重新下发 REAL/BACH/STAT/IVTY/QUES 权威快照。
     */
    private fun onProtocolBackpressureFailure() {
        if (_session.value.manualDisconnectRequested) {
            return
        }
        cancelLoginWaitTimeouts()
        bootstrapSnapshotTimeoutJob?.cancel()
        bootstrapSnapshotTimeoutJob = null
        bootstrapObserved = false
        roomSnapshotObserved = false
        connectionAttemptSerial += 1
        _connectionLifecycle.value =
            _connectionLifecycle.value.copy(generation = connectionAttemptSerial)
        _session.value = _session.value.copy(resumeToken = "")
        val endpoint = _session.value.lastEndpoint
        if (endpoint == null) {
            emitPhaseEvent(PhaseEvent.Failed("protocol backpressure overflow"))
            return
        }
        emitPhaseEvent(PhaseEvent.Connecting(endpoint, resuming = false))
        pendingResumeAttempt = false
        _connectionLifecycle.value =
            _connectionLifecycle.value.copy(
                loginStage = "RESYNCING",
                bootstrapCompleted = false,
                /* overflow 非用户意图（manual 恒 false），
                 * headless 场景置 required resync，重连 bootstrap 完成后由
                 * UI attach 消费重放——不允许"计数增加但连接继续可信"。 */
                requiresAuthoritativeResync = requiresResyncOnDrop(manual = false),
            )
        activeTransportConnectionId = 0L
        port.connect(endpoint)
        activeTransportConnectionId = port.connectionId()
    }

    // ===== actor 意图处理（唯一修改生命周期状态处） =====
    private fun handleLifecycleIntent(intent: LifecycleIntent) {
        when (intent) {
            is LifecycleIntent.Connect -> onConnectIntent(intent.endpoint, intent.resume)
            LifecycleIntent.UiAttached -> onUiAttachedIntent()
            LifecycleIntent.UiDetached -> onUiDetachedIntent()
            is LifecycleIntent.TransportStateChanged -> onTransportStateChanged(intent.state)
            LifecycleIntent.BootstrapCompleted -> onBootstrapCompletedIntent()
            is LifecycleIntent.RoomSnapshotCommitted ->
                onRoomSnapshotCommittedIntent(
                    intent.generation,
                )
            is LifecycleIntent.BootstrapSnapshotTimeout ->
                onBootstrapSnapshotTimeout(intent.generation)
            LifecycleIntent.ManualDisconnect -> onManualDisconnectIntent()
            LifecycleIntent.ClearManualDisconnectRequest -> onClearManualDisconnectRequestIntent()
            LifecycleIntent.ResyncRequested -> onResyncRequestedIntent()
            LifecycleIntent.LoginPromptSeen -> onLoginPromptSeenIntent()
            LifecycleIntent.LoginResultPending -> onLoginResultPendingIntent()
            LifecycleIntent.LoginRegistrationProgress -> onLoginRegistrationProgressIntent()
            is LifecycleIntent.ReconnectTimerFired ->
                onReconnectTimerFired(intent.endpoint, intent.attempt, intent.generation)
            is LifecycleIntent.StrategyDisconnect ->
                onStrategyDisconnect(
                    intent.reason,
                    intent.expectedGeneration,
                    intent.timedOut,
                )
            is LifecycleIntent.ResumeTokenStored -> onResumeTokenStored(intent.token)
            LifecycleIntent.ProtocolBackpressureFailure -> onProtocolBackpressureFailure()
        }
    }

    private fun onConnectIntent(
        endpoint: GatewayEndpoint,
        resume: Boolean,
    ) {
        nextConnectionAttempt()
        pendingResumeAttempt = resume
        bootstrapObserved = false
        roomSnapshotObserved = false
        bootstrapSnapshotTimeoutJob?.cancel()
        bootstrapSnapshotTimeoutJob = null
        _connectionLifecycle.value =
            _connectionLifecycle.value.copy(
                bootstrapCompleted = false,
                loginStage = "CONNECTING",
            )
        _session.value =
            if (resume) {
                _session.value.copy(lastEndpoint = endpoint)
            } else {
                SessionMeta(lastEndpoint = endpoint)
            }
        port.connect(endpoint)
        activeTransportConnectionId = port.connectionId()
    }

    private fun onUiAttachedIntent() {
        uiAttachmentCount += 1
        headlessReconnectAttempts = 0
        headlessReconnectJob?.cancel()
        headlessReconnectJob = null
    }

    private fun onUiDetachedIntent() {
        if (uiAttachmentCount > 0) {
            uiAttachmentCount -= 1
        }
        if (isHeadless) {
            maybeScheduleHeadlessReconnect(manual = false)
        }
    }

    private fun onTransportStateChanged(state: ConnectionState) {
        when (state) {
            ConnectionState.Idle -> Unit
            is ConnectionState.Connecting -> onConnecting(state.endpoint)
            is ConnectionState.Connected -> onConnected(state.endpoint)
            is ConnectionState.Disconnected -> onDisconnected(state.reason)
            is ConnectionState.Error -> onFailed(state.message)
            is ConnectionState.ProtocolBackpressureFailure -> onProtocolBackpressureFailure()
        }
    }

    private fun onManualDisconnectIntent() {
        cancelLoginWaitTimeouts()
        bootstrapObserved = false
        roomSnapshotObserved = false
        connectionAttemptSerial += 1
        _session.value =
            _session.value.copy(manualDisconnectRequested = true, resumeToken = "")
        _connectionLifecycle.value =
            _connectionLifecycle.value.copy(
                generation = connectionAttemptSerial,
                resyncInFlight = false,
            )
        activeTransportConnectionId = 0L
        port.disconnect(MANUAL_DISCONNECT_REASON)
    }

    private fun onClearManualDisconnectRequestIntent() {
        _session.value = _session.value.copy(manualDisconnectRequested = false)
    }

    private fun onResyncRequestedIntent() {
        val snapshot = _connectionLifecycle.value
        if (!snapshot.requiresAuthoritativeResync) {
            return
        }
        if (snapshot.resyncInFlight) {
            return
        }
        // Connecting 事件可能先于 Connected 状态到达；此时 lifecycle snapshot
        // 尚未写入 endpoint，但 session 仍保留最近一次可用端点。不能先消费
        // required 标记，否则无端点时会把重同步永久卡在 in-flight。
        val endpoint = snapshot.endpoint ?: _session.value.lastEndpoint
        if (endpoint == null) {
            emitPhaseEvent(PhaseEvent.Failed("authoritative resync has no endpoint"))
            return
        }
        headlessReconnectJob?.cancel()
        headlessReconnectJob = null
        _connectionLifecycle.value =
            snapshot.copy(
                requiresAuthoritativeResync = false,
                resyncInFlight = true,
                bootstrapCompleted = false,
                loginStage = "RESYNCING",
            )
        emitPhaseEvent(PhaseEvent.Connecting(endpoint, resuming = true))
        pendingResumeAttempt = true
        // 受控重连也走统一连接步进（generation 递增 + 超时重置）。
        nextConnectionAttempt()
        port.connect(endpoint)
        activeTransportConnectionId = port.connectionId()
    }

    private fun onLoginPromptSeenIntent() {
        loginPromptTimeoutJob?.cancel()
        loginPromptTimeoutJob = null
        emitPhaseEvent(PhaseEvent.LoginPromptReady)
        // headless 自举：无 UI 订阅者时 owner 直接发送凭据完成认证。
        if (isHeadless) {
            scope.launch { sendLoginCredentialsOnce() }
        }
    }

    private fun onLoginResultPendingIntent() {
        if (connectionAttemptSerial > 0L) {
            scheduleLoginResultTimeout(connectionAttemptSerial)
        }
    }

    private fun onLoginRegistrationProgressIntent() {
        // 注册交互允许用户停留在姓名/性别阶段；登录结果看门狗不能把
        // 尚未完成的人工注册误判为无响应并主动断开 socket。
        loginResultTimeoutJob?.cancel()
        loginResultTimeoutJob = null
    }

    private fun onBootstrapCompletedIntent() {
        cancelLoginWaitTimeouts()
        bootstrapObserved = true
        _connectionLifecycle.value =
            _connectionLifecycle.value.copy(
                loginStage = "WAITING_ROOM_SNAPSHOT",
                bootstrapCompleted = false,
            )
        if (roomSnapshotObserved) {
            completeBootstrap()
            return
        }
        val generation = connectionAttemptSerial
        bootstrapSnapshotTimeoutJob?.cancel()
        bootstrapSnapshotTimeoutJob =
            scope.launch {
                delay(BOOTSTRAP_SNAPSHOT_TIMEOUT_MS)
                sendIntent(LifecycleIntent.BootstrapSnapshotTimeout(generation))
            }
    }

    private fun onRoomSnapshotCommittedIntent(generation: Long?) {
        if (generation != null && generation != connectionLifecycle.value.generation) {
            return
        }
        roomSnapshotObserved = true
        if (bootstrapObserved) {
            completeBootstrap()
        }
    }

    private fun completeBootstrap() {
        bootstrapSnapshotTimeoutJob?.cancel()
        bootstrapSnapshotTimeoutJob = null
        bootstrapObserved = false
        roomSnapshotObserved = false
        emitPhaseEvent(PhaseEvent.LoginBootstrapCompleted)
        _connectionLifecycle.value =
            _connectionLifecycle.value.copy(
                loginStage = "BOOTSTRAPPED",
                bootstrapCompleted = true,
                resyncInFlight = false,
            )
    }

    private fun onBootstrapSnapshotTimeout(generation: Long) {
        if (generation != connectionAttemptSerial ||
            !bootstrapObserved || roomSnapshotObserved ||
            _connectionLifecycle.value.loginStage != "WAITING_ROOM_SNAPSHOT"
        ) {
            return
        }
        bootstrapSnapshotTimeoutJob = null
        onStrategyDisconnect(
            "登录后权威房间快照超时",
            expectedGeneration = generation,
            timedOut = true,
        )
    }

    private fun onResumeTokenStored(token: String) {
        val normalized = token.trim()
        // 断环：ResumeTokenIssued 语义 = 新 token 下发；同值重复存储不重发，
        // 否则经 collect→handler 回写同值会形成有界缓冲自锁（emit→消费→再 emit）。
        if (normalized.isNotBlank() && normalized != _session.value.resumeToken) {
            _session.value = _session.value.copy(resumeToken = normalized)
            emitPhaseEvent(PhaseEvent.ResumeTokenIssued(normalized))
        }
    }

    private fun onReconnectTimerFired(
        endpoint: GatewayEndpoint,
        attempt: Int,
        generation: Long,
    ) {
        // timer 到期回发意图，actor 重新验证 manual/attachment/attempts。
        if (_session.value.manualDisconnectRequested || !isHeadless ||
            port.hasActiveConnection() ||
            endpoint != _session.value.lastEndpoint ||
            generation != connectionAttemptSerial ||
            attempt <= 0 || attempt > HEADLESS_RECONNECT_MAX_ATTEMPTS ||
            attempt != headlessReconnectAttempts
        ) {
            return
        }
        headlessReconnectJob = null
        onConnectIntent(endpoint, resume = true)
    }

    // ===== 连接发起（公开入口 → 意图） =====

    /**
     * 用户显式连接（登录页「连接」按钮）：fresh 语义。
     * UI 侧重置由 shell 侧消费 [PhaseEvent.Connecting(resuming=false)] 完成。
     */
    fun connectFresh(endpoint: GatewayEndpoint) {
        sendIntent(LifecycleIntent.Connect(endpoint, resume = false))
    }

    /** 自动重连（前台恢复/断线重连）：resume 语义，UI 按 Connecting(resuming=true) 免全量重置。 */
    fun connectResume(endpoint: GatewayEndpoint) {
        sendIntent(LifecycleIntent.Connect(endpoint, resume = true))
    }

    /** 用户登出/手动断开：唯一允许 UI 触发的断连；保留 lastEndpoint 供下次快速连接。 */
    fun manualDisconnect() {
        sendIntent(LifecycleIntent.ManualDisconnect)
    }

    /** 壳侧重连覆盖层清除登出标志（用户重新连接时）。 */
    fun clearManualDisconnectRequest() {
        sendIntent(LifecycleIntent.ClearManualDisconnectRequest)
    }

    /** 登录等待超时等策略性断连（非用户意图）。 */
    fun disconnectForStrategy(reason: String) {
        sendIntent(LifecycleIntent.StrategyDisconnect(reason))
    }

    fun sendLine(text: String): Boolean = port.sendLine(text)

    fun hasActiveConnection(): Boolean = port.hasActiveConnection()

    fun lastEndpoint(): GatewayEndpoint? = _session.value.lastEndpoint

    fun resumeToken(): String = _session.value.resumeToken

    fun isManualDisconnectRequested(): Boolean = _session.value.manualDisconnectRequested

    // ===== 凭据发送策略（resume 优先 → 账密回退；in-flight 守卫防并发双发顶号） =====

    private val loginSendInFlight = AtomicBoolean(false)

    /**
     * 发送登录凭据（headless 自举路径）。守卫仅防并发重入；跨帧重发允许
     * （resume token 被拒后服务端重新提示属预期重试场景）。
     */
    suspend fun sendLoginCredentialsOnce(): Boolean {
        if (!loginSendInFlight.compareAndSet(false, true)) return false
        try {
            return sendLoginCredentials()
        } finally {
            loginSendInFlight.set(false)
        }
    }

    private suspend fun sendLoginCredentials(): Boolean {
        val token = _session.value.resumeToken
        if (token.isNotBlank()) {
            if (sendLine(LoginPayloadFormatter.formatResumeLogin(token))) {
                return true
            }
        }
        val credentials =
            try {
                credentialsStore.load()
            } catch (_: CredentialStoreException) {
                // 读取失败只阻止本次自举发送；不能再发起空值 save 覆盖
                // 可能已经由用户/其它 owner 写入的新凭据。
                return false
            }
        if (credentials.username.isBlank()) {
            return false
        }
        return sendLine(
            LoginPayloadFormatter.formatTextLogin(credentials.username, credentials.password),
        )
    }

    // ===== 服务端 resume token 提取（公开入口 → 意图） =====

    fun storeResumeToken(token: String) {
        sendIntent(LifecycleIntent.ResumeTokenStored(token))
    }

    /** 事件广播尽力而为：无订阅者（headless 自举）/缓冲满时丢弃旧事件；返回值显式消费。 */
    private fun emitPhaseEvent(event: PhaseEvent): Boolean {
        return _phaseEvents.tryEmit(event)
    }

    // ===== 内部连接态处理（仅 actor 内调用） =====

    private fun onConnecting(endpoint: GatewayEndpoint) {
        val resuming = isResumeAttempt()
        emitPhaseEvent(PhaseEvent.Connecting(endpoint, resuming))
    }

    private fun isResumeAttempt(): Boolean =
        pendingResumeAttempt.also { pendingResumeAttempt = false }

    private fun onConnected(endpoint: GatewayEndpoint) {
        headlessReconnectAttempts = 0
        val wasResume = _session.value.resumeToken.isNotBlank()
        emitPhaseEvent(PhaseEvent.Connected(endpoint, wasResume))
        scheduleLoginPromptTimeout(connectionAttemptSerial)
        _connectionLifecycle.value =
            _connectionLifecycle.value.copy(
                endpoint = endpoint,
                loginStage = "CONNECTED",
            )
    }

    private fun onDisconnected(reason: String) {
        cancelLoginWaitTimeouts()
        bootstrapObserved = false
        roomSnapshotObserved = false
        val manual = reason == MANUAL_DISCONNECT_REASON || _session.value.manualDisconnectRequested
        emitPhaseEvent(PhaseEvent.Disconnected(reason, manual))
        maybeScheduleHeadlessReconnect(manual)
        _connectionLifecycle.value =
            _connectionLifecycle.value.copy(
                loginStage = "DISCONNECTED",
                resyncInFlight = false,
                /* UI 当前订阅会话内断线由 UI 自驱重连消费实况帧，
                 * 不遗留 resync 需求（headless 场景才需要 attach 重放）。
                 * attachment 计数只在 actor 内维护，subscriber 计数
                 * 不参与所有权决策。 */
                requiresAuthoritativeResync = requiresResyncOnDrop(manual),
            )
    }

    /**
     * headless 自举自动重连（杀进程重建链的“死后复活”）。
     *
     * 仅当 (a) 非用户意图断开、(b) 无 UI attachment（attachment 计数，非
     * subscriber 计数）、(c) 存在可重连端点、(d) 未达上限时，按线性步进退避
     * （2s×attempt）调度重连。timer 到期回发 ReconnectTimerFired 意图，
     * actor 再次验证 manual/attachment/attempts 后才连接。
     * UI 在场后由壳侧重连覆盖层接管；UI 附着即取消本循环。
     */
    private fun maybeScheduleHeadlessReconnect(manual: Boolean) {
        if (manual || _session.value.manualDisconnectRequested) return
        // 健康会话（仍 Connected）不重连：detach 钩子不得拆掉正常连接。
        if (port.hasActiveConnection()) return
        if (!isHeadless) return
        val endpoint = _session.value.lastEndpoint ?: return
        if (headlessReconnectAttempts >= HEADLESS_RECONNECT_MAX_ATTEMPTS) return
        val attempt = ++headlessReconnectAttempts
        val generation = connectionAttemptSerial
        headlessReconnectJob?.cancel()
        headlessReconnectJob =
            scope.launch {
                delay(HEADLESS_RECONNECT_BACKOFF_MS * attempt)
                sendIntent(
                    LifecycleIntent.ReconnectTimerFired(
                        endpoint = endpoint,
                        attempt = attempt,
                        generation = generation,
                    ),
                )
            }
    }

    /** UI 订阅建立后调用：headless 重连循环让位于壳侧重连覆盖层。 */
    fun onUiSubscribed() {
        sendIntent(LifecycleIntent.UiAttached)
    }

    /** UI detach（ViewModel 清理时调用）——attachment 归零后重新评估
     *  headless 重连（断线时订阅在场、随后消失的场景不再长期停在 disconnected）。 */
    fun onUiUnsubscribed() {
        sendIntent(LifecycleIntent.UiDetached)
    }

    /** 测试隔离：取消全部收集/看门狗/重连协程，防 Holder.resetForTest 泄漏（生产不调用）。 */
    fun shutdownForTest() {
        cancelLoginWaitTimeouts()
        lifecycleActorJob?.cancel()
        stateCollectJob?.cancel()
        framesCollectJob?.cancel()
        headlessReconnectJob?.cancel()
    }

    private fun onFailed(message: String) {
        cancelLoginWaitTimeouts()
        bootstrapObserved = false
        roomSnapshotObserved = false
        val manual = _session.value.manualDisconnectRequested
        emitPhaseEvent(PhaseEvent.Failed(message))
        maybeScheduleHeadlessReconnect(manual)
        _connectionLifecycle.value =
            _connectionLifecycle.value.copy(
                loginStage = "ERROR",
                resyncInFlight = false,
                requiresAuthoritativeResync = requiresResyncOnDrop(manual),
            )
    }

    private fun onStrategyDisconnect(
        reason: String,
        expectedGeneration: Long?,
        timedOut: Boolean,
    ) {
        if (expectedGeneration != null && expectedGeneration != connectionAttemptSerial) {
            return
        }
        cancelLoginWaitTimeouts()
        bootstrapObserved = false
        roomSnapshotObserved = false
        if (timedOut) {
            emitPhaseEvent(PhaseEvent.LoginWaitTimedOut(reason))
        }
        connectionAttemptSerial += 1
        _connectionLifecycle.value =
            _connectionLifecycle.value.copy(
                generation = connectionAttemptSerial,
                loginStage = "DISCONNECTED",
                resyncInFlight = false,
            )
        port.disconnect(reason)
    }

    fun markLoginPromptFrameSeen() {
        sendIntent(LifecycleIntent.LoginPromptSeen)
    }

    fun markLoginResultPending() {
        sendIntent(LifecycleIntent.LoginResultPending)
    }

    fun markLoginRegistrationProgress() {
        sendIntent(LifecycleIntent.LoginRegistrationProgress)
    }

    fun markLoginBootstrapCompleted() {
        sendIntent(LifecycleIntent.BootstrapCompleted)
    }

    /** 房间四段权威快照已原子提交后，才允许 bootstrap 进入 Playing。 */
    fun markRoomSnapshotCommitted(generation: Long? = null) {
        sendIntent(LifecycleIntent.RoomSnapshotCommitted(generation))
    }

    // ===== 超时策略（仅 actor 内调度/取消） =====

    private fun nextConnectionAttempt(): Long {
        cancelLoginWaitTimeouts()
        connectionAttemptSerial += 1
        latestCommandRequest = null
        pendingCommandRequests.clear()
        _connectionLifecycle.value =
            _connectionLifecycle.value.copy(generation = connectionAttemptSerial)
        return connectionAttemptSerial
    }

    private fun cancelLoginWaitTimeouts() {
        loginPromptTimeoutJob?.cancel()
        loginPromptTimeoutJob = null
        loginResultTimeoutJob?.cancel()
        loginResultTimeoutJob = null
        bootstrapSnapshotTimeoutJob?.cancel()
        bootstrapSnapshotTimeoutJob = null
    }

    private fun scheduleLoginPromptTimeout(attemptSerial: Long) {
        loginPromptTimeoutJob?.cancel()
        loginPromptTimeoutJob =
            scope.launch {
                delay(LOGIN_PROMPT_TIMEOUT_MS)
                sendIntent(
                    LifecycleIntent.StrategyDisconnect(
                        LOGIN_PROMPT_TIMEOUT_MESSAGE,
                        expectedGeneration = attemptSerial,
                        timedOut = true,
                    ),
                )
            }
    }

    private fun scheduleLoginResultTimeout(attemptSerial: Long) {
        loginResultTimeoutJob?.cancel()
        loginResultTimeoutJob =
            scope.launch {
                delay(LOGIN_RESULT_TIMEOUT_MS)
                sendIntent(
                    LifecycleIntent.StrategyDisconnect(
                        LOGIN_RESULT_TIMEOUT_MESSAGE,
                        expectedGeneration = attemptSerial,
                        timedOut = true,
                    ),
                )
            }
    }

    companion object {
        const val MANUAL_DISCONNECT_REASON = "client disconnect"

        /** transcript 权威环形缓冲容量（杀进程前的最后 N 行原始帧）。 */
        const val TRANSCRIPT_CAPACITY = 400
        const val LOGIN_PROMPT_TIMEOUT_MS = 10_000L
        const val LOGIN_RESULT_TIMEOUT_MS = 15_000L
        const val BOOTSTRAP_SNAPSHOT_TIMEOUT_MS = 10_000L

        // 超时消息文案（中文，与壳侧 UI 文案同源；owner 发射与壳侧呈现共用同一值）。
        const val LOGIN_PROMPT_TIMEOUT_MESSAGE = "服务器登录提示超时，请重试"
        const val LOGIN_RESULT_TIMEOUT_MESSAGE = "进入江湖超时，请重试"

        /** headless 自举重连：最多 3 次，退避 2s×attempt 线性步进（2s/4s/6s）。 */
        internal const val HEADLESS_RECONNECT_MAX_ATTEMPTS = 3
        internal const val HEADLESS_RECONNECT_BACKOFF_MS = 2_000L
    }
}
