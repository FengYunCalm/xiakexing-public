package com.xiakexing.client.feature.shell

import com.xiakexing.client.core.protocol.IncomingFrame
import com.xiakexing.client.core.protocol.payload.MapGridPacket
import com.xiakexing.client.core.protocol.payload.QuickActionGroupPacket
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

/**
 * RoomProjection 域 owner——房间 staging、快照提交、地图/快捷动作包
 * 与移动自动滚动编排。纯状态变换在 RoomProjectionReducer.kt。
 */
internal class RoomProjectionCoordinator(
    private val scope: CoroutineScope,
    internal val sink: Sink,
) {
    interface Sink {
        fun currentState(): ShellUiState

        fun pendingInteractionRequest(): PendingInteractionRequest?

        fun clearPendingInteractionRequest()

        fun clearMessageFeeds()

        fun consumeLoginReadyBanner(enteringMainShell: Boolean)

        fun markAuthoritativeRoomSnapshotCommitted(connectionGeneration: Long? = null) = Unit

        fun refreshTrackedQuestRoute()

        fun onMapGridAccepted(map: MapGridPacket)

        fun updateState(transform: ShellUiState.() -> ShellUiState)
    }

    private var pendingRoomRefresh: PendingRoomRefresh? = null
    private var batchDepth: Int = 0
    private var batchHasSnapshotPacket: Boolean = false
    private var batchConnectionGeneration: Long? = null
    private var pendingCommitConnectionGeneration: Long? = null
    private var committedProjectionId: String = ""
    private var committedProjectionRevision: Long? = null
    private var committedProjectionGeneration: Long? = null
    private var committedTransactionId: String = ""
    private var committedRoomSnapshotRevision: Long? = null
    private var pendingSnapshotTransaction: RoomSnapshotTransaction? = null
    private var roomRefreshJob: Job? = null
    internal var primaryQuickActionGroup: QuickActionGroupPacket? = null
    internal var secondaryQuickActionGroup: QuickActionGroupPacket? = null
    internal var dynamicButtonGroup: QuickActionGroupPacket? = null
    private var pendingMoveAutoScroll = false
    private var moveAutoScrollJob: Job? = null

    fun stage(
        frame: IncomingFrame.Packet,
        canApplyStatusImmediately: Boolean,
    ): RoomRefreshStageResult {
        val stagedPacket = frame.toStagedRoomPacketOrNull() ?: return RoomRefreshStageResult(handled = false)
        return stage(stagedPacket, canApplyStatusImmediately)
    }

    fun stage(
        packet: StagedRoomPacket,
        canApplyStatusImmediately: Boolean,
    ): RoomRefreshStageResult {
        if (isStaleCommittedSnapshot(
                packet.meta,
                committedProjectionId,
                committedProjectionRevision,
                committedProjectionGeneration,
                committedTransactionId,
                committedRoomSnapshotRevision,
            )
        ) {
            return RoomRefreshStageResult(handled = true)
        }
        var pending = pendingRoomRefresh ?: PendingRoomRefresh()
        if (pending.snapshotKind in setOf("complete", "empty_shell") && packet.meta.isNavigationSnapshot) {
            return RoomRefreshStageResult(handled = true)
        }
        if (packet.hasPayloadRoomMismatch()) {
            absorbSnapshotMeta(pending, packet.meta)
            pending.snapshotInconsistent = true
            pendingRoomRefresh = pending
            if (batchDepth > 0 && packet.meta.snapshotKind.isNotBlank()) {
                batchHasSnapshotPacket = true
            }
            return RoomRefreshStageResult(handled = true)
        }
        /*
         * 房间 staging ledger：新 transport batch 的第一份权威快照可以替换上批遗留，
         * 同一 batch 内的第二个冲突 key 则整批拒绝。complete/empty_shell 总能替换
         * 先到达的 navigation 残余，但绝不复用其标题或出口。
         */
        if (packet.meta.isCompleteSnapshot) {
            val transaction = RoomSnapshotTransaction.fromMeta(packet.meta)
            if (transaction == null) {
                pending.snapshotInconsistent = true
                pendingRoomRefresh = pending
                return RoomRefreshStageResult(handled = true)
            }
            if (pendingSnapshotTransaction != null &&
                !pendingSnapshotTransaction!!.matches(packet.meta) &&
                pending.snapshotKind == packet.meta.snapshotKind &&
                pending.snapshotRoomId == packet.meta.snapshotRoomId &&
                pending.snapshotProjectionId == packet.meta.snapshotProjectionId &&
                pending.snapshotProjectionRevision == packet.meta.snapshotProjectionRevision &&
                pending.snapshotProjectionGeneration == packet.meta.snapshotProjectionGeneration &&
                pending.roomSnapshotRevision == packet.meta.roomSnapshotRevision
            ) {
                pending.snapshotInconsistent = true
                pendingRoomRefresh = pending
                return RoomRefreshStageResult(handled = true)
            }
            pendingSnapshotTransaction = pendingSnapshotTransaction ?: transaction
        }
        if (pending.conflictsWith(packet.meta)) {
            val authoritativeReplacesNavigation = packet.meta.isCompleteSnapshot && pending.snapshotKind == "navigation"
            if (authoritativeReplacesNavigation || batchDepth == 0 || !batchHasSnapshotPacket) {
                pending = PendingRoomRefresh()
                pendingSnapshotTransaction =
                    packet.meta.takeIf { it.isCompleteSnapshot }?.let { RoomSnapshotTransaction.fromMeta(it) }
            } else {
                pending.snapshotInconsistent = true
                pendingRoomRefresh = pending
                return RoomRefreshStageResult(handled = true)
            }
        }
        val result = stageInto(pending, packet, canApplyStatusImmediately, batchDepth)
        pendingRoomRefresh = pending
        if (batchDepth > 0 && packet.meta.snapshotKind.isNotBlank()) {
            batchHasSnapshotPacket = true
        }
        return result
    }

    fun flush(): PendingRoomRefresh? {
        val pending = pendingRoomRefresh ?: return null
        if (pending.snapshotInconsistent) {
            pendingRoomRefresh = null
            pendingSnapshotTransaction = null
            pendingCommitConnectionGeneration = null
            return null
        }
        if (!pending.isFlushable()) {
            return null
        }
        pendingRoomRefresh = null
        recordCommittedSnapshot(pending)?.let { record ->
            committedProjectionId = record.projectionId
            committedProjectionRevision = record.projectionRevision
            committedProjectionGeneration = record.projectionGeneration
            committedTransactionId = record.transactionId
            committedRoomSnapshotRevision = record.roomSnapshotRevision
        }
        pendingSnapshotTransaction = null
        return pending
    }

    fun clear() {
        pendingRoomRefresh = null
        batchHasSnapshotPacket = false
        batchConnectionGeneration = null
        pendingCommitConnectionGeneration = null
        committedProjectionId = ""
        committedProjectionRevision = null
        committedProjectionGeneration = null
        committedTransactionId = ""
        committedRoomSnapshotRevision = null
        pendingSnapshotTransaction = null
    }

    fun beginBatch(connectionGeneration: Long? = null) {
        if (batchDepth == 0) {
            batchHasSnapshotPacket = false
            batchConnectionGeneration = connectionGeneration
        }
        batchDepth += 1
    }

    fun endBatch(): Boolean {
        batchDepth = (batchDepth - 1).coerceAtLeast(0)
        if (batchDepth != 0) {
            return false
        }
        batchHasSnapshotPacket = false
        val pending =
            pendingRoomRefresh ?: run {
                batchConnectionGeneration = null
                return false
            }
        pendingCommitConnectionGeneration = batchConnectionGeneration
        batchConnectionGeneration = null
        if (pending.snapshotInconsistent) {
            pendingRoomRefresh = null
            pendingSnapshotTransaction = null
            pendingCommitConnectionGeneration = null
            return false
        }
        return pending.isFlushable()
    }

    fun latestMapGrid(): MapGridPacket? = sink.currentState().latestMapGrid

    /** 首屏不变量：是否已提交与当前投影同代的完整房间事务（REAL+TITL+EXIT+NPCS/ITEM 权威收敛）。 */
    fun hasCommittedCompleteSnapshot(
        projectionId: String,
        projectionRevision: Long?,
    ): Boolean = hasCommittedCompleteSnapshot(projectionId, projectionRevision, null)

    fun hasCommittedCompleteSnapshot(
        projectionId: String,
        projectionRevision: Long?,
        projectionGeneration: Long?,
    ): Boolean {
        if (committedRoomSnapshotRevision == null) {
            return false
        }
        if (projectionId.isNotBlank() && committedProjectionId != projectionId) {
            return false
        }
        if (projectionRevision != null && committedProjectionRevision != projectionRevision) {
            return false
        }
        if (projectionGeneration != null && committedProjectionGeneration != projectionGeneration) {
            return false
        }
        return committedTransactionId.isNotBlank()
    }

    fun primaryQuickActionGroup(): QuickActionGroupPacket? = primaryQuickActionGroup

    fun secondaryQuickActionGroup(): QuickActionGroupPacket? = secondaryQuickActionGroup

    fun dynamicButtonGroup(): QuickActionGroupPacket? = dynamicButtonGroup

    fun resetTransientCaches() {
        primaryQuickActionGroup = null
        secondaryQuickActionGroup = null
        dynamicButtonGroup = null
        sink.updateState { copy(latestMapGrid = null) }
    }

    fun clearPendingRoomRefresh() {
        clear()
        roomRefreshJob?.cancel()
        roomRefreshJob = null
    }

    fun clearRoomProjection() {
        clearPendingRoomRefresh()
        resetTransientCaches()
    }

    fun endBatchAndShouldFlush(): Boolean {
        return endBatch()
    }

    fun stageRoomRefreshPacket(frame: IncomingFrame.Packet): Boolean {
        return stage(
            stage(
                frame,
                sink.currentState().canApplyRoomStatusImmediately(),
            ),
        )
    }

    fun stageRoomRefreshPacket(packet: StagedRoomPacket): Boolean {
        return stage(
            stage(
                packet,
                sink.currentState().canApplyRoomStatusImmediately(),
            ),
        )
    }

    fun flushPendingRoomRefresh() {
        val pending = flush() ?: return
        roomRefreshJob?.cancel()
        roomRefreshJob = null
        val applyResult =
            pending.applyToShellState(
                currentState = sink.currentState(),
                currentMapGrid = sink.currentState().latestMapGrid,
            )
        val shouldClearInteractionPending =
            shouldClearInteractionPendingForRoomRefresh(
                pending = sink.pendingInteractionRequest(),
                nextInteractionObjects = applyResult.state.interactionObjects,
                isRoomSwitch = applyResult.roomChanged,
            )
        if (applyResult.enteredMainShell) {
            sink.clearMessageFeeds()
        }
        sink.consumeLoginReadyBanner(applyResult.enteredMainShell)
        sink.updateState { applyResult.state }
        if (pending.receivedWorldPackets.isNotEmpty()) {
            clearPendingMoveAutoScroll()
        }
        if (shouldClearInteractionPending) {
            sink.clearPendingInteractionRequest()
        }
        /*
         * 路线只在权威房间提交（complete/empty_shell 或完整四段）后刷新一次；
         * 导航子包不得触发路线请求，避免一次移动产生多条重复 ROUT。
         */
        if (pending.isAuthoritativeRoomCommit()) {
            sink.markAuthoritativeRoomSnapshotCommitted(pendingCommitConnectionGeneration)
            sink.refreshTrackedQuestRoute()
        }
        pendingCommitConnectionGeneration = null
    }

    private fun stage(stageResult: RoomRefreshStageResult): Boolean {
        stageResult.legacyCompatibilitySource
            .takeIf { source -> source.isNotBlank() }
            ?.let(LegacyCompatibilityDiagnostics::record)
        stageResult.immediateStatusSnapshot?.let { snapshot ->
            sink.updateState {
                applyStandaloneStatusSnapshot(snapshot)
            }
        }
        if (stageResult.shouldFlush) {
            flushPendingRoomRefresh()
        } else if (stageResult.shouldScheduleFlush) {
            scheduleRoomRefreshFlush()
        }
        return stageResult.handled
    }

    private fun scheduleRoomRefreshFlush() {
        roomRefreshJob?.cancel()
        roomRefreshJob =
            scope.launch {
                delay(ROOM_REFRESH_BATCH_MS)
                flushPendingRoomRefresh()
            }
    }

    fun armPendingMoveAutoScroll() {
        pendingMoveAutoScroll = true
        moveAutoScrollJob?.cancel()
    }

    fun markPendingMoveAutoScrollActivity() {
        if (!pendingMoveAutoScroll) {
            return
        }

        moveAutoScrollJob?.cancel()
        moveAutoScrollJob =
            scope.launch {
                delay(MOVE_AUTO_SCROLL_SETTLE_MS)
                if (!pendingMoveAutoScroll) {
                    return@launch
                }

                pendingMoveAutoScroll = false
                sink.updateState {
                    copy(transcriptScrollRequest = transcriptScrollRequest + 1)
                }
            }
    }

    fun clearPendingMoveAutoScroll() {
        pendingMoveAutoScroll = false
        moveAutoScrollJob?.cancel()
        moveAutoScrollJob = null
    }

    fun cancel() {
        moveAutoScrollJob?.cancel()
        moveAutoScrollJob = null
    }
}

private const val MOVE_AUTO_SCROLL_SETTLE_MS = 48L
