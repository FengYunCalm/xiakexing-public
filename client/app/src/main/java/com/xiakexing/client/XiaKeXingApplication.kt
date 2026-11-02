package com.xiakexing.client

import android.app.Application
import android.util.Log
import com.xiakexing.client.connect.TlsFallbackGatewaySessionPort
import com.xiakexing.client.core.network.GameConnectionRepository
import com.xiakexing.client.core.network.ProcessConnectionOwner
import com.xiakexing.client.core.network.ProcessConnectionOwnerProvider
import com.xiakexing.client.core.network.PublicEndpoint
import com.xiakexing.client.core.network.PublicEndpointProvider
import com.xiakexing.client.core.storage.AndroidGameCredentialStore
import kotlinx.coroutines.CoroutineExceptionHandler
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob

/** Process composition root for the Android client. */
class XiaKeXingApplication :
    Application(),
    PublicEndpointProvider,
    ProcessConnectionOwnerProvider {
    private val processScope =
        CoroutineScope(
            SupervisorJob() +
                Dispatchers.IO +
                CoroutineExceptionHandler { _, error ->
                    // 兜底：任何未经捕获的协程异常只记日志，绝不杀进程。
                    // 例：卸载重装后 KeyStore 密钥丢失、备份恢复了密文，
                    // 头less 自举读凭据解密失败（见 PersistenceCoordinator 降级）。
                    Log.e(TAG, "unhandled process-scope error", error)
                },
        )

    override val publicEndpoint: PublicEndpoint by lazy {
        PublicEndpoint(
            host = BuildConfig.PUBLIC_ENDPOINT_HOST,
            httpsPort = BuildConfig.PUBLIC_ENDPOINT_PORT,
        )
    }

    override val processConnectionOwner: ProcessConnectionOwner by lazy {
        val credentialsStore = AndroidGameCredentialStore(this)
        val repository =
            GameConnectionRepository(
                scope = processScope,
                credentialsStore = credentialsStore,
                initialPort = TlsFallbackGatewaySessionPort(processScope),
            )
        ProcessConnectionOwner(
            scope = processScope,
            repository = repository,
            publicEndpoint = publicEndpoint,
        )
    }

    override fun onCreate() {
        super.onCreate()
        // Construct the graph once, before any Activity or Service can attach.
        processConnectionOwner
    }

    private companion object {
        const val TAG = "XiaKeXingApp"
    }
}
