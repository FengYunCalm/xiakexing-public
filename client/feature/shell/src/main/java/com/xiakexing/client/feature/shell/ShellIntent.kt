package com.xiakexing.client.feature.shell

sealed interface ShellIntent {
    data class UsernameChanged(val value: String) : ShellIntent

    data class PasswordChanged(val value: String) : ShellIntent

    data class CommandChanged(val value: String) : ShellIntent

    data class RegisterOptionClicked(val value: String) : ShellIntent

    data class ExitClicked(val direction: String) : ShellIntent

    data object DungeonButtonClicked : ShellIntent

    data class InteractionObjectClicked(val objectId: String) : ShellIntent

    data class DockButtonClicked(val button: DockButtonType) : ShellIntent

    data class ExitOverlayButtonClicked(val action: ShellAction) : ShellIntent

    data object ExitOverlayDismissed : ShellIntent

    data class TitleActionClicked(val action: TitleActionType) : ShellIntent

    data object PopupDismissed : ShellIntent

    data object CommandPaletteDismissed : ShellIntent

    data object SheetDismissed : ShellIntent

    data object SheetBackClicked : ShellIntent

    data class PopupActionClicked(val action: ShellAction) : ShellIntent

    data class PopupInputChanged(val value: String) : ShellIntent

    data class SheetItemClicked(val item: ShellSheetItem) : ShellIntent

    data class SheetPrimaryActionClicked(val action: ShellAction) : ShellIntent

    data object ConnectClicked : ShellIntent

    data object DisconnectClicked : ShellIntent

    /** 方案 R-20：S14 重连覆盖层「重试」 */
    data object ReconnectRetryClicked : ShellIntent

    /** 方案 R-20：S14 重连覆盖层「返回登录」 */
    data object ReconnectBackToLoginClicked : ShellIntent

    data object AppForegrounded : ShellIntent

    data object AppBackgrounded : ShellIntent

    data object CommandPaletteSendClicked : ShellIntent

    data object SendCommandClicked : ShellIntent

    data class InlineActionClicked(val cmd: String) : ShellIntent

    data object ChatBubbleClicked : ShellIntent

    data object ChatPanelDismissed : ShellIntent

    data class ChatChannelSelected(val channel: ChatChannel) : ShellIntent

    data class ChatInputChanged(val value: String) : ShellIntent

    data object ChatSendClicked : ShellIntent

    /** 方案 R-30：点击本地失败气泡重发 */
    data class ChatResendClicked(val command: String) : ShellIntent

    data object SystemBackPressed : ShellIntent
}
