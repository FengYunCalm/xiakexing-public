package com.xiakexing.client.feature.shell

import com.xiakexing.client.core.protocol.payload.DialogActionGroupPacket
import com.xiakexing.client.core.protocol.payload.DialogActionPacket
import com.xiakexing.client.core.protocol.payload.DialogPacket

internal const val OPEN_INTERACTION_GIVE_ITEMS_COMMAND = "ui://interaction/give-items"
internal const val OPEN_INTERACTION_OBJECT_ROOT_COMMAND = "ui://interaction/object-root"
internal const val OPEN_INTERACTION_ROOT_COMMAND = "ui://interaction/root"
internal const val OPEN_INTERACTION_APPRENTICE_UNAVAILABLE_COMMAND = "ui://interaction/apprentice-unavailable"
internal const val OPEN_INTERACTION_UNAVAILABLE_ACTION_PREFIX = "ui://interaction/unavailable/"

internal fun InteractionObject.commandTarget(): String {
    return if (kind == InteractionObjectKind.ITEM && id.startsWith(ROOM_ITEM_INTERACTION_PREFIX)) {
        id.removePrefix(ROOM_ITEM_INTERACTION_PREFIX)
    } else {
        id
    }
}

internal fun InteractionObject.scopedCommandTarget(): String {
    return if (kind == InteractionObjectKind.ITEM && id.startsWith(ROOM_ITEM_INTERACTION_PREFIX)) {
        id
    } else {
        commandTarget()
    }
}

internal fun ShellAction.withRoomItemTarget(target: InteractionObject?): ShellAction {
    if (target?.kind != InteractionObjectKind.ITEM || !target.id.startsWith(ROOM_ITEM_INTERACTION_PREFIX)) {
        return this
    }
    if (!command.trim().equals("get ${target.commandTarget()}", ignoreCase = true)) {
        return this
    }
    return copy(command = "get ${target.scopedCommandTarget()}")
}

internal fun String.commandTargetFromInteractionId(): String {
    return removePrefix(ROOM_ITEM_INTERACTION_PREFIX)
}

internal fun String.routingToken(): String {
    return trim().lowercase()
}

internal fun String.npcCommandToken(): String {
    return trim().lowercase().replace('_', ' ').replace(Regex("\\s+"), " ")
}

internal fun String.isNpcDialogSource(): Boolean {
    return startsWith("npc_") ||
        this == "quest"
}

internal fun InteractionObject.rootActionsOrFallback(): List<ShellAction> {
    return actionGroups.firstOrNull { it.surface == "root" || it.id == "root" }?.actions
        ?.takeIf { it.isNotEmpty() }
        ?: emptyList()
}

internal fun ShellUiState.hasGiveableInventoryItems(): Boolean {
    return inventoryItems.any { item ->
        item.canGive && item.location != "room" && item.id.ifBlank { item.name }.isNotBlank()
    }
}

internal fun List<ShellActionGroup>.filterUsefulNpcActionGroups(
    hasGiveableItems: Boolean,
): List<ShellActionGroup> {
    return map { group ->
        group.copy(actions = group.actions.filterUsefulNpcActions(hasGiveableItems))
    }
}

internal fun List<ShellAction>.filterUsefulNpcActions(
    hasGiveableItems: Boolean,
): List<ShellAction> {
    return filter { action -> action.isUsefulNpcAction(hasGiveableItems) }
}

internal fun ShellSheetState.fixedNpcPrimaryActions(targetId: String): List<ShellAction> {
    val candidates = actionGroups.flatMap { group -> group.actions } + actions
    return fixedNpcActionSpecs.map { spec ->
        val action = candidates.firstOrNull(spec.matches)
        val actionCommand = action?.command?.trim().orEmpty()
        val command =
            when {
                spec.localCommand != null -> spec.localCommand.invoke(targetId)
                actionCommand.isNotBlank() -> actionCommand
                spec.requiresBackendAction -> "${OPEN_INTERACTION_UNAVAILABLE_ACTION_PREFIX}${spec.unavailableActionKey}"
                else -> spec.fallbackCommand(targetId)
            }
        (
            action?.takeIf { actionCommand.isNotBlank() } ?: ShellAction(
                label = spec.label,
                command = command,
                dismissAfterInvoke = false,
            )
        ).copy(
            label = spec.label,
            command = command,
            dismissAfterInvoke = false,
        )
    }
}

