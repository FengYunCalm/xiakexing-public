package com.xiakexing.client.core.protocol

class XkStreamDecoder {
    sealed interface Segment {
        data class RawText(val text: String) : Segment

        data class XkPacket(val text: String) : Segment

        data class InvalidPacket(val reason: String) : Segment
    }

    private val buffer = StringBuilder()
    private var bufferUtf8Bytes = 0
    private var afterPacket = false
    private var scanStart = -1
    private var scanPayloadStart = -1
    private var scanIndex = -1
    private var scanInString = false
    private var scanEscaped = false
    private var scanStack = ArrayDeque<Char>()
    private var packetScanFailure: String? = null

    /** XK 帧头标记（标准与括号两种形态）；core/network 的 MudGatewayClient 有对齐副本。 */
    val prefixes = listOf("\u001bXK", "\u001b[XK")

    @Synchronized
    fun reset() {
        buffer.setLength(0)
        bufferUtf8Bytes = 0
        afterPacket = false
        resetPacketScan()
    }

    @Synchronized
    fun feed(chunk: String): List<Segment> {
        if (chunk.isEmpty()) return emptyList()

        buffer.append(chunk)
        bufferUtf8Bytes += chunk.toByteArray(Charsets.UTF_8).size
        val segments = mutableListOf<Segment>()
        var cursor = 0

        while (cursor < buffer.length) {
            if (afterPacket) {
                cursor = skipPacketTerminators(buffer, cursor)
                if (cursor == buffer.length ||
                    (buffer[cursor] == '\u001b' && cursor + 1 == buffer.length)
                ) {
                    break
                }
                afterPacket = false
                resetPacketScan()
            }
            val prefixIndex = findNextPrefixIndex(buffer, cursor)

            if (prefixIndex < 0) {
                val pendingPrefixStart = findPendingPrefixStart(buffer, cursor)
                val rawEnd =
                    if (pendingPrefixStart >= 0) {
                        pendingPrefixStart
                    } else {
                        findRawTextCommitEnd(buffer, cursor)
                    }
                if (rawEnd > cursor) {
                    val rawSegment = buffer.substring(cursor, rawEnd)
                    segments += Segment.RawText(rawSegment)
                    cursor = rawEnd
                }
                break
            }

            if (prefixIndex > cursor) {
                segments += Segment.RawText(buffer.substring(cursor, prefixIndex))
                cursor = prefixIndex
                continue
            }

            val packetEnd = findPacketEnd(buffer, cursor)
            if (packetEnd == null) {
                if (packetScanFailure != null) {
                    segments += Segment.InvalidPacket(packetScanFailure!!)
                    val nextPrefix = findNextPrefixIndex(buffer, cursor + 1)
                    cursor = if (nextPrefix >= 0) nextPrefix else buffer.length
                    resetPacketScan()
                    afterPacket = false
                    continue
                }
                break
            }
            segments += Segment.XkPacket(buffer.substring(cursor, packetEnd))
            cursor = packetEnd
            afterPacket = true
            resetPacketScan()
        }

        if (cursor > 0) {
            bufferUtf8Bytes -= buffer.substring(0, cursor).toByteArray(Charsets.UTF_8).size
            buffer.delete(0, cursor)
        }
        if (trimOversizedBuffer()) {
            segments += Segment.InvalidPacket("decoder buffer exceeded byte budget")
        }

        return segments
    }

    /**
     * A partial authoritative frame cannot be safely tail-trimmed: doing so can
     * turn the remainder into a different valid frame or leak it as narrative.
     * Drop the incomplete stream and report a typed integrity failure instead.
     */
    private fun trimOversizedBuffer(): Boolean {
        if (bufferUtf8Bytes <= MAX_BUFFER_BYTES) {
            return false
        }
        buffer.setLength(0)
        bufferUtf8Bytes = 0
        afterPacket = false
        resetPacketScan()
        return true
    }

    private fun findNextPrefixIndex(
        text: CharSequence,
        startIndex: Int = 0,
    ): Int {
        if (prefixes.any { prefix -> text.startsWith(prefix, startIndex) }) {
            return startIndex
        }
        var bestIndex = -1
        prefixes.forEach { prefix ->
            val index = text.indexOf(prefix, startIndex)
            if (index >= 0 && (bestIndex < 0 || index < bestIndex)) {
                bestIndex = index
            }
        }
        return bestIndex
    }

