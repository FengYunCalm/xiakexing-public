package com.xiakexing.client.feature.shell

import com.google.gson.JsonObject
import com.xiakexing.client.core.protocol.payload.DialogPacket
import com.xiakexing.client.core.protocol.payload.QuestDeltaOperation
import com.xiakexing.client.core.protocol.payload.QuestEntryPacket
import com.xiakexing.client.core.protocol.payload.QuestListPacket
import com.xiakexing.client.core.protocol.payload.QuestUpdatePacket

/**
 * Quest 域编排层——quest 列表/详情/追踪/赏金的状态变换与 sink 副作用。
 * 纯变换在 QuestProjectionReducer.kt，路线/命令编排在 QuestRouteOrchestration.kt。
 */
internal fun clearQuestCache(
    state: QuestRuntimeState,
    sink: QuestCoordinator.Sink,
) {
    state.hasQuestListCache = false
    state.latestDomainRevision = -1L
    state.convergedDomainRevision = -1L
    state.seenDeltaIdsByRevision.clear()
    state.latestFloatingFeedbackSignatures.clear()
    state.latestProjection = null
    sink.updateState {
        val nextHub = objectiveHub.copy(quests = emptyList())
        val nextSheet = sheet.rebuildObjectiveHubIfVisible(nextHub)
        // 会话态与 sheet 成对写入：缓存清理不得把已打开的任务详情会话重置成列表，
        // 否则后续任一 quest_list 会经 validatedAgainst 把详情顶回列表。
        withQuestSheet(
            nextSheet,
            if (nextSheet == null) QuestPanelSessionState() else questPanelSession,
        ).copy(objectiveHub = nextHub)
    }
}

internal fun latestQuestEntry(
    sink: QuestCoordinator.Sink,
    questId: String,
): QuestEntryPacket? {
    val normalizedQuestId = questId.trim()
    return sink.currentState().objectiveHub.quests.firstOrNull { it.questId == normalizedQuestId }
}

internal fun handleQuestListPacket(
    state: QuestRuntimeState,
    sink: QuestCoordinator.Sink,
    payload: JsonObject,
) {
    handleQuestListPacket(state, sink, QuestListPacket.fromJson(payload))
}

internal fun handleQuestUpdatePacket(
    state: QuestRuntimeState,
    sink: QuestCoordinator.Sink,
    payload: JsonObject,
) {
    handleQuestUpdatePacket(state, sink, QuestUpdatePacket.fromJson(payload))
}

internal fun openNpcRequestedQuestDetail(
    sink: QuestCoordinator.Sink,
    packet: QuestUpdatePacket,
    request: PendingInteractionRequest,
    currentState: ShellUiState,
    currentSession: QuestPanelSessionState,
): Boolean {
    val currentQuest =
        sink.currentState().objectiveHub.quests.firstOrNull {
            it.questId == packet.questId
        }
    if (packet.operation == QuestDeltaOperation.REMOVE || currentQuest == null) {
        return false
    }
    sink.clearPendingInteractionRequest()
    /* 第七轮真机反馈：NPC 弹窗路径的详情是替换式打开，返回键 pop 空栈
     * 直接 dismiss 导致「回到任务界面一片空白」——把当前 NPC 任务面板
     * 压入 backStack，返回即恢复任务列表。 */
    val npcQuestPanel = currentState.sheet
    sink.updateState {
        copy(
            sheet =
                buildQuestDetailSheetState(
                    questId = packet.questId,
                    quest = currentQuest,
                    command = request.command,
                    trackedQuest = currentState.trackedQuest,
                    loading = false,
                ),
            sheetNavigation =
                sheetNavigation.copy(
                    backStack =
                        sheetNavigation.backStack +
                            SheetNavigationEntry(
                                sheet = npcQuestPanel ?: return@updateState this,
                                questPanelSession = currentSession,
                            ),
                ),
            // NPC 详情是替换式打开，不属于 dock 任务会话：此处写空会话，
            // 打开前的会话已随 backStack 压栈、返回时恢复；沿用旧会话会让
            // sheet 显示新任务而会话仍指向旧任务（漂移）。
            questPanelSession = QuestPanelSessionState(),
        )
    }
    return true
}

internal fun clearRelatedQuestSheetRequest(sink: QuestCoordinator.Sink) {
    val pending = sink.pendingProtocolSheetRequest() ?: return
    val command = pending.command.trim().lowercase()
    if (command == "quest" || command.isQuestInfoCommand()) {
        sink.clearPendingProtocolSheetRequest()
    }
}