internal fun unavailableInteractionActionNotice(actionKey: String): String {
    return when (actionKey.trim().lowercase()) {
        "fight" -> "这个人不适合切磋"
        "kill" -> "这个人不适合攻击"
        else -> "当前对象不支持这个操作"
    }
}

/* 对象详情页有意全组拍平（不经 visibleActions 组裁剪）：详情面天然展示
 * 全部协议动作，guided 自动可见。勿按「统一走 visibleActions」重构此处——
 * 那会把组作用域裁剪带回，复刻道谢式吞按钮方向。 */
internal fun buildObjectDetailActions(
    target: InteractionObject?,
    packetActionGroups: List<ShellActionGroup>,
): List<ShellAction> {
    val protocolActions =
        mergeActionGroups(target?.actionGroups.orEmpty(), packetActionGroups)
            .flatMap { group -> group.actions }
            .filterNot { action -> action.isLookOrViewAction() }
            .filterNot { action -> target.isInventoryOnlyAction(action) }
            .map { action -> action.withRoomItemTarget(target) }
    return mergeSheetActions(protocolActions)
}

private data class FixedNpcActionSpec(
    val label: String,
    val fallbackCommand: (String) -> String,
    val matches: (ShellAction) -> Boolean,
    val localCommand: ((String) -> String)? = null,
    val requiresBackendAction: Boolean = false,
    val unavailableActionKey: String = "",
)

private val fixedNpcActionSpecs =
    listOf(
        // #2 交谈标题化：不再合成"交谈"按钮（协议 conversation 话题直接显示）。
        FixedNpcActionSpec(
            label = "任务",
            fallbackCommand = {
                    targetId ->
                if (targetId.isBlank()) "quest" else "quest $targetId"
            },
            matches = { action ->
                val command = action.command.trim().lowercase()
                val kind = action.kind.trim().lowercase()
                val surface = action.surface.trim().lowercase()
                val targetGroup = action.targetGroup.trim().lowercase()
                // 第五轮 R3：指引动作不参与固定槽位匹配（避免劫持任务/给予槽）
                !action.guided && (
                    command.startsWith("quest") ||
                        kind == "quest" ||
                        surface == "quest" ||
                        targetGroup == "quest" ||
                        (action.label.trim() == "任务" && !command.startsWith("ask "))
                )
            },
        ),
        FixedNpcActionSpec(
            label = "技能",
            fallbackCommand = {
                    targetId ->
                if (targetId.isBlank()) "learn list" else "learn list $targetId"
            },
            matches = { action ->
                val command = action.command.trim().lowercase()
                val kind = action.kind.trim().lowercase()
                val surface = action.surface.trim().lowercase()
                val targetGroup = action.targetGroup.trim().lowercase()
                val label = action.label.trim()
                command.startsWith("learn list") ||
                    (
                        !command.startsWith("ask ") &&
                            !command.contains(" from ") &&
                            (kind == "learn" || surface == "learn" || targetGroup == "learn" || label in setOf("技能", "武学"))
                    )
            },
        ),
        FixedNpcActionSpec(
            label = "给予",
            fallbackCommand = { OPEN_INTERACTION_GIVE_ITEMS_COMMAND },
            matches = { action ->
                action.isGiveAction ||
                    action.command.trim() == OPEN_INTERACTION_GIVE_ITEMS_COMMAND ||
                    action.label.trim() == "给予"
            },
            localCommand = { OPEN_INTERACTION_GIVE_ITEMS_COMMAND },
        ),
        FixedNpcActionSpec(
            label = "切磋",
            fallbackCommand = {
                    targetId ->
                if (targetId.isBlank()) "fight" else "fight $targetId"
            },
            matches = {
                    action ->
                action.command.trim().lowercase().startsWith("fight") || action.label.trim() == "切磋"
            },
            requiresBackendAction = true,
            unavailableActionKey = "fight",
        ),
        FixedNpcActionSpec(
            label = "攻击",
            fallbackCommand = { targetId -> if (targetId.isBlank()) "kill" else "kill $targetId" },
            matches = { action ->
                val command = action.command.trim().lowercase()
                command.startsWith("kill") || action.label.trim() in setOf("攻击", "战斗")
            },
            requiresBackendAction = true,
            unavailableActionKey = "kill",
        ),
    )

