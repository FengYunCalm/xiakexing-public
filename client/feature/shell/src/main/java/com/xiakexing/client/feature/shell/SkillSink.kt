package com.xiakexing.client.feature.shell

/**
 * Skill 域 Sink——编排层向 VM/其他域暴露的副作用接口。
 */
internal interface SkillSink {
    fun clearPendingInteractionRequest()

    fun clearPendingProtocolSheetRequest()

    fun pendingProtocolSheetRequest(): PendingProtocolSheetRequest?

    fun flushPendingRoomRefresh()

    fun prepareProtocolSheetCommand(
        currentSheet: ShellSheetState?,
        command: String,
    )

    fun prepareHiddenProtocolSheetCommand(
        currentSheet: ShellSheetState?,
        command: String,
    )

    fun sendDirectCommand(
        command: String,
        suppressImmediateScroll: Boolean,
    )

    fun showActionConfirmation(
        title: String,
        action: ShellAction,
        dismissSheetAfterConfirm: Boolean,
    )

    fun dismissPopup()

    fun appendTranscript(line: String)

    fun currentState(): ShellUiState

    fun updateState(transform: ShellUiState.() -> ShellUiState)
}
