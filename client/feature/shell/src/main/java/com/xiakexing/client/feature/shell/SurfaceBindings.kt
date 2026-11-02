package com.xiakexing.client.feature.shell

/**
 * Surface 域 factory——只接收显式依赖，返回不可变 [SurfaceOwners]。
 * 域内 surface ↔ quantityFlow 互引用用本地 [LazyRef] 断环；统一 SurfaceSink
 * 实现（4 个原 Sink 并集）保持不变。
 */
internal data class SurfaceOwners(
    val surfaceCoordinator: SurfaceCoordinator,
    val quantityFlowCoordinator: QuantityCoordinator,
)

internal class SurfaceBindings(
    private val deps: ShellSharedDeps,
    private val interactionSession: LazyRef<InteractionSessionCoordinator>,
    private val panel: LazyRef<PanelCoordinator>,
    private val skill: LazyRef<SkillCoordinator>,
    private val inventoryTrade: LazyRef<InventoryTradeCoordinator>,
    private val messageRef: LazyRef<MessageCoordinator>,
    private val commandCoordinator: CommandCoordinator,
    private val questCoordinator: QuestCoordinator,
    private val clientUpdateCoordinator: UpdatePromptCoordinator,
) {
    fun build(): SurfaceOwners {
        val stateBridge = deps.stateBridge
        val vmBridge = deps.vmBridge
        // 域内环：surface Sink 引用 quantityFlow（本域后构造）与 surface 自身。
        val quantityRef = LazyRef<QuantityCoordinator>()
        val surfaceRef = LazyRef<SurfaceCoordinator>()

        val surfaceCoordinator =
            SurfaceCoordinator(
                object : SurfaceSink {
                    override fun currentState(): ShellUiState = stateBridge.currentUiState()

                    override fun updateState(transform: ShellUiState.() -> ShellUiState) =
                        stateBridge.updateState(
                            transform,
                        )

                    // ===== navigation 域 =====

                    override fun clearPendingInteractionRequest() =
                        interactionSession.get().clearPendingInteractionRequest()

                    override fun closeChatPanel() = commandCoordinator.closeChatPanel()

                    override fun showCharacterHubPanel() = panel.get().showCharacterHubPanel()

                    override fun showActionHubPanel() = skill.get().showActionHubPanel()

                    override fun showWorldHubPanel() = panel.get().showWorldHubPanel()

                    override fun requestQuestListFromDock() =
                        questCoordinator.requestQuestListFromDock()

                    override fun showMapPanel() = panel.get().showMapPanel()

                    override fun showMessagePanel() = panel.get().showMailPanel()

                    // ===== sheet-navigation 域 =====

                    override fun popSheet(): Boolean = surfaceRef.get().pop()

                    // ===== dismissal 域 =====

                    override fun clearPendingProtocolSheetRequest() =
                        interactionSession.get().clearPendingProtocolSheetRequest()

                    override fun clearPendingProtocolPopupRequest() =
                        interactionSession.get().clearPendingProtocolPopupRequest()

                    override fun clearPendingInventoryInspectItem() =
                        inventoryTrade.get().clearPendingInventoryInspectItem()

                    override fun clearBankQuantityState() = quantityRef.get().clear()

                    override fun clearQuantityFlowState() = quantityRef.get().clear()

                    override fun dismissCommandPalette() {
                        stateBridge.updateState { dismissCommandPalette() }
                    }

                    override fun dismissExitOverlay() {
                        stateBridge.updateState {
                            if (exitOverlay == null) this else copy(exitOverlay = null)
                        }
                    }

                    override fun consumeDismissSheetOnPopupAction(): Boolean =
                        surfaceRef.get().consumeDismissSheetOnPopupAction()

                    override fun clearDismissSheetOnPopupAction() =
                        surfaceRef.get().clearDismissSheetOnPopupAction()

                    override fun showActionConfirmation(
                        title: String,
                        action: ShellAction,
                        dismissSheetAfterConfirm: Boolean,
                    ) = surfaceRef.get().showActionConfirmation(
                        title,
                        action,
                        dismissSheetAfterConfirm,
                    )

                    // ===== action 域 =====

                    override fun checkForClientUpdate(silent: Boolean) =
                        clientUpdateCoordinator.checkForClientUpdate(
                            silent,
                        )

                    override fun downloadClientUpdate() =
                        clientUpdateCoordinator.downloadClientUpdate()

                    override fun appendInteractionFeedback(line: String) =
                        messageRef.get().appendNarrativeTranscript(
                            line,
                        )

                    override fun skillSummariesForPicker() = skill.get().skillSummaries()

                    override fun showFloatingNotice(text: String) =
                        messageRef.get().appendFloatingNotice(
                            text,
                        )

                    override fun showFacilitySkillPicker(group: ShellActionGroup) {
                        stateBridge.updateState {
                            // 第五轮 R4：选技承载复用当前 sheet；取消恢复由 dismissSheet 的
                            // R-35 重建分支承担（从 interactionObjects 重建 NPC 面板）。
                            copy(sheet = sheet?.copy(actionGroups = listOf(group)))
                        }
                    }

                    override fun sendDirectCommand(
                        command: String,
                        suppressImmediateScroll: Boolean,
                    ) = vmBridge.sendDirectCommand(
                        command,
                        clearInputAfterSend = false,
                        suppressImmediateScroll = suppressImmediateScroll,
                        bypassThrottle = false,
                    )

                    override fun sendRouteCommandSequence(commands: List<String>) =
                        questCoordinator.startRouteCommands(
                            commands,
                        )

                    override fun dismissPopup() = surfaceRef.get().dismissPopup()

                    override fun dismissPopupGroup() = surfaceRef.get().dismissPopupGroup()

                    override fun dismissSheet() = surfaceRef.get().dismissSheet()

                    override fun prepareProtocolPopupCommand(
                        currentDialog: ShellPopupState?,
                        command: String,
                    ) = interactionSession.get().prepareProtocolPopupCommand(currentDialog, command)

                    override fun prepareProtocolSheetCommand(
                        currentSheet: ShellSheetState?,
                        command: String,
                    ) = interactionSession.get().prepareProtocolSheetCommand(currentSheet, command)

                    override fun prepareInteractionSheetCommand(
                        command: String,
                        requestedGroupId: String,
                    ): Boolean =
                        interactionSession.get().prepareInteractionSheetCommand(
                            command,
                            requestedGroupId,
                        )

                    override fun openInteractionGiveItemsSheet() =
                        interactionSession.get().openInteractionGiveItemsSheet()

                    override fun returnToInteractionObjectRoot() =
                        interactionSession.get().returnToInteractionObjectRoot()

                    override fun returnToInteractionRoot() =
                        interactionSession.get().returnToInteractionRoot()

                    override fun showApprenticeUnavailablePopup() =
                        vmBridge.showApprenticeUnavailablePopup()

                    override fun showBankRootSheet() = quantityRef.get().showBankRootSheet()

                    override fun showBankDepositSheet() =
                        quantityRef.get().showBankQuantityPopup(
                            BankOperation.DEPOSIT,
                        )

                    override fun showBankWithdrawSheet() =
                        quantityRef.get().showBankQuantityPopup(
                            BankOperation.WITHDRAW,
                        )

                    override fun showQuestListSheet() = questCoordinator.showQuestListSheet()

                    override fun showQuestDetailSheet(questId: String) =
                        questCoordinator.showQuestDetailSheet(
                            questId,
                        )

                    override fun showActivityPanel() = panel.get().showActivityPanel()

                    override fun trackQuest(questId: String) =
                        questCoordinator.trackQuest(
                            questCoordinator.latestQuestEntry(questId),
                        )

                    override fun clearTrackedQuest() = questCoordinator.clearTrackedQuest()

                    override fun toggleReduceMotion() {
                        stateBridge.updateState {
                            copy(
                                reduceMotion = !reduceMotion,
                                sheet = buildSettingsSheetState(reduceMotion = !reduceMotion),
                            )
                        }
                    }

                    override fun showCharacterPanel() = panel.get().showCharacterPanel()

                    override fun showAttributePanel() = panel.get().showAttributePanel()

                    override fun showInventoryPanel(categoryId: String) =
                        inventoryTrade.get().showInventoryPanel(
                            categoryId,
                        )

                    override fun showEquipmentPanel() = inventoryTrade.get().showEquipmentPanel()

                    override fun showFunctionPanel() = panel.get().showFunctionPanel()

                    override fun showWorldShopGuidePanel() =
                        inventoryTrade.get().showWorldShopGuidePanel()

                    override fun showSettingsPanel() = panel.get().showSettingsPanel()

                    override fun showSkillsPanel() = skill.get().showSkillsPanel()

                    override fun showCombatPerformPicker(slotIndex: Int) =
                        skill.get().showCombatPerformPicker(
                            slotIndex,
                        )

                    override fun showCombatPerformSkillPicker(
                        slotIndex: Int,
                        skillId: String,
                    ) = skill.get().showCombatPerformSkillPicker(slotIndex, skillId)

                    override fun handleAssignCombatPerformCommand(command: String) =
                        skill.get().handleAssignCombatPerformCommand(
                            command,
                        )

                    override fun removeCombatPerform(skillId: String) =
                        skill.get().removeCombatPerform(
                            skillId,
                        )

                    override fun requestRemoveCombatPerform(skillId: String) =
                        skill.get().requestRemoveCombatPerform(
                            skillId,
                        )

                    override fun showHelpPanel(categoryId: String) =
                        panel.get().showHelpPanel(
                            categoryId,
                        )

                    override fun showHelpDetail(entryId: String) =
                        panel.get().showHelpDetail(
                            entryId,
                        )

                    override fun requestInventoryItemDetail(itemId: String) =
                        inventoryTrade.get().requestInventoryItemDetail(
                            itemId,
                        )

                    override fun showSkillsPanel(categoryId: String) =
                        skill.get().showSkillsPanel(
                            categoryId,
                            refresh = false,
                        )

                    override fun requestSkillDetail(skillId: String) =
                        skill.get().requestSkillDetail(
                            skillId,
                        )

                    override fun showSkillBreakthroughChoices(skillId: String) =
                        skill.get().showSkillBreakthroughChoices(
                            skillId,
                        )

                    override fun selectLearningSkillCategory(categoryId: String) =
                        vmBridge.selectLearningSkillCategory(
                            categoryId,
                        )

                    override fun showGiveQuantityPopup(command: String) =
                        quantityRef.get().showGiveQuantityPopup(
                            command,
                        )

                    override fun showTradeMoneyPopup() = quantityRef.get().showTradeMoneyPopup()

                    override fun submitPendingGiveQuantity(amount: Int) =
                        quantityRef.get().submitPendingGiveQuantity(
                            amount,
                        )

                    override fun submitPendingBankQuantity(amount: String) =
                        quantityRef.get().submitPendingBankQuantity(
                            amount,
                        )

                    override fun submitPendingTradeMoney(amount: String) =
                        quantityRef.get().submitPendingTradeMoney(
                            amount,
                        )

                    override fun submitPendingGiveAll() = quantityRef.get().submitPendingGiveAll()

                    override fun showLearnQuantityPopup(command: String) =
                        quantityRef.get().showLearnQuantityPopup(
                            command,
                        )

                    override fun submitPendingLearnQuantity(levels: String) =
                        quantityRef.get().submitPendingLearnQuantity(
                            levels,
                        )
                },
            )

        val quantityFlowCoordinator =
            QuantityCoordinator(
                object : QuantityCoordinator.Sink {
                    override fun currentState(): ShellUiState = stateBridge.currentUiState()

                    override fun prepareInteractionSheetCommand(command: String): Boolean =
                        interactionSession.get().prepareInteractionSheetCommand(command)

                    override fun dismissPopup() = surfaceRef.get().dismissPopup()

                    override fun dismissSheet() = surfaceRef.get().dismissSheet()

                    override fun sendDirectCommand(
                        command: String,
                        suppressImmediateScroll: Boolean,
                    ) = vmBridge.sendDirectCommand(
                        command,
                        clearInputAfterSend = false,
                        suppressImmediateScroll = suppressImmediateScroll,
                        bypassThrottle = false,
                    )

                    override fun updateState(transform: ShellUiState.() -> ShellUiState) =
                        stateBridge.updateState(
                            transform,
                        )
                },
            )

        quantityRef.bind(quantityFlowCoordinator)
        surfaceRef.bind(surfaceCoordinator)
        return SurfaceOwners(surfaceCoordinator, quantityFlowCoordinator)
    }
}