private fun ShellAction.isUsefulNpcAction(hasGiveableItems: Boolean): Boolean {
    val normalized = command.trim().lowercase()
    if (normalized.isBlank()) {
        return false
    }
    if (isStepGuidance) {
        return true
    }
    if (this.isHelpConversationAction() || this.isNpcLookAction()) {
        return false
    }
    if (isGiveAction || normalized == OPEN_INTERACTION_GIVE_ITEMS_COMMAND) {
        return hasGiveableItems
    }
    return true
}

private fun ShellAction.isHelpConversationAction(): Boolean {
    val normalized = command.trim().lowercase()
    if (!normalized.startsWith("ask ")) {
        return false
    }
    val topic = normalized.substringAfter(" about ", missingDelimiterValue = "").trim()
    val normalizedLabel = label.trim()
    return topic in setOf("help", "newbie", "intro", "talk", "帮助", "新手", "介绍", "对话") ||
        normalizedLabel in setOf("询问帮助", "帮助", "询问")
}

private fun ShellAction.isNpcLookAction(): Boolean {
    return isLookOrViewAction()
}

internal fun ShellAction.isLookOrViewAction(): Boolean {
    val normalized = command.trim().lowercase()
    val normalizedKind = kind.trim().lowercase()
    val normalizedLabel = label.trim()
    return normalized == "view" ||
        normalized == "look" ||
        normalized.startsWith("look ") ||
        normalizedKind == "look" ||
        normalizedKind == "view" ||
        normalizedLabel == "查看"
}

internal fun ShellAction.isGetAction(): Boolean {
    val normalized = command.trim().lowercase()
    val normalizedKind = kind.trim().lowercase()
    return normalized.startsWith("get ") ||
        normalizedKind == "get" ||
        label.trim() == "拾取"
}

private fun InteractionObject?.isInventoryOnlyAction(action: ShellAction): Boolean {
    val target = this ?: return false
    if (target.kind != InteractionObjectKind.ITEM || !target.id.startsWith(ROOM_ITEM_INTERACTION_PREFIX)) {
        return false
    }
    val normalized = action.command.trim().lowercase()
    val normalizedKind = action.kind.trim().lowercase()
    return normalized.startsWith("drop ") ||
        normalized.startsWith("equip ") ||
        normalized.startsWith("unequip ") ||
        normalized.startsWith("eat ") ||
        normalized.startsWith("drink ") ||
        normalizedKind in setOf("drop", "equip", "unequip", "eat", "drink") ||
        action.label.trim() in setOf("丢弃", "装备", "卸下", "使用", "饮用")
}

internal fun ShellSheetState.allSheetActions(): List<ShellAction> {
    return actions + actionGroups.flatMap { group -> group.actions }
}

internal fun List<DialogActionGroupPacket>.toShellActionGroups(
    actionTarget: String,
): List<ShellActionGroup> {
    return map { group ->
        ShellActionGroup(
            id = group.id,
            title = group.title,
            surface = group.surface.ifBlank { group.id },
            actions =
                group.actions.mapNotNull { action ->
                    action.toShellActionOrNull(actionTarget, group.surface.ifBlank { group.id })
                },
        )
    }
}

