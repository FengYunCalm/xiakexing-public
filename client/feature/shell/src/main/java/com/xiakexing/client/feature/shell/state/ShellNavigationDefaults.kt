package com.xiakexing.client.feature.shell

import com.xiakexing.client.core.protocol.payload.ExitSnapshot

internal val shellTitleActions =
    listOf(
        TitleActionItem(TitleActionType.MESSAGE, "邮箱"),
        TitleActionItem(TitleActionType.SYSTEM, "系统"),
    )

internal val specialExitDirections = setOf("up", "down", "enter", "out")
internal val gridExitDirections =
    setOf(
        "northwest",
        "north",
        "northeast",
        "west",
        "east",
        "southwest",
        "south",
        "southeast",
    )

internal fun buildExitOverlayButtons(type: ExitOverlayType): List<ShellAction> {
    return when (type) {
        ExitOverlayType.COMMON ->
            listOf(
                ShellAction("背包", OPEN_INVENTORY_PANEL_COMMAND, dismissAfterInvoke = false),
                ShellAction("人物", OPEN_CHARACTER_STATUS_COMMAND, dismissAfterInvoke = false),
                ShellAction("属性", OPEN_CHARACTER_ATTRIBUTES_COMMAND, dismissAfterInvoke = false),
                ShellAction("装备", OPEN_EQUIPMENT_PANEL_COMMAND, dismissAfterInvoke = false),
                ShellAction("任务", OPEN_ACTIVITY_PANEL_COMMAND, dismissAfterInvoke = false),
                ShellAction("商店", OPEN_WORLD_SHOP_COMMAND, dismissAfterInvoke = false),
                ShellAction("地图", OPEN_WORLD_MAP_COMMAND, dismissAfterInvoke = false),
                ShellAction("功能", "功能", dismissAfterInvoke = false),
            )
        ExitOverlayType.COMBAT -> emptyList()
    }
}

internal fun List<ExitSnapshot>.partitionForMainShell(): Pair<List<ExitSnapshot>, List<ExitSnapshot>> {
    if (isEmpty()) {
        return emptyList<ExitSnapshot>() to emptyList()
    }

    val special = ArrayList<ExitSnapshot>(size)
    val grid = ArrayList<ExitSnapshot>(size)
    for (exit in this) {
        when (exit.direction.lowercase()) {
            in specialExitDirections -> special += exit
            in gridExitDirections -> grid += exit
        }
    }
    return special to grid
}
