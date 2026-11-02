package com.xiakexing.client.core.network

/** 回环主机判定（localhost/127.0.0.1/::1）。
 * 安全不变量「insecure ⇔ loopback」的唯一真源：
 * GatewayEndpoint 的 require 与 PublicEndpoint 的明文派生共用此判定，
 * 禁止在别处复制第二份归一化逻辑。 */
internal fun isLoopbackHost(value: String): Boolean {
    val normalized = value.trim().trim('[', ']').lowercase()
    return normalized == "localhost" || normalized == "127.0.0.1" || normalized == "::1"
}

data class GatewayEndpoint(
    val host: String,
    val port: Int,
    val path: String,
    val secure: Boolean = true,
) {
    init {
        require(secure || isLoopbackHost(host)) {
            "insecure gateway endpoints are restricted to loopback hosts"
        }
    }

    fun toWebSocketUrl(): String {
        val normalizedPath = if (path.startsWith("/")) path else "/$path"
        val scheme = if (secure) "wss" else "ws"
        return "$scheme://$host:$port$normalizedPath"
    }
}