internal fun captureQuestDetailDialog(
    state: QuestRuntimeState,
    sink: QuestCoordinator.Sink,
    packet: DialogPacket,
): Boolean {
    val pending = sink.pendingProtocolSheetRequest() ?: return false
    if (pending.command.isLookInteractionCommand()) {
        return captureQuestLookDialog(sink, packet, pending)
    }
    if (!pending.command.isQuestInfoCommand()) {
        return false
    }
    val currentSheet = sink.currentState().sheet ?: return false
    if (!currentSheet.isQuestDetailSheet() || currentSheet.sheetIdentity() != pending.sheetIdentity) {
        return false
    }

    val questId = pending.command.trim().removePrefix("quest info ").trim()
    if (state.questDetailRequestAtMillis > 0L) {
        android.util.Log.d(
            "QuestDetailPerf",
            "info round-trip ${System.currentTimeMillis() - state.questDetailRequestAtMillis}ms (id=$questId)",
        )
        state.questDetailRequestAtMillis = 0L
    }
    val quest = sink.currentState().objectiveHub.quests.firstOrNull { it.questId == questId }
    // 结构化数据优先；若本地尚未缓存该任务，保留这次 DLOG 的可读正文。
    // 对已缓存任务，空列表是服务端的权威清空，不得再回填旧值。
    val nextDetailLines = quest?.toQuestWorkbenchDetailLines() ?: currentSheet.detailLines
    sink.updateState {
        val latestSheet = sheet
        if (latestSheet == null || latestSheet.sheetIdentity() != pending.sheetIdentity) {
            return@updateState this
        }
        withQuestSheet(
            latestSheet.copy(
                dialogId = packet.dialogId.ifBlank { latestSheet.dialogId },
                detailLines = nextDetailLines,
                interactionLines = emptyList(),
                panelStats = quest?.toQuestWorkbenchStats() ?: latestSheet.panelStats,
                actions =
                    buildQuestDetailActionsKeepingNavigation(
                        questId = questId,
                        quest = quest,
                        trackedQuest = trackedQuest,
                        existingActions = latestSheet.actions,
                    ),
                loading = false,
            ),
            QuestPanelSessionState(QuestPanelMode.DETAIL, questId),
        ).copy(dialog = null)
    }
    sink.clearPendingProtocolSheetRequest()
    sink.clearPendingInteractionRequest()
    return true
}

internal fun showQuestListSheet(
    state: QuestRuntimeState,
    sink: QuestCoordinator.Sink,
    refresh: Boolean = false,
) {
    if (!refresh) {
        sink.clearPendingProtocolSheetRequest()
    }
    val shouldLoad = refresh || !state.hasQuestListCache
    val questSheet =
        buildQuestListSheetState(sink.currentState().objectiveHub.quests, loading = false)
    sink.updateState {
        withQuestSheet(questSheet).copy(dialog = null, exitOverlay = null)
    }
    if (shouldLoad) {
        sink.prepareHiddenProtocolSheetCommand(questSheet, command = "quest")
        sink.sendDirectCommand("quest", suppressImmediateScroll = true)
    }
}

internal fun requestQuestListFromDock(sink: QuestCoordinator.Sink) {
    sink.clearPendingInteractionRequest()
    sink.clearPendingProtocolPopupRequest()
    val questSheet =
        buildQuestListSheetState(sink.currentState().objectiveHub.quests, loading = false)
    sink.prepareHiddenProtocolSheetCommand(questSheet, command = "quest")
    sink.updateState {
        withQuestSheet(questSheet).copy(dialog = null, exitOverlay = null)
    }
    sink.sendDirectCommand("quest", suppressImmediateScroll = true)
}

