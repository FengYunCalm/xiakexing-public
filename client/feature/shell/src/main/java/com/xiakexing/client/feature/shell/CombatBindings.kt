package com.xiakexing.client.feature.shell

/**
 * Combat 域 factory——只接收显式依赖，返回不可变 [CombatOwners]。
 * 域内 combat → skill 后向引用用本地 [LazyRef] 断环；surface 为跨域后向
 * 引用（SurfaceBindings 先于本域执行），由组合根传入 [LazyRef]。
 */
internal data class CombatOwners(
    val connectionPresentationCoordinator: ConnectionPresentationCoordinator,
    val combatPacketCoordinator: CombatCoordinator,
    val skillCoordinator: SkillCoordinator,
)

internal class CombatBindings(
    private val deps: ShellSharedDeps,
    private val messageRef: LazyRef<MessageCoordinator>,
    private val roomProjection: LazyRef<RoomProjectionCoordinator>,
    private val interactionSession: LazyRef<InteractionSessionCoordinator>,
    private val quest: LazyRef<QuestCoordinator>,
    private val login: LazyRef<LoginCoordinator>,
    private val surface: LazyRef<SurfaceCoordinator>,
    private val inventoryTrade: LazyRef<InventoryTradeCoordinator>,
    private val protocolIngressCoordinator: ProtocolIngressCoordinator,
    private val clientUpdateCoordinator: UpdatePromptCoordinator,
    private val persistenceCoordinator: PersistenceCoordinator,
) {
    fun build(): CombatOwners {
        val stateBridge = deps.stateBridge
        val vmBridge = deps.vmBridge
        val skillRef = LazyRef<SkillCoordinator>()

        val connectionPresentationCoordinator =
            ConnectionPresentationCoordinator(
                deps.connectionRepository,
                deps.connectionSession,
                deps.streamDecoder,
                deps.scope,
                object : ConnectionPresentationSink {
                    override fun currentState(): ShellUiState = stateBridge.currentUiState()

                    override fun resetIncomingStream() = vmBridge.resetIncomingProtocolStream()

                    override fun resetProtocolPacketGate() = vmBridge.resetProtocolPacketGate()

                    override fun resetMessageForConnection(
                        endpointLine: String,
                        preserveNoticeFeed: Boolean,
                    ) = messageRef.get().resetForConnection(endpointLine, preserveNoticeFeed)

                    override fun visibleTranscript(): List<TranscriptLine> =
                        messageRef.get().visibleTranscript()

                    override fun recentNoticeFeed(): List<String> =
                        messageRef.get().recentNoticeFeed()

                    override fun appendTranscript(line: String) =
                        messageRef.get().appendTranscript(
                            line,
                        )

                    override fun appendErrorFeedback(message: String) =
                        messageRef.get().appendErrorFeedback(
                            message,
                        )

                    override fun clearPendingRoomRefresh() = vmBridge.clearPendingRoomRefresh()

                    override fun clearPendingMoveAutoScroll() =
                        roomProjection.get().clearPendingMoveAutoScroll()

                    override fun clearPendingInteractionRequest() =
                        interactionSession.get().clearPendingInteractionRequest()

                    override fun clearPendingProtocolSheetRequest() =
                        interactionSession.get().clearPendingProtocolSheetRequest()

                    override fun clearPendingProtocolPopupRequest() =
                        interactionSession.get().clearPendingProtocolPopupRequest()

                    override fun clearTrackedQuest() = quest.get().clearTrackedQuest()

                    override fun hasCommittedCompleteRoomSnapshot(
                        projectionId: String,
                        projectionRevision: Long?,
                        projectionGeneration: Long?,
                    ): Boolean =
                        roomProjection.get()
                            .hasCommittedCompleteSnapshot(
                                projectionId,
                                projectionRevision,
                                projectionGeneration,
                            )

                    override fun requestAuthoritativeResync() =
                        deps.connectionRepository.requestAuthoritativeResync()

                    override fun resetConnectionTransientState() =
                        vmBridge.resetConnectionTransientState()

                    override fun resetRoomCaches() = roomProjection.get().resetTransientCaches()

                    override fun clearSkillSummaries() = skillRef.get().clearSkillSummaries()

                    override fun clearQuestCache() = quest.get().clearQuestCache()

                    override fun clearPendingInventoryInspectItem() =
                        inventoryTrade.get().clearPendingInventoryInspectItem()

                    override fun clearInventoryInspectItem() =
                        inventoryTrade.get().clearPendingInventoryInspectItem()

                    override fun clearUnhandledPacketTypes() =
                        protocolIngressCoordinator.clearPacketTypes()

                    override fun shouldShowLoginError(): Boolean =
                        login.get().shouldShowLoginError()

                    override fun showLoginError(message: String) =
                        login.get().showLoginError(
                            message,
                        )

                    override fun maybeStartAutomaticUpdateCheck() =
                        clientUpdateCoordinator.maybeStartAutomaticUpdateCheck()

                    override fun updateState(transform: ShellUiState.() -> ShellUiState) =
                        stateBridge.updateState(
                            transform,
                        )

                    override fun updateStateWithSessionEvent(
                        event: ShellEvent,
                        transform: ShellUiState.() -> ShellUiState,
                    ) = stateBridge.updateStateWithSessionEvent(event, transform)
                },
            )

        val combatPacketCoordinator =
            CombatCoordinator(
                object : CombatSink {
                    override fun dismissObscuringSurfacesForCombatStart() =
                        surface.get().dismissObscuringSurfacesForCombatStart()

                    override fun refreshCombatExitOverlayIfVisible() =
                        skillRef.get().refreshCombatExitOverlayIfVisible()

                    override fun openCombatActionHubPanel() =
                        skillRef.get().openCombatActionHubPanel(requireCombat = true)

                    override fun currentState(): ShellUiState = stateBridge.currentUiState()

                    override fun appendCombatTranscript(line: String) =
                        messageRef.get().appendNarrativeTranscript(
                            line,
                        )

                    override fun updateState(transform: ShellUiState.() -> ShellUiState) =
                        stateBridge.updateState(
                            transform,
                        )
                },
            )

        val skillCoordinator =
            SkillCoordinator(
                persistenceCoordinator,
                object : SkillSink {
                    override fun clearPendingInteractionRequest() =
                        interactionSession.get().clearPendingInteractionRequest()

                    override fun clearPendingProtocolSheetRequest() =
                        interactionSession.get().clearPendingProtocolSheetRequest()

                    override fun pendingProtocolSheetRequest(): PendingProtocolSheetRequest? =
                        interactionSession.get().pendingProtocolSheetRequest()

                    override fun flushPendingRoomRefresh() = vmBridge.flushPendingRoomRefresh()

                    override fun prepareProtocolSheetCommand(
                        currentSheet: ShellSheetState?,
                        command: String,
                    ) = interactionSession.get().prepareProtocolSheetCommand(currentSheet, command)

                    override fun prepareHiddenProtocolSheetCommand(
                        currentSheet: ShellSheetState?,
                        command: String,
                    ) = interactionSession.get().prepareHiddenProtocolSheetCommand(
                        currentSheet,
                        command,
                    )

                    override fun sendDirectCommand(
                        command: String,
                        suppressImmediateScroll: Boolean,
                    ) = vmBridge.sendDirectCommand(
                        command,
                        clearInputAfterSend = false,
                        suppressImmediateScroll = suppressImmediateScroll,
                        bypassThrottle = false,
                    )

                    override fun showActionConfirmation(
                        title: String,
                        action: ShellAction,
                        dismissSheetAfterConfirm: Boolean,
                    ) = surface.get().showActionConfirmation(
                        title,
                        action,
                        dismissSheetAfterConfirm,
                    )

                    override fun dismissPopup() = surface.get().dismissPopup()

                    override fun appendTranscript(line: String) =
                        messageRef.get().appendTranscript(
                            line,
                        )

                    override fun currentState(): ShellUiState = stateBridge.currentUiState()

                    override fun updateState(transform: ShellUiState.() -> ShellUiState) =
                        stateBridge.updateState(
                            transform,
                        )
                },
            )

        skillRef.bind(skillCoordinator)
        return CombatOwners(
            connectionPresentationCoordinator,
            combatPacketCoordinator,
            skillCoordinator,
        )
    }
}
