package com.xiakexing.client.core.network

interface PublicEndpointProvider {
    val publicEndpoint: PublicEndpoint
}

data class PublicEndpoint(
    val host: String,
    val httpsPort: Int = 443,
) {
    /** 回环主机（localhost/127.0.0.1/::1）走明文 ws，非回环强制 wss。
     * 与 GatewayEndpoint 的 require(secure || isLoopbackHost) 约束同语义：
     * 明文只可能出现在本机回环，本地调试（adb reverse 链路）无需 TLS。 */
    val isLoopback: Boolean
        get() = isLoopbackHost(host)

    fun toGatewayEndpoint(path: String = "/ws"): GatewayEndpoint =
        GatewayEndpoint(
            host = host,
            port = httpsPort,
            path = path,
            secure = !isLoopback,
        )

    fun httpsOrigin(): String =
        if (httpsPort == 443) {
            "https://$host"
        } else {
            "https://$host:$httpsPort"
        }
}

/**
 * Test/development fallback only. Production composition must inject a public
 * endpoint from the application configuration.
 */
object PublicEndpointDefaults {
    val origin = PublicEndpoint(host = "127.0.0.1", httpsPort = 8080)
}
