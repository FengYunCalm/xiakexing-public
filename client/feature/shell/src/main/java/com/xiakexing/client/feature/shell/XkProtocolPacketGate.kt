package com.xiakexing.client.feature.shell

import com.google.gson.JsonObject
import com.xiakexing.client.core.protocol.XkMessageType
import com.xiakexing.client.core.protocol.XkProtocolMeta
import com.xiakexing.client.core.protocol.payload.ProjectionStatePacket

/** gate 拒绝回调：正式包由 ShellPacketDiagnosticsCoordinator 消费（挂账⑤）。 */
internal fun interface XkProtocolGateRejectSink {
    fun onReject(
        reason: String,
        meta: XkProtocolMeta,
    )
}

/**
 * 协议包门 composition facade（三门拆分）：
 * - ProtocolOrderingGate：server_seq 去重 + epoch 回退拒绝；
 * - ProjectionScopeTracker：投影身份跟踪 + REAL 跟随 + room/scoped 投影匹配。
 *
 * Incoming 统一走 [accept] 单入口（旧 shouldAccept 兼容重载已随
 * 收尾删除，测试全量迁移）。
 */
internal class XkProtocolPacketGate(
    private val maxRetainedServerSequences: Int = DEFAULT_RETAINED_SERVER_SEQUENCES,
    private val rejectSink: XkProtocolGateRejectSink = XkProtocolGateRejectSink { _, _ -> },
) {
    private val orderingGate = ProtocolOrderingGate(maxRetainedServerSequences)
    private val scopeTracker = ProjectionScopeTracker()

    private fun reject(
        reason: String,
        meta: XkProtocolMeta,
    ) {
        rejectSink.onReject(reason, meta)
    }

    /** Incoming 唯一入口：type/payload/meta 三元组一次裁决。 */
    fun accept(
        type: XkMessageType?,
        meta: XkProtocolMeta,
        payload: JsonObject? = null,
    ): Boolean {
        if (type == XkMessageType.REAL) {
            return acceptProjection(payload, meta)
        }
        if (type in roomScopedTypes) {
            return acceptRoom(meta)
        }
        if (type in projectionScopedTypes) {
            if (payload == null) {
                reject("scoped_missing_payload", meta)
                return false
            }
            if (!acceptProjectionScoped(payload, meta)) {
                return false
            }
        }
        return acceptOrdering(meta)
    }

    // ===== 内部裁决 =====

    private fun acceptProjection(
        payload: JsonObject?,
        meta: XkProtocolMeta,
    ): Boolean {
        if (payload == null) {
            reject("projection_missing_payload", meta)
            return false
        }
        val packet = ProjectionStatePacket.fromJsonOrNull(payload)
        if (packet == null) {
            reject("projection_identity_required", meta)
            return false
        }
        val projection = packet.projection

        /*
         * Validate the REAL against the current ordering window before changing
         * that window. A stale packet must not reset the high-water marks merely
         * because it claims a different projection identity.
         */
        if (!orderingGate.canAccept(meta)) {
            reject("ordering_rejected", meta)
            return false
        }
        val current = scopeTracker.currentProjection()
        if (!projection.matches(current)) {
            resetOrderingState()
        }
        if (!acceptOrdering(meta)) {
            return false
        }
        scopeTracker.commitProjection(projection)
        return true
    }

    private fun acceptRoom(meta: XkProtocolMeta): Boolean {
        if (meta.isCompleteSnapshot && meta.snapshotTransactionId.isBlank()) {
            reject("snapshot_transaction_required", meta)
            return false
        }
        if (!scopeTracker.shouldAcceptRoom(meta)) {
            reject("room_projection_mismatch", meta)
            return false
        }
        return acceptOrdering(meta)
    }

    private fun acceptProjectionScoped(
        payload: JsonObject,
        meta: XkProtocolMeta,
    ): Boolean {
        if (!scopeTracker.shouldAcceptScopedProjection(payload)) {
            reject("scoped_projection_mismatch", meta)
            return false
        }
        return true
    }

    private fun acceptOrdering(meta: XkProtocolMeta): Boolean {
        if (!orderingGate.shouldAccept(meta)) {
            reject("ordering_rejected", meta)
            return false
        }
        return true
    }

    fun reset() {
        orderingGate.reset()
        scopeTracker.reset()
    }

    private fun resetOrderingState() {
        orderingGate.reset()
    }

    private companion object {
        private const val DEFAULT_RETAINED_SERVER_SEQUENCES = 4_096
        private val projectionScopedTypes =
            setOf(
                XkMessageType.QUES,
                XkMessageType.QUEU,
                XkMessageType.ROUT,
                XkMessageType.MAPS,
            )
        private val roomScopedTypes =
            setOf(
                XkMessageType.TITL,
                XkMessageType.EXIT,
                XkMessageType.NPCS,
                XkMessageType.ITEM,
            )
    }
}
