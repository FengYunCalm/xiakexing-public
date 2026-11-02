package com.xiakexing.client.feature.shell

import com.google.gson.JsonElement
import com.google.gson.JsonObject
import com.xiakexing.client.core.protocol.IncomingFrame
import com.xiakexing.client.core.protocol.XkEnvelope
import com.xiakexing.client.core.protocol.XkMessageType

internal fun ShellSheetState.sheetIdentity(): String {
    return buildString {
        append(source.name)
        append('|')
        append(title)
        append('|')
        append(dialogSource)
        append('|')
        append(panelMode.name)
        if (isQuestDetailSheet()) {
            append('|')
            append(primaryAction?.command.orEmpty())
        }
    }
}

internal fun ShellPopupState.popupIdentity(): String {
    return "${source.name}|$title|$subtitle|$badge"
}

internal const val ACTION_CONFIRMATION_CANCEL_COMMAND = "ui://action-confirmation/cancel"

internal fun buildActionConfirmationPopup(
    title: String,
    action: ShellAction,
): ShellPopupState {
    return buildProtocolPopup(
        title = title,
        actions =
            listOf(
                action.copy(label = "确认"),
                ShellAction("取消", ACTION_CONFIRMATION_CANCEL_COMMAND, dismissAfterInvoke = true),
            ),
        primaryLabel = "",
        kind = ShellPopupKind.DANGER_CONFIRMATION,
    )
}

internal fun String.toPendingErrorLine(): String? {
    val normalized = normalizeInteractionLine() ?: return null
    return "错误：$normalized"
}

internal fun ShellSheetState.canCaptureTradeResult(): Boolean {
    return isTradeSessionSheet() ||
        (
            panelMode == ShellPanelMode.SHOP_GRID &&
                primaryAction?.command?.trim()?.lowercase() == "list"
        )
}

internal fun ShellSheetState.canCaptureTradeCommand(command: String): Boolean {
    return isTradeSessionSheet() && command.trim().lowercase().startsWith("trade ")
}

internal fun ShellSheetState.canCaptureQuestUpdate(questId: String = ""): Boolean {
    return primaryAction?.command?.trim()?.lowercase() == "quest" ||
        (isQuestDetailSheet() && matchesQuestInfoCommand(questId))
}

internal fun ShellSheetState.canCapturePrimaryCommandResult(command: String): Boolean {
    if (source == PopupSource.INTERACTION_OBJECT) {
        return false
    }

    val normalizedCommand = command.trim().lowercase()
    if (dialogSource == BANK_DIALOG_SOURCE && normalizedCommand.startsWith("bank")) {
        return true
    }
    if (isQuestDetailSheet() && normalizedCommand.isQuestInfoCommand()) {
        return true
    }
    return when (normalizedCommand) {
        "list" -> panelMode == ShellPanelMode.SHOP_GRID
        "quest" -> primaryAction?.command?.trim()?.lowercase() == "quest"
        "dungeon status" -> primaryAction?.command?.trim()?.lowercase() == "dungeon status"
        else -> false
    }
}

internal fun ShellSheetState.canCaptureProtocolDialogCommand(command: String): Boolean {
    if (source != PopupSource.PROTOCOL) {
        return false
    }

    val normalizedDialogSource = dialogSource.trim().lowercase()
    val normalizedCommand = command.trim().lowercase()
    if (normalizedCommand.shouldCaptureProtocolUtilityCommand()) {
        return true
    }
    if (normalizedDialogSource.isNpcDialogSource()) {
        return normalizedCommand.isLookInteractionCommand() ||
            normalizedCommand.startsWith("talk ") ||
            normalizedCommand.startsWith("ask ") ||
            normalizedCommand.startsWith("learn ") ||
            normalizedCommand.startsWith("quest") ||
            normalizedCommand == "list"
    }

    return when (normalizedDialogSource) {
        "mail" -> normalizedCommand.startsWith("mail")
        "team" -> normalizedCommand.startsWith("team")
        "guild" -> normalizedCommand.startsWith("guild")
        "rank" -> normalizedCommand.startsWith("rank")
        "dungeon" -> normalizedCommand.startsWith("dungeon")
        BANK_DIALOG_SOURCE -> normalizedCommand.startsWith("bank")
        "who" -> normalizedCommand == "who" || normalizedCommand.startsWith("who ")
        "time" -> normalizedCommand == "time"
        "status" -> normalizedCommand == "status"
        "score" -> normalizedCommand == "score"
        "save" -> normalizedCommand == "save"
        else -> false
    }
}

