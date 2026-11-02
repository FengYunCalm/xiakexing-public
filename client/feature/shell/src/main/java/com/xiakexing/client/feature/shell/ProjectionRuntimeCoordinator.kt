package com.xiakexing.client.feature.shell

import com.google.gson.JsonObject
import com.xiakexing.client.core.protocol.payload.ProjectionStatePacket

/**
 * 公共作用域运行时协调器。
 * 只消费 REAL 状态包：投影变化时清房间投影，其余更新 projection 运行时状态。
 */
internal class ProjectionRuntimeCoordinator(
    private val sink: Sink,
) {
    interface Sink {
        fun currentState(): ShellUiState

        fun updateState(transform: ShellUiState.() -> ShellUiState)

        fun clearRoomProjection()
    }

    fun handleProjectionState(payload: JsonObject) {
        val nextProjection = ProjectionStatePacket.fromJsonOrNull(payload) ?: return
        val currentRuntime = sink.currentState().projectionRuntime
        val previousProjection = currentRuntime.projection
        val projectionChanged =
            when {
                previousProjection == null -> nextProjection.projection.isV2
                else -> previousProjection.hasDifferentRuntimeScope(nextProjection)
            }
        if (projectionChanged) {
            sink.clearRoomProjection()
        }
        val nextRuntime =
            if (projectionChanged) {
                ProjectionRuntimeState(projection = nextProjection)
            } else {
                currentRuntime.copy(projection = nextProjection)
            }
        commitRuntime(nextRuntime)
    }

    fun reset() {
        sink.updateState { copy(projectionRuntime = ProjectionRuntimeState()) }
    }

    private fun commitRuntime(runtime: ProjectionRuntimeState) {
        sink.updateState {
            copy(projectionRuntime = runtime)
        }
    }
}

/**
 * Full runtime-scope comparison: projection id + projection revision
 * and projection generation. Projection revision itself is monotonic and
 * remains part of the authoritative epoch identity.
 */
private fun ProjectionStatePacket.hasDifferentRuntimeScope(other: ProjectionStatePacket): Boolean {
    if (projection.projectionId != other.projection.projectionId ||
        projection.projectionRevision != other.projection.projectionRevision
    ) {
        return true
    }
    return projectionGeneration != other.projectionGeneration
}
