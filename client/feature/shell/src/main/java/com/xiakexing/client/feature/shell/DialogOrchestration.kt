package com.xiakexing.client.feature.shell

import com.google.gson.JsonObject
import com.xiakexing.client.core.protocol.AnsiSanitizer
import com.xiakexing.client.core.protocol.payload.DialogPacket

/**
 * InteractionSession 域 dialog 编排——交互会话状态机
 * （ShellDialogPacketCoordinator 收敛）。判定纯函数在 DialogStateReducer.kt。
 */
internal fun handleDialogPacket(
    capture: InteractionCaptureState,
    sink: InteractionSessionSink,
    payload: JsonObject,
) {
    sink.flushPendingRoomRefresh()
    handleParsedDialogPacket(capture, sink, DialogPacket.fromJson(payload))
}

internal fun handleDialogPacket(
    capture: InteractionCaptureState,
    sink: InteractionSessionSink,
    packet: DialogPacket,
) {
    sink.flushPendingRoomRefresh()
    handleParsedDialogPacket(capture, sink, packet)
}

internal fun handleParsedDialogPacket(
    capture: InteractionCaptureState,
    sink: InteractionSessionSink,
    packet: DialogPacket,
) {
    if (sink.handleNpcLearningDialog(packet)) {
        return
    }
    if (sink.handleInventoryItemDialog(packet)) {
        return
    }
    if (sink.handleMailDialog(packet)) {
        return
    }

    val state = sink.currentState()
    val pending = capture.pendingInteractionRequest
    val currentSheet = state.sheet
    val pendingTarget =
        resolveInteractionTarget(pending, currentSheet, state.interactionObjects)
    if (packet.source == "look_item") {
        if (pending == null && currentSheet?.source != PopupSource.INTERACTION_OBJECT) {
            return
        }
        val expectedTarget = pendingTarget?.commandTarget().orEmpty()
        val packetTarget = packet.target?.id.orEmpty()
        if (expectedTarget.isNotBlank() && packetTarget.isNotBlank() &&
            expectedTarget.routingToken() != packetTarget.routingToken()
        ) {
            return
        }
    }
    val pendingActionGroupId =
        pending?.activeActionGroupId.orEmpty()
            .ifBlank { currentSheet?.activeActionGroupId.orEmpty() }
    val isNpcTaskInfoDialog = packet.isNpcTaskInfoDialog()
    /* 冷到达（无 pending/sheet）的 provider 面板帧直接丢弃——
     * 只按 request token + dialog_id 接收，迟到包不得重开已关表面。 */
    val coldArrivalProviderPanel =
        packet.isProviderPanelDialog() &&
            pending == null &&
            currentSheet == null
    if (coldArrivalProviderPanel) {
        return
    }
    val pendingQuestId =
        pending?.command.orEmpty()
            .substringAfter("quest info ", "")
            .trim()
            .takeIf { it.isNotBlank() }
    val unscopedNpcQuestResponse =
        isNpcTaskInfoDialog &&
            packet.providerId.isBlank() &&
            packet.speakerId.isBlank() &&
            packet.target == null &&
            (
                packet.source == "quest" ||
                    (
                        packet.source.startsWith("quest_info:") &&
                            packet.source.removePrefix("quest_info:") == pendingQuestId
                    )
            )
    val shouldStayInline =
        shouldStayInInteractionSheet(pending, currentSheet) &&
            (packet.belongsToInteractionSession() || unscopedNpcQuestResponse) &&
            (
                packet.matchesInteractionSession(pendingTarget, currentSheet) ||
                    (unscopedNpcQuestResponse && pendingTarget?.kind == InteractionObjectKind.NPC)
            )
    val shouldUseInteractionShell = shouldStayInline
    val inlineTarget = pendingTarget.takeIf { shouldUseInteractionShell }
    val inlineNpcCommandTarget =
        pendingTarget
            ?.takeIf { target -> shouldStayInline && target.kind == InteractionObjectKind.NPC }
            ?.commandTarget()
            .orEmpty()
            .ifBlank {
                resolveVisibleNpcCommandTarget(
                    packet.providerId,
                    state.interactionObjects,
                )
            }
    val hasGiveableItems = state.hasGiveableInventoryItems()
    val packetActionGroups =
        packet.actionGroups
            .toShellActionGroups(inlineNpcCommandTarget)
            .filterUsefulNpcActionGroups(hasGiveableItems)
    val contentLines =
        sanitizeDialogLines(
            packet = packet,
            lines = AnsiSanitizer.sanitize(packet.content).toParagraphs(),
        )
    if (pending?.isNarrativeConversationRequest(packet) == true && pending.keepSheetClosedOnResponse) {
        appendNarrativeDialogToTranscript(
            sink,
            state = state,
            pending = pending,
            currentSheet = currentSheet,
            contentLines = contentLines,
        )
        sink.clearPendingInteractionRequest()
        return
    }
    if (pending == null && isNpcTaskInfoDialog &&
        capture.shouldConsumeSettledNpcQuestDialog(packet.providerId)
    ) {
        return
    }
    if (isNpcTaskInfoDialog &&
        pendingTarget?.kind == InteractionObjectKind.NPC &&
        packet.providerId.isNotBlank() &&
        !packet.matchesInteractionTarget(pendingTarget)
    ) {
        return
    }
    // 游离交互会话对话→主文本区。第六轮真机反馈：provider 面板帧豁免
    // （root「任务」直发+关弹窗后冷到达，须开面板而非倾倒）；
    // npc_provider_progress 维持倾倒（第五轮 R8 进度文本规格）。
    val straySessionDialog =
        pending == null &&
            currentSheet == null &&
            packet.belongsToInteractionSession() &&
            !packet.isProviderPanelDialog() &&
            (
                state.chatPanelOpen ||
                    packet.speakerId.isNotBlank() ||
                    packet.source.contains("interaction", ignoreCase = true)
            )
    if (straySessionDialog) {
        appendDialogLinesToTranscript(sink, state, contentLines)
        return
    }
    if (!(isNpcTaskInfoDialog && shouldUseInteractionShell) &&
        sink.captureQuestDetailDialog(packet)
    ) {
        return
    }

    val contentBelongsToDetails = packet.source == "look_item"
    val shouldReplaceCurrentSheet = !packet.replaceMode.equals("append", ignoreCase = true)
    val nextDialogId = packet.dialogId.ifBlank { currentSheet?.dialogId.orEmpty() }
    val nextDialogSource = packet.source.ifBlank { currentSheet?.dialogSource.orEmpty() }
    val shouldAppendToCurrentDialog =
        !shouldReplaceCurrentSheet &&
            currentSheet != null &&
            currentSheet.dialogId.isNotBlank() &&
            currentSheet.dialogId == nextDialogId &&
            currentSheet.dialogSource == nextDialogSource

    val incomingDetailLines =
        if (isNpcTaskInfoDialog && contentLines.isNotEmpty()) {
            contentLines
        } else if (contentBelongsToDetails && contentLines.isNotEmpty()) {
            contentLines
        } else {
            pending?.descriptionLines.orEmpty()
        }
    val incomingInteractionLines =
        if (isNpcTaskInfoDialog) {
            emptyList()
        } else if (!contentBelongsToDetails && contentLines.isNotEmpty()) {
            contentLines
        } else {
            pending?.interactionLines.orEmpty()
        }

    val resolvedInteractionMode =
        if (shouldUseInteractionShell) {
            currentSheet?.interactionMode ?: InteractionSheetMode.DETAIL
        } else {
            InteractionSheetMode.DETAIL
        }
    val actionGroups =
        when {
            inlineTarget?.kind == InteractionObjectKind.NPC ->
                mergeActionGroups(
                    inlineTarget.actionGroups.filterUsefulNpcActionGroups(hasGiveableItems),
                    packetActionGroups,
                )
            packetActionGroups.isNotEmpty() -> packetActionGroups
            else -> emptyList()
        }
    val nextActiveActionGroupId =
        if (shouldUseInteractionShell && inlineTarget?.kind == InteractionObjectKind.NPC) {
            packet.preferredInteractionActionGroup(
                currentSheet?.activeActionGroupId.orEmpty(),
                actionGroups,
            )
        } else {
            currentSheet?.activeActionGroupId.orEmpty().ifBlank { "root" }
        }
    val presentationTarget =
        pending.presentationTargetForDialog(
            packet,
            shouldUseInteractionShell,
        )
    val shouldKeepNpcConversationPanel =
        shouldUseInteractionShell &&
            inlineTarget?.kind == InteractionObjectKind.NPC &&
            (
                pendingActionGroupId.ifBlank { currentSheet?.activeActionGroupId.orEmpty() } == "conversation" ||
                    currentSheet?.interactionMode == InteractionSheetMode.CONVERSATION
            )
    val keepNpcRootPanel =
        presentationTarget == InteractionPresentationTarget.NarrativeFeed &&
            shouldUseInteractionShell &&
            inlineTarget?.kind == InteractionObjectKind.NPC &&
            !shouldKeepNpcConversationPanel
    val visibleActionGroupId = if (keepNpcRootPanel) "root" else nextActiveActionGroupId
    val actions =
        when {
            inlineTarget?.kind == InteractionObjectKind.NPC ->
                actionGroups.visibleActions(
                    visibleActionGroupId,
                )
            shouldUseInteractionShell -> buildObjectDetailActions(inlineTarget, packetActionGroups)
            else -> packetActionGroups.flatMap { it.actions }
        }.filterUsefulNpcActions(hasGiveableItems)
    if (isNpcTaskInfoDialog &&
        shouldUseInteractionShell &&
        inlineTarget?.kind == InteractionObjectKind.NPC &&
        pending?.command.orEmpty().isNpcQuestProviderQueryCommand() &&
        (contentLines.isNpcTaskEmptyFeedback() || packetActionGroups.noConcreteQuestActions()) &&
        actions.none { action -> action.isConcreteQuestFlowAction() }
    ) {
        capture.settlePendingNpcQuestEmpty()
        sink.updateState {
            withSheet(
                currentSheet?.copy(
                    title = inlineTarget.title.ifBlank { currentSheet.title },
                    detailLines = inlineTarget.description.ifBlank { inlineTarget.summary }.toParagraphs(),
                    interactionLines = emptyList(),
                    actions =
                        actionGroups.visibleActions("root")
                            .ifEmpty { currentSheet.actions }
                            .filterUsefulNpcActions(hasGiveableItems),
                    actionGroups = actionGroups,
                    activeActionGroupId = "root",
                    interactionTargetId = inlineTarget.id,
                    interactionTargetUiKey = inlineTarget.uiKey,
                    interactionTargetKind = inlineTarget.kind,
                    interactionMode = InteractionSheetMode.DETAIL,
                    loading = false,
                    navigation =
                        SheetNavigationMeta(
                            routeKey = "interaction/npc/${inlineTarget.uiKey.ifBlank { inlineTarget.id }}",
                        ),
                ),
            ).withNpcQuestEmptyFeedback(inlineTarget.uiKey.ifBlank { inlineTarget.id })
        }
        sink.clearPendingInteractionRequest()
        return
    }
    if (presentationTarget == InteractionPresentationTarget.NarrativeFeed) {
        appendNarrativeDialogToTranscript(
            sink,
            state = state,
            pending = pending,
            currentSheet = currentSheet,
            contentLines = contentLines,
        )
        sink.updateState {
            withSheet(
                currentSheet?.copy(
                    title = packet.title.ifBlank { inlineTarget?.title ?: currentSheet.title },
                    dialogId = nextDialogId,
                    dialogSource = nextDialogSource,
                    interactionLines = emptyList(),
                    actions = actions,
                    actionGroups = actionGroups,
                    activeActionGroupId = visibleActionGroupId,
                    interactionTargetId = inlineTarget?.id.orEmpty().ifBlank { currentSheet.interactionTargetId },
                    interactionTargetUiKey =
                        inlineTarget?.uiKey.orEmpty().ifBlank {
                            currentSheet.interactionTargetUiKey
                        },
                    interactionTargetKind = inlineTarget?.kind ?: currentSheet.interactionTargetKind,
                    interactionMode = if (keepNpcRootPanel) InteractionSheetMode.DETAIL else InteractionSheetMode.CONVERSATION,
                    loading = false,
                ),
            )
        }
        settleInteractionRequestAfterDialog(sink, packet, pending, shouldUseInteractionShell)
        return
    }
    val shouldPreserveInlineLines = shouldUseInteractionShell && contentLines.isEmpty() && packetActionGroups.isNotEmpty()
    val mergedDetailLines =
        when {
            shouldAppendToCurrentDialog ->
                mergeInteractionLines(
                    currentSheet?.detailLines.orEmpty(),
                    incomingDetailLines,
                )
            shouldPreserveInlineLines && incomingDetailLines.isEmpty() -> currentSheet?.detailLines.orEmpty()
            else -> incomingDetailLines
        }
    val mergedInteractionLines =
        when {
            shouldAppendToCurrentDialog ->
                mergeInteractionLines(
                    currentSheet?.interactionLines.orEmpty(),
                    incomingInteractionLines,
                )
            shouldPreserveInlineLines && incomingInteractionLines.isEmpty() -> currentSheet?.interactionLines.orEmpty()
            else -> incomingInteractionLines
        }
    if (contentBelongsToDetails &&
        shouldUseInteractionShell &&
        inlineTarget != null &&
        inlineTarget.kind != InteractionObjectKind.NPC
    ) {
        val detailActions =
            buildObjectDetailActions(
                target = inlineTarget,
                packetActionGroups = packetActionGroups,
            )
        sink.updateState {
            copy(
                sheet =
                    buildInteractionProtocolSheetState(
                        source = PopupSource.INTERACTION_OBJECT,
                        title = packet.title.ifBlank { inlineTarget.title },
                        dialogId = nextDialogId,
                        dialogSource = nextDialogSource,
                        detailLines = mergedDetailLines,
                        interactionLines = emptyList(),
                        actions = detailActions,
                        actionGroups = emptyList(),
                        activeActionGroupId = "root",
                        interactionTargetId = inlineTarget.id,
                        interactionTargetUiKey = inlineTarget.uiKey,
                        interactionTargetKind = inlineTarget.kind,
                        interactionMode = InteractionSheetMode.DETAIL,
                        compactPanel = true,
                    ),
                dialog = null,
            )
        }
        settleInteractionRequestAfterDialog(sink, packet, pending, shouldUseInteractionShell)
        return
    }
    if (isNpcTaskInfoDialog &&
        shouldUseInteractionShell &&
        inlineTarget?.kind == InteractionObjectKind.NPC
    ) {
        val pendingCommand = pending?.command.orEmpty()
        val normalizedPendingCommand = pendingCommand.trim().lowercase()
        val isNpcQuestMutation = normalizedPendingCommand.isNpcQuestMutationCommand()
        val shouldRemainOnNpcQuestDetail =
            isNpcQuestMutation &&
                currentSheet?.isNpcQuestInteractionSheet() == true
        val isNpcQuestDetail = pendingCommand.isQuestInfoCommand() || shouldRemainOnNpcQuestDetail
        val npcQuestTitle =
            if (isNpcQuestDetail) {
                resolveNpcQuestDetailTitle(
                    sink = sink,
                    command = pendingCommand,
                    currentSheet = currentSheet,
                    packet = packet,
                )
            } else {
                "任务"
            }
        val npcQuestDetailLines =
            if (isNpcQuestMutation) {
                mergeInteractionLines(
                    currentSheet?.detailLines.orEmpty(),
                    mergedDetailLines,
                )
            } else {
                mergedDetailLines
            }
        // 任务面板动作范围（含对话动作剔除）收在 buildNpcQuestInteractionSheetState，
        // 此处只做「当前操作命令」自去重。
        val npcQuestActions =
            if (isNpcQuestMutation) {
                actions.filterNot { action ->
                    action.command.trim().equals(pendingCommand.trim(), ignoreCase = true)
                }
            } else {
                actions
            }
        sink.updateState {
            withSheet(
                buildNpcQuestInteractionSheetState(
                    title = npcQuestTitle,
                    dialogId = nextDialogId,
                    dialogSource = "quest",
                    detailLines = npcQuestDetailLines,
                    actions = npcQuestActions,
                    actionGroups = actionGroups,
                    interactionTargetId = inlineTarget.id,
                    interactionTargetUiKey = inlineTarget.uiKey,
                    interactionTargetKind = inlineTarget.kind,
                    detailMode = isNpcQuestDetail,
                ),
            )
        }
        settleInteractionRequestAfterDialog(sink, packet, pending, shouldUseInteractionShell)
        return
    }

    sink.updateState {
        copy(
            sheet =
                buildInteractionProtocolSheetState(
                    source = if (shouldUseInteractionShell) PopupSource.INTERACTION_OBJECT else PopupSource.PROTOCOL,
                    title = packet.title.ifBlank { inlineTarget?.title ?: "交互" },
                    dialogId = nextDialogId,
                    dialogSource = nextDialogSource,
                    detailLines = mergedDetailLines,
                    interactionLines = mergedInteractionLines,
                    actions = actions,
                    actionGroups = actionGroups,
                    activeActionGroupId = nextActiveActionGroupId,
                    interactionTargetId = inlineTarget?.id.orEmpty(),
                    interactionTargetUiKey = inlineTarget?.uiKey.orEmpty(),
                    interactionTargetKind = inlineTarget?.kind,
                    interactionMode = resolvedInteractionMode,
                ),
            dialog = null,
        )
    }
    settleInteractionRequestAfterDialog(sink, packet, pending, shouldUseInteractionShell)
}

