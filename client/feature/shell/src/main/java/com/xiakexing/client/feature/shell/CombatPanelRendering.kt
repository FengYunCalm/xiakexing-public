package com.xiakexing.client.feature.shell

internal const val COMBAT_PANEL_TITLE = "战斗"
internal const val OPEN_COMBAT_SHORTCUTS_COMMAND = "ui://panel/combat/shortcuts"
internal const val OPEN_COMBAT_SKILLS_COMMAND = "ui://panel/combat/skills"
internal const val OPEN_COMBAT_PERFORM_PICKER_COMMAND = "ui://panel/combat/perform-picker/"
internal const val OPEN_COMBAT_PERFORM_SKILL_PICKER_COMMAND = "ui://panel/combat/perform-picker-skill/"
internal const val ASSIGN_COMBAT_PERFORM_PREFIX = "ui://panel/combat/assign-perform/"
internal const val REMOVE_COMBAT_PERFORM_PREFIX = "ui://panel/combat/remove-perform/"
internal const val CONFIRM_REMOVE_COMBAT_PERFORM_PREFIX = "ui://panel/combat/remove-perform-confirm/"
internal const val COMBAT_ACTION_PICKER_DIALOG_SOURCE = "combat_action_picker"
internal const val COMBAT_ACTION_SKILL_PICKER_DIALOG_SOURCE = "combat_action_skill_picker"
internal const val COMBAT_PERFORM_SLOT_COUNT = 10
internal const val COMBAT_FLEE_COMMAND = "flee"

internal fun buildActionHubSheetState(
    actions: List<CombatShortcutAction>,
    assignedActionRefs: List<String>,
): ShellSheetState {
    val cards = buildCombatPerformCards(actions, assignedActionRefs)
    return buildGridSheet(
        source = PopupSource.DOCK_BUTTON,
        title = COMBAT_PANEL_TITLE,
        gridMode = ShellPanelMode.COMBAT_GRID,
        panelCards = cards,
        panelEmptyText = "无招式",
        navigation = SheetNavigationMeta(routeKey = "combat"),
        kind = ShellSheetKind.COMBAT,
    )
}

internal fun buildCombatOverlayButtons(
    actions: List<CombatShortcutAction>,
    assignedActionRefs: List<String>,
): List<ShellAction> {
    val performActions =
        buildCombatPerformCards(actions, assignedActionRefs)
            .take(COMBAT_PERFORM_SLOT_COUNT)
            .map { card ->
                ShellAction(
                    label = card.title,
                    command = card.command,
                    dismissAfterInvoke = card.dismissAfterInvoke,
                    supportingLabel = "",
                    longClickCommand = card.longClickCommand,
                )
            }
    return performActions
}

internal fun buildCombatPerformPickerSheetState(
    actions: List<CombatShortcutAction>,
    slotIndex: Int,
    loading: Boolean = false,
): ShellSheetState {
    val groups = actions.toCombatActionSkillGroups()
    val items =
        groups.map { group ->
            ShellSheetItem(
                title = group.skillName.ifBlank { group.skillId },
                subtitle = group.supportingText(),
                trailing = group.level.takeIf { it > 0 }?.let { "等级$it" }.orEmpty(),
                command = "$OPEN_COMBAT_PERFORM_SKILL_PICKER_COMMAND$slotIndex/${group.skillId}",
            )
        }
    return buildInfoListSheet(
        source = PopupSource.DOCK_BUTTON,
        title = "选择武学",
        subtitle = "先选武学，再选动作",
        dialogId = slotIndex.toString(),
        dialogSource = COMBAT_ACTION_PICKER_DIALOG_SOURCE,
        items = items,
        panelEmptyText = "无动作",
        loading = loading,
        navigation =
            SheetNavigationMeta(
                routeKey = "combat/perform/$slotIndex",
                depth = 1,
            ),
        kind = ShellSheetKind.COMBAT,
    )
}

