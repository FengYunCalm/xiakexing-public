package com.xiakexing.client.feature.shell

import androidx.compose.runtime.Immutable
import com.xiakexing.client.core.protocol.payload.QuestEntryPacket
import com.xiakexing.client.core.protocol.payload.QuestRewardsPacket
import com.xiakexing.client.core.protocol.payload.QuestUpdatePacket

internal const val OPEN_QUEST_DETAIL_PREFIX = "ui://panel/quest/detail/"
internal const val OPEN_QUEST_LIST_COMMAND = "ui://panel/quest/list"

@Immutable
data class QuestPanelSessionState(
    val mode: QuestPanelMode = QuestPanelMode.LIST,
    val selectedQuestId: String = "",
)

@Immutable
internal data class QuestFloatingFeedback(
    val text: String,
    val ttlMs: Long,
    val dedupeKey: String,
    val signature: String,
)

enum class QuestPanelMode {
    LIST,
    DETAIL,
}

internal fun QuestEntryPacket.questTrailing(fallback: String = ""): String {
    return when {
        canTurnIn -> "交付"
        progressLabel().isNotBlank() -> progressLabel()
        else -> fallback
    }
}

internal fun buildQuestSheetItems(quests: List<QuestEntryPacket>): List<ShellSheetItem> {
    return quests.mapIndexed { index, quest ->
        ShellSheetItem(
            title = quest.displayName(index),
            trailing = quest.questTrailing(),
            command = "$OPEN_QUEST_DETAIL_PREFIX${quest.questId}",
        )
    }
}

internal fun buildQuestListSheetState(
    quests: List<QuestEntryPacket>,
    loading: Boolean,
): ShellSheetState {
    return buildInfoListSheet(
        source = PopupSource.DOCK_BUTTON,
        title = "任务",
        showHeaderTitle = true,
        dialogSource = "quest",
        items = buildQuestSheetItems(quests),
        primaryAction = ShellAction(label = "刷新", command = "quest", dismissAfterInvoke = false),
        panelEmptyText = "暂无任务，可点刷新读取最新任务",
        emptyPresentation = ShellSheetEmptyPresentation.MESSAGE,
        loading = loading,
        navigation =
            SheetNavigationMeta(
                dockSection = DockWorkspaceSection.ACTIVITY,
                routeKey = "objective/quest",
            ),
        kind = ShellSheetKind.QUEST,
    )
}

internal fun buildInlineQuestListSheetState(quests: List<QuestEntryPacket>): ShellSheetState {
    return buildDetailSheet(
        source = PopupSource.PROTOCOL,
        title = "任务",
        showHeaderTitle = false,
        dialogSource = "quest",
        items = buildQuestSheetItems(quests),
        loading = false,
        kind = ShellSheetKind.QUEST,
    )
}

internal fun buildQuestDetailSheetState(
    questId: String,
    quest: QuestEntryPacket?,
    command: String,
    trackedQuest: TrackedQuestState = TrackedQuestState(),
    loading: Boolean = quest == null && questId.isNotBlank(),
): ShellSheetState {
    val detailLines = quest?.toQuestWorkbenchDetailLines().orEmpty()
    return buildInfoListSheet(
        source = PopupSource.DOCK_BUTTON,
        title = quest?.displayName() ?: questId.ifBlank { "任务" },
        dialogSource = "quest",
        detailLines = detailLines,
        actions = buildQuestDetailActions(questId, quest, trackedQuest),
        primaryAction = ShellAction("刷新", command, dismissAfterInvoke = false),
        panelStats = quest?.toQuestWorkbenchStats().orEmpty(),
        panelEmptyText = "暂无任务内容",
        loading = loading,
        navigation =
            SheetNavigationMeta(
                dockSection = DockWorkspaceSection.ACTIVITY,
                routeKey = "objective/quest/$questId",
                depth = 1,
            ),
        kind = ShellSheetKind.QUEST,
    )
}

