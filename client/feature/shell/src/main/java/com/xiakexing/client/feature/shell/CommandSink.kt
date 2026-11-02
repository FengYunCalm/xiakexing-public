package com.xiakexing.client.feature.shell

import com.xiakexing.client.core.network.CommandRequestIdentity

/**
 * Command 域 Sink——编排层向 VM/其他域暴露的副作用接口
 * （SkillSink/MessageCoordinatorSink/CombatSink 先例，独立文件）。
 */
internal interface CommandSink {
    fun currentState(): ShellUiState

    fun pendingInteractionRequest(): PendingInteractionRequest?

    fun reserveCommandRequest(): CommandRequestIdentity

    fun bindPendingCommandRequest(
        request: CommandRequestIdentity,
        command: String,
    )

    fun rollbackPendingCommandRequest(request: CommandRequestIdentity)

    fun sendCommand(
        request: CommandRequestIdentity,
        command: String,
    ): Boolean

    fun sendRawLine(line: String): Boolean

    fun clearPendingMoveAutoScroll()

    fun clearPendingInteractionRequest()

    fun prepareInteractionSheetCommand(command: String): Boolean

    fun appendNarrativeTranscript(line: String)

    fun appendErrorFeedback(message: String)

    fun updateState(transform: ShellUiState.() -> ShellUiState)
}
