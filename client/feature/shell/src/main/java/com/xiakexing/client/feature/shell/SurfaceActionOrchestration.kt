package com.xiakexing.client.feature.shell

import com.xiakexing.client.feature.shell.update.UPDATE_DOWNLOAD_COMMAND

/**
 * Surface 域 action 编排——出口浮层/弹窗/sheet 的点击路由与命令分派
 * （ShellSurfaceActionCoordinator 收敛）。判定纯函数在 SurfaceStateReducer.kt。
 */
private val surfaceCommandRouter = ShellSurfaceCommandRouter()

internal fun handleExitOverlayButtonClick(
    sink: SurfaceSink,
    action: ShellAction,
) {
    val normalizedLabel = action.label.trim()
    val command = action.command.trim()
    if (normalizedLabel.isBlank() && command.isBlank()) return
    sink.closeChatPanel()
    val commandRoute = surfaceCommandRouter.routeCommand(command)
    if (commandRoute != null) {
        handleSurfaceCommandRoute(sink, commandRoute)
        return
    }
    if (action.requiresConfirmation()) {
        sink.showActionConfirmation(
            title = command.confirmationTitle(),
            action = action.copy(label = normalizedLabel.ifBlank { command }, role = "danger"),
            dismissSheetAfterConfirm = false,
        )
        return
    }
    if (command.isNotBlank() && command != normalizedLabel) {
        sink.sendDirectCommand(command)
        return
    }
    val labelRoute = surfaceCommandRouter.routeExitOverlayLabel(normalizedLabel)
    if (labelRoute != null) {
        handleSurfaceCommandRoute(sink, labelRoute)
        return
    }
    if (command.isNotBlank()) sink.sendDirectCommand(command)
}

internal fun handleSurfaceCommandRoute(
    sink: SurfaceSink,
    route: ShellSurfaceCommandRoute,
) {
    // 聊天层只占主文本区；任何导航型入口都先收起，避免两个表面同时抢触点。
    sink.closeChatPanel()
    when (route) {
        ShellSurfaceCommandRoute.InteractionGiveItems -> sink.openInteractionGiveItemsSheet()
        ShellSurfaceCommandRoute.InteractionObjectRoot -> sink.returnToInteractionObjectRoot()
        ShellSurfaceCommandRoute.InteractionRoot -> sink.returnToInteractionRoot()
        ShellSurfaceCommandRoute.InteractionApprenticeUnavailable -> sink.showApprenticeUnavailablePopup()
        is ShellSurfaceCommandRoute.InteractionUnavailableAction -> {
            sink.appendInteractionFeedback(unavailableInteractionActionNotice(route.actionKey))
        }
        ShellSurfaceCommandRoute.BankRoot -> sink.showBankRootSheet()
        ShellSurfaceCommandRoute.BankDeposit -> sink.showBankDepositSheet()
        ShellSurfaceCommandRoute.BankWithdraw -> sink.showBankWithdrawSheet()
        ShellSurfaceCommandRoute.QuestList -> sink.showQuestListSheet()
        ShellSurfaceCommandRoute.ActivityPanel -> sink.showActivityPanel()
        is ShellSurfaceCommandRoute.QuestDetail -> sink.showQuestDetailSheet(route.questId)
        is ShellSurfaceCommandRoute.TrackQuest -> sink.trackQuest(route.questId)
        ShellSurfaceCommandRoute.ClearTrackedQuest -> sink.clearTrackedQuest()
        ShellSurfaceCommandRoute.SetReduceMotion -> sink.toggleReduceMotion()
        ShellSurfaceCommandRoute.CharacterPanel -> sink.showCharacterPanel()
        ShellSurfaceCommandRoute.AttributePanel -> sink.showAttributePanel()
        ShellSurfaceCommandRoute.InventoryPanel -> sink.showInventoryPanel()
        ShellSurfaceCommandRoute.EquipmentPanel -> sink.showEquipmentPanel()
        ShellSurfaceCommandRoute.FunctionPanel -> sink.showFunctionPanel()
        ShellSurfaceCommandRoute.MapPanel -> sink.showMapPanel()
        ShellSurfaceCommandRoute.MessagePanel -> sink.showMessagePanel()
        ShellSurfaceCommandRoute.WorldShopGuidePanel -> sink.showWorldShopGuidePanel()
        ShellSurfaceCommandRoute.SettingsPanel -> sink.showSettingsPanel()
        ShellSurfaceCommandRoute.ActionHubPanel -> sink.showActionHubPanel()
        ShellSurfaceCommandRoute.SkillsPanel -> sink.showSkillsPanel()
        is ShellSurfaceCommandRoute.CombatPerformPicker ->
            sink.showCombatPerformPicker(
                route.slotIndex,
            )
        is ShellSurfaceCommandRoute.CombatPerformSkillPicker -> {
            sink.showCombatPerformSkillPicker(route.slotIndex, route.skillId)
        }
        is ShellSurfaceCommandRoute.AssignCombatPerform ->
            sink.handleAssignCombatPerformCommand(
                route.command,
            )
        is ShellSurfaceCommandRoute.RemoveCombatPerform ->
            sink.requestRemoveCombatPerform(
                route.skillId,
            )
        is ShellSurfaceCommandRoute.HelpCategory -> sink.showHelpPanel(route.categoryId)
        is ShellSurfaceCommandRoute.HelpDetail -> sink.showHelpDetail(route.entryId)
        is ShellSurfaceCommandRoute.InventoryItemDetail ->
            sink.requestInventoryItemDetail(
                route.itemId,
            )
        is ShellSurfaceCommandRoute.InventoryCategory ->
            sink.showInventoryPanel(
                route.categoryId,
            )
        is ShellSurfaceCommandRoute.SkillCategory -> sink.showSkillsPanel(route.categoryId)
        is ShellSurfaceCommandRoute.SkillDetail -> sink.requestSkillDetail(route.skillId)
        is ShellSurfaceCommandRoute.SkillBreakthrough ->
            sink.showSkillBreakthroughChoices(
                route.skillId,
            )
        is ShellSurfaceCommandRoute.LearningSkillCategory ->
            sink.selectLearningSkillCategory(
                route.categoryId,
            )
    }
}

