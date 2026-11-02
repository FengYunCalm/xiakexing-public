package com.xiakexing.client.feature.shell

import com.xiakexing.client.feature.shell.component.SKETCH_DOCK_BUTTON_TAG_PREFIX

/**
 * Shell 外壳恒定合同（单人优先）：主 Shell 的六个 Dock 槽位是唯一真源。
 *
 * 内容 profile 只允许改变面板内部数据、可用性与活动来源徽标；空状态文案、
 * label、命令入口 ID、test tag 与点击语义固定不变，
 * 任何槽位都不允许在渲染层按 playMode 分支。
 */
internal const val OPEN_ACTIVITY_PANEL_COMMAND = "ui://panel/activity"

internal data class DockSpec(
    val slot: Int,
    val label: String,
    val command: String,
    val testTag: String,
    val kind: DockSlotKind,
)

internal enum class DockSlotKind {
    SHEET,
    CHAT,
}

internal val shellDockSpecs: List<DockSpec> =
    listOf(
        DockSpec(
            1,
            "属性",
            OPEN_CHARACTER_ATTRIBUTES_COMMAND,
            "sketch-dock-button-属性",
            DockSlotKind.SHEET,
        ),
        DockSpec(
            2,
            "背包",
            OPEN_INVENTORY_PANEL_COMMAND,
            "sketch-dock-button-背包",
            DockSlotKind.SHEET,
        ),
        DockSpec(
            3,
            "战斗",
            OPEN_COMBAT_SHORTCUTS_COMMAND,
            "sketch-dock-button-战斗",
            DockSlotKind.SHEET,
        ),
        DockSpec(4, "技能", "skills", "sketch-dock-button-技能", DockSlotKind.SHEET),
        DockSpec(5, "任务", OPEN_ACTIVITY_PANEL_COMMAND, "sketch-dock-button-任务", DockSlotKind.SHEET),
        DockSpec(6, "聊天", "", "sketch-dock-button-聊天", DockSlotKind.CHAT),
    )

internal val DockSpec.testTagPrefix: String
    get() = "${SKETCH_DOCK_BUTTON_TAG_PREFIX}$label"
