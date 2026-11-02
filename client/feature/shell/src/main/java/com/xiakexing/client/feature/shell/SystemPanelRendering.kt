package com.xiakexing.client.feature.shell

internal const val UPDATE_CHECK_COMMAND = "ui://client-update/check"
internal const val OPEN_SETTINGS_PANEL_COMMAND = "ui://panel/settings"

/** 设置内「减少动态效果」开关命令 */
internal const val SET_REDUCE_MOTION_COMMAND = "ui://settings/reduce-motion"

internal fun buildFunctionSheetState(): ShellSheetState {
    return buildInfoListSheet(
        source = PopupSource.DOCK_BUTTON,
        title = "功能",
        items =
            listOf(
                ShellSheetItem("邮箱收件箱", command = "mail", dismissAfterInvoke = true),
                ShellSheetItem("未读邮件", command = "mail unread", dismissAfterInvoke = true),
                ShellSheetItem("银行总览", command = "bank", dismissAfterInvoke = true),
                ShellSheetItem("银行查账", command = "bank check", dismissAfterInvoke = true),
                ShellSheetItem("当前时辰", command = "time", dismissAfterInvoke = true),
                ShellSheetItem("在线玩家", command = "who", dismissAfterInvoke = true),
                ShellSheetItem("手动保存", command = "save", dismissAfterInvoke = true),
                ShellSheetItem("发送邮件", command = "mail send-select", dismissAfterInvoke = true),
                ShellSheetItem("检查更新", command = UPDATE_CHECK_COMMAND),
            ),
        panelEmptyText = "无功能",
    )
}

internal fun buildSettingsSheetState(reduceMotion: Boolean = false): ShellSheetState {
    return buildInfoListSheet(
        source = PopupSource.DOCK_BUTTON,
        title = "设置",
        items =
            listOf(
                ShellSheetItem(
                    "减少动态效果",
                    command = SET_REDUCE_MOTION_COMMAND,
                    dismissAfterInvoke = false,
                    role = ShellSheetItemRole.OPTION,
                    selected = reduceMotion,
                ),
            ),
        panelEmptyText = "无配置",
        presentation = ShellSheetPresentation.SETTINGS,
    )
}
