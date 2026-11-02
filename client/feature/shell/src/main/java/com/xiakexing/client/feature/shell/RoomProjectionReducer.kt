package com.xiakexing.client.feature.shell

import com.google.gson.JsonElement
import com.google.gson.JsonObject
import com.xiakexing.client.core.protocol.IncomingFrame
import com.xiakexing.client.core.protocol.XkEnvelope
import com.xiakexing.client.core.protocol.XkMessageType
import com.xiakexing.client.core.protocol.XkProtocolMeta
import com.xiakexing.client.core.protocol.payload.ExitSnapshot
import com.xiakexing.client.core.protocol.payload.InventoryItemSnapshot
import com.xiakexing.client.core.protocol.payload.InventorySummary
import com.xiakexing.client.core.protocol.payload.NamedEntity
import com.xiakexing.client.core.protocol.payload.PlayerStatusSnapshot
import com.xiakexing.client.core.protocol.payload.RoomTitleSnapshot

/**
 * RoomProjection 纯逻辑层——房间 staging 状态变换、快照一致性吸收、
 * 提交判定与投影身份记录。不持有 sink，不执行任何副作用。
 */
internal const val ROOM_REFRESH_BATCH_MS = 24L

internal val COMPLETE_SNAPSHOT_FIELDS = setOf("title", "exits", "npcs", "items")

internal data class RoomRefreshStageResult(
    val handled: Boolean,
    val immediateStatusSnapshot: PlayerStatusSnapshot? = null,
    val shouldFlush: Boolean = false,
    val shouldScheduleFlush: Boolean = false,
    val legacyCompatibilitySource: String = "",
)

/** 一次权威房间提交后记录的投影身份基线，用于防重放与 stale 判定。 */
internal data class CommittedSnapshotRecord(
    val projectionId: String,
    val projectionRevision: Long?,
    val projectionGeneration: Long?,
    val transactionId: String,
    val roomSnapshotRevision: Long?,
)

internal data class PendingRoomRefresh(
    var roomId: String? = null,
    var roomTitle: String? = null,
    var roomZone: String? = null,
    var instanceKind: String? = null,
    var dungeonType: String? = null,
    var dungeonGate: String? = null,
    var exits: List<ExitSnapshot>? = null,
    var npcs: List<NamedEntity>? = null,
    var items: List<NamedEntity>? = null,
    var inventoryItems: List<InventoryItemSnapshot>? = null,
    var inventorySummary: InventorySummary? = null,
    var statusSnapshot: PlayerStatusSnapshot? = null,
    val receivedWorldPackets: MutableSet<WorldPacket> = linkedSetOf(),
    var snapshotKind: String? = null,
    var snapshotRoomId: String? = null,
    var snapshotProjectionId: String? = null,
    var snapshotProjectionRevision: Long? = null,
    var snapshotProjectionGeneration: Long? = null,
    var snapshotTransactionId: String? = null,
    var roomSnapshotRevision: Long? = null,
    var snapshotInconsistent: Boolean = false,
    val completeFields: MutableSet<String> = linkedSetOf(),
) {
    fun hasNavigationalSnapshot(): Boolean {
        return roomTitle != null && exits != null
    }

    fun isEmpty(): Boolean {
        return roomId == null &&
            roomTitle == null &&
            exits == null &&
            npcs == null &&
            items == null &&
            inventoryItems == null &&
            statusSnapshot == null &&
            receivedWorldPackets.isEmpty()
    }

    fun hasCompleteRoomSnapshot(): Boolean {
        return roomTitle != null &&
            exits != null &&
            npcs != null &&
            items != null
    }

    /** 完整四段收敛：complete/empty_shell 声明了权威实体集合，且四段已到齐。 */
    fun isAuthoritativeEntitySnapshot(): Boolean {
        if (snapshotInconsistent) {
            return false
        }
        if (snapshotKind == "complete" || snapshotKind == "empty_shell") {
            return completeFields.containsAll(COMPLETE_SNAPSHOT_FIELDS) &&
                hasCompleteRoomSnapshot() &&
                snapshotProjectionGeneration != null &&
                snapshotTransactionId?.isNotBlank() == true
        }
        return hasCompleteRoomSnapshot()
    }

    /** 完整房间提交：一次移动/同步事务的权威收敛，可用于触发路线刷新。 */
    fun isAuthoritativeRoomCommit(): Boolean {
        return isAuthoritativeEntitySnapshot()
    }

    fun shouldFlushImmediately(): Boolean {
        return when (snapshotKind) {
            "complete", "empty_shell" -> isAuthoritativeRoomCommit()
            "navigation" -> hasNavigationalSnapshot() && !snapshotInconsistent
            null -> hasCompleteRoomSnapshot()
            else -> false
        }
    }

    fun canUseLegacyTimedFlush(): Boolean {
        return snapshotKind == null && !snapshotInconsistent && !isEmpty()
    }

    fun isFlushable(): Boolean {
        if (snapshotInconsistent) {
            return false
        }
        return when (snapshotKind) {
            "complete", "empty_shell" -> isAuthoritativeRoomCommit()
            "navigation" -> hasNavigationalSnapshot()
            null -> !isEmpty()
            else -> false
        }
    }
}

