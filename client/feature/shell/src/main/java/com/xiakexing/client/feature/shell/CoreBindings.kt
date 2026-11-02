package com.xiakexing.client.feature.shell

import com.xiakexing.client.core.protocol.IncomingFrame
import com.xiakexing.client.core.protocol.payload.DialogPacket

/**
 * Core 域 factory——只接收显式依赖（[ShellSharedDeps] +
 * 本域实际引用的跨域环引用），返回不可变 [CoreOwners]，不再回填注册表。
 */
internal data class CoreOwners(
    val interactionCaptureState: InteractionCaptureState,
    val interactionCaptureOrchestrator: ShellInteractionCaptureOrchestrator,
    val interactionSessionCoordinator: InteractionSessionCoordinator,
    val protocolIngressCoordinator: ProtocolIngressCoordinator,
    val projectionRuntimeCoordinator: ProjectionRuntimeCoordinator,
    val clientUpdateCoordinator: UpdatePromptCoordinator,
    val persistenceCoordinator: PersistenceCoordinator,
)

internal class CoreBindings(
    private val deps: ShellSharedDeps,
    private val messageRef: LazyRef<MessageCoordinator>,
    private val inventoryTrade: LazyRef<InventoryTradeCoordinator>,
    private val surface: LazyRef<SurfaceCoordinator>,
    private val quest: LazyRef<QuestCoordinator>,
    private val roomProjection: LazyRef<RoomProjectionCoordinator>,
    private val packetHandler: LazyRef<ShellPacketHandler>,
    private val interactionSessionRef: LazyRef<InteractionSessionCoordinator>,
) {
    fun build(): CoreOwners {
        val stateBridge = deps.stateBridge
        val vmBridge = deps.vmBridge
        val interactionCaptureState = InteractionCaptureState(deps.scope)

        val interactionCaptureOrchestrator =
            ShellInteractionCaptureOrchestrator(interactionCaptureState)

        val interactionSessionCoordinator =
            InteractionSessionCoordinator(
                interactionCaptureState,
                interactionCaptureOrchestrator,
                object : InteractionSessionSink {
                    override fun currentState(): ShellUiState = stateBridge.currentUiState()

                    override fun appendInteractionFeedback(line: String) =
                        messageRef.get().appendNarrativeTranscript(
                            line,
                        )

                    override fun showFloatingFeedback(line: String) =
                        messageRef.get().appendFloatingFeedback(
                            line,
                        )

                    override fun updateState(transform: ShellUiState.() -> ShellUiState) =
                        stateBridge.updateState(
                            transform,
                        )

                    override fun pendingInteractionRequest(): PendingInteractionRequest? =
                        interactionCaptureState.pendingInteractionRequest

                    override fun replacePendingInteractionRequest(
                        request: PendingInteractionRequest,
                    ) = interactionSessionRef.get().replacePendingInteractionRequest(request)

                    override fun clearPendingInteractionRequest() =
                        interactionSessionRef.get().clearPendingInteractionRequest()

                    override fun requestSceneItemDetail(target: InteractionObject) =
                        inventoryTrade.get().requestSceneItemDetail(
                            target,
                        )

                    override fun popSheet(): Boolean = surface.get().pop()

                    override fun flushPendingRoomRefresh() = vmBridge.flushPendingRoomRefresh()

                    override fun handleNpcLearningDialog(packet: DialogPacket): Boolean =
                        handleNpcLearningDialog(interactionCaptureState, this, packet)

                    override fun handleMailDialog(packet: DialogPacket): Boolean =
                        handleMailDialog(interactionCaptureState, this, packet)

                    override fun handleInventoryItemDialog(packet: DialogPacket): Boolean =
                        inventoryTrade.get().handleInventoryItemDialog(packet)

                    override fun captureQuestDetailDialog(packet: DialogPacket): Boolean =
                        quest.get().captureQuestDetailDialog(packet)

                    override fun latestQuestEntryName(questId: String): String =
                        quest.get().latestQuestEntry(questId)?.displayName().orEmpty()

                    override fun appendTranscript(line: String) =
                        messageRef.get().appendTranscript(
                            line,
                        )

                    override fun appendNarrativeTranscript(line: String) =
                        messageRef.get().appendNarrativeTranscript(
                            line,
                        )

                    override fun cancelInteractionRequestSettle() =
                        interactionSessionRef.get().cancelInteractionRequestSettle()

                    override fun clearInteractionRequestSettleJob() =
                        interactionSessionRef.get().clearInteractionRequestSettleJob()

                    override fun clearPendingProtocolSheetRequest() =
                        interactionSessionRef.get().clearPendingProtocolSheetRequest()
                },
            )

        val protocolIngressCoordinator =
            ProtocolIngressCoordinator(
                streamDecoder = deps.streamDecoder,
                isDebugBuild = !deps.automaticUpdateChecksEnabled,
                decodeDispatcher = deps.protocolDecodeDispatcher,
                sink =
                    object : ProtocolIngressCoordinator.Sink {
                        override fun appendTranscript(line: String) =
                            messageRef.get().appendTranscript(
                                line,
                            )

                        override fun stageRoomRefreshPacket(frame: IncomingFrame.Packet): Boolean =
                            roomProjection.get().stageRoomRefreshPacket(frame)

                        override fun stageRoomRefreshPacket(packet: StagedRoomPacket): Boolean =
                            roomProjection.get().stageRoomRefreshPacket(packet)

                        override fun flushPendingRoomRefresh() = vmBridge.flushPendingRoomRefresh()

                        override fun beginRoomPacketBatch(connectionGeneration: Long?) =
                            roomProjection.get().beginBatch(connectionGeneration)

                        override fun endRoomPacketBatchAndShouldFlush(): Boolean =
                            roomProjection.get().endBatchAndShouldFlush()

                        override fun acceptsCommandResponse(
                            commandResponseId: String,
                            connectionGeneration: Long?,
                        ): Boolean {
                            if (connectionGeneration != null &&
                                connectionGeneration != deps.connectionRepository.connectionLifecycle.value.generation
                            ) {
                                return false
                            }
                            if (!deps.connectionRepository.acceptsCommandResponseFromCurrentGeneration(
                                    commandResponseId,
                                )
                            ) {
                                return false
                            }
                            val interactionSession = interactionSessionRef.get()
                            return interactionSession.acceptsCommandResponseForPending(
                                commandResponseId,
                                connectionGeneration,
                            )
                        }

                        override fun dispatchPacket(
                            frame: IncomingFrame.Packet,
                            preparedPayload: PreparedProtocolPayload?,
                        ) = packetHandler.get().handle(frame, preparedPayload)
                    },
            )

        val projectionRuntimeCoordinator =
            ProjectionRuntimeCoordinator(
                object : ProjectionRuntimeCoordinator.Sink {
                    override fun currentState(): ShellUiState = stateBridge.currentUiState()

                    override fun updateState(transform: ShellUiState.() -> ShellUiState) =
                        stateBridge.updateState(
                            transform,
                        )

                    override fun clearRoomProjection() =
                        vmBridge.clearRoomProjectionForProjectionSwitch()
                },
            )

        val clientUpdateCoordinator =
            UpdatePromptCoordinator(
                deps.scope,
                deps.updateManager,
                deps.automaticUpdateChecksEnabled,
                object : UpdatePromptCoordinator.Sink {
                    override fun updateState(transform: ShellUiState.() -> ShellUiState) =
                        stateBridge.updateState(transform)

                    override fun updateSceneState(): ShellScene =
                        stateBridge.currentUiState().appScene

                    override fun disconnectForRequiredUpdate() {
                        deps.connectionRepository.manualDisconnect()
                    }
                },
            )

        val persistenceCoordinator =
            PersistenceCoordinator(
                deps.scope,
                deps.combatShortcutStore,
                deps.credentialsStore,
                object : PersistenceCoordinator.Sink {
                    override fun updateState(transform: ShellUiState.() -> ShellUiState) =
                        stateBridge.updateState(transform)
                },
            )

        return CoreOwners(
            interactionCaptureState,
            interactionCaptureOrchestrator,
            interactionSessionCoordinator,
            protocolIngressCoordinator,
            projectionRuntimeCoordinator,
            clientUpdateCoordinator,
            persistenceCoordinator,
        )
    }
}
