package com.xiakexing.client.feature.shell

import androidx.compose.runtime.Immutable

@Immutable
data class ShellPopupProgress(
    /** `null` 表示服务端没有提供可计算总量，使用不确定型进度条。 */
    val fraction: Float? = null,
)

@Immutable
data class ShellPopupState(
    val source: PopupSource,
    val title: String,
    val subtitle: String = "",
    val message: String = "",
    val actions: List<ShellAction> = emptyList(),
    val badge: String = "",
    val meta: List<String> = emptyList(),
    val primaryLabel: String = "",
    val inputValue: String = "",
    val inputPlaceholder: String = "",
    val inputAction: ShellAction? = null,
    val progress: ShellPopupProgress? = null,
    /** 明确弹窗交互类型，决定空白/返回/按钮层级；默认保留旧信息弹窗行为。 */
    val kind: ShellPopupKind = ShellPopupKind.INFORMATION,
    /** 二级弹窗的冻结父级；只允许一层，防止出现三级弹窗。 */
    val parent: ShellPopupState? = null,
)

enum class ShellPopupKind {
    INFORMATION,
    CONFIRMATION,
    DANGER_CONFIRMATION,
    QUANTITY_INPUT,
    TEXT_INPUT,
    UPDATE_OPTIONAL,
    UPDATE_REQUIRED,
    BLOCKING_PROGRESS,
    BLOCKING_ERROR,
}

internal fun ShellPopupState.allowsOutsideDismiss(): Boolean {
    if (parent != null) return false
    return kind in
        setOf(
            ShellPopupKind.INFORMATION,
            ShellPopupKind.CONFIRMATION,
            ShellPopupKind.UPDATE_OPTIONAL,
        )
}

internal fun ShellPopupState.allowsSystemBackDismiss(): Boolean {
    return kind != ShellPopupKind.UPDATE_REQUIRED && kind != ShellPopupKind.BLOCKING_PROGRESS
}

internal fun ShellPopupState.withParentPopup(parentPopup: ShellPopupState?): ShellPopupState {
    if (parent != null || parentPopup == null || parentPopup.parent != null) return this
    return copy(parent = parentPopup.copy(parent = null))
}

internal fun ShellPopupState.shouldDismissPopupGroupAfterAction(): Boolean {
    return parent != null && kind == ShellPopupKind.DANGER_CONFIRMATION
}

@Immutable
data class CommandPaletteState(
    val isOpen: Boolean = true,
)

@Immutable
data class ShellSheetItem(
    val title: String,
    val subtitle: String = "",
    val trailing: String = "",
    val command: String = "",
    val dismissAfterInvoke: Boolean = false,
    val actionCommand: String = "",
    val metadata: Map<String, String> = emptyMap(),
    val role: ShellSheetItemRole = ShellSheetItemRole.AUTO,
    val section: String = "",
    val selected: Boolean = false,
)

enum class ShellSheetItemRole {
    AUTO,
    INFO,
    ACTION,
    OPTION,
    NAVIGATION,
    DANGER,
}

enum class ShellSheetPresentation {
    DEFAULT,
    KEY_VALUE_GRID,
    SETTINGS,
    MANAGEMENT_LIST,
    HELP_INDEX,
}

enum class ShellSheetEmptyPresentation {
    SILENT,
    MESSAGE,
}

enum class ShellPanelMode {
    NONE,
    SHOP_GRID,
    SKILL_GRID,
    INVENTORY_GRID,
    COMBAT_GRID,
    INFO_LIST,
    MAP_ATLAS,
}

/**
 * Stable sheet identity for state updates and navigation chrome.
 *
 * [UNKNOWN] keeps older packet- and test-created sheets compatible with the
 * display-title fallback in [resolvedSheetKind].
 */
enum class ShellSheetKind {
    UNKNOWN,
    CHARACTER,
    ATTRIBUTE,
    INVENTORY,
    EQUIPMENT,
    SKILL,
    COMBAT,
    WORLD,
    ACTIVITY,
    QUEST,
    MAP,
    MAIL,
    TEAM,
    DUNGEON,
}

/** Identifies the Dock workspace that owns a sheet without relying on display text. */
enum class DockWorkspaceSection {
    ATTRIBUTE,
    INVENTORY,
    SKILL,
    ACTIVITY,
}

/** Stable local navigation state for a Dock workspace page. */
@Immutable
data class SheetNavigationMeta(
    val dockSection: DockWorkspaceSection? = null,
    val routeKey: String = "",
    val depth: Int = 0,
)

enum class SheetNavigationOperation {
    RESET,
    PUSH,
    REPLACE,
    POP,
}

@Immutable
data class SheetNavigationEntry(
    val sheet: ShellSheetState,
    val questPanelSession: QuestPanelSessionState,
)

@Immutable
data class SheetNavigationState(
    val backStack: List<SheetNavigationEntry> = emptyList(),
) {
    val canPop: Boolean
        get() = backStack.isNotEmpty()
}

@Immutable
data class ShellMapQuestMarkerDetail(
    val coord: String,
    val entryRoomId: String = "",
    val targetRoomId: String = "",
    val entryCommand: String = "",
    val entryDirection: String = "",
    val label: String = "",
    val questIds: List<String> = emptyList(),
)

