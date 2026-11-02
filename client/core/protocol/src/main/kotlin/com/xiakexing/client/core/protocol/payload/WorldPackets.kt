package com.xiakexing.client.core.protocol.payload

import com.google.gson.JsonArray
import com.google.gson.JsonObject

data class RoomTitleSnapshot(
    val roomId: String,
    val title: String,
    val zone: String,
    val instanceKind: String = "",
    val dungeonType: String = "",
    val dungeonGate: String = "",
) {
    companion object {
        fun fromJson(payload: JsonObject): RoomTitleSnapshot {
            return RoomTitleSnapshot(
                roomId = payload.string("room_id"),
                title = payload.string("title"),
                zone = payload.string("zone"),
                instanceKind = payload.string("instance_kind"),
                dungeonType = payload.string("dungeon_type"),
                dungeonGate = payload.string("dungeon_gate"),
            )
        }
    }
}

data class ExitSnapshot(
    val direction: String,
    val name: String,
    val targetRoomId: String = "",
    val questAvailable: Boolean = false,
    val questMarkerOnly: Boolean = false,
    val availableQuestIds: List<String> = emptyList(),
) {
    companion object {
        fun listFrom(payload: JsonObject): List<ExitSnapshot> {
            return payload.array("exits").map { exit ->
                ExitSnapshot(
                    direction = exit.obj().string("direction"),
                    name = exit.obj().string("name"),
                    targetRoomId = exit.obj().string("target_room_id"),
                    questAvailable = exit.obj().bool("quest_available"),
                    questMarkerOnly = exit.obj().bool("quest_marker_only"),
                    availableQuestIds = exit.obj().stringArray("available_quest_ids"),
                )
            }
        }
    }
}

data class NamedEntity(
    val id: String,
    val name: String,
    val description: String,
    val type: String,
    val summary: String = "",
    val typeLabel: String = "",
    val questAvailable: Boolean = false,
    val questMarkerOnly: Boolean = false,
    val availableQuestIds: List<String> = emptyList(),
    val actionGroups: List<DialogActionGroupPacket> = emptyList(),
) {
    companion object {
        fun listFrom(
            payload: JsonObject,
            key: String,
        ): List<NamedEntity> {
            return payload.array(key).map { entry ->
                NamedEntity(
                    id = entry.obj().string("id"),
                    name = entry.obj().string("name"),
                    description = entry.obj().string("description"),
                    type = entry.obj().string("type"),
                    summary = entry.obj().string("summary"),
                    typeLabel = entry.obj().string("type_label"),
                    questAvailable = entry.obj().bool("quest_available"),
                    questMarkerOnly = entry.obj().bool("quest_marker_only"),
                    availableQuestIds =
                        entry.obj().stringArray("available_quest_ids")
                            .ifEmpty { entry.obj().stringArray("quest_available_ids") },
                    actionGroups = entry.obj().dialogActionGroups(),
                )
            }
        }
    }
}

private fun JsonObject.dialogActionGroups(): List<DialogActionGroupPacket> {
    return array("action_groups").map { rawGroup ->
        val group = rawGroup.obj()
        DialogActionGroupPacket(
            id = group.string("id").ifBlank { group.string("surface").ifBlank { "main" } },
            title = group.string("title"),
            surface = group.string("surface").ifBlank { group.string("id") },
            role = group.string("role"),
            actions =
                group.array("actions").map { rawAction ->
                    val action = rawAction.obj()
                    DialogActionPacket(
                        label = action.string("label"),
                        command = action.string("cmd").ifBlank { action.string("command") },
                        type = action.string("type").ifBlank { "action" },
                        kind = action.string("kind"),
                        role = action.string("role"),
                        surface = action.string("surface"),
                        targetGroup = action.string("target_group"),
                        guided = action.int("guided") == 1,
                    )
                }.filter { it.label.isNotBlank() || it.command.isNotBlank() },
        )
    }.filter { it.actions.isNotEmpty() }
}

