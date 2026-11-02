package com.xiakexing.client.core.protocol.payload

import com.google.gson.JsonArray
import com.google.gson.JsonObject

data class SkillListPacket(
    val skills: List<SkillSummaryPacket>,
    val activeSlots: Map<String, ActiveSkillSlotPacket>,
) {
    companion object {
        fun fromJson(payload: JsonObject): SkillListPacket {
            return SkillListPacket(
                skills = SkillSummaryPacket.listFrom(payload),
                activeSlots = ActiveSkillSlotPacket.mapFrom(payload.obj("active_slots")),
            )
        }
    }
}

data class ActiveSkillSlotPacket(
    val slot: String,
    val skillId: String,
    val skillName: String,
    val category: String = "",
) {
    companion object {
        private val supportedSlots = setOf("outer", "parry", "force", "dodge")

        fun mapFrom(payload: JsonObject): Map<String, ActiveSkillSlotPacket> {
            val result = linkedMapOf<String, ActiveSkillSlotPacket>()
            payload.entrySet().forEach { (slotId, element) ->
                val normalizedSlot = slotId.trim().lowercase()
                if (normalizedSlot in supportedSlots && element.isJsonObject) {
                    val slotPayload = element.asJsonObject
                    result[normalizedSlot] =
                        ActiveSkillSlotPacket(
                            slot = normalizedSlot,
                            skillId = slotPayload.string("skill_id"),
                            skillName = slotPayload.string("skill_name"),
                        )
                }
            }

            val activeByCategory = payload.get("active_by_category")
            if (activeByCategory?.isJsonObject == true) {
                result.remove("outer")
                activeByCategory.asJsonObject.entrySet().forEach { (categoryId, element) ->
                    val category = categoryId.trim().lowercase()
                    if (category.isNotBlank() && element.isJsonObject) {
                        val slotPayload = element.asJsonObject
                        val skillId = slotPayload.string("skill_id")
                        if (skillId.isNotBlank()) {
                            result["outer:$category"] =
                                ActiveSkillSlotPacket(
                                    slot = "outer",
                                    skillId = skillId,
                                    skillName = slotPayload.string("skill_name"),
                                    category = category,
                                )
                        }
                    }
                }
            }
            return result
        }
    }
}

data class SkillSummaryPacket(
    val id: String,
    val name: String,
    val type: String,
    val category: String,
    val level: Int,
    val maxLevel: Int,
    val exp: Int,
    val nextCost: Int,
    val breakthroughTier: Int,
    val nextBreakthroughLevel: Int,
    val breakthroughPendingCount: Int,
    val sourceTypes: List<String>,
    val hasPerform: Boolean,
    val hasExert: Boolean,
    val canPractice: Boolean,
    /** 第五轮 R4：服务端计算布尔（numeric_category 轴），客户端选技过滤纯呈现消费。 */
    val canIdlePractice: Boolean = false,
    val isForceSkill: Boolean = false,
    val activationOptions: Map<String, SkillActivationOptionPacket>,
    val actionIntros: List<SkillActionIntroPacket>,
) {
    companion object {
        fun listFrom(payload: JsonObject): List<SkillSummaryPacket> {
            return payload.array("skills").map { entry ->
                SkillSummaryPacket(
                    id = entry.obj().string("id"),
                    name = entry.obj().string("name"),
                    type = entry.obj().string("type"),
                    category = entry.obj().string("category"),
                    level = entry.obj().int("level"),
                    maxLevel = entry.obj().int("max_level"),
                    exp = entry.obj().int("exp"),
                    nextCost = entry.obj().int("next_cost"),
                    breakthroughTier = entry.obj().int("breakthrough_tier"),
                    nextBreakthroughLevel = entry.obj().int("next_breakthrough_level"),
                    breakthroughPendingCount = entry.obj().int("breakthrough_pending_count"),
                    sourceTypes = entry.obj().stringArray("source_types"),
                    hasPerform = entry.obj().bool("has_perform"),
                    hasExert = entry.obj().bool("has_exert"),
                    canPractice = entry.obj().bool("can_practice"),
                    canIdlePractice = entry.obj().bool("can_idle_practice"),
                    isForceSkill = entry.obj().bool("is_force_skill"),
                    activationOptions =
                        SkillActivationOptionPacket.mapFrom(
                            entry.obj().obj("activation_options"),
                        ),
                    actionIntros =
                        SkillActionIntroPacket.listFrom(
                            entry.obj().array("action_intros"),
                        ),
                )
            }.filter { it.id.isNotBlank() || it.name.isNotBlank() }
        }
    }
}

