package com.xiakexing.client.feature.shell

/**
 * InteractionSession 域 capture 编排——transcript 捕获计划应用、
 * 错误包结算与失效目标清理（ShellProtocolCaptureCoordinator 收敛）。
 * 调度/Job 在 InteractionCaptureState，纯变换在 ShellInteractionCaptureOrchestrator.kt。
 */
internal fun applyTranscriptCapturePlan(
    state: InteractionCaptureState,
    sink: InteractionSessionSink,
    interactionCaptureOrchestrator: ShellInteractionCaptureOrchestrator,
    capturePlan: TranscriptCapturePlan,
): Boolean {
    if (capturePlan is TranscriptCapturePlan.Interaction &&
        capturePlan.result.feedbackLine.isNotBlank()
    ) {
        if (capturePlan.result.resetInteractionToRoot) {
            val currentState = sink.currentState()
            val currentSheet = currentState.sheet
            val pendingTarget = state.pendingInteractionRequest?.target
            val target =
                pendingTarget?.takeIf { it.kind == InteractionObjectKind.NPC }
                    ?: resolveInteractionTarget(
                        pending = null,
                        currentSheet = currentSheet,
                        interactionObjects = currentState.interactionObjects,
                    )?.takeIf { it.kind == InteractionObjectKind.NPC }
            val rootSheet =
                target?.let {
                    currentState.buildNpcContextResumeSheet(
                        target = it,
                        hasGiveableItems = currentState.hasGiveableInventoryItems(),
                    )
                } ?: currentSheet?.copy(
                    interactionLines = emptyList(),
                    activeActionGroupId = "root",
                    interactionMode = InteractionSheetMode.DETAIL,
                    actions =
                        if (currentSheet.interactionTargetKind == InteractionObjectKind.NPC) {
                            currentSheet.actionGroups
                                .visibleActions("root")
                                .ifEmpty { currentSheet.actions }
                                .filterUsefulNpcActions(currentState.hasGiveableInventoryItems())
                        } else {
                            currentSheet.actions
                        },
                    loading = false,
                )
            sink.updateState {
                withSheet(rootSheet).withNpcQuestEmptyFeedback(
                    target?.uiKey.orEmpty()
                        .ifBlank { target?.id.orEmpty() }
                        .ifBlank {
                            currentSheet?.interactionTargetUiKey
                                .orEmpty()
                                .ifBlank { currentSheet?.interactionTargetId.orEmpty() }
                        },
                )
            }
        } else {
            sink.appendInteractionFeedback(capturePlan.result.feedbackLine)
        }
        state.clearPendingInteractionRequest()
        return true
    }
    return when (
        val uiPlan =
            interactionCaptureOrchestrator.resolveTranscriptCaptureUiPlan(
                currentState = sink.currentState(),
                capturePlan = capturePlan,
            )
    ) {
        TranscriptCaptureUiPlan.NotHandled -> false
        is TranscriptCaptureUiPlan.ProtocolPopup -> applyProtocolPopupUiPlan(state, sink, uiPlan)
        is TranscriptCaptureUiPlan.ProtocolSheet -> applyProtocolSheetUiPlan(state, sink, uiPlan)
        is TranscriptCaptureUiPlan.Interaction -> applyInteractionUiPlan(state, sink, uiPlan)
    }
}

internal fun capturePendingError(
    state: InteractionCaptureState,
    sink: InteractionSessionSink,
    message: String,
): Boolean {
    val command = state.pendingInteractionRequest?.command.orEmpty().trim().lowercase()
    if (command.startsWith("get ") || command.startsWith("quest action ")) {
        sink.showFloatingFeedback(message)
        sink.updateState { copy(sheet = sheet?.copy(loading = false)) }
        state.clearPendingInteractionRequest()
        return true
    }
    when (state.capturePendingError(message)) {
        PendingErrorCapture.None -> return false
        PendingErrorCapture.ProtocolPopup -> {
            state.settlePendingProtocolPopupRequest(clearAfterSettle = true)
            return true
        }
        PendingErrorCapture.ProtocolSheet -> {
            state.settlePendingProtocolSheetRequest(clearAfterSettle = true)
            return true
        }
        PendingErrorCapture.ProtocolSheetNotice -> {
            sink.appendInteractionFeedback("这个物品已不在这里")
            sink.updateState {
                copy(sheet = sheet?.copy(loading = false))
            }
            state.clearPendingProtocolSheetRequest()
            return true
        }
        PendingErrorCapture.Interaction -> {
            state.settlePendingInteractionRequest(clearAfterSettle = true)
            return true
        }
        PendingErrorCapture.InteractionNotice -> {
            invalidatePendingInteractionTarget(state, sink, state.pendingInteractionRequest)
            return true
        }
        PendingErrorCapture.CombatNotice -> {
            sink.showFloatingFeedback(message)
            state.clearPendingInteractionRequest()
            return true
        }
    }
}

