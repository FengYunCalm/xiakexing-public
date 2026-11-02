package com.xiakexing.client.feature.shell

internal const val OPEN_LEARNING_SKILL_CATEGORY_PREFIX = "ui://interaction/learn/category/"

internal fun buildInteractionObjectSheetState(
    target: InteractionObject,
    detailLines: List<String>,
    actions: List<ShellAction>,
    actionGroups: List<ShellActionGroup>,
): ShellSheetState {
    return buildDetailSheet(
        source = PopupSource.INTERACTION_OBJECT,
        title = target.title,
        detailLines = detailLines,
        actions = actions,
        actionGroups = actionGroups,
        activeActionGroupId = "root",
        interactionTargetId = target.id,
        interactionTargetUiKey = target.uiKey,
        interactionTargetKind = target.kind,
        loading = false,
        navigation =
            SheetNavigationMeta(
                routeKey = "interaction/${target.kind.name.lowercase()}/${target.uiKey.ifBlank { target.id }}",
            ),
    )
}

internal fun buildInteractionProtocolSheetState(
    source: PopupSource,
    title: String,
    dialogId: String,
    dialogSource: String,
    detailLines: List<String>,
    interactionLines: List<String>,
    actions: List<ShellAction>,
    actionGroups: List<ShellActionGroup>,
    activeActionGroupId: String,
    interactionTargetId: String,
    interactionTargetUiKey: String = "",
    interactionTargetKind: InteractionObjectKind?,
    interactionMode: InteractionSheetMode,
    compactPanel: Boolean = false,
): ShellSheetState {
    val routeSegment = if (compactPanel) "detail" else activeActionGroupId.ifBlank { "detail" }
    return buildDetailSheet(
        source = source,
        title = title,
        dialogId = dialogId,
        dialogSource = dialogSource,
        detailLines = detailLines,
        interactionLines = interactionLines,
        actions = actions,
        actionGroups = actionGroups,
        activeActionGroupId = activeActionGroupId,
        interactionTargetId = interactionTargetId,
        interactionTargetUiKey = interactionTargetUiKey,
        interactionTargetKind = interactionTargetKind,
        interactionMode = interactionMode,
        compactPanel = compactPanel,
        loading = false,
        navigation =
            SheetNavigationMeta(
                routeKey =
                    "interaction/${interactionTargetKind?.name?.lowercase().orEmpty()}/" +
                        "${interactionTargetUiKey.ifBlank { interactionTargetId }}/$routeSegment",
                // 第五轮 R6：场景物品/场景根视图无可回退层级，恒 depth=0（无返回箭头）；
                // NPC 二级流保持原判定。
                depth =
                    if (source == PopupSource.INTERACTION_OBJECT &&
                        interactionTargetKind != InteractionObjectKind.ITEM &&
                        interactionTargetKind != InteractionObjectKind.SCENE &&
                        (compactPanel || activeActionGroupId != "root")
                    ) {
                        1
                    } else {
                        0
                    },
            ),
    )
}

internal fun buildShopSheetState(
    source: PopupSource,
    title: String,
    detailLines: List<String>,
    interactionLines: List<String>,
    shopCards: List<ShellPanelCard>,
    interactionTargetId: String,
    interactionTargetUiKey: String = "",
    interactionTargetKind: InteractionObjectKind?,
): ShellSheetState {
    return buildGridSheet(
        source = source,
        title = title,
        gridMode = ShellPanelMode.SHOP_GRID,
        detailLines = detailLines,
        interactionLines = interactionLines,
        primaryAction = ShellAction(label = "刷新", command = "list", dismissAfterInvoke = false),
        panelCards = shopCards,
        panelEmptyText = "无货品",
        interactionTargetId = interactionTargetId,
        interactionTargetUiKey = interactionTargetUiKey,
        interactionTargetKind = interactionTargetKind,
        loading = false,
        navigation =
            SheetNavigationMeta(
                routeKey =
                    "interaction/${interactionTargetKind?.name?.lowercase().orEmpty()}/" +
                        "${interactionTargetUiKey.ifBlank { interactionTargetId }}/shop",
                depth = if (source == PopupSource.INTERACTION_OBJECT) 1 else 0,
            ),
    )
}

