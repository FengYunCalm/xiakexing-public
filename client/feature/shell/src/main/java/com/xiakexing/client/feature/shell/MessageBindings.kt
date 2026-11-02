package com.xiakexing.client.feature.shell

/**
 * Message 域 factory——只接收显式依赖，返回不可变 [MessageOwners]。
 * 域内 inventoryTrade → message 前向引用直接传实例。
 */
internal data class MessageOwners(
    val messagePacketCoordinator: MessageCoordinator,
    val inventoryTradeCoordinator: InventoryTradeCoordinator,
)

internal class MessageBindings(
    private val deps: ShellSharedDeps,
    private val interactionSession: LazyRef<InteractionSessionCoordinator>,
    private val login: LazyRef<LoginCoordinator>,
    private val panel: LazyRef<PanelCoordinator>,
    private val roomProjection: LazyRef<RoomProjectionCoordinator>,
    private val interactionCaptureOrchestrator: ShellInteractionCaptureOrchestrator,
) {
    fun build(): MessageOwners {
        val stateBridge = deps.stateBridge
        val vmBridge = deps.vmBridge

        val messagePacketCoordinator =
            MessageCoordinator(
                interactionCaptureOrchestrator,
                object : MessageCoordinatorSink {
                    override fun currentState(): ShellUiState = stateBridge.currentUiState()

                    override fun flushPendingRoomRefresh() = vmBridge.flushPendingRoomRefresh()

                    override fun showOrDeferLoginReadyBanner() =
                        login.get().showOrDeferLoginReadyBanner()

                    override fun shouldShowLoginError(): Boolean =
                        login.get().shouldShowLoginError()

                    override fun showLoginError(message: String) =
                        login.get().showLoginError(
                            message,
                        )

                    override fun capturePendingError(message: String): Boolean =
                        interactionSession.get().capturePendingError(
                            message,
                        )

                    override fun clearPendingInteractionRequest() =
                        interactionSession.get().clearPendingInteractionRequest()

                    override fun settleIfAwaitingQuestAnswer() =
                        interactionSession.get().settleIfAwaitingQuestAnswer()

                    override fun onMapAlreadyLatest(): Boolean = panel.get().onMapAlreadyLatest()

                    override fun onErrorPacketConnectionFailed(message: String) {
                        stateBridge.updateStateWithSessionEvent(
                            ShellEvent.ConnectionFailed(message),
                        ) {
                            copy(
                                appScene = if (appScene == ShellScene.REGISTER) ShellScene.REGISTER else ShellScene.LOGIN,
                            )
                        }
                    }

                    override fun clearInventoryRefreshPending() =
                        vmBridge.clearInventoryRefreshPending()

                    override fun applyPromptSettlePlan(settlePlan: PromptSettlePlan) =
                        interactionSession.get().applyPromptSettlePlan(settlePlan)

                    override fun applyTranscriptCapturePlan(
                        capturePlan: TranscriptCapturePlan,
                    ): Boolean = interactionSession.get().applyTranscriptCapturePlan(capturePlan)

                    override fun markPendingMoveAutoScrollActivity() =
                        roomProjection.get().markPendingMoveAutoScrollActivity()

                    override fun updateState(transform: ShellUiState.() -> ShellUiState) =
                        stateBridge.updateState(
                            transform,
                        )
                },
            )

        val inventoryTradeCoordinator =
            InventoryTradeCoordinator(
                interactionSession.get(),
                object : InventoryTradeCoordinator.Sink {
                    override fun currentState(): ShellUiState = stateBridge.currentUiState()

                    override fun flushPendingRoomRefresh() = vmBridge.flushPendingRoomRefresh()

                    override fun clearPendingInteractionRequest() =
                        interactionSession.get().clearPendingInteractionRequest()

                    override fun clearPendingProtocolSheetRequest() =
                        interactionSession.get().clearPendingProtocolSheetRequest()

                    override fun prepareProtocolSheetCommand(
                        currentSheet: ShellSheetState?,
                        command: String,
                    ) = interactionSession.get().prepareProtocolSheetCommand(currentSheet, command)

                    override fun sendDirectCommand(
                        command: String,
                        suppressImmediateScroll: Boolean,
                    ) = vmBridge.sendDirectCommand(
                        command,
                        clearInputAfterSend = false,
                        suppressImmediateScroll = suppressImmediateScroll,
                        bypassThrottle = false,
                    )

                    override fun scheduleInteractionRequestSettle() =
                        interactionSession.get().scheduleInteractionRequestSettle()

                    override fun appendTranscript(line: String) =
                        messagePacketCoordinator.appendTranscript(
                            line,
                        )

                    override fun updateState(transform: ShellUiState.() -> ShellUiState) =
                        stateBridge.updateState(
                            transform,
                        )
                },
            )

        return MessageOwners(messagePacketCoordinator, inventoryTradeCoordinator)
    }
}
