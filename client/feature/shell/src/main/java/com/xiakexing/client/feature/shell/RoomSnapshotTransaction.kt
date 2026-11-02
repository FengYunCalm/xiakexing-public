package com.xiakexing.client.feature.shell

import com.xiakexing.client.core.protocol.XkProtocolMeta

/**
 * 房间权威快照的跨包事务身份。
 *
 * 四个实体包必须属于同一个 `(projection, room, transaction)`；传输层的
 * server_seq/epoch 只负责顺序，不足以证明四段包来自同一份房间状态。
 */
internal data class RoomSnapshotTransaction(
    val projectionId: String,
    val projectionRevision: Long?,
    val projectionGeneration: Long?,
    val roomId: String,
    val transactionId: String,
    val roomSnapshotRevision: Long?,
) {
    fun matches(meta: XkProtocolMeta): Boolean {
        return projectionId == meta.snapshotProjectionId &&
            projectionRevision == meta.snapshotProjectionRevision &&
            projectionGeneration == meta.snapshotProjectionGeneration &&
            roomId == meta.snapshotRoomId &&
            transactionId == meta.snapshotTransactionId &&
            roomSnapshotRevision == meta.roomSnapshotRevision
    }

    companion object {
        fun fromMeta(meta: XkProtocolMeta): RoomSnapshotTransaction? {
            if (!meta.isCompleteSnapshot ||
                meta.snapshotProjectionId.isBlank() ||
                meta.snapshotProjectionRevision == null ||
                meta.snapshotProjectionGeneration == null ||
                meta.snapshotRoomId.isBlank() ||
                meta.snapshotTransactionId.isBlank() ||
                meta.roomSnapshotRevision == null
            ) {
                return null
            }
            return RoomSnapshotTransaction(
                projectionId = meta.snapshotProjectionId,
                projectionRevision = meta.snapshotProjectionRevision,
                projectionGeneration = meta.snapshotProjectionGeneration,
                roomId = meta.snapshotRoomId,
                transactionId = meta.snapshotTransactionId,
                roomSnapshotRevision = meta.roomSnapshotRevision,
            )
        }
    }
}
