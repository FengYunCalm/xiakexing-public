package com.xiakexing.client.feature.shell

import com.xiakexing.client.core.protocol.IncomingFrame
import com.xiakexing.client.core.protocol.XkStreamDecoder
import com.xiakexing.client.core.protocol.payload.RegisterPrompt

/**
 * ShellCompositionVmBridge 实现（VM 瘦身——桥接副作用经
 * [ShellFacade] 指向 coordinator，VM 只保留 state-owner 与生命周期）。
 * 本实现先于 facade 构造（facade 依赖 vmBridge 进入组合根），因此以
 * 惰性 `facade: () -> ShellFacade` 引用；桥方法在构造完成后才被调用。
 */
internal class ShellCompositionVmBridgeImpl(
    private val updateState: (ShellUiState.() -> ShellUiState) -> Unit,
    private val currentUiState: () -> ShellUiState,
    private val streamDecoder: XkStreamDecoder,
    private val facade: () -> ShellFacade,
    private val resumeLoginFallbackAllowed: () -> Boolean,
    private val onLoginPromptReadyFn: () -> Unit,
    private val onLoginBootstrapCompleteFn: () -> Unit,
    private val scheduleRoutePreviewTimeoutFn: (String) -> Unit,
    private val cancelRoutePreviewTimeoutFn: (String) -> Unit,
) : ShellCompositionVmBridge {
    override fun cancelRoutePreviewTimeout(requestId: String) =
        cancelRoutePreviewTimeoutFn(requestId)

    override fun resetProtocolPacketGate() = facade().resetProtocolPacketGate()

    override fun resetIncomingProtocolStream() {
        streamDecoder.reset()
        facade().resetProtocolPacketGate()
    }

    override fun clearInventoryRefreshPending() {
        updateState {
            if (!inventoryRefreshPending) this else copy(inventoryRefreshPending = false)
        }
    }

    override fun clearPendingRoomRefresh() {
        facade().clearPendingRoomRefresh()
    }

    override fun clearRoomProjectionForProjectionSwitch() {
        facade().clearRoomProjectionForProjectionSwitch()
    }

    override fun flushPendingRoomRefresh() {
        facade().flushPendingRoomRefresh()
    }

    override fun resetConnectionTransientState() {
        facade().resetConnectionTransientStateWithProjections()
    }

    override fun scheduleRoutePreviewTimeout(requestId: String) =
        scheduleRoutePreviewTimeoutFn(requestId)

    override fun selectLearningSkillCategory(categoryId: String) {
        updateState {
            val currentSheet = sheet
            if (currentSheet?.source == PopupSource.INTERACTION_LEARN) {
                copy(sheet = currentSheet.selectLearningSkillCategory(categoryId))
            } else {
                this
            }
        }
    }

    override fun sendDirectCommand(
        command: String,
        clearInputAfterSend: Boolean,
        suppressImmediateScroll: Boolean,
        bypassThrottle: Boolean,
    ) {
        if (command.requiresInventoryRefresh()) {
            updateState { copy(inventoryRefreshPending = true) }
        }
        facade().sendDirectCommand(
            command,
            clearInputAfterSend,
            suppressImmediateScroll,
            bypassThrottle,
        )
    }

    override fun showApprenticeUnavailablePopup() {
        facade().showApprenticeUnavailablePopup()
    }

    override fun shouldStopAutomaticLoginRetry(): Boolean {
        return currentUiState().shouldStopAutomaticLoginRetry() && !resumeLoginFallbackAllowed()
    }

    override fun currentLoginFailureMessage(): String {
        return currentUiState().loginErrorMessage
            .ifBlank {
                facade().recentTranscriptLines().lastOrNull()?.toLoginFailureMessageOrNull().orEmpty()
            }
            .ifBlank { "登录失败，请重新输入账号密码" }
    }

    override fun showLoginError(message: String) = facade().showLoginError(message)

    override fun onLoginPromptReady() = onLoginPromptReadyFn()

    override fun onLoginBootstrapComplete() = onLoginBootstrapCompleteFn()

    override fun sendLoginForPrompt() = facade().sendLoginForPrompt()

    override fun onRegisterPrompt(prompt: RegisterPrompt) = facade().onRegisterPrompt(prompt)

    override fun handleUnhandledPacket(frame: IncomingFrame.Packet) =
        facade().handleUnhandledPacket(frame)
}
