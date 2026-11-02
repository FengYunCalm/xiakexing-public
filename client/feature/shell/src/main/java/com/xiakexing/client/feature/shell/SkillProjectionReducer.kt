package com.xiakexing.client.feature.shell

import com.xiakexing.client.core.protocol.payload.ActiveSkillSlotPacket
import com.xiakexing.client.core.protocol.payload.PlayerStatusSnapshot
import com.xiakexing.client.core.protocol.payload.SkillEventPacket
import com.xiakexing.client.core.protocol.payload.SkillInfoPacket
import com.xiakexing.client.core.protocol.payload.SkillListPacket
import com.xiakexing.client.core.protocol.payload.SkillSummaryPacket
import com.xiakexing.client.feature.shell.component.isCombatExitSheet

/**
 * Skill 域纯逻辑层——技能列表/事件/详情的状态变换、战斗快捷栏
 * 自动填充与冷却合并。不持有 sink，不执行任何副作用。
 */
internal val ACTIVE_SKILL_SLOTS = setOf("outer", "parry", "force", "dodge")

internal data class SkillListStateUpdate(
    val sheet: ShellSheetState?,
    val exitOverlay: ExitOverlayState?,
    val combatActionCooldowns: Map<String, CombatActionCooldown>,
    val activeSkillSlots: Map<String, ActiveSkillSlotPacket>,
)

internal data class SkillEventStateUpdate(
    val activeSkillSlots: Map<String, ActiveSkillSlotPacket>,
    val sheet: ShellSheetState?,
)

internal data class CombatSheetRefreshUpdate(
    val sheet: ShellSheetState?,
    val dialog: ShellPopupState?,
    val exitOverlay: ExitOverlayState?,
)

/** 技能列表收敛：按当前 sheet 域重建技能面板/战斗面板/选技弹窗，合并冷却。 */
internal fun buildSkillListStateUpdate(
    packet: SkillListPacket,
    actions: List<CombatShortcutAction>,
    assignedRefs: List<String>,
    currentSheet: ShellSheetState?,
    currentOverlay: ExitOverlayState?,
    currentCooldowns: Map<String, CombatActionCooldown>,
    statusSnapshot: PlayerStatusSnapshot?,
): SkillListStateUpdate {
    val nextCooldowns = mergeSkillListCooldowns(currentCooldowns, actions)
    return SkillListStateUpdate(
        sheet =
            when {
                currentSheet?.isSkillWorkspaceRoot() == true -> {
                    buildSkillsSheetState(
                        skills = packet.skills,
                        categoryId = currentSheet.panelSelectedTabId.ifBlank { DEFAULT_SKILL_PANEL_CATEGORY },
                        loading = false,
                        activeSkillSlots = packet.activeSlots,
                        statusSnapshot = statusSnapshot,
                    )
                }
                currentSheet?.isCombatExitSheet() == true -> {
                    buildActionHubSheetState(actions, assignedRefs)
                }
                currentSheet?.dialogSource == COMBAT_ACTION_PICKER_DIALOG_SOURCE -> {
                    buildCombatPerformPickerSheetState(
                        actions = actions,
                        slotIndex = currentSheet.dialogId.toIntOrNull() ?: 0,
                    )
                }
                currentSheet?.dialogSource == COMBAT_ACTION_SKILL_PICKER_DIALOG_SOURCE -> {
                    val parts = currentSheet.dialogId.split("/", limit = 2)
                    buildCombatPerformSkillPickerSheetState(
                        actions = actions,
                        slotIndex = parts.getOrNull(0)?.toIntOrNull() ?: 0,
                        skillId = parts.getOrNull(1).orEmpty(),
                    )
                }
                currentSheet?.isSkillDomainSurface() == true && currentSheet.loading == true ->
                    currentSheet.copy(
                        loading = false,
                    )
                else -> currentSheet
            },
        exitOverlay =
            if (currentOverlay?.type == ExitOverlayType.COMBAT) {
                ExitOverlayState(
                    type = ExitOverlayType.COMBAT,
                    buttons =
                        buildCombatOverlayButtons(
                            actions,
                            assignedRefs,
                        ),
                )
            } else {
                currentOverlay
            },
        combatActionCooldowns = nextCooldowns,
        activeSkillSlots = packet.activeSlots,
    )
}

