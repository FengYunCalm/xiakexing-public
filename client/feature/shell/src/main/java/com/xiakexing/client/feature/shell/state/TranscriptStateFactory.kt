package com.xiakexing.client.feature.shell

import androidx.compose.runtime.Immutable

@Immutable
data class TranscriptLine(
    val key: String,
    val text: String,
    /** 同一协议文本块的身份，用于排版段落节奏；不从正文内容猜测段落。 */
    val blockKey: String = key.substringBefore(':'),
    /** 任务或 NPC 对话反馈块：所有行共用强调色，不按正文逐行推断。 */
    val isFeedbackBlock: Boolean = false,
    /** 块边界分隔行（服务端 `-----`）：渲染为整行宽度的横线，不显示文本。 */
    val isDivider: Boolean = false,
)

internal fun buildVisibleTranscript(transcript: List<String>): List<TranscriptLine> {
    val lines = mutableListOf<TranscriptLine>()
    transcript.forEachIndexed { chunkIndex, chunk ->
        lines += buildVisibleTranscriptChunk(chunk, chunkIndex.toLong())
    }
    return lines
}

internal fun appendVisibleTranscript(
    visibleTranscript: List<TranscriptLine>,
    transcript: List<String>,
    appendedLine: String,
    transcriptLimit: Int,
): List<TranscriptLine> {
    val appendedChunkIndex =
        visibleTranscript.lastOrNull()
            ?.key
            ?.substringBefore(':')
            ?.toLongOrNull()
            ?.plus(1)
            ?: transcript.size.toLong()
    val appendedVisibleLines = buildVisibleTranscriptChunk(appendedLine, appendedChunkIndex)
    if (transcript.size < transcriptLimit) {
        return buildList(visibleTranscript.size + appendedVisibleLines.size) {
            addAll(visibleTranscript)
            addAll(appendedVisibleLines)
        }
    }

    val droppedChunk = transcript.firstOrNull().orEmpty()
    val droppedVisibleLineCount = buildVisibleTranscriptChunk(droppedChunk, 0).size
    val retainedStart = droppedVisibleLineCount.coerceAtMost(visibleTranscript.size)
    return buildList(visibleTranscript.size - retainedStart + appendedVisibleLines.size) {
        for (index in retainedStart until visibleTranscript.size) {
            add(visibleTranscript[index])
        }
        addAll(appendedVisibleLines)
    }
}

internal fun formatNpcDialogueBlock(lines: List<String>): String {
    val body = lines.flatMap { it.lines() }.filter { it.isNotBlank() }
    if (body.isEmpty()) return ""
    if (body.first().isFeedbackDividerLine() && body.last().isFeedbackDividerLine()) {
        return body.joinToString("\n")
    }
    return (listOf("-----") + body + "-----").joinToString("\n")
}

private fun buildVisibleTranscriptChunk(
    chunk: String,
    chunkIndex: Long,
): List<TranscriptLine> {
    val chunkLines = chunk.lineSequence().map { it.trimEnd() }.toList()
    val isFeedbackBlock =
        chunkLines.any { it.isFeedbackDividerLine() || it.trimStart().startsWith(QUEST_BLOCK_MARKER) }
    return buildList {
        chunkLines.forEachIndexed { lineIndex, text ->
            if (text.isBlank() ||
                text.isPromptOnlyTranscriptLine() ||
                text.startsWith(">>") ||
                text.matches(QUEST_ID_TRANSCRIPT_LINE_REGEX)
            ) {
                return@forEachIndexed
            }
            add(
                TranscriptLine(
                    key = "$chunkIndex:$lineIndex:${text.hashCode()}",
                    text = text,
                    blockKey = chunkIndex.toString(),
                    isFeedbackBlock = isFeedbackBlock,
                    isDivider = text.isFeedbackDividerLine(),
                ),
            )
        }
    }
}

private fun String.isPromptOnlyTranscriptLine(): Boolean {
    val normalized = trim()
    return normalized == ">" || normalized.matches(PROMPT_ONLY_TRANSCRIPT_REGEX)
}

/** 反馈块边界标记行（`-----`）渲染成整行宽度的横线。 */
private fun String.isFeedbackDividerLine(): Boolean = trim().matches(FEEDBACK_DIVIDER_REGEX)

private val PROMPT_ONLY_TRANSCRIPT_REGEX = Regex("^[^\\s]{0,12}>$")

/** `[id] 名称` 形态的任务条目行（dialog 清洗与 transcript 构建共用同一判据，
 * 两份内联正则会漂移）。只过滤 ID 形态，不误伤正文标记行。 */
internal val QUEST_ID_TRANSCRIPT_LINE_REGEX = Regex("^\\[[A-Za-z0-9_-]+]\\s+.*")

/** 任务反馈块的标记前缀（服务端历史形态 [任务] 文本块首行）。 */
private const val QUEST_BLOCK_MARKER = "[任务]"

private val FEEDBACK_DIVIDER_REGEX = Regex("^-{3,}$")
