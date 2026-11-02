package com.xiakexing.client.feature.shell

import com.xiakexing.client.feature.shell.component.isCombatExitSheet

/*
 * Surface 域纯状态变换——dismiss/sheet 重建等 SurfaceUiState 变换。
 * 编排在 SurfaceCoordinator.kt 与 *Orchestration.kt，不持有 sink。
 */

/** 方案 R-35：重建 NPC 上下文板（root 模式，六主动作 + 描述）。 */
internal fun ShellUiState.buildNpcContextResumeSheet(
    target: InteractionObject,
    hasGiveableItems: Boolean,
): ShellSheetState {
    val rootActions = target.rootActionsOrFallback().filterUsefulNpcActions(hasGiveableItems)
    val rootActionGroups =
        target.actionGroups
            .filterUsefulNpcActionGroups(hasGiveableItems)
            .ifEmpty {
                if (rootActions.isEmpty()) {
                    emptyList()
                } else {
                    listOf(ShellActionGroup("root", "主要操作", "root", rootActions))
                }
            }
    return buildInteractionObjectSheetState(
        target = target,
        detailLines = target.description.ifBlank { target.summary }.toParagraphs(),
        actions = rootActions,
        actionGroups = rootActionGroups,
    )
}

/** 战斗开始时仍保留的表面（出口浮层）判定。 */
internal fun ShellSheetState.shouldRemainVisibleDuringCombatStart(): Boolean {
    return isCombatExitSheet()
}

/** 命令面板打开变换（清 sheet/dialog，Surface 域语义）。 */
internal fun ShellUiState.showCommandPaletteState(): ShellUiState {
    return copy(
        sheet = null,
        commandPalette = CommandPaletteState(),
        dialog = null,
    )
}

/**
 * sheet 表面互斥规则（单一真源）：打开/替换/清空 sheet 时，同步关闭 dialog 与
 * 聊天面板。覆盖最常见的 `copy(sheet = …, dialog = null, chatPanelOpen = false)`
 * 组合；命令面板与出口浮层由各自规则处理，不在此处收口。
 */
internal fun ShellUiState.withSheet(sheet: ShellSheetState?): ShellUiState {
    return copy(sheet = sheet, dialog = null, chatPanelOpen = false)
}

/** 表面清理变换：命令面板/sheet/dialog/出口浮层/聊天面板一并关闭。 */
internal fun ShellUiState.dismissTransientInteractionSurfacesState(): ShellUiState {
    return if (commandPalette == null && sheet == null && dialog == null && exitOverlay == null && !chatPanelOpen) {
        this
    } else {
        copy(
            commandPalette = null,
            sheet = null,
            dialog = null,
            exitOverlay = null,
            chatPanelOpen = false,
        )
    }
}