/** 技能激发事件：写入激活槽位并重建技能工作区 sheet。 */
internal fun buildSkillEventStateUpdate(
    packet: SkillEventPacket,
    skillName: String,
    currentSheet: ShellSheetState?,
    currentActiveSlots: Map<String, ActiveSkillSlotPacket>,
    summaries: List<SkillSummaryPacket>,
    statusSnapshot: PlayerStatusSnapshot?,
): SkillEventStateUpdate {
    val slot = packet.slot.trim().lowercase()
    val category = packet.category.trim().lowercase()
    val stateKey = if (slot == "outer" && category.isNotBlank()) "outer:$category" else slot
    val nextActiveSkillSlots =
        currentActiveSlots +
            (
                stateKey to
                    ActiveSkillSlotPacket(
                        slot = slot,
                        skillId = packet.skillId,
                        skillName = skillName,
                        category = category,
                    )
            )
    return SkillEventStateUpdate(
        activeSkillSlots = nextActiveSkillSlots,
        sheet =
            if (currentSheet?.isSkillWorkspaceRoot() == true) {
                buildSkillsSheetState(
                    skills = summaries,
                    categoryId = currentSheet.panelSelectedTabId.ifBlank { DEFAULT_SKILL_PANEL_CATEGORY },
                    loading = currentSheet.loading,
                    activeSkillSlots = nextActiveSkillSlots,
                    statusSnapshot = statusSnapshot,
                )
            } else {
                currentSheet
            },
    )
}

/** 技能详情收敛：按当前 sheet 域重建详情/突破 sheet。 */
internal fun buildSkillInfoStateUpdate(
    packet: SkillInfoPacket,
    currentSheet: ShellSheetState?,
): ShellSheetState? =
    when {
        currentSheet?.isSkillWorkspaceRoot() == true -> {
            buildSkillDetailSheetState(packet)
        }
        currentSheet?.dialogSource == SKILL_BREAKTHROUGH_DIALOG_SOURCE && packet.breakthroughChoices.isNotEmpty() ->
            buildSkillBreakthroughSheetState(
                packet,
            )
        currentSheet?.dialogSource == SKILL_BREAKTHROUGH_DIALOG_SOURCE -> null
        currentSheet.isSkillDomainSurface() -> buildSkillDetailSheetState(packet)
        else -> currentSheet
    }

/** 战斗面板/出口浮层刷新；不可见且非强制时返回 null（无状态变化）。 */
internal fun buildCombatSheetRefreshUpdate(
    currentSheet: ShellSheetState?,
    currentOverlay: ExitOverlayState?,
    currentDialog: ShellPopupState?,
    actions: List<CombatShortcutAction>,
    assignedRefs: List<String>,
    forceOpen: Boolean,
): CombatSheetRefreshUpdate? {
    val combatSheetVisible = currentSheet?.isCombatExitSheet() == true
    val combatOverlayVisible = currentOverlay?.type == ExitOverlayType.COMBAT
    if (!forceOpen && !combatSheetVisible && !combatOverlayVisible) {
        return null
    }
    return CombatSheetRefreshUpdate(
        sheet =
            when {
                combatSheetVisible ->
                    buildActionHubSheetState(
                        actions,
                        assignedRefs,
                    )
                forceOpen -> null
                else -> currentSheet
            },
        dialog = if (forceOpen) null else currentDialog,
        exitOverlay =
            if (combatOverlayVisible || forceOpen) {
                ExitOverlayState(
                    type = ExitOverlayType.COMBAT,
                    buttons =
                        buildCombatOverlayButtons(
                            actions,
                            assignedRefs,
                        ),
                )
            } else {
                currentOverlay
            },
    )
}

/** 冷却合并：活跃冷却写入，已过期且非活跃的移除。 */
internal fun mergeSkillListCooldowns(
    currentCooldowns: Map<String, CombatActionCooldown>,
    actions: List<CombatShortcutAction>,
): Map<String, CombatActionCooldown> {
    val next = currentCooldowns.toMutableMap()
    actions.forEach { action ->
        val cooldown = action.cooldown
        if (cooldown.active) {
            next[action.ref] = cooldown
        } else if (action.ref in next && next.getValue(action.ref).remaining <= 0) {
            next.remove(action.ref)
        }
    }
    return next.filterValues { cooldown -> cooldown.active }
}

