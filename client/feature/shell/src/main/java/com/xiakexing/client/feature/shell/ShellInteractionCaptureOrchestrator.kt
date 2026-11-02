package com.xiakexing.client.feature.shell

import com.xiakexing.client.core.network.CommandRequestIdentity

internal class ShellInteractionCaptureOrchestrator(
    private val capture: InteractionCaptureState,
) {
    fun hasPendingCombatStart(): Boolean = capture.hasPendingCombatStart()

    fun pendingCombatStartFailure(line: String): String? = capture.pendingCombatStartFailure(line)

    fun rollbackPendingCommandRequest(request: CommandRequestIdentity) =
        capture.rollbackPendingCommandRequest(request)

    fun acceptsCommandResponseForPending(commandResponseId: String): Boolean =
        capture.acceptsCommandResponseForPending(commandResponseId)

    fun hasTaggedPendingCommandRequest(): Boolean = capture.hasTaggedPendingCommandRequest()

    fun settleOnPrompt(): PromptSettlePlan {
        return PromptSettlePlan(
            settleProtocolPopup = capture.pendingProtocolPopupRequest != null,
            settleProtocolSheet = capture.pendingProtocolSheetRequest != null,
            settleInteraction = capture.shouldSettleInteractionOnPrompt(),
        )
    }

    fun collectTranscriptLine(line: String): TranscriptCapturePlan {
        val protocolPopup = capture.collectPendingProtocolPopupLine(line)
        if (protocolPopup.handled) {
            return TranscriptCapturePlan.ProtocolPopup(protocolPopup)
        }

        val protocolSheet = capture.collectPendingProtocolSheetLine(line)
        if (protocolSheet.handled) {
            return TranscriptCapturePlan.ProtocolSheet(protocolSheet)
        }

        val interaction = capture.collectPendingInteractionLine(line)
        if (interaction.handled) {
            return TranscriptCapturePlan.Interaction(interaction)
        }

        return TranscriptCapturePlan.None
    }
}

internal data class PromptSettlePlan(
    val settleProtocolPopup: Boolean,
    val settleProtocolSheet: Boolean,
    val settleInteraction: Boolean,
)

internal sealed interface TranscriptCaptureUiPlan {
    data object NotHandled : TranscriptCaptureUiPlan

    data class ProtocolPopup(
        val shouldSettleImmediately: Boolean,
        val nextState: ShellUiState? = null,
        val shouldScheduleSettle: Boolean = false,
    ) : TranscriptCaptureUiPlan

    data class ProtocolSheet(
        val shouldSettleImmediately: Boolean,
        val nextState: ShellUiState? = null,
        val shouldScheduleSettle: Boolean = false,
    ) : TranscriptCaptureUiPlan

    data class Interaction(
        val shouldSettleImmediately: Boolean,
        val nextState: ShellUiState? = null,
        val shouldScheduleSettle: Boolean = false,
    ) : TranscriptCaptureUiPlan
}

internal sealed interface TranscriptCapturePlan {
    data object None : TranscriptCapturePlan

    data class ProtocolPopup(
        val result: ProtocolPopupCollectionResult,
    ) : TranscriptCapturePlan

    data class ProtocolSheet(
        val result: ProtocolSheetCollectionResult,
    ) : TranscriptCapturePlan

    data class Interaction(
        val result: InteractionCollectionResult,
    ) : TranscriptCapturePlan
}

internal fun ShellUiState.applyInteractionCapture(
    result: InteractionCollectionResult,
): ShellUiState {
    val currentSheet = sheet
    if (currentSheet == null || currentSheet.source != PopupSource.INTERACTION_OBJECT) {
        return this
    }

    return copy(
        sheet =
            currentSheet.copy(
                detailLines =
                    mergeInteractionLines(
                        currentSheet.detailLines,
                        result.detailLines,
                    ),
                interactionLines =
                    mergeInteractionLines(
                        currentSheet.interactionLines,
                        result.interactionLines,
                    ),
                actions = currentSheet.actionsForSettledInteraction(result.activeActionGroupId),
                activeActionGroupId = result.activeActionGroupId.ifBlank { currentSheet.activeActionGroupId },
                loading = false,
            ),
    )
}

internal fun ShellUiState.applyProtocolSheetCapture(
    result: ProtocolSheetCollectionResult,
): ShellUiState {
    val currentSheet = sheet
    if (currentSheet == null || currentSheet.sheetIdentity() != result.sheetIdentity) {
        return this
    }

    return copy(
        sheet =
            currentSheet.withCapturedProtocolLines(
                command = result.command,
                lines = result.lines,
                loading = false,
            ),
    )
}