private fun resolveVisibleNpcCommandTarget(
    providerId: String,
    interactionObjects: List<InteractionObject>,
): String {
    val providerToken = providerId.npcCommandToken()
    if (providerToken.isBlank()) {
        return ""
    }
    return interactionObjects
        .firstOrNull { target ->
            target.kind == InteractionObjectKind.NPC &&
                (
                    target.id.npcCommandToken() == providerToken ||
                        target.commandTarget().npcCommandToken() == providerToken
                )
        }
        ?.commandTarget()
        .orEmpty()
}

private fun appendDialogLinesToTranscript(
    sink: InteractionSessionSink,
    state: ShellUiState,
    contentLines: List<String>,
) {
    val currentPlayerName =
        state.statusSnapshot?.name
            ?.ifBlank { state.username }
            ?: state.username.ifBlank { "你" }
    val block =
        formatNpcDialogueBlock(contentLines.map { it.toCompactSayLine(currentPlayerName) ?: it })
    if (block.isNotBlank()) sink.appendNarrativeTranscript(block)
}

private fun appendNarrativeDialogToTranscript(
    sink: InteractionSessionSink,
    state: ShellUiState,
    pending: PendingInteractionRequest?,
    currentSheet: ShellSheetState?,
    contentLines: List<String>,
) {
    val activePending = pending ?: return
    val currentPlayerName =
        state.statusSnapshot?.name
            ?.ifBlank { state.username }
            ?: state.username.ifBlank { "你" }
    val dialogueLines = mutableListOf<String>()
    if (!activePending.narrativeLeadLineEmitted && contentLines.isNotEmpty()) {
        val leadLine =
            activePending.command.toNarrativePlayerActionLine(
                activePending.target,
                currentSheet,
            )
        if (leadLine.isNotBlank()) dialogueLines += leadLine
        activePending.narrativeLeadLineEmitted = true
    }
    activePending.markNarrativeLinesSeen(contentLines)
    dialogueLines += contentLines.map { it.toCompactSayLine(currentPlayerName) ?: it }
    val block = formatNpcDialogueBlock(dialogueLines)
    if (block.isNotBlank()) sink.appendNarrativeTranscript(block)
}