/** 战斗快捷栏自动填充：保留已分配、补入可用且未移除的动作，去重后返回。 */
internal fun computeAutoFilledCombatPerformSlots(
    assignedRefs: List<String>,
    removedRefs: Set<String>,
    actions: List<CombatShortcutAction>,
): List<String> {
    val availableActionRefs = actions.map { it.ref }
    val availableActionRefSet = actions.associateBy { it.ref }.keys
    val nextAssignedRefs =
        assignedRefs
            .padCombatActionSlots()
            .take(COMBAT_PERFORM_SLOT_COUNT)
            .map { actionRef ->
                actionRef.toCombatShortcutActionRefOrBlank()
                    .takeIf { it in availableActionRefSet }
                    .orEmpty()
            }
            .toMutableList()
    val addedRefs =
        availableActionRefs.filter {
            it !in nextAssignedRefs && it !in removedRefs
        }
    for (actionRef in addedRefs) {
        val emptyIndex = nextAssignedRefs.indexOf("")
        if (emptyIndex == -1) {
            break
        }
        nextAssignedRefs[emptyIndex] = actionRef
    }
    while (nextAssignedRefs.size < COMBAT_PERFORM_SLOT_COUNT) {
        nextAssignedRefs += ""
    }
    return nextAssignedRefs.dedupeCombatActionSlots()
}

internal fun buildCombatExitOverlay(
    actions: List<CombatShortcutAction>,
    assignedRefs: List<String>,
): ExitOverlayState =
    ExitOverlayState(
        type = ExitOverlayType.COMBAT,
        buttons =
            buildCombatOverlayButtons(
                actions,
                assignedRefs,
            ),
    )

internal fun parseAssignCombatPerformCommand(command: String): Pair<Int, String>? {
    val parts = command.removePrefix(ASSIGN_COMBAT_PERFORM_PREFIX).split("/", limit = 2)
    if (parts.size != 2) {
        return null
    }
    val slotIndex = parts[0].toIntOrNull() ?: return null
    return slotIndex to parts[1]
}

internal fun computeRemovedActionName(
    actionRef: String,
    actionsByRef: Map<String, CombatShortcutAction>,
): String {
    val action = actionsByRef[actionRef]
    return action?.label.orEmpty()
        .ifBlank { actionRef.combatShortcutActionId() }
        .ifBlank { action?.skillName.orEmpty() }
}

internal fun computeRemovedCombatPerformRefs(
    assignedRefs: List<String>,
    removedRefs: Set<String>,
    actionRef: String,
): Pair<List<String>, Set<String>> {
    val nextAssignedRefs =
        assignedRefs
            .map { if (it.toCombatShortcutActionRefOrBlank() == actionRef) "" else it }
            .padCombatActionSlots()
    return nextAssignedRefs to (removedRefs + actionRef)
}

internal fun computeAssignedCombatPerformRefs(
    assignedRefs: List<String>,
    removedRefs: Set<String>,
    actionRef: String,
    slotIndex: Int,
    actionsByRef: Map<String, CombatShortcutAction>,
): Pair<List<String>, Set<String>>? {
    if (slotIndex !in 0 until COMBAT_PERFORM_SLOT_COUNT) {
        return null
    }
    if (actionRef.isBlank() || actionRef !in actionsByRef) {
        return null
    }
    val mutableRefs = assignedRefs.toMutableList()
    mutableRefs.remove(actionRef)
    while (mutableRefs.size <= slotIndex && mutableRefs.size < COMBAT_PERFORM_SLOT_COUNT) {
        mutableRefs += ""
    }
    if (slotIndex in 0 until COMBAT_PERFORM_SLOT_COUNT) {
        mutableRefs[slotIndex] = actionRef
    }
    val nextAssignedRefs = mutableRefs.take(COMBAT_PERFORM_SLOT_COUNT).dedupeCombatActionSlots()
    return nextAssignedRefs to (removedRefs - actionRef)
}

