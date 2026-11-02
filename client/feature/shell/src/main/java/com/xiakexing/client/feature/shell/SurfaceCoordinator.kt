package com.xiakexing.client.feature.shell

/**
 * Surface 域 owner——表面导航、sheet 导航栈与全表面动作入口。
 * Action/Dismissal 编排在 SurfaceActionOrchestration.kt /
 * SurfaceDismissalOrchestration.kt（sink 参数顶层函数）。
 */
internal class SurfaceCoordinator(
    private val sink: SurfaceSink,
) {
    // ===== Navigation（原 ShellNavigationSurfaceCoordinator） =====

    fun dismissCommandPaletteIfShowing() {
        sink.updateState { dismissCommandPalette() }
    }

    fun dismissExitOverlayIfShowing() {
        sink.updateState {
            if (exitOverlay == null) this else copy(exitOverlay = null)
        }
    }

    fun showCommandPalette() {
        sink.clearPendingInteractionRequest()
        sink.updateState { showCommandPaletteState() }
    }

    fun showDockPopup(button: DockButtonType) {
        sink.clearPendingInteractionRequest()
        if (button != DockButtonType.INPUT) {
            sink.closeChatPanel()
        }
        when (button) {
            DockButtonType.CHARACTER -> sink.showCharacterHubPanel()
            DockButtonType.ACTION -> sink.showActionHubPanel()
            DockButtonType.WORLD -> sink.showWorldHubPanel()
            DockButtonType.MAP -> sink.showMapPanel()
            DockButtonType.INPUT -> showCommandPalette()
            DockButtonType.QUEST -> sink.requestQuestListFromDock()
        }
    }

    fun showCommonExitOverlay(buttons: List<ShellAction>) {
        showExitOverlay(ExitOverlayType.COMMON, buttons)
    }

    fun showExitOverlay(
        type: ExitOverlayType,
        buttons: List<ShellAction>,
    ) {
        sink.updateState {
            copy(
                sheet = null,
                dialog = null,
                commandPalette = null,
                exitOverlay = ExitOverlayState(type = type, buttons = buttons),
            )
        }
    }

    fun showTitleActionPopup(action: TitleActionType) {
        sink.closeChatPanel()
        when (action) {
            TitleActionType.MESSAGE -> sink.showMessagePanel()
            TitleActionType.SYSTEM -> showCommandPalette()
        }
    }

    // ===== Sheet 导航栈（原 ShellSheetNavigationCoordinator） =====

    fun reset(
        sheet: ShellSheetState,
        questPanelSession: QuestPanelSessionState = QuestPanelSessionState(),
    ) {
        navigate(SheetNavigationOperation.RESET, sheet, questPanelSession)
    }

    fun push(
        sheet: ShellSheetState,
        questPanelSession: QuestPanelSessionState = sink.currentState().questPanelSession,
    ) {
        navigate(SheetNavigationOperation.PUSH, sheet, questPanelSession)
    }

    fun replace(
        sheet: ShellSheetState,
        questPanelSession: QuestPanelSessionState = sink.currentState().questPanelSession,
    ) {
        navigate(SheetNavigationOperation.REPLACE, sheet, questPanelSession)
    }

    fun pop(): Boolean {
        val current = sink.currentState()
        val previous = current.sheetNavigation.backStack.lastOrNull() ?: return false
        val remaining = current.sheetNavigation.backStack.dropLast(1)
        sink.updateState {
            withQuestSheet(
                previous.sheet.withNavigationDepth(remaining.size),
                previous.questPanelSession,
            ).copy(
                sheetNavigation = SheetNavigationState(remaining),
                dialog = null,
                commandPalette = null,
                exitOverlay = null,
            )
        }
        return true
    }

    fun clear() {
        sink.updateState {
            withQuestSheet(null).copy(sheetNavigation = SheetNavigationState())
        }
    }

    private fun navigate(
        operation: SheetNavigationOperation,
        destination: ShellSheetState,
        questPanelSession: QuestPanelSessionState,
    ) {
        sink.updateState {
            val nextBackStack =
                when (operation) {
                    SheetNavigationOperation.RESET -> emptyList()
                    SheetNavigationOperation.PUSH -> {
                        val currentSheet = sheet
                        if (currentSheet == null) {
                            emptyList()
                        } else {
                            (sheetNavigation.backStack + SheetNavigationEntry(currentSheet, this.questPanelSession))
                                .takeLast(MAX_SHEET_NAVIGATION_DEPTH)
                        }
                    }
                    SheetNavigationOperation.REPLACE -> sheetNavigation.backStack
                    SheetNavigationOperation.POP -> error("Use pop() for POP navigation")
                }
            withQuestSheet(
                destination.withNavigationDepth(nextBackStack.size),
                questPanelSession,
            ).copy(
                sheetNavigation = SheetNavigationState(nextBackStack),
                dialog = null,
                commandPalette = null,
                exitOverlay = null,
            )
        }
    }

    // ===== 动作确认状态（原 ShellSurfaceDismissalCoordinator 字段） =====

    private var dismissSheetOnPopupAction = false

    fun consumeDismissSheetOnPopupAction(): Boolean {
        val shouldDismiss = dismissSheetOnPopupAction
        dismissSheetOnPopupAction = false
        return shouldDismiss
    }

    fun clearDismissSheetOnPopupAction() {
        dismissSheetOnPopupAction = false
    }

    fun showActionConfirmation(
        title: String,
        action: ShellAction,
        dismissSheetAfterConfirm: Boolean,
    ) {
        // #8 丢弃即关：移除类动作（drop）确认后关闭底层弹窗，规则单源于此。
        dismissSheetOnPopupAction = dismissSheetAfterConfirm || action.isDropRemovalCommand()
        sink.updateState {
            copy(dialog = buildActionConfirmationPopup(title, action).withParentPopup(dialog))
        }
    }

    // ===== Dismissal 编排委托（SurfaceDismissalOrchestration.kt） =====

    fun handleBack(): Boolean = handleBack(sink)

    fun dismissSheet() = dismissSheet(sink)

    fun dismissPopup() = dismissPopup(sink)

    fun dismissPopupGroup() = dismissPopupGroup(sink)

    fun dismissTransientInteractionSurfaces() = dismissTransientInteractionSurfaces(sink)

    fun dismissObscuringSurfacesAroundInteractionSheet() =
        dismissObscuringSurfacesAroundInteractionSheet(
            sink,
        )

    fun dismissObscuringSurfacesForExternalCommand() =
        dismissObscuringSurfacesForExternalCommand(
            sink,
        )

    fun dismissObscuringSurfacesForCombatStart() = dismissObscuringSurfacesForCombatStart(sink)

    fun dismissSheetIfShowing() = dismissSheetIfShowing(sink)

    // ===== Action 编排委托（SurfaceActionOrchestration.kt） =====

    fun handleExitOverlayButtonClick(action: ShellAction) =
        handleExitOverlayButtonClick(
            sink,
            action,
        )

    fun handleSurfaceCommandRoute(route: ShellSurfaceCommandRoute) =
        handleSurfaceCommandRoute(
            sink,
            route,
        )

    fun handlePopupAction(action: ShellAction) = handlePopupAction(sink, action)

    fun handleSheetItem(item: ShellSheetItem) = handleSheetItem(sink, item)

    fun handleSheetPrimaryAction(action: ShellAction) = handleSheetPrimaryAction(sink, action)
}

