package com.xiakexing.client.feature.shell

import com.xiakexing.client.core.network.CommandRequestIdentity

/**
 * Command 域 factory——只接收显式依赖，返回不可变 [CommandOwners]。
 */
internal data class CommandOwners(
    val commandCoordinator: CommandCoordinator,
)

internal class CommandBindings(
    private val deps: ShellSharedDeps,
    private val interactionSession: LazyRef<InteractionSessionCoordinator>,
    private val roomProjection: LazyRef<RoomProjectionCoordinator>,
    private val messageRef: LazyRef<MessageCoordinator>,
) {
    fun build(): CommandOwners {
        val commandCoordinator =
            CommandCoordinator(
                object : CommandSink {
                    override fun currentState(): ShellUiState = deps.stateBridge.currentUiState()

                    override fun pendingInteractionRequest(): PendingInteractionRequest? =
                        interactionSession.get().pendingInteractionRequest()

                    override fun reserveCommandRequest(): CommandRequestIdentity =
                        deps.connectionRepository.reserveCommandRequest()

                    override fun bindPendingCommandRequest(
                        request: CommandRequestIdentity,
                        command: String,
                    ) = interactionSession.get().bindPendingCommandRequest(request, command)

                    override fun rollbackPendingCommandRequest(request: CommandRequestIdentity) =
                        interactionSession.get().rollbackPendingCommandRequest(request)

                    override fun sendCommand(
                        request: CommandRequestIdentity,
                        command: String,
                    ): Boolean = deps.connectionRepository.sendCommand(request, command)

                    override fun sendRawLine(line: String): Boolean =
                        deps.connectionRepository.sendLine(line)

                    override fun clearPendingMoveAutoScroll() =
                        roomProjection.get().clearPendingMoveAutoScroll()

                    override fun clearPendingInteractionRequest() =
                        interactionSession.get().clearPendingInteractionRequest()

                    override fun prepareInteractionSheetCommand(command: String): Boolean =
                        interactionSession.get().prepareInteractionSheetCommand(command)

                    override fun appendNarrativeTranscript(line: String) =
                        messageRef.get().appendNarrativeTranscript(
                            line,
                        )

                    override fun appendErrorFeedback(message: String) =
                        messageRef.get().appendErrorFeedback(
                            message,
                        )

                    override fun updateState(transform: ShellUiState.() -> ShellUiState) =
                        deps.stateBridge.updateState(
                            transform,
                        )
                },
            )
        return CommandOwners(commandCoordinator)
    }
}
