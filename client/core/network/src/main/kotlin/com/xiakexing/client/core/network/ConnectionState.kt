package com.xiakexing.client.core.network

sealed interface ConnectionState {
    data object Idle : ConnectionState

    data class Connecting(val endpoint: GatewayEndpoint) : ConnectionState

    data class Connected(val endpoint: GatewayEndpoint) : ConnectionState

    data class Disconnected(val reason: String) : ConnectionState

    data class Error(val message: String) : ConnectionState

    /** 协议帧背压溢出：权威帧被丢弃 = 连接级数据完整性失败。 */
    data class ProtocolBackpressureFailure(
        val droppedProtocolFrames: Int,
        val message: String = "protocol frame dropped under backpressure",
    ) : ConnectionState
}
