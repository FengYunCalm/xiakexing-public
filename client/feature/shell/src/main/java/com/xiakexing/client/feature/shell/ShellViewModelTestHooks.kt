package com.xiakexing.client.feature.shell

import androidx.annotation.VisibleForTesting
import com.google.gson.JsonObject
import com.google.gson.JsonParser
import com.xiakexing.client.core.network.GameConnectionRepository
import com.xiakexing.client.core.protocol.payload.SkillEventPacket

/**
 * ShellViewModel 测试钩子（VM 瘦身——测试面外移为扩展函数，
 * 测试调用 viewModel.xxxForTest() 不变）。
 */
@VisibleForTesting
internal suspend fun ShellViewModel.handleIncomingMessageForTest(rawMessage: String) {
    handleIncomingMessage(normalizeProjectionFixture(rawMessage))
}

/**
 * 行为测试中的短包字面量默认代表当前 v2 投影。真实协议仍由 gate 严格校验；
 * 需要覆盖缺失投影的拒绝路径时直接测试 [XkProtocolPacketGate]。
 */
private fun normalizeProjectionFixture(rawMessage: String): String {
    val match = TEST_PACKET_PATTERN.matchEntire(rawMessage) ?: return rawMessage
    val type = match.groupValues[1]
    if (type !in PROJECTION_SCOPED_FIXTURE_TYPES) {
        return rawMessage
    }
    val payload =
        runCatching {
            JsonParser.parseString(match.groupValues[2]).asJsonObject
        }.getOrNull()
            ?: return rawMessage
    if (type in PROJECTION_SCOPED_FIXTURE_TYPES) {
        if (!payload.has("protocol_version")) {
            payload.addProperty("protocol_version", "2.3")
        }
        if (!payload.has("projection_id")) {
            payload.addProperty("projection_id", "projection-public")
        }
        if (!payload.has("projection_revision")) {
            payload.addProperty("projection_revision", 1)
        }
        if (!payload.has("projection_generation")) {
            payload.addProperty("projection_generation", 1)
        }
    }
    return "\u001bXK$type$payload\u001b\n"
}

private val TEST_PACKET_PATTERN =
    Regex(
        "^\\u001b(?:\\[)?XK([A-Z0-9]{4})(\\{.*})\\u001b(?:\\n)?$",
        RegexOption.DOT_MATCHES_ALL,
    )
private val PROJECTION_SCOPED_FIXTURE_TYPES = setOf("QUES", "QUEU", "ROUT")

@VisibleForTesting
internal fun ShellViewModel.setUiStateForTest(state: ShellUiState) {
    check(stateBatchDepth == 0) { "Cannot replace UI state during a packet batch" }
    batchedUiState = null
    _uiState.value = state
}

@VisibleForTesting
internal fun ShellViewModel.prepareInteractionSheetCommandForTest(command: String): Boolean {
    val prepared = prepareInteractionSheetCommand(command)
    if (prepared) {
        requireShellFacade.bindTestCommandRequest(command)
    }
    return prepared
}

@VisibleForTesting
internal fun ShellViewModel.requestInventoryItemDetailForTest(itemId: String) {
    requestInventoryItemDetail(itemId)
}

@VisibleForTesting
internal fun ShellViewModel.requestSceneItemDetailForTest(target: InteractionObject) {
    requestSceneItemDetail(target)
}

@VisibleForTesting
internal fun ShellViewModel.prepareProtocolSheetCommandForTest(sheet: ShellSheetState) {
    prepareProtocolSheetCommand(sheet)
    requireShellFacade.bindTestCommandRequest("")
}

@VisibleForTesting
internal fun ShellViewModel.prepareProtocolPopupCommandForTest(popup: ShellPopupState) {
    prepareProtocolPopupCommand(popup)
    requireShellFacade.bindTestCommandRequest("")
}

@VisibleForTesting
internal fun ShellViewModel.appendTranscriptForTest(line: String) {
    requireShellFacade.appendTranscriptForTest(line)
}

@VisibleForTesting
internal fun ShellViewModel.handlePromptPacketForTest(payload: JsonObject) {
    requireShellFacade.handlePromptPacketForTest(payload)
}

@VisibleForTesting
internal fun ShellViewModel.handleErrorPacketForTest(payload: JsonObject) {
    requireShellFacade.handleErrorPacketForTest(payload)
}

@VisibleForTesting
internal fun ShellViewModel.handleDialogPacketForTest(payload: JsonObject) {
    requireShellFacade.handleDialogPacketForTest(payload)
}

@VisibleForTesting
internal fun ShellViewModel.handleSkillListPacketForTest(payload: JsonObject) {
    requireShellFacade.handleSkillListPacketForTest(payload)
}

@VisibleForTesting
internal fun ShellViewModel.handleSkillInfoPacketForTest(payload: JsonObject) {
    requireShellFacade.handleSkillInfoPacketForTest(payload)
}

@VisibleForTesting
internal fun ShellViewModel.handleSkillEventPacketForTest(payload: JsonObject) {
    val packet = SkillEventPacket.fromJson(payload)
    requireShellFacade.handleSkillEventPacketForTest(packet)
}

@VisibleForTesting
internal fun ShellViewModel.handlePhaseEventForTest(event: GameConnectionRepository.PhaseEvent) {
    requireShellFacade.handlePhaseEvent(event)
}

@VisibleForTesting
internal fun ShellViewModel.resumeLoginFallbackAllowedForTest(): Boolean {
    return requireShellFacade.resumeLoginFallbackAllowed()
}

@VisibleForTesting
internal fun ShellViewModel.armPendingMoveAutoScrollForTest() {
    armPendingMoveAutoScroll()
}

@VisibleForTesting
internal fun ShellViewModel.latestMapRowsForTest(): List<String> {
    return requireShellFacade.latestMapRowsForTest()
}

@VisibleForTesting
internal fun ShellViewModel.clearForTest() {
    clearForTestInternal()
}
