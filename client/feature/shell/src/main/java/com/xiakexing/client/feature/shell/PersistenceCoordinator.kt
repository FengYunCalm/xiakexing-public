package com.xiakexing.client.feature.shell

import com.xiakexing.client.core.network.CredentialStoreException
import com.xiakexing.client.core.network.GameCredentialStore
import com.xiakexing.client.core.network.GameCredentials
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.launch
import java.util.concurrent.atomic.AtomicLong

/**
 * 本地持久化协调器（合并：战斗快捷键 + 登录凭据）。
 * 职责：磁盘读写与 state 回填，无渲染/协议逻辑。
 */
internal class PersistenceCoordinator(
    private val scope: CoroutineScope,
    private val combatShortcutStore: ShellCombatShortcutStore,
    private val credentialStore: GameCredentialStore,
    private val sink: Sink,
) {
    private val credentialsEditGeneration = AtomicLong(0L)

    interface Sink {
        fun updateState(transform: ShellUiState.() -> ShellUiState)
    }

    // ===== 战斗快捷键 =====

    suspend fun loadCombatShortcuts(): StoredCombatShortcuts {
        return combatShortcutStore.load()
    }

    fun saveCombatShortcuts(
        assignedIds: List<String>,
        removedIds: Set<String>,
    ) {
        scope.launch {
            combatShortcutStore.save(assignedIds, removedIds)
        }
    }

    // ===== 登录凭据 =====

    fun markCredentialsEdited() {
        credentialsEditGeneration.incrementAndGet()
    }

    suspend fun loadStoredCredentials() {
        val loadGeneration = credentialsEditGeneration.get()
        var loadFailed = false
        val credentials =
            try {
                credentialStore.load()
            } catch (_: CredentialStoreException) {
                // 保留持久载荷供后续恢复/用户成功保存覆盖；load 失败不能
                // 另起一笔空值 save 去覆盖可能属于更新线程的新值。
                loadFailed = true
                GameCredentials(username = "", password = "")
            }
        val formWasEdited = credentialsEditGeneration.get() != loadGeneration
        sink.updateState {
            copy(
                username = if (formWasEdited) username else credentials.username,
                password = if (formWasEdited) password else credentials.password,
                loginErrorMessage =
                    if (loadFailed) "本地凭据暂不可用，请重新输入" else loginErrorMessage,
            )
        }
    }

    fun saveCredentials(
        username: String,
        password: String,
    ) {
        scope.launch {
            try {
                credentialStore.save(username, password)
            } catch (_: CredentialStoreException) {
                sink.updateState {
                    copy(loginErrorMessage = "本地凭据保存失败，请稍后重试")
                }
            }
        }
    }
}
