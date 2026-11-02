package com.xiakexing.client.feature.shell

import com.xiakexing.client.core.protocol.payload.CombatDamagePacket
import com.xiakexing.client.core.protocol.payload.CombatEndPacket
import com.xiakexing.client.core.protocol.payload.CombatRoundPacket
import com.xiakexing.client.core.protocol.payload.CombatStartPacket
import com.xiakexing.client.core.protocol.payload.SkillEventPacket

/**
 * Combat 域文本构造层——战斗日志/广播/回合计数的文案纯函数。
 * 不持有 sink，不执行任何副作用；状态变换在 CombatReducers.kt。
 */

internal fun buildCombatStartLines(packet: CombatStartPacket): List<String> {
    val modeLabel = if (packet.killMode) "生死战" else "切磋"
    val opponent = buildCombatOpponentLabel(packet)
    val message = packet.message.ifBlank { "${modeLabel}开始，对手：$opponent" }
    return listOf(message).map { it.trim() }.filter { it.isNotBlank() }.distinct()
}

internal fun buildCombatOpponentLabel(packet: CombatStartPacket): String =
    packet.opponent.ifBlank {
        packet.opponents.firstOrNull()?.name?.takeIf { it.isNotBlank() } ?: "未知对手"
    }

/** 即时伤害由后续结算包反馈时，CBSK 只更新状态、不再生成一条文字反馈。 */
internal fun SkillEventPacket.hasDeferredCombatResultFeedback(): Boolean =
    event.trim().equals("perform", ignoreCase = true) &&
        targetPolicy.trim().equals("combat_target", ignoreCase = true) &&
        message.isBlank() && detailMessage.isBlank()

/** 技能事件日志文案：显式消息优先，否则按事件类型构造。 */
internal fun buildSkillEventMessage(packet: SkillEventPacket): String {
    val explicitMessage = packet.message.ifBlank { packet.detailMessage }
    if (explicitMessage.isNotBlank()) return explicitMessage
    if (packet.hasDeferredCombatResultFeedback()) return ""
    val skillLabel = packet.actionName.ifBlank { packet.skillName }.ifBlank { "这一式" }
    return explicitMessage.ifBlank {
        when (packet.event.trim().lowercase()) {
            "perform" ->
                buildString {
                    append("施展 ${skillLabel.combatQuotedOrGeneric("招式")}")
                    appendSkillEventCosts(packet)
                    appendSkillEventEffectsUnlessCombatTarget(packet)
                    if (packet.justUnlocked) {
                        append("，刚刚领悟")
                    }
                }

            "exert" ->
                buildString {
                    append("运起 ${skillLabel.combatQuotedOrGeneric("内功")}")
                    appendSkillEventCosts(packet)
                    appendSkillEventEffectsUnlessCombatTarget(packet)
                    if (packet.justUnlocked) {
                        append("，刚刚领悟")
                    }
                }

            "basic_attack" -> {
                val damage =
                    packet.damage.takeIf { it > 0 }?.let {
                            value ->
                        "，造成 $value 点伤害"
                    }.orEmpty()
                "${skillLabel.combatQuotedOrGeneric("招式")}命中$damage"
            }

            else -> packet.resultLabel.ifBlank { "战斗中有一股劲力生效" }
        }
    }
}

/** 伤害事件回退文案（无显式消息时）。 */
internal fun buildCombatDamageFallback(packet: CombatDamagePacket): String {
    val source = packet.source.ifBlank { "未知来源" }
    val target = packet.target.ifBlank { "未知目标" }
    val action = packet.combatDamageActionPlainLabel()
    val hpSuffix =
        packet.targetHp.takeIf { it >= 0 }?.let {
                hp ->
            "，${target}剩余 $hp 点气血"
        }.orEmpty()
    return when (packet.event.trim().lowercase()) {
        "status_effect" -> "${packet.effectName.ifBlank { "状态效果" }}持续作用于$target，造成 ${packet.damage} 点伤害$hpSuffix"
        "npc_skill" -> "${source}使出${packet.combatDamageActionLabel()}，命中$target，造成 ${packet.damage} 点伤害$hpSuffix"
        "perform", "exert" -> "（${source.damageOwnerPrefix()}${action}对${target}造成${packet.damage}伤害）"
        else -> "$source：$action -> $target：${packet.damage}"
    }
}

/** 回合回退文案（无显式消息时）；无需追加时返回 null。 */
internal fun buildCombatRoundFallback(packet: CombatRoundPacket): String? {
    return when (packet.event.trim().lowercase()) {
        "parry" -> {
            val target = packet.target.ifBlank { packet.opponent.ifBlank { "对手" } }
            val source = packet.source.ifBlank { "对手" }
            val damage =
                packet.damage.takeIf { it > 0 }?.let {
                        value ->
                    "，仍受到 $value 点伤害"
                }.orEmpty()
            "$target 化解了 $source 的部分攻势$damage"
        }

        "status_block" -> "${packet.target.ifBlank { "你" }}一时受制，攻势被迫停了下来"

        else -> {
            if (packet.round > 0) {
                val opponent = packet.opponent.ifBlank { "对手" }
                "第${packet.round}回合，对手：$opponent"
            } else {
                null
            }
        }
    }
}

/** 战斗结束回退文案（无显式消息时）。 */
internal fun buildCombatEndFallback(packet: CombatEndPacket): String {
    val opponent = packet.opponent.ifBlank { "对手" }
    return when (packet.result.trim().lowercase()) {
        "win" -> "你战胜了$opponent"
        "lose" -> "你败给了$opponent"
        "draw" -> "你与${opponent}未分胜负"
        "flee" -> "你已脱离与${opponent}的战斗"
        "opponent_fled" -> "${opponent}已脱离战斗"
        else -> "${opponent}的战斗已结束"
    }
}

private fun String.combatQuotedOrGeneric(generic: String): String {
    val label = trim()
    if (label.isBlank()) {
        return generic
    }
    return "「$label」"
}

private fun StringBuilder.appendSkillEventEffectsUnlessCombatTarget(packet: SkillEventPacket) {
    if (packet.targetPolicy.trim().lowercase() != "combat_target") {
        appendSkillEventEffects(packet)
    }
}

private fun CombatDamagePacket.combatDamageActionLabel(): String {
    val actionLabel = actionName.ifBlank { skillName }
    if (skillName.isNotBlank() && actionName.isNotBlank() && actionName != skillName) {
        return "「$skillName·$actionName」"
    }
    return actionLabel.combatQuotedOrGeneric("招式")
}

private fun CombatDamagePacket.combatDamageActionPlainLabel(): String {
    return actionName.ifBlank { skillName }.ifBlank { "普通攻击" }
}

private fun String.damageOwnerPrefix(): String {
    return if (trim() == "你") "你的" else "${trim()}的"
}