internal fun DialogActionPacket.toShellActionOrNull(
    actionTarget: String = "",
    fallbackSurface: String = "",
): ShellAction? {
    val normalizedCommand = command.trim()
    val normalizedLabel = label.trim()
    if (normalizedCommand.isBlank() && normalizedLabel.isBlank()) {
        return null
    }
    return ShellAction(
        label = normalizedLabel.ifBlank { normalizedCommand },
        command = normalizedCommand.withNpcInteractionCommandTarget(actionTarget),
        dismissAfterInvoke = !normalizedCommand.shouldRemainInInteractionSession(),
        kind = kind,
        surface = surface.ifBlank { fallbackSurface },
        role = role,
        targetGroup = targetGroup,
        // 第五轮 R3：主转换路径必须携带标记，否则豁免守卫失效
        guided = guided,
    )
}

internal fun String.withNpcInteractionCommandTarget(actionTarget: String): String {
    if (actionTarget.isBlank()) {
        return this
    }
    val command = trim()
    val normalized = command.lowercase()
    return when {
        normalized.startsWith("talk ") -> "talk $actionTarget"
        normalized == "look" || normalized == "view" -> "look $actionTarget"
        normalized.startsWith("look ") -> "look $actionTarget"
        normalized.startsWith("get ") -> "get $actionTarget"
        normalized.startsWith("fight ") -> "fight $actionTarget"
        normalized.startsWith("kill ") -> "kill $actionTarget"
        normalized.startsWith("ask ") -> {
            val aboutIndex = normalized.indexOf(" about ")
            if (aboutIndex >= 0) {
                "ask $actionTarget" + command.substring(aboutIndex)
            } else {
                "ask $actionTarget"
            }
        }
        normalized.startsWith("quest ") -> {
            val rest = command.substringAfter(" ", "").trim()
            if (rest.isNotBlank() && !rest.contains(" ")) "quest $actionTarget" else this
        }
        normalized.startsWith("learn list ") -> "learn list $actionTarget"
        normalized.startsWith("learn ") && normalized.contains(" from ") -> {
            command.substringBefore(" from ") + " from $actionTarget"
        }
        else -> this
    }
}

internal fun mergeActionGroups(vararg groupSets: List<ShellActionGroup>): List<ShellActionGroup> {
    val merged = linkedMapOf<String, ShellActionGroup>()
    groupSets.forEach { groups ->
        groups.forEach { group ->
            val key = group.surface.ifBlank { group.id }
            merged[key] = group
        }
    }
    return merged.values.filter { it.actions.isNotEmpty() }
}

internal fun List<ShellActionGroup>.visibleActions(activeGroupId: String): List<ShellAction> {
    val normalizedActiveGroup = activeGroupId.ifBlank { "root" }
    val active = visibleGroups(normalizedActiveGroup).flatMap { group -> group.actions }
    // 独立步骤动作跨组可见；交物动作由固定给予入口承载。
    val guided =
        asSequence()
            .flatMap { group -> group.actions.asSequence() }
            .filter { action -> action.isStepGuidance }
            .filter {
                    candidate ->
                active.none { it.command.equals(candidate.command, ignoreCase = true) }
            }
            .toList()
    return active + guided
}

internal fun List<ShellActionGroup>.visibleGroups(activeGroupId: String): List<ShellActionGroup> {
    val normalizedActiveGroup = activeGroupId.ifBlank { "root" }
    return filter { group -> group.matchesActionGroup(normalizedActiveGroup) }
}

private fun ShellActionGroup.matchesActionGroup(groupId: String): Boolean {
    return id == groupId || surface == groupId
}