internal fun ShellUiState.applyProtocolPopupCapture(
    result: ProtocolPopupCollectionResult,
): ShellUiState {
    val currentDialog = dialog
    if (currentDialog == null || currentDialog.popupIdentity() != result.popupIdentity) {
        return this
    }

    return copy(
        dialog =
            currentDialog.copy(
                message = result.message,
            ),
    )
}

internal fun ShellUiState.applySettledProtocolSheet(
    pending: PendingProtocolSheetRequest,
): ShellUiState {
    val currentSheet = sheet
    if (currentSheet == null || currentSheet.sheetIdentity() != pending.sheetIdentity || !currentSheet.matchesWorkspaceRequest(pending)) {
        return this
    }

    return copy(
        sheet =
            currentSheet.withCapturedProtocolLines(
                command = pending.command,
                lines = pending.lines.toList(),
                loading = false,
            ).withSettledFeedback(pending.command, pending.lines),
    )
}

private fun ShellSheetState.withSettledFeedback(
    command: String,
    lines: List<String>,
): ShellSheetState {
    val error =
        lines.lastOrNull { it.startsWith("错误：") }
            ?.removePrefix("错误：")
            ?.trim()
            .orEmpty()
    val retry =
        command.takeIf { error.isNotBlank() && it.isSafeSheetRetryCommand() }?.let { safeCommand ->
            primaryAction?.takeIf { it.command.trim().equals(safeCommand.trim(), ignoreCase = true) }
                ?: ShellAction("重试", safeCommand, dismissAfterInvoke = false, role = "primary")
        }
    return copy(errorMessage = error, retryAction = retry)
}

private fun String.isSafeSheetRetryCommand(): Boolean {
    val normalized = trim().lowercase()
    return normalized in setOf("inventory", "i", "skills", "quest", "mail", "mail unread", "map xk", "bank", "bank check", "who", "status", "score", "time") ||
        normalized.startsWith("quest info ") ||
        normalized.startsWith("help ") ||
        normalized.startsWith("guild status") ||
        normalized.startsWith("guild list") ||
        normalized.startsWith("rank list") ||
        normalized.startsWith("rank me")
}

internal fun ShellUiState.applySettledProtocolPopup(
    pending: PendingProtocolPopupRequest,
): ShellUiState {
    val currentDialog = dialog
    if (currentDialog == null || currentDialog.popupIdentity() != pending.popupIdentity) {
        return this
    }

    return copy(
        dialog =
            currentDialog.copy(
                message = pending.renderedMessage(),
            ),
    )
}

internal fun ShellUiState.applySettledInteraction(
    pending: PendingInteractionRequest,
): ShellUiState {
    val currentSheet = sheet
    if (currentSheet == null || currentSheet.source != PopupSource.INTERACTION_OBJECT) {
        return this
    }
    val currentTargetKey =
        currentSheet.interactionTargetUiKey.ifBlank {
            currentSheet.interactionTargetId
        }
    if (currentTargetKey.isNotBlank() && currentTargetKey !=
        pending.target.uiKey.ifBlank {
            pending.target.id
        }
    ) {
        return this
    }

    val isNpcQuestMutation =
        currentSheet.isNpcQuestInteractionSheet() &&
            pending.target.kind == InteractionObjectKind.NPC &&
            pending.command.isNpcQuestMutationCommand()
    val pendingError =
        if (isNpcQuestMutation) {
            pending.interactionLines
                .lastOrNull { line -> line.startsWith("错误：") }
                ?.removePrefix("错误：")
                ?.trim()
                .orEmpty()
        } else {
            ""
        }
    val settledInteractionLines =
        if (isNpcQuestMutation) {
            pending.interactionLines.filterNot { line -> line.startsWith("错误：") }
        } else {
            pending.interactionLines
        }
    /* 服务端任务面板已落地（任务组 + 任务行）时，prompt 结算不得把活动组退回 pending 组：
     * 房间投影的根「任务」动作 target_group=root，结算会把面板切回 NPC 根页渲染，
     * 已下发的可接取任务行随之从界面上消失（真机：点任务员→任务→空白任务面板）。
     * 同理不得把 pending 从 NPC 根页带来的描述行合并回面板：任务列表面板按设计
     * 只画任务行（buildNpcQuestInteractionSheetState list 分支 detailLines 恒空），
     * 合并会往面板顶部塞回一句 NPC 描述（真机：任务面板出现“一名负责发放测试任务的 NPC。”）。 */
    val deliveredQuestList =
        currentSheet.isNpcQuestInteractionSheet() && currentSheet.items.isNotEmpty()
    val settledActionGroupId =
        if (deliveredQuestList) {
            currentSheet.activeActionGroupId
        } else {
            pending.activeActionGroupId.ifBlank { currentSheet.activeActionGroupId }
        }
    return copy(
        sheet =
            currentSheet.copy(
                detailLines =
                    if (deliveredQuestList || !pending.command.isLookInteractionCommand()) {
                        currentSheet.detailLines
                    } else {
                        mergeInteractionLines(
                            currentSheet.detailLines,
                            pending.descriptionLines,
                        )
                    },
                interactionLines =
                    if (deliveredQuestList) {
                        currentSheet.interactionLines
                    } else {
                        mergeInteractionLines(
                            currentSheet.interactionLines,
                            settledInteractionLines,
                        )
                    },
                items =
                    if (isNpcQuestMutation) {
                        currentSheet.items.map { item ->
                            if (item.questActionPendingCommand
                                    .equals(pending.command.trim(), ignoreCase = true)
                            ) {
                                item.copy(
                                    trailing = pending.command.npcQuestActionLabel(),
                                    metadata =
                                        item.metadata -
                                            SheetItemMetadata.QuestActionPending -
                                            SheetItemMetadata.QuestActionPendingCommand,
                                )
                            } else {
                                item
                            }
                        }
                    } else {
                        currentSheet.items
                    },
                actions =
                    (
                        if (isNpcQuestMutation || deliveredQuestList) {
                            currentSheet.actions
                        } else {
                            currentSheet.actionsForSettledInteraction(pending.activeActionGroupId)
                        }
                    ).map { action ->
                        if (isNpcQuestMutation && action.kind == "quest_pending" &&
                            action.command.trim().equals(pending.command.trim(), ignoreCase = true)
                        ) {
                            action.copy(label = pending.command.npcQuestActionLabel(), kind = "")
                        } else {
                            action
                        }
                    },
                activeActionGroupId = settledActionGroupId,
                loading = false,
                errorMessage = pendingError,
                retryAction = null,
            ),
    )
}