internal fun findSkillInfoForBreakthrough(
    skillId: String,
    latestSkillInfo: SkillInfoPacket?,
): SkillInfoPacket? =
    latestSkillInfo?.takeIf { info ->
        info.id == skillId || info.name == skillId
    }

internal fun buildSkillsPanelSheet(
    skills: List<SkillSummaryPacket>,
    categoryId: String,
    loading: Boolean,
    activeSkillSlots: Map<String, ActiveSkillSlotPacket>,
    statusSnapshot: PlayerStatusSnapshot?,
): ShellSheetState =
    buildSkillsSheetState(
        skills = skills,
        categoryId = categoryId,
        loading = loading,
        activeSkillSlots = activeSkillSlots,
        statusSnapshot = statusSnapshot,
    )

internal fun buildLoadingSkillDetailSheet(currentSheet: ShellSheetState?): ShellSheetState? =
    currentSheet?.copy(loading = true)

internal fun parseStoredCombatShortcuts(
    shortcuts: StoredCombatShortcuts,
): Pair<List<String>, Set<String>> {
    val assignedRefs = shortcuts.assignedActionRefs.padCombatPerformSlots()
    val removedRefs =
        shortcuts.removedActionRefs
            .map { it.toCombatShortcutActionRefOrBlank() }
            .filter { it.isNotBlank() }
            .toSet()
    return assignedRefs to removedRefs
}

internal fun buildEmptySkillStateTransform(
    includeSkillInfo: Boolean,
): ShellUiState.() -> ShellUiState {
    return {
        copy(
            activeSkillSlots = emptyMap(),
            latestSkillSummaries = emptyList(),
            latestSkillInfo = if (includeSkillInfo) null else latestSkillInfo,
            combatActionCooldowns = if (includeSkillInfo) emptyMap() else combatActionCooldowns,
        )
    }
}

internal fun isActiveSkillSlot(slot: String): Boolean = slot in ACTIVE_SKILL_SLOTS

internal fun buildSheetReplacementTransform(
    sheetState: ShellSheetState?,
): ShellUiState.() -> ShellUiState {
    return {
        copy(
            sheet = sheetState,
            dialog = null,
            exitOverlay = null,
        )
    }
}

internal fun buildBreakthroughSheetTransform(
    packet: SkillInfoPacket,
): ShellUiState.() -> ShellUiState {
    return {
        copy(
            sheet = buildSkillBreakthroughSheetState(packet),
            dialog = null,
            exitOverlay = null,
        )
    }
}

internal fun buildSkillInfoSheetTransform(
    packet: SkillInfoPacket,
    currentSheet: ShellSheetState?,
): ShellUiState.() -> ShellUiState {
    return {
        copy(
            sheet = buildSkillInfoStateUpdate(packet, currentSheet),
            dialog = null,
        )
    }
}

internal fun PendingProtocolSheetRequest?.isSkillListRequest(): Boolean {
    return this?.command?.trim()?.lowercase() == "skills"
}

internal fun PendingProtocolSheetRequest?.matchesSkillInfoRequest(
    packet: SkillInfoPacket,
    commandResponseId: String = "",
): Boolean {
    val expectedRequestId = this?.commandRequestId.orEmpty()
    if (expectedRequestId.isNotBlank() && expectedRequestId != commandResponseId) {
        return false
    }
    val normalizedCommand = this?.command?.trim()?.lowercase().orEmpty()
    if (!normalizedCommand.startsWith("skills ")) {
        return false
    }
    val requestedSkill = normalizedCommand.removePrefix("skills ").trim()
    return requestedSkill.isNotBlank() && requestedSkill in
        setOf(
            packet.id.trim().lowercase(),
            packet.name.trim().lowercase(),
        )
}

internal fun ShellSheetState?.isSkillDomainSurface(): Boolean {
    return this != null && (
        kind == ShellSheetKind.SKILL ||
            navigation.dockSection == DockWorkspaceSection.SKILL ||
            dialogSource == SKILL_BREAKTHROUGH_DIALOG_SOURCE
    )
}
