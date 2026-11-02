package com.xiakexing.client.feature.shell

/**
 * InteractionSession 域 sheet 编排——对象弹窗呈现、给予面板与
 * 交互命令驻留判定（ShellInteractionSheetCoordinator 收敛）。
 * 纯变换在 InteractionSheetReducer.kt。
 */
internal fun showInteractionObjectPopup(
    capture: InteractionCaptureState,
    sink: InteractionSessionSink,
    objectId: String,
) {
    capture.clearPendingInteractionRequest()
    val state = sink.currentState()
    val target = state.interactionObjects.findInteractionObjectByKey(objectId) ?: return
    val hasGiveableItems = state.hasGiveableInventoryItems()
    val fallbackActions =
        when (target.kind) {
            InteractionObjectKind.NPC ->
                target.rootActionsOrFallback().filterUsefulNpcActions(
                    hasGiveableItems,
                )
            else -> target.objectBaseActions()
        }
    val actionGroups =
        when (target.kind) {
            InteractionObjectKind.NPC ->
                target.actionGroups.ifEmpty {
                    if (fallbackActions.isEmpty()) {
                        emptyList()
                    } else {
                        listOf(
                            ShellActionGroup("root", "主要操作", "root", fallbackActions),
                        )
                    }
                }.filterUsefulNpcActionGroups(hasGiveableItems)
            else -> target.objectRootActionGroups(fallbackActions)
        }
    val rootActions =
        if (target.kind == InteractionObjectKind.NPC) {
            fallbackActions
        } else {
            actionGroups.visibleObjectRootActions()
        }
    val initialDetailLines = target.description.ifBlank { target.summary }.toParagraphs()

    sink.updateState {
        withSheet(
            buildInteractionObjectSheetState(
                target = target,
                detailLines = initialDetailLines,
                actions = rootActions,
                actionGroups = actionGroups,
            ),
        )
    }
}

internal fun openInteractionGiveItemsSheet(
    capture: InteractionCaptureState,
    sink: InteractionSessionSink,
) {
    val state = sink.currentState()
    val currentSheet = state.sheet ?: return
    if (currentSheet.source != PopupSource.INTERACTION_OBJECT) {
        return
    }
    // 两阶段渲染根除：背包刷新中不再拦截提示，直接打开面板（数据到达后原位刷新）。
    if (!state.hasGiveableInventoryItems()) {
        capture.clearPendingInteractionRequest()
        sink.appendInteractionFeedback("背包里没有可给予的物品")
        return
    }

    val target =
        resolveInteractionTarget(
            capture.pendingInteractionRequest,
            currentSheet,
            state.interactionObjects,
        ) ?: return
    capture.clearPendingInteractionRequest()
    sink.updateState {
        copy(
            sheet =
                buildGiveSheetState(target, target.commandTarget(), inventoryItems).copy(
                    navigation =
                        SheetNavigationMeta(
                            routeKey = "interaction/${target.kind.name.lowercase()}/${target.uiKey.ifBlank { target.id }}/give",
                            depth = 1,
                        ),
                ),
        )
    }
}

internal fun returnToInteractionRoot(
    capture: InteractionCaptureState,
    sink: InteractionSessionSink,
) {
    if (sink.popSheet()) {
        return
    }
    val state = sink.currentState()
    val currentSheet = state.sheet ?: return
    val target =
        resolveInteractionTarget(
            capture.pendingInteractionRequest,
            currentSheet,
            state.interactionObjects,
        )?.let { resolved ->
            state.interactionObjects.findInteractionObjectByKey(resolved.uiKey) ?: resolved
        } ?: return
    val hasGiveableItems = state.hasGiveableInventoryItems()
    val rootActions = target.rootActionsOrFallback().filterUsefulNpcActions(hasGiveableItems)
    val rootActionGroups =
        target.actionGroups
            .filterUsefulNpcActionGroups(hasGiveableItems)
            .ifEmpty {
                if (rootActions.isEmpty()) {
                    emptyList()
                } else {
                    listOf(
                        ShellActionGroup("root", "主要操作", "root", rootActions),
                    )
                }
            }
    capture.clearPendingInteractionRequest()
    sink.updateState {
        copy(
            sheet =
                currentSheet.copy(
                    title = target.title,
                    detailLines = target.description.ifBlank { target.summary }.toParagraphs(),
                    interactionLines = emptyList(),
                    actions = if (target.kind == InteractionObjectKind.NPC) rootActions else currentSheet.actions,
                    actionGroups =
                        if (target.kind == InteractionObjectKind.NPC) {
                            rootActionGroups
                        } else {
                            currentSheet.actionGroups
                        },
                    activeActionGroupId = "root",
                    interactionTargetId = target.id,
                    interactionTargetUiKey = target.uiKey,
                    interactionTargetKind = target.kind,
                    interactionMode = InteractionSheetMode.DETAIL,
                    loading = false,
                ),
        )
    }
}

