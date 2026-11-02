package com.xiakexing.client.feature.shell

import com.xiakexing.client.core.protocol.payload.MapGridPacket
import com.xiakexing.client.core.protocol.payload.QuestEntryPacket

/**
 * Room 域 factory——只接收显式依赖，返回不可变 [RoomOwners]。
 * 域内 quest ↔ roomProjection 互引用用本地 [LazyRef] 断环，不向外暴露。
 */
internal data class RoomOwners(
    val roomProjectionCoordinator: RoomProjectionCoordinator,
    val questCoordinator: QuestCoordinator,
)

internal class RoomBindings(
    private val deps: ShellSharedDeps,
    private val interactionSession: LazyRef<InteractionSessionCoordinator>,
    private val messageRef: LazyRef<MessageCoordinator>,
    private val login: LazyRef<LoginCoordinator>,
    private val panel: LazyRef<PanelCoordinator>,
) {
    fun build(): RoomOwners {
        val stateBridge = deps.stateBridge
        val vmBridge = deps.vmBridge
        // 域内环：quest Sink 引用 quest 自身与 roomProjection；roomProjection
        // Sink 引用 quest。Sink 闭包调用时求值，本域构建完成即 bind。
        val questRef = LazyRef<QuestCoordinator>()
        val roomRef = LazyRef<RoomProjectionCoordinator>()

        val roomProjectionCoordinator =
            RoomProjectionCoordinator(
                deps.scope,
                object : RoomProjectionCoordinator.Sink {
                    override fun currentState(): ShellUiState = stateBridge.currentUiState()

                    override fun pendingInteractionRequest(): PendingInteractionRequest? =
                        interactionSession.get().pendingInteractionRequest()

                    override fun clearPendingInteractionRequest() =
                        interactionSession.get().clearPendingInteractionRequest()

                    override fun clearMessageFeeds() = messageRef.get().clearFeeds()

                    override fun consumeLoginReadyBanner(enteringMainShell: Boolean) =
                        login.get().consumeLoginReadyBanner(enteringMainShell)

                    override fun markAuthoritativeRoomSnapshotCommitted(
                        connectionGeneration: Long?,
                    ) = deps.connectionRepository.markRoomSnapshotCommitted(connectionGeneration)

                    override fun refreshTrackedQuestRoute() = questRef.get().refreshTrackedRoute()

                    override fun onMapGridAccepted(map: MapGridPacket) =
                        panel.get().onMapGridAccepted(
                            map,
                        )

                    override fun updateState(transform: ShellUiState.() -> ShellUiState) =
                        stateBridge.updateState(
                            transform,
                        )
                },
            )

        val questCoordinator =
            QuestCoordinator(
                deps.scope,
                object : QuestCoordinator.Sink {
                    override fun currentState(): ShellUiState = stateBridge.currentUiState()

                    override fun pendingInteractionRequest(): PendingInteractionRequest? =
                        interactionSession.get().pendingInteractionRequest()

                    override fun pendingProtocolSheetRequest(): PendingProtocolSheetRequest? =
                        interactionSession.get().pendingProtocolSheetRequest()

                    override fun clearPendingInteractionRequest() =
                        interactionSession.get().clearPendingInteractionRequest()

                    override fun clearPendingProtocolSheetRequest() =
                        interactionSession.get().clearPendingProtocolSheetRequest()

                    override fun clearPendingProtocolPopupRequest() =
                        interactionSession.get().clearPendingProtocolPopupRequest()

                    override fun prepareHiddenProtocolSheetCommand(
                        currentSheet: ShellSheetState?,
                        command: String,
                    ) = interactionSession.get().prepareHiddenProtocolSheetCommand(
                        currentSheet,
                        command,
                    )

                    override fun cancelProtocolSheetSettle() =
                        interactionSession.get().cancelProtocolSheetSettle()

                    override fun sendDirectCommand(
                        command: String,
                        suppressImmediateScroll: Boolean,
                    ) = vmBridge.sendDirectCommand(
                        command,
                        clearInputAfterSend = false,
                        suppressImmediateScroll = suppressImmediateScroll,
                        bypassThrottle = false,
                    )

                    override fun autoTrackNewbieQuest(quests: List<QuestEntryPacket>) =
                        questRef.get().autoTrackNewbieQuest(quests)

                    override fun latestMapGrid(): MapGridPacket? = roomRef.get().latestMapGrid()

                    override fun sendRoutePreviewCommand(request: RoutePreviewRequest) {
                        val command =
                            if (request.projection.isV2) {
                                listOf(
                                    "route preview v2",
                                    request.requestId,
                                    request.sourceKind,
                                    request.objectiveId,
                                    request.acceptanceId,
                                    request.targetRoomId,
                                ).joinToString(" ")
                            } else {
                                "route preview ${request.targetRoomId}"
                            }
                        vmBridge.sendDirectCommand(
                            command,
                            clearInputAfterSend = false,
                            suppressImmediateScroll = true,
                            bypassThrottle = true,
                        )
                    }

                    override fun scheduleRoutePreviewTimeout(requestId: String) =
                        vmBridge.scheduleRoutePreviewTimeout(requestId)

                    override fun cancelRoutePreviewTimeout(requestId: String) =
                        vmBridge.cancelRoutePreviewTimeout(requestId)

                    override fun sendRouteCommand(command: String) =
                        vmBridge.sendDirectCommand(
                            command,
                            clearInputAfterSend = false,
                            suppressImmediateScroll = false,
                            bypassThrottle = true,
                        )

                    override fun updateState(transform: ShellUiState.() -> ShellUiState) =
                        stateBridge.updateState(
                            transform,
                        )
                },
            )

        questRef.bind(questCoordinator)
        roomRef.bind(roomProjectionCoordinator)
        return RoomOwners(roomProjectionCoordinator, questCoordinator)
    }
}
