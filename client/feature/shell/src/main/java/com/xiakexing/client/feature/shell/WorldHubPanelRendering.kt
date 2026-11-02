package com.xiakexing.client.feature.shell

internal const val ACTIVITY_PANEL_TITLE = "任务"
internal const val OPEN_WORLD_MAP_COMMAND = "ui://panel/world/map"
internal const val OPEN_WORLD_MAIL_COMMAND = "ui://panel/world/mail"
internal const val OPEN_WORLD_MESSAGES_COMMAND = "ui://panel/world/messages"
internal const val OPEN_WORLD_SHOP_COMMAND = "ui://panel/world/shop"

internal fun buildWorldHubOverlayButtons(): List<ShellAction> {
    return listOf(
        ShellAction("地图", OPEN_WORLD_MAP_COMMAND, dismissAfterInvoke = false),
        ShellAction("邮箱", OPEN_WORLD_MAIL_COMMAND, dismissAfterInvoke = false),
        ShellAction("商店", OPEN_WORLD_SHOP_COMMAND, dismissAfterInvoke = false),
        ShellAction(
            "帮助",
            "$OPEN_HELP_PANEL_CATEGORY_PREFIX$DEFAULT_HELP_PANEL_CATEGORY",
            dismissAfterInvoke = false,
        ),
        ShellAction("设置", OPEN_SETTINGS_PANEL_COMMAND, dismissAfterInvoke = false),
    )
}

internal fun buildWorldShopGuideSheetState(): ShellSheetState {
    return buildInfoListSheet(
        source = PopupSource.DOCK_BUTTON,
        title = "商店",
        detailLines =
            listOf(
                "当前全局商店不再展示本地样例货品。",
                "真实购买请在场景内找到商人，使用对话里的货单入口。",
            ),
        items =
            listOf(
                ShellSheetItem("前往杂货摊", command = "east", dismissAfterInvoke = true),
                ShellSheetItem(
                    "打开邮箱",
                    command = OPEN_WORLD_MAIL_COMMAND,
                    dismissAfterInvoke = false,
                ),
            ),
        actions =
            listOf(
                ShellAction("邮箱", "mail", dismissAfterInvoke = false),
            ),
        panelEmptyText = "无商店",
    )
}

internal fun buildWorldHubSheetState(): ShellSheetState {
    return buildGridSheet(
        source = PopupSource.DOCK_BUTTON,
        title = "江湖",
        gridMode = ShellPanelMode.INVENTORY_GRID,
        panelCards =
            listOf(
                ShellPanelCard("map", "地图", command = OPEN_WORLD_MAP_COMMAND),
                ShellPanelCard("mail", "邮箱", command = OPEN_WORLD_MAIL_COMMAND),
                ShellPanelCard("shop", "商店", command = OPEN_WORLD_SHOP_COMMAND),
                ShellPanelCard(
                    "help",
                    "帮助",
                    command = "$OPEN_HELP_PANEL_CATEGORY_PREFIX$DEFAULT_HELP_PANEL_CATEGORY",
                ),
                ShellPanelCard("settings", "设置", command = OPEN_SETTINGS_PANEL_COMMAND),
            ),
        panelEmptyText = "暂无江湖系统",
        kind = ShellSheetKind.WORLD,
    )
}

internal fun buildActivitySheetState(
    objectiveHub: ObjectiveHubState = ObjectiveHubState(),
): ShellSheetState {
    val trackedObjective = objectiveHub.trackedObjective
    val allItems =
        objectiveHub.quests
            .map { quest ->
                ShellSheetItem(
                    title = quest.displayName(),
                    trailing = quest.questTrailing(fallback = "任务"),
                    command = "$OPEN_QUEST_DETAIL_PREFIX${quest.questId}",
                    dismissAfterInvoke = false,
                    selected =
                        trackedObjective.questId == quest.questId || trackedObjective.objectiveId == quest.objectiveId,
                )
            }
            .sortedWith(compareByDescending<ShellSheetItem> { it.selected }.thenBy { it.title })
    return buildInfoListSheet(
        source = PopupSource.DOCK_BUTTON,
        title = ACTIVITY_PANEL_TITLE,
        showHeaderTitle = true,
        items = allItems,
        panelEmptyText = "暂无任务",
        kind = ShellSheetKind.ACTIVITY,
        navigation =
            SheetNavigationMeta(
                dockSection = DockWorkspaceSection.ACTIVITY,
                routeKey = "objective/activity",
            ),
    )
}