data class InventoryItemSnapshot(
    val id: String,
    val name: String,
    val description: String,
    val type: String,
    val quantity: Int,
    val canUse: Boolean,
    val canEquip: Boolean,
    val canGive: Boolean,
    val equipped: Boolean,
    val location: String,
    val consumeAction: String,
    val summary: String = "",
    val typeLabel: String = "",
    val requiredSkillCap: Int = 0,
    val quality: Int = 1,
    // 物品身份与差异化字段：template_id 标识模板，instance_id 标识独立实例
    // （注意：与 ProjectionPackets 中房间 instance_id 不同，此处是物品实例），
    // owner_id/binding_policy/provenance/usage_modes 描述归属、绑定、来源与用途。
    val templateId: String = "",
    val instanceId: String = "",
    val ownerId: String = "",
    val bindingPolicy: String = "",
    val provenance: Map<String, String> = emptyMap(),
    val usageModes: List<String> = emptyList(),
) {
    companion object {
        fun listFrom(payload: JsonObject): List<InventoryItemSnapshot> {
            return payload.array("items").map { entry ->
                InventoryItemSnapshot(
                    id = entry.obj().string("id"),
                    name = entry.obj().string("name"),
                    description = entry.obj().string("description"),
                    type = entry.obj().string("type"),
                    quantity = entry.obj().int("quantity", 1).coerceAtLeast(1),
                    canUse = entry.obj().bool("can_use"),
                    canEquip = entry.obj().bool("can_equip"),
                    canGive = entry.obj().bool("can_give"),
                    equipped = entry.obj().bool("equipped"),
                    location = entry.obj().string("location"),
                    consumeAction = entry.obj().string("consume_action"),
                    summary = entry.obj().string("summary"),
                    typeLabel = entry.obj().string("type_label"),
                    requiredSkillCap = entry.obj().int("required_skill_cap"),
                    quality = entry.obj().int("quality", 1),
                    templateId = entry.obj().string("template_id"),
                    instanceId = entry.obj().string("instance_id"),
                    ownerId = entry.obj().string("owner_id"),
                    bindingPolicy = entry.obj().string("binding_policy"),
                    provenance = entry.obj().stringMap("provenance"),
                    usageModes = entry.obj().stringArray("usage_modes"),
                )
            }
        }
    }
}

/** 背包容量汇总：服务端 XKIVTY 投影下发，客户端以服务端为准（本地计算仅作 fallback）。
 *  容量常量唯一事实源在服务端 INVENTORY_CAPACITY；客户端字面量只保留 DEFAULT_INVENTORY_CAPACITY。 */
data class InventorySummary(
    val capacity: Int,
    val usedSlots: Int,
    val totalCount: Int,
) {
    companion object {
        fun fromJson(payload: JsonObject): InventorySummary? {
            // 0 视为缺键（服务端归一化可能合成 0），回退本地计算。
            if (payload.int("capacity", 0) <= 0) {
                return null
            }
            return InventorySummary(
                capacity = payload.int("capacity", 0).coerceAtLeast(1),
                usedSlots = payload.int("used_slots", 0).coerceAtLeast(0),
                totalCount = payload.int("total_count", 0).coerceAtLeast(0),
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

private fun JsonObject.int(
    key: String,
    default: Int = 0,
): Int {
    return payloadInt(key, default)
}

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

private fun JsonObject.stringArray(key: String): List<String> {
    return array(key).mapNotNull { element ->
        element.takeIf { it.isJsonPrimitive && !it.isJsonNull }
            ?.asString
            ?.trim()
            ?.takeIf { value -> value.isNotBlank() }
    }
}

private fun JsonObject.stringMap(key: String): Map<String, String> {
    if (!has(key) || get(key).isJsonNull || !get(key).isJsonObject) {
        return emptyMap()
    }
    return get(key).asJsonObject.entrySet().mapNotNull { entry ->
        val value = entry.value
        if (value.isJsonPrimitive && !value.isJsonNull) {
            entry.key to value.asString
        } else {
            null
        }
    }.toMap()
}

private fun com.google.gson.JsonElement.obj(): JsonObject = asJsonObject
