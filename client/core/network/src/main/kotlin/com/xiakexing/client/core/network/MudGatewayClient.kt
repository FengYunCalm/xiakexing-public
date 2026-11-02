package com.xiakexing.client.core.network

import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.receiveAsFlow
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.Response
import okhttp3.WebSocket
import okhttp3.WebSocketListener
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicLong

class MudGatewayClient private constructor(
    private val okHttpClient: OkHttpClient,
) {
    constructor() : this(
        OkHttpClient.Builder()
            .pingInterval(WEBSOCKET_PING_INTERVAL_SECONDS, TimeUnit.SECONDS)
            .build(),
    )

    private val _connectionState = MutableStateFlow<ConnectionState>(ConnectionState.Idle)
    val connectionState: StateFlow<ConnectionState> = _connectionState.asStateFlow()

    private val messageChannel = Channel<GatewayMessage>(capacity = MAX_PENDING_MESSAGE_COUNT)
    val messages: Flow<GatewayMessage> = messageChannel.receiveAsFlow()

    // 降级丢弃计数：channel 满载时被丢弃的积压帧数。
    // 用于诊断——若用户仍感“卡”，该值持续增长说明 Main 线程消费仍跟不上，
    // 而非连接/网络问题。
    @Volatile
    private var droppedFrameCount: Long = 0L

    // 结构帧丢弃计数（含 XK 帧标记的协议帧，标准 \u001bXK 与括号 \u001b[XK 两种形态）。
    // 丢弃属可观测事件（见 onMessage 降级路径），与普通文本降级分开计数。
    @Volatile
    private var droppedProtocolFrameCount: Long = 0L

    fun droppedFrameCount(): Long = droppedFrameCount

    /** 结构帧（含 XK 帧标记）在背压降级中被丢弃的次数。 */
    fun droppedProtocolFrameCount(): Long = droppedProtocolFrameCount

    @Volatile
    private var webSocket: WebSocket? = null

    @Volatile
    private var activeConnectionId: Long = 0L

    fun connectionId(): Long = activeConnectionId

    fun hasActiveConnection(): Boolean = webSocket != null

    fun connect(endpoint: GatewayEndpoint) {
        // Replacing a socket is one connect transition, not a transient public
        // disconnect. Publishing Disconnected here races the repository actor with
        // the new Connecting state and lets an old callback schedule a second retry.
        val previousSocket = webSocket
        clearBufferedMessages()
        webSocket = null
        activeConnectionId = 0L
        previousSocket?.close(1000, "replaced")

        val connectionId = NEXT_CONNECTION_ID.incrementAndGet()
        activeConnectionId = connectionId
        _connectionState.value = ConnectionState.Connecting(endpoint)
        val request = Request.Builder().url(endpoint.toWebSocketUrl()).build()
        webSocket = okHttpClient.newWebSocket(request, listener(endpoint, connectionId))
    }

    fun sendLine(text: String): Boolean {
        val socket = webSocket ?: return false
        val payload = if (text.endsWith("\n")) text else "$text\n"
        return socket.send(payload)
    }

    /** Sends the only supported gameplay-command wire shape. */
    fun sendCommand(
        commandRequestId: String,
        payload: String,
    ): Boolean {
        val envelope = buildCommandEnvelopeOrNull(commandRequestId, payload) ?: return false
        val socket = webSocket ?: return false
        return socket.send(envelope)
    }

    fun disconnect(reason: String = "client disconnect") {
        val socket = webSocket
        clearBufferedMessages()
        webSocket = null
        activeConnectionId = 0L
        socket?.close(1000, reason)
        if (_connectionState.value !is ConnectionState.Disconnected) {
            _connectionState.value = ConnectionState.Disconnected(reason)
        }
    }

    /** 协议帧被弹掉后使连接失效（不保留半坏连接）。 */
    private fun failConnectionForProtocolDrop() {
        clearBufferedMessages()
        val socket = webSocket
        webSocket = null
        activeConnectionId = 0L
        socket?.close(PROTOCOL_DROP_CLOSE_CODE, "protocol frame dropped under backpressure")
        _connectionState.value =
            ConnectionState.ProtocolBackpressureFailure(
                droppedProtocolFrames = droppedProtocolFrameCount.toInt(),
            )
    }

    /** Legacy test hook; production always binds the listener to a fresh socket id. */
    @Suppress("unused")
    private fun listener(endpoint: GatewayEndpoint): WebSocketListener =
        listener(endpoint, activeConnectionId)

    private fun listener(
        endpoint: GatewayEndpoint,
        connectionId: Long,
    ): WebSocketListener {
        return object : WebSocketListener() {
            override fun onOpen(
                webSocket: WebSocket,
                response: Response,
            ) {
                if (!webSocket.isCurrentSocket()) {
                    webSocket.close(1000, "stale connection")
                    return
                }
                _connectionState.value = ConnectionState.Connected(endpoint)
            }

            override fun onMessage(
                webSocket: WebSocket,
                text: String,
            ) {
                if (!webSocket.isCurrentSocket()) {
                    return
                }
                if (messageChannel.trySend(GatewayMessage(text, connectionId)).isFailure) {
                    // 背压降级：channel 满载时丢弃最旧积压帧。
                    // 普通文本帧允许丢旧保新；结构帧被弹掉 = 连接级数据完整性失败
                    // （丢一帧 BACH/REAL 即丢权威态），关闭当前 socket 并置
                    // ProtocolBackpressureFailure，由 repository 发起受控重连；
                    // 失败后不再向该 socket 提交后续帧（socket 已失效，sendLine 返回 false）。
                    val evicted = messageChannel.tryReceive().getOrNull()
                    messageChannel.trySend(GatewayMessage(text, connectionId))
                    droppedFrameCount += 1
                    if (evicted != null && isProtocolFrame(evicted.text)) {
                        droppedProtocolFrameCount += 1
                        failConnectionForProtocolDrop()
                    }
                }
            }

            override fun onClosing(
                webSocket: WebSocket,
                code: Int,
                reason: String,
            ) {
                webSocket.close(code, reason)
            }

            override fun onClosed(
                webSocket: WebSocket,
                code: Int,
                reason: String,
            ) {
                if (!webSocket.isCurrentSocket()) {
                    return
                }
                clearBufferedMessages()
                this@MudGatewayClient.webSocket = null
                this@MudGatewayClient.activeConnectionId = 0L
                _connectionState.value = ConnectionState.Disconnected(reason.ifBlank { "closed" })
            }

            override fun onFailure(
                webSocket: WebSocket,
                t: Throwable,
                response: Response?,
            ) {
                if (!webSocket.isCurrentSocket()) {
                    return
                }
                clearBufferedMessages()
                this@MudGatewayClient.webSocket = null
                this@MudGatewayClient.activeConnectionId = 0L
                _connectionState.value = ConnectionState.Error(t.message ?: "WebSocket failure")
            }
        }
    }

    private fun clearBufferedMessages() {
        while (messageChannel.tryReceive().getOrNull() != null) {
            // Drop stale queued messages after disconnect or failure.
            // (Overload 路径已改为丢旧保新的背压降级，不再清空缓冲。)
        }
    }

    /**
     * 协议帧判定：含 XK 帧标记的行视为结构帧。
     * 标记集与 core/protocol 的 XkStreamDecoder.prefixes 对齐（防漂移测试钉死）——
     * core/network 不依赖 core/protocol（分层决策），此处是刻意重复。
     */
    private fun isProtocolFrame(text: String): Boolean =
        PROTOCOL_FRAME_MARKS.any { text.contains(it) }

    private fun WebSocket.isCurrentSocket(): Boolean = this === this@MudGatewayClient.webSocket

    companion object {
        /** Creates the Android transport variant that omits SNI but keeps hostname verification. */
        fun createNoSni(): MudGatewayClient = MudGatewayClient(NoSniOkHttpClient.create())

        // 背压缓冲容量。进拥挤房间/战斗洪峰时 Main 线程消费可能瞬时跟不上，
        // 容量越大越不易触发降级丢弃；配合 onMessage 的丢旧保新降级，避免断连。
        private const val MAX_PENDING_MESSAGE_COUNT = 1024
        private val NEXT_CONNECTION_ID = AtomicLong(0L)

        /** 协议完整性失败关闭码（WebSocket 1002 = protocol error）。 */
        private const val PROTOCOL_DROP_CLOSE_CODE = 1002
        private const val WEBSOCKET_PING_INTERVAL_SECONDS = 20L

        /** 帧头标记（与 core/protocol 的 XkStreamDecoder.prefixes 对齐；防漂移测试钉死）。 */
        internal val PROTOCOL_FRAME_MARKS = listOf("\u001bXK", "\u001b[XK")
    }
}

