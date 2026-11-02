package com.xiakexing.client.feature.shell

import com.google.gson.JsonObject
import com.xiakexing.client.core.protocol.payload.CombatDamagePacket
import com.xiakexing.client.core.protocol.payload.CombatOpponentSnapshot
import com.xiakexing.client.core.protocol.payload.CombatRoundPacket
import com.xiakexing.client.core.protocol.payload.CombatRuntimeCooldownPacket
import com.xiakexing.client.core.protocol.payload.CombatStartPacket
import com.xiakexing.client.core.protocol.payload.SkillEventPacket
import java.math.BigDecimal
import java.math.BigInteger

/**
 * Combat 域纯工具层——buff/冷却/状态芯片解析、效果解析与变换工具。
 * 状态变换在 CombatReducers.kt，文本构造在 CombatMessageRendering.kt。
 * 不持有 sink，不执行任何副作用。
 */

internal fun SkillEventPacket.combatActionRefOrBlank(): String {
    val normalizedEvent = event.trim().lowercase()
    if (normalizedEvent != COMBAT_SHORTCUT_PERFORM && normalizedEvent != COMBAT_SHORTCUT_EXERT) {
        return ""
    }
    return combatShortcutActionRef(normalizedEvent, skillId, actionId)
}

internal fun List<CombatBuffChip>.mergeCombatBuffChips(
    effects: List<JsonObject>,
): List<CombatBuffChip> {
    val current = associateBy { it.combatStatusKey() }.toMutableMap()
    effects.mapNotNull { effect -> effect.toCombatBuffChipOrNull() }.forEach { chip ->
        val key = chip.combatStatusKey()
        current[key] = chip.withRetainedStatusTiming(current[key])
    }
    return current.values.sortedBy { it.label }.take(8)
}

internal fun List<JsonObject>.toCombatBuffChips(): List<CombatBuffChip> {
    return mapNotNull { effect -> effect.toCombatBuffChipOrNull() }
        .associateBy { chip -> chip.combatStatusKey() }
        .values
        .sortedBy { chip -> chip.label }
        .take(8)
}

internal fun List<CombatRuntimeCooldownPacket>.toCombatActionCooldownMap(
    nowMillis: Long,
): Map<String, CombatActionCooldown> {
    return mapNotNull { cooldown ->
        val ref = cooldown.combatActionRefOrBlank()
        if (ref.isBlank()) return@mapNotNull null
        val remaining =
            maxOf(cooldown.remainingSeconds, cooldown.cooldownRemaining, cooldown.intervalRemaining)
        val total = maxOf(cooldown.totalSeconds, remaining)
        if (remaining <= 0 || total <= 0) return@mapNotNull null
        ref to
            CombatActionCooldown(
                label = cooldown.actionName.ifBlank { cooldown.actionId }.ifBlank { cooldown.skillId },
                remaining = remaining,
                total = total,
                capturedAtMillis = nowMillis,
            )
    }.toMap()
}

private fun CombatRuntimeCooldownPacket.combatActionRefOrBlank(): String {
    val normalizedSkillId = skillId.ifBlank { actionId }.trim()
    if (normalizedSkillId.isBlank()) {
        return ""
    }
    return combatShortcutActionRef(
        actionType = actionType,
        skillId = normalizedSkillId,
        actionId = actionId.takeIf { skillId.isNotBlank() }.orEmpty(),
    )
}

private fun CombatBuffChip.withRetainedStatusTiming(previous: CombatBuffChip?): CombatBuffChip {
    if (previous == null) {
        return this
    }
    return copy(
        remainingSeconds = remainingSeconds.takeIf { it > 0 } ?: previous.remainingSeconds,
        remainingRounds = remainingRounds.takeIf { it > 0 } ?: previous.remainingRounds,
    )
}

internal fun List<CombatBuffChip>.removeCombatBuffChips(
    effects: List<JsonObject>,
): List<CombatBuffChip> {
    if (isEmpty() || effects.isEmpty()) {
        return this
    }
    val removedKeys =
        effects.mapNotNull {
                effect ->
            effect.toCombatBuffChipOrNull()?.combatStatusKey()
        }.toSet()
    if (removedKeys.isEmpty()) {
        return this
    }
    return filterNot { chip -> chip.combatStatusKey() in removedKeys }
}

private fun CombatBuffChip.combatStatusKey(): String = label.ifBlank { id }.trim().lowercase()

internal fun JsonObject.combatStatusOwner(): String =
    combatPayloadString(
        "owner",
    ).trim().lowercase()