internal fun PendingRoomRefresh.conflictsWith(meta: XkProtocolMeta): Boolean {
    if (snapshotKind == null || meta.snapshotKind.isBlank()) {
        return false
    }
    if (snapshotKind != meta.snapshotKind) {
        return true
    }
    if (snapshotRoomId != null && meta.snapshotRoomId.isNotBlank() && snapshotRoomId != meta.snapshotRoomId) {
        return true
    }
    if (snapshotProjectionId != null && meta.snapshotProjectionId.isNotBlank() &&
        snapshotProjectionId != meta.snapshotProjectionId
    ) {
        return true
    }
    if (snapshotProjectionRevision != null && meta.snapshotProjectionRevision != null &&
        snapshotProjectionRevision != meta.snapshotProjectionRevision
    ) {
        return true
    }
    if (snapshotProjectionGeneration != null && meta.snapshotProjectionGeneration != null &&
        snapshotProjectionGeneration != meta.snapshotProjectionGeneration
    ) {
        return true
    }
    if (snapshotTransactionId != null && meta.snapshotTransactionId.isNotBlank() &&
        snapshotTransactionId != meta.snapshotTransactionId
    ) {
        return true
    }
    return roomSnapshotRevision != null && meta.roomSnapshotRevision != null &&
        roomSnapshotRevision != meta.roomSnapshotRevision
}

internal fun StagedRoomPacket.hasPayloadRoomMismatch(): Boolean {
    if (this !is StagedRoomPacket.Title) {
        return false
    }
    return roomId.isNotBlank() &&
        meta.snapshotRoomId.isNotBlank() &&
        roomId != meta.snapshotRoomId
}

internal sealed interface StagedRoomPacket {
    val meta: XkProtocolMeta

    /** 协议类型（gate.accept 单入口按类型分派）。 */
    fun messageType(): XkMessageType =
        when (this) {
            is Title -> XkMessageType.TITL
            is Exits -> XkMessageType.EXIT
            is Npcs -> XkMessageType.NPCS
            is Items -> XkMessageType.ITEM
            is Inventory -> XkMessageType.IVTY
            is Status -> XkMessageType.STAT
        }

    data class Title(
        val roomId: String = "",
        val title: String,
        val zone: String,
        val instanceKind: String = "",
        val dungeonType: String = "",
        val dungeonGate: String = "",
        override val meta: XkProtocolMeta = XkProtocolMeta(),
    ) : StagedRoomPacket

    data class Exits(
        val exits: List<ExitSnapshot>,
        override val meta: XkProtocolMeta = XkProtocolMeta(),
    ) : StagedRoomPacket

    data class Npcs(
        val npcs: List<NamedEntity>,
        override val meta: XkProtocolMeta = XkProtocolMeta(),
    ) : StagedRoomPacket

    data class Items(
        val items: List<NamedEntity>,
        override val meta: XkProtocolMeta = XkProtocolMeta(),
    ) : StagedRoomPacket

