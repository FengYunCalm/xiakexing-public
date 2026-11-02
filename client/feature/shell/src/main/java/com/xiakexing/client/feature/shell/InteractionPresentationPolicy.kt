package com.xiakexing.client.feature.shell

import com.xiakexing.client.core.protocol.payload.DialogPacket

internal enum class InteractionPresentationTarget {
    NarrativeFeed,
    InlineActionSurface,
    InlineSheet,
    Popup,
    StructuredPanel,
}

internal fun PendingInteractionRequest?.presentationTargetForDialog(
    packet: DialogPacket,
    shouldUseInteractionShell: Boolean,
): InteractionPresentationTarget {
    val pending =
        this ?: return if (shouldUseInteractionShell) {
            InteractionPresentationTarget.InlineSheet
        } else {
            InteractionPresentationTarget.StructuredPanel
        }
    val normalizedCommand = pending.command.trim().lowercase()
    val bucket = packet.dialogBucket.trim().lowercase()
    val source = packet.source.trim().lowercase()
    return when {
        !shouldUseInteractionShell -> InteractionPresentationTarget.StructuredPanel
        pending.isNarrativeConversationRequest(
            packet,
        ) -> InteractionPresentationTarget.NarrativeFeed
        normalizedCommand.startsWith("quest ") ||
            bucket == "quest" -> InteractionPresentationTarget.StructuredPanel
        normalizedCommand.startsWith("give ") ||
            normalizedCommand.startsWith("learn ") ||
            normalizedCommand.startsWith("list") ||
            source == "look_item" -> InteractionPresentationTarget.InlineSheet
        else -> InteractionPresentationTarget.InlineSheet
    }
}

internal fun PendingInteractionRequest.isNarrativeConversationRequest(
    packet: DialogPacket? = null,
): Boolean {
    if (target.kind != InteractionObjectKind.NPC) {
        return false
    }
    val normalizedCommand = command.trim().lowercase()
    if (!normalizedCommand.startsWith("talk ") && !normalizedCommand.startsWith("ask ")) {
        return false
    }
    val bucket = packet?.dialogBucket?.trim()?.lowercase().orEmpty()
    val source = packet?.source?.trim()?.lowercase().orEmpty()
    return bucket in setOf("", "talk", "help", "intro", "conversation") ||
        source.contains("interaction")
}

internal fun ShellSheetState.isNpcNarrativeConversationSheet(): Boolean {
    return source == PopupSource.INTERACTION_OBJECT &&
        interactionTargetKind == InteractionObjectKind.NPC &&
        interactionMode == InteractionSheetMode.CONVERSATION &&
        panelMode == ShellPanelMode.NONE
}

internal fun PendingInteractionRequest.markNarrativeLinesSeen(lines: List<String>) {
    lines.mapNotNullTo(seenInteractionLines) { it.normalizeInteractionLine() }
}

internal fun String.toNarrativePlayerActionLine(
    target: InteractionObject,
    currentSheet: ShellSheetState?,
): String {
    val normalizedCommand = trim().lowercase()
    val targetTitle = target.title.ifBlank { "对方" }
    return when {
        normalizedCommand.startsWith("ask ") -> {
            val topic =
                currentSheet?.allActions().orEmpty()
                    .firstOrNull {
                            action ->
                        action.command.trim().equals(trim(), ignoreCase = true)
                    }
                    ?.interactionTopicLabel()
                    .orEmpty()
                    .ifBlank { commandTopicLabel() }
            "你向${targetTitle}打听$topic"
        }
        normalizedCommand.startsWith("talk ") -> "你向${targetTitle}打听"
        else -> "你看向$targetTitle。"
    }
}

private fun ShellSheetState.allActions(): List<ShellAction> {
    return actions + actionGroups.flatMap { group -> group.actions }
}

private fun ShellAction.interactionTopicLabel(): String {
    val label =
        this.label.trim()
            .removePrefix("询问")
            .removePrefix("打听")
            .trim()
    return label.ifBlank { command.commandTopicLabel() }
}

private fun String.commandTopicLabel(): String {
    val topic = substringAfter(" about ", missingDelimiterValue = "").trim()
    return when (topic.lowercase()) {
        "road", "route" -> "路线"
        "quest" -> "任务"
        "help" -> "帮助"
        else -> topic
    }
}