internal fun returnToInteractionObjectRoot(
    capture: InteractionCaptureState,
    sink: InteractionSessionSink,
) {
    if (sink.popSheet()) {
        return
    }
    val state = sink.currentState()
    val currentSheet = state.sheet ?: return
    val target =
        resolveInteractionTarget(
            capture.pendingInteractionRequest,
            currentSheet,
            state.interactionObjects,
        )?.let { resolved ->
            state.interactionObjects.findInteractionObjectByKey(resolved.uiKey)
        }
    if (target == null || target.kind == InteractionObjectKind.NPC) {
        capture.clearPendingInteractionRequest()
        sink.appendInteractionFeedback("这个物品已不在这里")
        sink.updateState { withSheet(null) }
        return
    }

    val fallbackActions = target.objectBaseActions()
    val actionGroups = target.objectRootActionGroups(fallbackActions)
    capture.clearPendingInteractionRequest()
    sink.updateState {
        withSheet(
            buildInteractionObjectSheetState(
                target = target,
                detailLines = target.description.ifBlank { target.summary }.toParagraphs(),
                actions = actionGroups.visibleObjectRootActions(),
                actionGroups = actionGroups,
            ),
        )
    }
}

internal fun preserveInteractionSessionForDraft(
    capture: InteractionCaptureState,
    sink: InteractionSessionSink,
    currentSheet: ShellSheetState,
) {
    val state = sink.currentState()
    val target =
        resolveInteractionTarget(
            capture.pendingInteractionRequest,
            currentSheet,
            state.interactionObjects,
        ) ?: return
    val hasGiveableItems = state.hasGiveableInventoryItems()
    sink.replacePendingInteractionRequest(
        buildPendingInteractionRequest(
            target = target,
            fallbackActions =
                currentSheet.actions
                    .ifEmpty { target.rootActionsOrFallback() }
                    .filterUsefulNpcActions(hasGiveableItems),
            detailLines = currentSheet.detailLines,
            interactionLines = currentSheet.interactionLines,
            activeActionGroupId = currentSheet.activeActionGroupId,
        ),
    )
}

