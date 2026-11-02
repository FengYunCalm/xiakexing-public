package com.xiakexing.client.core.protocol

import com.google.gson.JsonObject
import com.google.gson.JsonParser

object XkPacketParser {
    private const val STANDARD_HEADER = "\u001bXK"
    private const val BRACKET_HEADER = "\u001b[XK"
    private const val TYPE_CODE_LENGTH = 4

    private val headers = listOf(STANDARD_HEADER, BRACKET_HEADER)

    fun parse(rawMessage: String): IncomingFrame {
        val header =
            headers.firstOrNull { rawMessage.startsWith(it) }
                ?: return IncomingFrame.PlainText(AnsiSanitizer.sanitize(rawMessage))

        val typeStart = header.length
        val payloadStart = typeStart + TYPE_CODE_LENGTH
        if (rawMessage.length < payloadStart) {
            return IncomingFrame.InvalidPacket(rawType = "????", reason = "missing_type")
        }

        val rawType = rawMessage.substring(typeStart, payloadStart)
        val rawPayload =
            rawMessage
                .substring(payloadStart)
                .trimEnd('\u001b', '\n', '\r')
                .trim()
        val payload =
            parsePayload(rawPayload)
                ?: return IncomingFrame.InvalidPacket(rawType = rawType, reason = "invalid_payload")

        return IncomingFrame.Packet(
            XkEnvelope(
                rawType = rawType,
                type = XkMessageType.fromCode(rawType),
                payload = payload,
            ),
        )
    }

    private fun parsePayload(rawPayload: String): JsonObject? {
        if (rawPayload.isBlank()) {
            return JsonObject()
        }

        return runCatching {
            JsonParser.parseString(rawPayload).asJsonObject
        }.getOrNull()
    }
}
