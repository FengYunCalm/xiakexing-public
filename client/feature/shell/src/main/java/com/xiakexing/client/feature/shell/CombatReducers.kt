package com.xiakexing.client.feature.shell

import com.google.gson.JsonObject
import com.xiakexing.client.core.protocol.payload.CombatDamagePacket
import com.xiakexing.client.core.protocol.payload.CombatRoundPacket
import com.xiakexing.client.core.protocol.payload.CombatRuntimeSnapshotPacket
import com.xiakexing.client.core.protocol.payload.SkillEventPacket

/**
 * combat 纯状态变换（reducer）——(state, packet) → state，无 sink/副作用。
 * handler 只做副作用（append/refresh 系列）与 reducer 应用；本文件可纯函数测试。
 * 已知天花板：busy 的内部游标（combatSelfBusyCapturedAtMillis 由外部 nowMillis
 * 传入，保持纯函数形态）；其余副作用（refresh/append）留在 coordinator。
 * 战斗反馈由 coordinator 统一追加到主文本区；reducer 只维护战斗状态。
 * 工具层在 CombatStatusTools.kt，文本构造在 CombatMessageRendering.kt。
 */
internal fun ShellUiState.reduceSkillEventStatus(packet: SkillEventPacket): ShellUiState {
    val snapshot = statusSnapshot ?: return this
    val costs = packet.resourceCosts
    val effects = packet.effects
    return copy(
        statusSnapshot =
            snapshot.copy(
                hp =
                    (snapshot.hp - (costs["hp"] ?: 0L) + (effects["recover_hp"] ?: 0L))
                        .coerceIn(0L, snapshot.maxHp.coerceAtLeast(0L)),
                mp =
                    (snapshot.mp - (costs["mp"] ?: 0L) + (effects["recover_mp"] ?: 0L))
                        .coerceIn(0L, snapshot.maxMp.coerceAtLeast(0L)),
                ep =
                    (snapshot.ep - (costs["ep"] ?: 0L) + (effects["recover_ep"] ?: 0L))
                        .coerceIn(0L, snapshot.maxEp.coerceAtLeast(0L)),
            ),
    )
}

internal fun ShellUiState.reduceCombatDamageStatus(packet: CombatDamagePacket): ShellUiState {
    if (packet.targetHp < 0) {
        return this
    }
    val snapshot = statusSnapshot ?: return this
    if (!packet.target.matchesCurrentPlayer(snapshot, username)) {
        return this
    }
    val nextSnapshot =
        snapshot.copy(
            hp = packet.targetHp.coerceIn(0L, snapshot.maxHp.coerceAtLeast(0L)),
        )
    if (nextSnapshot == snapshot) return this
    return copy(statusSnapshot = nextSnapshot)
}

internal fun ShellUiState.reduceCombatDamageOpponentVitals(
    packet: CombatDamagePacket,
): ShellUiState {
    if (packet.targetHp < 0) {
        return this
    }
    val snapshot = statusSnapshot
    if (snapshot != null && packet.target.matchesCurrentPlayer(snapshot, username)) {
        return this
    }
    val targetName = packet.target.ifBlank { combatOpponent }.trim()
    if (targetName.isBlank()) {
        return this
    }
    val currentOpponents =
        combatOpponents.ifEmpty {
            combatOpponent.takeIf { it.isNotBlank() }?.let {
                listOf(CombatOpponentState(it, combatOpponentVitals))
            }.orEmpty()
        }
    val previousVitals =
        currentOpponents.firstOrNull { it.name == targetName }?.vitals
            ?: combatOpponentVitals.takeIf { targetName == combatOpponent }
            ?: CombatOpponentVitals()
    val nextMax = previousVitals.maxHp?.coerceAtLeast(packet.targetHp)
    val nextVitals =
        previousVitals.copy(
            hp = packet.targetHp.coerceAtLeast(0L),
            maxHp = packet.targetMaxHp.takeIf { it > 0 } ?: nextMax,
            mp = packet.targetMp.takeIf { it >= 0 } ?: previousVitals.mp,
            maxMp = packet.targetMaxMp.takeIf { it > 0 } ?: previousVitals.maxMp,
            ep = packet.targetEp.takeIf { it >= 0 } ?: previousVitals.ep,
            maxEp = packet.targetMaxEp.takeIf { it > 0 } ?: previousVitals.maxEp,
        )
    val nextOpponents = currentOpponents.upsertCombatOpponent(targetName, nextVitals)
    val isPrimaryTarget = combatOpponent.isBlank() || targetName == combatOpponent
    return copy(
        combatOpponent = if (combatOpponent.isBlank()) targetName else combatOpponent,
        combatOpponentVitals = if (isPrimaryTarget) nextVitals else combatOpponentVitals,
        combatOpponents = nextOpponents,
    )
}

