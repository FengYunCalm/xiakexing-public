package com.xiakexing.client.feature.shell

import com.xiakexing.client.core.protocol.payload.DialogPacket
import com.xiakexing.client.core.protocol.payload.QuestEntryPacket

/**
 * 纯协议分类谓词与命令解析：从 QuestPanelRendering.kt 抽出，零 UI 依赖。
 * 同包 internal，行为与抽取前一致（原 private 提升为 internal 以便跨文件调用）。
 */
internal fun ShellSheetState.isQuestDetailSheet(): Boolean {
    return source == PopupSource.DOCK_BUTTON &&
        dialogSource == "quest" &&
        primaryAction?.command.orEmpty().isQuestInfoCommand()
}

/** dock 侧任务面板判据。NPC 交互任务面板的 dialogSource 同为 "quest"，但两者
 * 渲染与生命周期完全不同；此处内聚 NPC 排除，调用点无需各自补
 * `!isNpcQuestInteractionSheet()`（曾靠四处手工配对，遗漏即误判）。 */
internal fun ShellSheetState.isQuestPanelSheet(): Boolean {
    val normalizedPrimary = primaryAction?.command.orEmpty().trim().lowercase()
    return (
        dialogSource == "quest" ||
            normalizedPrimary == "quest" ||
            normalizedPrimary.isQuestInfoCommand()
    ) && !isNpcQuestInteractionSheet()
}

internal fun ShellSheetState.isQuestListSheet(): Boolean {
    return isQuestPanelSheet() &&
        !isQuestDetailSheet() &&
        !isNpcQuestInteractionSheet() &&
        panelMode == ShellPanelMode.INFO_LIST &&
        items.isNotEmpty()
}

internal fun ShellSheetState.isNpcQuestInteractionSheet(): Boolean {
    return source == PopupSource.INTERACTION_OBJECT &&
        interactionTargetKind == InteractionObjectKind.NPC &&
        panelMode == ShellPanelMode.INFO_LIST &&
        activeActionGroupId.ifBlank { "root" } == "quest"
}

internal fun ShellSheetState.isNpcQuestListSheet(): Boolean {
    return isNpcQuestInteractionSheet() && items.isNotEmpty()
}

internal fun ShellSheetState.isNpcQuestDetailSheet(): Boolean {
    return isNpcQuestInteractionSheet() && items.isEmpty()
}

internal fun ShellSheetState.matchesQuestInfoCommand(questId: String): Boolean {
    val normalizedQuestId = questId.trim()
    if (normalizedQuestId.isBlank()) {
        return true
    }
    return primaryAction?.command.orEmpty().trim().equals(
        "quest info $normalizedQuestId",
        ignoreCase = true,
    )
}

internal fun String.isQuestInfoCommand(): Boolean {
    return trim().lowercase().startsWith("quest info ")
}

/** quest info 请求与应答 questId 的匹配口径（大小写不敏感，单点属主）。 */
internal fun String.isQuestInfoRequestFor(questId: String): Boolean {
    val normalized = trim().lowercase()
    return normalized.startsWith("quest info ") && normalized.questCommandId() == questId.trim()
}

/** #9：接取响应已由服务端降级为纯文本（直达主文本区），
 * 客户端对这类命令统一走直发+关弹窗，不进任何捕获会话。 */
internal fun String.isQuestAcceptCommand(): Boolean {
    return trim().lowercase().startsWith("quest accept ")
}

internal fun String.isNpcQuestMutationCommand(): Boolean {
    val normalized = trim().lowercase()
    return normalized.startsWith("quest accept ") ||
        normalized.startsWith("quest complete ") ||
        normalized.startsWith("quest recover ")
}

internal fun String.npcQuestActionLabel(): String {
    val normalized = trim().lowercase()
    return when {
        normalized.startsWith("quest accept ") -> "接取"
        normalized.startsWith("quest complete ") -> "交付"
        normalized.startsWith("quest recover ") -> "恢复"
        else -> "处理"
    }
}

internal fun String.isQuestFailureStatus(): Boolean {
    return trim().lowercase() in
        setOf(
            "失败/放弃",
            "已失败",
            "已放弃",
            "failed",
            "abandoned",
            "cancelled",
            "canceled",
        )
}

