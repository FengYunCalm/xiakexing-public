package com.xiakexing.client.feature.shell.popup

import com.xiakexing.client.feature.shell.InteractionObjectKind
import com.xiakexing.client.feature.shell.InteractionSheetMode
import com.xiakexing.client.feature.shell.OPEN_INTERACTION_GIVE_ITEMS_COMMAND
import com.xiakexing.client.feature.shell.PopupSource
import com.xiakexing.client.feature.shell.ShellAction
import com.xiakexing.client.feature.shell.ShellActionGroup
import com.xiakexing.client.feature.shell.ShellSheetState
import com.xiakexing.client.feature.shell.isStepGuidance

internal fun ShellSheetState.isNpcRootMode(): Boolean {
    return activeActionGroupId.ifBlank { "root" } == "root" &&
        interactionMode == InteractionSheetMode.DETAIL
}

internal data class NpcRootActionSections(
    val baseActions: List<ShellAction>,
    val topicActions: List<ShellAction>,
    val guidanceActions: List<ShellAction>,
    val extensionActions: List<ShellAction>,
) {
    val isEmpty: Boolean
        get() =
            baseActions.isEmpty() && topicActions.isEmpty() &&
                guidanceActions.isEmpty() && extensionActions.isEmpty()
}

internal fun ShellSheetState.npcRootActionSections(): NpcRootActionSections {
    if (source != PopupSource.INTERACTION_OBJECT || interactionTargetKind != InteractionObjectKind.NPC) {
        return NpcRootActionSections(emptyList(), emptyList(), emptyList(), emptyList())
    }
    val allActions = actionGroups.flatMap { group -> group.actions } + actions
    val topicActions =
        allActions
            .filter { action -> action.isNpcConversationTopicAction() }
            .dedupeNpcRootActions()
    val topicCommands =
        topicActions
            .map { action -> action.command.trim().lowercase() }
            .filter { it.isNotBlank() }
            .toSet()
    // 当前步骤独立动作置于常驻动作上方；交物仅走固定给予入口。
    val guidanceActions =
        allActions
            .filter { action -> action.isStepGuidance && !action.isNpcConversationTopicAction() }
            .filterNot { action -> action.command.trim().lowercase() in topicCommands }
            .dedupeNpcRootActions()
    val extensionActions =
        allActions
            .filterNot { action -> action.isNpcConversationTopicAction() }
            .filterNot { action -> action.isStepGuidance }
            .filterNot { action -> action.isNpcBaseAction() }
            .filterNot { action -> action.command.trim().lowercase() in topicCommands }
            .dedupeNpcRootActions()
    return NpcRootActionSections(
        baseActions = buildNpcBaseActions(this),
        topicActions = topicActions,
        guidanceActions = guidanceActions,
        extensionActions = extensionActions,
    )
}

internal fun ShellSheetState.npcSpecialActions(): List<ShellAction> {
    return npcRootActionSections().extensionActions
}

private fun ShellAction.isNpcBaseAction(): Boolean {
    val normalized = command.trim().lowercase()
    val normalizedLabel = label.trim()
    return normalized.startsWith("talk ") ||
        normalized.startsWith("give ") ||
        normalized.startsWith("fight ") ||
        normalized.startsWith("kill ") ||
        normalized.startsWith("quest") ||
        normalized == "learn list" ||
        normalized.startsWith("learn list ") ||
        normalized == OPEN_INTERACTION_GIVE_ITEMS_COMMAND ||
        normalized == "view" ||
        normalized == "look" ||
        normalized.startsWith("look ") ||
        normalized.startsWith("ask ") ||
        normalizedLabel in setOf("任务", "技能", "武学", "给予", "切磋", "攻击", "战斗")
}

private fun ShellAction.isNpcConversationTopicAction(): Boolean {
    val normalized = command.trim().lowercase()
    return normalized.startsWith("ask ") ||
        targetGroup.trim().lowercase() == "conversation" ||
        surface.trim().lowercase() == "conversation"
}

private fun List<ShellAction>.dedupeNpcRootActions(): List<ShellAction> {
    val seenCommands = linkedSetOf<String>()
    return filter { action ->
        val key = action.command.trim().lowercase().ifBlank { action.label.trim() }
        key.isNotBlank() && seenCommands.add(key)
    }
}

internal fun buildNpcBaseActions(sheet: ShellSheetState): List<ShellAction> {
    val targetId = sheet.interactionTargetId.ifBlank { sheet.title }.trim()
    val questCommand = if (targetId.isBlank()) "quest" else "quest $targetId"
    val learnCommand = if (targetId.isBlank()) "learn list" else "learn list $targetId"
    val giveAction =
        ShellAction(
            label = "给予",
            command = OPEN_INTERACTION_GIVE_ITEMS_COMMAND,
            dismissAfterInvoke = false,
            kind = "give",
            surface = "root",
            role = "secondary",
            targetGroup = "give",
        )
    // #2 交谈标题化：不合成"交谈"按钮，协议 conversation 话题（如阿禾"失物"）
    // 由 topicActions 直接提供，点击走 ask 话题路径（关闭弹窗回主文本区）。
    return listOf(
        ShellAction(
            label = "任务",
            command = questCommand,
            dismissAfterInvoke = false,
            kind = "quest",
            surface = "root",
            role = "secondary",
            targetGroup = "quest",
        ),
        ShellAction(
            label = "技能",
            command = learnCommand,
            dismissAfterInvoke = false,
            kind = "learn",
            surface = "root",
            role = "secondary",
            targetGroup = "learn",
        ),
        giveAction,
        ShellAction(
            label = "切磋",
            command = if (targetId.isBlank()) "fight" else "fight $targetId",
            dismissAfterInvoke = false,
            kind = "fight",
            surface = "root",
            role = "secondary",
            targetGroup = "root",
        ),
        ShellAction(
            label = "攻击",
            command = if (targetId.isBlank()) "kill" else "kill $targetId",
            dismissAfterInvoke = false,
            kind = "fight",
            surface = "root",
            role = "danger",
            targetGroup = "root",
        ),
    )
}

internal fun ShellSheetState.visibleInteractionActionGroups(): List<ShellActionGroup> {
    if (source != PopupSource.INTERACTION_OBJECT || actionGroups.isEmpty()) {
        return emptyList()
    }
    val activeGroup = activeActionGroupId.ifBlank { "root" }
    val visibleGroups =
        actionGroups
            .filter { group -> group.id == activeGroup || group.surface == activeGroup }
            .filter { group -> group.actions.isNotEmpty() }
    if (activeGroup == "root") {
        return visibleGroups
    }
    return visibleGroups
}

internal fun ShellAction.interactionShortLabelForDisplay(): String {
    val normalizedLabel = label.trim()
    return normalizedLabel.removePrefix("询问").trim()
        .ifBlank { askTopicDisplay() }
        .ifBlank { "话题" }
}

private fun ShellAction.askTopicDisplay(): String {
    val topic = command.substringAfter(" about ", missingDelimiterValue = "").trim()
    return when (topic.lowercase()) {
        "quest" -> "任务"
        "road", "route" -> "路线"
        else -> topic
    }
}
