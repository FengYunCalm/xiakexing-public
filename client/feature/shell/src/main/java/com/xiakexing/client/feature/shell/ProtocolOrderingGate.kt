package com.xiakexing.client.feature.shell

import com.xiakexing.client.core.protocol.XkProtocolMeta
import java.util.ArrayDeque

/**
 * 排序门：server_seq 去重（FIFO 窗口）+ epoch 回退拒绝。
 *
 * 契约（服务端已持久化 epoch/seq 高水位，跨重启单调）：
 * - epoch 回退（< latestEpoch）拒绝：只可能是滞留旧包或服务端计数重建；
 *   优雅重启由服务端关停落盘保证单调，硬崩溃窗口有界自愈（连接重置/
 *   REAL 投影变化处的 reset 是恢复手段）。
 * - server_seq 去重：4096 FIFO 窗口，服务端 seq 高水位避免重启撞窗。
 *   窗口用“插入序 + 出现计数”唯一结构：同一序号因 epoch 推进被重放时只增加
 *   计数，淘汰队首时仅在其计数归零后才真正遗忘，避免重放序号被提前移除。
 */
internal class ProtocolOrderingGate(
    private val maxRetainedServerSequences: Int = DEFAULT_RETAINED_SERVER_SEQUENCES,
) {
    private val serverSeqCounts = mutableMapOf<Long, Int>()
    private val acceptedServerSequenceOccurrences = ArrayDeque<Long>()
    private val latestEpochByStream = mutableMapOf<String, Long>()

    init {
        require(maxRetainedServerSequences > 0) { "maxRetainedServerSequences must be positive" }
    }

    /** Returns whether a packet can enter the current ordering window. */
    fun canAccept(meta: XkProtocolMeta): Boolean {
        val stream = meta.stream.takeIf { it.isNotBlank() }
        val epoch = meta.epoch
        val epochAdvances =
            stream != null && epoch != null &&
                latestEpochByStream[stream]?.let { epoch > it } == true
        if (stream != null && epoch != null) {
            val latestEpoch = latestEpochByStream[stream]
            if (latestEpoch != null && epoch < latestEpoch) return false
        }
        val serverSeq = meta.serverSeq
        return serverSeq == null || !serverSeqCounts.containsKey(serverSeq) || epochAdvances
    }

    /** Accepts and commits a packet after its ordering window was validated. */
    fun shouldAccept(meta: XkProtocolMeta): Boolean {
        if (!canAccept(meta)) return false

        val stream = meta.stream.takeIf { it.isNotBlank() }
        val epoch = meta.epoch
        val serverSeq = meta.serverSeq
        if (serverSeq != null) {
            serverSeqCounts[serverSeq] = (serverSeqCounts[serverSeq] ?: 0) + 1
            acceptedServerSequenceOccurrences.addLast(serverSeq)
            evictBeyondCapacity()
        }
        if (stream != null && epoch != null) {
            val latestEpoch = latestEpochByStream[stream]
            if (latestEpoch == null || epoch > latestEpoch) {
                latestEpochByStream[stream] = epoch
            }
        }
        return true
    }

    private fun evictBeyondCapacity() {
        while (acceptedServerSequenceOccurrences.size > maxRetainedServerSequences) {
            val evicted = acceptedServerSequenceOccurrences.removeFirst()
            val remaining = (serverSeqCounts[evicted] ?: 1) - 1
            if (remaining > 0) {
                serverSeqCounts[evicted] = remaining
            } else {
                serverSeqCounts.remove(evicted)
            }
        }
    }

    fun reset() {
        serverSeqCounts.clear()
        acceptedServerSequenceOccurrences.clear()
        latestEpochByStream.clear()
    }

    private companion object {
        private const val DEFAULT_RETAINED_SERVER_SEQUENCES = 4_096
    }
}