private fun applyProtocolPopupUiPlan(
    state: InteractionCaptureState,
    sink: InteractionSessionSink,
    uiPlan: TranscriptCaptureUiPlan.ProtocolPopup,
): Boolean {
    if (uiPlan.shouldSettleImmediately) {
        state.settlePendingProtocolPopupRequest(clearAfterSettle = true)
    } else {
        uiPlan.nextState?.let { nextState -> sink.updateState { nextState } }
        if (uiPlan.shouldScheduleSettle) {
            state.scheduleProtocolPopupRequestSettle()
        }
    }
    return true
}

private fun applyProtocolSheetUiPlan(
    state: InteractionCaptureState,
    sink: InteractionSessionSink,
    uiPlan: TranscriptCaptureUiPlan.ProtocolSheet,
): Boolean {
    if (uiPlan.shouldSettleImmediately) {
        state.settlePendingProtocolSheetRequest(clearAfterSettle = true)
    } else {
        uiPlan.nextState?.let { nextState -> sink.updateState { nextState } }
        if (uiPlan.shouldScheduleSettle) {
            state.scheduleProtocolSheetRequestSettle()
        }
    }
    return true
}

private fun applyInteractionUiPlan(
    state: InteractionCaptureState,
    sink: InteractionSessionSink,
    uiPlan: TranscriptCaptureUiPlan.Interaction,
): Boolean {
    if (uiPlan.shouldSettleImmediately) {
        state.settlePendingInteractionRequest(clearAfterSettle = true)
    } else {
        uiPlan.nextState?.let { nextState -> sink.updateState { nextState } }
        if (uiPlan.shouldScheduleSettle) {
            state.scheduleInteractionRequestSettle()
        }
    }
    return true
}

private fun invalidatePendingInteractionTarget(
    state: InteractionCaptureState,
    sink: InteractionSessionSink,
    pending: PendingInteractionRequest?,
) {
    val target = pending?.target
    if (target == null || target.kind == InteractionObjectKind.NPC) {
        sink.appendInteractionFeedback(pending.lookFailureNoticeText())
        state.clearPendingInteractionRequest()
        return
    }

    val silentPickupMiss = pending.command.trim().lowercase().startsWith("get ")
    val invalidatedInteractionKey = target.uiKey.ifBlank { target.id }
    val commandTarget = target.commandTarget().routingToken()
    if (!silentPickupMiss) {
        sink.appendInteractionFeedback("这个物品已不在这里")
    }
    sink.updateState {
        var removedRoomItem = false
        val nextItems =
            if (commandTarget.isBlank()) {
                items
            } else {
                items.filterNot { item ->
                    if (removedRoomItem) {
                        false
                    } else {
                        val matches =
                            item.id.routingToken() == commandTarget ||
                                item.name.routingToken() == commandTarget
                        if (matches) {
                            removedRoomItem = true
                        }
                        matches
                    }
                }
            }
        val currentSheet = sheet
        val currentTargetsInvalidated =
            currentSheet.targetsInvalidatedInteraction(
                targetId = target.id,
                targetUiKey = invalidatedInteractionKey,
            )
        copy(
            items = nextItems,
            interactionObjects =
                interactionObjects.filterNot { interactionObject ->
                    interactionObject.uiKey == invalidatedInteractionKey
                },
            sheet = if (currentTargetsInvalidated) null else currentSheet,
            dialog = if (currentTargetsInvalidated) null else dialog,
        )
    }
    state.clearPendingInteractionRequest()
}
