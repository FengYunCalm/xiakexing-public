package com.xiakexing.client.feature.shell

import com.xiakexing.client.core.protocol.payload.MapGridPacket
import com.xiakexing.client.core.protocol.payload.ProjectionIdentity
import com.xiakexing.client.core.protocol.payload.ProjectionStatePacket
import com.xiakexing.client.core.protocol.payload.QuestEntryPacket
import com.xiakexing.client.core.protocol.payload.QuestListPacket
import com.xiakexing.client.core.protocol.payload.QuestUpdatePacket
import com.xiakexing.client.core.protocol.payload.RoutePreviewPacket
import kotlinx.coroutines.Job

internal const val ROUTE_COMMAND_INTERVAL_MS = 300L

internal class QuestRuntimeState(
    var latestProjection: ProjectionIdentity? = null,
    var questDetailRequestAtMillis: Long = 0L,
    var nextRouteRequestSerial: Long = 0L,
    var pendingRoutePreview: RoutePreviewRequest? = null,
    var pendingRouteKey: String? = null,
    var lastSuccessfulRouteKey: String? = null,
    val sourceCandidates: MutableMap<String, List<TrackedObjectiveState>> = linkedMapOf(),
    val suppressedAcceptanceIds: MutableSet<String> = mutableSetOf(),
    var routeJob: Job? = null,
    var convergedDomainRevision: Long = -1L,
    var hasQuestListCache: Boolean = false,
    var latestDomainRevision: Long = -1L,
    val latestFloatingFeedbackSignatures: MutableMap<String, String> = mutableMapOf(),
    var roomId: String? = null,
    var projectionRuntime: ProjectionRuntimeState = ProjectionRuntimeState(),
    val seenDeltaIdsByRevision: MutableMap<Long, MutableSet<String>> = mutableMapOf(),
)

internal fun buildQuestDetailActionsKeepingNavigation(
    questId: String,
    quest: QuestEntryPacket?,
    trackedQuest: TrackedQuestState,
    existingActions: List<ShellAction>,
): List<ShellAction> {
    val rebuiltActions = buildQuestDetailActions(questId, quest, trackedQuest)
    if (!trackedQuest.active) {
        return rebuiltActions
    }
    val rebuiltCommands = rebuiltActions.map { action -> action.command }.toSet()
    val preservedActions =
        existingActions.filter { action ->
            action.command !in rebuiltCommands && action.isQuestNavigationAction()
        }
    val trackingAction =
        if (
            trackedQuest.questId == questId &&
            rebuiltActions.none { action -> action.isQuestTrackingAction() }
        ) {
            listOf(
                ShellAction(
                    "取消追踪",
                    UNTRACK_QUEST_COMMAND,
                    dismissAfterInvoke = false,
                    role = "secondary",
                ),
            )
        } else {
            emptyList()
        }
    return preservedActions + trackingAction + rebuiltActions
}

