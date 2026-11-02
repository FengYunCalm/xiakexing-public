package com.xiakexing.client.core.network

import okhttp3.OkHttpClient
import java.io.InputStream
import java.io.OutputStream
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.Socket
import java.net.SocketAddress
import java.nio.channels.SocketChannel
import javax.net.ssl.HandshakeCompletedListener
import javax.net.ssl.SSLContext
import javax.net.ssl.SSLParameters
import javax.net.ssl.SSLSocket
import javax.net.ssl.SSLSocketFactory
import javax.net.ssl.TrustManagerFactory
import javax.net.ssl.X509TrustManager

/** Creates the native client variant used when the endpoint rejects SNI. */
internal object NoSniOkHttpClient {
    fun create(): OkHttpClient {
        val trustManager = systemTrustManager()
        val sslContext = SSLContext.getInstance("TLS")
        sslContext.init(null, arrayOf(trustManager), null)
        return OkHttpClient.Builder()
            .sslSocketFactory(NoSniSocketFactory(sslContext.socketFactory), trustManager)
            .pingInterval(WEBSOCKET_PING_INTERVAL_SECONDS, java.util.concurrent.TimeUnit.SECONDS)
            .build()
    }

    private fun systemTrustManager(): X509TrustManager {
        val factory = TrustManagerFactory.getInstance(TrustManagerFactory.getDefaultAlgorithm())
        factory.init(null as java.security.KeyStore?)
        return factory.trustManagers.filterIsInstance<X509TrustManager>().single()
    }

    private const val WEBSOCKET_PING_INTERVAL_SECONDS = 20L
}

private class NoSniSocketFactory(
    private val delegate: SSLSocketFactory,
) : SSLSocketFactory() {
    override fun getDefaultCipherSuites(): Array<String> = delegate.defaultCipherSuites

    override fun getSupportedCipherSuites(): Array<String> = delegate.supportedCipherSuites

    override fun createSocket(
        socket: Socket,
        host: String?,
        port: Int,
        autoClose: Boolean,
    ): Socket =
        NoSniSocket(
            delegate.createSocket(socket, null, port, autoClose) as SSLSocket,
        )

    override fun createSocket(
        host: String,
        port: Int,
    ): Socket = createConnectedSocket(InetSocketAddress(host, port))

    override fun createSocket(
        host: String,
        port: Int,
        localHost: InetAddress,
        localPort: Int,
    ): Socket =
        createConnectedSocket(
            remote = InetSocketAddress(host, port),
            local = InetSocketAddress(localHost, localPort),
        )

    override fun createSocket(
        host: InetAddress,
        port: Int,
    ): Socket = createConnectedSocket(InetSocketAddress(host, port))

    override fun createSocket(
        address: InetAddress,
        port: Int,
        localAddress: InetAddress,
        localPort: Int,
    ): Socket =
        createConnectedSocket(
            remote = InetSocketAddress(address, port),
            local = InetSocketAddress(localAddress, localPort),
        )

    private fun createConnectedSocket(
        remote: SocketAddress,
        local: SocketAddress? = null,
    ): Socket {
        val rawSocket = Socket()
        if (local != null) {
            rawSocket.bind(local)
        }
        rawSocket.connect(remote)
        return NoSniSocket(delegate.createSocket(rawSocket, null, remote.port(), true) as SSLSocket)
    }
}

