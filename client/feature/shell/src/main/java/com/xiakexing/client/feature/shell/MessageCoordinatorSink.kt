package com.xiakexing.client.feature.shell

/**
 * Message 域 Sink——编排层向 VM/其他域暴露的副作用接口
 * （SkillSink 先例，独立文件）。
 */
internal interface MessageCoordinatorSink {
    fun currentState(): ShellUiState

    fun flushPendingRoomRefresh()

    fun showOrDeferLoginReadyBanner()

    fun shouldShowLoginError(): Boolean

    fun showLoginError(message: String)

    fun capturePendingError(message: String): Boolean

    fun clearPendingInteractionRequest()

    fun settleIfAwaitingQuestAnswer()

    fun onMapAlreadyLatest(): Boolean

    fun onErrorPacketConnectionFailed(message: String)

    fun clearInventoryRefreshPending()

    fun applyPromptSettlePlan(settlePlan: PromptSettlePlan)

    fun applyTranscriptCapturePlan(capturePlan: TranscriptCapturePlan): Boolean

    fun markPendingMoveAutoScrollActivity()

    fun updateState(transform: ShellUiState.() -> ShellUiState)
}