private fun ShellSheetState.actionsForSettledInteraction(activeGroupId: String): List<ShellAction> {
    if (interactionTargetKind != InteractionObjectKind.NPC) {
        return actions
    }
    val resolvedGroupId = activeGroupId.ifBlank { activeActionGroupId }.ifBlank { "root" }
    val groupActions = actionGroups.visibleActions(resolvedGroupId)
    return if (resolvedGroupId == "root") {
        groupActions.ifEmpty { actions }
    } else {
        groupActions
    }
}

internal fun ShellInteractionCaptureOrchestrator.resolveTranscriptCaptureUiPlan(
    currentState: ShellUiState,
    capturePlan: TranscriptCapturePlan,
): TranscriptCaptureUiPlan {
    return when (capturePlan) {
        TranscriptCapturePlan.None -> TranscriptCaptureUiPlan.NotHandled
        is TranscriptCapturePlan.ProtocolPopup -> {
            if (!capturePlan.result.handled) {
                TranscriptCaptureUiPlan.NotHandled
            } else if (capturePlan.result.shouldSettle) {
                TranscriptCaptureUiPlan.ProtocolPopup(shouldSettleImmediately = true)
            } else {
                TranscriptCaptureUiPlan.ProtocolPopup(
                    shouldSettleImmediately = false,
                    nextState = currentState.applyProtocolPopupCapture(capturePlan.result),
                    shouldScheduleSettle = capturePlan.result.shouldScheduleSettle,
                )
            }
        }
        is TranscriptCapturePlan.ProtocolSheet -> {
            if (!capturePlan.result.handled) {
                TranscriptCaptureUiPlan.NotHandled
            } else if (capturePlan.result.shouldSettle) {
                TranscriptCaptureUiPlan.ProtocolSheet(shouldSettleImmediately = true)
            } else {
                TranscriptCaptureUiPlan.ProtocolSheet(
                    shouldSettleImmediately = false,
                    nextState = currentState.applyProtocolSheetCapture(capturePlan.result),
                    shouldScheduleSettle = capturePlan.result.shouldScheduleSettle,
                )
            }
        }
        is TranscriptCapturePlan.Interaction -> {
            if (!capturePlan.result.handled) {
                TranscriptCaptureUiPlan.NotHandled
            } else if (capturePlan.result.shouldSettle) {
                TranscriptCaptureUiPlan.Interaction(shouldSettleImmediately = true)
            } else {
                TranscriptCaptureUiPlan.Interaction(
                    shouldSettleImmediately = false,
                    nextState = currentState.applyInteractionCapture(capturePlan.result),
                    shouldScheduleSettle = capturePlan.result.shouldScheduleSettle,
                )
            }
        }
    }
}
