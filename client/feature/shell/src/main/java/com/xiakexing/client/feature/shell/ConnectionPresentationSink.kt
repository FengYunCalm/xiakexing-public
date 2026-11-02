package com.xiakexing.client.feature.shell

/**
 * 连接呈现协调器 sink 面（自 ConnectionPresentationCoordinator 拆出，收窄 owner 文件）。
 */
interface ConnectionPresentationSink {
    fun currentState(): ShellUiState

    fun resetIncomingStream()

    fun resetProtocolPacketGate()

    fun resetMessageForConnection(
        endpointLine: String,
        preserveNoticeFeed: Boolean,
    )

    fun visibleTranscript(): List<TranscriptLine>

    fun recentNoticeFeed(): List<String>

    fun appendTranscript(line: String)

    fun appendErrorFeedback(message: String)

    fun clearPendingRoomRefresh()

    fun clearPendingMoveAutoScroll()

    fun clearPendingInteractionRequest()

    fun clearPendingProtocolSheetRequest()

    fun clearPendingProtocolPopupRequest()

    fun clearPendingInventoryInspectItem()

    fun clearInventoryInspectItem()

    fun clearTrackedQuest()

    fun hasCommittedCompleteRoomSnapshot(
        projectionId: String,
        projectionRevision: Long?,
        projectionGeneration: Long?,
    ): Boolean = hasCommittedCompleteRoomSnapshot(projectionId, projectionRevision)

    fun hasCommittedCompleteRoomSnapshot(
        projectionId: String,
        projectionRevision: Long?,
    ): Boolean = hasCommittedAnyCompleteRoomSnapshot()

    @Suppress("UNUSED_PARAMETER")
    fun hasCommittedAnyCompleteRoomSnapshot(): Boolean = false

    fun requestAuthoritativeResync()

    fun resetConnectionTransientState()

    fun resetRoomCaches()

    fun clearSkillSummaries()

    fun clearQuestCache()

    fun clearUnhandledPacketTypes()

    fun shouldShowLoginError(): Boolean

    fun showLoginError(message: String)

    fun maybeStartAutomaticUpdateCheck()

    fun updateState(transform: ShellUiState.() -> ShellUiState)

    fun updateStateWithSessionEvent(
        event: ShellEvent,
        transform: ShellUiState.() -> ShellUiState = { this },
    )
}
