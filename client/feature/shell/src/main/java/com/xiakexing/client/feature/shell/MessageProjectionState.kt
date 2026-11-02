package com.xiakexing.client.feature.shell

/**
 * Message 域投影状态与追加变换——transcript/通知/聊天/飘窗的会话内
 * 可变状态、ShellUiState 追加变换（QuestProjectionReducer 的"纯变换+状态"
 * 先例）。Coordinator 只做输入编排与 sink 副作用，状态读写收敛于此。
 */
internal class MessageProjectionState {
    var recentTranscriptLines: List<String> = emptyList()
        private set
    var recentNoticeFeed: List<String> = emptyList()
        private set
    var pendingStructuredMessageEcho: String = ""
    var pendingStructuredMessageEchoSuppressions: Int = 0
    var chatMessageSerial: Long = 0L

    fun recentMessageLines(): List<String> {
        return (
            recentNoticeFeed.ifEmpty {
                recentTranscriptLines.takeLast(
                    8,
                )
            }
        ).takeLast(10).reversed()
    }

    fun resetForConnection(
        endpointLine: String,
        preserveNoticeFeed: Boolean,
    ) {
        recentTranscriptLines =
            if (preserveNoticeFeed) {
                recentTranscriptLines.withRetainedTranscriptLine(endpointLine)
            } else {
                listOf(endpointLine)
            }
        if (!preserveNoticeFeed) {
            recentNoticeFeed = emptyList()
        }
        pendingStructuredMessageEcho = ""
        pendingStructuredMessageEchoSuppressions = 0
    }

    fun clear() {
        recentTranscriptLines = emptyList()
        recentNoticeFeed = emptyList()
        pendingStructuredMessageEcho = ""
        pendingStructuredMessageEchoSuppressions = 0
    }

    fun nextChatMessageSequence(): Long {
        val next = nextChatMessageSequence(chatMessageSerial)
        chatMessageSerial = next
        return next
    }

    fun consumePendingStructuredMessageEcho(line: String): Boolean {
        val consumption =
            consumeStructuredEcho(
                pendingEcho = pendingStructuredMessageEcho,
                suppressions = pendingStructuredMessageEchoSuppressions,
                line = line,
                recentTranscriptLines = recentTranscriptLines,
            ) ?: return false
        pendingStructuredMessageEcho = consumption.pendingEcho
        pendingStructuredMessageEchoSuppressions = consumption.suppressions
        return true
    }

    fun markPendingStructuredMessageEcho(
        line: String,
        suppressions: Int,
    ) {
        pendingStructuredMessageEcho = line.structuredMessageEchoKey()
        pendingStructuredMessageEchoSuppressions = suppressions.coerceAtLeast(0)
    }

    fun applyStructuredEchoPlan(
        line: String,
        plan: StructuredEchoPlan,
    ) {
        if (plan.consumePending) {
            consumePendingStructuredMessageEcho(line)
        }
        if (plan.markSuppressions > 0) {
            markPendingStructuredMessageEcho(line, plan.markSuppressions)
        }
    }

    fun appendTranscript(line: String) {
        recentTranscriptLines = recentTranscriptLines.withRetainedTranscriptLine(line)
    }

    fun appendNotice(line: String) {
        recentNoticeFeed = recentNoticeFeed.withRetainedNoticeLine(line)
    }

    fun updateNoticeFeed(line: String) {
        recentNoticeFeed = buildNoticeFeedUpdate(line, recentNoticeFeed)
    }
}

/** 飘窗消息追加变换（消息体已构造）：id 统一由当前列表生成（单一 id 真源）。 */
internal fun buildFloatingEventAppendTransform(
    message: FloatingMessageState,
): ShellUiState.() -> ShellUiState =
    {
        copy(
            floatingMessages =
                floatingMessages.withFloatingMessage(
                    message.copy(id = nextFloatingMessageId()),
                ),
        )
    }

/** 主文本区可见行追加变换（去重与上限由调用方先行完成）。 */
internal fun buildVisibleTranscriptAppendTransform(
    visibleTranscript: List<TranscriptLine>,
): ShellUiState.() -> ShellUiState =
    {
        copy(visibleTranscript = visibleTranscript)
    }

/** 下沉：世界加载场景切换变换。 */
internal fun buildWorldLoadingTransform(): ShellUiState.() -> ShellUiState =
    {
        copy(appScene = ShellScene.WORLD_LOADING, commandPalette = null, exitOverlay = null)
    }

/** 下沉：对话弹窗追加变换。 */
internal fun buildDialogAppendTransform(
    update: DialogMessageStateUpdate,
): ShellUiState.() -> ShellUiState =
    {
        copy(
            sheet = update.sheet,
            dialog = update.dialog,
            chatPanelOpen = update.chatPanelOpen,
        )
    }

/** 下沉：聊天追加变换。 */
internal fun buildChatAppendTransform(update: ChatAppendUpdate): ShellUiState.() -> ShellUiState =
    {
        copy(
            chatMessages = update.chatMessages,
            unreadChatChannels = update.unreadChatChannels,
            chatUnreadCount = update.chatUnreadCount,
        )
    }

/** 下沉：quest_hint 应答后的 sheet 复位变换。 */
internal fun buildQuestHintSettleTransform(): ShellUiState.() -> ShellUiState =
    {
        val currentSheet = sheet
        val target =
            if (currentSheet?.source == PopupSource.INTERACTION_OBJECT &&
                currentSheet.interactionTargetKind == InteractionObjectKind.NPC
            ) {
                interactionObjects.findInteractionObjectByKey(
                    currentSheet.interactionTargetUiKey.ifBlank { currentSheet.interactionTargetId },
                )
            } else {
                null
            }
        val settledSheet =
            target?.let { buildNpcContextResumeSheet(it, hasGiveableInventoryItems()) }
                ?: currentSheet?.copy(loading = false, activeActionGroupId = "root")
        copy(sheet = settledSheet)
    }
