package com.xiakexing.client.feature.shell

import com.google.gson.JsonObject
import com.xiakexing.client.core.protocol.payload.MessageEventPacket
import com.xiakexing.client.core.protocol.payload.PromptPacket
import com.xiakexing.client.core.protocol.payload.SystemPacket

/*
 * Message 域纯逻辑层——消息事件分类、对话/飘窗/战斗/聊天状态变换、
 * transcript 追加计划与结构化回显去重。不持有 sink，不执行任何副作用。
 */

/** transcript 行分类：忽略/回显消费/登录失败/直达 feed。 */
internal fun classifyTranscriptLine(
    line: String,
    recentTranscriptLines: List<String>,
    pendingEcho: String,
    suppressions: Int,
): TranscriptLinePlan {
    if (line.isBlank() || line.isLoginSuccessStatusLine()) {
        return TranscriptLinePlan.Ignored
    }
    if (consumeStructuredEcho(pendingEcho, suppressions, line, recentTranscriptLines) != null) {
        return TranscriptLinePlan.EchoConsumed
    }
    line.toLoginFailureMessageOrNull()?.let { return TranscriptLinePlan.LoginFailure(it) }
    return TranscriptLinePlan.Feed
}

/** command_error 反馈：战斗动作不得被当前交互/协议 sheet 捕获。 */
internal fun JsonObject.isCombatActionError(): Boolean {
    return shellString("command").trim().lowercase() in COMBAT_ACTION_ERROR_COMMANDS
}

/** 消息事件分类：按 payload kind / displayMode / channel 路由到处理分支。 */
internal fun classifyMessageEvent(packet: MessageEventPacket): MessageEventKind {
    if (packet.payload.shellString("kind").trim().lowercase() == FLOATING_MESSAGE_KIND ||
        packet.displayMode.trim().lowercase() == "float"
    ) {
        return MessageEventKind.Floating
    }
    if (packet.text.isBlank()) {
        return MessageEventKind.Blank
    }
    if (packet.displayMode == "silent_state") {
        return MessageEventKind.Silent
    }
    return when (packet.channel) {
        "chat" -> MessageEventKind.Chat
        "dialog" -> MessageEventKind.Dialog
        "combat" -> MessageEventKind.Combat
        "error", "main", "room", "system" ->
            if (isQuestProgressKind(packet)) {
                MessageEventKind.QuestProgress
            } else if (packet.channel == "error") {
                MessageEventKind.Error
            } else if (packet.payload.shellString("kind") == "item_pickup") {
                MessageEventKind.ItemPickup
            } else {
                MessageEventKind.MainChannel
            }
        else -> MessageEventKind.Blank
    }
}

/** quest_hint 飘窗（「任务」点击应答）判定。 */
internal fun isQuestHintCollapse(packet: MessageEventPacket): Boolean =
    packet.collapseKey.startsWith(QUEST_HINT_COLLAPSE_PREFIX)

/** 任务进度/完成/奖励反馈直达主文本区（payload.kind 标记）。 */
internal fun isQuestProgressKind(packet: MessageEventPacket): Boolean =
    packet.payload.shellString("kind").trim().lowercase() in
        setOf("quest_progress", "quest_complete", "reward_gain")

/** 飘窗消息状态构造：文本规范化 + 等级/ttl/去重键解析（id 由追加变换统一生成）。 */
internal fun buildFloatingMessageState(packet: MessageEventPacket): FloatingMessageState? {
    val text =
        packet.normalizedFloatingMessageText()
            .normalizeInteractionLine()
            ?: return null
    if (text.isBlank()) {
        return null
    }
    val level =
        packet.payload.shellString("level")
            .ifBlank { packet.priority }
            .ifBlank { "info" }
            .trim()
            .lowercase()
    val ttlMs = packet.payload.shellLong("ttl_ms", packet.ttlMs).coerceFloatingMessageTtl()
    val dedupeKey =
        packet.payload.shellString("dedupe_key")
            .ifBlank { packet.collapseKey }
    return FloatingMessageState(
        text = text,
        level = level,
        ttlMs = ttlMs,
        dedupeKey = dedupeKey,
    )
}

/** 聊天消息构造（含序列号）。 */
internal fun buildChatMessageItem(
    packet: MessageEventPacket,
    serial: Long,
): ChatMessageItem? = packet.toChatMessageItemOrNull(serial)

/** 对话消息收敛：按当前 sheet 域重建 NPC 任务/交互/通用协议 sheet。
 * 返回 null 表示不产生任何状态更新（如 dock 任务详情面板的呈现权裁决）。 */
