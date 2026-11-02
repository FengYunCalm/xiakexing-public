package com.xiakexing.client.feature.shell

import com.xiakexing.client.core.protocol.payload.QuestDeltaOperation
import com.xiakexing.client.core.protocol.payload.QuestUpdatePacket

/**
 * Quest 详情编排——QuestUpdatePacket 的 typed 处理（列表/详情/追踪三态收敛）。
 */
internal fun handleQuestUpdatePacket(
    state: QuestRuntimeState,
    sink: QuestCoordinator.Sink,
    packet: QuestUpdatePacket,
) {
    // 详情刷新探针：quest info 的服务端应答是不带 delta_id 的 QUEU
    // （唯一生产者 cmds/player/quest.c varargs 默认参数）。若走增量去重门
    // 会被 deltaId 空白/revision 收敛双门静默丢弃，详情面板永远 loading。
    // 探针携带全量服务端真值快照，必须照常入账（upsert/hub/autoTrack），
    // 只是无需 delta 去重；投影门照留。
    if (packet.isInfoRefreshProbe) {
        if (!acceptProjection(state, sink.currentState().projectionRuntime.projection, packet.projection, packet.isV2) || packet.questId.isBlank()) {
            return
        }
    } else if (!acceptProjection(state, sink.currentState().projectionRuntime.projection, packet.projection, packet.isV2) || !acceptQuestDelta(state, packet) || packet.questId.isBlank()) {
        return
    }
    val currentQuests = sink.currentState().objectiveHub.quests
    val nextQuests =
        when (packet.operation) {
            QuestDeltaOperation.REMOVE -> currentQuests.filterNot { it.questId == packet.questId }
            QuestDeltaOperation.UPSERT -> currentQuests.upsert(packet)
        }
    state.hasQuestListCache = true
    sink.updateState {
        val nextHub = objectiveHub.copy(quests = nextQuests)
        copy(
            objectiveHub = nextHub,
            sheet = sheet.rebuildObjectiveHubIfVisible(nextHub),
        )
    }
    sink.autoTrackNewbieQuest(nextQuests)
    val currentQuest = nextQuests.firstOrNull { it.questId == packet.questId }
    val floatingFeedback = currentQuest?.toQuestFloatingFeedback()
    val currentState = sink.currentState()
    val currentSheet = currentState.sheet
    val pendingInteraction = sink.pendingInteractionRequest()
    val capturesNpcQuestMutation =
        currentSheet?.isNpcQuestInteractionSheet() == true &&
            pendingInteraction?.target?.kind == InteractionObjectKind.NPC &&
            pendingInteraction.command.isNpcQuestMutationCommand() &&
            pendingInteraction.command.questCommandId() == packet.questId
    val capturesNpcQuestDetail = currentSheet?.isNpcQuestDetailSheet() == true
    // NPC 弹窗路径的 quest info 详情请求（服务端只回 QUEU 不回 DLOG）。
    val npcInfoRequest =
        pendingInteraction?.takeIf { req ->
            req.command.isQuestInfoRequestFor(packet.questId)
        }
    val currentSession = currentState.questPanelSession.validatedAgainst(nextQuests)
    val capturedQuestSheet =
        currentSheet?.takeIf {
            it.isQuestPanelSheet() && !it.isNpcQuestInteractionSheet() && it.canCaptureQuestUpdate(packet.questId)
        }
    if (capturedQuestSheet != null) {
        floatingFeedback?.rememberLatestSignature(state)
        val nextQuestSheet =
            if (packet.operation == QuestDeltaOperation.REMOVE &&
                capturedQuestSheet.isQuestDetailSheet()
            ) {
                buildQuestListSheetState(nextQuests, loading = false)
            } else if (capturedQuestSheet.isQuestDetailSheet()) {
                capturedQuestSheet.copy(
                    title = currentQuest?.displayName() ?: capturedQuestSheet.title,
                    // 详情页不拼 feedbackLine：标题已有任务名、panelStats 已有进度，
                    // 反馈行前缀重复且其 maxLines 截断会切掉目标/奖励尾部（真机实测）。
                    detailLines =
                        currentQuest?.toQuestWorkbenchDetailLines().orEmpty().let { lines ->
                            if (packet.isInfoRefreshProbe) lines else lines.ifEmpty { capturedQuestSheet.detailLines }
                        },
                    panelStats =
                        currentQuest?.toQuestWorkbenchStats().orEmpty().let { stats ->
                            if (packet.isInfoRefreshProbe) stats else stats.ifEmpty { capturedQuestSheet.panelStats }
                        },
                    actions =
                        buildQuestDetailActionsKeepingNavigation(
                            questId = packet.questId,
                            quest = currentQuest,
                            trackedQuest = currentState.trackedQuest,
                            existingActions = capturedQuestSheet.actions,
                        ),
                    primaryAction =
                        ShellAction(
                            "刷新",
                            "quest info ${packet.questId}",
                            dismissAfterInvoke = false,
                        ),
                    loading = false,
                )
            } else {
                capturedQuestSheet.copy(
                    detailLines = emptyList(),
                    items = buildQuestSheetItems(nextQuests),
                    loading = false,
                )
            }
        sink.updateState {
            withQuestSheet(nextQuestSheet, currentSession)
        }
        clearRelatedQuestSheetRequest(sink)
    } else if (npcInfoRequest != null) {
        if (!openNpcRequestedQuestDetail(
                sink,
                packet,
                npcInfoRequest,
                currentState,
                currentSession,
            )
        ) {
            floatingFeedback?.emitIfChanged(state, sink)
        } else {
            floatingFeedback?.rememberLatestSignature(state)
        }
    }
    if (capturedQuestSheet == null && !capturesNpcQuestMutation && !capturesNpcQuestDetail) {
        floatingFeedback?.emitIfChanged(state, sink)
    } else if (capturesNpcQuestMutation || capturesNpcQuestDetail) {
        floatingFeedback?.rememberLatestSignature(state)
    }
}
