package com.xiakexing.client.feature.shell

/**
 * Command 域 owner——命令提交、聊天面板与直接命令副作用。
 * 纯变换在 CommandInputReducer.kt。
 */
internal class CommandCoordinator(
    private val sink: CommandSink,
) {
    private var lastDirectCommand: String = ""
    private var lastDirectCommandAtNanos: Long = 0L

    fun sendCommand() {
        when (val decision = sink.currentState().resolveCommandSubmitDecision()) {
            null -> return
            else ->
                when (decision.route) {
                    CommandSubmitRoute.CHAT_MESSAGE -> sendChatMessage(decision.command)
                    CommandSubmitRoute.CLEAR_INPUT_ONLY ->
                        sink.updateState {
                            copy(
                                commandInput = "",
                            )
                        }
                    CommandSubmitRoute.DIRECT_COMMAND ->
                        sendDirectCommand(
                            decision.command,
                            clearInputAfterSend = true,
                        )
                }
        }
    }

    fun sendChatMessage(message: String) {
        sendChatMessage(message, ChatChannel.WORLD)
    }

    fun sendChatMessage(
        message: String,
        channel: ChatChannel,
    ) {
        if (sink.currentState().clientUpdateRequired) {
            return
        }
        val normalizedMessage = message.trim()
        if (normalizedMessage.isBlank()) {
            return
        }
        val command =
            when (channel.kind) {
                ChatChannelKind.WORLD -> "shout $normalizedMessage"
                ChatChannelKind.NEARBY -> "say $normalizedMessage"
                ChatChannelKind.PRIVATE -> {
                    val peerName = channel.peerName.trim()
                    if (peerName.isBlank()) return
                    normalizedMessage.takeIf { it.startsWith("tell ") || it.startsWith("whisper ") }
                        ?: "tell $peerName $normalizedMessage"
                }
            }
        val request = sink.reserveCommandRequest()
        sink.bindPendingCommandRequest(request, command)
        if (sink.sendCommand(request, command)) {
            sink.updateState {
                copy(
                    commandInput = "",
                    chatInput = "",
                    commandPalette = null,
                    transcriptScrollRequest = transcriptScrollRequest + 1,
                )
            }
        } else {
            sink.rollbackPendingCommandRequest(request)
            // 方案 R-30：发送失败 → 本地失败气泡（Cinnabar 感叹号 + 点击重发），不伪造服务器消息
            sink.appendErrorFeedback("当前连接不可发送聊天")
            sink.updateState {
                copy(
                    chatMessages =
                        chatMessages +
                            ChatMessageItem(
                                id = "local-failed-${System.currentTimeMillis()}",
                                content = normalizedMessage,
                                channel = channel,
                                channelName = channel.localChannelName(),
                                senderName = "我",
                                direction = ChatDirection.SELF,
                                timestamp = System.currentTimeMillis(),
                                localFailed = true,
                                localCommand = command,
                            ),
                )
            }
        }
    }

    /** 方案 R-30：点击失败气泡重发；成功后移除本地气泡，等服务器广播上屏。 */
    fun resendChatMessage(command: String) {
        if (sink.currentState().clientUpdateRequired) {
            return
        }
        val request = sink.reserveCommandRequest()
        sink.bindPendingCommandRequest(request, command)
        if (sink.sendCommand(request, command)) {
            sink.updateState {
                copy(
                    chatMessages = chatMessages.filterNot { it.localFailed && it.localCommand == command },
                    transcriptScrollRequest = transcriptScrollRequest + 1,
                )
            }
        } else {
            sink.rollbackPendingCommandRequest(request)
            sink.appendErrorFeedback("当前连接不可发送聊天")
        }
    }

    fun openChatPanel() {
        sink.clearPendingInteractionRequest()
        sink.updateState {
            val remainingUnreadChannels = unreadChatChannels - activeChatChannel
            copy(
                chatPanelOpen = true,
                chatUnreadCount = remainingUnreadChannels.size,
                unreadChatChannels = remainingUnreadChannels,
                commandPalette = null,
            )
        }
    }

    fun closeChatPanel() {
        sink.updateState { copy(chatPanelOpen = false) }
    }

    fun selectChatChannel(channel: ChatChannel) {
        sink.updateState {
            val remainingUnreadChannels = unreadChatChannels - channel
            copy(
                activeChatChannel = channel,
                unreadChatChannels = remainingUnreadChannels,
                chatUnreadCount = remainingUnreadChannels.size,
            )
        }
    }

    fun sendChatPanelMessage() {
        val current = sink.currentState()
        sendChatMessage(current.chatInput, current.activeChatChannel)
    }

    fun submitRegisterOption(value: String) {
        val line = value.trim()
        if (line.isBlank()) {
            return
        }
        sink.updateState { copy(commandInput = line) }
        if (sink.sendRawLine(line)) {
            sink.updateState { copy(commandInput = "") }
        } else {
            sink.appendErrorFeedback("当前连接不可发送注册选项")
        }
    }

    fun sendDirectCommand(
        command: String,
        clearInputAfterSend: Boolean = false,
        suppressImmediateScroll: Boolean = false,
        bypassThrottle: Boolean = false,
    ) {
        if (command.isBlank() || sink.currentState().clientUpdateRequired) {
            return
        }
        if (!bypassThrottle && shouldThrottleDirectCommand(command)) {
            return
        }

        val request = sink.reserveCommandRequest()
        val shouldHandleInsideInteractionSheet = sink.prepareInteractionSheetCommand(command)
        sink.bindPendingCommandRequest(request, command)
        if (sink.sendCommand(request, command)) {
            rememberDirectCommand(command)
            if (!shouldHandleInsideInteractionSheet && sink.pendingInteractionRequest() == null) {
                command.toManualNpcConversationLeadLine(
                    sink.currentState(),
                )?.let(sink::appendNarrativeTranscript)
            }
            val currentState = sink.currentState()
            val nextCommandInput = if (clearInputAfterSend) "" else currentState.commandInput
            val nextTranscriptScrollRequest =
                if (suppressImmediateScroll || shouldHandleInsideInteractionSheet) {
                    currentState.transcriptScrollRequest
                } else {
                    currentState.transcriptScrollRequest + 1
                }
            if (nextCommandInput != currentState.commandInput ||
                nextTranscriptScrollRequest != currentState.transcriptScrollRequest
            ) {
                sink.updateState {
                    copy(
                        commandInput = nextCommandInput,
                        transcriptScrollRequest = nextTranscriptScrollRequest,
                    )
                }
            }
        } else {
            sink.rollbackPendingCommandRequest(request)
            if (suppressImmediateScroll) {
                sink.clearPendingMoveAutoScroll()
            }
            sink.appendErrorFeedback("当前连接不可执行操作")
        }
    }

    private fun shouldThrottleDirectCommand(command: String): Boolean {
        val now = System.nanoTime()
        val elapsedNanos = now - lastDirectCommandAtNanos
        return command == lastDirectCommand && elapsedNanos in 0 until DIRECT_COMMAND_THROTTLE_NANOS
    }

    private fun rememberDirectCommand(command: String) {
        lastDirectCommand = command
        lastDirectCommandAtNanos = System.nanoTime()
    }

    private fun String.toManualNpcConversationLeadLine(state: ShellUiState): String? {
        val command = trim()
        val normalized = command.lowercase()
        val isAsk = normalized.startsWith("ask ")
        val isTalk = normalized.startsWith("talk ")
        if (!isAsk && !isTalk) {
            return null
        }
        val targetToken =
            if (isAsk) {
                command.removePrefix("ask ").substringBefore(" about ").trim()
            } else {
                command.removePrefix("talk ").trim()
            }
        if (targetToken.isBlank()) {
            return null
        }
        val target =
            state.interactionObjects.firstOrNull { objectInfo ->
                objectInfo.kind == InteractionObjectKind.NPC &&
                    (
                        objectInfo.id.equals(targetToken, ignoreCase = true) ||
                            objectInfo.commandTarget().equals(targetToken, ignoreCase = true)
                    )
            }
        val targetTitle = target?.title?.ifBlank { targetToken } ?: targetToken
        if (isTalk) {
            return "你向${targetTitle}打听"
        }
        val topic =
            command.substringAfter(" about ", missingDelimiterValue = "")
                .trim()
                .toManualTopicLabel()
        return "你向${targetTitle}打听$topic"
    }

    private fun String.toManualTopicLabel(): String {
        return when (lowercase()) {
            "road", "route" -> "路线"
            "quest" -> "任务"
            "help" -> "帮助"
            else -> this
        }
    }

    private companion object {
        private const val DIRECT_COMMAND_THROTTLE_NANOS = 150_000_000L
    }
}

internal fun ChatChannel.localChannelName(): String {
    return when (kind) {
        ChatChannelKind.WORLD -> "世界"
        ChatChannelKind.NEARBY -> "附近"
        ChatChannelKind.PRIVATE -> peerName.ifBlank { "私聊" }
    }
}

/**
 * 直接命令是否触发背包刷新：命令域侧分类器（get/drop/give/…）。
 * 原挂 ShellViewModel 文件底部，归口命令域（行为不变）。
 */
internal fun String.requiresInventoryRefresh(): Boolean {
    val normalized = trim().lowercase()
    return normalized.startsWith("get ") ||
        normalized.startsWith("drop ") ||
        normalized.startsWith("give ") ||
        normalized.startsWith("eat ") ||
        normalized.startsWith("drink ") ||
        normalized.startsWith("equip ") ||
        normalized.startsWith("unequip ") ||
        normalized.startsWith("buy ") ||
        normalized.startsWith("sell ")
}