internal fun buildDialogMessageStateUpdate(
    packet: MessageEventPacket,
    currentSheet: ShellSheetState?,
    lines: List<String>,
): DialogMessageStateUpdate? {
    val eventPayload = packet.payload
    val dialogId =
        eventPayload.shellString("dialog_id")
            .ifBlank { eventPayload.shellString("dialogId") }
            .ifBlank {
                packet.collapseKey.substringAfterLast(
                    '/',
                ).takeIf { it != packet.collapseKey }.orEmpty()
            }
            .ifBlank { currentSheet?.dialogId.orEmpty() }
            .ifBlank { "message_dialog" }
    val dialogSource =
        eventPayload.shellString("source")
            .ifBlank { eventPayload.shellString("kind") }
            .ifBlank { "message_event" }
    val title =
        eventPayload.shellString("title")
            .ifBlank { currentSheet?.title.orEmpty() }
            .ifBlank { "通知" }
    val shouldAppend = packet.intent.equals("append", ignoreCase = true) && currentSheet?.dialogId == dialogId
    if (currentSheet?.isNpcQuestInteractionSheet() == true && currentSheet.dialogId == dialogId) {
        return DialogMessageStateUpdate(
            sheet =
                currentSheet.copy(
                    detailLines =
                        if (shouldAppend) {
                            mergeInteractionLines(currentSheet.detailLines, lines)
                        } else {
                            lines
                        },
                    interactionLines = emptyList(),
                    loading = false,
                ),
            dialog = null,
            chatPanelOpen = false,
        )
    }
    if (currentSheet?.source == PopupSource.INTERACTION_OBJECT && currentSheet.dialogId == dialogId) {
        return DialogMessageStateUpdate(
            sheet =
                currentSheet.copy(
                    interactionLines =
                        if (shouldAppend) {
                            mergeInteractionLines(currentSheet.interactionLines, lines)
                        } else {
                            lines
                        },
                    loading = false,
                ),
            dialog = null,
            chatPanelOpen = false,
        )
    }
    /* 第六轮裁决：dock 任务详情面板呈现权归结构化渲染器，任何 dialog-channel
     * 文本包（含 runtime-unavailable 回退的 emit_quest_dialog）都不得顶掉
     * 或清空它的动作区；QUEU 结构化刷新是唯一更新通道。 */
    if (currentSheet?.source == PopupSource.DOCK_BUTTON && currentSheet.isQuestDetailSheet()) {
        return null
    }
    val detailLines =
        if (shouldAppend) {
            mergeInteractionLines(currentSheet?.detailLines.orEmpty(), lines)
        } else {
            lines
        }
    return DialogMessageStateUpdate(
        sheet =
            buildInteractionProtocolSheetState(
                source = PopupSource.PROTOCOL,
                title = title,
                dialogId = dialogId,
                dialogSource = dialogSource,
                detailLines = detailLines,
                interactionLines = emptyList(),
                actions = currentSheet?.actions.orEmpty().takeIf { shouldAppend }.orEmpty(),
                actionGroups = currentSheet?.actionGroups.orEmpty().takeIf { shouldAppend }.orEmpty(),
                activeActionGroupId = currentSheet?.activeActionGroupId.orEmpty().ifBlank { "root" },
                interactionTargetId = currentSheet?.interactionTargetId.orEmpty().takeIf { shouldAppend }.orEmpty(),
                interactionTargetUiKey =
                    currentSheet?.interactionTargetUiKey.orEmpty().takeIf {
                        shouldAppend
                    }.orEmpty(),
                interactionTargetKind = currentSheet?.interactionTargetKind.takeIf { shouldAppend },
                interactionMode = currentSheet?.interactionMode ?: InteractionSheetMode.DETAIL,
            ),
        dialog = null,
        chatPanelOpen = false,
    )
}

/** 聊天追加计划：去重 + 未读渠道/计数更新。 */
internal fun buildChatAppendUpdate(
    message: ChatMessageItem,
    chatMessages: List<ChatMessageItem>,
    chatPanelOpen: Boolean,
    activeChatChannel: ChatChannel,
    unreadChatChannels: Set<ChatChannel>,
): ChatAppendUpdate? {
    if (chatMessages.lastOrNull()?.isSameChatMessage(message) == true) {
        return null
    }
    val nextMessages = chatMessages.withRetainedChatMessage(message)
    val isCurrentChannelVisible = chatPanelOpen && activeChatChannel == message.channel
    val nextUnreadChannels =
        if (isCurrentChannelVisible) {
            unreadChatChannels - message.channel
        } else {
            unreadChatChannels + message.channel
        }
    return ChatAppendUpdate(
        chatMessages = nextMessages,
        unreadChatChannels = nextUnreadChannels,
        chatUnreadCount = nextUnreadChannels.size.coerceAtMost(99),
    )
}

