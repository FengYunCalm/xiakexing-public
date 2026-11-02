package com.xiakexing.client.feature.shell

import com.xiakexing.client.core.protocol.payload.DialogPacket

internal fun buildMailSheetState(
    title: String,
    dialogId: String,
    detailLines: List<String>,
    mailItems: List<ShellSheetItem>,
    actions: List<ShellAction>,
    actionGroups: List<ShellActionGroup>,
    isMailListDialog: Boolean,
    panelEmptyText: String,
): ShellSheetState {
    val sheetTitle = "邮箱"
    val detailSubtitle =
        title.trim().takeUnless {
            it.isBlank() || it == "邮箱" || it == "未读邮件"
        }.orEmpty()
    val visibleActions =
        if (isMailListDialog) {
            actions
        } else {
            actions.filterNot {
                it.isMailNavigationAction()
            }
        }
    val visibleActionGroups =
        if (isMailListDialog) {
            actionGroups
        } else {
            actionGroups.mapNotNull { group ->
                group.copy(actions = group.actions.filterNot { it.isMailNavigationAction() })
                    .takeIf { it.actions.isNotEmpty() }
            }
        }
    return if (isMailListDialog) {
        buildInfoListSheet(
            source = PopupSource.PROTOCOL,
            title = sheetTitle,
            dialogId = dialogId,
            dialogSource = "mail",
            detailLines = detailLines,
            items = mailItems,
            actions = visibleActions,
            actionGroups = visibleActionGroups,
            panelEmptyText = panelEmptyText,
            emptyPresentation = ShellSheetEmptyPresentation.MESSAGE,
            loading = false,
            navigation = SheetNavigationMeta(routeKey = "mail"),
            kind = ShellSheetKind.MAIL,
        )
    } else {
        buildInfoListSheet(
            source = PopupSource.PROTOCOL,
            title = sheetTitle,
            subtitle = detailSubtitle,
            dialogId = dialogId,
            dialogSource = "mail",
            detailLines = detailLines,
            items = mailItems,
            actions = visibleActions,
            actionGroups = visibleActionGroups,
            panelStats = emptyList(),
            panelEmptyText = panelEmptyText,
            emptyPresentation = ShellSheetEmptyPresentation.MESSAGE,
            loading = false,
            navigation =
                SheetNavigationMeta(
                    routeKey = "mail/${dialogId.ifBlank { detailSubtitle.ifBlank { "detail" } }}",
                    depth = 1,
                ),
            kind = ShellSheetKind.MAIL,
        )
    }
}

private fun ShellAction.isMailNavigationAction(): Boolean {
    return label.trim().startsWith("返回") && command.trim().equals("mail", ignoreCase = true)
}

internal fun DialogPacket.isMailDialog(): Boolean {
    return source.trim().equals("mail", ignoreCase = true) ||
        dialogId.trim().lowercase().startsWith("mail_") ||
        dialogId.trim().lowercase().startsWith("mail:")
}

internal fun ShellSheetState.isMailSheet(): Boolean {
    return resolvedSheetKind() == ShellSheetKind.MAIL
}

internal fun ShellSheetState.isMailDetailSheet(): Boolean {
    val normalizedDialogId = dialogId.trim().lowercase()
    return isMailSheet() && detailLines.isNotEmpty() &&
        normalizedDialogId != "mail_inbox" && normalizedDialogId != "mail_unread"
}

internal fun buildMailLoadingSheetState(): ShellSheetState {
    return buildInfoListSheet(
        source = PopupSource.TITLE_ACTION,
        title = "邮箱",
        dialogSource = "mail",
        panelEmptyText = "",
        loading = true,
        navigation = SheetNavigationMeta(routeKey = "mail"),
        kind = ShellSheetKind.MAIL,
    )
}

internal fun DialogPacket.isMailListDialog(): Boolean {
    val normalizedDialogId = dialogId.trim().lowercase()
    return normalizedDialogId == "mail_inbox" ||
        normalizedDialogId == "mail_unread"
}

internal fun DialogPacket.mailListEmptyText(): String {
    return if (dialogId.trim().equals("mail_unread", ignoreCase = true) || title == "未读邮件") {
        "没有未读邮件"
    } else {
        "邮箱里空空如也"
    }
}

internal fun String.isMailListChromeLine(): Boolean {
    val normalized = trim()
    return normalized == "邮件列表：" ||
        normalized == "未读邮件：" ||
        normalized == "邮件列表:" ||
        normalized == "未读邮件:" ||
        normalized == "可用操作：mail read <编号>、mail claim <编号>、mail delete <编号>"
}

internal fun String.toMailListItemOrNull(): ShellSheetItem? {
    val match = mailListRowRegex.matchEntire(trim()) ?: return null
    val mailId = match.groupValues[1]
    val status = match.groupValues[2]
    val subject = match.groupValues[4]
    return ShellSheetItem(
        title = subject,
        trailing = status,
        command = "mail read $mailId",
        dismissAfterInvoke = true,
    )
}

private val mailListRowRegex = Regex("^#(\\d+)\\s+\\[(.+?)]\\s+(.+?)\\s+(.+)$")