internal fun buildNpcQuestInteractionSheetState(
    title: String,
    dialogId: String,
    dialogSource: String,
    detailLines: List<String>,
    actions: List<ShellAction>,
    actionGroups: List<ShellActionGroup>,
    interactionTargetId: String,
    interactionTargetUiKey: String = "",
    interactionTargetKind: InteractionObjectKind,
    detailMode: Boolean = false,
): ShellSheetState {
    /* guided 步骤指引的可见性由 visibleActions 共享规则保证（第七轮），
     * actions 入参已含跨组 guided 动作，此处不再私有提升。
     * 但任务面板只承载任务动作：交谈/追问话题属对话流，guided 跨组追加不得把它们
     * 摆到任务列表底部（接取 talk/ask 步骤的任务后必现）。过滤收在这一处，
     * 保证所有调用方（含快照测试）看到同一动作集。 */
    val panelActions = actions.filterNot { action -> action.isNpcConversationGuidanceAction() }
    val questItems = if (detailMode) emptyList() else buildNpcQuestSheetItems(panelActions)
    val visibleActions =
        if (detailMode) {
            panelActions.filterNpcQuestDetailActions()
        } else {
            emptyList()
        }
    return buildInfoListSheet(
        source = PopupSource.INTERACTION_OBJECT,
        title = title.ifBlank { "任务" },
        showHeaderTitle = detailMode,
        dialogId = dialogId,
        dialogSource = dialogSource,
        detailLines = if (detailMode) detailLines else emptyList(),
        items = questItems,
        actions = visibleActions,
        actionGroups = actionGroups,
        activeActionGroupId = "quest",
        panelEmptyText = "此人眼下没有可处理的任务",
        interactionTargetId = interactionTargetId,
        interactionTargetUiKey = interactionTargetUiKey,
        interactionTargetKind = interactionTargetKind,
        interactionMode = InteractionSheetMode.DETAIL,
        loading = false,
        navigation =
            SheetNavigationMeta(
                routeKey =
                    buildString {
                        append("interaction/npc/")
                        append(interactionTargetUiKey.ifBlank { interactionTargetId })
                        append("/quest")
                        if (detailMode) {
                            val detailId =
                                actions.firstOrNull {
                                        action ->
                                    action.command.isQuestInfoCommand()
                                }
                                    ?.command
                                    ?.questCommandId()
                                    .orEmpty()
                            if (detailId.isNotBlank()) append("/").append(detailId)
                        }
                    },
                depth = 1,
            ),
    )
}

internal fun buildQuestSheetForSession(
    session: QuestPanelSessionState,
    quests: List<QuestEntryPacket>,
    trackedQuest: TrackedQuestState = TrackedQuestState(),
    inlineList: Boolean = false,
    loadingList: Boolean = false,
): ShellSheetState {
    val normalizedQuestId = session.selectedQuestId.trim()
    val quest = quests.firstOrNull { it.questId == normalizedQuestId }
    if (session.mode == QuestPanelMode.DETAIL && normalizedQuestId.isNotBlank() && quest != null) {
        return buildQuestDetailSheetState(
            questId = normalizedQuestId,
            quest = quest,
            command = "quest info $normalizedQuestId",
            trackedQuest = trackedQuest,
            loading = false,
        )
    }
    return if (inlineList) {
        buildInlineQuestListSheetState(quests)
    } else {
        buildQuestListSheetState(quests, loading = loadingList)
    }
}

/**
 * 任务面板 sheet 与会话态成对写入（单一入口）：两者必须同步更新，
 * 单独写其一会造成面板模式与 sheet 漂移（validatedAgainst 只能事后校正）。
 */
internal fun ShellUiState.withQuestSheet(
    sheet: ShellSheetState?,
    session: QuestPanelSessionState = QuestPanelSessionState(),
): ShellUiState = copy(sheet = sheet, questPanelSession = session)

internal fun QuestPanelSessionState.validatedAgainst(
    quests: List<QuestEntryPacket>,
): QuestPanelSessionState {
    val normalizedQuestId = selectedQuestId.trim()
    if (mode != QuestPanelMode.DETAIL || normalizedQuestId.isBlank()) {
        return QuestPanelSessionState()
    }
    return if (quests.any { it.questId == normalizedQuestId }) {
        copy(selectedQuestId = normalizedQuestId)
    } else {
        QuestPanelSessionState()
    }
}

internal fun QuestEntryPacket.toQuestWorkbenchStats(): List<String> {
    return buildList {
        // 第七轮真机反馈：详情页「可接取」小字冗余（看的就是可接取任务），删。
        status.takeIf {
            it.isNotBlank() && !it.isQuestInProgressStatus() && !it.isQuestAcceptableStatus()
        }
            ?.let { add(it) }
        progressLabel().takeIf { it.isNotBlank() }?.let { add("我的进度 $it") }
        if (canTurnIn) {
            add("可回报")
        }
    }
}