@Immutable
data class ShellMapPanelContent(
    val width: Int = 0,
    val height: Int = 0,
    val rows: List<String> = emptyList(),
    val legend: List<String> = emptyList(),
    val paths: Map<String, List<String>> = emptyMap(),
    val rooms: Map<String, String> = emptyMap(),
    val questMarkers: Set<String> = emptySet(),
    val questMarkerDetails: List<ShellMapQuestMarkerDetail> = emptyList(),
    val trackedRooms: Set<String> = emptySet(),
) {
    val questMarkerCells: Set<String>
        get() = questMarkers + questMarkerDetails.mapTo(linkedSetOf()) { detail -> detail.coord }
}

@Immutable
data class ShellPanelCard(
    val id: String,
    val title: String,
    val subtitle: String = "",
    val supportingText: String = "",
    val bottomLabel: String = "",
    val badges: List<String> = emptyList(),
    val highlighted: Boolean = false,
    val command: String = "",
    val actionLabel: String = "",
    val dismissAfterInvoke: Boolean = false,
    val longClickCommand: String = "",
    val metadata: Map<String, String> = emptyMap(),
)

@Immutable
data class ShellSheetState(
    val source: PopupSource,
    val title: String,
    val subtitle: String = "",
    val message: String = "",
    val dialogId: String = "",
    val dialogSource: String = "",
    val detailLines: List<String> = emptyList(),
    val interactionLines: List<String> = emptyList(),
    val items: List<ShellSheetItem> = emptyList(),
    val actions: List<ShellAction> = emptyList(),
    val actionGroups: List<ShellActionGroup> = emptyList(),
    val activeActionGroupId: String = "root",
    val primaryAction: ShellAction? = null,
    val panelMode: ShellPanelMode = ShellPanelMode.NONE,
    val panelTabs: List<ShellAction> = emptyList(),
    val panelSelectedTabId: String = "",
    val panelCards: List<ShellPanelCard> = emptyList(),
    val panelAllCards: List<ShellPanelCard> = emptyList(),
    val panelStats: List<String> = emptyList(),
    val mapContent: ShellMapPanelContent? = null,
    val panelEmptyText: String = "暂无内容",
    val emptyPresentation: ShellSheetEmptyPresentation = ShellSheetEmptyPresentation.SILENT,
    val interactionTargetId: String = "",
    val interactionTargetUiKey: String = "",
    val interactionTargetKind: InteractionObjectKind? = null,
    val interactionMode: InteractionSheetMode = InteractionSheetMode.DETAIL,
    val compactPanel: Boolean = false,
    val loading: Boolean = false,
    val presentation: ShellSheetPresentation = ShellSheetPresentation.DEFAULT,
    val errorMessage: String = "",
    val retryAction: ShellAction? = null,
    val navigation: SheetNavigationMeta = SheetNavigationMeta(),
    val metadata: Map<String, String> = emptyMap(),
    val kind: ShellSheetKind = ShellSheetKind.UNKNOWN,
    val showHeaderTitle: Boolean = true,
    /** 无标题弹窗头部中间槽的居中信息文字（如背包容量、技能等级上限）。 */
    val headerText: String = "",
    /** 无标题且无头部信息时关闭按钮悬浮右上、内容贴顶（由各面板状态声明，如属性面板）。 */
    val floatingClose: Boolean = false,
)

/** Returns an explicit kind when available, otherwise preserves legacy title-based behavior. */
internal fun ShellSheetState.resolvedSheetKind(): ShellSheetKind {
    if (kind != ShellSheetKind.UNKNOWN) {
        return kind
    }
    return when (dialogSource.trim().lowercase()) {
        "quest" -> ShellSheetKind.QUEST
        "mail" -> ShellSheetKind.MAIL
        "dungeon" -> ShellSheetKind.DUNGEON
        else -> title.legacySheetKind()
    }
}

private fun String.legacySheetKind(): ShellSheetKind {
    return when (trim()) {
        "人物" -> ShellSheetKind.CHARACTER
        "属性" -> ShellSheetKind.ATTRIBUTE
        "背包" -> ShellSheetKind.INVENTORY
        "装备" -> ShellSheetKind.EQUIPMENT
        "技能", "武学" -> ShellSheetKind.SKILL
        "战斗" -> ShellSheetKind.COMBAT
        "江湖" -> ShellSheetKind.WORLD
        "活动" -> ShellSheetKind.ACTIVITY
        "任务" -> ShellSheetKind.QUEST
        "地图" -> ShellSheetKind.MAP
        "邮件", "邮箱", "未读邮件" -> ShellSheetKind.MAIL
        "队伍" -> ShellSheetKind.TEAM
        "副本" -> ShellSheetKind.DUNGEON
        else -> ShellSheetKind.UNKNOWN
    }
}

@Immutable
data class ExitOverlayState(
    val type: ExitOverlayType,
    val buttons: List<ShellAction> = buildExitOverlayButtons(type),
)

/** 通用「名字[数值]」标签：技能卡（等级）与背包卡（数量）共用，避免格式双源。 */
internal fun formatBracketedLabel(
    name: String,
    value: Int,
): String = "$name[$value]"