    data class Inventory(
        val inventoryItems: List<InventoryItemSnapshot>,
        val inventorySummary: InventorySummary? = null,
        override val meta: XkProtocolMeta = XkProtocolMeta(),
    ) : StagedRoomPacket

    data class Status(
        val statusSnapshot: PlayerStatusSnapshot,
        override val meta: XkProtocolMeta = XkProtocolMeta(),
    ) : StagedRoomPacket
}

internal fun IncomingFrame.Packet.toStagedRoomPacketOrNull(): StagedRoomPacket? {
    return when (envelope.type) {
        XkMessageType.TITL ->
            RoomTitleSnapshot.fromJson(envelope.payload).let { snapshot ->
                StagedRoomPacket.Title(
                    roomId = snapshot.roomId,
                    title = snapshot.title,
                    zone = snapshot.zone,
                    instanceKind = snapshot.instanceKind,
                    dungeonType = snapshot.dungeonType,
                    dungeonGate = snapshot.dungeonGate,
                    meta = envelope.meta,
                )
            }

        XkMessageType.EXIT ->
            StagedRoomPacket.Exits(
                ExitSnapshot.listFrom(envelope.payload),
                envelope.meta,
            )
        XkMessageType.NPCS ->
            StagedRoomPacket.Npcs(
                NamedEntity.listFrom(envelope.payload, "npcs"),
                envelope.meta,
            )
        XkMessageType.ITEM ->
            StagedRoomPacket.Items(
                NamedEntity.listFrom(envelope.payload, "items"),
                envelope.meta,
            )
        XkMessageType.IVTY ->
            StagedRoomPacket.Inventory(
                InventoryItemSnapshot.listFrom(envelope.payload).stackInventoryItemsForDisplay(),
                InventorySummary.fromJson(envelope.payload),
                envelope.meta,
            )

        XkMessageType.STAT ->
            StagedRoomPacket.Status(
                PlayerStatusSnapshot.fromJson(envelope.payload),
                envelope.meta,
            )
        else -> null
    }
}

internal fun JsonElement.toBatchStagedRoomPacketOrNull(): StagedRoomPacket? {
    if (!isJsonObject) {
        return null
    }

    val envelope = asJsonObject
    val rawType = envelope.batchTypeCode()
    val type = XkMessageType.fromCode(rawType)
    if (type !in roomPacketTypes) {
        return null
    }

    return runCatching {
        IncomingFrame.Packet(
            XkEnvelope(
                rawType = rawType,
                type = type,
                payload = envelope.batchPayload(),
                meta = envelope.batchChildMeta(envelope.batchPayload()),
            ),
        ).toStagedRoomPacketOrNull()
    }.getOrNull()
}

internal fun JsonObject.batchTypeCode(): String {
    return listOf("type", "code", "raw_type", "message_type")
        .firstNotNullOfOrNull { key ->
            string(key).trim().uppercase().takeIf { value -> value.length == 4 }
        }
        .orEmpty()
}

internal fun JsonObject.batchPayload(): JsonObject {
    val payload = get("payload")?.takeIf { it.isJsonObject }?.asJsonObject
    if (payload != null) {
        return payload
    }

    val data = get("data")?.takeIf { it.isJsonObject }?.asJsonObject
    if (data != null) {
        return data
    }

    val fallback = JsonObject()
    entrySet().forEach { (key, value) ->
        if (key !in setOf("type", "code", "raw_type", "message_type")) {
            fallback.add(key, value)
        }
    }
    return fallback
}

/**
 * Merge the transport-level meta of a batch child envelope (top-level `meta` key)
 * with any meta nested inside the child payload. Transport meta wins on conflicts;
 * payload meta only fills fields the transport meta does not carry.
 */
internal fun JsonObject.batchChildMeta(payload: JsonObject): XkProtocolMeta {
    val transportMeta = get("meta")?.takeIf { it.isJsonObject }?.asJsonObject
    val payloadMeta = payload.get("meta")?.takeIf { it.isJsonObject }?.asJsonObject
    if (transportMeta == null && payloadMeta == null) {
        return XkProtocolMeta()
    }
    val merged = JsonObject()
    payloadMeta?.entrySet()?.forEach { (key, value) -> merged.add(key, value) }
    transportMeta?.entrySet()?.forEach { (key, value) -> merged.add(key, value) }
    return XkProtocolMeta.fromPayload(JsonObject().apply { add("meta", merged) })
}