internal data class SkillRuntimeResult(
    val state: ShellUiState,
    val refreshCombatActions: Boolean = false,
)

/** 技能事件运行期结算：资源（复用 reduceSkillEventStatus）+ 冷却 + 忙乱 + 状态芯片。 */
internal fun reduceSkillEventRuntime(
    state: ShellUiState,
    packet: SkillEventPacket,
    nowMillis: Long,
): SkillRuntimeResult {
    var refreshCombatActions = false
    var next = state.reduceSkillEventStatus(packet)
    val actionRef = packet.combatActionRefOrBlank()
    if (actionRef.isNotBlank()) {
        val remaining =
            maxOf(packet.cooldownRemaining, packet.intervalRemaining, packet.cooldown)
        val total = maxOf(packet.cooldown, remaining)
        val label =
            packet.actionName.ifBlank {
                packet.skillName
            }.ifBlank { packet.actionId }.ifBlank { packet.skillId }
        val cooldown =
            CombatActionCooldown(label = label, remaining = remaining, total = total)
        val nextCooldowns =
            if (cooldown.active) {
                next.combatActionCooldowns + (actionRef to cooldown)
            } else {
                next.combatActionCooldowns - actionRef
            }
        if (nextCooldowns != next.combatActionCooldowns) {
            next = next.copy(combatActionCooldowns = nextCooldowns)
        }
    }

    if (!next.combatActive) {
        return SkillRuntimeResult(next, refreshCombatActions)
    }

    if (packet.busy > 0) {
        val currentRemaining =
            remainingSecondsAt(
                next.combatSelfBusySeconds,
                next.combatSelfBusyCapturedAtMillis,
                nowMillis,
            )
        if (packet.busy > currentRemaining) {
            next =
                next.copy(
                    combatSelfBusySeconds = packet.busy,
                    combatSelfBusyCapturedAtMillis = nowMillis,
                )
        }
    }

    val event = packet.event.trim().lowercase()
    if (packet.statuses.isNotEmpty()) {
        val remove = event == "status_expire" || event == "status_clear"
        val visibleStatuses =
            packet.statuses.filter { status ->
                status.combatPayloadInt("visible", 1) != 0 && status.combatPayloadString("category") != "action"
            }
        val selfStatuses = visibleStatuses.filter { it.combatStatusOwner() == "self" }
        val targetStatuses = visibleStatuses.filterNot { it.combatStatusOwner() == "self" }
        if (selfStatuses.isNotEmpty()) {
            val nextBuffs =
                if (remove) {
                    next.combatSelfBuffs.removeCombatBuffChips(selfStatuses)
                } else {
                    next.combatSelfBuffs.mergeCombatBuffChips(selfStatuses)
                }
            if (nextBuffs != next.combatSelfBuffs) {
                next = next.copy(combatSelfBuffs = nextBuffs)
                refreshCombatActions = true
            }
        }
        if (targetStatuses.isNotEmpty()) {
            val nextChips =
                if (remove) {
                    next.combatStatusChips.removeCombatBuffChips(targetStatuses)
                } else {
                    next.combatStatusChips.mergeCombatBuffChips(targetStatuses)
                }
            if (nextChips != next.combatStatusChips) {
                next = next.copy(combatStatusChips = nextChips)
            }
        }
        return SkillRuntimeResult(next, refreshCombatActions)
    }

    val effects =
        packet.statusEffects.ifEmpty {
            packet.message.ifBlank { packet.detailMessage }.toCombatStatusEffectObjects()
        }
    if (effects.isNotEmpty()) {
        if (packet.targetPolicy.trim().lowercase() == "combat_target") {
            val nextChips = next.combatStatusChips.mergeCombatBuffChips(effects)
            if (nextChips != next.combatStatusChips) {
                next = next.copy(combatStatusChips = nextChips)
            }
        } else {
            val nextBuffs = next.combatSelfBuffs.mergeCombatBuffChips(effects)
            if (nextBuffs != next.combatSelfBuffs) {
                next = next.copy(combatSelfBuffs = nextBuffs)
                refreshCombatActions = true
            }
        }
    }
    return SkillRuntimeResult(next, refreshCombatActions)
}

