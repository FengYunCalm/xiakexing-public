package com.xiakexing.client.core.network

/** 凭据存取接口（原 feature/shell 的 ShellCredentialStore 下沉重命名）。 */
data class GameCredentials(
    val username: String,
    val password: String,
)

/** Failure category exposed to the persistence owner without exposing secrets. */
enum class CredentialStoreFailureKind {
    CORRUPT_PAYLOAD,
    STORAGE_UNAVAILABLE,
    COMMIT_REJECTED,
}

/**
 * 凭据存储失败（密文损坏/不可解密/持久化拒绝）。
 *
 * 定义在接口层：调用方（shell 持久化协调器、登录发送链）必须捕获并降级为
 * 登出态——存储失败绝不能杀进程。典型诱因：allowBackup 恢复了旧密文，但
 * AndroidKeyStore 密钥已随卸载删除（密钥不备份），新密钥解旧密文抛
 * AEADBadTagException。
 */
class CredentialStoreException(
    message: String,
    cause: Throwable? = null,
    val kind: CredentialStoreFailureKind = CredentialStoreFailureKind.STORAGE_UNAVAILABLE,
) : IllegalStateException(message, cause)

interface GameCredentialStore {
    suspend fun load(): GameCredentials

    suspend fun save(
        username: String,
        password: String,
    )
}
