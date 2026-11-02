package com.xiakexing.client.feature.shell

import com.xiakexing.client.core.protocol.payload.DialogPacket

/**
 * InteractionSession 域统一 Sink——6 个原 coordinator Sink 的并集
 * （capture/protocol-capture/sheet/dialog/mail/npc-learning 全部副作用）。
 */
internal interface InteractionSessionSink {
    fun currentState(): ShellUiState

    fun updateState(transform: ShellUiState.() -> ShellUiState)

    fun appendInteractionFeedback(line: String)

    fun showFloatingFeedback(line: String)

    fun pendingInteractionRequest(): PendingInteractionRequest?

    fun replacePendingInteractionRequest(request: PendingInteractionRequest)

    fun clearPendingInteractionRequest()

    fun requestSceneItemDetail(target: InteractionObject)

    fun popSheet(): Boolean

    fun flushPendingRoomRefresh()

    fun handleNpcLearningDialog(packet: DialogPacket): Boolean

    fun handleMailDialog(packet: DialogPacket): Boolean

    fun handleInventoryItemDialog(packet: DialogPacket): Boolean

    fun captureQuestDetailDialog(packet: DialogPacket): Boolean

    fun latestQuestEntryName(questId: String): String

    fun appendTranscript(line: String)

    fun appendNarrativeTranscript(line: String)

    fun cancelInteractionRequestSettle()

    fun clearInteractionRequestSettleJob()

    fun clearPendingProtocolSheetRequest()
}