internal fun handlePopupAction(
    sink: SurfaceSink,
    action: ShellAction,
) {
    if (action.command == ACTION_CONFIRMATION_CANCEL_COMMAND) {
        sink.dismissPopup()
        return
    }
    val shouldDismissPopupGroup = sink.currentState().dialog?.shouldDismissPopupGroupAfterAction() == true
    if (action.command.startsWith(GIVE_QUANTITY_PREFIX)) {
        sink.submitPendingGiveQuantity(
            action.command.removePrefix(GIVE_QUANTITY_PREFIX).toIntOrNull() ?: 0,
        )
        return
    }
    if (action.command.startsWith(BANK_QUANTITY_PREFIX)) {
        sink.submitPendingBankQuantity(action.command.removePrefix(BANK_QUANTITY_PREFIX))
        return
    }
    if (action.command.startsWith(TRADE_MONEY_PREFIX)) {
        sink.submitPendingTradeMoney(action.command.removePrefix(TRADE_MONEY_PREFIX))
        return
    }
    if (action.command == TRADE_MONEY_SUBMIT_COMMAND) {
        sink.submitPendingTradeMoney(sink.currentState().dialog?.inputValue.orEmpty())
        return
    }
    if (sink.currentState().sheet?.canCaptureTradeCommand(action.command) == true) {
        sink.prepareProtocolSheetCommand(sink.currentState().sheet, action.command)
        sink.sendDirectCommand(action.command, suppressImmediateScroll = true)
        if (action.dismissAfterInvoke) sink.dismissPopup()
        return
    }
    if (action.command == BANK_QUANTITY_INPUT_COMMAND) {
        sink.submitPendingBankQuantity(sink.currentState().dialog?.inputValue.orEmpty())
        return
    }
    if (action.command == GIVE_QUANTITY_INPUT_COMMAND) {
        sink.submitPendingGiveQuantity(
            sink.currentState().dialog?.inputValue.orEmpty().toIntOrNull() ?: 0,
        )
        return
    }
    if (action.command == GIVE_QUANTITY_CONFIRM_ALL_COMMAND) {
        sink.submitPendingGiveAll()
        return
    }
    if (action.command.startsWith(LEARN_QUANTITY_PREFIX)) {
        sink.submitPendingLearnQuantity(action.command.removePrefix(LEARN_QUANTITY_PREFIX))
        return
    }
    if (action.command == UPDATE_CHECK_COMMAND) {
        sink.checkForClientUpdate(silent = false)
        return
    }
    if (action.command == UPDATE_DOWNLOAD_COMMAND) {
        sink.downloadClientUpdate()
        return
    }
    if (action.command.startsWith(CONFIRM_REMOVE_COMBAT_PERFORM_PREFIX)) {
        sink.removeCombatPerform(
            action.command.removePrefix(CONFIRM_REMOVE_COMBAT_PERFORM_PREFIX),
        )
        if (shouldDismissPopupGroup) sink.dismissPopupGroup()
        return
    }
    surfaceCommandRouter.routeCommand(action.command)?.let { route ->
        handleSurfaceCommandRoute(sink, route)
        if (shouldDismissPopupGroup) sink.dismissPopupGroup()
        return
    }
    val shouldDismissUnderlyingSheet = sink.consumeDismissSheetOnPopupAction()
    val currentDialog = sink.currentState().dialog
    if (shouldKeepPopupCommandResult(currentDialog, action.command)) {
        sink.prepareProtocolPopupCommand(currentDialog, action.command)
        sink.sendDirectCommand(action.command, suppressImmediateScroll = true)
        return
    }
    sink.sendDirectCommand(action.command)
    if (action.dismissAfterInvoke) {
        if (shouldDismissPopupGroup) {
            sink.dismissPopupGroup()
        } else {
            sink.dismissPopup()
        }
    }
    if (shouldDismissUnderlyingSheet) {
        sink.dismissSheet()
    }
}

