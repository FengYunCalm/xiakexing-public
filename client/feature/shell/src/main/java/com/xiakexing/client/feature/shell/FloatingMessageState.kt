package com.xiakexing.client.feature.shell

import androidx.compose.runtime.Immutable

internal const val FLOATING_MESSAGE_KIND = "float_message"

/** 服务端 quest_hint 飘窗的 collapse_key 前缀（跨端协议契约，
 * 服务端 lib/base/npc.c do_quest 拼接处有镜像注释）。 */
internal const val QUEST_HINT_COLLAPSE_PREFIX = "float/quest_hint/"

@Immutable
data class FloatingMessageState(
    /** id 的唯一真源是追加变换 buildFloatingEventAppendTransform；
     * 直接构造时无需关心（默认 0 会被变换覆盖为列表内唯一值）。 */
    val id: Long = 0L,
    val text: String,
    val level: String = "info",
    val ttlMs: Long = DEFAULT_FLOATING_MESSAGE_TTL_MS,
    val dedupeKey: String = "",
    /** 默认=构造时刻（Kotlin 默认参数每次构造求值）；copy() 自动保留原值。 */
    val createdAt: Long = System.currentTimeMillis(),
)

internal const val DEFAULT_FLOATING_MESSAGE_TTL_MS = 2_000L
internal const val MIN_FLOATING_MESSAGE_TTL_MS = 1_200L
internal const val MAX_FLOATING_MESSAGE_TTL_MS = 3_500L

internal fun Long.coerceFloatingMessageTtl(): Long {
    return takeIf { it > 0L }
        ?.coerceIn(MIN_FLOATING_MESSAGE_TTL_MS, MAX_FLOATING_MESSAGE_TTL_MS)
        ?: DEFAULT_FLOATING_MESSAGE_TTL_MS
}

/** 第五轮 R7：TTL 过期权威收归状态层——入列与读取统一按 now-created>ttl 驱逐，
 * 宿主离开组合再回来不再复活死消息（此前过期只活在 Composable 可见性定时器里）。 */
internal fun List<FloatingMessageState>.withoutExpired(
    nowMs: Long = System.currentTimeMillis(),
): List<FloatingMessageState> {
    return filter { item -> nowMs - item.createdAt <= item.ttlMs }
}

/**
 * 浮层消息 id 单一真源：基于当前列表最小值向小扩展，保证同一列表内不重复。
 * 收敛历史四套方案（正自增序列 / 独立负值序列 / 时间戳），避免同一列表混用。
 */
internal fun ShellUiState.nextFloatingMessageId(): Long {
    return (floatingMessages.minOfOrNull { item -> item.id } ?: 0L) - 1L
}

internal fun List<FloatingMessageState>.withFloatingMessage(
    message: FloatingMessageState,
    nowMs: Long = System.currentTimeMillis(),
): List<FloatingMessageState> {
    val survivors = withoutExpired(nowMs)
    val withoutDuplicate =
        if (message.dedupeKey.isNotBlank()) {
            survivors.filterNot { it.dedupeKey == message.dedupeKey }
        } else {
            survivors
        }
    // 兜底打戳：createdAt 未填（历史生产点/测试构造）视为"现在入列"，避免被判过期。
    val stamped = if (message.createdAt <= 0L) message.copy(createdAt = nowMs) else message
    return (withoutDuplicate + stamped).takeLast(3)
}

internal fun ShellUiState.withNpcQuestEmptyFeedback(targetKey: String): ShellUiState {
    return withNpcEmptyFeedback("npc-quest-empty:$targetKey", NPC_QUEST_EMPTY_MESSAGE, "quest")
}

internal fun ShellUiState.withNpcLearningEmptyFeedback(targetKey: String): ShellUiState {
    return withNpcEmptyFeedback("npc-learn-empty:$targetKey", NPC_LEARNING_EMPTY_MESSAGE, "learn")
}

private fun ShellUiState.withNpcEmptyFeedback(
    dedupeKey: String,
    message: String,
    level: String,
): ShellUiState {
    val transform =
        buildFloatingEventAppendTransform(
            FloatingMessageState(
                text = message,
                level = level,
                dedupeKey = dedupeKey,
            ),
        )
    return transform(this)
}
