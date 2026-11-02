package com.xiakexing.client.core.protocol.payload

import com.google.gson.JsonObject

data class DialogActionPacket(
    val label: String,
    val command: String,
    val type: String,
    val kind: String = "",
    val role: String = "",
    val surface: String = "",
    val targetGroup: String = "",
    /** 当前步骤的独立动作；固定给予入口不由该标记替代。 */
    val guided: Boolean = false,
)

data class DialogActionGroupPacket(
    val id: String,
    val title: String,
    val surface: String,
    val role: String,
    val actions: List<DialogActionPacket>,
)

data class DialogTargetPacket(
    val id: String,
    val name: String,
    val description: String,
    val type: String,
    val subType: String,
    val quantity: Int,
    val canUse: Boolean,
    val canEquip: Boolean,
    val equipped: Boolean,
    val location: String,
    val consumeAction: String,
    val templateId: String,
)

data class DialogPacket(
    val title: String,
    val content: String,
    val type: String,
    val source: String,
    val dialogId: String,
    val schemaVersion: Int,
    val seq: Int,
    val partIndex: Int,
    val partTotal: Int,
    val isFinal: Boolean,
    val replaceMode: String,
    val speakerId: String,
    val actionGroups: List<DialogActionGroupPacket> = emptyList(),
    val target: DialogTargetPacket?,
    val providerId: String = "",
    val dialogBucket: String = "",
) {
    companion object {
        fun fromJson(payload: JsonObject): DialogPacket {
            val actionGroups =
                payload.array("action_groups").map { rawGroup ->
                    val group = rawGroup.obj()
                    DialogActionGroupPacket(
                        id =
                            group.string(
                                "id",
                            ).ifBlank { group.string("surface").ifBlank { "main" } },
                        title = group.string("title"),
                        surface = group.string("surface").ifBlank { group.string("id") },
                        role = group.string("role"),
                        actions =
                            group.array("actions").map { rawAction ->
                                val button = rawAction.obj()
                                DialogActionPacket(
                                    label = button.string("label"),
                                    command =
                                        button.string(
                                            "cmd",
                                        ).ifBlank { button.string("command") },
                                    type = button.string("type").ifBlank { "action" },
                                    kind = button.string("kind"),
                                    role = button.string("role"),
                                    surface = button.string("surface"),
                                    targetGroup = button.string("target_group"),
                                    guided = button.int("guided") == 1,
                                )
                            }.filter { it.label.isNotBlank() || it.command.isNotBlank() },
                    )
                }.let { groups ->
                    if (payload.has("action_groups")) groups else payload.legacyActionsAsGroup()
                }
            val target =
                payload.objOrNull("target")?.let { rawTarget ->
                    DialogTargetPacket(
                        id = rawTarget.string("id"),
                        name = rawTarget.string("name"),
                        description = rawTarget.string("description"),
                        type = rawTarget.string("type"),
                        subType = rawTarget.string("sub_type"),
                        quantity = rawTarget.int("quantity", 1),
                        canUse = rawTarget.bool("can_use"),
                        canEquip = rawTarget.bool("can_equip"),
                        equipped = rawTarget.bool("equipped"),
                        location = rawTarget.string("location"),
                        consumeAction = rawTarget.string("consume_action"),
                        templateId = rawTarget.string("template_id"),
                    )
                }

            return DialogPacket(
                title = payload.string("title"),
                content = payload.string("content"),
                type = payload.string("type").ifBlank { "info" },
                source = payload.string("source"),
                dialogId = payload.string("dialog_id"),
                providerId = payload.string("provider_id"),
                dialogBucket = payload.string("dialog_bucket"),
                schemaVersion = payload.int("schema_version", 1),
                seq = payload.int("seq"),
                partIndex = payload.int("part_index", 1),
                partTotal = payload.int("part_total", 1),
                isFinal = payload.bool("final", true),
                replaceMode = payload.string("replace_mode").ifBlank { "replace" },
                speakerId = payload.string("speaker_id"),
                actionGroups = actionGroups,
                target = target,
            )
        }
    }
}

private fun JsonObject.legacyActionsAsGroup(): List<DialogActionGroupPacket> {
    val actions =
        array("actions").map { rawAction ->
            val button = rawAction.obj()
            DialogActionPacket(
                label = button.string("label"),
                command = button.string("cmd").ifBlank { button.string("command") },
                type = button.string("type").ifBlank { "action" },
                kind = button.string("kind"),
                role = button.string("role"),
                surface = button.string("surface"),
                targetGroup = button.string("target_group"),
                guided = button.int("guided") == 1,
            )
        }.filter { it.label.isNotBlank() || it.command.isNotBlank() }
    if (actions.isEmpty()) {
        return emptyList()
    }
    return listOf(
        DialogActionGroupPacket(
            id = "main",
            title = "操作",
            surface = "main",
            role = "",
            actions = actions,
        ),
    )
}

private fun JsonObject.array(key: String) = payloadArray(key)

private fun JsonObject.string(key: String): String = payloadString(key)

private fun JsonObject.int(
    key: String,
    default: Int = 0,
): Int = payloadInt(key, default)

private fun JsonObject.bool(
    key: String,
    default: Boolean = false,
): Boolean {
    if (!has(key) || get(key).isJsonNull) {
        return default
    }
    val value = get(key).asJsonPrimitive
    return if (value.isBoolean) value.asBoolean else payloadLong(key) != 0L
}

private fun JsonObject.objOrNull(key: String): JsonObject? =
    if (has(key) && get(key).isJsonObject) {
        getAsJsonObject(
            key,
        )
    } else {
        null
    }

private fun com.google.gson.JsonElement.obj(): JsonObject = asJsonObject