private fun isValidCommandRequestId(value: String): Boolean =
    value.isNotEmpty() && value.length <= 96 && value.all { it.code in 0x21..0x7e }

private fun isValidCommandPayload(value: String): Boolean =
    value.isNotEmpty() &&
        !containsUnpairedSurrogate(value) &&
        value.toByteArray(Charsets.UTF_8).size <= MAX_COMMAND_PAYLOAD_BYTES &&
        value.none { it == '\u0000' || it == '\r' || it == '\n' }

internal fun buildCommandEnvelopeOrNull(
    commandRequestId: String,
    payload: String,
): String? {
    if (!isValidCommandRequestId(commandRequestId) || !isValidCommandPayload(payload)) {
        return null
    }
    val envelope =
        "{\"schema\":\"$COMMAND_INPUT_SCHEMA\",\"command_request_id\":${jsonQuote(
            commandRequestId,
        )},\"payload\":${jsonQuote(payload)}}"
    return envelope.takeIf {
        it.toByteArray(Charsets.UTF_8).size <= MAX_COMMAND_ENVELOPE_BYTES
    }
}

private fun containsUnpairedSurrogate(value: String): Boolean {
    var index = 0
    while (index < value.length) {
        val character = value[index]
        when {
            character in '\uD800'..'\uDBFF' -> {
                if (index + 1 >= value.length || value[index + 1] !in '\uDC00'..'\uDFFF') {
                    return true
                }
                index += 2
            }
            character in '\uDC00'..'\uDFFF' -> return true
            else -> index += 1
        }
    }
    return false
}

private const val COMMAND_INPUT_SCHEMA = "xk.input.v1"
private const val MAX_COMMAND_PAYLOAD_BYTES = 512
private const val MAX_COMMAND_ENVELOPE_BYTES = 2048

private fun jsonQuote(value: String): String {
    val builder = StringBuilder(value.length + 2)
    builder.append('"')
    value.forEach { character ->
        when (character) {
            '"' -> builder.append("\\\"")
            '\\' -> builder.append("\\\\")
            '\b' -> builder.append("\\b")
            '\u000C' -> builder.append("\\f")
            '\n' -> builder.append("\\n")
            '\r' -> builder.append("\\r")
            '\t' -> builder.append("\\t")
            else ->
                if (character.code < 0x20) {
                    builder.append("\\u")
                    builder.append(character.code.toString(16).padStart(4, '0'))
                } else {
                    builder.append(character)
                }
        }
    }
    return builder.append('"').toString()
}