internal fun ShellSheetState.canCaptureInfoSheetAction(command: String): Boolean {
    return source != PopupSource.INTERACTION_OBJECT &&
        panelMode == ShellPanelMode.INFO_LIST &&
        title in capturableInfoSheetTitles &&
        !command.needsConfirmation() &&
        command.shouldCaptureInfoSheetResult(currentSheet = this)
}

internal fun shouldKeepSheetCommandResult(
    currentSheet: ShellSheetState?,
    item: ShellSheetItem,
    command: String,
): Boolean {
    if (currentSheet == null || currentSheet.source == PopupSource.INTERACTION_OBJECT) {
        return false
    }
    if (!item.dismissAfterInvoke || currentSheet.panelMode != ShellPanelMode.INFO_LIST) {
        return false
    }
    if (currentSheet.dialogSource == SKILL_BREAKTHROUGH_DIALOG_SOURCE) {
        return false
    }
    if (currentSheet.dialogSource == "mail" && command.trim().lowercase().startsWith("mail")) {
        return true
    }
    return command.shouldCaptureInfoSheetResult(currentSheet) && currentSheet.title in capturableInfoSheetTitles
}

internal fun shouldKeepPopupCommandResult(
    currentDialog: ShellPopupState?,
    command: String,
): Boolean {
    return currentDialog?.source == PopupSource.DOCK_BUTTON &&
        command.trim().lowercase().startsWith("look ")
}

internal fun String.shouldCaptureInfoSheetResult(currentSheet: ShellSheetState? = null): Boolean {
    val normalized = trim().lowercase()
    return normalized.isQuestSheetCommand(currentSheet) ||
        normalized.startsWith("team") ||
        normalized.startsWith("guild") ||
        normalized.startsWith("rank") ||
        normalized.startsWith("dungeon") ||
        normalized.startsWith("follow ") ||
        normalized.startsWith("growth") ||
        normalized.startsWith("reputation") ||
        normalized.startsWith("breakthrough ") ||
        normalized == "mail" ||
        normalized.startsWith("mail ") ||
        normalized == "bank" ||
        normalized.startsWith("bank ") ||
        normalized == "who" ||
        normalized.startsWith("who ") ||
        normalized == "time" ||
        normalized == "save" ||
        normalized == "score" ||
        normalized == "status" ||
        normalized.startsWith("help ")
}

internal fun String.shouldCaptureProtocolUtilityCommand(): Boolean {
    val normalized = trim().lowercase()
    return normalized == "who" ||
        normalized.startsWith("who ") ||
        normalized == "time" ||
        normalized == "status" ||
        normalized == "score" ||
        normalized == "save" ||
        normalized.startsWith("follow ") ||
        normalized.startsWith("help ")
}

internal fun ShellAction.isDropRemovalCommand(): Boolean =
    command.trim().lowercase().startsWith("drop ")

internal fun String.needsConfirmation(): Boolean {
    val normalized = trim().lowercase()
    return normalized.startsWith("drop ") ||
        normalized.startsWith("kill ") ||
        normalized == "guild leave" ||
        normalized == "guild dismiss" ||
        normalized == "team leave" ||
        normalized == "team dismiss" ||
        normalized.startsWith("team kick") ||
        normalized.startsWith("quest abandon ")
}

