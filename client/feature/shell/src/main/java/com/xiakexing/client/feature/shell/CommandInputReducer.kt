package com.xiakexing.client.feature.shell

internal enum class CommandSubmitRoute {
    CHAT_MESSAGE,
    CLEAR_INPUT_ONLY,
    DIRECT_COMMAND,
}

internal data class CommandSubmitDecision(
    val command: String,
    val route: CommandSubmitRoute,
)

internal fun ShellUiState.resolveCommandSubmitDecision(): CommandSubmitDecision? {
    val command = commandInput.trim()
    if (command.isBlank()) {
        return null
    }
    val route =
        when {
            commandPalette != null -> CommandSubmitRoute.CHAT_MESSAGE
            session.phase == ShellPhase.INTERACTIVE -> CommandSubmitRoute.CLEAR_INPUT_ONLY
            else -> CommandSubmitRoute.DIRECT_COMMAND
        }
    return CommandSubmitDecision(command = command, route = route)
}

internal fun ShellUiState.dismissCommandPalette(): ShellUiState {
    return if (commandPalette == null) this else copy(commandPalette = null)
}
