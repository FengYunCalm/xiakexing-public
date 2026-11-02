package com.xiakexing.client.core.protocol.payload

import com.google.gson.JsonObject

// 任务链步骤（服务端 quest_info.steps 运行态投影）。
data class QuestStepPacket(
    val desc: String = "",
    val progress: Int = 0,
    val total: Int = 0,
)

internal fun JsonObject.toQuestSteps(): List<QuestStepPacket> {
    val arr =
        if (has("steps") && get("steps").isJsonArray) {
            get("steps").asJsonArray
        } else {
            null
                ?: return emptyList()
        }
    return arr.mapNotNull { el ->
        if (!el.isJsonObject) return@mapNotNull null
        val o = el.asJsonObject
        QuestStepPacket(
            desc = o.string("desc"),
            progress = o.int("progress"),
            total = o.int("total"),
        )
    }
}

data class QuestEntryPacket(
    val questId: String,
    val sourceKind: String = "quest",
    val objectiveId: String = "",
    val acceptanceId: String = "",
    val acceptanceRevision: Long = 0L,
    val autoTrack: Boolean = false,
    val trackingPriority: Int = 0,
    val name: String,
    val status: String,
    val progress: Int,
    val total: Int,
    val providerId: String = "",
    val providerName: String = "",
    val summary: String = "",
    val rewards: QuestRewardsPacket = QuestRewardsPacket(),
    val canTurnIn: Boolean = false,
    val nextStepLabel: String = "",
    val targetRoomId: String = "",
    val targetRoomName: String = "",
    val targetEntityId: String = "",
    val targetEntityName: String = "",
    val recommendedCommand: String = "",
    val canNavigate: Boolean = false,
    val steps: List<QuestStepPacket> = emptyList(),
)

data class QuestRewardsPacket(
    val exp: Int = 0,
    val gold: Int = 0,
    val potential: Int = 0,
    val item: String = "",
    val itemCount: Int = 0,
    val title: String = "",
) {
    companion object {
        fun fromJson(payload: JsonObject): QuestRewardsPacket =
            QuestRewardsPacket(
                exp = payload.int("exp"),
                gold = payload.int("gold"),
                potential = payload.int("potential"),
                item = payload.string("item"),
                itemCount = payload.int("item_count"),
                title = payload.string("title"),
            )
    }
}

data class QuestListPacket(
    val quests: List<QuestEntryPacket>,
    val protocolVersion: String = "",
    val projection: ProjectionIdentity? = null,
    val domainRevision: Long = 0L,
) {
    val isV2: Boolean
        get() = projection?.isV2 == true

    companion object {
        fun fromJson(payload: JsonObject): QuestListPacket {
            val questsObject =
                if (payload.has("quests") && payload.get("quests").isJsonObject) {
                    payload.getAsJsonObject("quests")
                } else {
                    JsonObject()
                }
            val quests =
                questsObject.entrySet().mapNotNull { (key, value) ->
                    if (key.isBlank() || !value.isJsonObject) {
                        return@mapNotNull null
                    }
                    val quest = value.asJsonObject
                    val name = quest.string("name")
                    val status = quest.string("status")
                    val summary = quest.string("summary")
                    if (name.isBlank() && status.isBlank() && summary.isBlank()) {
                        return@mapNotNull null
                    }
                    QuestEntryPacket(
                        questId = key,
                        sourceKind = quest.string("source_kind").ifBlank { "quest" },
                        objectiveId = quest.string("objective_id").ifBlank { "legacy:quest:$key" },
                        acceptanceId =
                            quest.string(
                                "acceptance_id",
                            ).ifBlank { "legacy:quest:$key" },
                        acceptanceRevision =
                            quest.payloadLong(
                                "acceptance_revision",
                            ).coerceAtLeast(0L),
                        autoTrack =
                            if (quest.has(
                                    "auto_track",
                                )
                            ) {
                                quest.bool("auto_track")
                            } else {
                                quest.bool("can_navigate")
                            },
                        trackingPriority = quest.int("tracking_priority", 50),
                        name = name,
                        status = status,
                        progress = quest.int("progress"),
                        total = quest.int("total"),
                        providerId = quest.string("provider_id"),
                        providerName = quest.string("provider_name"),
                        summary = summary,
                        rewards = QuestRewardsPacket.fromJson(quest.obj("rewards")),
                        canTurnIn = quest.bool("can_turn_in"),
                        nextStepLabel = quest.string("next_step_label"),
                        targetRoomId = quest.string("target_room_id"),
                        targetRoomName = quest.string("target_room_name"),
                        targetEntityId = quest.string("target_entity_id"),
                        targetEntityName = quest.string("target_entity_name"),
                        recommendedCommand = quest.string("recommended_command"),
                        canNavigate = quest.bool("can_navigate"),
                        steps = quest.toQuestSteps(),
                    )
                }

            return QuestListPacket(
                quests = quests,
                protocolVersion = payload.string("protocol_version"),
                projection = ProjectionIdentity.fromJsonOrNull(payload),
                domainRevision = payload.payloadLong("domain_revision").coerceAtLeast(0L),
            )
        }
    }
}

