package com.xiakexing.client.feature.shell

import com.xiakexing.client.core.protocol.payload.DialogPacket

internal fun String.normalizeInteractionLine(): String? {
    val normalized =
        normalizeLiteralLineBreakEscapes()
            .lineSequence()
            .map { it.trim() }
            .joinToString(" ")
            .replace(Regex("(?:/{2,}n|\\\\+n)+\\s*$"), "")
            .replace(Regex("\\s+"), " ")
            .trim()
    return normalized.takeIf { it.isNotBlank() }
}

internal fun String.toParagraphs(): List<String> {
    return normalizeLiteralLineBreakEscapes()
        .split(Regex("\\n\\s*\\n|\\n"))
        .mapNotNull { it.normalizeInteractionLine() }
}

private fun String.normalizeLiteralLineBreakEscapes(): String {
    return replace(Regex("(?:/{2,}n|\\\\+n)"), "\n")
}

internal fun String.toCompactSayLine(currentPlayerName: String): String? {
    val match = sayLineRegex.matchEntire(trim()) ?: return null
    val speaker =
        match.groupValues[1].trim()
            .let { name -> if (name == "你") currentPlayerName else name }
            .ifBlank { currentPlayerName }
    val message = match.groupValues[2].trim()
    if (message.isBlank()) {
        return null
    }
    return "$speaker\u8bf4\u9053\uff1a$message"
}

internal fun String.shouldSuppressMainTranscriptLine(): Boolean {
    val normalized = normalizeInteractionLine() ?: return false
    return sayLineRegex.matches(normalized)
}

internal fun InteractionObject.classifyInteractionSection(
    line: String,
): InteractionResponseSection {
    if (kind == InteractionObjectKind.NPC) {
        val dialogueMarkers = listOf("说道", "告诉你", "答道", "低声道", "对你说", "向你说道", "问道", "回应你")
        if (dialogueMarkers.any { marker -> line.contains(marker) }) {
            return InteractionResponseSection.INTERACTION
        }
    }

    return InteractionResponseSection.DETAIL
}

internal fun mergeInteractionLines(
    existing: List<String>,
    incoming: List<String>,
): List<String> {
    val merged = linkedSetOf<String>()
    existing.mapNotNullTo(merged) { it.normalizeInteractionLine() }
    incoming.mapNotNullTo(merged) { it.normalizeInteractionLine() }
    return merged.toList()
}

internal fun sanitizeDialogLines(
    packet: DialogPacket,
    lines: List<String>,
): List<String> {
    if (lines.isEmpty()) {
        return lines
    }

    val isQuestDialog =
        packet.dialogBucket.equals("quest", ignoreCase = true) ||
            packet.source.contains("quest", ignoreCase = true) ||
            packet.providerId.isNotBlank()
    val isNpcDialog =
        packet.source.trim().lowercase().isNpcDialogSource() ||
            packet.speakerId.isNotBlank()
    if (!isQuestDialog && !isNpcDialog) {
        return lines
    }

    return lines.filterNot { line ->
        val normalized = line.trim()
        normalized.matches(QUEST_ID_TRANSCRIPT_LINE_REGEX) ||
            normalized.contains("<任务ID>") ||
            normalized.startsWith("任务 ID：") ||
            normalized.startsWith("使用 quest accept ") ||
            normalized.startsWith("使用 quest complete ") ||
            normalized.startsWith("使用 quest recover ") ||
            npcCommandReferenceRegex.containsMatchIn(normalized)
    }
}

private val sayLineRegex = Regex("^(.+?)(?:说道|说)[:：][「“\"]?(.*?)[」”\"]?$")

private val npcCommandReferenceRegex =
    Regex(
        pattern = "(?i)(\\b(look|go|talk|ask|quest|list|buy|sell|bank|heal|fight|kill|learn|alchemy|tailor|forge|repair|score|get|inventory|save)\\b|<[^>]+>|用法：|格式：|命令：|指令|可用操作)",
    )