internal fun handleSheetItem(
    sink: SurfaceSink,
    item: ShellSheetItem,
) {
    sink.closeChatPanel()
    val currentSheet = sink.currentState().sheet
    val command = item.command.trim().withInteractionSheetCommandTarget(currentSheet)
    val commandRoute = surfaceCommandRouter.routeCommand(command)
    if (currentSheet?.loading == true && commandRoute == null && !command.canNavigateLocallyWhileLoading()) {
        return
    }
    if (commandRoute != null) {
        handleSurfaceCommandRoute(sink, commandRoute)
        return
    }
    if (command == UPDATE_CHECK_COMMAND) {
        sink.checkForClientUpdate(silent = false)
        return
    }
    if (currentSheet?.source == PopupSource.INTERACTION_GIVE && command.startsWith("give ")) {
        sink.showGiveQuantityPopup(command)
        return
    }
    if (currentSheet?.source == PopupSource.INTERACTION_LEARN && command.isLearnSkillCommand()) {
        sink.showLearnQuantityPopup(command)
        return
    }
    if (currentSheet?.canCaptureTradeCommand(command) == true) {
        sink.prepareProtocolSheetCommand(currentSheet, command)
        sink.sendDirectCommand(command, suppressImmediateScroll = true)
        return
    }
    if (command.isBlank()) {
        return
    }
    if (item.requiresConfirmation()) {
        sink.showActionConfirmation(
            title = command.confirmationTitle(),
            action =
                ShellAction(
                    item.title.ifBlank {
                        command
                    },
                    command,
                    dismissAfterInvoke = true,
                    role = "danger",
                ),
            dismissSheetAfterConfirm = false,
        )
        return
    }
    if (currentSheet?.source == PopupSource.INTERACTION_OBJECT &&
        command.isFacilitySkillPickerCommand()
    ) {
        // 第五轮 R4：设施动作改开选技弹窗——练功选已学外功、打坐选已学内功；
        // 空态飘窗提示。点选后发带参命令（practice idle <id> / dazuo <id>）。
        routeFacilitySkillPicker(sink, command)
        return
    }
    if (command.isQuestAcceptCommand() &&
        currentSheet?.source == PopupSource.INTERACTION_OBJECT &&
        currentSheet.interactionTargetKind == InteractionObjectKind.NPC
    ) {
        // #9 接取反馈直达主文本区：直发命令并关闭交互弹窗，
        // 不把响应文本捕获进弹窗，也不弹任务面板。
        sink.sendDirectCommand(command)
        sink.dismissSheet()
        return
    }
    val shouldHandleInsideInteractionSheet = sink.prepareInteractionSheetCommand(command)
    if (shouldHandleInsideInteractionSheet) {
        sink.sendDirectCommand(command, suppressImmediateScroll = true)
        return
    }
    if (shouldKeepSheetCommandResult(currentSheet, item, command)) {
        sink.prepareProtocolSheetCommand(currentSheet, command = command)
        sink.sendDirectCommand(command, suppressImmediateScroll = true)
        return
    }
    sink.sendDirectCommand(command)
    if (item.dismissAfterInvoke && currentSheet?.shouldKeepSheetAfterAction() != true) {
        sink.dismissSheet()
    }
}