data class SkillActivationOptionPacket(
    val slot: String,
    val label: String,
    val command: String,
) {
    companion object {
        fun mapFrom(payload: JsonObject): Map<String, SkillActivationOptionPacket> {
            return payload.entrySet().mapNotNull { (slotId, element) ->
                if (!element.isJsonObject) return@mapNotNull null
                val option = element.asJsonObject
                slotId to
                    SkillActivationOptionPacket(
                        slot = option.string("slot").ifBlank { slotId },
                        label = option.string("label"),
                        command = option.string("command"),
                    )
            }.filter { (_, option) -> option.command.isNotBlank() }.toMap()
        }
    }
}

data class SkillInfoPacket(
    val id: String,
    val name: String,
    val type: String,
    val category: String,
    val level: Int,
    val cap: Int,
    val nextCost: Int,
    val breakthroughTier: Int,
    val nextBreakthroughLevel: Int,
    val breakthrough: SkillBreakthroughStatePacket?,
    val breakthroughPendingCount: Int,
    val breakthroughEntries: List<SkillBreakthroughEntryPacket>,
    val breakthroughChoices: List<SkillBreakthroughChoicePacket>,
    val exp: Int,
    val maxExp: Int,
    val description: String,
    val detailDescription: String,
    val damage: String,
    val cost: String,
    val cooldown: Int,
    val requirements: String,
    val sourceTypes: List<String>,
    val skillIntro: SkillIntroPacket?,
    val actionIntros: List<SkillActionIntroPacket>,
    val performAvailable: Boolean,
    val exertAvailable: Boolean,
    val performProfile: SkillActionProfilePacket?,
    val exertProfile: SkillActionProfilePacket?,
    val canPractice: Boolean,
    val activationOptions: Map<String, SkillActivationOptionPacket>,
    val command: String,
) {
    companion object {
        fun fromJson(payload: JsonObject): SkillInfoPacket {
            val unlocks = payload.obj("unlocks")
            val breakthrough =
                SkillBreakthroughStatePacket.fromJsonOrNull(
                    payload.obj("breakthrough"),
                )
            val breakthroughEntries =
                SkillBreakthroughEntryPacket.listFrom(
                    payload.array("breakthrough_selected"),
                ).ifEmpty {
                    SkillBreakthroughEntryPacket.listFrom(
                        payload.array("breakthrough_entries"),
                    ).ifEmpty {
                        breakthrough?.selected.orEmpty().ifEmpty { breakthrough?.entries.orEmpty() }
                    }
                }
            val breakthroughChoices =
                SkillBreakthroughChoicePacket.listFrom(
                    payload.array("breakthrough_choices"),
                ).ifEmpty {
                    breakthrough?.available.orEmpty()
                }
            return SkillInfoPacket(
                id = payload.string("id"),
                name = payload.string("name"),
                type = payload.string("type"),
                category = payload.string("category"),
                level = payload.int("level"),
                cap = payload.int("cap"),
                nextCost = payload.int("next_cost"),
                breakthroughTier = payload.int("breakthrough_tier", breakthrough?.tier ?: 0),
                nextBreakthroughLevel =
                    payload.int(
                        "next_breakthrough_level",
                        breakthrough?.nextLevel ?: 0,
                    ),
                breakthrough = breakthrough,
                breakthroughPendingCount =
                    payload.int(
                        "breakthrough_pending_count",
                        breakthrough?.pendingCount ?: 0,
                    ),
                breakthroughEntries = breakthroughEntries,
                breakthroughChoices = breakthroughChoices,
                exp = payload.int("exp"),
                maxExp = payload.int("max_exp"),
                description = payload.string("description"),
                detailDescription = payload.string("detail_description"),
                damage = payload.string("damage"),
                cost = payload.string("cost"),
                cooldown = payload.int("cooldown"),
                requirements = payload.string("requirements"),
                sourceTypes = payload.stringArray("source_types"),
                skillIntro = SkillIntroPacket.fromJsonOrNull(payload.obj("skill_intro")),
                actionIntros = SkillActionIntroPacket.listFrom(payload.array("action_intros")),
                performAvailable =
                    if (payload.has("perform_available")) {
                        payload.bool("perform_available")
                    } else {
                        unlocks.bool("perform")
                    },
                exertAvailable =
                    if (payload.has("exert_available")) {
                        payload.bool("exert_available")
                    } else {
                        unlocks.bool("exert")
                    },
                performProfile =
                    SkillActionProfilePacket.fromJsonOrNull(
                        payload.obj("perform_profile"),
                    ),
                exertProfile =
                    SkillActionProfilePacket.fromJsonOrNull(
                        payload.obj("exert_profile"),
                    ),
                canPractice = payload.bool("can_practice"),
                activationOptions =
                    SkillActivationOptionPacket.mapFrom(
                        payload.obj("activation_options"),
                    ),
                command = payload.string("command"),
            )
        }
    }
}