/** 回合状态效果结算：目标/自身 buff 芯片合并或移除。 */
internal fun ShellUiState.reduceRoundStatusEffects(
    packet: CombatRoundPacket,
    effects: List<JsonObject>,
    event: String,
): ShellUiState {
    val targetIsSelf = packet.target.matchesCurrentPlayerOrYou(statusSnapshot, username)
    return if (targetIsSelf) {
        val nextBuffs =
            when (event) {
                "status_expire", "status_clear" ->
                    combatSelfBuffs.removeCombatBuffChips(
                        effects,
                    )
                else -> combatSelfBuffs.mergeCombatBuffChips(effects)
            }
        if (nextBuffs == combatSelfBuffs) this else copy(combatSelfBuffs = nextBuffs)
    } else {
        val nextChips =
            when (event) {
                "status_expire", "status_clear" ->
                    combatStatusChips.removeCombatBuffChips(
                        effects,
                    )
                else -> combatStatusChips.mergeCombatBuffChips(effects)
            }
        if (nextChips == combatStatusChips) this else copy(combatStatusChips = nextChips)
    }
}

/** 伤害事件状态效果结算：按目标归属合并 buff 芯片。 */
internal fun ShellUiState.reduceCombatDamageStatusEffects(
    packet: CombatDamagePacket,
    effects: List<JsonObject>,
): ShellUiState {
    val targetIsSelf = packet.target.matchesCurrentPlayerOrYou(statusSnapshot, username)
    return if (targetIsSelf) {
        copy(combatSelfBuffs = combatSelfBuffs.mergeCombatBuffChips(effects))
    } else {
        copy(combatStatusChips = combatStatusChips.mergeCombatBuffChips(effects))
    }
}

/** 状态实例批量结算：self/target 芯片合并或移除。 */
internal fun ShellUiState.reduceCombatStatusInstances(
    statuses: List<JsonObject>,
    remove: Boolean,
): ShellUiState {
    val visibleStatuses =
        statuses.filter { status ->
            status.combatPayloadInt("visible", 1) != 0 && status.combatPayloadString("category") != "action"
        }
    if (visibleStatuses.isEmpty()) {
        return this
    }
    val selfStatuses = visibleStatuses.filter { it.combatStatusOwner() == "self" }
    val targetStatuses = visibleStatuses.filterNot { it.combatStatusOwner() == "self" }
    var next = this
    if (selfStatuses.isNotEmpty()) {
        val nextBuffs =
            if (remove) {
                next.combatSelfBuffs.removeCombatBuffChips(selfStatuses)
            } else {
                next.combatSelfBuffs.mergeCombatBuffChips(selfStatuses)
            }
        if (nextBuffs != next.combatSelfBuffs) {
            next = next.copy(combatSelfBuffs = nextBuffs)
        }
    }
    if (targetStatuses.isNotEmpty()) {
        val nextChips =
            if (remove) {
                next.combatStatusChips.removeCombatBuffChips(targetStatuses)
            } else {
                next.combatStatusChips.mergeCombatBuffChips(targetStatuses)
            }
        if (nextChips != next.combatStatusChips) {
            next = next.copy(combatStatusChips = nextChips)
        }
    }
    return next
}

/** 实时状态刷新：hp/mp 资源同步。 */
internal fun ShellUiState.reduceRealtimeStatus(
    resource: String,
    payload: JsonObject,
): ShellUiState {
    val snapshot = statusSnapshot ?: return this
    val nextSnapshot =
        when (resource.trim().lowercase()) {
            "hp" ->
                snapshot.copy(
                    hp =
                        payload.statusValue(
                            "hp",
                            payload.statusValue("value", snapshot.hp),
                        ).coerceAtLeast(0L),
                    maxHp =
                        payload.statusValue(
                            "max_hp",
                            snapshot.maxHp,
                        ).coerceAtLeast(0L),
                    effHp =
                        payload.statusValue(
                            "eff_hp",
                            snapshot.effHp,
                        ).coerceAtLeast(0L),
                )
            "mp" ->
                snapshot.copy(
                    mp =
                        payload.statusValue(
                            "mp",
                            payload.statusValue("value", snapshot.mp),
                        ).coerceAtLeast(0L),
                    maxMp =
                        payload.statusValue(
                            "max_mp",
                            snapshot.maxMp,
                        ).coerceAtLeast(0L),
                    effMp =
                        payload.statusValue(
                            "eff_mp",
                            snapshot.effMp,
                        ).coerceAtLeast(0L),
                    ep = payload.statusValue("ep", snapshot.ep).coerceAtLeast(0L),
                    maxEp = payload.statusValue("max_ep", snapshot.maxEp).coerceAtLeast(0L),
                )
            else -> snapshot
        }
    if (nextSnapshot == snapshot) return this
    return copy(statusSnapshot = nextSnapshot)
}

