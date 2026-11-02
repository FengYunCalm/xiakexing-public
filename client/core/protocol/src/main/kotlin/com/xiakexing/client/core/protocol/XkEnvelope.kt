package com.xiakexing.client.core.protocol

import com.google.gson.JsonObject

data class XkEnvelope(
    val rawType: String,
    val type: XkMessageType?,
    val payload: JsonObject,
    val meta: XkProtocolMeta = XkProtocolMeta.fromPayload(payload),
    val commandResponseId: String =
        payload.string("command_request_id").ifBlank { meta.commandResponseId },
)

private fun JsonObject.string(key: String): String {
    return get(key)
        ?.takeUnless { it.isJsonNull }
        ?.takeIf { it.isJsonPrimitive && it.asJsonPrimitive.isString }
        ?.asString
        .orEmpty()
}

sealed interface IncomingFrame {
    data class Packet(val envelope: XkEnvelope) : IncomingFrame

    data class PlainText(val text: String) : IncomingFrame

    data class InvalidPacket(val rawType: String, val reason: String) : IncomingFrame
}