private val roomPacketTypes =
    setOf(
        XkMessageType.TITL,
        XkMessageType.EXIT,
        XkMessageType.NPCS,
        XkMessageType.ITEM,
        XkMessageType.IVTY,
        XkMessageType.STAT,
    )

private fun JsonObject.string(key: String): String {
    return if (has(key) && !get(key).isJsonNull) get(key).asString else ""
}

/**
 * 房间 staging ledger 状态变换：把单个子包吸收进 pending 快照。
 * batchDepth 由调用方传入（Coordinator 持有），本函数不读写外部状态。
 */
internal fun stageInto(
    pending: PendingRoomRefresh,
    packet: StagedRoomPacket,
    canApplyStatusImmediately: Boolean,
    batchDepth: Int,
): RoomRefreshStageResult {
    // 完整批次已开始后到达的 navigation 残余包直接忽略，不污染权威批次。
    if (pending.snapshotKind == "complete" && packet.meta.isNavigationSnapshot) {
        return RoomRefreshStageResult(handled = true)
    }
    when (packet) {
        is StagedRoomPacket.Title -> {
            pending.receivedWorldPackets += WorldPacket.TITLE
            pending.roomId = packet.roomId.takeIf { it.isNotBlank() }
            pending.roomTitle = packet.title
            pending.roomZone = packet.zone
            // instance_kind/dungeon_gate 是权威负值（空串也有效）：
            // 离开副本后服务端发空串，必须覆盖旧值，不能 takeIf 丢弃。
            pending.instanceKind = packet.instanceKind
            pending.dungeonType = packet.dungeonType
            pending.dungeonGate = packet.dungeonGate
        }

        is StagedRoomPacket.Exits -> {
            pending.receivedWorldPackets += WorldPacket.EXIT
            pending.exits = packet.exits
        }

        is StagedRoomPacket.Npcs -> {
            pending.receivedWorldPackets += WorldPacket.NPCS
            pending.npcs = packet.npcs
        }

        is StagedRoomPacket.Items -> {
            pending.receivedWorldPackets += WorldPacket.ITEMS
            pending.items = packet.items
        }

        is StagedRoomPacket.Inventory -> {
            pending.inventoryItems = packet.inventoryItems
            pending.inventorySummary = packet.inventorySummary
        }

        is StagedRoomPacket.Status -> {
            if (canApplyStatusImmediately && pending.isEmpty() && batchDepth == 0) {
                return RoomRefreshStageResult(
                    handled = true,
                    immediateStatusSnapshot = packet.statusSnapshot,
                )
            }
            pending.receivedWorldPackets += WorldPacket.STAT
            pending.statusSnapshot = packet.statusSnapshot
        }
    }
    absorbSnapshotMeta(pending, packet.meta)

    val shouldFlush = batchDepth == 0 && pending.shouldFlushImmediately()
    val shouldScheduleFlush = batchDepth == 0 && !shouldFlush && pending.canUseLegacyTimedFlush()
    return RoomRefreshStageResult(
        handled = true,
        shouldFlush = shouldFlush,
        shouldScheduleFlush = shouldScheduleFlush,
        legacyCompatibilitySource =
            if (shouldScheduleFlush) {
                "room.timed_flush"
            } else {
                ""
            },
    )
}

/*
 * 房间快照 envelope 一致性吸收：同一批内所有子包的 snapshot_kind / room_id /
 * 投影身份必须一致；不一致（跨房间/跨投影混合包）整批标记为不可提交，
 * 由 applyToShellState 拒绝部分状态落地。
 */
