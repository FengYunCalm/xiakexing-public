package com.xiakexing.client.feature.shell

import androidx.compose.runtime.Immutable

@Immutable
data class ChatChannel(
    val kind: ChatChannelKind,
    val peerName: String = "",
) {
    companion object {
        val NEARBY = ChatChannel(ChatChannelKind.NEARBY)
        val WORLD = ChatChannel(ChatChannelKind.WORLD)

        fun privateWith(peerName: String): ChatChannel {
            return ChatChannel(ChatChannelKind.PRIVATE, peerName.trim())
        }
    }
}

@Immutable
data class ChatMessageItem(
    val id: String,
    val content: String,
    val channel: ChatChannel,
    val channelName: String,
    val senderName: String,
    val direction: ChatDirection,
    val timestamp: Long,
    val targetName: String = "",
    /** 方案 R-30：本地发送失败气泡（断线时发送），点击重发 */
    val localFailed: Boolean = false,
    /** 本地失败气泡对应的原始命令（重发用，不复制协议消息事实） */
    val localCommand: String = "",
)