internal fun String.toInteractionActionGroupId(currentGroupId: String): String {
    return when {
        startsWith("talk ") || startsWith("ask ") -> "conversation"
        startsWith("quest") -> "quest"
        startsWith("learn ") && !isNpcLearnListCommand() -> "learn"
        startsWith("buy ") || startsWith("sell ") || startsWith("trade ") || this == "trade" || this == "list" -> "trade"
        startsWith("steal ") || startsWith("listen ") -> "root"
        startsWith("fight ") || startsWith("kill ") -> "root"
        else -> currentGroupId.ifBlank { "root" }
    }
}

internal fun DialogPacket.preferredInteractionActionGroup(
    currentGroupId: String,
    groups: List<ShellActionGroup>,
): String {
    val preferred =
        when (dialogBucket.trim().lowercase()) {
            "quest", "progress" -> "quest"
            "talk", "help", "intro" -> "conversation"
            "skills" -> "learn"
            "goods" -> "trade"
            else -> currentGroupId.ifBlank { "root" }
        }
    return if (groups.any { group -> group.matchesActionGroup(preferred) }) {
        preferred
    } else {
        currentGroupId.ifBlank { "root" }
    }
}

/* 对话类动作（交谈 / 询问话题）。任务面板不承载对话流：guided 步骤指引会
 * 跨组追加到任何激活组，接取 talk/ask 步骤的任务后，任务列表底部会冒出
 * 「交谈」「询问任务链」。任务面板按此判据剔除这些动作。 */
internal fun ShellAction.isNpcConversationGuidanceAction(): Boolean {
    val normalized = command.trim().lowercase()
    return kind.trim().lowercase() in setOf("talk", "ask") ||
        normalized.startsWith("talk ") ||
        normalized.startsWith("ask ")
}

internal fun mergeSheetActions(vararg groups: List<ShellAction>): List<ShellAction> {
    val merged = linkedMapOf<String, ShellAction>()
    groups.forEach { actions ->
        actions.forEach { action ->
            val key = action.command.trim().lowercase()
            if (key.isNotBlank() && key !in merged) {
                merged[key] = action
            }
        }
    }
    return merged.values.toList()
}

internal fun String.shouldStayInInteractionSheet(): Boolean {
    return trim().lowercase().isInteractionResidentCommand()
}

internal fun String.shouldRemainInInteractionSession(): Boolean {
    val normalized = trim().lowercase()
    return normalized == OPEN_INTERACTION_GIVE_ITEMS_COMMAND ||
        normalized == "trade" ||
        normalized.startsWith("buy ") ||
        normalized.startsWith("sell ") ||
        normalized == "list" ||
        normalized.isInteractionResidentCommand()
}

/** 交互命令驻留判定单一真源（sheet 内分派与交互会话驻留共用，入参须已 trim+lowercase）。 */
private fun String.isInteractionResidentCommand(): Boolean {
    return isLookInteractionCommand() ||
        startsWith("get ") ||
        startsWith("ask ") ||
        startsWith("talk ") ||
        startsWith("quest") ||
        startsWith("give ") ||
        startsWith("learn ") ||
        this == "trade" ||
        startsWith("trade ") ||
        this == "fight" ||
        startsWith("fight ") ||
        this == "kill" ||
        startsWith("kill ")
}

internal fun String.isLookInteractionCommand(): Boolean {
    val normalized = trim().lowercase()
    return normalized == "look" ||
        normalized == "view" ||
        normalized.startsWith("look ")
}

internal fun String.isLearnSkillCommand(): Boolean {
    val normalized = trim().lowercase()
    return normalized.startsWith("learn ") &&
        normalized.contains(" from ") &&
        !normalized.startsWith("learn list ")
}

internal fun String.isNpcLearnListCommand(): Boolean {
    val normalized = trim().lowercase()
    return normalized == "learn list" || normalized.startsWith("learn list ")
}

internal fun String.learnSkillIdFromCommand(): String {
    val command = trim()
    if (!command.isLearnSkillCommand()) {
        return command
    }
    return command.removePrefix("learn ").substringBefore(" from ").trim()
}