internal fun absorbSnapshotMeta(
    pending: PendingRoomRefresh,
    meta: XkProtocolMeta,
) {
    if (meta.snapshotKind.isBlank()) {
        return
    }
    val existingKind = pending.snapshotKind
    if (existingKind != null && existingKind != meta.snapshotKind) {
        pending.snapshotInconsistent = true
        return
    }
    val existingRoom = pending.snapshotRoomId
    if (existingRoom != null && meta.snapshotRoomId.isNotBlank() && existingRoom != meta.snapshotRoomId) {
        pending.snapshotInconsistent = true
        return
    }
    val existingProjection = pending.snapshotProjectionId
    if (existingProjection != null && meta.snapshotProjectionId.isNotBlank() &&
        existingProjection != meta.snapshotProjectionId
    ) {
        pending.snapshotInconsistent = true
        return
    }
    val existingProjectionRevision = pending.snapshotProjectionRevision
    if (existingProjectionRevision != null && meta.snapshotProjectionRevision != null &&
        existingProjectionRevision != meta.snapshotProjectionRevision
    ) {
        pending.snapshotInconsistent = true
        return
    }
    val existingProjectionGeneration = pending.snapshotProjectionGeneration
    if (existingProjectionGeneration != null && meta.snapshotProjectionGeneration != null &&
        existingProjectionGeneration != meta.snapshotProjectionGeneration
    ) {
        pending.snapshotInconsistent = true
        return
    }
    val existingTransactionId = pending.snapshotTransactionId
    if (existingTransactionId != null && meta.snapshotTransactionId.isNotBlank() &&
        existingTransactionId != meta.snapshotTransactionId
    ) {
        pending.snapshotInconsistent = true
        return
    }
    val existingRoomRevision = pending.roomSnapshotRevision
    if (existingRoomRevision != null && meta.roomSnapshotRevision != null &&
        existingRoomRevision != meta.roomSnapshotRevision
    ) {
        pending.snapshotInconsistent = true
        return
    }
    pending.snapshotKind = meta.snapshotKind
    pending.snapshotRoomId = meta.snapshotRoomId
    pending.snapshotProjectionId = meta.snapshotProjectionId
    meta.snapshotProjectionRevision?.let { pending.snapshotProjectionRevision = it }
    meta.snapshotProjectionGeneration?.let { pending.snapshotProjectionGeneration = it }
    if (meta.snapshotTransactionId.isNotBlank()) {
        pending.snapshotTransactionId = meta.snapshotTransactionId
    }
    meta.roomSnapshotRevision?.let { pending.roomSnapshotRevision = it }
    if (meta.completeFields.isNotEmpty()) {
        pending.completeFields += meta.completeFields
    }
}

/**
 * 服务端 revision 已持久化（玩家对象重建后从存档恢复继续递增，正常
 * 路径单调）；同投影内同 revision 重复提交判 stale（防重放）。
 * revision 回退（< committed）视为服务端计数重建（旧存档/崩溃回滚），
 * 接受并重置 committed 基线，避免“后端移动、前端永不刷新”。
 */
internal fun isStaleCommittedSnapshot(
    meta: XkProtocolMeta,
    committedProjectionId: String,
    committedProjectionRevision: Long?,
    committedProjectionGeneration: Long?,
    committedTransactionId: String,
    committedRoomSnapshotRevision: Long?,
): Boolean {
    val revision = meta.roomSnapshotRevision ?: return false
    val committedRevision = committedRoomSnapshotRevision ?: return false
    if (!meta.isCompleteSnapshot) {
        return false
    }
    return meta.snapshotProjectionId == committedProjectionId &&
        meta.snapshotProjectionRevision == committedProjectionRevision &&
        meta.snapshotProjectionGeneration == committedProjectionGeneration &&
        meta.snapshotTransactionId == committedTransactionId &&
        revision == committedRevision
}

/** 权威房间提交后记录投影身份基线；非权威提交不记录。 */
internal fun recordCommittedSnapshot(pending: PendingRoomRefresh): CommittedSnapshotRecord? {
    val revision = pending.roomSnapshotRevision ?: return null
    if (!pending.isAuthoritativeRoomCommit()) {
        return null
    }
    return CommittedSnapshotRecord(
        projectionId = pending.snapshotProjectionId.orEmpty(),
        projectionRevision = pending.snapshotProjectionRevision,
        projectionGeneration = pending.snapshotProjectionGeneration,
        transactionId = pending.snapshotTransactionId.orEmpty(),
        roomSnapshotRevision = revision,
    )
}