internal fun prepareInteractionSheetCommand(
    sink: InteractionSessionSink,
    command: String,
    requestedGroupId: String = "",
): Boolean {
    val state = sink.currentState()
    val currentSheet = state.sheet ?: return false
    if (currentSheet.source != PopupSource.INTERACTION_OBJECT || !command.shouldRemainInInteractionSession()) {
        return false
    }

    val normalizedCommand = command.trim().lowercase()
    val isConversationCommand = normalizedCommand.startsWith("talk ") || normalizedCommand.startsWith("ask ")
    val isConcreteConversationTopic =
        normalizedCommand.startsWith("ask ") ||
            (normalizedCommand.startsWith("talk ") && normalizedCommand.contains(" about "))
    val isCombatStartCommand =
        normalizedCommand == "fight" || normalizedCommand.startsWith("fight ") ||
            normalizedCommand == "kill" || normalizedCommand.startsWith("kill ")
    val isNonNpcQuietCommand =
        (normalizedCommand.isLookInteractionCommand() || normalizedCommand.startsWith("get ")) &&
            currentSheet.interactionTargetKind != InteractionObjectKind.NPC
    if (currentSheet.interactionTargetKind == InteractionObjectKind.NPC && normalizedCommand.isNpcLearnListCommand()) {
        return false
    }
    if (normalizedCommand.isQuestAcceptCommand()) {
        // #9 接取反馈直达主文本区：服务端已把接取响应降级为纯文本，
        // 不再进入 NPC 交互捕获会话（否则反馈行会被吸进弹窗展示）。
        return false
    }
    if (currentSheet.interactionTargetKind == InteractionObjectKind.NPC &&
        normalizedCommand.startsWith("quest") &&
        !normalizedCommand.isQuestInfoCommand()
    ) {
        val isQuestMutation = normalizedCommand.isNpcQuestMutationCommand()
        val hasMatchingPendingAction =
            isQuestMutation && (
                currentSheet.items.any { item ->
                    item.questActionPendingCommand.equals(normalizedCommand, ignoreCase = true)
                } ||
                    currentSheet.actions.any { action ->
                        action.kind == "quest_pending" &&
                            action.command.trim().equals(normalizedCommand, ignoreCase = true)
                    }
            )
        if (hasMatchingPendingAction) {
            return true
        }
        val targetId =
            currentSheet.interactionTargetId.ifBlank {
                return false
            }
        val target =
            state.interactionObjects.findInteractionObjectByKey(
                currentSheet.interactionTargetUiKey.ifBlank {
                    targetId
                },
            )
                ?: InteractionObject(
                    id = targetId,
                    uiKey = currentSheet.interactionTargetUiKey.ifBlank { targetId },
                    title = currentSheet.title,
                    subtitle = "",
                    summary = currentSheet.detailLines.firstOrNull().orEmpty(),
                    kind = InteractionObjectKind.NPC,
                )
        sink.replacePendingInteractionRequest(
            buildPendingInteractionRequest(
                target = target,
                fallbackActions = currentSheet.actions,
                command = command,
                detailLines = currentSheet.detailLines,
                interactionLines = currentSheet.interactionLines,
                activeActionGroupId = currentSheet.activeActionGroupId.ifBlank { "root" },
                keepCollectingUntilPrompt = true,
                keepSheetClosedOnResponse = false,
            ),
        )
        sink.updateState {
            withSheet(
                currentSheet.copy(
                    items =
                        if (isQuestMutation) {
                            currentSheet.items.map { item ->
                                if (item.actionCommand.trim().equals(
                                        normalizedCommand,
                                        ignoreCase = true,
                                    )
                                ) {
                                    item.copy(
                                        trailing = normalizedCommand.npcQuestActionLabel(),
                                        metadata =
                                            item.metadata +
                                                mapOf(
                                                    SheetItemMetadata.QuestActionPending to "1",
                                                    SheetItemMetadata.QuestActionPendingCommand to normalizedCommand,
                                                ),
                                    )
                                } else {
                                    item
                                }
                            }
                        } else {
                            currentSheet.items
                        },
                    actions =
                        if (isQuestMutation) {
                            currentSheet.actions.map { action ->
                                if (action.command.trim().equals(
                                        normalizedCommand,
                                        ignoreCase = true,
                                    )
                                ) {
                                    action.copy(
                                        label = normalizedCommand.npcQuestActionLabel(),
                                        kind = "quest_pending",
                                    )
                                } else {
                                    action
                                }
                            }
                        } else {
                            currentSheet.actions
                        },
                    interactionMode = InteractionSheetMode.DETAIL,
                    activeActionGroupId = currentSheet.activeActionGroupId.ifBlank { "root" },
                    loading = false,
                    errorMessage = "",
                    retryAction = null,
                ),
            )
        }
        return true
    }
    val nextActionGroupId =
        requestedGroupId.ifBlank {
            normalizedCommand.toInteractionActionGroupId(currentSheet.activeActionGroupId)
        }
    if (isConversationCommand) {
        val targetId =
            currentSheet.interactionTargetId.ifBlank {
                return false
            }
        val target =
            state.interactionObjects.findInteractionObjectByKey(
                currentSheet.interactionTargetUiKey.ifBlank {
                    targetId
                },
            )
                ?: InteractionObject(
                    id = targetId,
                    uiKey = currentSheet.interactionTargetUiKey.ifBlank { targetId },
                    title = currentSheet.title,
                    subtitle = "",
                    summary = currentSheet.detailLines.firstOrNull().orEmpty(),
                    kind = currentSheet.interactionTargetKind ?: InteractionObjectKind.NPC,
                )
        sink.replacePendingInteractionRequest(
            buildPendingInteractionRequest(
                target = target,
                fallbackActions = currentSheet.actions,
                command = command,
                detailLines = currentSheet.detailLines,
                interactionLines = emptyList(),
                activeActionGroupId = "conversation",
                keepCollectingUntilPrompt = true,
                keepSheetClosedOnResponse = isConcreteConversationTopic,
            ),
        )
        sink.updateState {
            withSheet(
                if (isConcreteConversationTopic) {
                    null
                } else {
                    currentSheet.copy(
                        interactionLines = emptyList(),
                        interactionMode = InteractionSheetMode.CONVERSATION,
                        activeActionGroupId = "conversation",
                        loading = false,
                    )
                },
            )
        }
        return true
    }
    val loadingActionGroupId =
        if (isConversationCommand) {
            currentSheet.activeActionGroupId
        } else {
            nextActionGroupId
        }
    val nextInteractionMode =
        if (isConversationCommand) {
            InteractionSheetMode.CONVERSATION
        } else if (normalizedCommand.startsWith("quest")) {
            InteractionSheetMode.DETAIL
        } else if (normalizedCommand.startsWith("give ")) {
            InteractionSheetMode.DETAIL
        } else {
            currentSheet.interactionMode
        }

    if (!command.shouldStayInInteractionSheet()) {
        sink.updateState {
            copy(
                sheet =
                    currentSheet.copy(
                        items = if (nextInteractionMode == InteractionSheetMode.GIVE_ITEMS) currentSheet.items else emptyList(),
                        interactionMode = nextInteractionMode,
                        activeActionGroupId = loadingActionGroupId,
                        loading = true,
                    ),
            )
        }
        return true
    }

    val targetId =
        currentSheet.interactionTargetId.ifBlank {
            return false
        }
    val target =
        state.interactionObjects.findInteractionObjectByKey(
            currentSheet.interactionTargetUiKey.ifBlank {
                targetId
            },
        )
            ?: InteractionObject(
                id = targetId,
                uiKey = currentSheet.interactionTargetUiKey.ifBlank { targetId },
                title = currentSheet.title,
                subtitle = "",
                summary = currentSheet.detailLines.firstOrNull().orEmpty(),
                kind = currentSheet.interactionTargetKind ?: InteractionObjectKind.SCENE,
            )
    val preservedInteractionLines =
        if (normalizedCommand.isLookInteractionCommand()) {
            emptyList()
        } else if (isConversationCommand) {
            emptyList()
        } else {
            currentSheet.interactionLines
        }

    val keepCollectingUntilPrompt =
        normalizedCommand.isLookInteractionCommand() ||
            normalizedCommand.startsWith("get ") ||
            normalizedCommand.startsWith("quest action ") ||
            isCombatStartCommand ||
            (
                target.kind == InteractionObjectKind.NPC && (
                    normalizedCommand.startsWith("talk ") ||
                        normalizedCommand.startsWith("ask ") ||
                        normalizedCommand.startsWith("give ") ||
                        normalizedCommand.startsWith("quest")
                )
            )

    sink.replacePendingInteractionRequest(
        buildPendingInteractionRequest(
            target = target,
            fallbackActions = currentSheet.actions,
            command = command,
            detailLines = currentSheet.detailLines,
            interactionLines = preservedInteractionLines,
            activeActionGroupId = loadingActionGroupId,
            keepCollectingUntilPrompt = keepCollectingUntilPrompt,
        ),
    )
    if (isNonNpcQuietCommand) {
        sink.updateState {
            withSheet(
                currentSheet.copy(
                    interactionLines = emptyList(),
                    loading = normalizedCommand.startsWith("get "),
                ),
            )
        }
        return true
    }
    sink.updateState {
        copy(
            sheet =
                currentSheet.copy(
                    items = if (nextInteractionMode == InteractionSheetMode.GIVE_ITEMS) currentSheet.items else emptyList(),
                    interactionLines = preservedInteractionLines,
                    interactionMode = nextInteractionMode,
                    activeActionGroupId = loadingActionGroupId,
                    loading = !isCombatStartCommand,
                ),
        )
    }
    return true
}