internal fun List<QuestEntryPacket>.upsert(packet: QuestUpdatePacket): List<QuestEntryPacket> {
    val existingIndex = indexOfFirst { it.questId == packet.questId }
    if (existingIndex < 0) {
        return this + packet.toQuestEntryPacket()
    }

    return mapIndexed { index, quest ->
        if (index != existingIndex) {
            return@mapIndexed quest
        }
        // quest info 探针是服务端全量真值，空字符串/空数组也必须覆盖旧缓存；
        // 普通 delta 才允许沿用缺省字段的增量合并语义。
        if (packet.isInfoRefreshProbe) {
            return@mapIndexed packet.toQuestEntryPacket()
        }
        // 状态词汇表唯一真源：此处曾内联第二份词表，缺 in_progress/ongoing，
        // 服务端下发这些词形时会把 next_step/target/recommended 整体清空。
        val keepPreviousNextStep = packet.status.isQuestInProgressStatus()
        quest.copy(
            sourceKind = packet.sourceKind.ifBlank { quest.sourceKind },
            objectiveId = packet.objectiveId.ifBlank { quest.objectiveId },
            acceptanceId = packet.acceptanceId.ifBlank { quest.acceptanceId },
            acceptanceRevision =
                if (packet.isV2 || packet.acceptanceRevision > 0L) {
                    packet.acceptanceRevision
                } else {
                    quest.acceptanceRevision
                },
            autoTrack = if (packet.isV2) packet.autoTrack else packet.autoTrack || quest.autoTrack,
            trackingPriority =
                if (packet.isV2 || packet.trackingPriority > 0) {
                    packet.trackingPriority
                } else {
                    quest.trackingPriority
                },
            name = packet.name.ifBlank { quest.name },
            status = packet.status.ifBlank { quest.status },
            progress = packet.progress,
            total = packet.total,
            providerId = packet.providerId.ifBlank { quest.providerId },
            providerName = packet.providerName.ifBlank { quest.providerName },
            summary = packet.summary.ifBlank { quest.summary },
            rewards = packet.rewards,
            canTurnIn = packet.canTurnIn,
            nextStepLabel =
                packet.nextStepLabel.ifBlank {
                    if (keepPreviousNextStep) quest.nextStepLabel else ""
                },
            targetRoomId = packet.targetRoomId.ifBlank { if (keepPreviousNextStep) quest.targetRoomId else "" },
            targetRoomName =
                packet.targetRoomName.ifBlank {
                    if (keepPreviousNextStep) quest.targetRoomName else ""
                },
            targetEntityId =
                packet.targetEntityId.ifBlank {
                    if (keepPreviousNextStep) quest.targetEntityId else ""
                },
            targetEntityName =
                packet.targetEntityName.ifBlank {
                    if (keepPreviousNextStep) quest.targetEntityName else ""
                },
            recommendedCommand =
                packet.recommendedCommand.ifBlank {
                    if (keepPreviousNextStep) quest.recommendedCommand else ""
                },
            canNavigate = packet.canNavigate || (keepPreviousNextStep && quest.canNavigate),
            steps = if (packet.isV2) packet.steps else packet.steps.ifEmpty { quest.steps },
        )
    }
}

internal fun TrackedQuestState.validatedAgainst(quests: List<QuestEntryPacket>): TrackedQuestState {
    if (!active) {
        return this
    }
    return validatedAgainst(quests.firstOrNull { it.questId == questId })
}

internal fun QuestEntryPacket.canKeepTrackedQuest(): Boolean {
    if (canTurnIn) {
        return true
    }
    // 复用状态谓词，避免第三份内联词表再次分叉（曾缺 in_progress/ongoing，
    // 服务端下发这些词形时追踪被误清）。可提交/可交付是追踪态额外词形。
    return status.isQuestInProgressStatus() ||
        status.isQuestDeliveredStatus() ||
        status.trim().lowercase() in setOf("可提交", "可交付", "turn_in", "ready")
}

internal fun ShellSheetState.rebuildQuestDetailActionsForTrackedQuest(
    questId: String,
    quest: QuestEntryPacket?,
    trackedQuest: TrackedQuestState,
): ShellSheetState {
    if (!isQuestDetailSheet() || !matchesQuestInfoCommand(questId)) {
        return this
    }
    return copy(actions = buildQuestDetailActions(questId, quest, trackedQuest))
}

internal fun routeKeyOf(
    projection: ProjectionIdentity,
    startRoomId: String,
    objective: TrackedObjectiveState,
): String {
    return listOf(
        projection.projectionId,
        projection.projectionRevision,
        startRoomId,
        objective.sourceKind,
        objective.objectiveId,
        objective.acceptanceId,
        objective.acceptanceRevision,
        objective.targetRoomId,
    ).joinToString("|")
}

internal fun allCandidates(state: QuestRuntimeState): List<TrackedObjectiveState> =
    state.sourceCandidates.values.flatten()