internal fun showQuestDetailSheet(
    state: QuestRuntimeState,
    sink: QuestCoordinator.Sink,
    questId: String,
) {
    val normalizedQuestId = questId.trim()
    val command = "quest info $normalizedQuestId"
    state.questDetailRequestAtMillis = System.currentTimeMillis()
    // 两阶段渲染根除：打开详情不渲染列表缓存行，直接空白态等 quest info 全量到达，
    // 避免"先一段字再刷新为另一段字"的割裂体验。
    val cachedName =
        sink.currentState().objectiveHub.quests.firstOrNull {
            it.questId == normalizedQuestId
        }?.displayName()
    val questSheet =
        buildQuestDetailSheetState(
            normalizedQuestId,
            quest = null,
            command,
            sink.currentState().trackedQuest,
            loading = true,
        ).copy(title = cachedName ?: normalizedQuestId.ifBlank { "任务" })
    if (normalizedQuestId.isNotBlank()) {
        sink.prepareHiddenProtocolSheetCommand(
            currentSheet = questSheet,
            command = command,
        )
        sink.cancelProtocolSheetSettle()
    } else {
        sink.clearPendingProtocolSheetRequest()
    }
    sink.updateState {
        copy(
            sheet = questSheet,
            questPanelSession =
                QuestPanelSessionState(
                    QuestPanelMode.DETAIL,
                    normalizedQuestId,
                ),
            dialog = null,
            exitOverlay = null,
        )
    }
    if (normalizedQuestId.isNotBlank()) {
        sink.sendDirectCommand(command, suppressImmediateScroll = true)
    }
}

internal fun trackQuest(
    state: QuestRuntimeState,
    sink: QuestCoordinator.Sink,
    quest: QuestEntryPacket?,
) {
    val target = quest ?: return
    val previous = sink.currentState().trackedQuest
    val tracked =
        target.toTrackedQuestState()
            .normalizedObjective()
            .withPreservedRoute(previous)
            .copy(manuallySelected = true)
    tracked.acceptanceId.takeIf { it.isNotBlank() }?.let(state.suppressedAcceptanceIds::remove)
    applyTrackedObjective(state, sink, tracked)
    sink.updateState {
        copy(sheet = sheet.withTrackedQuestDetailAction(target, tracked))
    }
}

internal fun autoTrackNewbieQuest(
    state: QuestRuntimeState,
    sink: QuestCoordinator.Sink,
    quests: List<QuestEntryPacket>,
) {
    replaceSourceCandidates(
        state,
        sink,
        sourceKind = "quest",
        candidates =
            quests
                .map { it.toTrackedQuestState() },
    )
}

internal fun trackObjective(
    state: QuestRuntimeState,
    sink: QuestCoordinator.Sink,
    objective: TrackedObjectiveState?,
) {
    val target = objective?.normalizedObjective()?.takeIf { it.active } ?: return
    target.acceptanceId.takeIf { it.isNotBlank() }?.let(state.suppressedAcceptanceIds::remove)
    val previous = sink.currentState().trackedQuest
    val tracked = target.withPreservedRoute(previous).copy(manuallySelected = true)
    applyTrackedObjective(state, sink, tracked)
}

internal fun replaceSourceCandidates(
    state: QuestRuntimeState,
    sink: QuestCoordinator.Sink,
    sourceKind: String,
    candidates: List<TrackedObjectiveState>,
) {
    val normalizedSource = sourceKind.trim().ifBlank { "quest" }
    val normalizedCandidates =
        candidates
            .map {
                    candidate ->
                candidate.copy(sourceKind = normalizedSource).normalizedObjective()
            }
            .filter { it.active }
    state.sourceCandidates[normalizedSource] = normalizedCandidates
    val current = sink.currentState().trackedQuest
    val currentCandidate = allCandidates(state).firstOrNull { it.hasSameAcceptance(current) }

    if (current.manuallySelected && currentCandidate != null) {
        applyTrackedObjective(
            state,
            sink,
            currentCandidate.withPreservedRoute(current).copy(manuallySelected = true),
        )
        return
    }
    if (current.manuallySelected && current.sourceKind != normalizedSource) {
        // An unrelated source refresh must not override a manual objective.
        return
    }
    if (current.manuallySelected && current.sourceKind == normalizedSource) {
        // The refreshed source is authoritative; a missing manual objective is no longer trackable.
        clearTrackedState(state, sink, suppressAcceptance = false)
    }

    val automatic =
        allCandidates(state)
            .asSequence()
            .filter { it.autoTrack && it.acceptanceId !in state.suppressedAcceptanceIds }
            .sortedWith(
                compareByDescending<TrackedObjectiveState> { it.trackingPriority }
                    .thenBy { it.sourceKind }
                    .thenBy { it.objectiveId },
            )
            .firstOrNull()

    if (automatic == null) {
        if (current.active && !current.manuallySelected) {
            clearTrackedState(state, sink, suppressAcceptance = false)
        }
        return
    }
    applyTrackedObjective(
        state,
        sink,
        automatic.withPreservedRoute(current).copy(manuallySelected = false),
    )
}