internal fun handleSheetPrimaryAction(
    sink: SurfaceSink,
    action: ShellAction,
) {
    sink.closeChatPanel()
    val currentSheet = sink.currentState().sheet
    val command = action.command.trim().withInteractionSheetCommandTarget(currentSheet)
    val commandRoute = surfaceCommandRouter.routeCommand(command)
    if (command.isQuestAcceptCommand() &&
        currentSheet?.source == PopupSource.INTERACTION_OBJECT &&
        currentSheet.interactionTargetKind == InteractionObjectKind.NPC
    ) {
        // #9 接取反馈直达主文本区：与 handleSheetItem 同规则，
        // 直发命令并关闭交互弹窗，不进捕获会话。
        sink.sendDirectCommand(command)
        sink.dismissSheet()
        return
    }
    if (currentSheet?.source == PopupSource.INTERACTION_OBJECT &&
        command.isFacilitySkillPickerCommand()
    ) {
        // 第五轮 R4：与 handleSheetItem 同款镜像，消除入口分叉（root 组按钮走 primary 路径）。
        routeFacilitySkillPicker(sink, command)
        return
    }
    if (currentSheet?.loading == true &&
        !action.kind.equals("back", ignoreCase = true) &&
        commandRoute == null &&
        !command.canNavigateLocallyWhileLoading()
    ) {
        return
    }
    if (!action.kind.equals("back", ignoreCase = true) &&
        currentSheet != null && handleDockWorkspaceReentry(sink, currentSheet, command)
    ) {
        return
    }
    val resolvedAction =
        if (command == action.command.trim()) {
            action
        } else {
            action.copy(
                command = command,
            )
        }
    if (action.commandSequence.isNotEmpty()) {
        sink.sendRouteCommandSequence(action.commandSequence)
        if (shouldDismissSheetAfterAction(resolvedAction, currentSheet)) {
            sink.dismissSheet()
        }
        return
    }
    if (commandRoute != null) {
        handleSurfaceCommandRoute(sink, commandRoute)
        return
    }
    if (currentSheet?.source == PopupSource.INTERACTION_GIVE && command.startsWith("give ")) {
        sink.showGiveQuantityPopup(command)
        return
    }
    if (currentSheet?.source == PopupSource.INTERACTION_LEARN && command.isLearnSkillCommand()) {
        sink.showLearnQuantityPopup(command)
        return
    }
    if (currentSheet?.isTradeSessionSheet() == true && command == TRADE_MONEY_INPUT_COMMAND) {
        sink.showTradeMoneyPopup()
        return
    }
    if (action.isGiveAction && currentSheet?.source == PopupSource.INTERACTION_OBJECT) {
        sink.openInteractionGiveItemsSheet()
        return
    }
    if (resolvedAction.requiresConfirmation()) {
        sink.showActionConfirmation(
            title = command.confirmationTitle(),
            action = resolvedAction.copy(dismissAfterInvoke = true, role = "danger"),
            dismissSheetAfterConfirm = false,
        )
        return
    }
    if (currentSheet?.source == PopupSource.INTERACTION_OBJECT &&
        currentSheet.interactionTargetKind == InteractionObjectKind.NPC &&
        command.isNpcLearnListCommand()
    ) {
        sink.sendDirectCommand(command, suppressImmediateScroll = true)
        return
    }
    if (currentSheet?.isQuestDetailSheet() == true && command.isLookInteractionCommand()) {
        sink.prepareProtocolSheetCommand(currentSheet, command = command)
        sink.sendDirectCommand(command, suppressImmediateScroll = true)
        return
    }
    if (currentSheet?.canCaptureInfoSheetAction(command) == true) {
        sink.prepareProtocolSheetCommand(currentSheet, command = command)
        sink.sendDirectCommand(command, suppressImmediateScroll = true)
        return
    }
    if (currentSheet?.canCaptureTradeCommand(command) == true) {
        sink.prepareProtocolSheetCommand(currentSheet, command)
        sink.sendDirectCommand(command, suppressImmediateScroll = true)
        return
    }
    if (currentSheet?.source == PopupSource.INTERACTION_OBJECT &&
        currentSheet.interactionTargetKind != InteractionObjectKind.NPC &&
        command.isQuestInfoCommand()
    ) {
        handleSurfaceCommandRoute(
            sink,
            ShellSurfaceCommandRoute.QuestDetail(command.removePrefix("quest info ").trim()),
        )
        return
    }
    if (currentSheet?.canCaptureProtocolDialogCommand(command) == true) {
        sink.prepareProtocolSheetCommand(currentSheet, command = command)
        sink.sendDirectCommand(command, suppressImmediateScroll = true)
        return
    }
    if (currentSheet?.canCapturePrimaryCommandResult(command) == true) {
        sink.prepareProtocolSheetCommand(currentSheet, command = command)
        sink.sendDirectCommand(command, suppressImmediateScroll = true)
        return
    }
    val shouldHandleInsideInteractionSheet =
        sink.prepareInteractionSheetCommand(
            command = command,
            requestedGroupId = resolvedAction.targetGroup,
        )
    sink.sendDirectCommand(
        command,
        suppressImmediateScroll = shouldHandleInsideInteractionSheet,
    )
    if (shouldHandleInsideInteractionSheet) {
        return
    } else if (shouldDismissSheetAfterAction(resolvedAction, currentSheet)) {
        sink.dismissSheet()
    }
}

