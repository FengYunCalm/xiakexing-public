package com.xiakexing.client.feature.shell

/**
 * Login 域 factory——只接收显式依赖，返回不可变 [LoginOwners]。
 */
internal data class LoginOwners(
    val loginCoordinator: LoginCoordinator,
)

internal class LoginBindings(
    private val deps: ShellSharedDeps,
    private val messageRef: LazyRef<MessageCoordinator>,
    private val persistenceCoordinator: PersistenceCoordinator,
) {
    fun build(): LoginOwners {
        val loginCoordinator =
            LoginCoordinator(
                deps.connectionRepository.port,
                deps.connectionRepository,
                deps.connectionSession,
                persistenceCoordinator,
                object : LoginCoordinator.Sink {
                    override fun currentState(): ShellUiState = deps.stateBridge.currentUiState()

                    override fun appendTranscript(line: String) =
                        messageRef.get().appendTranscript(
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

                    override fun updateStateWithSessionEvent(
                        event: ShellEvent,
                        transform: ShellUiState.() -> ShellUiState,
                    ) = deps.stateBridge.updateStateWithSessionEvent(event, transform)
                },
            )
        return LoginOwners(loginCoordinator)
    }
}
