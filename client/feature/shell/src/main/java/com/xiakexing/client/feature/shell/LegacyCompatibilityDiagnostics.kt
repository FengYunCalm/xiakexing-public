package com.xiakexing.client.feature.shell

import android.util.Log
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.atomic.AtomicLong

/** 记录仍在消费的协议兼容路径，便于在退役前确认真实命中量和来源。 */
internal object LegacyCompatibilityDiagnostics {
    private val hitCounts = ConcurrentHashMap<String, AtomicLong>()

    fun record(source: String) {
        val normalizedSource = source.trim().ifBlank { "unknown" }
        val hits = hitCounts.computeIfAbsent(normalizedSource) { AtomicLong() }.incrementAndGet()
        if (hits == 1L || hits % 100L == 0L) {
            runCatching { Log.w(TAG, "legacy_compat source=$normalizedSource hits=$hits") }
        }
    }
}

private const val TAG = "XkxLegacyCompatibility"
