package com.xiakexing.client.feature.shell

import com.google.gson.JsonObject
import com.xiakexing.client.core.protocol.payload.DialogPacket

/**
 * ShellViewModel 委托方法（VM 瘦身——面板/弹窗/协议捕获
 * 副作用全部经 [ShellFacade] 指向 coordinator，测试调用 viewModel.xxx() 不变）。
 */
internal fun ShellViewModel.handleSurfaceCommandRoute(route: ShellSurfaceCommandRoute) =
    requireShellFacade.handleSurfaceCommandRoute(route)

internal fun ShellViewModel.showCombatExitOverlay() = requireShellFacade.showCombatExitOverlay()

internal fun ShellViewModel.openInteractionGiveItemsSheet() =
    requireShellFacade.openInteractionGiveItemsSheet()

internal fun ShellViewModel.returnToInteractionObjectRoot() =
    requireShellFacade.returnToInteractionObjectRoot()

internal fun ShellViewModel.returnToInteractionRoot() = requireShellFacade.returnToInteractionRoot()

internal fun ShellViewModel.preserveInteractionSessionForDraft(currentSheet: ShellSheetState) =
    requireShellFacade.preserveInteractionSessionForDraft(currentSheet)

internal fun ShellViewModel.prepareInteractionSheetCommand(
    command: String,
    requestedGroupId: String = "",
): Boolean = requireShellFacade.prepareInteractionSheetCommand(command, requestedGroupId)

internal fun ShellViewModel.handleLoginPromptReady() {
    connectionRepository.markLoginResultPending()
    requireShellFacade.handleLoginPromptReceived()
    requireShellFacade.handleLoginPromptReady()
}

internal fun ShellViewModel.handleLoginBootstrapComplete() {
    // 仅记录服务端 bootstrap 标记；房间四段权威快照提交后由
    // RoomProjectionCoordinator 通过 owner 事件推进 Playing。
    connectionRepository.markLoginBootstrapCompleted()
}

internal fun ShellViewModel.handleBatchPacket(payload: JsonObject) =
    requireShellFacade.handleBatchPacket(payload)

internal fun ShellViewModel.requestInventoryItemDetail(itemId: String) =
    requireShellFacade.requestInventoryItemDetail(itemId)

internal fun ShellViewModel.requestSceneItemDetail(target: InteractionObject) =
    requireShellFacade.requestSceneItemDetail(target)

internal fun ShellViewModel.showSkillBreakthroughChoices(skillId: String) =
    requireShellFacade.showSkillBreakthroughChoices(skillId)

internal fun ShellViewModel.applySessionEvent(event: ShellEvent) =
    requireShellFacade.applySessionEvent(event)

internal fun ShellViewModel.appendTranscript(line: String) =
    requireShellFacade.appendTranscript(line)

internal fun ShellViewModel.appendErrorFeedback(message: String) =
    requireShellFacade.appendErrorFeedback(message)

internal fun ShellViewModel.appendNotice(line: String) = requireShellFacade.appendNotice(line)

internal fun ShellViewModel.appendChatTranscriptAndNotice(
    line: String,
    duplicateRawEcho: String? = null,
) = requireShellFacade.appendChatTranscriptAndNotice(line, duplicateRawEcho)

internal fun ShellViewModel.showCommandPalette() = requireShellFacade.showCommandPalette()

internal fun ShellViewModel.showGiveQuantityPopup(command: String) =
    requireShellFacade.showGiveQuantityPopup(command)

internal fun ShellViewModel.submitPendingGiveQuantity(amount: Int) =
    requireShellFacade.submitPendingGiveQuantity(amount)

internal fun ShellViewModel.submitPendingGiveAll() = requireShellFacade.submitPendingGiveAll()

internal fun ShellViewModel.showLearnQuantityPopup(command: String) =
    requireShellFacade.showLearnQuantityPopup(command)

internal fun ShellViewModel.submitPendingLearnQuantity(levels: String) =
    requireShellFacade.submitPendingLearnQuantity(levels)

internal fun ShellViewModel.handleMailDialog(packet: DialogPacket): Boolean =
    requireShellFacade.handleMailDialog(packet)

internal fun ShellViewModel.handleNpcLearningDialog(packet: DialogPacket): Boolean =
    requireShellFacade.handleNpcLearningDialog(packet)