/** transcript 追加计划：保留上限内的可见行。 */
internal fun buildTranscriptAppendPlan(
    line: String,
    currentTranscript: List<String>,
    visibleTranscript: List<TranscriptLine>,
): TranscriptAppendPlan =
    TranscriptAppendPlan(
        visibleTranscript =
            appendVisibleTranscript(
                visibleTranscript = visibleTranscript,
                transcript = currentTranscript,
                appendedLine = line,
                transcriptLimit = TRANSCRIPT_LIMIT,
            ),
    )

/** 聊天+通知追加计划：重复原始回显时先丢弃末行。 */
internal fun buildChatTranscriptAppendPlan(
    line: String,
    duplicateRawEcho: String?,
    currentTranscript: List<String>,
    visibleTranscript: List<TranscriptLine>,
): ChatTranscriptAppendPlan {
    val shouldDropRawEcho =
        duplicateRawEcho != null &&
            currentTranscript.lastOrNull()?.normalizeInteractionLine() == duplicateRawEcho.normalizeInteractionLine()
    val baseTranscript =
        if (shouldDropRawEcho) {
            currentTranscript.dropLast(
                1,
            )
        } else {
            currentTranscript
        }
    val nextTranscript = baseTranscript.withRetainedTranscriptLine(line)
    return ChatTranscriptAppendPlan(
        visibleTranscript =
            if (shouldDropRawEcho) {
                buildVisibleTranscript(nextTranscript)
            } else {
                appendVisibleTranscript(
                    visibleTranscript = visibleTranscript,
                    transcript = currentTranscript,
                    appendedLine = line,
                    transcriptLimit = TRANSCRIPT_LIMIT,
                )
            },
        shouldDropRawEcho = shouldDropRawEcho,
    )
}

internal fun nextChatMessageSequence(serial: Long): Long = serial + 1L

/** 飘窗文本规范化：优先使用协议携带的显示文本。 */
internal fun MessageEventPacket.normalizedFloatingMessageText(): String =
    payload.shellString("text").ifBlank { text }

/** 结构化回显消费：匹配 pending 回显时递减抑制计数。 */
internal fun consumeStructuredEcho(
    pendingEcho: String,
    suppressions: Int,
    line: String,
    recentTranscriptLines: List<String>,
): StructuredEchoConsumption? {
    if (suppressions <= 0 || pendingEcho.isBlank()) {
        return null
    }
    val normalized = line.structuredMessageEchoKey()
    if (normalized != pendingEcho || !isRecentTranscriptDuplicate(normalized, recentTranscriptLines)) {
        return null
    }
    val nextSuppressions = suppressions - 1
    return StructuredEchoConsumption(
        pendingEcho = if (nextSuppressions <= 0) "" else pendingEcho,
        suppressions = nextSuppressions.coerceAtLeast(0),
    )
}

internal fun isRecentTranscriptDuplicate(
    line: String,
    recentTranscriptLines: List<String>,
): Boolean {
    val normalized = line.structuredMessageEchoKey()
    return normalized.isNotBlank() && recentTranscriptLines.lastOrNull()?.structuredMessageEchoKey() == normalized
}

/** 结构化错误反馈计划：回显消费/去重抑制/追加决策。 */
internal fun buildStructuredErrorFeedbackPlan(
    message: String,
    pendingEcho: String,
    suppressions: Int,
    recentTranscriptLines: List<String>,
): StructuredEchoPlan {
    val normalized = message.structuredMessageEchoKey()
    if (normalized.isBlank()) {
        return StructuredEchoPlan(append = false, markSuppressions = 0)
    }
    if (consumeStructuredEcho(pendingEcho, suppressions, normalized, recentTranscriptLines) != null) {
        return StructuredEchoPlan(append = false, markSuppressions = 0, consumePending = true)
    }
    if (isRecentTranscriptDuplicate(normalized, recentTranscriptLines)) {
        return StructuredEchoPlan(append = false, markSuppressions = 1)
    }
    return StructuredEchoPlan(append = true, markSuppressions = 1)
}

/** 结构化可见消息计划：回显消费/去重抑制/追加决策。 */
internal fun buildStructuredVisiblePlan(
    message: String,
    pendingEcho: String,
    suppressions: Int,
    recentTranscriptLines: List<String>,
): StructuredEchoPlan {
    val normalized = message.structuredMessageEchoKey()
    if (normalized.isBlank()) {
        return StructuredEchoPlan(append = false, markSuppressions = 0)
    }
    if (consumeStructuredEcho(pendingEcho, suppressions, normalized, recentTranscriptLines) != null) {
        return StructuredEchoPlan(append = false, markSuppressions = 0, consumePending = true)
    }
    if (isRecentTranscriptDuplicate(normalized, recentTranscriptLines)) {
        return StructuredEchoPlan(append = false, markSuppressions = 1)
    }
    return StructuredEchoPlan(append = true, markSuppressions = 1)
}

