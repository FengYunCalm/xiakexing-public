package com.xiakexing.client.core.protocol.payload

import com.google.gson.JsonArray
import com.google.gson.JsonObject

data class CombatOpponentSnapshot(
    val name: String,
    val hp: Long,
    val maxHp: Long,
    val mp: Long,
    val maxMp: Long,
    val ep: Long,
    val maxEp: Long,
) {
    companion object {
        fun fromJson(payload: JsonObject): CombatOpponentSnapshot {
            return CombatOpponentSnapshot(
                name = payload.string("name").ifBlank { payload.string("opponent") },
                hp = payload.payloadLong("hp", -1L),
                maxHp = payload.payloadLong("max_hp", -1L),
                mp = payload.payloadLong("mp", -1L),
                maxMp = payload.payloadLong("max_mp", -1L),
                ep = payload.payloadLong("ep", -1L),
                maxEp = payload.payloadLong("max_ep", -1L),
            )
        }
    }
}

data class CombatStartPacket(
    val combatId: String,
    val opponent: String,
    val killMode: Boolean,
    val opponentHp: Long,
    val opponentMaxHp: Long,
    val opponentMp: Long,
    val opponentMaxMp: Long,
    val opponentEp: Long,
    val opponentMaxEp: Long,
    val message: String,
    val detailMessage: String,
    val opponents: List<CombatOpponentSnapshot> = emptyList(),
) {
    companion object {
        fun fromJson(payload: JsonObject): CombatStartPacket {
            return CombatStartPacket(
                combatId = payload.string("combat_id"),
                opponent = payload.string("opponent"),
                killMode = payload.bool("kill_mode"),
                opponentHp = payload.payloadLong("opponent_hp", -1L),
                opponentMaxHp = payload.payloadLong("opponent_max_hp", -1L),
                opponentMp = payload.payloadLong("opponent_mp", -1L),
                opponentMaxMp = payload.payloadLong("opponent_max_mp", -1L),
                opponentEp = payload.payloadLong("opponent_ep", -1L),
                opponentMaxEp = payload.payloadLong("opponent_max_ep", -1L),
                message = payload.string("message"),
                detailMessage = payload.string("detail_message"),
                opponents = payload.objectArray("opponents").map(CombatOpponentSnapshot::fromJson),
            )
        }
    }
}

data class CombatDamagePacket(
    val event: String,
    val source: String,
    val target: String,
    val damage: Long,
    val targetHp: Long,
    val targetMaxHp: Long,
    val targetMp: Long,
    val targetMaxMp: Long,
    val targetEp: Long,
    val targetMaxEp: Long,
    val message: String,
    val detailMessage: String,
    val skillName: String,
    val actionName: String,
    val effectName: String,
    val resultLabel: String,
    val resourceCosts: Map<String, Long>,
    val effects: Map<String, Long>,
    val statusEffects: List<JsonObject>,
    val statuses: List<JsonObject>,
) {
    companion object {
        fun fromJson(payload: JsonObject): CombatDamagePacket {
            return CombatDamagePacket(
                event = payload.string("event"),
                source = payload.string("source"),
                target = payload.string("target"),
                damage = payload.payloadLong("damage"),
                targetHp = payload.payloadLong("target_hp"),
                targetMaxHp = payload.payloadLong("target_max_hp", -1L),
                targetMp = payload.payloadLong("target_mp", -1L),
                targetMaxMp = payload.payloadLong("target_max_mp", -1L),
                targetEp = payload.payloadLong("target_ep", -1L),
                targetMaxEp = payload.payloadLong("target_max_ep", -1L),
                message = payload.string("message"),
                detailMessage = payload.string("detail_message"),
                skillName = payload.string("skill_name"),
                actionName = payload.string("action_name"),
                effectName = payload.string("effect_name"),
                resultLabel = payload.string("result_label"),
                resourceCosts = payload.payloadLongMap("resource_costs"),
                effects = payload.payloadLongMap("effects"),
                statusEffects = payload.objectArray("status_effects"),
                statuses = payload.objectArray("statuses"),
            )
        }
    }
}

data class CombatRoundPacket(
    val round: Int,
    val opponent: String,
    val event: String,
    val source: String,
    val target: String,
    val damage: Long,
    val message: String,
    val detailMessage: String,
    val resultLabel: String,
    val statusEffects: List<JsonObject>,
    val statuses: List<JsonObject>,
) {
    companion object {
        fun fromJson(payload: JsonObject): CombatRoundPacket {
            return CombatRoundPacket(
                round = payload.payloadInt("round"),
                opponent = payload.string("opponent"),
                event = payload.string("event"),
                source = payload.string("source"),
                target = payload.string("target"),
                damage = payload.payloadLong("damage"),
                message = payload.string("message"),
                detailMessage = payload.string("detail_message"),
                resultLabel = payload.string("result_label"),
                statusEffects = payload.objectArray("status_effects"),
                statuses = payload.objectArray("statuses"),
            )
        }
    }
}

