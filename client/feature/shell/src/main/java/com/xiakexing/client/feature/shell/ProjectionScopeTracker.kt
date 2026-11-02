package com.xiakexing.client.feature.shell

import com.google.gson.JsonObject
import com.xiakexing.client.core.protocol.XkProtocolMeta
import com.xiakexing.client.core.protocol.payload.ProjectionIdentity

/**
 * 投影作用域跟踪：当前投影身份 + v2 建立状态 + REAL 跟随。
 *
 * - REAL 是服务端权威同步点：无论投影如何变化（推进/回退/身份重建），
 *   客户端必须跟随服务端当前投影，否则 currentProjection 冻结在旧值，
 *   后续所有房间包都被 room_projection_mismatch 拒绝——表现为“后端移动、
 *   前端永不刷新”。因此不再终局拒绝：revision 回退（崩溃回滚/旧存档恢复）
 *   与同 revision 换 id（身份重建）都跟随；投影变化时返回 true 通知调用方
 *   重置 ordering 状态，拒绝滞留旧包的职责由 ordering 承担。
 * - room 包（TITL/EXIT/NPCS/ITEM）要求投影 meta 与当前投影精确匹配。
 */
internal class ProjectionScopeTracker {
    private var currentProjection: ProjectionIdentity? = null
    private var hasSeenV2Projection: Boolean = false

    fun currentProjection(): ProjectionIdentity? = currentProjection

    fun hasSeenV2(): Boolean = hasSeenV2Projection

    /** REAL 跟随落账：v1 回退时清空 v2 作用域，后续 v2 REAL 可重新建立投影。 */
    fun commitProjection(projection: ProjectionIdentity) {
        currentProjection = if (projection.isV2) projection else null
        hasSeenV2Projection = projection.isV2
    }

    /** room 包接受（不含 ordering；投影 meta 缺失/不匹配拒绝）。 */
    fun shouldAcceptRoom(meta: XkProtocolMeta): Boolean {
        val current = currentProjection
        val packetHasProjection =
            meta.snapshotProjectionId.isNotBlank() ||
                meta.snapshotProjectionRevision != null ||
                meta.snapshotProjectionGeneration != null
        if (packetHasProjection) {
            if (!hasSeenV2Projection || current?.isV2 != true) {
                return false
            }
            if (meta.snapshotProjectionId != current.projectionId ||
                meta.snapshotProjectionRevision != current.projectionRevision ||
                meta.snapshotProjectionGeneration != current.projectionGeneration
            ) {
                return false
            }
        } else if (hasSeenV2Projection) {
            return false
        }
        return true
    }

    /** 投影作用域包（QUES/QUEU/ROUT/MAPS）的投影匹配检查。 */
    fun shouldAcceptScopedProjection(payload: JsonObject): Boolean {
        val hasProjectionFields =
            payload.has("projection_id") ||
                payload.has("projection_revision") ||
                payload.has("protocol_version")
        if (!hasProjectionFields) {
            return !hasSeenV2Projection
        }
        val packetProjection = ProjectionIdentity.fromJsonOrNull(payload) ?: return false
        return hasSeenV2Projection && packetProjection.matches(currentProjection)
    }

    fun reset() {
        currentProjection = null
        hasSeenV2Projection = false
    }
}