internal fun applyTrackedObjective(
    state: QuestRuntimeState,
    sink: QuestCoordinator.Sink,
    tracked: TrackedObjectiveState,
) {
    state.pendingRoutePreview
        ?.takeUnless { request -> request.matches(tracked, sink.currentState()) }
        ?.let { clearPendingRoutePreview(state, sink) }
    sink.updateState {
        val nextHub = objectiveHub.copy(trackedObjective = tracked)
        copy(
            trackedQuest = tracked,
            objectiveHub = nextHub,
            sheet =
                sheet
                    .rebuildMapSheetIfVisible(roomTitle, exits, sink.latestMapGrid(), tracked)
                    .rebuildObjectiveHubIfVisible(nextHub),
        )
    }
    requestRoutePreview(state, sink, tracked)
}

internal fun clearTrackedQuest(
    state: QuestRuntimeState,
    sink: QuestCoordinator.Sink,
) {
    clearTrackedState(state, sink, suppressAcceptance = true)
}

internal fun resetProjectionState(
    state: QuestRuntimeState,
    sink: QuestCoordinator.Sink,
) {
    clearPendingRoutePreview(state, sink)
    state.lastSuccessfulRouteKey = null
    state.sourceCandidates.clear()
    state.suppressedAcceptanceIds.clear()
}

internal fun clearTrackedState(
    state: QuestRuntimeState,
    sink: QuestCoordinator.Sink,
    suppressAcceptance: Boolean,
) {
    clearPendingRoutePreview(state, sink)
    state.lastSuccessfulRouteKey = null
    val previous = sink.currentState().trackedQuest
    if (suppressAcceptance && previous.acceptanceId.isNotBlank()) {
        state.suppressedAcceptanceIds += previous.acceptanceId
    }
    sink.updateState {
        val emptyTracked = TrackedQuestState()
        val nextHub = objectiveHub.copy(trackedObjective = emptyTracked)
        copy(
            trackedQuest = emptyTracked,
            objectiveHub = nextHub,
            sheet =
                sheet
                    .withClearedTrackedQuestDetailAction()
                    .rebuildMapSheetIfVisible(
                        roomTitle,
                        exits,
                        sink.latestMapGrid(),
                        emptyTracked,
                    )
                    .rebuildObjectiveHubIfVisible(nextHub),
        )
    }
}

internal fun captureQuestLookDialog(
    sink: QuestCoordinator.Sink,
    packet: DialogPacket,
    pending: PendingProtocolSheetRequest,
): Boolean {
    if (packet.source != "look_item") {
        return false
    }
    val currentSheet = sink.currentState().sheet ?: return false
    if (!currentSheet.isQuestDetailSheet() || currentSheet.sheetIdentity() != pending.sheetIdentity) {
        return false
    }

    sink.updateState {
        val latestSheet = sheet
        if (latestSheet == null || latestSheet.sheetIdentity() != pending.sheetIdentity) {
            return@updateState this
        }
        withSheet(latestSheet.copy(loading = false))
    }
    sink.clearPendingProtocolSheetRequest()
    sink.clearPendingInteractionRequest()
    return true
}

internal fun handleQuestListPacket(
    state: QuestRuntimeState,
    sink: QuestCoordinator.Sink,
    packet: QuestListPacket,
) {
    if (!acceptProjection(state, sink.currentState().projectionRuntime.projection, packet.projection, packet.isV2) || !acceptQuestSnapshot(state, packet)) {
        return
    }
    state.hasQuestListCache = true
    sink.updateState {
        val nextHub = objectiveHub.copy(quests = packet.quests)
        copy(
            objectiveHub = nextHub,
            sheet = sheet.rebuildObjectiveHubIfVisible(nextHub),
        )
    }
    sink.autoTrackNewbieQuest(packet.quests)
    val currentState = sink.currentState()
    val currentSheet = currentState.sheet
    if (currentSheet?.isQuestPanelSheet() != true || currentSheet.isNpcQuestInteractionSheet()) {
        return
    }
    val nextSession = currentState.questPanelSession.validatedAgainst(packet.quests)
    val questSheet =
        buildQuestSheetForSession(
            session = nextSession,
            quests = packet.quests,
            trackedQuest = currentState.trackedQuest,
            inlineList = false,
        )
    sink.updateState {
        withQuestSheet(questSheet, nextSession)
    }
    clearRelatedQuestSheetRequest(sink)
}
