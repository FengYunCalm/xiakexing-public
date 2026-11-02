package com.xiakexing.client.feature.shell

/**
 * Surface 域统一 Sink——4 个原 coordinator Sink 的并集
 * （navigation/sheet-navigation/dismissal/action 全部表面副作用）。
 */
internal interface SurfaceSink {
    fun currentState(): ShellUiState

    fun updateState(transform: ShellUiState.() -> ShellUiState)

    // ===== navigation 域 =====

    fun clearPendingInteractionRequest()

    fun closeChatPanel()

    fun showCharacterHubPanel()

    fun showActionHubPanel()

    fun showWorldHubPanel()

    fun requestQuestListFromDock()

    fun showMapPanel()

    fun showMessagePanel()

    // ===== sheet-navigation 域 =====

    fun popSheet(): Boolean

    // ===== dismissal 域 =====

    fun clearPendingProtocolSheetRequest()

    fun clearPendingProtocolPopupRequest()

    fun clearPendingInventoryInspectItem()

    fun clearBankQuantityState()

    fun clearQuantityFlowState()

    fun dismissCommandPalette()

    fun dismissExitOverlay()

    fun consumeDismissSheetOnPopupAction(): Boolean

    fun clearDismissSheetOnPopupAction()

    fun showActionConfirmation(
        title: String,
        action: ShellAction,
        dismissSheetAfterConfirm: Boolean,
    )

    // ===== action 域 =====

    fun checkForClientUpdate(silent: Boolean)

    fun downloadClientUpdate()

    fun appendInteractionFeedback(line: String)

    fun skillSummariesForPicker(): List<com.xiakexing.client.core.protocol.payload.SkillSummaryPacket>

    fun showFloatingNotice(text: String)

    fun showFacilitySkillPicker(group: ShellActionGroup)

    fun sendDirectCommand(
        command: String,
        suppressImmediateScroll: Boolean = false,
    )

    fun sendRouteCommandSequence(commands: List<String>)

    fun dismissPopup()

    fun dismissPopupGroup()

    fun dismissSheet()

    fun prepareProtocolPopupCommand(
        currentDialog: ShellPopupState?,
        command: String = "",
    )

    fun prepareProtocolSheetCommand(
        currentSheet: ShellSheetState?,
        command: String,
    )

    fun prepareInteractionSheetCommand(
        command: String,
        requestedGroupId: String = "",
    ): Boolean

    fun openInteractionGiveItemsSheet()

    fun returnToInteractionObjectRoot()

    fun returnToInteractionRoot()

    fun showApprenticeUnavailablePopup()

    fun showBankRootSheet()

    fun showBankDepositSheet()

    fun showBankWithdrawSheet()

    fun showQuestListSheet()

    fun showQuestDetailSheet(questId: String)

    fun showActivityPanel()

    fun trackQuest(questId: String)

    fun clearTrackedQuest()

    fun toggleReduceMotion()

    fun showCharacterPanel()

    fun showAttributePanel()

    fun showInventoryPanel(categoryId: String = "all")

    fun showEquipmentPanel()

    fun showFunctionPanel()

    fun showWorldShopGuidePanel()

    fun showSettingsPanel()

    fun showSkillsPanel()

    fun showCombatPerformPicker(slotIndex: Int)

    fun showCombatPerformSkillPicker(
        slotIndex: Int,
        skillId: String,
    )

    fun handleAssignCombatPerformCommand(command: String)

    fun removeCombatPerform(skillId: String)

    fun requestRemoveCombatPerform(skillId: String)

    fun showHelpPanel(categoryId: String)

    fun showHelpDetail(entryId: String)

    fun requestInventoryItemDetail(itemId: String)

    fun showSkillsPanel(categoryId: String)

    fun requestSkillDetail(skillId: String)

    fun showSkillBreakthroughChoices(skillId: String)

    fun selectLearningSkillCategory(categoryId: String)

    fun showGiveQuantityPopup(command: String)

    fun showTradeMoneyPopup()

    fun submitPendingGiveQuantity(amount: Int)

    fun submitPendingBankQuantity(amount: String)

    fun submitPendingTradeMoney(amount: String)

    fun submitPendingGiveAll()

    fun showLearnQuantityPopup(command: String)

    fun submitPendingLearnQuantity(levels: String)
}