internal fun String.confirmationTitle(): String {
    val normalized = trim().lowercase()
    return when {
        normalized.startsWith("drop ") -> "确认丢弃"
        normalized.startsWith("kill ") -> "确认攻击"
        normalized == "guild leave" -> "确认退帮"
        normalized == "guild dismiss" -> "确认解散帮派"
        normalized == "team leave" -> "确认离队"
        normalized == "team dismiss" -> "确认解散队伍"
        normalized.startsWith("team kick") -> "确认移出队员"
        normalized.startsWith("quest abandon ") -> "确认放弃任务"
        else -> "确认操作"
    }
}

internal fun prependSheetFeedbackLine(
    existing: List<String>,
    feedbackLine: String,
    maxLines: Int = 4,
): List<String> {
    val normalizedFeedback = feedbackLine.normalizeInteractionLine() ?: return existing
    val preservedLines =
        existing
            .mapNotNull { it.normalizeInteractionLine() }
            .filterNot { it == normalizedFeedback }
            .take((maxLines - 1).coerceAtLeast(0))
    return listOf(normalizedFeedback) + preservedLines
}

internal fun JsonObject.shellString(key: String): String {
    return if (has(key) && !get(key).isJsonNull) get(key).asString else ""
}

internal fun JsonObject.objectOrNull(key: String): JsonObject? {
    return get(key)?.takeIf { it.isJsonObject }?.asJsonObject
}

internal fun JsonElement.toBatchFrameOrNull(): IncomingFrame.Packet? {
    if (!isJsonObject) {
        return null
    }

    val envelope = asJsonObject
    val rawType = envelope.batchTypeCode()
    if (rawType.length != 4) {
        return null
    }

    val payload = envelope.batchPayload()
    return IncomingFrame.Packet(
        XkEnvelope(
            rawType = rawType,
            type = XkMessageType.fromCode(rawType),
            payload = payload,
            meta = envelope.batchChildMeta(payload),
        ),
    )
}

internal fun JsonObject.debugSummary(limit: Int = 120): String {
    val compact = toString().replace(Regex("\\s+"), " ").trim()
    if (compact.isBlank()) {
        return ""
    }
    return if (compact.length <= limit) compact else compact.take(limit - 1) + "…"
}

internal fun ShellSheetState.withCapturedProtocolLines(
    command: String,
    lines: List<String>,
    loading: Boolean,
): ShellSheetState {
    val normalizedCommand = command.trim().lowercase()
    val normalizedLines = lines.mapNotNull { it.normalizeInteractionLine() }
    if (isQuestDetailSheet() && normalizedCommand.isQuestInfoCommand()) {
        // 结构化包唯一胜出，服务端文本行不并入（双生产者收口之二）。
        return copy(loading = loading)
    }
    if (isQuestDetailSheet() && normalizedCommand.isLookInteractionCommand()) {
        return copy(loading = loading)
    }
    if (!isDungeonSheet(normalizedCommand)) {
        return copy(detailLines = normalizedLines, loading = loading)
    }

    return when {
        normalizedCommand.startsWith(
            "dungeon list",
        ) -> withDungeonListLines(normalizedLines, loading)
        normalizedCommand.startsWith(
            "dungeon status",
        ) -> withDungeonStatusLines(normalizedLines, loading)
        normalizedCommand.startsWith(
            "dungeon info",
        ) -> withDungeonInfoLines(command, normalizedLines, loading)
        else ->
            copy(
                subtitle = "",
                dialogSource = "dungeon",
                panelStats = emptyList(),
                detailLines = normalizedLines,
                items = dungeonPanelCommandItems(),
                actions = emptyList(),
                primaryAction = null,
                loading = loading,
            )
    }
}

private fun ShellSheetState.isDungeonSheet(command: String): Boolean {
    return resolvedSheetKind() == ShellSheetKind.DUNGEON || command.startsWith("dungeon")
}

private fun ShellSheetState.withDungeonListLines(
    lines: List<String>,
    loading: Boolean,
): ShellSheetState {
    val dungeonItems = lines.mapNotNull { line -> line.toDungeonListItemOrNull() }
    return copy(
        subtitle = "",
        dialogSource = "dungeon",
        panelStats = emptyList(),
        detailLines =
            if (dungeonItems.isEmpty()) {
                lines.filterNot {
                    it.startsWith("可进入的副本")
                }
            } else {
                emptyList()
            },
        items = dungeonItems.ifEmpty { dungeonPanelCommandItems() },
        actions = emptyList(),
        primaryAction = null,
        loading = loading,
    )
}

