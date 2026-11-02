package com.xiakexing.client.core.protocol.payload

import com.google.gson.JsonObject
import com.xiakexing.client.core.protocol.AnsiSanitizer

data class MessageEventScopePacket(
    val type: String,
    val id: String,
) {
    companion object {
        fun fromJson(payload: JsonObject): MessageEventScopePacket {
            return MessageEventScopePacket(
                type = payload.payloadString("type").ifBlank { "player" },
                id = payload.payloadString("id"),
            )
        }
    }
}

data class InlineAction(
    val label: String,
    val cmd: String,
) {
    companion object {
        fun fromJson(element: JsonObject): InlineAction {
            return InlineAction(
                label = element.payloadString("label"),
                cmd = element.payloadString("cmd"),
            )
        }
    }
}

data class MessageEventPacket(
    val id: String,
    val schemaVersion: Int,
    val seq: Long,
    val scope: MessageEventScopePacket,
    val causationId: String,
    val correlationId: String,
    val channel: String,
    val intent: String,
    val priority: String,
    val reliability: String,
    val displayMode: String,
    val ttlMs: Long,
    val collapseKey: String,
    val text: String,
    val inlineActions: List<InlineAction>,
    val payload: JsonObject,
    val timestamp: Long,
) {
    companion object {
        fun fromJson(payload: JsonObject): MessageEventPacket {
            val inlineActions = mutableListOf<InlineAction>()
            if (payload.has("inline_actions") && payload.get("inline_actions").isJsonArray) {
                payload.getAsJsonArray("inline_actions").forEach { el ->
                    if (el.isJsonObject) {
                        runCatching { inlineActions.add(InlineAction.fromJson(el.asJsonObject)) }
                    }
                }
            }
            return MessageEventPacket(
                id = payload.payloadString("id"),
                schemaVersion = payload.payloadInt("schema_version", 1),
                seq = payload.payloadLong("seq"),
                scope = MessageEventScopePacket.fromJson(payload.payloadObject("scope")),
                causationId = payload.payloadString("causation_id"),
                correlationId = payload.payloadString("correlation_id"),
                channel = payload.payloadString("channel").ifBlank { "main" },
                intent = payload.payloadString("intent").ifBlank { "append" },
                priority = payload.payloadString("priority").ifBlank { "normal" },
                reliability = payload.payloadString("reliability").ifBlank { "important" },
                displayMode = payload.payloadString("display_mode").ifBlank { "paced" },
                ttlMs = payload.payloadLong("ttl_ms", 30_000L),
                collapseKey = payload.payloadString("collapse_key"),
                text = AnsiSanitizer.sanitize(payload.payloadString("text")),
                inlineActions = inlineActions,
                payload = payload.payloadObject("payload"),
                timestamp = payload.payloadLong("timestamp"),
            )
        }
    }
}

data class SystemPacket(
    val content: String,
    val level: String,
) {
    companion object {
        fun fromJson(payload: JsonObject): SystemPacket {
            return SystemPacket(
                content = payload.string("content"),
                level = payload.string("level"),
            )
        }
    }
}

data class ChatPacket(
    val content: String,
    val channel: String,
    val channelName: String,
    val senderName: String,
    val direction: String,
    val timestamp: Long,
    val targetName: String,
) {
    companion object {
        fun fromJson(payload: JsonObject): ChatPacket {
            return ChatPacket(
                content = payload.string("content"),
                channel = payload.string("channel"),
                channelName = payload.string("channel_name"),
                senderName = payload.string("sender_name"),
                direction = payload.string("direction"),
                timestamp = payload.long("timestamp"),
                targetName = payload.string("target_name"),
            )
        }
    }
}

data class PromptPacket(
    val prompt: String,
) {
    companion object {
        fun fromJson(payload: JsonObject): PromptPacket {
            return PromptPacket(
                prompt =
                    payload.string("prompt")
                        .ifBlank { payload.string("content") }
                        .ifBlank { payload.string("text") },
            )
        }
    }
}

data class TextPacket(
    val text: String,
    val channel: String,
    val mode: String,
    val done: Boolean,
) {
    companion object {
        fun fromJson(payload: JsonObject): TextPacket {
            return TextPacket(
                text = payload.string("text").ifBlank { payload.string("content") },
                channel = payload.string("channel").ifBlank { "main" },
                mode = payload.string("mode").ifBlank { "append" },
                done = payload.boolean("done", default = true),
            )
        }
    }
}

private fun JsonObject.string(key: String): String {
    return if (has(key) && !get(key).isJsonNull) get(key).asString else ""
}

private fun JsonObject.boolean(
    key: String,
    default: Boolean,
): Boolean {
    if (!has(key) || get(key).isJsonNull || !get(key).isJsonPrimitive) {
        return default
    }
    return runCatching { get(key).asBoolean }.getOrDefault(default)
}

private fun JsonObject.long(key: String): Long {
    return if (has(key) && !get(key).isJsonNull) {
        runCatching {
            get(key).asLong
        }.getOrDefault(0L)
    } else {
        0L
    }
}