/** 任务进度反馈计划：去重后直达 feed。 */
internal fun buildQuestProgressPlan(
    message: String,
    recentTranscriptLines: List<String>,
): Boolean {
    val normalized = message.structuredMessageEchoKey()
    if (normalized.isBlank()) {
        return false
    }
    return !isRecentTranscriptDuplicate(normalized, recentTranscriptLines)
}

/** 系统包计划：登录横幅/场景迁移/错误提示/追加决策。 */
internal fun buildSystemPacketPlan(
    packet: SystemPacket,
    appScene: ShellScene,
    shouldShowLoginError: Boolean,
    onMapAlreadyLatest: Boolean,
): SystemPacketPlan? {
    if (packet.content.isBlank()) {
        return null
    }
    if (packet.content.trim().contains("地图已是最新") && onMapAlreadyLatest) {
        return null
    }
    if (packet.content.isLoginSuccessStatusLine()) {
        return SystemPacketPlan(
            showLoginReadyBanner = true,
            transitionToWorldLoading = false,
            showLoginError = false,
            appendNotice = false,
        )
    }
    val normalizedLevel = packet.level.trim().lowercase()
    return SystemPacketPlan(
        showLoginReadyBanner = false,
        transitionToWorldLoading = appScene == ShellScene.REGISTER && normalizedLevel == "success",
        showLoginError = normalizedLevel == "error" && shouldShowLoginError,
        appendNotice = normalizedLevel !in setOf("success", "warning", "error"),
    )
}

/** 提示包计划：登录失败提示/最新 prompt 更新决策。 */
internal fun buildPromptPlan(
    packet: PromptPacket,
    currentState: ShellUiState,
    recentTranscriptLines: List<String>,
): PromptPlan {
    val loginFailure =
        currentState.isWaitingForLoginResult() && packet.isLoginPromptLike()
    return PromptPlan(
        showLoginError = loginFailure,
        loginErrorMessage =
            recentTranscriptLines.lastOrNull()?.toLoginFailureMessageOrNull()
                ?: "登录失败，请检查账号或密码",
        updateLatestPrompt = currentState.appScene != ShellScene.MAIN_SHELL,
    )
}

/** 错误包计划：结构化追加/登录错误/连接失败决策。 */
internal fun buildErrorPacketPlan(
    currentState: ShellUiState,
    shouldShowLoginError: Boolean,
): ErrorPacketPlan {
    return ErrorPacketPlan(
        appendStructured = true,
        showLoginError = shouldShowLoginError,
        reportConnectionFailed = !shouldShowLoginError && currentState.session.phase != ShellPhase.INTERACTIVE,
    )
}

/** 通知 feed 追加（保留上限）。 */
internal fun buildNoticeFeedUpdate(
    line: String,
    recentNoticeFeed: List<String>,
): List<String> = recentNoticeFeed.withRetainedNoticeLine(line)

internal fun ChatMessageItem.isSameChatMessage(other: ChatMessageItem): Boolean {
    return content == other.content &&
        channel == other.channel &&
        senderName == other.senderName &&
        direction == other.direction &&
        (timestamp <= 0L || other.timestamp <= 0L || timestamp == other.timestamp)
}

internal fun String.structuredMessageEchoKey(): String {
    return normalizeInteractionLine()?.trim().orEmpty().ifBlank { trim() }
}

private val COMBAT_ACTION_ERROR_COMMANDS =
    setOf(
        "attack",
        "exert",
        "fight",
        "flee",
        "kill",
        "perform",
    )

internal fun JsonObject.shellLong(
    key: String,
    default: Long,
): Long {
    return if (has(key) && !get(key).isJsonNull) {
        runCatching { get(key).asLong }.getOrDefault(default)
    } else {
        default
    }
}

internal fun JsonObject.shellBooleanOrDefault(
    key: String,
    default: Boolean,
): Boolean {
    return if (has(key) && !get(key).isJsonNull) {
        runCatching {
            val value = get(key).asJsonPrimitive
            when {
                value.isBoolean -> value.asBoolean
                value.isNumber -> value.asInt != 0
                value.isString -> value.asString == "1" || value.asString.equals("true", ignoreCase = true)
                else -> default
            }
        }.getOrDefault(default)
    } else {
        default
    }
}
