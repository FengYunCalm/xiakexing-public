package com.xiakexing.client.core.protocol.payload

import com.google.gson.JsonObject

data class ProjectionIdentity(
    val protocolVersion: String,
    val projectionId: String,
    val projectionRevision: Long,
    val projectionGeneration: Long,
) {
    val isV2: Boolean
        get() = projectionId.isNotBlank() && protocolVersion.isAtLeastProtocolVersion(2, 3)

    fun matches(other: ProjectionIdentity?): Boolean {
        return other != null &&
            protocolVersion == other.protocolVersion &&
            projectionId == other.projectionId &&
            projectionRevision == other.projectionRevision &&
            projectionGeneration == other.projectionGeneration
    }

    companion object {
        fun fromJsonOrNull(payload: JsonObject): ProjectionIdentity? {
            val projectionId = payload.payloadStrictString("projection_id") ?: return null
            val protocolVersion = payload.payloadStrictString("protocol_version") ?: return null
            val revision = payload.payloadStrictLong("projection_revision") ?: return null
            val generation = payload.payloadStrictLong("projection_generation") ?: return null
            if (projectionId.isBlank() ||
                !protocolVersion.isAtLeastProtocolVersion(2, 3) ||
                revision < 0L ||
                generation <= 0L
            ) {
                return null
            }
            return ProjectionIdentity(
                protocolVersion = protocolVersion,
                projectionId = projectionId,
                projectionRevision = revision,
                projectionGeneration = generation,
            )
        }
    }
}

data class ObjectiveIdentityPacket(
    val sourceKind: String,
    val objectiveId: String,
    val acceptanceId: String,
    val acceptanceRevision: Long,
)

private fun String.isAtLeastProtocolVersion(
    requiredMajor: Int,
    requiredMinor: Int,
): Boolean {
    val parts = trim().split('.', limit = 3)
    val major = parts.getOrNull(0)?.toIntOrNull() ?: return false
    val minor = parts.getOrNull(1)?.toIntOrNull() ?: 0
    return major > requiredMajor || major == requiredMajor && minor >= requiredMinor
}