internal data class CombatRuntimeResult(
    val state: ShellUiState,
    val shouldRefreshCombatActions: Boolean = false,
)

/** 战斗运行期快照结算：非 active 收尾 / active 刷新对手与状态。 */
internal fun reduceCombatRuntime(
    state: ShellUiState,
    packet: CombatRuntimeSnapshotPacket,
    nowMillis: Long,
): CombatRuntimeResult {
    if (!packet.active) {
        return CombatRuntimeResult(
            state.copy(
                combatActive = false,
                exitOverlay = state.exitOverlay?.takeUnless { it.type == ExitOverlayType.COMBAT },
                combatOpponent = "",
                combatOpponentVitals = CombatOpponentVitals(),
                combatOpponents = emptyList(),
                combatStatusChips = emptyList(),
                combatSelfBuffs = emptyList(),
                combatSelfBusySeconds = 0,
                combatSelfBusyCapturedAtMillis = 0L,
                combatActionCooldowns = emptyMap(),
            ),
        )
    }
    if (!state.combatActive) {
        return CombatRuntimeResult(state)
    }
    val nextCooldowns = packet.cooldowns.toCombatActionCooldownMap(nowMillis)
    val nextSelfBuffs = packet.selfStatuses.toCombatBuffChips()
    val nextTargetStatuses = packet.targetStatuses.toCombatBuffChips()
    val nextOpponent = packet.opponent.ifBlank { state.combatOpponent }
    val currentOpponents =
        state.combatOpponents.ifEmpty {
            state.combatOpponent.takeIf { it.isNotBlank() }?.let {
                listOf(CombatOpponentState(it, state.combatOpponentVitals))
            }.orEmpty()
        }
    val nextOpponents = currentOpponents.upsertCombatOpponent(nextOpponent, CombatOpponentVitals())
    val shouldRefreshCombatActions = nextSelfBuffs != state.combatSelfBuffs
    return CombatRuntimeResult(
        state.copy(
            combatOpponent = nextOpponent,
            combatOpponents = nextOpponents,
            combatStatusChips = nextTargetStatuses,
            combatSelfBuffs = nextSelfBuffs,
            combatSelfBusySeconds = packet.busySeconds.coerceAtLeast(0),
            combatSelfBusyCapturedAtMillis = if (packet.busySeconds > 0) nowMillis else 0L,
            combatActionCooldowns = nextCooldowns,
        ),
        shouldRefreshCombatActions,
    )
}

/** 战斗开始结算：初始化对手与冷却状态。 */
internal fun ShellUiState.reduceCombatStart(
    opponent: String,
    opponentVitals: CombatOpponentVitals,
    opponents: List<CombatOpponentState> =
        listOf(CombatOpponentState(opponent, opponentVitals)).filter { it.name.isNotBlank() },
): ShellUiState {
    return copy(
        combatActive = true,
        combatOpponent = opponent,
        combatOpponentVitals = opponentVitals,
        combatOpponents = opponents,
        combatStatusChips = emptyList(),
        combatSelfBuffs = emptyList(),
        combatSelfBusySeconds = 0,
        combatSelfBusyCapturedAtMillis = 0L,
        combatActionCooldowns = emptyMap(),
    )
}

/** 战斗结束结算：清空战斗状态。正文由 coordinator 追加到主文本区。 */
internal fun ShellUiState.reduceCombatEnd(): ShellUiState {
    return copy(
        combatActive = false,
        exitOverlay = exitOverlay?.takeUnless { it.type == ExitOverlayType.COMBAT },
        combatOpponent = "",
        combatOpponentVitals = CombatOpponentVitals(),
        combatOpponents = emptyList(),
        combatStatusChips = emptyList(),
        combatSelfBuffs = emptyList(),
        combatActionCooldowns = emptyMap(),
        combatSelfBusySeconds = 0,
        combatSelfBusyCapturedAtMillis = 0L,
    )
}
