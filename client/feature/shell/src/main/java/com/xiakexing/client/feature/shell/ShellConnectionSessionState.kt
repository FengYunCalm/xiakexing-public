package com.xiakexing.client.feature.shell

import com.xiakexing.client.core.network.GameConnectionRepository

/**
 * 连接会话 UI 侧状态（endpoint/resumeToken/manual 事实委托 repository，
 * 不再镜像；本类只保留 UI 生命周期与登录流程决策状态）。
 */
internal class ShellConnectionSessionState(
    private val repository: GameConnectionRepository,
) {
    var appInForeground: Boolean = true
        private set
    var pendingForegroundResume: Boolean = false
        private set
    var pendingBackgroundDisconnect: Boolean = false
        private set
    var resumeConnectionInProgress: Boolean = false
        private set
    private var preserveUiForResumeLogin: Boolean = false
    var resumeLoginFallbackAllowed: Boolean = false
        private set
    private var resumeTokenConsumedForPrompt = false

    // ===== 连接事实（repository 唯一真源，本类只读不存） =====

    fun manualDisconnectRequested(): Boolean = repository.isManualDisconnectRequested()

    fun lastEndpoint() = repository.lastEndpoint()

    fun activeResumeToken(): String = repository.resumeToken()

    fun prepareConnection(resumeSession: Boolean) {
        pendingForegroundResume = false
        pendingBackgroundDisconnect = false
        resumeConnectionInProgress = resumeSession
        preserveUiForResumeLogin = resumeSession
        resumeLoginFallbackAllowed = false
        resumeTokenConsumedForPrompt = false
        if (!resumeSession) {
            repository.storeResumeToken("")
        }
    }

    fun prepareManualDisconnect() {
        repository.manualDisconnect()
        pendingForegroundResume = false
        pendingBackgroundDisconnect = false
        resumeConnectionInProgress = false
        preserveUiForResumeLogin = false
        resumeLoginFallbackAllowed = false
        resumeTokenConsumedForPrompt = false
    }

    fun clearManualDisconnectRequest() {
        repository.clearManualDisconnectRequest()
    }

    fun markForegrounded() {
        appInForeground = true
    }

    fun markBackgrounded() {
        appInForeground = false
    }

    fun shouldKeepRecoverableSessionAlive(phase: ShellPhase): Boolean {
        return !manualDisconnectRequested() && phase in
            setOf(
                ShellPhase.WAITING_LOGIN_PROMPT,
                ShellPhase.AUTHENTICATING,
                ShellPhase.REGISTERING,
                ShellPhase.WAITING_WORLD,
                ShellPhase.INTERACTIVE,
                ShellPhase.SUSPENDED,
            )
    }

    fun shouldSuspendForForegroundResume(): Boolean {
        return !appInForeground && !manualDisconnectRequested() && lastRequestedEndpointOrNull() != null
    }

    private fun lastRequestedEndpointOrNull() = repository.lastEndpoint()

    fun markPendingForegroundResume() {
        pendingForegroundResume = true
    }

    fun markSuspendedForForegroundResume() {
        pendingForegroundResume = true
        resumeConnectionInProgress = false
    }

    fun markPendingBackgroundDisconnect() {
        pendingBackgroundDisconnect = true
    }

    fun clearPendingBackgroundDisconnect() {
        pendingBackgroundDisconnect = false
    }

    fun connectedWasResume(): Boolean {
        val resumed = resumeConnectionInProgress
        resumeConnectionInProgress = false
        return resumed
    }

    fun isResumeConnectionInProgress(): Boolean {
        return resumeConnectionInProgress
    }

    fun shouldPreserveUiForResumeLogin(): Boolean {
        return preserveUiForResumeLogin
    }

    fun prepareResumeConnection(): com.xiakexing.client.core.network.GatewayEndpoint? {
        if (!pendingForegroundResume || resumeConnectionInProgress || !appInForeground || manualDisconnectRequested()) {
            return null
        }
        val endpoint = repository.lastEndpoint() ?: return null
        pendingForegroundResume = false
        resumeConnectionInProgress = true
        return endpoint
    }

    fun takePendingResumeLoginToken(): String {
        // UI 决策状态：同一次登录会话只消费一次 resume token（服务端单次消费）；
        // token 本身保留在 repository（重连快速恢复用）。
        if (resumeTokenConsumedForPrompt) {
            return ""
        }
        val token = repository.resumeToken().trim()
        if (token.isBlank()) {
            return ""
        }
        resumeTokenConsumedForPrompt = true
        return token
    }

    fun markResumeLoginSent() {
        resumeLoginFallbackAllowed = true
    }

    fun markStoredLoginSent() {
        resumeLoginFallbackAllowed = false
    }

    fun storeResumeToken(token: String) {
        repository.storeResumeToken(token)
        preserveUiForResumeLogin = false
        resumeLoginFallbackAllowed = false
    }

    fun setPendingResumeLoginTokenForTest(token: String) {
        repository.storeResumeToken(token)
    }
}