data class SkillIntroPacket(
    val title: String,
    val summary: String,
    val type: String,
    val category: String,
    val level: Int,
    val cap: Int,
    val sourceTypes: List<String>,
) {
    companion object {
        fun fromJsonOrNull(payload: JsonObject): SkillIntroPacket? {
            if (payload.entrySet().isEmpty()) return null
            return SkillIntroPacket(
                title = payload.string("title"),
                summary = payload.string("summary"),
                type = payload.string("type"),
                category = payload.string("category"),
                level = payload.int("level"),
                cap = payload.int("cap"),
                sourceTypes = payload.stringArray("source_types"),
            )
        }
    }
}

data class SkillActionIntroPacket(
    val type: String,
    val label: String,
    val command: String,
    val usage: String,
    val targetPolicy: String,
    val available: Boolean,
    val unavailableReason: String,
    val requirements: String,
    val resourceCosts: Map<String, Int>,
    val cooldown: Int,
    val cooldownRemaining: Int,
    val intervalRemaining: Int,
    val busy: Int,
    val effects: Map<String, Int>,
    val statusEffects: List<JsonObject>,
    val pvpFactorBp: Int,
    val pveFactorBp: Int,
    val description: String,
) {
    companion object {
        fun listFrom(entries: JsonArray): List<SkillActionIntroPacket> {
            return entries.mapNotNull { element ->
                if (!element.isJsonObject) return@mapNotNull null
                val payload = element.asJsonObject
                SkillActionIntroPacket(
                    type = payload.string("type"),
                    label = payload.string("label"),
                    command = payload.string("command"),
                    usage = payload.string("usage"),
                    targetPolicy = payload.string("target_policy"),
                    available = payload.bool("available"),
                    unavailableReason = payload.string("unavailable_reason"),
                    requirements = payload.string("requirements"),
                    resourceCosts = payload.intMap("resource_costs"),
                    cooldown = payload.int("cooldown"),
                    cooldownRemaining = payload.int("cooldown_remaining"),
                    intervalRemaining = payload.int("interval_remaining"),
                    busy = payload.int("busy"),
                    effects = payload.intMap("effects"),
                    statusEffects = payload.objectArray("status_effects"),
                    pvpFactorBp = payload.int("pvp_factor_bp", 10000),
                    pveFactorBp = payload.int("pve_factor_bp", 10000),
                    description = payload.string("description"),
                )
            }
        }
    }
}

data class SkillBreakthroughEntryPacket(
    val id: String,
    val tier: Int,
    val domain: String,
    val description: String,
    val effects: Map<String, Int>,
) {
    companion object {
        fun listFrom(entries: JsonArray): List<SkillBreakthroughEntryPacket> {
            return entries.mapNotNull { element ->
                if (!element.isJsonObject) return@mapNotNull null
                fromJson(element.asJsonObject)
            }
        }

        private fun fromJson(entry: JsonObject): SkillBreakthroughEntryPacket {
            return SkillBreakthroughEntryPacket(
                id = entry.string("id"),
                tier = entry.int("tier"),
                domain = entry.string("domain"),
                description = entry.string("description"),
                effects = entry.intMap("effects"),
            )
        }
    }
}

