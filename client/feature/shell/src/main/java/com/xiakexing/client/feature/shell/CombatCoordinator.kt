package com.xiakexing.client.feature.shell

import com.google.gson.JsonObject
import com.xiakexing.client.core.protocol.payload.CombatDamagePacket
import com.xiakexing.client.core.protocol.payload.CombatEndPacket
import com.xiakexing.client.core.protocol.payload.CombatRoundPacket
import com.xiakexing.client.core.protocol.payload.CombatRuntimeSnapshotPacket
import com.xiakexing.client.core.protocol.payload.CombatStartPacket
import com.xiakexing.client.core.protocol.payload.SkillEventPacket

/**
 * Combat 域 owner——战斗包编排与状态提交。
 * 状态变换在 CombatReducers.kt，工具在 CombatStatusTools.kt，
 * 文本构造在 CombatMessageRendering.kt。
 */
internal class CombatCoordinator(
    private val sink: CombatSink,
) {
    fun handleCombatStartPacket(payload: JsonObject) {
        handleCombatStartPacket(CombatStartPacket.fromJson(payload))
    }

    fun handleCombatStartPacket(packet: CombatStartPacket) {
        val opponentStates = packet.toOpponentStates()
        val opponent = buildCombatOpponentLabel(packet)
        val startLines = buildCombatStartLines(packet)
        val opponentVitals =
            packet.toOpponentVitalsOrNull()
                ?: opponentStates.firstOrNull()?.vitals
                ?: CombatOpponentVitals()
        sink.dismissObscuringSurfacesForCombatStart()
        sink.updateState { reduceCombatStart(opponent, opponentVitals, opponentStates) }
        startLines.forEach(sink::appendCombatTranscript)
        sink.openCombatActionHubPanel()
    }

    fun handleCombatDamagePacket(payload: JsonObject) {
        handleCombatDamagePacket(CombatDamagePacket.fromJson(payload))
    }

    fun handleCombatDamagePacket(packet: CombatDamagePacket) {
        applyCombatDamageToStatus(packet)
        applyCombatDamageToOpponentVitals(packet)
        applyCombatDamageStatusEffects(packet)
        packet.message.ifBlank { packet.detailMessage }.takeIf { it.isNotBlank() }?.let { message ->
            sink.appendCombatTranscript(message)
            return
        }
        sink.appendCombatTranscript(buildCombatDamageFallback(packet))
    }

    fun handleCombatRoundPacket(payload: JsonObject) {
        handleCombatRoundPacket(CombatRoundPacket.fromJson(payload))
    }

    fun handleCombatRoundPacket(packet: CombatRoundPacket) {
        applyRoundStatusEffects(packet)
        val message = packet.message.ifBlank { packet.detailMessage }
        if (message.isNotBlank()) {
            sink.appendCombatTranscript(message)
            return
        }
        buildCombatRoundFallback(packet)?.let(sink::appendCombatTranscript)
    }

    fun handleSkillEventPacket(payload: JsonObject) {
        handleSkillEventPacket(SkillEventPacket.fromJson(payload))
    }

    fun handleSkillEventPacket(packet: SkillEventPacket) {
        val runtimeUpdate = applySkillEventRuntime(packet)
        if (runtimeUpdate.refreshCombatActions) {
            sink.refreshCombatExitOverlayIfVisible()
        }
        val message = buildSkillEventMessage(packet)
        if (message.isBlank()) return
        sink.appendCombatTranscript(message)
    }

    fun handleCombatRuntimePacket(payload: JsonObject) {
        handleCombatRuntimePacket(CombatRuntimeSnapshotPacket.fromJson(payload))
    }

    fun handleCombatRuntimePacket(packet: CombatRuntimeSnapshotPacket) {
        val result = reduceCombatRuntime(sink.currentState(), packet, System.currentTimeMillis())
        sink.updateState { result.state }
        if (result.shouldRefreshCombatActions) {
            sink.refreshCombatExitOverlayIfVisible()
        }
    }

    fun handleRealtimeStatusPacket(
        resource: String,
        payload: JsonObject,
    ) {
        sink.updateState { reduceRealtimeStatus(resource, payload) }
    }

    fun handleCombatEndPacket(payload: JsonObject) {
        handleCombatEndPacket(CombatEndPacket.fromJson(payload))
    }

    fun handleCombatEndPacket(packet: CombatEndPacket) {
        val message =
            packet.message.ifBlank { packet.detailMessage }
                .ifBlank { buildCombatEndFallback(packet) }
        sink.updateState { reduceCombatEnd() }
        sink.appendCombatTranscript(message)
    }

    private fun applySkillEventRuntime(packet: SkillEventPacket): SkillRuntimeResult {
        val result =
            reduceSkillEventRuntime(
                sink.currentState(),
                packet,
                System.currentTimeMillis(),
            )
        sink.updateState { result.state }
        return result
    }

    private fun applyRoundStatusEffects(packet: CombatRoundPacket) {
        val state = sink.currentState()
        if (!state.combatActive) {
            return
        }
        val event = packet.event.trim().lowercase()
        if (packet.statuses.isNotEmpty()) {
            applyCombatStatusInstances(packet.statuses, event)
            return
        }
        val effects = buildRoundStatusEffects(packet, event)
        if (effects.isEmpty()) return
        sink.updateState { reduceRoundStatusEffects(packet, effects, event) }
    }

    private fun applyCombatDamageStatusEffects(packet: CombatDamagePacket) {
        val state = sink.currentState()
        if (!state.combatActive) return
        if (packet.statuses.isNotEmpty()) {
            applyCombatStatusInstances(packet.statuses, packet.event.trim().lowercase())
            return
        }
        val effects = buildDamageStatusEffects(packet)
        if (effects.isEmpty()) return
        sink.updateState { reduceCombatDamageStatusEffects(packet, effects) }
    }

    private fun applyCombatStatusInstances(
        statuses: List<JsonObject>,
        event: String,
    ) {
        val remove = event == "status_expire" || event == "status_clear"
        sink.updateState { reduceCombatStatusInstances(statuses, remove) }
    }

    private fun applyCombatDamageToStatus(packet: CombatDamagePacket) {
        sink.updateState { reduceCombatDamageStatus(packet) }
    }

    private fun applyCombatDamageToOpponentVitals(packet: CombatDamagePacket) {
        sink.updateState { reduceCombatDamageOpponentVitals(packet) }
    }
}
