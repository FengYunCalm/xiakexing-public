package com.xiakexing.client.feature.shell

import com.xiakexing.client.core.protocol.payload.PlayerStatusSnapshot

internal const val OPEN_CHARACTER_STATUS_COMMAND = "ui://panel/character/status"
internal const val OPEN_CHARACTER_ATTRIBUTES_COMMAND = "ui://panel/character/attributes"
internal const val OPEN_EQUIPMENT_PANEL_COMMAND = "ui://panel/character/equipment"

internal fun buildCharacterSheetState(snapshot: PlayerStatusSnapshot?): ShellSheetState {
    return buildInfoListSheet(
        source = PopupSource.DOCK_BUTTON,
        title = "人物",
        items = buildCharacterItems(snapshot),
        panelEmptyText = "加载中",
        presentation = ShellSheetPresentation.KEY_VALUE_GRID,
        loading = snapshot == null,
        kind = ShellSheetKind.CHARACTER,
    )
}

internal fun buildCharacterHubSheetState(): ShellSheetState {
    return buildGridSheet(
        source = PopupSource.DOCK_BUTTON,
        title = "人物",
        gridMode = ShellPanelMode.INVENTORY_GRID,
        panelCards =
            listOf(
                ShellPanelCard("status", "状态", command = OPEN_CHARACTER_STATUS_COMMAND),
                ShellPanelCard("inventory", "背包", command = OPEN_INVENTORY_PANEL_COMMAND),
                ShellPanelCard("equipment", "装备", command = OPEN_EQUIPMENT_PANEL_COMMAND),
                ShellPanelCard("skills", "武学", command = "skills"),
                ShellPanelCard("attributes", "属性", command = OPEN_CHARACTER_ATTRIBUTES_COMMAND),
            ),
        panelEmptyText = "暂无人物功能",
        kind = ShellSheetKind.CHARACTER,
    )
}

internal fun buildAttributeSheetState(snapshot: PlayerStatusSnapshot?): ShellSheetState {
    return buildInfoListSheet(
        source = PopupSource.DOCK_BUTTON,
        title = "人物",
        showHeaderTitle = false,
        items = buildAttributeItems(snapshot),
        panelEmptyText = "加载中",
        presentation = ShellSheetPresentation.KEY_VALUE_GRID,
        loading = snapshot == null,
        navigation =
            SheetNavigationMeta(
                dockSection = DockWorkspaceSection.ATTRIBUTE,
                routeKey = "attribute",
            ),
        kind = ShellSheetKind.ATTRIBUTE,
    ).copy(floatingClose = true)
}

private fun buildCharacterItems(snapshot: PlayerStatusSnapshot?): List<ShellSheetItem> {
    if (snapshot == null) {
        return emptyList()
    }
    return listOf(
        ShellSheetItem("姓名", trailing = snapshot.name.ifBlank { "未命名" }),
        ShellSheetItem("技能上限", trailing = snapshot.skillCap.toString()),
        ShellSheetItem("成长档位", trailing = snapshot.progressionRank.toString()),
        ShellSheetItem("气血", trailing = "${snapshot.hp}/${maxOf(snapshot.hp, snapshot.effHp)}"),
        ShellSheetItem("气血上限", trailing = snapshot.maxHp.toString()),
        ShellSheetItem("内力", trailing = "${snapshot.mp}/${snapshot.maxMp}"),
        ShellSheetItem("内力上限", trailing = snapshot.maxMp.toString()),
        ShellSheetItem("精力", trailing = "${snapshot.ep}/${snapshot.maxEp}"),
        ShellSheetItem("经验", trailing = snapshot.exp.toString()),
    )
}

private fun buildAttributeItems(snapshot: PlayerStatusSnapshot?): List<ShellSheetItem> {
    if (snapshot == null) {
        return emptyList()
    }
    return buildList {
        // 顶部身份行：姓名 + 年龄（年龄 = 18 + 游戏天数/365，服务端计算）。
        add(ShellSheetItem("姓名", trailing = snapshot.name.ifBlank { "未命名" }))
        add(ShellSheetItem("年龄", trailing = snapshot.age.toString()))
        // section 仅用于布局分组（组间分隔线），渲染不产生色块/小标题。
        // 潜能恒显示（服务端恒发，0 也显示 0）。
        add(ShellSheetItem("经验", trailing = snapshot.exp.toString(), section = "成长"))
        add(ShellSheetItem("潜能", trailing = (snapshot.potential ?: 0L).toString(), section = "成长"))
        add(ShellSheetItem("技能上限", trailing = snapshot.skillCap.toString(), section = "成长"))
        add(ShellSheetItem("成长档位", trailing = snapshot.progressionRank.toString(), section = "成长"))
        add(ShellSheetItem("臂力", trailing = snapshot.strength.toString(), section = "基础"))
        add(ShellSheetItem("身法", trailing = snapshot.dexterity.toString(), section = "基础"))
        add(ShellSheetItem("根骨", trailing = snapshot.constitution.toString(), section = "基础"))
        add(ShellSheetItem("悟性", trailing = snapshot.intelligence.toString(), section = "基础"))
        add(ShellSheetItem("攻击", trailing = snapshot.attack.toString(), section = "战斗"))
        add(ShellSheetItem("防御", trailing = snapshot.defense.toString(), section = "战斗"))
        add(ShellSheetItem("命中", trailing = snapshot.hit.toString(), section = "战斗"))
        add(ShellSheetItem("闪避", trailing = snapshot.dodge.toString(), section = "战斗"))
        add(ShellSheetItem("招架", trailing = snapshot.parry.toString(), section = "战斗"))
        add(ShellSheetItem("暴击", trailing = snapshot.critical.toString(), section = "战斗"))
        add(ShellSheetItem("抗暴", trailing = snapshot.criticalResist.toString(), section = "战斗"))
        add(ShellSheetItem("破甲", trailing = snapshot.armorBreak.toString(), section = "战斗"))
        add(ShellSheetItem("攻速", trailing = snapshot.attackSpeed.toString(), section = "战斗"))
    }
}
