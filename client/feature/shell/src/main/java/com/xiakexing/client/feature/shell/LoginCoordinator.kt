package com.xiakexing.client.feature.shell

import com.google.gson.JsonObject
import com.xiakexing.client.core.network.GameConnectionRepository
import com.xiakexing.client.core.network.GatewaySessionPort
import com.xiakexing.client.core.network.LoginPayloadFormatter
import com.xiakexing.client.core.protocol.payload.RegisterPrompt
import com.xiakexing.client.feature.shell.update.ClientUpdateDownloadProgress
import com.xiakexing.client.feature.shell.update.ClientUpdateManifest

/**
 * 登录协调器（ShellLogin + ShellLoginStatus + StartupUi 合并）。
 * 职责：登录提示/注册提示/会话恢复呈现、ready banner 延迟消费、登录错误展示。
 * 纯函数（resolveLoginReadyBanner 等）同文件顶层。
 */
internal class LoginCoordinator(
    private val gatewayClient: GatewaySessionPort,
    private val connectionRepository: GameConnectionRepository,
    private val connectionSession: ShellConnectionSessionState,
    private val persistenceCoordinator: PersistenceCoordinator,
    private val sink: Sink,
) {
    interface Sink {
        fun currentState(): ShellUiState

        fun appendTranscript(line: String)

        fun appendErrorFeedback(message: String)

        fun updateState(transform: ShellUiState.() -> ShellUiState)

        fun updateStateWithSessionEvent(
            event: ShellEvent,
            transform: ShellUiState.() -> ShellUiState = { this },
        )
    }

    // ===== ready banner =====

    private var pendingLoginReadyBanner = false

    fun showOrDeferLoginReadyBanner() {
        pendingLoginReadyBanner = sink.currentState().resolveLoginReadyBanner().shouldKeepPending
    }

    fun consumeLoginReadyBanner(enteringMainShell: Boolean) {
        pendingLoginReadyBanner =
            consumePendingLoginReadyBanner(
                enteringMainShell,
                pendingLoginReadyBanner,
            ).shouldKeepPending
    }

    // ===== login error =====

    fun shouldShowLoginError(): Boolean {
        val current = sink.currentState()
        return current.appScene != ShellScene.REGISTER && current.session.phase != ShellPhase.INTERACTIVE
    }

    fun showLoginError(message: String) {
        if (message.isBlank()) {
            return
        }

        sink.updateStateWithSessionEvent(ShellEvent.ConnectionFailed(message)) {
            copy(
                appScene = ShellScene.LOGIN,
                loginErrorMessage = message,
                dialog =
                    dialog?.takeIf { popup ->
                        clientUpdateRequired && popup.meta.contains(CLIENT_UPDATE_POPUP_META)
                    },
                sheet = null,
                commandPalette = null,
                exitOverlay = null,
            )
        }
    }

    // ===== 登录/注册提示 =====

    fun handleLoginPromptReady() {
        sink.updateStateWithSessionEvent(ShellEvent.LoginPromptReceived) {
            if (connectionSession.shouldPreserveUiForResumeLogin() && appScene == ShellScene.MAIN_SHELL) {
                copy(loginErrorMessage = "")
            } else {
                copy(
                    loginErrorMessage = "",
                    commandPalette = null,
                    exitOverlay = null,
                )
            }
        }
        sink.appendTranscript("[登录] 收到登录提示")
    }

    fun handleRegisterPrompt(prompt: RegisterPrompt) {
        connectionRepository.markLoginRegistrationProgress()
        sink.updateStateWithSessionEvent(
            ShellEvent.RegisterPromptReceived(
                stage = prompt.stage,
                message = prompt.message,
            ),
        ) {
            copy(
                appScene = ShellScene.REGISTER,
                loginErrorMessage = "",
                commandPalette = null,
                exitOverlay = null,
            )
        }
        if (prompt.message.isNotBlank()) {
            sink.appendTranscript(prompt.message)
        }
    }

    fun handleReportPacket(payload: JsonObject) {
        val command = payload.shellString("command")
        if (command != XK_REPORT_SESSION_RESUME) {
            return
        }

        val data = payload.objectOrNull("data") ?: return
        val resumeToken = data.shellString("resume_token").trim()
        if (resumeToken.isBlank()) {
            return
        }

        // owner 是 resume token 唯一权威（headless 重连免密快恢复依赖它）。
        connectionRepository.storeResumeToken(resumeToken)
        connectionSession.storeResumeToken(resumeToken)
    }

    fun sendLoginForPrompt() {
        val resumeToken = connectionSession.takePendingResumeLoginToken()
        if (resumeToken.isNotBlank()) {
            if (gatewayClient.sendLine(LoginPayloadFormatter.formatResumeLogin(resumeToken))) {
                connectionSession.markResumeLoginSent()
                showWorldLoadingWhileLoginCompletes()
                sink.appendTranscript("[恢复] 已发送会话恢复凭据")
                return
            }
            sink.appendErrorFeedback("恢复凭据发送失败，改用账号密码登录")
        }

        sendStoredLogin()
    }

    private fun sendStoredLogin() {
        connectionSession.markStoredLoginSent()
        val current = sink.currentState()
        val payload = LoginPayloadFormatter.formatTextLogin(current.username, current.password)
        if (gatewayClient.sendLine(payload)) {
            persistenceCoordinator.saveCredentials(current.username, current.password)
            showWorldLoadingWhileLoginCompletes()
        } else {
            sink.updateStateWithSessionEvent(ShellEvent.ConnectionFailed("连接未就绪，无法发送登录凭据")) {
                copy(appScene = ShellScene.LOGIN)
            }
            sink.appendErrorFeedback("连接未就绪，无法发送登录凭据")
        }
    }

    private fun showWorldLoadingWhileLoginCompletes() {
        sink.updateState {
            if (connectionSession.shouldPreserveUiForResumeLogin() && appScene == ShellScene.MAIN_SHELL) {
                return@updateState copy(
                    commandInput = "",
                    loginErrorMessage = "",
                    commandPalette = null,
                    exitOverlay = null,
                )
            }
            copy(
                appScene = ShellScene.WORLD_LOADING,
                commandInput = "",
                loginErrorMessage = "",
                commandPalette = null,
                exitOverlay = null,
            )
        }
    }
}

