package com.xiakexing.client.core.protocol.payload

import com.google.gson.JsonArray
import com.google.gson.JsonObject

data class QuickActionEntryPacket(
    val label: String,
    val command: String,
)

data class QuickActionGroupPacket(
    val title: String,
    val actions: List<QuickActionEntryPacket>,
    val columns: Int,
    val priority: Int,
) {
    companion object {
        fun fromJson(
            payload: JsonObject,
            fallbackTitle: String = "",
        ): QuickActionGroupPacket {
            return QuickActionGroupPacket(
                title = payload.string("title").ifBlank { fallbackTitle },
                actions =
                    payload.actionEntries("actions").ifEmpty {
                        payload.actionEntries("buttons")
                    },
                columns = payload.int("cols").coerceAtLeast(1),
                priority = payload.int("priority").coerceAtLeast(1),
            )
        }
    }
}

private fun JsonObject.actionEntries(key: String): List<QuickActionEntryPacket> {
    return array(key).mapNotNull { element ->
        val entry = element.takeIf { it.isJsonObject }?.asJsonObject ?: return@mapNotNull null
        val label =
            entry.string("label")
                .ifBlank { entry.string("title") }
                .ifBlank { entry.string("text") }
                .ifBlank { entry.string("name") }
        val command =
            entry.string("cmd")
                .ifBlank { entry.string("command") }
                .ifBlank { entry.string("action") }
        if (label.isBlank() && command.isBlank()) {
            null
        } else {
            QuickActionEntryPacket(
                label = label.ifBlank { command },
                command = command,
            )
        }
    }
}

private fun JsonObject.array(key: String): JsonArray {
    return payloadArray(key)
}

private fun JsonObject.string(key: String): String {
    return payloadString(key)
}

private fun JsonObject.int(key: String): Int {
    return payloadInt(key)
}
