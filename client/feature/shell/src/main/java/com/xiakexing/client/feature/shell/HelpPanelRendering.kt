package com.xiakexing.client.feature.shell

internal const val OPEN_HELP_PANEL_CATEGORY_PREFIX = "ui://panel/help/category/"
internal const val OPEN_HELP_ITEM_DETAIL_PREFIX = "ui://panel/help/item/"
internal const val DEFAULT_HELP_PANEL_CATEGORY = "beginner"

internal fun buildHelpSheetState(categoryId: String): ShellSheetState {
    val selectedCategory =
        helpCategoryDefinitions.firstOrNull { it.id == categoryId }
            ?: helpCategoryDefinitions.first()
    return buildInfoListSheet(
        source = PopupSource.DOCK_BUTTON,
        title = "帮助",
        panelTabs =
            helpCategoryDefinitions.map { category ->
                ShellAction(
                    label = category.label,
                    command = "$OPEN_HELP_PANEL_CATEGORY_PREFIX${category.id}",
                    dismissAfterInvoke = false,
                )
            },
        panelSelectedTabId = selectedCategory.id,
        items =
            selectedCategory.entries.map { entry ->
                ShellSheetItem(
                    title = entry.title,
                    subtitle = entry.description,
                    command = "$OPEN_HELP_ITEM_DETAIL_PREFIX${entry.id}",
                    dismissAfterInvoke = false,
                    role = ShellSheetItemRole.NAVIGATION,
                )
            },
        panelEmptyText = "无帮助",
        presentation = ShellSheetPresentation.HELP_INDEX,
        navigation = SheetNavigationMeta(routeKey = "help/${selectedCategory.id}"),
    )
}

internal fun HelpEntrySample.toHelpDetailSheet(): ShellSheetState {
    return buildInfoListSheet(
        source = PopupSource.DOCK_BUTTON,
        title = title,
        detailLines = listOf(description),
        panelStats = commandHints,
        panelEmptyText = "暂无说明",
        navigation =
            SheetNavigationMeta(
                routeKey = "help/detail/$id",
                depth = 1,
            ),
    )
}

internal data class HelpEntrySample(
    val id: String,
    val title: String,
    val description: String,
    val commandHints: List<String> = emptyList(),
)

private data class HelpCategoryDefinition(
    val id: String,
    val label: String,
    val entries: List<HelpEntrySample>,
)

private val helpBeginnerEntries =
    listOf(
        HelpEntrySample("help_beginner_look", "查看与移动", "主界面会展示当前房间、出口和可交互对象；移动直接点出口按钮完成。"),
        HelpEntrySample("help_beginner_interact", "物品与场景", "点左侧对象先看详情，再按界面返回的真实动作处理拾取、使用或装备。"),
    )

private val helpBattleEntries =
    listOf(
        HelpEntrySample("help_battle_basics", "基础战斗", "切磋适合试手，攻击会直接进入敌对状态；先在人物弹窗里确认对象，再点对应动作。"),
        HelpEntrySample("help_battle_recover", "战后处理", "战后先看状态栏，再决定是否使用背包药品或离开当前房间。"),
    )

private val helpQuestEntries =
    listOf(
        HelpEntrySample("help_quest_board", "查看任务", "任务入口会展示当前进度；能接取、回报或继续追踪时，界面会给出按钮。"),
        HelpEntrySample("help_quest_npc", "人物任务", "人物界面里优先用交谈承接后端话题，不再靠前端硬编码文案猜任务。"),
    )

private val helpTradeEntries =
    listOf(
        HelpEntrySample("help_trade_shop", "商店购买", "商人货单会以卡片展示，价格和库存以协议回包为准，购买直接点货品。"),
        HelpEntrySample("help_trade_sell", "出售物品", "出售前先在背包里确认物品，再使用界面提供的出售动作，避免误卖装备。"),
    )

private val helpNpcEntries =
    listOf(
        HelpEntrySample(
            "help_npc_talk",
            "人物主动作",
            "人物首屏固定保留任务、技能、给予、切磋、攻击五个主动作（交谈话题直接展示在面板），真实补充动作走后端协议。",
        ),
        HelpEntrySample("help_npc_topics", "交谈话题", "进入交谈页后只消费后端下发的话题，不再前端硬编码二级对话选项。"),
    )

private val helpSystemEntries =
    listOf(
        HelpEntrySample("help_system_chat", "聊天", "底栏聊天只发送聊天内容；移动、人物、任务、背包等操作统一使用界面按钮。"),
        HelpEntrySample("help_system_message", "信息与界面", "信息类结果优先留在弹窗或面板内，邮件可以从邮箱页直接打开和领取。"),
    )

private val helpCategoryDefinitions =
    listOf(
        HelpCategoryDefinition("beginner", "新手", helpBeginnerEntries),
        HelpCategoryDefinition("battle", "战斗", helpBattleEntries),
        HelpCategoryDefinition("quest", "任务", helpQuestEntries),
        HelpCategoryDefinition("trade", "交易", helpTradeEntries),
        HelpCategoryDefinition("npc", "人物", helpNpcEntries),
        HelpCategoryDefinition("system", "系统", helpSystemEntries),
    )

internal val helpEntriesById =
    helpCategoryDefinitions
        .flatMap { category -> category.entries }
        .associateBy { entry -> entry.id }
