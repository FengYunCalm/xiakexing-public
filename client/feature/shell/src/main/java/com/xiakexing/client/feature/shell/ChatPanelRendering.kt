package com.xiakexing.client.feature.shell

import com.xiakexing.client.core.protocol.payload.ChatPacket
import com.xiakexing.client.core.protocol.payload.MessageEventPacket

internal fun ChatPacket.toChatMessageItemOrNull(sequence: Long): ChatMessageItem? {
    val normalizedContent = content.normalizeInteractionLine() ?: content.trim()
    val normalizedChannel = channel.trim().lowercase()
    val resolvedDirection =
        when (direction.trim().lowercase()) {
            "self" -> ChatDirection.SELF
            "system" -> ChatDirection.SYSTEM
            else -> ChatDirection.INCOMING
        }
    val peerName = privatePeerName(resolvedDirection)
    val resolvedChannel =
        when (normalizedChannel) {
            "say" -> ChatChannel.NEARBY
            "shout" -> ChatChannel.WORLD
            "tell", "whisper" -> ChatChannel.privateWith(peerName)
            else -> return null
        }
    return ChatMessageItem(
        id = "${timestamp.takeIf { it > 0L } ?: System.nanoTime()}:$sequence:${normalizedContent.hashCode()}",
        content = normalizedContent,
        channel = resolvedChannel,
        channelName = resolvedChannel.displayName(),
        senderName = senderName.ifBlank { if (resolvedDirection == ChatDirection.SELF) "我" else "有人" },
        direction = resolvedDirection,
        timestamp = timestamp,
        targetName = targetName,
    )
}

internal fun MessageEventPacket.toChatMessageItemOrNull(sequence: Long): ChatMessageItem? {
    val eventPayload = payload
    return ChatPacket(
        content = text,
        channel =
            eventPayload.shellString("chat_type")
                .ifBlank { eventPayload.shellString("channel") }
                .ifBlank { eventPayload.shellString("type") },
        channelName = eventPayload.shellString("channel_name"),
        senderName = eventPayload.shellString("sender_name"),
        direction = eventPayload.shellString("direction"),
        timestamp = eventPayload.shellLong("timestamp").takeIf { it > 0L } ?: timestamp,
        targetName = eventPayload.shellString("target_name"),
    ).toChatMessageItemOrNull(sequence)
}

internal fun ChatChannel.displayName(): String {
    return when (this) {
        ChatChannel.WORLD -> "世界"
        ChatChannel.NEARBY -> "附近"
        else -> peerName.ifBlank { "密语" }
    }
}

internal fun ShellUiState.currentPlayerChatName(): String {
    return statusSnapshot?.name?.trim().orEmpty()
        .ifBlank { username.trim() }
        .ifBlank { "我" }
}

private fun ChatPacket.privatePeerName(direction: ChatDirection): String {
    val target = targetName.trim()
    val sender = senderName.trim()
    val channel = channelName.trim().takeUnless { it == "私聊" || it == "密语" }.orEmpty()
    return when (direction) {
        ChatDirection.SELF -> target.ifBlank { sender }.ifBlank { channel }
        else -> sender.ifBlank { target }.ifBlank { channel }
    }
}

private fun com.google.gson.JsonObject.shellLong(key: String): Long {
    return if (has(key) && !get(key).isJsonNull) {
        runCatching {
            get(key).asLong
        }.getOrDefault(0L)
    } else {
        0L
    }
}
