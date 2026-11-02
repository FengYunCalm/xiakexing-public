package com.xiakexing.client.feature.shell

import com.xiakexing.client.core.protocol.payload.SkillActionIntroPacket
import com.xiakexing.client.core.protocol.payload.SkillInfoPacket
import com.xiakexing.client.core.protocol.payload.SkillSummaryPacket

internal const val COMBAT_SHORTCUT_PERFORM = "perform"
internal const val COMBAT_SHORTCUT_EXERT = "exert"

internal data class CombatShortcutAction(
    val ref: String,
    val label: String,
    val command: String,
    val skillId: String,
    val skillName: String,
    val level: Int,
    val available: Boolean,
    val unavailableReason: String,
    val requirements: String,
    val cooldown: CombatActionCooldown = CombatActionCooldown(),
) {
    val removeToken: String
        get() = ref.combatShortcutRemovalToken()
}

data class CombatActionCooldown(
    val label: String = "",
    val remaining: Int = 0,
    val total: Int = 0,
    val capturedAtMillis: Long = System.currentTimeMillis(),
) {
    val active: Boolean
        get() = remaining > 0 && total > 0
}

internal fun CombatActionCooldown.remainingAt(nowMillis: Long): Int {
    if (!active) return 0
    val elapsedSeconds = ((nowMillis - capturedAtMillis).coerceAtLeast(0L) / 1000L).toInt()
    return (remaining - elapsedSeconds).coerceAtLeast(0)
}

internal fun CombatActionCooldown.activeAt(nowMillis: Long): Boolean =
    remainingAt(nowMillis) > 0 && total > 0

internal fun SkillSummaryPacket.combatShortcutActions(): List<CombatShortcutAction> {
    return actionIntros.toCombatShortcutActions(id, name, level)
}

internal fun SkillInfoPacket.combatShortcutActions(): List<CombatShortcutAction> {
    return actionIntros.toCombatShortcutActions(id, name, level)
}

private fun List<SkillActionIntroPacket>.toCombatShortcutActions(
    skillId: String,
    skillName: String,
    level: Int,
): List<CombatShortcutAction> {
    return mapNotNull { intro ->
        val actionType = intro.type.trim().lowercase()
        if (actionType != COMBAT_SHORTCUT_PERFORM && actionType != COMBAT_SHORTCUT_EXERT) {
            return@mapNotNull null
        }
        val normalizedSkillId = skillId.trim()
        if (normalizedSkillId.isBlank()) {
            return@mapNotNull null
        }
        val actionId = intro.command.combatShortcutCommandActionId(normalizedSkillId)
        val ref = combatShortcutActionRef(actionType, normalizedSkillId, actionId)
        val remaining = maxOf(intro.cooldownRemaining, intro.intervalRemaining)
        val total = maxOf(intro.cooldown, remaining)
        val label = intro.actionDisplayLabel().ifBlank { normalizedSkillId }
        CombatShortcutAction(
            ref = ref,
            label = label,
            command = intro.command.trim().ifBlank { ref.combatShortcutCommand() },
            skillId = normalizedSkillId,
            skillName = skillName.ifBlank { normalizedSkillId },
            level = level,
            available = intro.available,
            unavailableReason = intro.unavailableReason,
            requirements = intro.requirements,
            cooldown = CombatActionCooldown(label = label, remaining = remaining, total = total),
        )
    }.distinctBy { it.ref }
}

internal fun CombatShortcutAction.supportingText(): String {
    if (!available && unavailableReason.isNotBlank() && requirements.isNotBlank()) {
        return "$unavailableReason $requirements"
    }
    if (!available && unavailableReason.isNotBlank()) {
        return unavailableReason
    }
    return requirements
}

private fun SkillActionIntroPacket.actionDisplayLabel(): String {
    val normalizedLabel = label.trim()
    if (normalizedLabel.isNotGenericCombatActionLabel()) {
        return normalizedLabel
    }
    val descriptionLead = description.substringBefore('：').substringBefore(':').trim()
    if (descriptionLead.isNotGenericCombatActionLabel()) {
        return descriptionLead
    }
    return command.combatShortcutCommandActionId("").ifBlank { normalizedLabel }
}

