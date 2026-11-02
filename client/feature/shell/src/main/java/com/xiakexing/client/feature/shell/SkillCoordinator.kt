package com.xiakexing.client.feature.shell

import com.google.gson.JsonObject
import com.xiakexing.client.core.protocol.payload.SkillEventPacket
import com.xiakexing.client.core.protocol.payload.SkillInfoPacket
import com.xiakexing.client.core.protocol.payload.SkillListPacket
import com.xiakexing.client.core.protocol.payload.SkillSummaryPacket

/** Skill 域 owner：技能投影、战斗快捷栏与出口浮层。 */
internal class SkillCoordinator(
    private val persistence: PersistenceCoordinator,
    private val sink: SkillSink,
) {
    /** 选技弹窗数据源（练功=canIdlePractice 外功 / 打坐=isForceSkill 内功）。 */
    fun skillSummaries(): List<SkillSummaryPacket> = sink.currentState().latestSkillSummaries

    private var latestCombatActions: List<CombatShortcutAction> = emptyList()
    private var latestCombatActionsByRef: Map<String, CombatShortcutAction> = emptyMap()
    private var assignedCombatActionRefs: List<String> = emptyList()
    private var removedCombatActionRefs: Set<String> = emptySet()
    private val combatActionState = SkillCombatActionState()

    fun clearSkillSummaries() {
        latestCombatActions = emptyList()
        latestCombatActionsByRef = emptyMap()
        combatActionState.reset()
        sink.updateState(buildEmptySkillStateTransform(includeSkillInfo = false))
    }

    fun resetProjectionState() {
        latestCombatActions = emptyList()
        latestCombatActionsByRef = emptyMap()
        combatActionState.reset()
        sink.updateState(buildEmptySkillStateTransform(includeSkillInfo = true))
    }

    suspend fun loadStoredCombatShortcuts() {
        val (assignedRefs, removedRefs) =
            parseStoredCombatShortcuts(persistence.loadCombatShortcuts())
        assignedCombatActionRefs = assignedRefs
        removedCombatActionRefs = removedRefs
        autoFillCombatPerformSlots()
        refreshCombatSheetIfVisible()
    }

    fun handleSkillListPacket(payload: JsonObject) {
        handleSkillListPacket(SkillListPacket.fromJson(payload))
    }

    fun handleSkillListPacket(packet: SkillListPacket) {
        if (sink.pendingProtocolSheetRequest().isSkillListRequest()) {
            sink.clearPendingProtocolSheetRequest()
        }
        handleParsedSkillListPacket(packet)
    }

    private fun handleParsedSkillListPacket(packet: SkillListPacket) {
        latestCombatActions = packet.skills.flatMap { it.combatShortcutActions() }
        latestCombatActionsByRef = latestCombatActions.associateBy { action -> action.ref }
        combatActionState.markLoaded()
        autoFillCombatPerformSlots()
        val current = sink.currentState()
        val shouldOpenPendingCombatOverlay =
            combatActionState.consumePendingOpen(current.combatActive)
        val update =
            buildSkillListStateUpdate(
                packet = packet,
                actions = latestCombatActions,
                assignedRefs = assignedCombatActionRefs,
                currentSheet = current.sheet,
                currentOverlay = current.exitOverlay,
                currentCooldowns = current.combatActionCooldowns,
                statusSnapshot = current.statusSnapshot,
            )
        sink.updateState {
            copy(
                latestSkillSummaries = packet.skills,
                sheet = update.sheet,
                exitOverlay =
                    if (shouldOpenPendingCombatOverlay) {
                        buildCombatExitOverlay(
                            latestCombatActions,
                            assignedCombatActionRefs,
                        )
                    } else {
                        update.exitOverlay
                    },
                combatActionCooldowns = update.combatActionCooldowns,
                activeSkillSlots = update.activeSkillSlots,
            )
        }
    }

    fun handleSkillEventPacket(payload: JsonObject): Boolean =
        handleSkillEventPacket(SkillEventPacket.fromJson(payload))

    fun handleSkillEventPacket(packet: SkillEventPacket): Boolean {
        if (packet.event.trim().lowercase() != "activate") {
            return false
        }
        val slot = packet.slot.trim().lowercase()
        if (!isActiveSkillSlot(slot)) {
            return true
        }
        val current = sink.currentState()
        val skillName =
            packet.skillName.ifBlank {
                current.latestSkillSummaries.firstOrNull {
                        summary ->
                    summary.id == packet.skillId
                }?.name.orEmpty()
            }
        val update =
            buildSkillEventStateUpdate(
                packet = packet,
                skillName = skillName,
                currentSheet = current.sheet,
                currentActiveSlots = current.activeSkillSlots,
                summaries = current.latestSkillSummaries,
                statusSnapshot = current.statusSnapshot,
            )
        sink.updateState {
            copy(
                activeSkillSlots = update.activeSkillSlots,
                sheet = update.sheet,
            )
        }
        return true
    }

    fun handleSkillInfoPacket(
        payload: JsonObject,
        commandResponseId: String = "",
    ) {
        val packet = SkillInfoPacket.fromJson(payload)
        sink.updateState { copy(latestSkillInfo = packet) }
        val currentSheet = sink.currentState().sheet
        val pending = sink.pendingProtocolSheetRequest()
        if (!pending.matchesSkillInfoRequest(packet, commandResponseId)) {
            // 事实缓存可以接纳迟到数据，但旧请求不能结束新表面的 loading。
            return
        }
        sink.clearPendingProtocolSheetRequest()
        if (!currentSheet.isSkillDomainSurface()) return
        sink.updateState(buildSkillInfoSheetTransform(packet, currentSheet))
    }

    fun showActionHubPanel() {
        val current = sink.currentState()
        if (current.exitOverlay?.type == ExitOverlayType.COMBAT || current.sheet?.kind == ShellSheetKind.COMBAT) {
            combatActionState.cancelPendingOpen()
            sink.clearPendingProtocolSheetRequest()
            sink.updateState {
                copy(
                    sheet = null,
                    dialog = null,
                    exitOverlay = null,
                )
            }
            return
        }
        // 技能请求未返回时保持 pending open，不能把 dock 点击当成关闭动作。
        if (combatActionState.hasPendingOpen()) return
        openCombatActionHubPanel(requireCombat = false)
    }

    fun openCombatActionHubPanel(requireCombat: Boolean = false) {
        sink.flushPendingRoomRefresh()
        sink.clearPendingInteractionRequest()
        autoFillCombatPerformSlots()
        val sheetState = buildActionHubSheetState(latestCombatActions, assignedCombatActionRefs)
        val waitForCombatActions =
            combatActionState.requestOpen(requireCombat, waitForRefresh = requireCombat)
        if (!waitForCombatActions) {
            showCombatExitOverlay()
        } else {
            sink.updateState {
                copy(
                    sheet = null,
                    dialog = null,
                    exitOverlay = null,
                )
            }
        }
        sendSkillsCommand(sheetState)
    }

    fun showCombatPerformPicker(slotIndex: Int) {
        val normalizedSlot = slotIndex.coerceIn(0, COMBAT_PERFORM_SLOT_COUNT - 1)
        val sheetState =
            buildCombatPerformPickerSheetState(
                actions = latestCombatActions,
                slotIndex = normalizedSlot,
                loading = true,
            )
        showCombatPerformSheet(sheetState)
    }

    fun showCombatPerformSkillPicker(
        slotIndex: Int,
        skillId: String,
    ) {
        val normalizedSlot = slotIndex.coerceIn(0, COMBAT_PERFORM_SLOT_COUNT - 1)
        val normalizedSkillId = skillId.trim()
        if (normalizedSkillId.isBlank()) {
            return
        }
        val sheetState =
            buildCombatPerformSkillPickerSheetState(
                actions = latestCombatActions,
                slotIndex = normalizedSlot,
                skillId = normalizedSkillId,
                loading = true,
            )
        showCombatPerformSheet(sheetState)
    }

    fun handleAssignCombatPerformCommand(command: String) {
        val parsed = parseAssignCombatPerformCommand(command) ?: return
        assignCombatPerform(parsed.first, parsed.second)
    }

    fun requestRemoveCombatPerform(actionToken: String) {
        val actionRef = actionToken.toCombatShortcutActionRefOrBlank()
        val actionName = computeRemovedActionName(actionRef, latestCombatActionsByRef)
        if (actionRef.isBlank() || actionName.isBlank()) {
            return
        }
        sink.showActionConfirmation(
            title = "移除$actionName？",
            action =
                ShellAction(
                    label = "移除",
                    command = "$CONFIRM_REMOVE_COMBAT_PERFORM_PREFIX${actionRef.combatShortcutRemovalToken()}",
                    dismissAfterInvoke = true,
                ),
            dismissSheetAfterConfirm = false,
        )
    }

    fun removeCombatPerform(actionToken: String) {
        val actionRef = actionToken.toCombatShortcutActionRefOrBlank()
        if (actionRef.isBlank()) {
            return
        }
        val (nextAssignedRefs, nextRemovedRefs) =
            computeRemovedCombatPerformRefs(
                assignedCombatActionRefs,
                removedCombatActionRefs,
                actionRef,
            )
        assignedCombatActionRefs = nextAssignedRefs
        removedCombatActionRefs = nextRemovedRefs
        persistCombatShortcuts()
        sink.dismissPopup()
        refreshCombatSheetIfVisible(forceOpen = true)
    }

    fun showCombatExitOverlay() {
        combatActionState.cancelPendingOpen()
        sink.updateState {
            copy(
                sheet = null,
                dialog = null,
                commandPalette = null,
                exitOverlay =
                    buildCombatExitOverlay(
                        latestCombatActions,
                        assignedCombatActionRefs,
                    ),
            )
        }
    }

    fun refreshCombatExitOverlayIfVisible() = refreshCombatSheetIfVisible()

    fun showSkillsPanel(
        categoryId: String = DEFAULT_SKILL_PANEL_CATEGORY,
        refresh: Boolean = true,
    ) {
        sink.clearPendingInteractionRequest()
        val current = sink.currentState()
        val nextSheet =
            buildSkillsPanelSheet(
                skills = current.latestSkillSummaries,
                categoryId = categoryId,
                loading = refresh,
                activeSkillSlots = current.activeSkillSlots,
                statusSnapshot = current.statusSnapshot,
            )
        sink.updateState(buildSheetReplacementTransform(nextSheet))
        if (refresh) {
            sink.prepareProtocolSheetCommand(nextSheet, command = "skills")
            sink.sendDirectCommand("skills", suppressImmediateScroll = true)
        }
    }

    fun requestSkillDetail(skillId: String) {
        val normalizedSkillId = skillId.trim()
        if (normalizedSkillId.isBlank()) {
            return
        }
        val currentSheet = sink.currentState().sheet
        if (!currentSheet.isSkillDomainSurface()) {
            return
        }
        val pendingSheet = buildLoadingSkillDetailSheet(currentSheet)
        sink.updateState {
            copy(
                sheet = pendingSheet,
            )
        }
        val command = "skills $normalizedSkillId"
        sendSkillsCommand(pendingSheet, command)
    }

    fun showSkillBreakthroughChoices(skillId: String) {
        val normalizedSkillId = skillId.trim()
        if (normalizedSkillId.isBlank()) {
            return
        }
        val packet =
            findSkillInfoForBreakthrough(normalizedSkillId, sink.currentState().latestSkillInfo)
        if (packet == null) {
            requestSkillDetail(normalizedSkillId)
            return
        }
        sink.dismissPopup()
        sink.updateState(buildBreakthroughSheetTransform(packet))
    }

    private fun assignCombatPerform(
        slotIndex: Int,
        actionToken: String,
    ) {
        val actionRef = actionToken.toCombatShortcutActionRefOrBlank()
        val refs =
            computeAssignedCombatPerformRefs(
                assignedRefs = assignedCombatActionRefs,
                removedRefs = removedCombatActionRefs,
                actionRef = actionRef,
                slotIndex = slotIndex,
                actionsByRef = latestCombatActionsByRef,
            ) ?: return
        assignedCombatActionRefs = refs.first
        removedCombatActionRefs = refs.second
        persistCombatShortcuts()
        refreshCombatSheetIfVisible(forceOpen = true)
    }

    private fun autoFillCombatPerformSlots() {
        val normalizedNextRefs =
            computeAutoFilledCombatPerformSlots(
                assignedRefs = assignedCombatActionRefs,
                removedRefs = removedCombatActionRefs,
                actions = latestCombatActions,
            )
        if (normalizedNextRefs != assignedCombatActionRefs) {
            assignedCombatActionRefs = normalizedNextRefs
            persistCombatShortcuts()
        }
    }

    private fun refreshCombatSheetIfVisible(forceOpen: Boolean = false) {
        val current = sink.currentState()
        val update =
            buildCombatSheetRefreshUpdate(
                currentSheet = current.sheet,
                currentOverlay = current.exitOverlay,
                currentDialog = current.dialog,
                actions = latestCombatActions,
                assignedRefs = assignedCombatActionRefs,
                forceOpen = forceOpen,
            ) ?: return
        sink.updateState {
            copy(
                sheet = update.sheet,
                dialog = update.dialog,
                exitOverlay = update.exitOverlay,
            )
        }
    }

    private fun showCombatPerformSheet(sheetState: ShellSheetState) {
        sink.updateState(buildSheetReplacementTransform(sheetState))
        sendSkillsCommand(sheetState)
    }

    private fun sendSkillsCommand(
        sheetState: ShellSheetState?,
        command: String = "skills",
    ) {
        sink.prepareHiddenProtocolSheetCommand(sheetState, command)
        sink.sendDirectCommand(command, suppressImmediateScroll = true)
    }

    private fun persistCombatShortcuts() {
        persistence.saveCombatShortcuts(assignedCombatActionRefs, removedCombatActionRefs)
    }
}
