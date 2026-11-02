package com.xiakexing.client.feature.shell

/**
 * Message 域纯逻辑模型——分类与计划 data class。
 * 与 MessageProjectionReducer.kt 拆分（该文件保持 ≤550 行）。
 */
internal sealed class MessageEventKind {
    data object Floating : MessageEventKind()

    data object Chat : MessageEventKind()

    data object Dialog : MessageEventKind()

    data object Combat : MessageEventKind()

    data object QuestProgress : MessageEventKind()

    data object ItemPickup : MessageEventKind()

    data object Error : MessageEventKind()

    data object MainChannel : MessageEventKind()

    data object Silent : MessageEventKind()

    data object Blank : MessageEventKind()
}

internal data class DialogMessageStateUpdate(
    val sheet: ShellSheetState?,
    val dialog: ShellPopupState?,
    val chatPanelOpen: Boolean,
)

internal data class ChatAppendUpdate(
    val chatMessages: List<ChatMessageItem>,
    val unreadChatChannels: Set<ChatChannel>,
    val chatUnreadCount: Int,
)

internal data class TranscriptAppendPlan(
    val visibleTranscript: List<TranscriptLine>,
)

internal data class ChatTranscriptAppendPlan(
    val visibleTranscript: List<TranscriptLine>,
    val shouldDropRawEcho: Boolean,
)

internal data class StructuredEchoConsumption(
    val pendingEcho: String,
    val suppressions: Int,
)

internal data class StructuredEchoPlan(
    val append: Boolean,
    val markSuppressions: Int,
    val consumePending: Boolean = false,
)

internal data class SystemPacketPlan(
    val showLoginReadyBanner: Boolean,
    val transitionToWorldLoading: Boolean,
    val showLoginError: Boolean,
    val appendNotice: Boolean,
)

internal data class PromptPlan(
    val showLoginError: Boolean,
    val loginErrorMessage: String,
    val updateLatestPrompt: Boolean,
)

internal data class ErrorPacketPlan(
    val appendStructured: Boolean,
    val showLoginError: Boolean,
    val reportConnectionFailed: Boolean,
)

internal sealed class TranscriptLinePlan {
    data object Ignored : TranscriptLinePlan()

    data object EchoConsumed : TranscriptLinePlan()

    data class LoginFailure(val message: String) : TranscriptLinePlan()

    data object Feed : TranscriptLinePlan()
}
