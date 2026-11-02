package com.xiakexing.client.core.protocol

import com.google.gson.JsonObject

data class XkProtocolMeta(
    val serverSeq: Long? = null,
    val stream: String = "",
    val epoch: Long? = null,
    val reliability: String = "",
    val priority: String = "",
    val collapseKey: String = "",
    val ttlMs: Long? = null,
    val snapshotKind: String = "",
    val snapshotRoomId: String = "",
    val snapshotProjectionId: String = "",
    val snapshotProjectionRevision: Long? = null,
    val snapshotProjectionGeneration: Long? = null,
    val snapshotTransactionId: String = "",
    val roomSnapshotRevision: Long? = null,
    val commandResponseId: String = "",
    val completeFields: List<String> = emptyList(),
) {
    val isEmpty: Boolean
        get() =
            serverSeq == null && stream.isBlank() && epoch == null && reliability.isBlank() &&
                priority.isBlank() && collapseKey.isBlank() && ttlMs == null &&
                snapshotKind.isBlank() && snapshotRoomId.isBlank() &&
                snapshotProjectionId.isBlank() && snapshotProjectionRevision == null &&
                snapshotProjectionGeneration == null && snapshotTransactionId.isBlank() &&
                roomSnapshotRevision == null && commandResponseId.isBlank() && completeFields.isEmpty()

    /** 完整换房收敛快照：实体集合（可能为空）是权威状态。 */
    val isCompleteSnapshot: Boolean
        get() = snapshotKind == "complete" || snapshotKind == "empty_shell"

    /** 仅移动路线/标题的导航快照：不得清空客户端当前实体。 */
    val isNavigationSnapshot: Boolean
        get() = snapshotKind == "navigation"

    companion object {
        fun fromPayload(payload: JsonObject): XkProtocolMeta {
            val meta = payload.get("meta")?.takeIf { it.isJsonObject }?.asJsonObject ?: return XkProtocolMeta()
            return XkProtocolMeta(
                serverSeq = meta.longOrNull("server_seq"),
                stream = meta.string("stream"),
                epoch = meta.longOrNull("epoch"),
                reliability = meta.string("reliability"),
                priority = meta.string("priority"),
                collapseKey = meta.string("collapse_key"),
                ttlMs = meta.longOrNull("ttl_ms"),
                snapshotKind = meta.string("snapshot_kind"),
                snapshotRoomId = meta.string("room_id"),
                snapshotProjectionId = meta.string("projection_id"),
                snapshotProjectionRevision = meta.longOrNull("projection_revision"),
                snapshotProjectionGeneration = meta.longOrNull("projection_generation"),
                snapshotTransactionId = meta.string("transaction_id"),
                roomSnapshotRevision = meta.longOrNull("room_snapshot_revision"),
                commandResponseId = meta.strictString("command_request_id"),
                completeFields = meta.stringArray("complete_fields"),
            )
        }
    }
}

private fun JsonObject.stringArray(key: String): List<String> {
    val element = get(key) ?: return emptyList()
    if (!element.isJsonArray) {
        return emptyList()
    }
    return element.asJsonArray.mapNotNull { item ->
        item.takeUnless { it.isJsonNull }?.takeIf { it.isJsonPrimitive }?.asString
    }
}

private fun JsonObject.strictString(key: String): String {
    return get(key)
        ?.takeUnless { it.isJsonNull }
        ?.takeIf { it.isJsonPrimitive && it.asJsonPrimitive.isString }
        ?.asString
        .orEmpty()
}

private fun JsonObject.string(key: String): String {
    return get(key)
        ?.takeUnless { it.isJsonNull }
        ?.takeIf { it.isJsonPrimitive }
        ?.asString
        .orEmpty()
}

private fun JsonObject.longOrNull(key: String): Long? {
    return get(key)
        ?.takeUnless { it.isJsonNull }
        ?.takeIf { it.isJsonPrimitive }
        ?.asJsonPrimitive
        ?.takeIf { it.isNumber }
        ?.asLong
}
