package com.xiakexing.client.feature.shell

import com.xiakexing.client.core.protocol.payload.DialogPacket

/**
 * InteractionSession 域 NPC 学习弹窗编排——技能卡面板构造
 * （ShellNpcLearningDialogCoordinator 收敛）。
 */
internal fun handleNpcLearningDialog(
    capture: InteractionCaptureState,
    sink: InteractionSessionSink,
    packet: DialogPacket,
): Boolean {
    if (packet.source != "npc_learning") {
        return false
    }
    val currentSheet = sink.currentState().sheet
    val targetId =
        currentSheet?.interactionTargetId?.takeIf { it.isNotBlank() }
            ?: packet.speakerId
    val targetName =
        currentSheet?.title?.takeIf { it.isNotBlank() }
            ?: packet.title.takeIf { it.isNotBlank() }
            ?: targetId
    val targetKind = currentSheet?.interactionTargetKind ?: InteractionObjectKind.NPC
    val skillActions =
        packet.actionGroups
            .flatMap { group -> group.actions }
            .mapNotNull { action -> action.toShellActionOrNull(targetId, "learn") }
            .filter { action -> action.command.isLearnSkillCommand() }
    val skillCards =
        skillActions.map { action ->
            val title = action.label.ifBlank { action.command.learnSkillIdFromCommand() }
            ShellPanelCard(
                id = action.command,
                title = title,
                command = action.command,
                dismissAfterInvoke = false,
            )
        }
    if (skillCards.isEmpty()) {
        // 无技能可学：飘窗提示，面板原样保留不关闭（不触碰正文）。
        sink.updateState {
            copy(dialog = null).withNpcLearningEmptyFeedback(targetId)
        }
        capture.clearPendingInteractionRequest()
        sink.clearPendingProtocolSheetRequest()
        return true
    }
    sink.updateState {
        copy(
            sheet =
                buildNpcLearningSheetState(
                    dialogId = packet.dialogId,
                    dialogSource = packet.source,
                    skillCards = skillCards,
                    targetId = targetId,
                    targetName = targetName,
                    targetKind = targetKind,
                ),
            dialog = null,
        )
    }
    capture.clearPendingInteractionRequest()
    sink.clearPendingProtocolSheetRequest()
    return true
}