private class NoSniSocket(
    private val delegate: SSLSocket,
) : SSLSocket() {
    override fun getSupportedCipherSuites(): Array<String> = delegate.supportedCipherSuites

    override fun getEnabledCipherSuites(): Array<String> = delegate.enabledCipherSuites

    override fun setEnabledCipherSuites(suites: Array<out String>) {
        delegate.enabledCipherSuites = suites
    }

    override fun getSupportedProtocols(): Array<String> = delegate.supportedProtocols

    override fun getEnabledProtocols(): Array<String> = delegate.enabledProtocols

    override fun setEnabledProtocols(protocols: Array<out String>) {
        delegate.enabledProtocols = protocols
    }

    override fun getSession() = delegate.session

    override fun addHandshakeCompletedListener(listener: HandshakeCompletedListener) {
        delegate.addHandshakeCompletedListener(listener)
    }

    override fun removeHandshakeCompletedListener(listener: HandshakeCompletedListener) {
        delegate.removeHandshakeCompletedListener(listener)
    }

    override fun startHandshake() {
        delegate.startHandshake()
    }

    override fun setUseClientMode(mode: Boolean) {
        delegate.useClientMode = mode
    }

    override fun getUseClientMode(): Boolean = delegate.useClientMode

    override fun setNeedClientAuth(need: Boolean) {
        delegate.needClientAuth = need
    }

    override fun getNeedClientAuth(): Boolean = delegate.needClientAuth

    override fun setWantClientAuth(want: Boolean) {
        delegate.wantClientAuth = want
    }

    override fun getWantClientAuth(): Boolean = delegate.wantClientAuth

    override fun setEnableSessionCreation(flag: Boolean) {
        delegate.enableSessionCreation = flag
    }

    override fun getEnableSessionCreation(): Boolean = delegate.enableSessionCreation

    override fun getSSLParameters(): SSLParameters = delegate.sslParameters

    override fun setSSLParameters(params: SSLParameters) {
        delegate.sslParameters = params
    }

    override fun getInputStream(): InputStream = delegate.inputStream

    override fun getOutputStream(): OutputStream = delegate.outputStream

    override fun connect(endpoint: SocketAddress) {
        delegate.connect(endpoint)
    }

    override fun connect(
        endpoint: SocketAddress,
        timeout: Int,
    ) {
        delegate.connect(endpoint, timeout)
    }

    override fun bind(bindpoint: SocketAddress) {
        delegate.bind(bindpoint)
    }

    override fun close() {
        delegate.close()
    }

    override fun shutdownInput() {
        delegate.shutdownInput()
    }

    override fun shutdownOutput() {
        delegate.shutdownOutput()
    }

    override fun getInetAddress(): InetAddress = delegate.inetAddress

    override fun getLocalAddress(): InetAddress = delegate.localAddress

    override fun getPort(): Int = delegate.port

    override fun getLocalPort(): Int = delegate.localPort

    override fun getRemoteSocketAddress(): SocketAddress? = delegate.remoteSocketAddress

    override fun getLocalSocketAddress(): SocketAddress? = delegate.localSocketAddress

    override fun isConnected(): Boolean = delegate.isConnected

    override fun isBound(): Boolean = delegate.isBound

    override fun isClosed(): Boolean = delegate.isClosed

    override fun isInputShutdown(): Boolean = delegate.isInputShutdown

    override fun isOutputShutdown(): Boolean = delegate.isOutputShutdown

    override fun setTcpNoDelay(on: Boolean) {
        delegate.tcpNoDelay = on
    }

    override fun getTcpNoDelay(): Boolean = delegate.tcpNoDelay

    override fun setSoLinger(
        on: Boolean,
        linger: Int,
    ) {
        delegate.setSoLinger(on, linger)
    }

    override fun getSoLinger(): Int = delegate.soLinger

    override fun sendUrgentData(data: Int) {
        delegate.sendUrgentData(data)
    }

    override fun setOOBInline(on: Boolean) {
        delegate.oobInline = on
    }

    override fun getOOBInline(): Boolean = delegate.oobInline

    override fun setSoTimeout(timeout: Int) {
        delegate.soTimeout = timeout
    }

    override fun getSoTimeout(): Int = delegate.soTimeout

    override fun setSendBufferSize(size: Int) {
        delegate.sendBufferSize = size
    }

    override fun getSendBufferSize(): Int = delegate.sendBufferSize

    override fun setReceiveBufferSize(size: Int) {
        delegate.receiveBufferSize = size
    }

    override fun getReceiveBufferSize(): Int = delegate.receiveBufferSize

    override fun setKeepAlive(on: Boolean) {
        delegate.keepAlive = on
    }

    override fun getKeepAlive(): Boolean = delegate.keepAlive

    override fun setTrafficClass(tc: Int) {
        delegate.trafficClass = tc
    }

    override fun getTrafficClass(): Int = delegate.trafficClass

    override fun setReuseAddress(on: Boolean) {
        delegate.reuseAddress = on
    }

    override fun getReuseAddress(): Boolean = delegate.reuseAddress

    override fun getChannel(): SocketChannel? = delegate.channel
}

private fun SocketAddress.port(): Int =
    when (this) {
        is InetSocketAddress -> port
        else -> error("SSL socket requires an Internet address")
    }
