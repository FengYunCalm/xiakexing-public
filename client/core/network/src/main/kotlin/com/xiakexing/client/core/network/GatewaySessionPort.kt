package com.xiakexing.client.core.network

import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.StateFlow

/** 连接域端口接口（原 feature/shell 的 ShellGatewayClient 下沉重命名）。 */
interface GatewaySessionPort {
    val connectionState: StateFlow<ConnectionState>
    val messages: Flow<GatewayMessage>

    /** Stable identity of the currently opened transport socket, or 0 when absent. */
    fun connectionId(): Long

    fun hasActiveConnection(): Boolean

    fun droppedFrameCount(): Long

    /** 背压降级中被丢弃的协议帧（结构帧）数；可观测事件，不静默。 */
    fun droppedProtocolFrameCount(): Long

    fun connect(endpoint: GatewayEndpoint)

    fun sendLine(text: String): Boolean

    /** Sends one typed command envelope; raw line fallback is intentionally unavailable. */
    fun sendCommand(
        commandRequestId: String,
        payload: String,
    ): Boolean

    fun disconnect(reason: String = "client disconnect")
}

class MudGatewayClientAdapter(
    internal val delegate: MudGatewayClient,
) : GatewaySessionPort {
    override val connectionState: StateFlow<ConnectionState>
        get() = delegate.connectionState

    override val messages: Flow<GatewayMessage>
        get() = delegate.messages

    override fun connectionId(): Long = delegate.connectionId()

    override fun hasActiveConnection(): Boolean = delegate.hasActiveConnection()

    override fun droppedFrameCount(): Long = delegate.droppedFrameCount()

    override fun droppedProtocolFrameCount(): Long = delegate.droppedProtocolFrameCount()

    override fun connect(endpoint: GatewayEndpoint) = delegate.connect(endpoint)

    override fun sendLine(text: String): Boolean = delegate.sendLine(text)

    override fun sendCommand(
        commandRequestId: String,
        payload: String,
    ): Boolean = delegate.sendCommand(commandRequestId, payload)

    override fun disconnect(reason: String) = delegate.disconnect(reason)
}

/** One transport message is bound to the socket that received it. */
data class GatewayMessage(
    val text: String,
    val connectionId: Long,
)

/** One command identity is bound to the current connection generation. */
data class CommandRequestIdentity(
    val id: String,
    val generation: Long,
)