data class SkillBreakthroughChoicePacket(
    val tier: Int,
    val level: Int,
    val candidates: List<SkillBreakthroughEntryPacket>,
) {
    companion object {
        fun listFrom(entries: JsonArray): List<SkillBreakthroughChoicePacket> {
            return entries.mapNotNull { element ->
                if (!element.isJsonObject) return@mapNotNull null
                val entry = element.asJsonObject
                SkillBreakthroughChoicePacket(
                    tier = entry.int("tier"),
                    level = entry.int("level"),
                    candidates = SkillBreakthroughEntryPacket.listFrom(entry.array("candidates")),
                )
            }
        }
    }
}

data class SkillBreakthroughStatePacket(
    val ok: Boolean,
    val skillId: String,
    val kind: String,
    val level: Int,
    val tier: Int,
    val unlockedTier: Int,
    val interval: Int,
    val maxTier: Int,
    val nextLevel: Int,
    val selected: List<SkillBreakthroughEntryPacket>,
    val entries: List<SkillBreakthroughEntryPacket>,
    val available: List<SkillBreakthroughChoicePacket>,
    val pendingCount: Int,
) {
    companion object {
        fun fromJsonOrNull(payload: JsonObject): SkillBreakthroughStatePacket? {
            if (payload.entrySet().isEmpty()) return null
            return SkillBreakthroughStatePacket(
                ok = payload.bool("ok"),
                skillId = payload.string("skill_id"),
                kind = payload.string("kind"),
                level = payload.int("level"),
                tier = payload.int("tier"),
                unlockedTier = payload.int("unlocked_tier"),
                interval = payload.int("interval"),
                maxTier = payload.int("max_tier"),
                nextLevel = payload.int("next_level"),
                selected = SkillBreakthroughEntryPacket.listFrom(payload.array("selected")),
                entries = SkillBreakthroughEntryPacket.listFrom(payload.array("entries")),
                available = SkillBreakthroughChoicePacket.listFrom(payload.array("available")),
                pendingCount = payload.int("pending_count"),
            )
        }
    }
}

data class SkillActionProfilePacket(
    val busy: Int,
    val cooldown: Int,
    val resourceCosts: Map<String, Int>,
    val targetPolicy: String,
    val effects: Map<String, Int>,
    val statusEffects: List<JsonObject>,
    val pvpFactorBp: Int,
    val pveFactorBp: Int,
) {
    companion object {
        fun fromJsonOrNull(payload: JsonObject): SkillActionProfilePacket? {
            if (payload.entrySet().isEmpty()) return null
            return SkillActionProfilePacket(
                busy = payload.int("busy"),
                cooldown = payload.int("cooldown"),
                resourceCosts = payload.intMap("resource_costs"),
                targetPolicy = payload.string("target_policy"),
                effects = payload.intMap("effects"),
                statusEffects = payload.objectArray("status_effects"),
                pvpFactorBp = payload.int("pvp_factor_bp", 10000),
                pveFactorBp = payload.int("pve_factor_bp", 10000),
            )
        }
    }
}

private fun JsonObject.array(key: String): JsonArray {
    return payloadArray(key)
}

private fun JsonObject.obj(key: String): JsonObject {
    return payloadObject(key)
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

private fun JsonObject.bool(key: String): Boolean {
    if (!has(key) || get(key).isJsonNull) {
        return false
    }
    val value = get(key).asJsonPrimitive
    return if (value.isBoolean) value.asBoolean else payloadLong(key) != 0L
}

private fun JsonObject.stringArray(key: String): List<String> {
    return payloadStringArray(key)
}

private fun JsonObject.intMap(key: String): Map<String, Int> {
    return payloadIntMap(key)
}

private fun JsonObject.objectArray(key: String): List<JsonObject> {
    return payloadObjectArray(key)
}

private fun com.google.gson.JsonElement.obj(): JsonObject = asJsonObject
