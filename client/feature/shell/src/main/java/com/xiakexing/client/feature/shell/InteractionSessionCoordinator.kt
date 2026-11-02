package com.xiakexing.client.feature.shell

import com.google.gson.JsonObject
import com.xiakexing.client.core.network.CommandRequestIdentity
import com.xiakexing.client.core.protocol.payload.DialogPacket

/**
 * InteractionSession 域 owner——会话状态、capture 入口与
 * dialog/sheet/mail/npc-learning 编排入口（6 个原 coordinator 收敛）。
 * 调度/Job 在 InteractionCaptureState，编排在 InteractionSessionOrchestration.kt /
 * DialogOrchestration.kt / SheetOrchestration.kt / MailDialogReducer.kt /
 * NpcLearningDialogReducer.kt。
 */
internal class InteractionSessionCoordinator(
    private val interactionCaptureState: InteractionCaptureState,
    private val interactionCaptureOrchestrator: ShellInteractionCaptureOrchestrator,
    private val sink: InteractionSessionSink,
) {
    private val capture = interactionCaptureState

    init {
        capture.attachSinks(
            onCombatTimeout = { sink.showFloatingFeedback("未能发起战斗，请稍后再试") },
            applyInteraction = { settled -> sink.updateState { applySettledInteraction(settled) } },
            applyProtocolSheet = {
                    pending ->
                sink.updateState { applySettledProtocolSheet(pending) }
            },
            applyProtocolPopup = {
                    pending ->
                sink.updateState { applySettledProtocolPopup(pending) }
            },
        )
    }

    // ===== capture 状态访问（原 InteractionCaptureCoordinator） =====

    fun pendingInteractionRequest(): PendingInteractionRequest? = capture.pendingInteractionRequest

    fun pendingProtocolSheetRequest(): PendingProtocolSheetRequest? =
        capture.pendingProtocolSheetRequest

    fun replacePendingInteractionRequest(request: PendingInteractionRequest?) {
        capture.replacePendingInteractionRequest(request)
    }

    fun clearPendingInteractionRequest() {
        capture.clearPendingInteractionRequest()
    }

    fun clearPendingProtocolSheetRequest() {
        capture.clearPendingProtocolSheetRequest()
    }

    fun clearPendingProtocolPopupRequest() {
        capture.clearPendingProtocolPopupRequest()
    }

    fun bindPendingCommandRequest(
        request: CommandRequestIdentity,
        command: String,
    ) {
        capture.bindPendingCommandRequest(request, command)
    }

    fun rollbackPendingCommandRequest(request: CommandRequestIdentity) {
        capture.rollbackPendingCommandRequest(request)
    }

    fun acceptsCommandResponseForPending(
        commandResponseId: String,
        connectionGeneration: Long? = null,
    ): Boolean = capture.acceptsCommandResponseForPending(commandResponseId, connectionGeneration)

    fun hasTaggedPendingCommandRequest(): Boolean = capture.hasTaggedPendingCommandRequest()

    fun pendingCommandResponseId(): String = capture.pendingCommandResponseId()

    fun keepCurrentInteractionPendingHidden() {
        capture.keepCurrentInteractionPendingHidden()
    }

    fun settleIfAwaitingQuestAnswer() {
        capture.settleIfAwaitingQuestAnswer()
    }

    fun hasPendingCombatStart(): Boolean = capture.hasPendingCombatStart()

    fun pendingCombatStartFailure(line: String): String? = capture.pendingCombatStartFailure(line)

    fun settlePendingNpcQuestEmpty() {
        capture.settlePendingNpcQuestEmpty()
    }

    fun shouldConsumeSettledNpcQuestDialog(providerId: String): Boolean =
        capture.shouldConsumeSettledNpcQuestDialog(providerId)

    // ===== 协程调度入口（Job 在 InteractionCaptureState） =====

    fun cancelProtocolSheetSettle() {
        capture.cancelProtocolSheetSettle()
    }

    fun cancelInteractionRequestSettle() {
        capture.cancelInteractionRequestSettle()
    }

    fun clearInteractionRequestSettleJob() {
        capture.clearInteractionRequestSettleJob()
    }

    fun scheduleInteractionRequestSettle() {
        capture.scheduleInteractionRequestSettle()
    }

    fun applyPromptSettlePlan(settlePlan: PromptSettlePlan) {
        if (settlePlan.settleProtocolPopup) {
            capture.settlePendingProtocolPopupRequest(clearAfterSettle = true)
        }
        if (settlePlan.settleProtocolSheet) {
            capture.settlePendingProtocolSheetRequest(clearAfterSettle = true)
        }
        if (settlePlan.settleInteraction) {
            capture.settlePendingInteractionRequest(clearAfterSettle = true)
        }
    }

    fun applyTranscriptCapturePlan(capturePlan: TranscriptCapturePlan): Boolean {
        return applyTranscriptCapturePlan(
            capture,
            sink,
            interactionCaptureOrchestrator,
            capturePlan,
        )
    }

    fun prepareProtocolSheetCommand(currentSheet: ShellSheetState?) {
        prepareProtocolSheetCommand(
            currentSheet,
            command = currentSheet?.primaryAction?.command.orEmpty(),
        )
    }

    fun prepareProtocolSheetCommand(
        currentSheet: ShellSheetState?,
        command: String,
    ) {
        if (currentSheet == null) {
            return
        }
        capture.prepareProtocolSheetCommand(currentSheet, command)
        if (capture.pendingProtocolSheetRequest == null) return
        if (currentSheet.shouldPreserveProtocolSheetSurface(command)) {
            return
        }
        sink.updateState {
            copy(
                sheet =
                    currentSheet.copy(
                        loading = true,
                        errorMessage = "",
                        retryAction = null,
                    ),
                dialog = null,
            )
        }
    }

    fun prepareProtocolSheetCommand(
        currentSheet: ShellSheetState,
        command: String,
        preservedLinesOverride: List<String>,
    ) {
        capture.prepareProtocolSheetCommand(
            currentSheet = currentSheet,
            command = command,
            preservedLinesOverride = preservedLinesOverride,
        )
    }

    fun prepareHiddenProtocolSheetCommand(
        currentSheet: ShellSheetState?,
        command: String,
    ) {
        if (currentSheet == null) {
            return
        }
        capture.prepareProtocolSheetCommand(currentSheet, command)
    }

    fun prepareProtocolPopupCommand(
        currentDialog: ShellPopupState?,
        command: String = "",
    ) {
        if (currentDialog == null) {
            return
        }
        capture.prepareProtocolPopupCommand(currentDialog, command)
        sink.updateState {
            copy(
                dialog = currentDialog,
            )
        }
    }

    fun capturePendingError(message: String): Boolean {
        return capturePendingError(capture, sink, message)
    }

    // ===== dialog / sheet / mail / npc-learning 编排入口 =====

    fun handleDialogPacket(
        payload: JsonObject,
        commandResponseId: String = "",
    ) {
        if (!acceptsCommandResponseForPending(commandResponseId)) return
        handleDialogPacket(capture, sink, payload)
    }

    fun handleDialogPacket(
        packet: DialogPacket,
        commandResponseId: String = "",
    ) {
        if (!acceptsCommandResponseForPending(commandResponseId)) return
        handleDialogPacket(capture, sink, packet)
    }

    fun showInteractionObjectPopup(objectId: String) {
        showInteractionObjectPopup(capture, sink, objectId)
    }

    fun openInteractionGiveItemsSheet() {
        openInteractionGiveItemsSheet(capture, sink)
    }

    fun returnToInteractionRoot() {
        returnToInteractionRoot(capture, sink)
    }

    fun returnToInteractionObjectRoot() {
        returnToInteractionObjectRoot(capture, sink)
    }

    fun preserveInteractionSessionForDraft(currentSheet: ShellSheetState) {
        preserveInteractionSessionForDraft(capture, sink, currentSheet)
    }

    fun prepareInteractionSheetCommand(
        command: String,
        requestedGroupId: String = "",
    ): Boolean {
        return prepareInteractionSheetCommand(sink, command, requestedGroupId)
    }

    fun handleMailDialog(packet: DialogPacket): Boolean {
        return handleMailDialog(capture, sink, packet)
    }

    fun handleNpcLearningDialog(packet: DialogPacket): Boolean {
        return handleNpcLearningDialog(capture, sink, packet)
    }
}