internal fun QuestEntryPacket.toQuestWorkbenchDetailLines(): List<String> {
    return buildList {
        /* 第五轮 R9：详情三节定序 描述→目标→奖励（与服务端渲染器同序，用户规格）。
         * 目标：每步 desc(p/t)；奖励：名称+数量顿连；描述：summary。 */
        summary.takeIf { it.isNotBlank() }?.let { add("描述：$it") }
        if (steps.isNotEmpty()) {
            add("目标：")
            steps.forEach { step ->
                add("${step.desc}(${step.progress}/${step.total})")
            }
        } else {
            // AVAILABLE 态无运行态 steps：保留目的地信息（旧契约护栏）。
            val target = targetEntityName.ifBlank { targetRoomName }
            if (target.isNotBlank()) {
                add("目标：$target")
            } else {
                nextStepLabel.takeIf { it.isNotBlank() }?.let { add("目标：$it") }
            }
        }
        rewards.toRewardLine().takeIf { it.isNotBlank() }?.let { add("奖励：$it") }
    }
}

internal fun buildQuestDetailActions(
    questId: String,
    quest: QuestEntryPacket?,
    trackedQuest: TrackedQuestState = TrackedQuestState(),
): List<ShellAction> {
    return buildList {
        /* 第七轮真机反馈：可接取任务详情只有放弃没有接取——按状态门控：
         * 可接取→接取（无放弃）；进行中/可提交→放弃（可提交另加交付）。 */
        if (quest?.status?.isQuestAcceptableStatus() == true) {
            add(
                ShellAction(
                    "接取",
                    "quest accept $questId",
                    dismissAfterInvoke = false,
                    role = "primary",
                ),
            )
        }
        if (quest?.canTurnIn == true) {
            add(
                ShellAction(
                    "交付",
                    "quest complete $questId",
                    dismissAfterInvoke = false,
                    role = "primary",
                ),
            )
        }
        quest?.toQuestTrackingAction(questId, trackedQuest)?.let(::add)
        quest?.toRecommendedLookAction()?.let(::add)
        if (quest?.status?.isQuestAcceptableStatus() != true) {
            add(
                ShellAction(
                    "放弃",
                    "quest abandon $questId",
                    dismissAfterInvoke = false,
                    role = "danger",
                ),
            )
        }
    }
}

private fun QuestEntryPacket.toRecommendedLookAction(): ShellAction? {
    val command = recommendedCommand.trim()
    if (command != "look" && !command.startsWith("look ")) {
        return null
    }
    return ShellAction(
        label = "观察",
        command = command,
        dismissAfterInvoke = false,
        role = "primary",
    )
}

private fun QuestEntryPacket.toQuestTrackingAction(
    questId: String,
    trackedQuest: TrackedQuestState,
): ShellAction? {
    /* 第七轮真机反馈：canNavigate/targetRoomId 门控让部分任务无法追踪——
     * 追踪只钉住任务（dock 高亮+进度），路线高亮缺目标房间时自然不显示，
     * 不应因此剥夺追踪能力。 */
    val trackingThisQuest = trackedQuest.active && trackedQuest.questId == questId
    return if (trackingThisQuest) {
        ShellAction("取消追踪", UNTRACK_QUEST_COMMAND, dismissAfterInvoke = false, role = "secondary")
    } else {
        ShellAction(
            "追踪",
            "$TRACK_QUEST_PREFIX$questId",
            dismissAfterInvoke = false,
            role = "secondary",
        )
    }
}

internal fun QuestEntryPacket.progressLabel(): String {
    return if (total > 0) "$progress/$total" else ""
}

internal fun QuestUpdatePacket.toQuestEntryPacket(): QuestEntryPacket {
    return QuestEntryPacket(
        questId = questId,
        sourceKind = sourceKind,
        objectiveId = objectiveId,
        acceptanceId = acceptanceId,
        acceptanceRevision = acceptanceRevision,
        autoTrack = autoTrack,
        trackingPriority = trackingPriority,
        name = name,
        status = status,
        progress = progress,
        total = total,
        providerId = providerId,
        providerName = providerName,
        summary = summary,
        rewards = rewards,
        canTurnIn = canTurnIn,
        nextStepLabel = nextStepLabel,
        targetRoomId = targetRoomId,
        targetRoomName = targetRoomName,
        targetEntityId = targetEntityId,
        targetEntityName = targetEntityName,
        recommendedCommand = recommendedCommand,
        canNavigate = canNavigate,
        steps = steps,
    )
}

