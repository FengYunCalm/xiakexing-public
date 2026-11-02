package com.xiakexing.client.core.protocol.payload

import com.google.gson.JsonObject

/**
 * 技术性投影状态包（XK 2.3 REAL 帧）。
 * 只承载连接期投影身份与同步代次，不承载世界或产品身份。
 */
data class ProjectionStatePacket(
    val protocolVersion: String,
    val projection: ProjectionIdentity,
    val projectionGeneration: Long = projection.projectionGeneration,
) {
    companion object {
        fun fromJson(payload: JsonObject): ProjectionStatePacket =
            fromJsonOrNull(payload)
                ?: throw IllegalArgumentException("REAL is not a valid projection identity")

        fun fromJsonOrNull(payload: JsonObject): ProjectionStatePacket? {
            val projection = ProjectionIdentity.fromJsonOrNull(payload) ?: return null
            return ProjectionStatePacket(
                protocolVersion = projection.protocolVersion,
                projection = projection,
                projectionGeneration = projection.projectionGeneration,
            )
        }
    }
}
