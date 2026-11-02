package com.xiakexing.client.feature.shell

internal class ShellIntentHandler(
    private val sink: Sink,
) {
    interface Sink {
        fun updateCredentials(
            username: String? = null,
            password: String? = null,
        )

        fun updateCommandInput(value: String)

        fun updateChatInput(value: String)

        fun submitRegisterOption(value: String)

        fun sendExit(direction: String)

        fun openDungeonPanel()

        fun openInteractionObject(objectId: String)

        fun toggleDockButton(button: DockButtonType)

        fun handleExitOverlayButtonClick(action: ShellAction)

        fun dismissExitOverlay()

        fun openTitleAction(action: TitleActionType)

        fun dismissPopup()

        fun dismissCommandPalette()

        fun openChatPanel()

        fun dismissChatOverlay()

        fun selectChatChannel(channel: ChatChannel)

        fun sendChatPanelMessage()

        fun resendChatMessage(command: String)

        fun dismissSheet()

        fun popSheet()

        fun handlePopupAction(action: ShellAction)

        fun handlePopupInputChanged(value: String)

        fun handleSheetItem(item: ShellSheetItem)

        fun handleSheetPrimaryAction(action: ShellAction)

        fun connect()

        fun disconnect()

        fun retryReconnect()

        fun leaveReconnectToLogin()

        fun appForegrounded()

        fun appBackgrounded()

        fun sendCommand()

        fun sendMainCommand()

        fun sendInlineCommand(command: String)

        fun systemBackPressed()
    }

    fun handle(intent: ShellIntent) {
        when (intent) {
            is ShellIntent.UsernameChanged -> sink.updateCredentials(username = intent.value)
            is ShellIntent.PasswordChanged -> sink.updateCredentials(password = intent.value)
            is ShellIntent.CommandChanged -> sink.updateCommandInput(intent.value)
            is ShellIntent.RegisterOptionClicked -> sink.submitRegisterOption(intent.value)
            is ShellIntent.ExitClicked -> sink.sendExit(intent.direction)
            ShellIntent.DungeonButtonClicked -> sink.openDungeonPanel()
            is ShellIntent.InteractionObjectClicked -> sink.openInteractionObject(intent.objectId)
            is ShellIntent.DockButtonClicked -> sink.toggleDockButton(intent.button)
            is ShellIntent.ExitOverlayButtonClicked ->
                sink.handleExitOverlayButtonClick(
                    intent.action,
                )
            ShellIntent.ExitOverlayDismissed -> sink.dismissExitOverlay()
            is ShellIntent.TitleActionClicked -> sink.openTitleAction(intent.action)
            ShellIntent.PopupDismissed -> sink.dismissPopup()
            ShellIntent.CommandPaletteDismissed -> sink.dismissCommandPalette()
            ShellIntent.ChatBubbleClicked -> sink.openChatPanel()
            ShellIntent.ChatPanelDismissed -> sink.dismissChatOverlay()
            is ShellIntent.ChatChannelSelected -> sink.selectChatChannel(intent.channel)
            is ShellIntent.ChatInputChanged -> sink.updateChatInput(intent.value)
            ShellIntent.ChatSendClicked -> sink.sendChatPanelMessage()
            is ShellIntent.ChatResendClicked -> sink.resendChatMessage(intent.command)
            ShellIntent.SheetDismissed -> sink.dismissSheet()
            ShellIntent.SheetBackClicked -> sink.popSheet()
            is ShellIntent.PopupActionClicked -> sink.handlePopupAction(intent.action)
            is ShellIntent.PopupInputChanged -> sink.handlePopupInputChanged(intent.value)
            is ShellIntent.SheetItemClicked -> sink.handleSheetItem(intent.item)
            is ShellIntent.SheetPrimaryActionClicked -> sink.handleSheetPrimaryAction(intent.action)
            ShellIntent.ConnectClicked -> sink.connect()
            ShellIntent.DisconnectClicked -> sink.disconnect()
            ShellIntent.ReconnectRetryClicked -> sink.retryReconnect()
            ShellIntent.ReconnectBackToLoginClicked -> sink.leaveReconnectToLogin()
            ShellIntent.AppForegrounded -> sink.appForegrounded()
            ShellIntent.AppBackgrounded -> sink.appBackgrounded()
            ShellIntent.CommandPaletteSendClicked -> sink.sendCommand()
            ShellIntent.SendCommandClicked -> sink.sendMainCommand()
            is ShellIntent.InlineActionClicked -> sink.sendInlineCommand(intent.cmd)
            ShellIntent.SystemBackPressed -> sink.systemBackPressed()
        }
    }
}
