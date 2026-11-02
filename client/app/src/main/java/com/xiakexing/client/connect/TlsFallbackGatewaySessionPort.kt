package com.xiakexing.client.connect

import com.xiakexing.client.core.network.ConnectionState
import com.xiakexing.client.core.network.GatewayEndpoint
import com.xiakexing.client.core.network.GatewayMessage
import com.xiakexing.client.core.network.GatewaySessionPort
import com.xiakexing.client.core.network.MudGatewayClient
import com.xiakexing.client.core.network.MudGatewayClientAdapter
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.collect
import kotlinx.coroutines.flow.receiveAsFlow
import kotlinx.coroutines.launch

/**
 * Uses the endpoint's known-compatible no-SNI TLS path first and falls back to
 * the regular Android TLS path if a network requires SNI.
 *
 * Both paths keep the same trusted DNS WSS endpoint and XK text-frame protocol.
 */
class TlsFallbackGatewaySessionPort(
    private val scope: CoroutineScope,
) : GatewaySessionPort {
    private val noSniPort =
        MudGatewayClientAdapter(
            MudGatewayClient.createNoSni(),
        )
    private val standardPort = MudGatewayClientAdapter(MudGatewayClient())
    private val lock = Any()
    private val messageChannel = Channel<GatewayMessage>(MAX_PENDING_MESSAGE_COUNT)
    private val _connectionState = MutableStateFlow<ConnectionState>(ConnectionState.Idle)

    @Volatile
    private var activePort: GatewaySessionPort = noSniPort

    @Volatile
    private var currentEndpoint: GatewayEndpoint? = null

    @Volatile
    private var standardPreferred = false

    @Volatile
    private var noSniConnected = false

    private var stateJob: Job? = null
    private var framesJob: Job? = null

    @Volatile
    private var droppedFrameCount = 0L

    @Volatile
    private var droppedProtocolFrameCount = 0L

    override val connectionState: StateFlow<ConnectionState> = _connectionState.asStateFlow()
    override val messages: Flow<GatewayMessage> = messageChannel.receiveAsFlow()

    override fun connectionId(): Long = activePort.connectionId()

    init {
        activate(noSniPort)
    }

    override fun hasActiveConnection(): Boolean = activePort.hasActiveConnection()

    override fun droppedFrameCount(): Long = droppedFrameCount + activePort.droppedFrameCount()

    override fun droppedProtocolFrameCount(): Long =
        droppedProtocolFrameCount + activePort.droppedProtocolFrameCount()

    override fun connect(endpoint: GatewayEndpoint) {
        currentEndpoint = endpoint
        noSniConnected = false
        clearBufferedMessages()
        val target =
            if (endpoint.secure && !standardPreferred) {
                noSniPort
            } else {
                standardPort
            }
        activate(target)
        target.connect(endpoint)
    }

    override fun sendLine(text: String): Boolean = activePort.sendLine(text)

    override fun sendCommand(
        commandRequestId: String,
        payload: String,
    ): Boolean = activePort.sendCommand(commandRequestId, payload)

    override fun disconnect(reason: String) {
        activePort.disconnect(reason)
    }

    private fun activate(target: GatewaySessionPort) {
        synchronized(lock) {
            if (activePort === target && stateJob != null && framesJob != null) {
                return
            }
            stateJob?.cancel()
            framesJob?.cancel()
            if (activePort !== target) {
                activePort.disconnect("transport switched")
                clearBufferedMessages()
                activePort = target
            }
            stateJob =
                scope.launch {
                    target.connectionState.collect { state ->
                        onTransportState(target, state)
                    }
                }
            framesJob =
                scope.launch {
                    target.messages.collect { message ->
                        if (target === activePort) {
                            forwardMessage(message)
                        }
                    }
                }
        }
    }

    private fun onTransportState(
        source: GatewaySessionPort,
        state: ConnectionState,
    ) {
        if (source !== activePort) {
            return
        }
        if (source === noSniPort && state is ConnectionState.Connected) {
            noSniConnected = true
        }
        if (source === noSniPort && state is ConnectionState.Error) {
            val endpoint = currentEndpoint
            if (endpoint?.secure == true && !standardPreferred && !noSniConnected) {
                standardPreferred = true
                activate(standardPort)
                standardPort.connect(endpoint)
                return
            }
        }
        _connectionState.value = state
    }

    private fun forwardMessage(message: GatewayMessage) {
        if (messageChannel.trySend(message).isSuccess) {
            return
        }
        val evicted = messageChannel.tryReceive().getOrNull()
        if (evicted != null && isProtocolFrame(evicted.text)) {
            droppedProtocolFrameCount += 1
            activePort.disconnect("protocol frame dropped under backpressure")
            _connectionState.value =
                ConnectionState.ProtocolBackpressureFailure(
                    droppedProtocolFrames = droppedProtocolFrameCount.toInt(),
                )
        }
        droppedFrameCount += 1
        messageChannel.trySend(message)
    }

    private fun clearBufferedMessages() {
        while (messageChannel.tryReceive().getOrNull() != null) {
            // Discard frames from the transport that was just replaced.
        }
    }

    private fun isProtocolFrame(message: String): Boolean =
        PROTOCOL_FRAME_MARKS.any(message::contains)

    private companion object {
        const val MAX_PENDING_MESSAGE_COUNT = 1024
        val PROTOCOL_FRAME_MARKS = listOf("\u001bXK", "\u001b[XK")
    }
}
