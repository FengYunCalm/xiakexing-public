package com.xiakexing.client.feature.shell
import com.google.gson.JsonObject
import com.xiakexing.client.core.protocol.payload.ChatPacket
import com.xiakexing.client.core.protocol.payload.MessageEventPacket
import com.xiakexing.client.core.protocol.payload.PromptPacket
import com.xiakexing.client.core.protocol.payload.SystemPacket

/** Message 域 owner；纯变换与会话状态分别由对应 reducer/state 持有。 */
internal class MessageCoordinator(
    private val interactionCaptureOrchestrator: ShellInteractionCaptureOrchestrator,
    internal val sink: MessageCoordinatorSink,
) {
    internal val projection = MessageProjectionState()

    fun recentTranscriptLines(): List<String> = projection.recentTranscriptLines

    fun recentNoticeFeed(): List<String> = projection.recentNoticeFeed

    fun recentMessageLines(): List<String> = projection.recentMessageLines()

    fun resetForConnection(
        endpointLine: String,
        preserveNoticeFeed: Boolean,
    ) {
        projection.resetForConnection(endpointLine, preserveNoticeFeed)
    }

    fun clearFeeds() {
        projection.clear()
    }

    fun appendFloatingFeedback(
        text: String,
        level: String = "warning",
        dedupeKey: String = "feedback:${text.trim()}",
    ) {
        val normalized = text.trim()
        if (normalized.isBlank()) return
        sink.updateState(
            buildFloatingEventAppendTransform(
                FloatingMessageState(
                    text = normalized,
                    level = level,
                    ttlMs = DEFAULT_FLOATING_MESSAGE_TTL_MS,
                    dedupeKey = dedupeKey,
                ),
            ),
        )
    }

    /** 第五轮 R4：设施选技空态等通用提示飘窗。 */
    fun appendFloatingNotice(text: String) {
        appendFloatingFeedback(text, level = "info", dedupeKey = "notice:${text.trim()}")
    }

    fun visibleTranscript(): List<TranscriptLine> =
        buildVisibleTranscript(projection.recentTranscriptLines)

    fun handleErrorPacket(
        payload: JsonObject,
        commandResponseId: String = "",
    ) {
        sink.flushPendingRoomRefresh()
        sink.clearInventoryRefreshPending()
        val message = payload.shellString("message").ifBlank { "服务器返回错误包" }
        val captureAllowed =
            interactionCaptureOrchestrator.acceptsCommandResponseForPending(
                commandResponseId,
            )
        if (payload.isCombatActionError()) {
            if (captureAllowed) {
                appendCombatActionFeedback(message, true, projection, ::appendTranscriptToFeed)
            }
            return
        }
        if (captureAllowed && sink.capturePendingError(message)) {
            return
        }
        val plan = buildErrorPacketPlan(sink.currentState(), sink.shouldShowLoginError())
        if (plan.appendStructured) {
            appendStructuredErrorFeedback(message)
        }
        if (plan.showLoginError) {
            sink.showLoginError(message)
        } else if (plan.reportConnectionFailed) {
            sink.onErrorPacketConnectionFailed(message)
        }
    }

    fun handleMessageEventPacket(
        packet: MessageEventPacket,
        commandResponseId: String = "",
    ) {
        val captureAllowed =
            interactionCaptureOrchestrator.acceptsCommandResponseForPending(
                commandResponseId,
            )
        if (commandResponseId.isBlank() || captureAllowed) {
            sink.updateState { copy(inlineActionChips = packet.inlineActions) }
        }
        when (classifyMessageEvent(packet)) {
            MessageEventKind.Floating -> handleFloatingMessageEvent(packet, captureAllowed)
            MessageEventKind.Chat -> handleChatMessageEvent(packet)
            MessageEventKind.Dialog -> if (captureAllowed) handleDialogMessageEvent(packet)
            MessageEventKind.Combat -> handleCombatMessageEvent(packet, captureAllowed)
            MessageEventKind.QuestProgress -> appendQuestProgressToTranscript(packet.text)
            MessageEventKind.ItemPickup -> {
                sink.flushPendingRoomRefresh()
                projection.markPendingStructuredMessageEcho(packet.text, suppressions = 1)
                appendTranscriptToFeed(packet.text)
            }
            MessageEventKind.Error -> handleErrorChannelMessage(packet, captureAllowed)
            MessageEventKind.MainChannel -> {
                sink.flushPendingRoomRefresh()
                appendStructuredVisibleMessage(packet.text)
            }
            MessageEventKind.Silent, MessageEventKind.Blank -> Unit
        }
    }

    private fun handleErrorChannelMessage(
        packet: MessageEventPacket,
        captureAllowed: Boolean,
    ) {
        sink.flushPendingRoomRefresh()
        sink.clearInventoryRefreshPending()
        if (packet.payload.isCombatActionError()) {
            appendCombatActionFeedback(packet.text, false, projection, ::appendNarrativeTranscript)
            return
        }
        if (captureAllowed && sink.capturePendingError(packet.text)) {
            return
        }
        val shown = appendStructuredVisibleMessage(packet.text)
        if (shown && sink.shouldShowLoginError()) {
            sink.showLoginError(packet.text)
        }
    }

    private fun handleFloatingMessageEvent(
        packet: MessageEventPacket,
        captureAllowed: Boolean,
    ) {
        sink.flushPendingRoomRefresh()
        /* 第六轮真机纠偏：服务端 quest_hint 飘窗是「任务」点击的应答，早派发
         * 绕过了 capture/套索链——经状态属主守卫结算（仅 quest 类 pending），
         * 并同步复位 dock 等 fallthrough 源面板上的 loading 态。 */
        if (captureAllowed && isQuestHintCollapse(packet)) {
            sink.settleIfAwaitingQuestAnswer()
            sink.updateState(buildQuestHintSettleTransform())
        }
        val message = buildFloatingMessageState(packet) ?: return
        sink.updateState(buildFloatingEventAppendTransform(message))
    }

    private fun handleChatMessageEvent(packet: MessageEventPacket) {
        sink.flushPendingRoomRefresh()
        val message = buildChatMessageItem(packet, projection.nextChatMessageSequence()) ?: return
        appendChatMessage(message)
    }

    private fun handleDialogMessageEvent(packet: MessageEventPacket) {
        sink.flushPendingRoomRefresh()
        val lines = packet.text.toParagraphs()
        if (lines.isEmpty()) {
            return
        }
        val update = buildDialogMessageStateUpdate(packet, sink.currentState().sheet, lines) ?: return
        sink.updateState(buildDialogAppendTransform(update))
    }

    private fun handleCombatMessageEvent(
        packet: MessageEventPacket,
        captureAllowed: Boolean,
    ) {
        sink.flushPendingRoomRefresh()
        val normalized = packet.text.normalizeInteractionLine() ?: return
        if (captureAllowed && !sink.currentState().combatActive &&
            interactionCaptureOrchestrator.hasPendingCombatStart()
        ) {
            sink.clearPendingInteractionRequest()
        }
        appendNarrativeTranscript(normalized)
    }

    fun handleSystemPacket(payload: JsonObject) {
        sink.flushPendingRoomRefresh()
        val packet = SystemPacket.fromJson(payload)
        val current = sink.currentState()
        val plan =
            buildSystemPacketPlan(
                packet = packet,
                appScene = current.appScene,
                shouldShowLoginError = sink.shouldShowLoginError(),
                onMapAlreadyLatest = sink.onMapAlreadyLatest(),
            ) ?: return
        if (plan.showLoginReadyBanner) {
            sink.showOrDeferLoginReadyBanner()
            return
        }
        if (plan.transitionToWorldLoading) {
            sink.updateState(buildWorldLoadingTransform())
        }
        if (plan.showLoginError) sink.showLoginError(packet.content)
        appendTranscript(packet.content)
        if (plan.appendNotice) {
            appendNotice(packet.content)
        }
    }

    fun handleChatPacket(payload: JsonObject) {
        sink.flushPendingRoomRefresh()
        val packet = ChatPacket.fromJson(payload)
        val message = packet.toChatMessageItemOrNull(projection.nextChatMessageSequence()) ?: return
        if (message.content.isBlank()) return
        if (message.content.isLoginSuccessStatusLine()) {
            sink.showOrDeferLoginReadyBanner()
            return
        }
        appendChatMessage(message)
    }

    private fun appendChatMessage(message: ChatMessageItem) {
        val current = sink.currentState()
        val update =
            buildChatAppendUpdate(
                message = message,
                chatMessages = current.chatMessages,
                chatPanelOpen = current.chatPanelOpen,
                activeChatChannel = current.activeChatChannel,
                unreadChatChannels = current.unreadChatChannels,
            ) ?: return
        sink.updateState(buildChatAppendTransform(update))
    }

    fun handlePromptPacket(
        payload: JsonObject,
        commandResponseId: String = "",
    ) {
        sink.flushPendingRoomRefresh()
        val packet = PromptPacket.fromJson(payload)
        if (packet.prompt.isBlank()) {
            return
        }
        val current = sink.currentState()
        val plan = buildPromptPlan(packet, current, projection.recentTranscriptLines)
        if (plan.showLoginError) {
            sink.showLoginError(plan.loginErrorMessage)
            return
        }
        val captureAllowed =
            interactionCaptureOrchestrator.acceptsCommandResponseForPending(
                commandResponseId,
            )
        if (captureAllowed) {
            val settlePlan = interactionCaptureOrchestrator.settleOnPrompt()
            sink.applyPromptSettlePlan(settlePlan)
        }
        if (plan.updateLatestPrompt) sink.updateState { copy(latestPrompt = packet.prompt) }
    }

    fun appendTranscript(
        line: String,
        commandResponseId: String = "",
    ) {
        val captureAllowed =
            interactionCaptureOrchestrator.acceptsCommandResponseForPending(
                commandResponseId,
            )
        val classification =
            classifyTranscriptLine(
                line = line,
                recentTranscriptLines = projection.recentTranscriptLines,
                pendingEcho = projection.pendingStructuredMessageEcho,
                suppressions = projection.pendingStructuredMessageEchoSuppressions,
            )
        when (classification) {
            TranscriptLinePlan.Ignored -> return
            TranscriptLinePlan.EchoConsumed -> {
                projection.consumePendingStructuredMessageEcho(line)
                return
            }
            is TranscriptLinePlan.LoginFailure -> {
                if (sink.shouldShowLoginError()) {
                    sink.showLoginError(classification.message)
                }
            }
            TranscriptLinePlan.Feed -> Unit
        }
        if (captureAllowed) {
            interactionCaptureOrchestrator.pendingCombatStartFailure(line)?.let { message ->
                sink.clearPendingInteractionRequest()
                appendFloatingFeedback(message)
                return
            }
            val capturePlan = interactionCaptureOrchestrator.collectTranscriptLine(line)
            if (sink.applyTranscriptCapturePlan(capturePlan)) {
                return
            }
        }
        if (line.isPromptOnlyLine()) {
            sink.updateState { copy(latestPrompt = line.trim()) }
            return
        }
        if (line.shouldSuppressMainTranscriptLine()) {
            return
        }

        appendTranscriptToFeed(line)
    }
}
