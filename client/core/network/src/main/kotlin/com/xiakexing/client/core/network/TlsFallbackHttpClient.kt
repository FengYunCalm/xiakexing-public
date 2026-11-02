package com.xiakexing.client.core.network

import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.Response
import java.io.Closeable
import java.io.IOException
import java.io.InputStream
import java.net.URL
import java.util.concurrent.TimeUnit

class TlsFallbackHttpResponse internal constructor(
    private val response: Response,
) : Closeable {
    val statusCode: Int
        get() = response.code

    val contentLength: Long
        get() = response.body?.contentLength() ?: -1L

    val body: InputStream?
        get() = response.body?.byteStream()

    override fun close() {
        response.close()
    }
}

class TlsFallbackHttpClient internal constructor(
    private val noSniClient: OkHttpClient,
    private val standardClient: OkHttpClient,
) {
    fun executeGet(
        url: URL,
        allowedHost: String,
        connectTimeoutMillis: Int,
        readTimeoutMillis: Int,
    ): TlsFallbackHttpResponse {
        validateSecureUpdateUrl(url, allowedHost)
        require(connectTimeoutMillis > 0 && readTimeoutMillis > 0) {
            "HTTP timeouts must be positive"
        }

        val request = Request.Builder().url(url.toString()).get().build()
        val noSniRequestClient =
            noSniClient.forUpdateRequest(
                connectTimeoutMillis,
                readTimeoutMillis,
            )
        val standardRequestClient =
            standardClient.forUpdateRequest(
                connectTimeoutMillis,
                readTimeoutMillis,
            )
        val response =
            try {
                noSniRequestClient.newCall(request).execute()
            } catch (noSniError: IOException) {
                try {
                    standardRequestClient.newCall(request).execute()
                } catch (standardError: IOException) {
                    if (standardError !== noSniError) {
                        standardError.addSuppressed(noSniError)
                    }
                    throw standardError
                }
            }
        return TlsFallbackHttpResponse(response)
    }

    companion object {
        val shared: TlsFallbackHttpClient by lazy {
            TlsFallbackHttpClient(
                noSniClient = NoSniOkHttpClient.create(),
                standardClient = OkHttpClient(),
            )
        }
    }
}

internal fun OkHttpClient.forUpdateRequest(
    connectTimeoutMillis: Int,
    readTimeoutMillis: Int,
): OkHttpClient =
    newBuilder()
        .connectTimeout(connectTimeoutMillis.toLong(), TimeUnit.MILLISECONDS)
        .readTimeout(readTimeoutMillis.toLong(), TimeUnit.MILLISECONDS)
        .followRedirects(false)
        .followSslRedirects(false)
        .build()

private fun validateSecureUpdateUrl(
    url: URL,
    allowedHost: String,
) {
    require(url.protocol.equals("https", ignoreCase = true)) {
        "update URL must use HTTPS"
    }
    require(url.userInfo == null && url.query == null && url.ref == null) {
        "update URL must not contain credentials or query parameters"
    }
    require(url.port == -1 || url.port == 443) {
        "update URL must use port 443"
    }
    require(allowedHost.isNotBlank() && url.host.equals(allowedHost, ignoreCase = true)) {
        "update URL host is not the configured update host"
    }
}