internal fun ShellSheetState.withNavigationDepth(depth: Int): ShellSheetState {
    val normalizedDepth = depth.coerceAtLeast(0)
    return if (navigation.depth == normalizedDepth) {
        this
    } else {
        copy(navigation = navigation.copy(depth = normalizedDepth))
    }
}

private const val MAX_SHEET_NAVIGATION_DEPTH = 8

/**
 * Interprets the destination's declared depth once, then stores the actual depth
 * from the bounded history. Packet refreshes of the same page therefore replace
 * content without adding another history entry.
 */
internal fun reconcileSheetNavigation(
    previous: ShellUiState,
    transformed: ShellUiState,
): ShellUiState {
    if (transformed.sheetNavigation != previous.sheetNavigation) {
        return transformed.copy(
            sheet =
                transformed.sheet?.withNavigationDepth(
                    transformed.sheetNavigation.backStack.size,
                ),
        )
    }
    val destination =
        transformed.sheet
            ?: return transformed.withQuestSheet(null)
                .copy(sheetNavigation = SheetNavigationState())
    val current =
        previous.sheet
            ?: return transformed.copy(
                sheet = destination.withNavigationDepth(0),
                sheetNavigation = SheetNavigationState(),
            )
    if (destination == current) {
        return transformed.copy(
            sheet = destination.withNavigationDepth(previous.sheetNavigation.backStack.size),
        )
    }

    val currentDepth = previous.sheetNavigation.backStack.size
    val requestedDepth = destination.navigation.depth.coerceAtLeast(0)
    val sameRoute =
        destination.navigation.routeKey.isNotBlank() &&
            destination.navigation.routeKey == current.navigation.routeKey
    val nextBackStack =
        when {
            sameRoute || requestedDepth == currentDepth -> previous.sheetNavigation.backStack
            requestedDepth <= 0 -> emptyList()
            requestedDepth > currentDepth -> {
                (previous.sheetNavigation.backStack + SheetNavigationEntry(current, previous.questPanelSession))
                    .takeLast(MAX_SHEET_NAVIGATION_DEPTH)
            }
            else -> previous.sheetNavigation.backStack.take(requestedDepth)
        }
    return transformed.copy(
        sheet = destination.withNavigationDepth(nextBackStack.size),
        sheetNavigation = SheetNavigationState(nextBackStack),
    )
}
