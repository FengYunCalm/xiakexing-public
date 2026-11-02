package com.xiakexing.client.feature.shell

import com.xiakexing.client.core.protocol.payload.RoutePreviewPacket
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

/**
 * Quest 路线/命令编排层——路线预览、超时、路线命令循环与浮动反馈。
 * 纯变换在 QuestProjectionReducer.kt，quest 域编排在 QuestOrchestration.kt。
 */
internal fun handleRoutePreviewPacket(
    state: QuestRuntimeState,
    sink: QuestCoordinator.Sink,
    packet: RoutePreviewPacket,
) {
    val request = state.pendingRoutePreview ?: return
    val currentState = sink.currentState()
    if (!packet.matches(request, currentState)) {
        return
    }
    val completedRouteKey = state.pendingRouteKey
    clearPendingRoutePreview(state, sink)
    if (packet.ok) {
        state.lastSuccessfulRouteKey = completedRouteKey
    } else if (state.lastSuccessfulRouteKey == completedRouteKey) {
        state.lastSuccessfulRouteKey = null
    }
    sink.updateState {
        val nextTracked = trackedQuest.withRoutePreview(packet)
        if (nextTracked == trackedQuest) {
            return@updateState this
        }
        val nextHub = objectiveHub.copy(trackedObjective = nextTracked)
        copy(
            trackedQuest = nextTracked,
            objectiveHub = nextHub,
            sheet =
                sheet
                    .rebuildMapSheetIfVisible(
                        roomTitle,
                        exits,
                        sink.latestMapGrid(),
                        nextTracked,
                    )
                    .rebuildObjectiveHubIfVisible(nextHub),
        )
    }
}

internal fun handleRoutePreviewTimeout(
    state: QuestRuntimeState,
    sink: QuestCoordinator.Sink,
    requestId: String,
) {
    val request = state.pendingRoutePreview ?: return
    if (request.requestId != requestId) {
        return
    }
    val currentState = sink.currentState()
    val stillCurrent = request.matches(currentState.trackedQuest, currentState)
    clearPendingRoutePreview(state, sink)
    if (!stillCurrent) {
        return
    }
    sink.updateState {
        val nextTracked =
            trackedQuest.copy(
                routeCommands = emptyList(),
                routeRooms = emptyList(),
                routeError = "路线请求超时，请重试",
            )
        val nextHub = objectiveHub.copy(trackedObjective = nextTracked)
        copy(
            trackedQuest = nextTracked,
            objectiveHub = nextHub,
            sheet =
                sheet
                    .rebuildMapSheetIfVisible(
                        roomTitle,
                        exits,
                        sink.latestMapGrid(),
                        nextTracked,
                    )
                    .rebuildObjectiveHubIfVisible(nextHub),
        )
    }
}

internal fun refreshTrackedRoute(
    state: QuestRuntimeState,
    sink: QuestCoordinator.Sink,
) {
    requestRoutePreview(state, sink, sink.currentState().trackedQuest)
}

internal fun requestRoutePreview(
    state: QuestRuntimeState,
    sink: QuestCoordinator.Sink,
    trackedQuest: TrackedQuestState,
    force: Boolean = false,
) {
    if (!trackedQuest.active || trackedQuest.targetRoomId.isBlank()) {
        clearPendingRoutePreview(state, sink)
        return
    }
    val currentState = sink.currentState()
    val projection = currentState.projectionRuntime.projection?.projection ?: return
    val startRoomId = currentState.roomId.trim()
    if (startRoomId.isBlank()) {
        clearPendingRoutePreview(state, sink)
        return
    }
    val key = routeKeyOf(projection, startRoomId, trackedQuest)
    val existing = state.pendingRoutePreview
    /*
     * 同一 (投影, 起始房间, source_kind, objective, acceptance, 目标房间) 的
     * ROUT 只允许存在一次；房间未变、目标未变时不得因 TITLE/MAPS 重复请求。
     * 显式目标切换/房间变化会产生新 key，自动放行。
     */
    if (!force && state.lastSuccessfulRouteKey == key) {
        return
    }
    if (!force && state.pendingRouteKey == key && existing?.matches(trackedQuest, currentState) == true) {
        return
    }
    if (existing != null) {
        clearPendingRoutePreview(state, sink)
    }
    state.nextRouteRequestSerial += 1L
    val request =
        RoutePreviewRequest(
            requestId = "route-${projection.projectionRevision}-${state.nextRouteRequestSerial}",
            projection = projection,
            sourceKind = trackedQuest.sourceKind,
            objectiveId = trackedQuest.objectiveId,
            acceptanceId = trackedQuest.acceptanceId,
            acceptanceRevision = trackedQuest.acceptanceRevision,
            startRoomId = startRoomId,
            targetRoomId = trackedQuest.targetRoomId,
        )
    state.pendingRoutePreview = request
    state.pendingRouteKey = key
    sink.sendRoutePreviewCommand(request)
    sink.scheduleRoutePreviewTimeout(request.requestId)
}

internal fun clearPendingRoutePreview(
    state: QuestRuntimeState,
    sink: QuestCoordinator.Sink,
) {
    state.pendingRoutePreview?.requestId?.let(sink::cancelRoutePreviewTimeout)
    state.pendingRoutePreview = null
    state.pendingRouteKey = null
}

internal fun startRouteCommands(
    state: QuestRuntimeState,
    sink: QuestCoordinator.Sink,
    scope: CoroutineScope,
    commands: List<String>,
) {
    val routeCommands =
        commands.map { command -> command.trim() }
            .filter { command -> command.isNotBlank() }
    if (routeCommands.isEmpty()) {
        return
    }

    state.routeJob?.cancel()
    state.routeJob =
        scope.launch {
            routeCommands.forEachIndexed { index, command ->
                if (index > 0) {
                    delay(ROUTE_COMMAND_INTERVAL_MS)
                }
                sink.sendRouteCommand(command)
            }
        }
}

internal fun cancelRouteCommands(state: QuestRuntimeState) {
    state.routeJob?.cancel()
    state.routeJob = null
}

internal fun QuestFloatingFeedback.emitIfChanged(
    state: QuestRuntimeState,
    sink: QuestCoordinator.Sink,
) {
    if (state.latestFloatingFeedbackSignatures[dedupeKey] == signature) {
        return
    }
    state.latestFloatingFeedbackSignatures[dedupeKey] = signature
    val message =
        FloatingMessageState(
            text = text,
            level = "quest",
            ttlMs = ttlMs,
            dedupeKey = dedupeKey,
        )
    sink.updateState(buildFloatingEventAppendTransform(message))
}