enum class QuestDeltaOperation {
    UPSERT,
    REMOVE,
}

data class QuestUpdatePacket(
    val questId: String,
    val protocolVersion: String = "",
    val projection: ProjectionIdentity? = null,
    val domainRevision: Long = 0L,
    /** 线契约：空白 deltaId 的 V2 包是 quest info 详情刷新探针（唯一生产者
     * cmds/player/quest.c varargs 默认参数），消费端跳过去重门；带 delta 的包
     * 照常走增量去重。探针携带服务端全量真值快照，必须入账覆盖缓存。
     * 安全假设：TCP 有序送达，探针前后的进度增量按序覆盖（传输层乱序则
     * 过期探针可能回退可见数据，届时需给探针发显式标志或真 delta）。 */
    val deltaId: String = "",
    val operation: QuestDeltaOperation = QuestDeltaOperation.UPSERT,
    val sourceKind: String = "quest",
    val objectiveId: String = "",
    val acceptanceId: String = "",
    val acceptanceRevision: Long = 0L,
    val autoTrack: Boolean = false,
    val trackingPriority: Int = 0,
    val name: String = "",
    val status: String,
    val progress: Int,
    val total: Int,
    val providerId: String = "",
    val providerName: String = "",
    val summary: String = "",
    val rewards: QuestRewardsPacket = QuestRewardsPacket(),
    val canTurnIn: Boolean = false,
    val nextStepLabel: String = "",
    val targetRoomId: String = "",
    val targetRoomName: String = "",
    val targetEntityId: String = "",
    val targetEntityName: String = "",
    val recommendedCommand: String = "",
    val canNavigate: Boolean = false,
    val steps: List<QuestStepPacket> = emptyList(),
) {
    val isV2: Boolean
        get() = projection?.isV2 == true

    /** 是否 quest info 详情刷新探针（协议语义随类型走，消费端勿各自判定）。 */
    val isInfoRefreshProbe: Boolean
        get() = isV2 && deltaId.isBlank()

    companion object {
        fun fromJson(payload: JsonObject): QuestUpdatePacket {
            return QuestUpdatePacket(
                questId = payload.string("quest_id"),
                protocolVersion = payload.string("protocol_version"),
                projection = ProjectionIdentity.fromJsonOrNull(payload),
                domainRevision = payload.payloadLong("domain_revision").coerceAtLeast(0L),
                deltaId = payload.string("delta_id"),
                operation =
                    if (payload.string("operation").equals("remove", ignoreCase = true)) {
                        QuestDeltaOperation.REMOVE
                    } else {
                        QuestDeltaOperation.UPSERT
                    },
                sourceKind = payload.string("source_kind").ifBlank { "quest" },
                objectiveId =
                    payload.string("objective_id").ifBlank {
                        payload.string(
                            "quest_id",
                        ).takeIf { it.isNotBlank() }?.let { "legacy:quest:$it" }.orEmpty()
                    },
                acceptanceId =
                    payload.string("acceptance_id").ifBlank {
                        payload.string(
                            "quest_id",
                        ).takeIf { it.isNotBlank() }?.let { "legacy:quest:$it" }.orEmpty()
                    },
                acceptanceRevision =
                    payload.payloadLong(
                        "acceptance_revision",
                    ).coerceAtLeast(0L),
                autoTrack =
                    if (payload.has(
                            "auto_track",
                        )
                    ) {
                        payload.bool("auto_track")
                    } else {
                        payload.bool("can_navigate")
                    },
                trackingPriority = payload.int("tracking_priority", 50),
                name = payload.string("name"),
                status = payload.string("status"),
                progress = payload.int("progress"),
                total = payload.int("total"),
                providerId = payload.string("provider_id"),
                providerName = payload.string("provider_name"),
                summary = payload.string("summary"),
                rewards = QuestRewardsPacket.fromJson(payload.obj("rewards")),
                canTurnIn = payload.bool("can_turn_in"),
                nextStepLabel = payload.string("next_step_label"),
                targetRoomId = payload.string("target_room_id"),
                targetRoomName = payload.string("target_room_name"),
                targetEntityId = payload.string("target_entity_id"),
                targetEntityName = payload.string("target_entity_name"),
                recommendedCommand = payload.string("recommended_command"),
                canNavigate = payload.bool("can_navigate"),
                steps = payload.toQuestSteps(),
            )
        }
    }
}

private fun JsonObject.string(key: String): String = payloadString(key)

private fun JsonObject.int(
    key: String,
    default: Int = 0,
): Int = payloadInt(key, default)

private fun JsonObject.obj(key: String): JsonObject = payloadObject(key)

private fun JsonObject.bool(key: String): Boolean {
    if (!has(key) || get(key).isJsonNull) {
        return false
    }
    val value = get(key).asJsonPrimitive
    return if (value.isBoolean) value.asBoolean else payloadLong(key) != 0L
}