internal fun buildNpcLearningSheetState(
    dialogId: String,
    dialogSource: String,
    skillCards: List<ShellPanelCard>,
    targetId: String,
    targetName: String,
    targetKind: InteractionObjectKind,
): ShellSheetState {
    val categories = buildLearningSkillCategories(skillCards)
    val selectedCategory = categories.firstOrNull()
    return buildGridSheet(
        source = PopupSource.INTERACTION_LEARN,
        title = "技能",
        showHeaderTitle = false,
        subtitle = targetName,
        gridMode = ShellPanelMode.SKILL_GRID,
        dialogId = dialogId,
        dialogSource = dialogSource,
        panelTabs =
            categories.map { category ->
                ShellAction(
                    label = category.label,
                    command = "$OPEN_LEARNING_SKILL_CATEGORY_PREFIX${category.id}",
                    dismissAfterInvoke = false,
                )
            },
        panelSelectedTabId = selectedCategory?.id.orEmpty(),
        panelCards = selectedCategory?.cards.orEmpty(),
        panelAllCards = skillCards,
        panelEmptyText = "此人当前没有可以传授给你的技能",
        interactionTargetId = targetId,
        interactionTargetKind = targetKind,
        loading = false,
        navigation =
            SheetNavigationMeta(
                routeKey = "interaction/${targetKind.name.lowercase()}/$targetId/learn",
                depth = 1,
            ),
    )
}

internal fun ShellSheetState.selectLearningSkillCategory(categoryId: String): ShellSheetState {
    val categories = buildLearningSkillCategories(panelAllCards.ifEmpty { panelCards })
    val selectedCategory =
        categories.firstOrNull { it.id == categoryId }
            ?: categories.firstOrNull()
    return copy(
        panelSelectedTabId = selectedCategory?.id.orEmpty(),
        panelCards = selectedCategory?.cards.orEmpty(),
    )
}

private data class LearningSkillCategory(
    val id: String,
    val label: String,
    val cards: List<ShellPanelCard>,
)

private fun buildLearningSkillCategories(cards: List<ShellPanelCard>): List<LearningSkillCategory> {
    if (cards.isEmpty()) {
        return emptyList()
    }
    val groupedCards = linkedMapOf<String, MutableList<ShellPanelCard>>()
    groupedCards[DEFAULT_SKILL_PANEL_CATEGORY] = cards.toMutableList()
    cards.forEach { card ->
        groupedCards.getOrPut(card.learningCategoryId()) { mutableListOf() } += card
    }
    return groupedCards.entries
        .sortedBy { (categoryId, _) -> learningCategorySortOrder(categoryId) }
        .map { (categoryId, entries) ->
            LearningSkillCategory(
                id = categoryId,
                label = learningCategoryLabel(categoryId),
                cards = entries.sortedBy { card -> card.title },
            )
        }
}

private fun ShellPanelCard.learningCategoryId(): String {
    return command.learnSkillIdFromCommand().learningSkillCategoryId()
}

private fun String.learningSkillCategoryId(): String {
    return when (trim().lowercase()) {
        "force" -> "inner"
        "dodge" -> "light"
        "parry" -> "parry"
        "sword", "blade", "staff", "club", "whip", "hammer", "axe", "spear", "throwing", "dagger" -> "weapon"
        "palm", "cuff", "claw", "finger", "leg" -> "fist"
        "medicine", "herb", "alchemy", "gather", "gathering", "craft", "crafting", "tailoring", "literate", "knowledge" -> "life"
        else -> "other"
    }
}

private fun learningCategorySortOrder(categoryId: String): Int {
    return when (categoryId) {
        DEFAULT_SKILL_PANEL_CATEGORY -> 0
        "inner" -> 1
        "light" -> 2
        "fist" -> 3
        "weapon" -> 4
        "parry" -> 5
        "life" -> 6
        else -> 99
    }
}

private fun learningCategoryLabel(categoryId: String): String {
    return when (categoryId) {
        DEFAULT_SKILL_PANEL_CATEGORY -> "全部"
        "inner" -> "内功"
        "light" -> "轻功"
        "fist" -> "拳脚"
        "weapon" -> "兵器"
        "parry" -> "招架"
        "life" -> "生活"
        else -> "其他"
    }
}
