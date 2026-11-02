package com.xiakexing.client.feature.shell

import com.xiakexing.client.core.protocol.AnsiSanitizer
import com.xiakexing.client.core.protocol.payload.DialogPacket

/**
 * InteractionSession 域 mail 弹窗编排——邮件列表/详情面板构造
 * （ShellMailDialogCoordinator 收敛）。
 */
internal fun handleMailDialog(
    capture: InteractionCaptureState,
    sink: InteractionSessionSink,
    packet: DialogPacket,
): Boolean {
    if (!packet.isMailDialog()) {
        return false
    }

    capture.clearPendingInteractionRequest()
    sink.clearPendingProtocolSheetRequest()
    val actionGroups = packet.actionGroups.toShellActionGroups("")
    val actions = actionGroups.flatMap { it.actions }
    val contentLines = AnsiSanitizer.sanitize(packet.content).toParagraphs()
    val mailItems =
        if (packet.isMailListDialog()) {
            contentLines.mapNotNull { line -> line.toMailListItemOrNull() }
        } else {
            emptyList()
        }
    val detailLines =
        if (packet.isMailListDialog()) {
            contentLines.filterNot { line ->
                line.isMailListChromeLine() || line.toMailListItemOrNull() != null
            }
        } else {
            contentLines
        }

    sink.updateState {
        copy(
            sheet =
                buildMailSheetState(
                    title =
                        packet.title.ifBlank {
                            if (packet.dialogId == "mail_unread") "未读邮件" else "邮箱"
                        },
                    dialogId = packet.dialogId,
                    detailLines = detailLines,
                    mailItems = mailItems,
                    actions = actions,
                    actionGroups = actionGroups,
                    isMailListDialog = packet.isMailListDialog(),
                    panelEmptyText = packet.mailListEmptyText(),
                ),
            dialog = null,
        )
    }
    return true
}