internal fun ShellViewModel.handleInventoryPacket(payload: JsonObject) =
    requireShellFacade.handleInventoryPacket(payload)

internal fun ShellViewModel.showHelpPanel(categoryId: String = DEFAULT_HELP_PANEL_CATEGORY) =
    requireShellFacade.showHelpPanel(categoryId)

internal fun ShellViewModel.showHelpDetail(entryId: String) =
    requireShellFacade.showHelpDetail(entryId)

internal fun ShellViewModel.showCharacterPanel() = requireShellFacade.showCharacterPanel()

internal fun ShellViewModel.showCharacterHubPanel() = requireShellFacade.showCharacterHubPanel()

internal fun ShellViewModel.showQuestListSheet(refresh: Boolean = false) =
    requireShellFacade.showQuestListSheet(refresh)

internal fun ShellViewModel.requestQuestListFromDock() =
    requireShellFacade.requestQuestListFromDock()

internal fun ShellViewModel.showQuestDetailSheet(questId: String) =
    requireShellFacade.showQuestDetailSheet(questId)

internal fun ShellViewModel.clearTrackedQuest() = requireShellFacade.clearTrackedQuest()

internal fun ShellViewModel.showAttributePanel() = requireShellFacade.showAttributePanel()

internal fun ShellViewModel.showFunctionPanel() = requireShellFacade.showFunctionPanel()

internal fun ShellViewModel.showSettingsPanel() = requireShellFacade.showSettingsPanel()

internal fun ShellViewModel.showMailPanel() = requireShellFacade.showMailPanel()

internal fun ShellViewModel.showMessagePanel() = showMailPanel()

internal fun ShellViewModel.showActivityPanel() = requireShellFacade.showActivityPanel()

internal fun ShellViewModel.showActionHubPanel() = requireShellFacade.showActionHubPanel()

internal fun ShellViewModel.showCombatPerformPicker(slotIndex: Int) =
    requireShellFacade.showCombatPerformPicker(slotIndex)

internal fun ShellViewModel.showCombatPerformSkillPicker(
    slotIndex: Int,
    skillId: String,
) = requireShellFacade.showCombatPerformSkillPicker(slotIndex, skillId)

internal fun ShellViewModel.prepareProtocolSheetCommand(currentSheet: ShellSheetState?) =
    requireShellFacade.prepareProtocolSheetCommand(currentSheet)

internal fun ShellViewModel.prepareProtocolSheetCommand(
    currentSheet: ShellSheetState?,
    command: String,
) = requireShellFacade.prepareProtocolSheetCommand(currentSheet, command)

internal fun ShellViewModel.prepareProtocolSheetCommand(
    currentSheet: ShellSheetState,
    command: String,
    preservedLinesOverride: List<String>,
) = requireShellFacade.prepareProtocolSheetCommand(
    currentSheet,
    command,
    preservedLinesOverride,
)

internal fun ShellViewModel.prepareHiddenProtocolSheetCommand(
    currentSheet: ShellSheetState?,
    command: String,
) = requireShellFacade.prepareHiddenProtocolSheetCommand(currentSheet, command)

internal fun ShellViewModel.prepareProtocolPopupCommand(currentDialog: ShellPopupState?) =
    requireShellFacade.prepareProtocolPopupCommand(currentDialog)

internal fun ShellViewModel.clearPendingProtocolSheetRequest() =
    requireShellFacade.clearPendingProtocolSheetRequest()

internal fun ShellViewModel.clearPendingProtocolPopupRequest() =
    requireShellFacade.clearPendingProtocolPopupRequest()

internal fun ShellViewModel.capturePendingError(message: String): Boolean =
    requireShellFacade.capturePendingError(message)

internal fun ShellViewModel.scheduleInteractionRequestSettle() =
    requireShellFacade.scheduleInteractionRequestSettle()

internal fun ShellViewModel.armPendingMoveAutoScroll() =
    requireShellFacade.armPendingMoveAutoScroll()

internal fun ShellViewModel.activeResumeTokenForTest(): String =
    requireShellFacade.activeResumeTokenForTest()

internal fun ShellViewModel.setPendingResumeLoginTokenForTest(token: String) {
    requireShellFacade.setPendingResumeLoginTokenForTest(token)
}