private const val XK_REPORT_SESSION_RESUME = "session_resume"

// ===== StartupUi 纯函数（并入 LoginCoordinator.kt） =====

internal data class LoginReadyBannerResolution(
    val shouldKeepPending: Boolean = false,
)

internal fun ShellUiState.resolveLoginReadyBanner(): LoginReadyBannerResolution {
    return if (appScene == ShellScene.MAIN_SHELL) {
        LoginReadyBannerResolution()
    } else {
        LoginReadyBannerResolution(shouldKeepPending = true)
    }
}

internal fun consumePendingLoginReadyBanner(
    enteringMainShell: Boolean,
    pendingLoginReadyBanner: Boolean,
): LoginReadyBannerResolution {
    return if (enteringMainShell && pendingLoginReadyBanner) {
        LoginReadyBannerResolution()
    } else {
        LoginReadyBannerResolution(shouldKeepPending = pendingLoginReadyBanner)
    }
}

internal fun ShellUiState.showUpdateCheckingState(): ShellUiState {
    return copy(
        dialog = buildUpdateCheckingPopup(),
        sheet = null,
        commandPalette = null,
        exitOverlay = null,
    )
}

internal fun ShellUiState.showUpdateAvailableState(
    manifest: ClientUpdateManifest,
    required: Boolean = manifest.force,
): ShellUiState {
    return copy(
        clientUpdateRequired = clientUpdateRequired || required,
        dialog = buildUpdateAvailablePopup(manifest, required = required),
        sheet = null,
        commandPalette = null,
        exitOverlay = null,
    )
}

internal fun ShellUiState.showNoUpdateState(installedVersionName: String): ShellUiState {
    return copy(
        dialog = buildNoUpdatePopup(installedVersionName),
        sheet = null,
    )
}

internal fun ShellUiState.showUpdateDownloadingState(
    progress: ClientUpdateDownloadProgress,
): ShellUiState {
    return copy(
        dialog = buildUpdateDownloadingPopup(progress),
        sheet = null,
        commandPalette = null,
        exitOverlay = null,
    )
}
