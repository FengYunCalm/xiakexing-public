package com.xiakexing.client.feature.shell

import com.xiakexing.client.core.protocol.payload.MapGridPacket

/**
 * Sheet 域 factory——只接收显式依赖，返回不可变 [SheetOwners]。
 * InteractionSession 域构造统一在 CoreBindings。
 */
internal data class SheetOwners(
    val panelCoordinator: PanelCoordinator,
)

internal class SheetBindings(
    private val deps: ShellSharedDeps,
    private val roomProjection: LazyRef<RoomProjectionCoordinator>,
    private val interactionSession: LazyRef<InteractionSessionCoordinator>,
    private val surface: LazyRef<SurfaceCoordinator>,
) {
    fun build(): SheetOwners {
        val panelCoordinator =
            PanelCoordinator(
                deps.scope,
                object : PanelCoordinator.Sink {
                    override fun currentState(): ShellUiState = deps.stateBridge.currentUiState()

                    override fun latestMapGrid(): MapGridPacket? =
                        roomProjection.get().latestMapGrid()

                    override fun sendDirectCommand(
                        command: String,
                        suppressImmediateScroll: Boolean,
                    ) = deps.vmBridge.sendDirectCommand(
                        command,
                        clearInputAfterSend = false,
                        suppressImmediateScroll = suppressImmediateScroll,
                        bypassThrottle = false,
                    )

                    override fun sendMapCommand() =
                        deps.vmBridge.sendDirectCommand(
                            "map xk",
                            clearInputAfterSend = false,
                            suppressImmediateScroll = true,
                            bypassThrottle = true,
                        )

                    override fun flushPendingRoomRefresh() = deps.vmBridge.flushPendingRoomRefresh()

                    override fun clearPendingInteractionRequest() =
                        interactionSession.get().clearPendingInteractionRequest()

                    override fun showCommonExitOverlay(buttons: List<ShellAction>) =
                        surface.get().showCommonExitOverlay(buttons)

                    override fun updateState(transform: ShellUiState.() -> ShellUiState) =
                        deps.stateBridge.updateState(
                            transform,
                        )
                },
            )
        return SheetOwners(panelCoordinator)
    }
}