internal fun acceptQuestSnapshot(
    state: QuestRuntimeState,
    packet: QuestListPacket,
): Boolean {
    if (!packet.isV2) {
        LegacyCompatibilityDiagnostics.record("quest.legacy_snapshot")
        return true
    }
    if (packet.domainRevision < state.latestDomainRevision) return false
    state.latestDomainRevision = packet.domainRevision
    state.convergedDomainRevision = packet.domainRevision
    state.seenDeltaIdsByRevision.keys.removeAll { it < packet.domainRevision }
    return true
}

internal fun acceptProjection(
    state: QuestRuntimeState,
    projection: ProjectionStatePacket?,
    packetProjection: ProjectionIdentity?,
    packetIsV2: Boolean,
): Boolean {
    if (packetIsV2) {
        if (projection == null || packetProjection == null || !packetProjection.matches(projection.projection)) {
            return false
        }
    } else if (projection != null && projection.projection.isV2) {
        // Legacy task packets are v2-projection compatibility only
        LegacyCompatibilityDiagnostics.record("quest.legacy_rejected_for_v2")
        return false
    }

    val effectiveProjection = projection?.projection
    if (effectiveProjection != null && state.latestProjection != null &&
        !effectiveProjection.matches(state.latestProjection)
    ) {
        state.hasQuestListCache = false
        state.latestDomainRevision = -1L
        state.convergedDomainRevision = -1L
        state.seenDeltaIdsByRevision.clear()
        state.latestFloatingFeedbackSignatures.clear()
    }
    if (effectiveProjection != null) {
        state.latestProjection = effectiveProjection
    }
    return true
}

internal fun acceptQuestDelta(
    state: QuestRuntimeState,
    packet: QuestUpdatePacket,
): Boolean {
    if (!packet.isV2) {
        LegacyCompatibilityDiagnostics.record("quest.legacy_delta")
        return true
    }
    if (packet.deltaId.isBlank()) return false
    if (packet.domainRevision < state.latestDomainRevision || packet.domainRevision <= state.convergedDomainRevision) {
        return false
    }
    if (packet.domainRevision > state.latestDomainRevision) {
        state.latestDomainRevision = packet.domainRevision
        state.seenDeltaIdsByRevision.keys.removeAll { it < packet.domainRevision }
    }
    return state.seenDeltaIdsByRevision
        .getOrPut(packet.domainRevision) { mutableSetOf() }
        .add(packet.deltaId)
}

internal fun QuestFloatingFeedback.rememberLatestSignature(state: QuestRuntimeState) {
    state.latestFloatingFeedbackSignatures[dedupeKey] = signature
}

internal fun TrackedObjectiveState.normalizedObjective(): TrackedObjectiveState {
    val normalizedSource = sourceKind.trim().ifBlank { "quest" }
    val fallbackId = questId.trim()
    if (objectiveId.isBlank() || acceptanceId.isBlank()) {
        LegacyCompatibilityDiagnostics.record("quest.legacy_objective_identity")
    }
    return copy(
        sourceKind = normalizedSource,
        objectiveId = objectiveId.trim().ifBlank { "legacy:$normalizedSource:$fallbackId" },
        acceptanceId = acceptanceId.trim().ifBlank { "legacy:$normalizedSource:$fallbackId" },
        acceptanceRevision = acceptanceRevision.coerceAtLeast(0L),
    )
}

internal fun TrackedObjectiveState.withPreservedRoute(
    previous: TrackedObjectiveState,
): TrackedObjectiveState {
    if (!hasSameAcceptance(previous) || targetRoomId != previous.targetRoomId) {
        return copy(routeCommands = emptyList(), routeRooms = emptyList(), routeError = "")
    }
    return copy(
        routeCommands = previous.routeCommands,
        routeRooms = previous.routeRooms,
        routeError = previous.routeError,
        maxSteps = previous.maxSteps,
    )
}