    private fun findPendingPrefixStart(
        text: CharSequence,
        startIndex: Int = 0,
    ): Int {
        val start = maxOf(startIndex, text.length - MAX_PREFIX_CHARS + 1, 0)
        for (index in start until text.length) {
            val suffixLength = text.length - index
            if (prefixes.any { prefix ->
                    suffixLength < prefix.length && text.regionMatches(index, prefix, 0, suffixLength)
                }
            ) {
                return index
            }
        }
        return -1
    }

    private fun findRawTextCommitEnd(
        text: CharSequence,
        startIndex: Int = 0,
    ): Int {
        for (index in text.length - 1 downTo startIndex.coerceAtLeast(0)) {
            if (text[index] == '\n' || text[index] == '\r') {
                return index + 1
            }
        }
        val trimmed = text.subSequence(startIndex.coerceAtLeast(0), text.length).trimEnd()
        return if (trimmed.endsWith('>') || trimmed.endsWith('？') || trimmed.endsWith('?')) text.length else startIndex
    }

    private fun findPacketEnd(
        text: CharSequence,
        startIndex: Int = 0,
    ): Int? {
        val prefix = prefixes.firstOrNull { text.startsWith(it, startIndex) } ?: return null
        val payloadStart = startIndex + prefix.length + 4
        if (text.length <= payloadStart) {
            return null
        }

        if (scanStart != startIndex || scanPayloadStart != payloadStart) {
            resetPacketScan()
            scanStart = startIndex
            scanPayloadStart = payloadStart
            scanIndex = payloadStart
            val firstPayloadChar = text[payloadStart]
            if (firstPayloadChar != '{' && firstPayloadChar != '[') {
                packetScanFailure = "XK payload does not start with JSON"
                return null
            }
            scanStack.addLast(firstPayloadChar)
            scanIndex += 1
        }

        while (scanIndex < text.length) {
            val char = text[scanIndex]
            if (scanEscaped) {
                scanEscaped = false
                scanIndex += 1
                continue
            }

            if (scanInString) {
                when (char) {
                    '\\' -> scanEscaped = true
                    '"' -> scanInString = false
                }
                scanIndex += 1
                continue
            }

            when (char) {
                '"' -> scanInString = true
                '{', '[' -> scanStack.addLast(char)
                '}', ']' -> {
                    val expected = if (char == '}') '{' else '['
                    if (scanStack.removeLastOrNull() != expected) {
                        packetScanFailure = "XK JSON bracket mismatch"
                        return null
                    }
                    if (scanStack.isEmpty()) {
                        return skipPacketTerminators(text, scanIndex + 1)
                    }
                }
            }
            scanIndex += 1
        }

        return null
    }

    private fun resetPacketScan() {
        scanStart = -1
        scanPayloadStart = -1
        scanIndex = -1
        scanInString = false
        scanEscaped = false
        scanStack.clear()
        packetScanFailure = null
    }

    private fun skipPacketTerminators(
        text: CharSequence,
        startIndex: Int,
    ): Int {
        var endIndex = startIndex
        while (endIndex < text.length) {
            if (text[endIndex] !in packetTerminators) {
                break
            }
            // ESC 可能属于下一帧或 ANSI 正文；只有后接明确结束符时才消费。
            if (text[endIndex] == '\u001b' && text.getOrNull(endIndex + 1) !in packetTerminators) {
                break
            }
            endIndex++
        }
        return endIndex
    }

    private fun CharSequence.indexOf(
        pattern: String,
        startIndex: Int,
    ): Int {
        if (pattern.isEmpty()) return startIndex.coerceAtMost(length)
        val maxStart = length - pattern.length
        if (startIndex > maxStart) return -1
        for (index in startIndex.coerceAtLeast(0)..maxStart) {
            if (regionMatches(index, pattern, 0, pattern.length)) {
                return index
            }
        }
        return -1
    }

    private fun CharSequence.startsWith(
        prefix: String,
        startIndex: Int = 0,
    ): Boolean {
        return regionMatches(startIndex, prefix, 0, prefix.length)
    }

    private fun CharSequence.regionMatches(
        thisOffset: Int,
        other: String,
        otherOffset: Int,
        length: Int,
    ): Boolean {
        if (thisOffset < 0 || otherOffset < 0) return false
        if (thisOffset + length > this.length || otherOffset + length > other.length) return false
        for (index in 0 until length) {
            if (this[thisOffset + index] != other[otherOffset + index]) {
                return false
            }
        }
        return true
    }

    private companion object {
        private const val MAX_BUFFER_BYTES = 256 * 1024
        private const val MAX_PENDING_PREFIX_CHARS = 4
        private const val MAX_PREFIX_CHARS = 4
        private val packetTerminators = setOf('\u001b', '\n', '\r')
    }
}