// 奖励行规格："(名称) (数量)" 中文逗号顿连，铜钱=gold 展示名。
internal fun QuestRewardsPacket.toRewardLine(): String {
    return buildList {
        if (exp > 0) add("经验 $exp")
        if (potential > 0) add("潜能 $potential")
        if (gold > 0) add("铜钱 $gold")
        if (item.isNotBlank()) add(item + if (itemCount > 1) " x$itemCount" else "")
        if (title.isNotBlank()) add("称号 $title")
    }.joinToString("，")
}

internal fun QuestEntryPacket.toQuestFloatingFeedback(): QuestFloatingFeedback? {
    val normalizedQuestId = questId.trim()
    // 第五轮 R8：进度叙事唯一权威是服务端 render_quest_step_text 文本块（主文本区直显），
    // 飘窗仅保留失败态提醒；成功/推进/交付飘窗全部停发。
    if (normalizedQuestId.isBlank() || !status.isQuestFailureStatus()) {
        return null
    }

    // 门控之后只剩失败态：曾在此保留 canTurnIn / 已完成两条不可达分支，
    // 且失败态本身落进「任务推进」文案（语义相反）。此处按唯一可达形态直出。
    val questName = displayName()
    val progressSuffix = progressLabel().takeIf { it.isNotBlank() }?.let { " · 我的进度（$it）" }.orEmpty()
    val secondLine =
        nextStepLabel.trim()
            .takeIf { it.isNotBlank() }
            ?.let { "下一步：$it" }
            .orEmpty()
    val text =
        listOf("任务失败 · $questName$progressSuffix", secondLine)
            .filter { it.isNotBlank() }
            .joinToString("\n")
    return QuestFloatingFeedback(
        text = text,
        ttlMs = 2_600L,
        dedupeKey = "quest:$normalizedQuestId",
        signature = "任务失败|$text",
    )
}

private fun buildNpcQuestSheetItems(actions: List<ShellAction>): List<ShellSheetItem> {
    val questRows =
        actions
            .filter { action -> action.isQuestFlowAction() }
            .groupBy { action -> action.command.questCommandId() }
            .filterKeys { questId -> questId.isNotBlank() }
            .map { (questId, questActions) ->
                val detailAction =
                    questActions.firstOrNull {
                            action ->
                        action.command.isQuestInfoCommand()
                    }
                val primaryAction =
                    questActions.firstOrNull {
                            action ->
                        !action.command.isQuestInfoCommand()
                    }
                val rowAction = detailAction ?: primaryAction ?: questActions.first()
                ShellSheetItem(
                    title = rowAction.questActionTitle(questId),
                    subtitle = "",
                    trailing = primaryAction?.label?.questActionVerb().orEmpty(),
                    command = rowAction.command,
                    actionCommand = primaryAction?.command.orEmpty(),
                    dismissAfterInvoke = false,
                )
            }
    /* 第七轮真机反馈：步骤指引（道谢/交还类 guided，cmd 为 talk/give 等非 quest
     * 命令）不满足 questCommandId 非空，会被上面过滤静默丢弃——单独成行，
     * 点击即执行步骤命令。按命令形状判而非 label：交付/接取等 label 会命中
     * isQuestFlowAction 的 label 兜底分支，若用 !isQuestFlowAction 圈定会
     * 再次吞掉 give 形态的指引按钮。 */
    val guidanceRows =
        actions
            .filter { action -> action.isStepGuidance && action.command.questCommandId().isBlank() }
            .map { action ->
                ShellSheetItem(
                    title = action.label.trim(),
                    subtitle = "",
                    trailing = "",
                    command = action.command,
                    actionCommand = "",
                    dismissAfterInvoke = false,
                )
            }
    return questRows + guidanceRows
}

private fun List<ShellAction>.filterNpcQuestDetailActions(): List<ShellAction> {
    return filterNot { action ->
        action.command.isQuestInfoCommand() ||
            action.label.trim() in setOf("详情", "任务详情") ||
            action.label.trim().startsWith("详情 ")
    }
}

private fun ShellAction.questActionTitle(fallback: String): String {
    return label.trim()
        .removePrefix("详情 ")
        .removePrefix("接取 ")
        .removePrefix("交付 ")
        .removePrefix("恢复 ")
        .ifBlank { fallback }
}

private fun String.questActionVerb(): String {
    val label = trim()
    return when {
        label.startsWith("接取") -> "接取"
        label.startsWith("交付") -> "交付"
        label.startsWith("恢复") -> "恢复"
        label.startsWith("详情") -> ""
        else -> label
    }
}