private fun JsonObject.toCombatBuffChipOrNull(): CombatBuffChip? {
    val typeLabel = combatPayloadString("type").combatStatusTypeLabel()
    val category = combatPayloadString("category").trim().lowercase()
    val rawName = combatPayloadString("name").trim()
    val rawId = combatPayloadString("id").trim()
    val label = rawName.ifBlank { rawId }.ifBlank { typeLabel }.trim()
    if (label.isBlank() || label.isGenericCombatStatusLabel()) {
        return null
    }
    return CombatBuffChip(
        id = rawId.ifBlank { label },
        label = label,
        category = category.ifBlank { combatPayloadString("type").trim().lowercase() },
        remainingSeconds =
            listOf(
                combatPayloadInt("remaining_seconds"),
                combatPayloadInt("duration"),
                combatPayloadInt("remaining_rounds"),
                combatPayloadInt("duration_rounds"),
            ).firstOrNull { it > 0 } ?: 0,
        remainingRounds = combatPayloadInt("remaining_rounds"),
        stacks = combatPayloadInt("stacks", 1).coerceAtLeast(1),
    )
}

internal fun remainingSecondsAt(
    initialSeconds: Int,
    capturedAtMillis: Long,
    nowMillis: Long,
): Int {
    if (initialSeconds <= 0) return 0
    val captured = capturedAtMillis.takeIf { it > 0L } ?: nowMillis
    val elapsedSeconds = ((nowMillis - captured).coerceAtLeast(0L) / 1000L).toInt()
    return (initialSeconds - elapsedSeconds).coerceAtLeast(0)
}

internal fun String.matchesCurrentPlayerOrYou(
    snapshot: com.xiakexing.client.core.protocol.payload.PlayerStatusSnapshot?,
    username: String,
): Boolean {
    return trim() == "你" || (snapshot != null && matchesCurrentPlayer(snapshot, username))
}

private fun String.combatStatusTypeLabel(): String {
    return when (trim().lowercase()) {
        "control" -> "受制"
        "poison" -> "中毒"
        "bleed" -> "流血"
        "burn" -> "灼烧"
        "buff" -> "增益"
        "debuff" -> "减益"
        else -> trim()
    }
}

private fun String.isGenericCombatStatusLabel(): Boolean {
    val normalized = trim()
    return normalized.isBlank() ||
        normalized == "状态" ||
        normalized == "状态效果" ||
        normalized == "效果" ||
        normalized == "影响"
}

internal fun String.toCombatStatusEffectObjects(): List<JsonObject> {
    val name = extractCombatStatusName()
    return if (name.isBlank()) emptyList() else listOf(combatStatusEffectObject(name, name))
}

internal fun String.extractCombatStatusName(): String {
    val text = trim()
    if (text.isBlank()) return ""
    Regex("「([^」]{1,12})」")
        .find(text)
        ?.groupValues
        ?.getOrNull(1)
        ?.takeIf { it.isNotBlank() }
        ?.let { return it }
    if (text.contains("受制")) return "受制"
    val patterns =
        listOf(
            Regex("([^，。；;：:\\s]{1,12})(?:状态|效果)(?:消失|解除|生效|持续|获得)?"),
            Regex("([^，。；;：:\\s]{1,12})持续作用"),
        )
    return patterns.firstNotNullOfOrNull { pattern ->
        pattern.find(text)?.groupValues?.getOrNull(1)?.trim()?.takeIf { it.isNotBlank() }
    }.orEmpty()
}

internal fun combatStatusEffectObject(
    id: String,
    name: String,
    duration: Int = 0,
): JsonObject {
    return JsonObject().apply {
        addProperty("id", id)
        addProperty("name", name)
        if (duration > 0) addProperty("duration", duration)
    }
}

internal fun JsonObject.combatPayloadString(key: String): String {
    return if (has(key) && !get(key).isJsonNull) get(key).asString else ""
}

internal fun JsonObject.combatPayloadInt(
    key: String,
    default: Int = 0,
): Int {
    if (!has(key) || get(key).isJsonNull || !get(key).isJsonPrimitive) {
        return default
    }
    return runCatching { BigDecimal(get(key).asString.trim()).toInt() }.getOrDefault(default)
}

internal fun CombatStartPacket.toOpponentVitalsOrNull(): CombatOpponentVitals? {
    if (opponentHp < 0 || opponentMaxHp <= 0 || opponentMp < 0 || opponentMaxMp <= 0 || opponentEp < 0 || opponentMaxEp <= 0) {
        return null
    }
    return CombatOpponentVitals(
        hp = opponentHp.coerceAtLeast(0L),
        maxHp = opponentMaxHp,
        mp = opponentMp.coerceAtLeast(0L),
        maxMp = opponentMaxMp,
        ep = opponentEp.coerceAtLeast(0L),
        maxEp = opponentMaxEp,
    )
}