private fun ShellSheetState.withDungeonStatusLines(
    lines: List<String>,
    loading: Boolean,
): ShellSheetState {
    return copy(
        subtitle = "",
        dialogSource = "dungeon",
        panelStats = emptyList(),
        detailLines = lines,
        items = dungeonPanelCommandItems(),
        actions = emptyList(),
        primaryAction = null,
        loading = loading,
    )
}

private fun ShellSheetState.withDungeonInfoLines(
    command: String,
    lines: List<String>,
    loading: Boolean,
): ShellSheetState {
    val fields = lines.toLabelValueMap()
    val commandType = command.substringAfter("dungeon info", missingDelimiterValue = "").trim()
    val dungeonType = fields["类型"] ?: commandType
    val dungeonName = fields["名称"] ?: dungeonType.ifBlank { "副本信息" }
    val infoItems =
        if (dungeonType.isBlank()) {
            dungeonPanelCommandItems()
        } else {
            listOf(
                ShellSheetItem(
                    "进入$dungeonName",
                    command = "dungeon enter $dungeonType",
                    dismissAfterInvoke = true,
                ),
            )
        }
    return copy(
        subtitle = "",
        dialogSource = "dungeon",
        panelStats = emptyList(),
        detailLines = lines,
        items = infoItems,
        actions = emptyList(),
        primaryAction = null,
        loading = loading,
    )
}

private fun String.toDungeonListItemOrNull(): ShellSheetItem? {
    val match = Regex("^-\\s*(.+?)（(.+?)）：(.+)$").find(trim()) ?: return null
    val dungeonType = match.groupValues[1].trim()
    val dungeonName = match.groupValues[2].trim().ifBlank { dungeonType }
    val status = match.groupValues[3].trim()
    if (dungeonType.isBlank()) {
        return null
    }
    return ShellSheetItem(
        title = dungeonName,
        trailing = status,
        command = "dungeon enter $dungeonType",
        dismissAfterInvoke = true,
    )
}

private fun List<String>.toLabelValueMap(): Map<String, String> {
    return mapNotNull { line ->
        val separatorIndex = line.indexOf('：').takeIf { it >= 0 } ?: line.indexOf(':')
        if (separatorIndex <= 0 || separatorIndex >= line.lastIndex) {
            null
        } else {
            line.take(separatorIndex).trim() to line.substring(separatorIndex + 1).trim()
        }
    }.toMap()
}

private fun String.isQuestSheetCommand(currentSheet: ShellSheetState? = null): Boolean {
    val normalized = trim().lowercase()
    if (normalized == "quest" || normalized.startsWith("quest info ")) {
        return true
    }
    if (currentSheet?.dialogSource != "quest") {
        return false
    }
    return normalized.isQuestAcceptCommand() ||
        normalized.startsWith("quest complete ") ||
        normalized.startsWith("quest abandon ") ||
        normalized.startsWith("quest recover ")
}

private val capturableInfoSheetTitles =
    setOf(
        "邮件",
        "邮箱",
        "未读邮件",
        "队伍",
        "帮会",
        "任务",
        "排行",
        "副本",
        "突破选择",
        "功能",
    )

internal val persistentInfoSheetTitles =
    setOf(
        "设置",
        "突破选择",
    )

private fun dungeonPanelCommandItems(): List<ShellSheetItem> {
    return listOf(
        ShellSheetItem(
            "进入$WRECK_HOLD_DUNGEON_NAME",
            command = WRECK_HOLD_ENTER_COMMAND,
            dismissAfterInvoke = true,
        ),
        ShellSheetItem("副本列表", command = "dungeon list", dismissAfterInvoke = true),
        ShellSheetItem("当前状态", command = "dungeon status", dismissAfterInvoke = true),
    )
}