data class SkillEventPacket(
    val event: String,
    val slot: String,
    val skillId: String,
    val actionId: String,
    val skillName: String,
    val actionName: String,
    val attackStyle: String,
    val damage: Long,
    val busy: Int,
    val cooldown: Int,
    val cooldownRemaining: Int,
    val intervalRemaining: Int,
    val resourceCosts: Map<String, Long>,
    val fatigueBp: Int,
    val fatigueFactorBp: Int,
    val targetPolicy: String,
    val pvpFactorBp: Int,
    val pveFactorBp: Int,
    val effects: Map<String, Long>,
    val statusEffects: List<JsonObject>,
    val statuses: List<JsonObject>,
    val justUnlocked: Boolean,
    val message: String,
    val detailMessage: String,
    val resultLabel: String,
    val category: String = "",
) {
    companion object {
        fun fromJson(payload: JsonObject): SkillEventPacket {
            return SkillEventPacket(
                event = payload.string("event"),
                slot = payload.string("slot"),
                skillId = payload.string("skill_id"),
                actionId = payload.string("action_id"),
                skillName = payload.string("skill_name"),
                actionName = payload.string("action_name"),
                attackStyle = payload.string("attack_style"),
                damage = payload.payloadLong("damage"),
                busy = payload.payloadInt("busy"),
                cooldown = payload.payloadInt("cooldown"),
                cooldownRemaining = payload.payloadInt("cooldown_remaining"),
                intervalRemaining = payload.payloadInt("interval_remaining"),
                resourceCosts = payload.payloadLongMap("resource_costs"),
                fatigueBp = payload.payloadInt("fatigue_bp"),
                fatigueFactorBp = payload.payloadInt("fatigue_factor_bp", 10000),
                targetPolicy = payload.string("target_policy"),
                pvpFactorBp = payload.payloadInt("pvp_factor_bp", 10000),
                pveFactorBp = payload.payloadInt("pve_factor_bp", 10000),
                effects = payload.payloadLongMap("effects"),
                statusEffects = payload.objectArray("status_effects"),
                statuses = payload.objectArray("statuses"),
                justUnlocked = payload.bool("just_unlocked"),
                message = payload.string("message"),
                detailMessage = payload.string("detail_message"),
                resultLabel = payload.string("result_label"),
                category = payload.string("category"),
            )
        }
    }
}

data class CombatRuntimeCooldownPacket(
    val actionType: String,
    val skillId: String,
    val actionId: String,
    val actionName: String,
    val remainingSeconds: Int,
    val totalSeconds: Int,
    val cooldownRemaining: Int,
    val intervalRemaining: Int,
) {
    companion object {
        fun fromJson(payload: JsonObject): CombatRuntimeCooldownPacket {
            return CombatRuntimeCooldownPacket(
                actionType = payload.string("action_type"),
                skillId = payload.string("skill_id"),
                actionId = payload.string("action_id"),
                actionName = payload.string("action_name"),
                remainingSeconds = payload.payloadInt("remaining_seconds"),
                totalSeconds = payload.payloadInt("total_seconds"),
                cooldownRemaining = payload.payloadInt("cooldown_remaining"),
                intervalRemaining = payload.payloadInt("interval_remaining"),
            )
        }
    }
}

data class CombatRuntimeSnapshotPacket(
    val active: Boolean,
    val combatId: String,
    val opponent: String,
    val busySeconds: Int,
    val selfStatuses: List<JsonObject>,
    val targetStatuses: List<JsonObject>,
    val cooldowns: List<CombatRuntimeCooldownPacket>,
) {
    companion object {
        fun fromJson(payload: JsonObject): CombatRuntimeSnapshotPacket {
            return CombatRuntimeSnapshotPacket(
                active = payload.bool("active"),
                combatId = payload.string("combat_id"),
                opponent = payload.string("opponent"),
                busySeconds = payload.payloadInt("busy_seconds"),
                selfStatuses = payload.objectArray("self_statuses"),
                targetStatuses = payload.objectArray("target_statuses"),
                cooldowns =
                    payload.objectArray(
                        "cooldowns",
                    ).map(CombatRuntimeCooldownPacket::fromJson),
            )
        }
    }
}

data class CombatEndPacket(
    val result: String,
    val opponent: String,
    val message: String,
    val detailMessage: String,
    val resultLabel: String,
) {
    companion object {
        fun fromJson(payload: JsonObject): CombatEndPacket {
            return CombatEndPacket(
                result = payload.string("result"),
                opponent = payload.string("opponent"),
                message = payload.string("message"),
                detailMessage = payload.string("detail_message"),
                resultLabel = payload.string("result_label"),
            )
        }
    }
}

private fun JsonObject.string(key: String): String {
    return if (has(key) && !get(key).isJsonNull) get(key).asString else ""
}

private fun JsonObject.bool(key: String): Boolean {
    if (!has(key) || get(key).isJsonNull) {
        return false
    }
    val value = get(key).asJsonPrimitive
    return if (value.isBoolean) value.asBoolean else payloadLong(key) != 0L
}

private fun JsonObject.objectArray(key: String): List<JsonObject> {
    val array = if (has(key) && get(key).isJsonArray) getAsJsonArray(key) else JsonArray()
    return array.mapNotNull { element -> element.takeIf { it.isJsonObject }?.asJsonObject }
}
