package com.xiakexing.client.feature.shell

internal const val TRANSCRIPT_LIMIT = 240
internal const val NOTICE_FEED_LIMIT = 40
internal const val CHAT_HISTORY_LIMIT = 200

internal fun List<String>.withRetainedTranscriptLine(line: String): List<String> {
    return withRetainedItem(line, TRANSCRIPT_LIMIT)
}

internal fun List<String>.withRetainedNoticeLine(line: String): List<String> {
    return withRetainedItem(line, NOTICE_FEED_LIMIT)
}

internal fun List<ChatMessageItem>.withRetainedChatMessage(
    message: ChatMessageItem,
): List<ChatMessageItem> {
    return withRetainedItem(message, CHAT_HISTORY_LIMIT)
}

private fun <T> List<T>.withRetainedItem(
    item: T,
    limit: Int,
): List<T> {
    if (limit <= 0) {
        return emptyList()
    }
    val retainedStart = (size - limit + 1).coerceAtLeast(0)
    return buildList(capacity = minOf(size + 1, limit)) {
        for (index in retainedStart until this@withRetainedItem.size) {
            add(this@withRetainedItem[index])
        }
        add(item)
    }
}