private fun String.withInteractionSheetCommandTarget(currentSheet: ShellSheetState?): String {
    if (currentSheet?.source != PopupSource.INTERACTION_OBJECT) return this
    val commandTarget = currentSheet.interactionTargetId
    return withNpcInteractionCommandTarget(commandTarget)
}

private fun handleDockWorkspaceReentry(
    sink: SurfaceSink,
    currentSheet: ShellSheetState,
    command: String,
): Boolean {
    // 重入折叠仅限 dock 工作区面板；交互弹窗即使锚定 dockSection 也不改直发语义。
    if (currentSheet.source != PopupSource.DOCK_BUTTON) return false
    val targetSection = command.dockWorkspaceSection() ?: return false
    if (currentSheet.navigation.dockSection != targetSection) return false
    if (currentSheet.navigation.depth == 0) {
        sink.dismissSheet()
        return true
    }
    return sink.popSheet()
}

private fun shouldDismissSheetAfterAction(
    action: ShellAction,
    currentSheet: ShellSheetState?,
): Boolean {
    if (action.dismissAfterInvoke && action.command.closesInfoSheetForRoomTransition()) {
        return true
    }
    if (currentSheet?.shouldKeepSheetAfterAction() == true) {
        return false
    }
    if (currentSheet?.source == PopupSource.INTERACTION_OBJECT) {
        return !action.command.shouldRemainInInteractionSession()
    }
    return action.dismissAfterInvoke
}

private fun String.closesInfoSheetForRoomTransition(): Boolean {
    val normalized = trim().lowercase()
    return normalized.startsWith("route ") ||
        normalized.startsWith("go ") ||
        normalized.startsWith("dungeon enter ")
}

/** 第五轮 R4：设施动作→技能选择弹窗路由（复用当前 sheet 动作组承载选技列表）。 */

private fun routeFacilitySkillPicker(
    sink: SurfaceSink,
    command: String,
) {
    val isPractice = command == "practice idle"
    val candidates =
        sink.skillSummariesForPicker().filter { summary ->
            if (isPractice) summary.canIdlePractice else summary.isForceSkill
        }
    if (candidates.isEmpty()) {
        sink.showFloatingNotice(
            if (isPractice) "当前无可练习武学" else "当前无可参悟武学",
        )
        return
    }
    val actions =
        candidates.map { summary ->
            ShellAction(
                label = "${summary.name}（${summary.level}级）",
                command = if (isPractice) "practice idle ${summary.id}" else "dazuo ${summary.id}",
                dismissAfterInvoke = true,
                role = "primary",
            )
        }
    val pickerGroup =
        ShellActionGroup(
            id = "facility_picker",
            title = if (isPractice) "选择要练习的外功" else "选择要参悟的内功",
            surface = "root",
            actions = actions,
        )
    sink.showFacilitySkillPicker(pickerGroup)
}

private fun String.isFacilitySkillPickerCommand(): Boolean =
    this == "practice idle" || this == "dazuo"

private fun String.dockWorkspaceSection(): DockWorkspaceSection? =
    when (trim()) {
        OPEN_CHARACTER_ATTRIBUTES_COMMAND -> DockWorkspaceSection.ATTRIBUTE
        OPEN_INVENTORY_PANEL_COMMAND -> DockWorkspaceSection.INVENTORY
        "skills" -> DockWorkspaceSection.SKILL
        OPEN_ACTIVITY_PANEL_COMMAND -> DockWorkspaceSection.ACTIVITY
        else -> null
    }

private fun ShellAction.requiresConfirmation(): Boolean {
    val normalized = command.trim().lowercase()
    return (normalized != "fight" && !normalized.startsWith("fight ")) &&
        (role.equals("danger", ignoreCase = true) || command.needsConfirmation())
}

private fun ShellSheetItem.requiresConfirmation(): Boolean =
    role == ShellSheetItemRole.DANGER || command.needsConfirmation()

private fun String.canNavigateLocallyWhileLoading(): Boolean =
    trim().startsWith(
        "ui://",
        ignoreCase = true,
    )