internal fun CombatStartPacket.toOpponentStates(): List<CombatOpponentState> {
    val structured = opponents.mapNotNull(CombatOpponentSnapshot::toCombatOpponentStateOrNull)
    if (structured.isNotEmpty()) {
        return structured
    }
    val name = opponent.trim()
    if (name.isBlank()) {
        return emptyList()
    }
    return listOf(CombatOpponentState(name, toOpponentVitalsOrNull() ?: CombatOpponentVitals()))
}

private fun CombatOpponentSnapshot.toCombatOpponentStateOrNull(): CombatOpponentState? {
    val normalizedName = name.trim()
    if (normalizedName.isBlank()) {
        return null
    }
    return CombatOpponentState(
        name = normalizedName,
        vitals =
            CombatOpponentVitals(
                hp = hp.takeIf { it >= 0 }?.coerceAtLeast(0L),
                maxHp = maxHp.takeIf { it > 0 },
                mp = mp.takeIf { it >= 0 }?.coerceAtLeast(0L),
                maxMp = maxMp.takeIf { it > 0 },
                ep = ep.takeIf { it >= 0 }?.coerceAtLeast(0L),
                maxEp = maxEp.takeIf { it > 0 },
            ),
    )
}

internal fun List<CombatOpponentState>.upsertCombatOpponent(
    name: String,
    vitals: CombatOpponentVitals,
): List<CombatOpponentState> {
    val normalizedName = name.trim()
    if (normalizedName.isBlank()) {
        return this
    }
    val index = indexOfFirst { it.name == normalizedName }
    if (index < 0) {
        return this + CombatOpponentState(normalizedName, vitals)
    }
    val previous = this[index]
    val next = toMutableList()
    next[index] =
        previous.copy(
            vitals =
                CombatOpponentVitals(
                    hp = vitals.hp ?: previous.vitals.hp,
                    maxHp = vitals.maxHp ?: previous.vitals.maxHp,
                    mp = vitals.mp ?: previous.vitals.mp,
                    maxMp = vitals.maxMp ?: previous.vitals.maxMp,
                    ep = vitals.ep ?: previous.vitals.ep,
                    maxEp = vitals.maxEp ?: previous.vitals.maxEp,
                ),
        )
    return next
}

internal fun List<CombatOpponentState>.sortedForVitals(): List<CombatOpponentState> {
    return sortedWith(
        compareBy<CombatOpponentState> { it.name.codePointCount(0, it.name.length) }
            .thenBy { it.name.firstOrNull()?.code ?: Int.MAX_VALUE },
    )
}

internal fun JsonObject.statusValue(
    key: String,
    fallback: Long,
): Long {
    return if (has(key) && !get(key).isJsonNull) {
        runCatching {
            BigDecimal(get(key).asString.trim()).toBigInteger().coerceInLongRange().toLong()
        }.getOrDefault(fallback)
    } else {
        fallback
    }
}

private fun BigInteger.coerceInLongRange(): BigInteger {
    val min = BigInteger.valueOf(Long.MIN_VALUE)
    val max = BigInteger.valueOf(Long.MAX_VALUE)
    return when {
        this < min -> min
        this > max -> max
        else -> this
    }
}

/** 回合状态效果解析：status_block 构造受制效果，否则从消息文本提取。 */
internal fun buildRoundStatusEffects(
    packet: CombatRoundPacket,
    event: String,
): List<JsonObject> {
    return packet.statusEffects.ifEmpty {
        if (event == "status_block") {
            listOf(combatStatusEffectObject(id = "status_block", name = "受制", duration = 1))
        } else {
            packet.message.ifBlank { packet.detailMessage }.toCombatStatusEffectObjects()
        }
    }
}

/** 伤害状态效果解析：优先效果名，否则从消息文本提取。 */
internal fun buildDamageStatusEffects(packet: CombatDamagePacket): List<JsonObject> {
    return packet.statusEffects.ifEmpty {
        val effectName =
            packet.effectName.ifBlank {
                packet.message.ifBlank { packet.detailMessage }.extractCombatStatusName()
            }
        if (effectName.isBlank()) {
            emptyList()
        } else {
            listOf(combatStatusEffectObject(effectName, effectName))
        }
    }
}