internal fun TrackedObjectiveState.hasSameAcceptance(other: TrackedObjectiveState): Boolean {
    return sourceKind == other.sourceKind &&
        objectiveId == other.objectiveId &&
        acceptanceId == other.acceptanceId &&
        acceptanceRevision == other.acceptanceRevision
}

internal fun RoutePreviewRequest.matches(
    objective: TrackedObjectiveState,
    state: ShellUiState,
): Boolean {
    return projection.matches(state.projectionRuntime.projection?.projection) &&
        startRoomId == state.roomId &&
        sourceKind == objective.sourceKind &&
        objectiveId == objective.objectiveId &&
        acceptanceId == objective.acceptanceId &&
        acceptanceRevision == objective.acceptanceRevision &&
        targetRoomId == objective.targetRoomId
}

internal fun ShellSheetState?.withTrackedQuestDetailAction(
    quest: QuestEntryPacket,
    trackedQuest: TrackedQuestState,
): ShellSheetState? {
    val current = this ?: return null
    if (!current.isQuestDetailSheet() || !current.matchesQuestInfoCommand(quest.questId)) {
        return current
    }
    return current.copy(
        actions = buildQuestDetailActions(quest.questId, quest, trackedQuest),
        detailLines = quest.toQuestWorkbenchDetailLines().ifEmpty { current.detailLines },
        panelStats = quest.toQuestWorkbenchStats().ifEmpty { current.panelStats },
    )
}

internal fun ShellSheetState?.withClearedTrackedQuestDetailAction(): ShellSheetState? {
    val current = this ?: return null
    if (!current.isQuestDetailSheet()) {
        return current
    }
    val questId =
        current.primaryAction?.command.orEmpty()
            .trim()
            .questCommandId()
    if (questId.isBlank()) {
        return current
    }
    return current.copy(
        actions =
            current.actions.map { action ->
                if (action.command == UNTRACK_QUEST_COMMAND) {
                    ShellAction("追踪", "$TRACK_QUEST_PREFIX$questId", dismissAfterInvoke = false)
                } else {
                    action
                }
            },
    )
}

internal fun ShellSheetState?.rebuildMapSheetIfVisible(
    roomTitle: String,
    exits: List<com.xiakexing.client.core.protocol.payload.ExitSnapshot>,
    mapGrid: MapGridPacket?,
    trackedQuest: TrackedQuestState,
): ShellSheetState? {
    if (this?.source != PopupSource.TITLE_ACTION || resolvedSheetKind() != ShellSheetKind.MAP) {
        return this
    }
    return buildMapSheetState(roomTitle, exits, mapGrid, trackedQuest)
}

internal fun ShellAction.isQuestNavigationAction(): Boolean {
    val normalizedCommand = command.trim().lowercase()
    return normalizedCommand.startsWith("route ")
}

internal fun ShellAction.isQuestTrackingAction(): Boolean {
    val normalizedCommand = command.trim().lowercase()
    return normalizedCommand.startsWith(TRACK_QUEST_PREFIX) || normalizedCommand == UNTRACK_QUEST_COMMAND
}

internal fun TrackedQuestState.validatedAgainst(quest: QuestEntryPacket?): TrackedQuestState {
    if (!active) {
        return this
    }
    if (quest == null || !quest.canKeepTrackedQuest()) {
        return TrackedQuestState()
    }
    return quest.toTrackedQuestState(this)
}

internal fun RoutePreviewPacket.matches(
    request: RoutePreviewRequest,
    state: ShellUiState,
): Boolean {
    if (!isV2 ||
        state.roomId != request.startRoomId ||
        targetRoomId.isNotBlank() && targetRoomId != request.targetRoomId
    ) {
        return false
    }
    return requestId == request.requestId &&
        projection?.matches(request.projection) == true &&
        sourceKind == request.sourceKind &&
        objectiveId == request.objectiveId &&
        acceptanceId == request.acceptanceId &&
        acceptanceRevision == request.acceptanceRevision &&
        startRoomId == request.startRoomId
}