internal fun String.isQuestDeliveredStatus(): Boolean {
    return trim().lowercase() in
        setOf(
            "已完成",
            "已交付",
            "delivered",
            "finished",
            "complete",
            "completed",
        )
}

internal fun QuestEntryPacket.displayName(index: Int? = null): String {
    val normalizedName = name.trim()
    if (normalizedName.isNotBlank() &&
        normalizedName != questId &&
        !normalizedName.looksLikeInternalId()
    ) {
        return normalizedName
    }

    return index?.let { "任务 ${it + 1}" } ?: "任务"
}

internal fun String.looksLikeInternalId(): Boolean {
    val normalized = trim()
    return normalized.any { it == '_' || it == '-' } &&
        normalized.none { it.code > 127 } &&
        normalized.all { it.isLetterOrDigit() || it == '_' || it == '-' }
}

/** 可接取状态词形。服务端 provider 面板下发「可接取」；裸 `== "可接取"`
 * 曾在工作台 stats、接取门、放弃门三处分散比较，措辞或空白变化即失效。 */
internal fun String.isQuestAcceptableStatus(): Boolean {
    return trim() == "可接取"
}

internal fun String.isQuestInProgressStatus(): Boolean {
    return trim().lowercase() in
        setOf(
            "进行中",
            "in_progress",
            "in-progress",
            "active",
            "ongoing",
            "accepted",
            "已接受",
        )
}

/** 服务端 provider 任务面板帧（npc_view_d.c present_provider_panel 产生）。
 * 无对应交互会话时按冷到达规则丢弃；正常 NPC 任务查询先建立 pending 会话，
 * 再由本响应路由更新原交互弹窗。 */
internal fun DialogPacket.isProviderPanelDialog(): Boolean {
    return source.trim().lowercase() == "npc_provider_panel"
}

internal fun DialogPacket.isNpcTaskInfoDialog(): Boolean {
    val normalizedSource = source.trim().lowercase()
    val normalizedBucket = dialogBucket.trim().lowercase()
    return normalizedSource == "npc_provider_panel" ||
        normalizedSource == "npc_provider_progress" ||
        normalizedSource == "quest" ||
        normalizedBucket == "quest" ||
        normalizedBucket == "progress"
}

internal fun List<String>.isNpcTaskEmptyFeedback(): Boolean {
    val normalized = joinToString(" ").trim().lowercase()
    if (normalized.isBlank()) {
        return true
    }
    return (
        normalized.contains("没有") ||
            normalized.contains("暂无") ||
            normalized.contains("无可")
    ) && (
        normalized.contains("任务") ||
            normalized.contains("委托")
    )
}

internal fun ShellAction.isQuestFlowAction(): Boolean {
    val normalizedCommand = command.trim().lowercase()
    return normalizedCommand.startsWith("quest accept ") ||
        normalizedCommand.startsWith("quest complete ") ||
        normalizedCommand.startsWith("quest info ") ||
        normalizedCommand.startsWith("quest recover ") ||
        normalizedCommand.startsWith(TRACK_QUEST_PREFIX) ||
        normalizedCommand == UNTRACK_QUEST_COMMAND ||
        label.startsWith("任务") ||
        label.startsWith("接取") ||
        label.startsWith("交付") ||
        label.startsWith("详情") ||
        label.startsWith("恢复")
}

internal fun ShellAction.isConcreteQuestFlowAction(): Boolean {
    val normalizedCommand = command.trim().lowercase()
    return (
        normalizedCommand.startsWith("quest accept ") ||
            normalizedCommand.startsWith("quest complete ") ||
            normalizedCommand.startsWith("quest info ") ||
            normalizedCommand.startsWith("quest recover ")
    ) && command.questCommandId().isNotBlank()
}

internal fun String.questCommandId(): String {
    val parts = trim().split(Regex("\\s+"))
    return if (parts.size >= 3 && parts[0].equals("quest", ignoreCase = true)) {
        parts.drop(2).joinToString(" ")
    } else {
        ""
    }
}