private fun settleInteractionRequestAfterDialog(
    sink: InteractionSessionSink,
    packet: DialogPacket,
    pending: PendingInteractionRequest?,
    shouldUseInteractionShell: Boolean,
) {
    if (shouldUseInteractionShell) {
        if (packet.isFinal) {
            sink.cancelInteractionRequestSettle()
            if (pending?.command.orEmpty().isNpcQuestMutationCommand()) {
                sink.clearPendingInteractionRequest()
            } else if (pending?.keepCollectingUntilPrompt == true) {
                sink.clearInteractionRequestSettleJob()
            } else {
                sink.clearPendingInteractionRequest()
            }
        } else {
            sink.cancelInteractionRequestSettle()
        }
    } else {
        sink.clearPendingInteractionRequest()
    }
}

private fun resolveNpcQuestDetailTitle(
    sink: InteractionSessionSink,
    command: String,
    currentSheet: ShellSheetState?,
    packet: DialogPacket,
): String {
    val normalizedCommand = command.trim()
    val questId = normalizedCommand.questCommandId()
    val listTitle =
        currentSheet?.items
            ?.firstOrNull { item ->
                item.command.trim().equals(normalizedCommand, ignoreCase = true) ||
                    item.actionCommand.trim().equals(normalizedCommand, ignoreCase = true)
            }
            ?.title
            .orEmpty()
    val cachedTitle =
        questId
            .takeIf { it.isNotBlank() }
            ?.let(sink::latestQuestEntryName)
            .orEmpty()
    val packetTitle =
        packet.title
            .takeUnless { it.isGenericQuestDetailTitle() }
            .orEmpty()
    return listTitle
        .ifBlank { cachedTitle }
        .ifBlank { packetTitle }
        .ifBlank { questId }
        .ifBlank { "任务" }
}
