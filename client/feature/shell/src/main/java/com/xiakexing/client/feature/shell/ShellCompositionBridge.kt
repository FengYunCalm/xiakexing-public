package com.xiakexing.client.feature.shell

import com.xiakexing.client.core.protocol.IncomingFrame
import com.xiakexing.client.core.protocol.payload.RegisterPrompt

/**
 * ShellComposition 到 ShellViewModel 的窄回调桥（组合根）。
 * 只保留 VM state-owner 角色与 VM 特有逻辑（路由预览计时、瞬态清理）；
 * 其余 sink 调用已在 composition 内直接指向对应 coordinator。
 * 注意：接口签名不含默认参数（override 不允许），默认值保留在 VM 实现。
 */
internal interface ShellCompositionStateBridge {
    fun currentUiState(): ShellUiState

    fun updateState(transform: ShellUiState.() -> ShellUiState)

    fun updateStateWithSessionEvent(
        event: ShellEvent,
        transform: ShellUiState.() -> ShellUiState,
    )
}

internal interface ShellCompositionVmBridge : ShellPacketHandler.Sink {
    fun cancelRoutePreviewTimeout(requestId: String)

    fun resetProtocolPacketGate()

    fun resetIncomingProtocolStream()

    fun clearInventoryRefreshPending()

    fun clearPendingRoomRefresh()

    fun clearRoomProjectionForProjectionSwitch()

    fun flushPendingRoomRefresh()

    fun resetConnectionTransientState()

    fun scheduleRoutePreviewTimeout(requestId: String)

    fun selectLearningSkillCategory(categoryId: String)

    fun sendDirectCommand(
        command: String,
        clearInputAfterSend: Boolean,
        suppressImmediateScroll: Boolean,
        bypassThrottle: Boolean,
    )

    fun showApprenticeUnavailablePopup()

    // ===== packet handler 桥（Step5：登录语义 + 未接入回退） =====

    override fun shouldStopAutomaticLoginRetry(): Boolean

    override fun currentLoginFailureMessage(): String

    override fun showLoginError(message: String)

    override fun onLoginPromptReady()

    override fun onLoginBootstrapComplete()

    override fun sendLoginForPrompt()

    override fun onRegisterPrompt(prompt: RegisterPrompt)

    override fun handleUnhandledPacket(frame: IncomingFrame.Packet)
}
