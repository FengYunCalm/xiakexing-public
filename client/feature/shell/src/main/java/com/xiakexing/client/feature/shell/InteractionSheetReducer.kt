package com.xiakexing.client.feature.shell

/**
 * InteractionSession 域 sheet 纯逻辑——对象动作组构建与
 * 实时目标刷新（ShellInteractionSheetCoordinator 收敛）。
 */

internal fun InteractionObject.objectBaseActions(): List<ShellAction> {
    return rootActionsOrFallback().filterNot {
        it.isLookOrViewAction()
    }.map { it.withRoomItemTarget(this) }
}

internal fun InteractionObject.objectRootActionGroups(
    fallbackActions: List<ShellAction>,
): List<ShellActionGroup> {
    val fallbackRootGroup =
        fallbackActions
            .takeIf { actions -> actions.isNotEmpty() }
            ?.let { actions -> ShellActionGroup("root", "基础操作", "root", actions) }

    if (actionGroups.isEmpty()) {
        return fallbackRootGroup?.let(::listOf).orEmpty()
    }

    val protocolRootGroups = actionGroups.filter { group -> group.isRootObjectActionGroup() }
    val protocolRootActions = protocolRootGroups.flatMap { group -> group.actions }
    // 场景物品不需要"查看"按钮：协议下发的 look/view 动作直接丢弃，不再合并回根动作。
    val getFallbackActions = fallbackActions.filter { action -> action.isGetAction() }
    val protocolBusinessActions =
        protocolRootActions
            .filterNot { action -> action.isLookOrViewAction() }
            .filterNot { action -> getFallbackActions.isNotEmpty() && action.isGetAction() }
    val mergedRootActions =
        mergeSheetActions(
            protocolBusinessActions,
            getFallbackActions,
        )
    val rootGroup =
        mergedRootActions
            .takeIf { actions -> actions.isNotEmpty() }
            ?.let { actions ->
                val protocolRoot = protocolRootGroups.firstOrNull()
                ShellActionGroup(
                    id = protocolRoot?.id ?: "root",
                    title = protocolRoot?.title ?: fallbackRootGroup?.title ?: "基础操作",
                    surface = protocolRoot?.surface?.ifBlank { "root" } ?: "root",
                    actions = actions,
                )
            }
    val secondaryGroups = actionGroups.filterNot { group -> group.isRootObjectActionGroup() }
    return listOfNotNull(rootGroup) + secondaryGroups
}

internal fun List<ShellActionGroup>.visibleObjectRootActions(): List<ShellAction> {
    return visibleActions("root").ifEmpty { firstOrNull()?.actions.orEmpty() }
}

internal fun ShellActionGroup.isRootObjectActionGroup(): Boolean {
    return id == "root" || surface == "root"
}

internal fun ShellSheetState.refreshInteractionTarget(
    target: InteractionObject,
    hasGiveableItems: Boolean,
): ShellSheetState {
    val activeGroup = activeActionGroupId.ifBlank { "root" }
    val groups =
        if (target.kind == InteractionObjectKind.NPC) {
            val conversationGroups =
                if (interactionMode == InteractionSheetMode.CONVERSATION && dialogId.isNotBlank()) {
                    actionGroups.filter { it.id == activeGroup || it.surface == activeGroup }
                        .filterNot { it.isRootObjectActionGroup() }
                        .map {
                                group ->
                            group.copy(actions = group.actions.filterNot { it.isStepGuidance })
                        }
                } else {
                    emptyList()
                }
            mergeActionGroups(
                target.actionGroups,
                conversationGroups,
            ).filterUsefulNpcActionGroups(hasGiveableItems)
        } else {
            target.objectRootActionGroups(target.objectBaseActions())
        }
    val latestActions =
        if (target.kind == InteractionObjectKind.NPC) {
            groups.visibleActions(activeGroup)
        } else {
            buildObjectDetailActions(target, groups)
        }
    return copy(
        title = if (activeGroup == "root") target.title else title,
        subtitle = if (activeGroup == "root") target.subtitle else subtitle,
        detailLines =
            if (interactionMode == InteractionSheetMode.DETAIL && activeGroup == "root") {
                target.description.ifBlank { target.summary }.toParagraphs()
            } else {
                detailLines
            },
        actions = latestActions,
        actionGroups = groups,
        activeActionGroupId = activeGroup,
        interactionTargetUiKey = target.uiKey,
    )
}
