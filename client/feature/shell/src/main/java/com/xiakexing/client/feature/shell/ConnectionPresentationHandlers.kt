package com.xiakexing.client.feature.shell

import com.xiakexing.client.core.network.ConnectionState
import com.xiakexing.client.core.network.GameConnectionRepository

/**
 * 连接呈现编排与 sink 执行序列（非纯函数，独立于 Reducer 的纯变换）。
 * 断线/错误/登录超时的呈现决策与 sink 调用序列。
 */
internal fun applyManualDisconnect(sink: ConnectionPresentationSink) {
    sink.clearInventoryInspectItem()
    sink.updateStateWithSessionEvent(ShellEvent.Disconnected(MANUAL_DISCONNECT_LABEL)) {
        manualDisconnectState()
    }
}

internal fun applyDisconnectReset(
    presentation: ConnectionDisconnectedPresentation,
    sink: ConnectionPresentationSink,
) {
    sink.updateStateWithSessionEvent(presentation.sessionEvent) {
        applyConnectionResetUi(presentation.uiState)
    }
    applyRuntimeReset(presentation.runtimeState, sink)
    sink.appendTranscript(presentation.transcriptLine)
    if (presentation.shouldShowLoginError) {
        sink.showLoginError(presentation.loginErrorMessage)
    }
}

/** 错误 RESET 呈现执行（sink 调用序列）。 */
internal fun applyErrorReset(
    presentation: ConnectionErrorPresentation,
    sink: ConnectionPresentationSink,
) {
    if (presentation.shouldShowLoginError) {
        sink.showLoginError(presentation.loginErrorMessage)
    } else {
        sink.updateStateWithSessionEvent(presentation.sessionEvent) {
            applyConnectionResetUi(presentation.uiState)
        }
    }
}

/** 登录等待超时呈现执行（sink 调用序列）。 */
internal fun applyLoginWaitTimeout(
    staysOnWorldLoading: Boolean,
    diagnosticMessage: String,
    sink: ConnectionPresentationSink,
) {
    sink.updateStateWithSessionEvent(ShellEvent.ConnectionFailed(diagnosticMessage)) {
        loginWaitTimeoutState(staysOnWorldLoading, diagnosticMessage)
    }
    sink.appendErrorFeedback(diagnosticMessage)
}

/** 手动断开（无活动连接时）呈现执行（sink 调用序列）。 */

internal fun resetForFreshConnection(sink: ConnectionPresentationSink) {
    sink.resetRoomCaches()
    sink.clearSkillSummaries()
    sink.clearQuestCache()
    sink.resetIncomingStream()
}

/** 登录引导完成的 UI 呈现。 */
internal fun resetForConnect(sink: ConnectionPresentationSink) {
    sink.resetRoomCaches()
    sink.clearSkillSummaries()
    sink.clearQuestCache()
    sink.clearUnhandledPacketTypes()
}

/** 断线呈现决策（保活语义：非用户意图断线一律重连覆盖层）。 */
internal enum class DisconnectFlow { RECONNECT_OVERLAY, MANUAL_RESET, RESET }

// ===== 断线/错误/超时呈现编排（下沉自 ConnectionPresentationCoordinator） =====

/** 断线 RESET 呈现执行（sink 调用序列）。 */

internal fun handleDisconnected(
    state: ConnectionState.Disconnected,
    connectionRepository: GameConnectionRepository,
    connectionSession: ShellConnectionSessionState,
    sink: ConnectionPresentationSink,
): DisconnectOutcome {
    connectionSession.clearPendingBackgroundDisconnect()
    sink.resetConnectionTransientState()
    val reason = state.reason.ifBlank { "连接已断开" }
    if (isOwnerPolicyDisconnect(reason)) {
        // owner 策略性断连：终态呈现单归 LoginWaitTimedOut 分支（R-19），不再叠加覆盖层。
        return DisconnectOutcome.Reset
    }
    if (
        disconnectFlowDecision(
            reason = reason,
            isManualRequested = connectionRepository.isManualDisconnectRequested(),
            appScene = sink.currentState().appScene,
        ) == DisconnectFlow.RECONNECT_OVERLAY
    ) {
        // 保活语义：非用户意图断线一律自动重连覆盖层 + 自动重连（不再有后台挂起分支）。
        return DisconnectOutcome.ReconnectOverlay
    }
    val presentation =
        disconnectedPresentation(
            reason = reason,
            shouldShowLoginError = reason != MANUAL_DISCONNECT_REASON && sink.shouldShowLoginError(),
        )
    applyDisconnectReset(presentation, sink)
    connectionSession.clearManualDisconnectRequest()
    return DisconnectOutcome.Reset
}

internal fun handleError(
    state: ConnectionState.Error,
    connectionSession: ShellConnectionSessionState,
    diagnostic: ConnectionDiagnostic,
    sink: ConnectionPresentationSink,
) {
    connectionSession.clearPendingBackgroundDisconnect()
    sink.resetConnectionTransientState()
    // 方案 R-20：重连中再次失败——保持覆盖层，直接给出重试/返回登录
    if (sink.currentState().reconnectOverlay) {
        sink.updateState { reconnectOverlayErrorState() }
        sink.appendErrorFeedback(state.message)
        return
    }
    val diagnosticMessage =
        diagnosticConnectionFailureMessage(
            headline = if (diagnostic.stage == "WebSocket 握手") "WebSocket 握手失败" else "连接异常",
            detail = state.message,
            endpoint = diagnostic.endpoint,
            stage = diagnostic.stage,
            lastFrame = diagnostic.lastFrame,
        )
    val presentation =
        errorPresentation(
            currentScene = sink.currentState().appScene,
            message = diagnosticMessage,
            shouldShowLoginError = sink.shouldShowLoginError(),
        )
    applyErrorReset(presentation, sink)
    sink.appendErrorFeedback(diagnosticMessage)
    connectionSession.clearManualDisconnectRequest()
}

/** 登录等待超时终态呈现（R-19）：owner 超时事件驱动的单一执行体。 */
internal fun failLoginWait(
    waitingPhases: Set<ShellPhase>,
    message: String,
    detail: String,
    connectionSession: ShellConnectionSessionState,
    diagnostic: ConnectionDiagnostic,
    sink: ConnectionPresentationSink,
) {
    val current = sink.currentState()
    if (!shouldFailLoginWait(current.appScene, current.session.phase, waitingPhases)) {
        return
    }
    val diagnosticMessage =
        diagnosticConnectionFailureMessage(
            headline = message,
            detail = detail,
            endpoint = diagnostic.endpoint,
            stage = diagnostic.stage,
            lastFrame = diagnostic.lastFrame,
        )
    sink.resetConnectionTransientState()
    // 方案 R-19：世界加载阶段超时不自动跳走——留在加载页呈现失败行与「重试 / 返回登录」
    applyLoginWaitTimeout(
        current.appScene == ShellScene.WORLD_LOADING,
        diagnosticMessage,
        sink,
    )
    connectionSession.clearManualDisconnectRequest()
}

/** 连接诊断状态快照（coordinator 私有字段的只读投影）。 */
internal data class ConnectionDiagnostic(
    val endpoint: String,
    val stage: String,
    val lastFrame: String,
)

/** 断线呈现结果（跨文件显式控制协议）。 */
internal sealed interface DisconnectOutcome {
    data object ReconnectOverlay : DisconnectOutcome

    data object Reset : DisconnectOutcome
}
