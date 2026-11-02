package com.xiakexing.client.feature.shell

import com.google.gson.JsonObject
import com.xiakexing.client.core.network.GameConnectionRepository
import com.xiakexing.client.core.protocol.IncomingFrame
import com.xiakexing.client.core.protocol.payload.DialogPacket
import com.xiakexing.client.core.protocol.payload.RegisterPrompt
import com.xiakexing.client.core.protocol.payload.SkillEventPacket

/**
 * ShellViewModel 的唯一领域门面。
 *
 * 旧 ShellComposition 向 VM 暴露约 20 个 coordinator getter；本类改为
 * 平面领域方法：VM 只委托 state 与 intent（[onIntent]），生命周期清理与
 * 持久化启动收口为聚合方法（[onShellViewModelCleared] 等），vmBridge 的
 * coordinator 副作用经 [ShellCompositionVmBridge] 转发到本门面。
 * 跨域协调仍由 coordinator 的 Sink 网络承担，本类不引入新逻辑。
 */
internal class ShellFacade(
    private val graph: ShellGraph,
    private val connectionRepository: GameConnectionRepository,
    private val updateState: (ShellUiState.() -> ShellUiState) -> Unit,
    private val currentUiState: () -> ShellUiState,
    private val vmBridge: ShellCompositionVmBridge,
) {
    /**
     * Gives direct protocol test fixtures the same request identity allocator
     * used by the transport path, so a test preparation cannot collide with a
     * later command or a closed-request tombstone.
     */
    fun bindTestCommandRequest(command: String): String {
        val request = connectionRepository.reserveCommandRequest()
        graph.interactionSessionCoordinator.bindPendingCommandRequest(request, command)
        return request.id
    }
    // ===== intent 入口（VM 只委托 state 与 intent） =====

    private val intentHandler by lazy {
        ShellIntentHandler(
            ShellIntentHandlerSink(
                updateState = updateState,
                currentUiState = currentUiState,
                sendDirectCommand = vmBridge::sendDirectCommand,
                surface = graph.surfaceCoordinator,
                commandCoordinator = graph.commandCoordinator,
                connectionPresentation = graph.connectionPresentationCoordinator,
                interactionSession = graph.interactionSessionCoordinator,
                panel = graph.panelCoordinator,
                quantityFlow = graph.quantityFlowCoordinator,
                markCredentialsEdited = graph.persistenceCoordinator::markCredentialsEdited,
            ),
        )
    }

    fun onIntent(intent: ShellIntent) = intentHandler.handle(intent)

    // ===== 生命周期聚合（VM onCleared / init） =====

    fun onShellViewModelCleared() {
        graph.roomProjectionCoordinator.cancel()
        graph.questCoordinator.cancelRouteCommands()
        graph.questCoordinator.resetProjectionState()
        graph.interactionSessionCoordinator.clearPendingInteractionRequest()
        graph.roomProjectionCoordinator.clearPendingRoomRefresh()
        graph.connectionPresentationCoordinator.cancel()
        graph.clientUpdateCoordinator.cancel()
    }

    fun maybeStartAutomaticUpdateCheck() =
        graph.clientUpdateCoordinator.maybeStartAutomaticUpdateCheck()

    suspend fun loadStoredCredentials() = graph.persistenceCoordinator.loadStoredCredentials()

    suspend fun loadStoredCombatShortcuts() = graph.skillCoordinator.loadStoredCombatShortcuts()

    // ===== 入站协议（VM raw 消息路径） =====

    suspend fun decodeIncomingMessage(rawMessage: String) =
        graph.protocolIngressCoordinator.decodeIncomingMessage(rawMessage)

    suspend fun decodeIncomingFrames(frames: List<IncomingFrame>) =
        graph.protocolIngressCoordinator.decodeIncomingFrames(frames)

    fun applyIncomingMessage(
        decodedSegments: List<DecodedIncomingSegment>,
        connectionGeneration: Long? = null,
    ) = graph.protocolIngressCoordinator.applyIncomingMessage(
        decodedSegments,
        connectionGeneration ?: connectionRepository.connectionLifecycle.value.generation,
    )

    fun handlePhaseEvent(event: GameConnectionRepository.PhaseEvent) =
        graph.connectionPresentationCoordinator.handlePhaseEvent(event)

    fun observeFrameDrop(
        portDropped: Long,
        portProtocolDropped: Long,
        repositoryDropped: Long,
    ) = graph.protocolIngressCoordinator.observeFrameDrop(
        portDropped,
        portProtocolDropped,
        repositoryDropped,
    )

    fun handleBatchPacket(payload: JsonObject) =
        graph.protocolIngressCoordinator.handleBatchPacket(
            payload,
            connectionRepository.connectionLifecycle.value.generation,
        )

    fun resetProtocolPacketGate() = graph.protocolIngressCoordinator.resetPacketGate()

    fun handleUnhandledPacket(frame: IncomingFrame.Packet) =
        graph.protocolIngressCoordinator.handleUnhandledPacket(frame)

    fun clearUnhandledPacketTypes() = graph.protocolIngressCoordinator.clearPacketTypes()

    // ===== connection presentation 域 =====

    fun updateStateWithSessionEvent(
        event: ShellEvent,
        transform: ShellUiState.() -> ShellUiState,
    ) = graph.connectionPresentationCoordinator.updateStateWithSessionEvent(event, transform)

    fun applySessionEvent(event: ShellEvent) =
        graph.connectionPresentationCoordinator.applySessionEvent(event)

    fun handleLoginPromptReceived() =
        graph.connectionPresentationCoordinator.handleLoginPromptReceived()

    fun handleLoginBootstrapComplete() =
        graph.connectionPresentationCoordinator.handleLoginBootstrapComplete()

    fun resetConnectionTransientState() =
        graph.connectionPresentationCoordinator.resetConnectionTransientState()

    // ===== login 域 =====

    fun handleLoginPromptReady() = graph.loginCoordinator.handleLoginPromptReady()

    fun sendLoginForPrompt() = graph.loginCoordinator.sendLoginForPrompt()

    fun onRegisterPrompt(prompt: RegisterPrompt) =
        graph.loginCoordinator.handleRegisterPrompt(
            prompt,
        )

    fun showLoginError(message: String) = graph.loginCoordinator.showLoginError(message)

    // ===== message 域 =====

    fun appendTranscript(line: String) = graph.messagePacketCoordinator.appendTranscript(line)

    fun appendErrorFeedback(message: String) =
        graph.messagePacketCoordinator.appendErrorFeedback(message)

    fun appendNotice(line: String) = graph.messagePacketCoordinator.appendNotice(line)

    fun appendChatTranscriptAndNotice(
        line: String,
        duplicateRawEcho: String? = null,
    ) = graph.messagePacketCoordinator.appendChatTranscriptAndNotice(line, duplicateRawEcho)

    fun recentTranscriptLines(): List<String> =
        graph.messagePacketCoordinator.recentTranscriptLines()

    fun showApprenticeUnavailablePopup() =
        graph.messagePacketCoordinator.appendTranscript("拜师功能尚未开发。")

    // ===== surface 域 =====

    fun handleSurfaceCommandRoute(route: ShellSurfaceCommandRoute) =
        graph.surfaceCoordinator.handleSurfaceCommandRoute(route)

    fun showCommandPalette() = graph.surfaceCoordinator.showCommandPalette()

    // ===== interaction 域 =====

    fun openInteractionGiveItemsSheet() =
        graph.interactionSessionCoordinator.openInteractionGiveItemsSheet()

    fun returnToInteractionObjectRoot() =
        graph.interactionSessionCoordinator.returnToInteractionObjectRoot()

    fun returnToInteractionRoot() = graph.interactionSessionCoordinator.returnToInteractionRoot()

    fun preserveInteractionSessionForDraft(currentSheet: ShellSheetState) =
        graph.interactionSessionCoordinator.preserveInteractionSessionForDraft(currentSheet)

    fun prepareInteractionSheetCommand(
        command: String,
        requestedGroupId: String = "",
    ): Boolean =
        graph.interactionSessionCoordinator.prepareInteractionSheetCommand(
            command,
            requestedGroupId,
        )

    fun handleMailDialog(packet: DialogPacket): Boolean =
        graph.interactionSessionCoordinator.handleMailDialog(packet)

    fun handleNpcLearningDialog(packet: DialogPacket): Boolean =
        graph.interactionSessionCoordinator.handleNpcLearningDialog(packet)

    fun prepareProtocolSheetCommand(currentSheet: ShellSheetState?) {
        graph.interactionSessionCoordinator.prepareProtocolSheetCommand(currentSheet)
    }

    fun prepareProtocolSheetCommand(
        currentSheet: ShellSheetState?,
        command: String,
    ) {
        graph.interactionSessionCoordinator.prepareProtocolSheetCommand(currentSheet, command)
    }

    fun prepareProtocolSheetCommand(
        currentSheet: ShellSheetState,
        command: String,
        preservedLinesOverride: List<String>,
    ) {
        graph.interactionSessionCoordinator.prepareProtocolSheetCommand(
            currentSheet,
            command,
            preservedLinesOverride,
        )
    }

    fun prepareHiddenProtocolSheetCommand(
        currentSheet: ShellSheetState?,
        command: String,
    ) {
        graph.interactionSessionCoordinator.prepareHiddenProtocolSheetCommand(currentSheet, command)
    }

    fun prepareProtocolPopupCommand(
        currentDialog: ShellPopupState?,
        command: String = "",
    ) {
        graph.interactionSessionCoordinator.prepareProtocolPopupCommand(currentDialog, command)
    }

    fun clearPendingProtocolSheetRequest() =
        graph.interactionSessionCoordinator.clearPendingProtocolSheetRequest()

    fun clearPendingProtocolPopupRequest() =
        graph.interactionSessionCoordinator.clearPendingProtocolPopupRequest()

    fun capturePendingError(message: String): Boolean =
        graph.interactionSessionCoordinator.capturePendingError(message)

    fun scheduleInteractionRequestSettle() =
        graph.interactionSessionCoordinator.scheduleInteractionRequestSettle()

    // ===== 测试钩子转发（生产路径经协议入口，不经此层） =====

    fun handlePromptPacketForTest(payload: JsonObject) =
        graph.messagePacketCoordinator.handlePromptPacket(
            payload,
            graph.interactionSessionCoordinator.pendingCommandResponseId(),
        )

    fun appendTranscriptForTest(line: String) =
        graph.messagePacketCoordinator.appendTranscript(
            line,
            graph.interactionSessionCoordinator.pendingCommandResponseId(),
        )

    fun handleErrorPacketForTest(payload: JsonObject) =
        graph.messagePacketCoordinator.handleErrorPacket(
            payload,
            graph.interactionSessionCoordinator.pendingCommandResponseId(),
        )

    fun handleDialogPacketForTest(payload: JsonObject) =
        graph.interactionSessionCoordinator.handleDialogPacket(
            payload,
            graph.interactionSessionCoordinator.pendingCommandResponseId(),
        )

    fun handleSkillListPacketForTest(payload: JsonObject) =
        graph.skillCoordinator.handleSkillListPacket(payload)

    fun handleSkillInfoPacketForTest(payload: JsonObject) =
        graph.skillCoordinator.handleSkillInfoPacket(
            payload,
            graph.interactionSessionCoordinator.pendingCommandResponseId(),
        )

    fun handleSkillEventPacketForTest(packet: SkillEventPacket) {
        if (!graph.skillCoordinator.handleSkillEventPacket(packet)) {
            graph.combatPacketCoordinator.handleSkillEventPacket(packet)
        }
    }

    fun latestMapRowsForTest(): List<String> =
        graph.roomProjectionCoordinator.latestMapGrid()?.rows.orEmpty()

    fun clearPendingInteractionRequest() =
        graph.interactionSessionCoordinator.clearPendingInteractionRequest()

    // ===== inventory/trade 域 =====

    fun requestInventoryItemDetail(itemId: String) =
        graph.inventoryTradeCoordinator.requestInventoryItemDetail(itemId)

    fun requestSceneItemDetail(target: InteractionObject) =
        graph.inventoryTradeCoordinator.requestSceneItemDetail(target)

    fun handleInventoryPacket(payload: JsonObject) =
        graph.inventoryTradeCoordinator.handleInventoryPacket(payload)

    // ===== skill 域 =====

    fun showSkillBreakthroughChoices(skillId: String) =
        graph.skillCoordinator.showSkillBreakthroughChoices(skillId)

    fun showActionHubPanel() = graph.skillCoordinator.showActionHubPanel()

    fun showCombatExitOverlay() = graph.skillCoordinator.showCombatExitOverlay()

    fun showCombatPerformPicker(slotIndex: Int) =
        graph.skillCoordinator.showCombatPerformPicker(slotIndex)

    fun showCombatPerformSkillPicker(
        slotIndex: Int,
        skillId: String,
    ) = graph.skillCoordinator.showCombatPerformSkillPicker(slotIndex, skillId)

    // ===== quest 域 =====

    fun showQuestListSheet(refresh: Boolean = false) =
        graph.questCoordinator.showQuestListSheet(refresh)

    fun requestQuestListFromDock() = graph.questCoordinator.requestQuestListFromDock()

    fun showQuestDetailSheet(questId: String) = graph.questCoordinator.showQuestDetailSheet(questId)

    fun clearTrackedQuest() = graph.questCoordinator.clearTrackedQuest()

    fun handleRoutePreviewTimeout(requestId: String) =
        graph.questCoordinator.handleRoutePreviewTimeout(requestId)

    // ===== room 域 =====

    fun armPendingMoveAutoScroll() = graph.roomProjectionCoordinator.armPendingMoveAutoScroll()

    fun clearPendingRoomRefresh() = graph.roomProjectionCoordinator.clearPendingRoomRefresh()

    fun flushPendingRoomRefresh() = graph.roomProjectionCoordinator.flushPendingRoomRefresh()

    fun clearRoomProjectionForProjectionSwitch() {
        graph.questCoordinator.cancelRouteCommands()
        graph.roomProjectionCoordinator.clearRoomProjection()
        graph.questCoordinator.resetProjectionState()
        graph.questCoordinator.clearQuestCache()
        graph.skillCoordinator.resetProjectionState()
        graph.interactionSessionCoordinator.clearPendingInteractionRequest()
        graph.interactionSessionCoordinator.clearPendingProtocolSheetRequest()
        graph.interactionSessionCoordinator.clearPendingProtocolPopupRequest()
        graph.inventoryTradeCoordinator.clearPendingInventoryInspectItem()
        updateState { clearRoomProjectionForProjectionSwitch() }
    }

    fun resetConnectionTransientStateWithProjections() {
        // 连接重置必须丢弃全部投影目标候选、接受抑制与挂起路由请求，
        // 否则旧投影可能在重连后被自动追踪或恢复。
        graph.connectionPresentationCoordinator.resetConnectionTransientState()
        graph.questCoordinator.resetProjectionState()
        graph.skillCoordinator.resetProjectionState()
        graph.projectionRuntimeCoordinator.reset()
    }

    // ===== panel 域 =====

    fun showHelpPanel(categoryId: String = DEFAULT_HELP_PANEL_CATEGORY) =
        graph.panelCoordinator.showHelpPanel(categoryId)

    fun showHelpDetail(entryId: String) = graph.panelCoordinator.showHelpDetail(entryId)

    fun showCharacterPanel() = graph.panelCoordinator.showCharacterPanel()

    fun showCharacterHubPanel() = graph.panelCoordinator.showCharacterHubPanel()

    fun showAttributePanel() = graph.panelCoordinator.showAttributePanel()

    fun showFunctionPanel() = graph.panelCoordinator.showFunctionPanel()

    fun showSettingsPanel() = graph.panelCoordinator.showSettingsPanel()

    fun showMailPanel() = graph.panelCoordinator.showMailPanel()

    fun showActivityPanel() = graph.panelCoordinator.showActivityPanel()

    // ===== quantity 域 =====

    fun showGiveQuantityPopup(command: String) =
        graph.quantityFlowCoordinator.showGiveQuantityPopup(command)

    fun submitPendingGiveQuantity(amount: Int) =
        graph.quantityFlowCoordinator.submitPendingGiveQuantity(amount)

    fun submitPendingGiveAll() = graph.quantityFlowCoordinator.submitPendingGiveAll()

    fun showLearnQuantityPopup(command: String) =
        graph.quantityFlowCoordinator.showLearnQuantityPopup(command)

    fun submitPendingLearnQuantity(levels: String) =
        graph.quantityFlowCoordinator.submitPendingLearnQuantity(levels)

    // ===== command 域 =====

    fun resumeLoginFallbackAllowed(): Boolean = graph.connectionSession.resumeLoginFallbackAllowed

    fun sendDirectCommand(
        command: String,
        clearInputAfterSend: Boolean,
        suppressImmediateScroll: Boolean,
        bypassThrottle: Boolean,
    ) = graph.commandCoordinator.sendDirectCommand(
        command,
        clearInputAfterSend,
        suppressImmediateScroll,
        bypassThrottle,
    )

    // ===== 会话状态（测试/VM 专属入口） =====

    fun activeResumeTokenForTest(): String = graph.connectionSession.activeResumeToken()

    fun setPendingResumeLoginTokenForTest(token: String) {
        graph.connectionSession.setPendingResumeLoginTokenForTest(token)
    }
}