private fun String.isNotGenericCombatActionLabel(): Boolean {
    val normalized = trim()
    return normalized.isNotBlank() &&
        normalized != "施展" &&
        normalized != "运功" &&
        normalized != "招式" &&
        normalized != "内功" &&
        normalized != COMBAT_SHORTCUT_PERFORM &&
        normalized != COMBAT_SHORTCUT_EXERT
}

private fun String.combatShortcutCommandActionId(skillId: String): String {
    val parts = trim().split(Regex("\\s+")).filter { it.isNotBlank() }
    if (parts.size < 3) {
        return ""
    }
    val commandType = parts[0].lowercase()
    if (commandType != COMBAT_SHORTCUT_PERFORM && commandType != COMBAT_SHORTCUT_EXERT) {
        return ""
    }
    if (skillId.isNotBlank() && parts[1] != skillId) {
        return ""
    }
    return parts[2]
}

internal fun combatShortcutActionRef(
    actionType: String,
    skillId: String,
    actionId: String = "",
): String {
    val normalizedType = actionType.trim().lowercase()
    val normalizedSkillId = skillId.trim()
    val normalizedActionId = actionId.trim()
    if (normalizedSkillId.isBlank()) {
        return ""
    }
    return when (normalizedType) {
        COMBAT_SHORTCUT_PERFORM, COMBAT_SHORTCUT_EXERT -> {
            if (normalizedActionId.isBlank()) {
                "$normalizedType:$normalizedSkillId"
            } else {
                "$normalizedType:$normalizedSkillId:$normalizedActionId"
            }
        }
        else -> ""
    }
}

internal fun String.toCombatShortcutActionRefOrBlank(): String {
    val normalized = trim()
    if (normalized.isBlank()) {
        return ""
    }
    val parts = normalized.split(':', limit = 3)
    if (parts.size == 1) {
        return combatShortcutActionRef(COMBAT_SHORTCUT_PERFORM, parts[0])
    }
    return combatShortcutActionRef(
        actionType = parts[0],
        skillId = parts.getOrNull(1).orEmpty(),
        actionId = parts.getOrNull(2).orEmpty(),
    )
}

internal fun String.combatShortcutActionType(): String {
    return toCombatShortcutActionRefOrBlank().substringBefore(':', COMBAT_SHORTCUT_PERFORM)
}

internal fun String.combatShortcutSkillId(): String {
    return toCombatShortcutActionRefOrBlank().split(':').getOrNull(1).orEmpty()
}

internal fun String.combatShortcutActionId(): String {
    return toCombatShortcutActionRefOrBlank().split(':').getOrNull(2).orEmpty()
}

internal fun String.combatShortcutCommand(): String {
    val ref = toCombatShortcutActionRefOrBlank()
    val skillId = ref.combatShortcutSkillId()
    if (skillId.isBlank()) {
        return ""
    }
    val actionId = ref.combatShortcutActionId()
    return buildString {
        append(ref.combatShortcutActionType())
        append(' ')
        append(skillId)
        if (actionId.isNotBlank()) {
            append(' ')
            append(actionId)
        }
    }
}

internal fun String.combatShortcutRemovalToken(): String {
    val ref = toCombatShortcutActionRefOrBlank()
    return if (ref.combatShortcutActionType() == COMBAT_SHORTCUT_PERFORM && ref.combatShortcutActionId().isBlank()) {
        ref.combatShortcutSkillId()
    } else {
        ref
    }
}

internal fun List<String>.padCombatActionSlots(): List<String> {
    return take(COMBAT_PERFORM_SLOT_COUNT) +
        List((COMBAT_PERFORM_SLOT_COUNT - size).coerceAtLeast(0)) {
            ""
        }
}

internal fun List<String>.dedupeCombatActionSlots(): List<String> {
    val seenRefs = mutableSetOf<String>()
    return map { actionRef ->
        val normalizedRef = actionRef.toCombatShortcutActionRefOrBlank()
        if (normalizedRef.isBlank() || !seenRefs.add(normalizedRef)) "" else normalizedRef
    }.padCombatActionSlots()
}

internal fun List<String>.padCombatPerformSlots(): List<String> =
    map {
        it.toCombatShortcutActionRefOrBlank()
    }.padCombatActionSlots()

internal fun List<String>.dedupeCombatPerformSlots(): List<String> = dedupeCombatActionSlots()
