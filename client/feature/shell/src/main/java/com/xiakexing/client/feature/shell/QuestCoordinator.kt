package com.xiakexing.client.feature.shell

import com.google.gson.JsonObject
import com.xiakexing.client.core.protocol.payload.DialogPacket
import com.xiakexing.client.core.protocol.payload.MapGridPacket
import com.xiakexing.client.core.protocol.payload.QuestEntryPacket
import com.xiakexing.client.core.protocol.payload.QuestListPacket
import com.xiakexing.client.core.protocol.payload.QuestUpdatePacket
import com.xiakexing.client.core.protocol.payload.RoutePreviewPacket
import kotlinx.coroutines.CoroutineScope

/**
 * Quest 域 owner——任务列表/增量/详情编排、目标追踪与路线预览、
 * 路线命令执行。编排在 QuestOrchestration.kt / QuestDetailOrchestration.kt /
 * QuestRouteOrchestration.kt，纯变换与状态在 QuestProjectionReducer.kt。
 */
internal class QuestCoordinator(
    private val scope: CoroutineScope,
    private val sink: Sink,
) {
    interface Sink {
        fun currentState(): ShellUiState

        fun pendingInteractionRequest(): PendingInteractionRequest?

        fun pendingProtocolSheetRequest(): PendingProtocolSheetRequest?

        fun clearPendingInteractionRequest()

        fun clearPendingProtocolSheetRequest()

        fun clearPendingProtocolPopupRequest()

        fun prepareHiddenProtocolSheetCommand(
            currentSheet: ShellSheetState?,
            command: String,
        )

        fun cancelProtocolSheetSettle()

        fun sendDirectCommand(
            command: String,
            suppressImmediateScroll: Boolean,
        )

        fun autoTrackNewbieQuest(quests: List<QuestEntryPacket>)

        fun updateState(transform: ShellUiState.() -> ShellUiState)

        fun latestMapGrid(): MapGridPacket?

        fun sendRoutePreviewCommand(request: RoutePreviewRequest)

        fun scheduleRoutePreviewTimeout(requestId: String)

        fun cancelRoutePreviewTimeout(requestId: String)

        fun sendRouteCommand(command: String)
    }

    private val state = QuestRuntimeState()

    fun clearQuestCache() = clearQuestCache(state, sink)

    fun latestQuestEntry(questId: String) = latestQuestEntry(sink, questId)

    fun handleQuestListPacket(payload: JsonObject) = handleQuestListPacket(state, sink, payload)

    fun handleQuestListPacket(packet: QuestListPacket) = handleQuestListPacket(state, sink, packet)

    fun handleQuestUpdatePacket(payload: JsonObject) = handleQuestUpdatePacket(state, sink, payload)

    fun handleQuestUpdatePacket(packet: QuestUpdatePacket) =
        handleQuestUpdatePacket(state, sink, packet)

    fun captureQuestDetailDialog(packet: DialogPacket) =
        captureQuestDetailDialog(state, sink, packet)

    fun showQuestListSheet(refresh: Boolean = false) = showQuestListSheet(state, sink, refresh)

    fun requestQuestListFromDock() = requestQuestListFromDock(sink)

    fun showQuestDetailSheet(questId: String) = showQuestDetailSheet(state, sink, questId)

    fun trackQuest(quest: QuestEntryPacket?) = trackQuest(state, sink, quest)

    fun autoTrackNewbieQuest(quests: List<QuestEntryPacket>) =
        autoTrackNewbieQuest(state, sink, quests)

    fun trackObjective(objective: TrackedObjectiveState?) = trackObjective(state, sink, objective)

    fun replaceSourceCandidates(
        sourceKind: String,
        candidates: List<TrackedObjectiveState>,
    ) = replaceSourceCandidates(state, sink, sourceKind, candidates)

    fun clearTrackedQuest() = clearTrackedQuest(state, sink)

    fun resetProjectionState() = resetProjectionState(state, sink)

    fun handleRoutePreviewPacket(packet: RoutePreviewPacket) =
        handleRoutePreviewPacket(state, sink, packet)

    fun handleRoutePreviewTimeout(requestId: String) =
        handleRoutePreviewTimeout(state, sink, requestId)

    fun refreshTrackedRoute() = refreshTrackedRoute(state, sink)

    fun startRouteCommands(commands: List<String>): Unit =
        startRouteCommands(state, sink, scope, commands)

    fun cancelRouteCommands(): Unit = cancelRouteCommands(state)
}
