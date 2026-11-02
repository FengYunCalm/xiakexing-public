package com.xiakexing.client.feature.shell

internal fun buildGuildSheetState(): ShellSheetState {
    return buildInfoListSheet(
        source = PopupSource.DOCK_BUTTON,
        title = "帮会",
        items =
            listOf(
                ShellSheetItem(
                    "查看帮派状态",
                    command = "guild status",
                    dismissAfterInvoke = true,
                    section = "概览",
                ),
                ShellSheetItem(
                    "查看帮派列表",
                    command = "guild list",
                    dismissAfterInvoke = true,
                    section = "概览",
                ),
                ShellSheetItem(
                    "查看帮派武学",
                    command = "guild skills",
                    dismissAfterInvoke = true,
                    section = "概览",
                ),
                ShellSheetItem(
                    "领取每日俸禄",
                    command = "guild salary",
                    dismissAfterInvoke = true,
                    section = "日常",
                ),
                ShellSheetItem(
                    "捐献帮派",
                    command = "guild donate-select",
                    dismissAfterInvoke = true,
                    section = "日常",
                ),
                ShellSheetItem(
                    "学习武学",
                    command = "guild learn-select",
                    dismissAfterInvoke = true,
                    section = "日常",
                ),
                ShellSheetItem(
                    "加入帮派",
                    command = "guild join-select",
                    dismissAfterInvoke = true,
                    section = "组织",
                ),
                ShellSheetItem(
                    "任命职位",
                    command = "guild appoint-select",
                    dismissAfterInvoke = true,
                    section = "组织",
                ),
                ShellSheetItem(
                    "退出当前帮派",
                    command = "guild leave",
                    dismissAfterInvoke = true,
                    role = ShellSheetItemRole.DANGER,
                    section = "危险操作",
                ),
            ),
        panelEmptyText = "无帮会",
        presentation = ShellSheetPresentation.MANAGEMENT_LIST,
    )
}

internal fun buildRankSheetState(): ShellSheetState {
    return buildInfoListSheet(
        source = PopupSource.DOCK_BUTTON,
        title = "排行",
        items =
            listOf(
                ShellSheetItem(
                    "实力榜前十",
                    command = "rank list power 10",
                    dismissAfterInvoke = true,
                    section = "榜单",
                ),
                ShellSheetItem(
                    "我的实力名次",
                    command = "rank me power",
                    dismissAfterInvoke = true,
                    section = "个人",
                ),
                ShellSheetItem(
                    "领取实力榜奖励",
                    command = "rank claim power",
                    dismissAfterInvoke = true,
                    section = "奖励",
                ),
            ),
        panelEmptyText = "无排行",
        presentation = ShellSheetPresentation.MANAGEMENT_LIST,
    )
}

internal fun buildTeamSheetState(selfName: String): ShellSheetState {
    return buildInfoListSheet(
        source = PopupSource.TITLE_ACTION,
        title = "队伍",
        items =
            listOf(
                ShellSheetItem(selfName, role = ShellSheetItemRole.INFO, section = "当前队伍"),
                ShellSheetItem(
                    "查看当前队伍",
                    command = "team",
                    dismissAfterInvoke = true,
                    section = "当前队伍",
                ),
                ShellSheetItem(
                    "接受邀请",
                    command = "team accept",
                    dismissAfterInvoke = true,
                    section = "邀请",
                ),
                ShellSheetItem(
                    "拒绝邀请",
                    command = "team reject",
                    dismissAfterInvoke = true,
                    section = "邀请",
                ),
                ShellSheetItem(
                    "邀请玩家",
                    command = "team with-select",
                    dismissAfterInvoke = true,
                    section = "邀请",
                ),
                ShellSheetItem(
                    "踢出队员",
                    command = "team kick-select",
                    dismissAfterInvoke = true,
                    role = ShellSheetItemRole.DANGER,
                    section = "管理",
                ),
                ShellSheetItem(
                    "离开队伍",
                    command = "team leave",
                    dismissAfterInvoke = true,
                    role = ShellSheetItemRole.DANGER,
                    section = "危险操作",
                ),
                ShellSheetItem(
                    "解散队伍",
                    command = "team dismiss",
                    dismissAfterInvoke = true,
                    role = ShellSheetItemRole.DANGER,
                    section = "危险操作",
                ),
            ),
        panelEmptyText = "无队伍",
        presentation = ShellSheetPresentation.MANAGEMENT_LIST,
        kind = ShellSheetKind.TEAM,
    )
}

internal fun buildMessageSheetState(
    @Suppress("UNUSED_PARAMETER") recentLines: List<String>,
): ShellSheetState {
    return buildInfoListSheet(
        source = PopupSource.TITLE_ACTION,
        title = "邮箱",
        dialogSource = "mail",
        actions =
            listOf(
                ShellAction("邮箱", "mail", dismissAfterInvoke = false),
                ShellAction("未读邮件", "mail unread", dismissAfterInvoke = false),
                ShellAction("全部领取", "mail claim all", dismissAfterInvoke = false),
            ),
        panelEmptyText = "无邮件",
        kind = ShellSheetKind.MAIL,
    )
}