internal fun buildCombatPerformSkillPickerSheetState(
    actions: List<CombatShortcutAction>,
    slotIndex: Int,
    skillId: String,
    loading: Boolean = false,
): ShellSheetState {
    val normalizedSkillId = skillId.trim()
    val skillActions =
        actions
            .filter { action -> action.skillId == normalizedSkillId }
            .sortedWith(
                compareByDescending<CombatShortcutAction> { if (it.available) 1 else 0 }
                    .thenByDescending { it.level }
                    .thenBy { it.label },
            )
    val skillName = skillActions.firstOrNull()?.skillName.orEmpty().ifBlank { normalizedSkillId }
    val items =
        skillActions.map { action ->
            ShellSheetItem(
                title = action.label,
                subtitle = action.supportingText(),
                trailing = action.skillName,
                command = "$ASSIGN_COMBAT_PERFORM_PREFIX$slotIndex/${action.ref}",
            )
        }
    return buildInfoListSheet(
        source = PopupSource.DOCK_BUTTON,
        title = skillName.ifBlank { "选择动作" },
        subtitle = "选择动作",
        dialogId = "$slotIndex/$normalizedSkillId",
        dialogSource = COMBAT_ACTION_SKILL_PICKER_DIALOG_SOURCE,
        items = items,
        panelEmptyText = "这门武学当前没有动作",
        loading = loading,
        navigation =
            SheetNavigationMeta(
                routeKey = "combat/perform/$slotIndex/skill/$normalizedSkillId",
                depth = 2,
            ),
        kind = ShellSheetKind.COMBAT,
    )
}

internal fun buildDungeonPopup(isInDungeonRoom: Boolean): ShellPopupState {
    val message = if (isInDungeonRoom) "退出当前副本？" else "进入$WRECK_HOLD_DUNGEON_NAME？"
    val action =
        if (isInDungeonRoom) {
            ShellAction("退出", "dungeon leave", dismissAfterInvoke = true)
        } else {
            ShellAction("进入", WRECK_HOLD_ENTER_COMMAND, dismissAfterInvoke = true)
        }
    return buildDockPopup(
        title = "副本",
        message = message,
        actions =
            listOf(
                action,
                ShellAction("取消", ACTION_CONFIRMATION_CANCEL_COMMAND, dismissAfterInvoke = true),
            ),
        primaryLabel = "",
        kind = ShellPopupKind.CONFIRMATION,
    )
}

private data class CombatActionSkillGroup(
    val skillId: String,
    val skillName: String,
    val level: Int,
    val actionCount: Int,
    val availableCount: Int,
    val actionTypes: List<String>,
)

private fun List<CombatShortcutAction>.toCombatActionSkillGroups(): List<CombatActionSkillGroup> {
    return groupBy { action -> action.skillId }
        .values
        .mapNotNull { skillActions ->
            val first = skillActions.firstOrNull() ?: return@mapNotNull null
            CombatActionSkillGroup(
                skillId = first.skillId,
                skillName = first.skillName,
                level = skillActions.maxOf { action -> action.level },
                actionCount = skillActions.size,
                availableCount = skillActions.count { action -> action.available },
                actionTypes =
                    skillActions
                        .map {
                                action ->
                            action.ref.combatShortcutActionType().combatShortcutActionTypeLabel()
                        }
                        .distinct(),
            )
        }
        .sortedWith(
            compareByDescending<CombatActionSkillGroup> { it.level }.thenBy { it.skillName },
        )
}

private fun CombatActionSkillGroup.supportingText(): String {
    val lockedCount = actionCount - availableCount
    return buildString {
        append("${actionCount}个动作")
        if (availableCount > 0) {
            append("，${availableCount}个可用")
        }
        if (lockedCount > 0) {
            append("，${lockedCount}个未达条件")
        }
        actionTypes.joinToString("/").takeIf { it.isNotBlank() }?.let { typeLabel ->
            append("，$typeLabel")
        }
    }
}

private fun String.combatShortcutActionTypeLabel(): String {
    return when (trim().lowercase()) {
        COMBAT_SHORTCUT_EXERT -> "内功"
        else -> "招式"
    }
}

private fun buildCombatPerformCards(
    actions: List<CombatShortcutAction>,
    assignedActionRefs: List<String>,
): List<ShellPanelCard> {
    val actionsByRef = actions.associateBy { action -> action.ref }
    val validAssignedSlots =
        assignedActionRefs
            .take(COMBAT_PERFORM_SLOT_COUNT)
            .map { assignedRef ->
                assignedRef.toCombatShortcutActionRefOrBlank()
                    .takeIf { actionRef -> actionRef in actionsByRef }
                    .orEmpty()
            }
    return List(COMBAT_PERFORM_SLOT_COUNT) { index ->
        val action =
            validAssignedSlots.getOrNull(index)?.let { actionRef ->
                actionsByRef[actionRef]
            }
        if (action == null) {
            ShellPanelCard(
                id = "perform-slot-${index + 1}",
                title = "",
                bottomLabel = "",
                command = "$OPEN_COMBAT_PERFORM_PICKER_COMMAND$index",
                dismissAfterInvoke = false,
            )
        } else {
            ShellPanelCard(
                id = action.ref,
                title = action.label,
                bottomLabel = "",
                command = action.command,
                dismissAfterInvoke = false,
                longClickCommand = "$REMOVE_COMBAT_PERFORM_PREFIX${action.removeToken}",
            )
        }
    }
}
