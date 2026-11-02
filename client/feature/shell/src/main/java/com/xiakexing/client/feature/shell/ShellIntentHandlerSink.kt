package com.xiakexing.client.feature.shell

/**
 * ShellIntentHandler 的 Sink 实现（VM 瘦身——intent 副作用
 * 直接指向 coordinator，VM 只保留 intent 入口与 state-owner 角色）。
 */
internal class ShellIntentHandlerSink(
    private val updateState: (ShellUiState.() -> ShellUiState) -> Unit,
    private val currentUiState: () -> ShellUiState,
    private val sendDirectCommand: (
        command: String,
        clearInputAfterSend: Boolean,
        suppressImmediateScroll: Boolean,
        bypassThrottle: Boolean,
    ) -> Unit,
    private val surface: SurfaceCoordinator,
    private val commandCoordinator: CommandCoordinator,
    private val connectionPresentation: ConnectionPresentationCoordinator,
    private val interactionSession: InteractionSessionCoordinator,
    private val panel: PanelCoordinator,
    private val quantityFlow: QuantityCoordinator,
    private val markCredentialsEdited: () -> Unit,
) : ShellIntentHandler.Sink {
    override fun updateCredentials(
        username: String?,
        password: String?,
    ) {
        if (username != null || password != null) {
            markCredentialsEdited()
        }
        updateState {
            copy(
                username = username ?: this.username,
                password = password ?: this.password,
                loginErrorMessage = "",
            )
        }
    }

    override fun updateCommandInput(value: String) =
        updateState {
            copy(
                commandInput = value,
            )
        }

    override fun updateChatInput(value: String) =
        updateState {
            copy(
                chatInput = value,
            )
        }

    override fun submitRegisterOption(value: String) =
        commandCoordinator.submitRegisterOption(value)

    override fun sendExit(direction: String) {
        val normalizedDirection = direction.trim()
        if (normalizedDirection.isBlank()) {
            return
        }
        surface.dismissTransientInteractionSurfaces()
        sendDirectCommand(
            "go $normalizedDirection",
            false,
            false,
            true,
        )
    }

    override fun openDungeonPanel() {
        surface.dismissTransientInteractionSurfaces()
        panel.showDungeonPanel()
    }

    override fun openInteractionObject(objectId: String) {
        surface.dismissTransientInteractionSurfaces()
        interactionSession.showInteractionObjectPopup(objectId)
        maybeLookSceneItem(objectId)
    }

    /* 点击场景物品即视为查看——弹窗展示缓存描述的同时补发
     * look 命令，任务 QUEST_STEP_LOOK 由服务端事件驱动照常推进。 */
    private fun maybeLookSceneItem(objectId: String) {
        val target = currentUiState().interactionObjects.findInteractionObjectByKey(objectId) ?: return
        if (target.kind != InteractionObjectKind.ITEM) {
            return
        }
        val token = target.scopedCommandTarget().trim()
        if (token.isBlank()) {
            return
        }
        sendDirectCommand(
            "look $token",
            false,
            false,
            false,
        )
    }

    override fun toggleDockButton(button: DockButtonType) {
        if (button == DockButtonType.INPUT && currentUiState().commandPalette != null) {
            surface.dismissCommandPaletteIfShowing()
            return
        }
        surface.dismissCommandPaletteIfShowing()
        if (button != DockButtonType.QUEST) {
            surface.dismissSheetIfShowing()
        }
        surface.showDockPopup(button)
    }

    override fun handleExitOverlayButtonClick(action: ShellAction) =
        surface.handleExitOverlayButtonClick(action)

    override fun dismissExitOverlay() = surface.dismissExitOverlayIfShowing()

    override fun openTitleAction(action: TitleActionType) {
        surface.dismissCommandPaletteIfShowing()
        surface.dismissSheetIfShowing()
        surface.dismissExitOverlayIfShowing()
        surface.showTitleActionPopup(action)
    }

    override fun dismissPopup() = surface.dismissPopup()

    override fun dismissCommandPalette() = surface.dismissCommandPaletteIfShowing()

    override fun openChatPanel() = commandCoordinator.openChatPanel()

    /* 玩家收起聊天浮层 = 离开聊天表面：连同其下被隐藏的 sheet/弹窗/出口浮层一并关闭，
     * 落回主界面；否则关闭聊天会把玩家丢回他不再期待打开的 dock 面板。 */
    override fun dismissChatOverlay() = surface.dismissTransientInteractionSurfaces()

    override fun selectChatChannel(channel: ChatChannel) =
        commandCoordinator.selectChatChannel(channel)

    override fun sendChatPanelMessage() = commandCoordinator.sendChatPanelMessage()

    override fun resendChatMessage(command: String) = commandCoordinator.resendChatMessage(command)

    override fun dismissSheet() = surface.dismissSheet()

    override fun popSheet() {
        if (!surface.pop()) {
            surface.dismissSheet()
        }
    }

    override fun handlePopupAction(action: ShellAction) = surface.handlePopupAction(action)

    override fun handlePopupInputChanged(value: String) =
        quantityFlow.handlePopupInputChanged(value)

    override fun handleSheetItem(item: ShellSheetItem) = surface.handleSheetItem(item)

    override fun handleSheetPrimaryAction(action: ShellAction) =
        surface.handleSheetPrimaryAction(action)

    override fun connect() = connectionPresentation.connect()

    override fun disconnect() = connectionPresentation.disconnect()

    override fun retryReconnect() = connectionPresentation.retryReconnect()

    override fun leaveReconnectToLogin() = connectionPresentation.leaveReconnectToLogin()

    override fun appForegrounded() = connectionPresentation.onForegrounded()

    override fun appBackgrounded() {
        connectionPresentation.onBackgrounded()
    }

    override fun sendCommand() = commandCoordinator.sendCommand()

    override fun systemBackPressed() {
        surface.handleBack()
    }

    override fun sendMainCommand() {
        if (currentUiState().shouldKeepInteractionSheetForManualCommand()) {
            surface.dismissObscuringSurfacesAroundInteractionSheet()
        } else {
            surface.dismissObscuringSurfacesForExternalCommand()
        }
        commandCoordinator.sendCommand()
    }

    override fun sendInlineCommand(command: String) {
        surface.dismissObscuringSurfacesForExternalCommand()
        sendDirectCommand(
            command,
            false,
            false,
            false,
        )
    }
}

/** 手动命令输入时交互弹窗驻留判定（原 VM 私有扩展）。 */
internal fun ShellUiState.shouldKeepInteractionSheetForManualCommand(): Boolean {
    val currentSheet = sheet ?: return false
    return currentSheet.source == PopupSource.INTERACTION_OBJECT &&
        commandInput.shouldRemainInInteractionSession()
}
