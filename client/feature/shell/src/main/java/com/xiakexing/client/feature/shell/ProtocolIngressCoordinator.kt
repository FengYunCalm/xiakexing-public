package com.xiakexing.client.feature.shell

import android.util.Log
import com.google.gson.JsonObject
import com.xiakexing.client.core.protocol.IncomingFrame
import com.xiakexing.client.core.protocol.XkMessageType
import com.xiakexing.client.core.protocol.XkProtocolMeta
import com.xiakexing.client.core.protocol.XkStreamDecoder
import com.xiakexing.client.core.protocol.payload.MessageEventPacket
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/**
 * ProtocolIngress 域 owner——帧解码/分段、gate 校验、批处理
 * 与诊断观测（ShellIncomingMessageCoordinator + ShellPacketDiagnosticsCoordinator
 * 收敛）。帧 drop 计数与 gate 拒绝观测仍由本 owner 输出。
 */
internal class ProtocolIngressCoordinator(
    private val streamDecoder: XkStreamDecoder,
    private val isDebugBuild: Boolean,
    private val sink: Sink,
    private val packetGate: XkProtocolPacketGate = XkProtocolPacketGate(),
    private val decodeDispatcher: CoroutineDispatcher = Dispatchers.Default,
) {
    internal data class PreparedIncomingMessage(
        val segments: List<DecodedIncomingSegment>,
    )

    interface Sink {
        fun appendTranscript(line: String)

        fun stageRoomRefreshPacket(frame: IncomingFrame.Packet): Boolean

        fun stageRoomRefreshPacket(packet: StagedRoomPacket): Boolean

        fun flushPendingRoomRefresh()

        fun beginRoomPacketBatch(connectionGeneration: Long? = null)

        fun endRoomPacketBatchAndShouldFlush(): Boolean

        fun acceptsCommandResponse(
            commandResponseId: String,
            connectionGeneration: Long? = null,
        ): Boolean

        fun dispatchPacket(
            frame: IncomingFrame.Packet,
            preparedPayload: PreparedProtocolPayload?,
        )
    }

    suspend fun decodeIncomingMessage(rawMessage: String): PreparedIncomingMessage {
        return withContext(decodeDispatcher) {
            val segments =
                streamDecoder.decodeIncomingMessage(rawMessage)
                    .dropLegacyEchoesBeforeMessageEvents()
            PreparedIncomingMessage(segments = segments)
        }
    }

    /** Applies the already-framed result produced by the process ingress owner. */
    suspend fun decodeIncomingFrames(frames: List<IncomingFrame>): PreparedIncomingMessage {
        return withContext(decodeDispatcher) {
            PreparedIncomingMessage(
                segments =
                    frames
                        .mapNotNull { it.toDecodedIncomingSegment() }
                        .dropLegacyEchoesBeforeMessageEvents(),
            )
        }
    }

    fun applyIncomingMessage(
        decodedSegments: List<DecodedIncomingSegment>,
        connectionGeneration: Long? = null,
    ) {
        decodedSegments.forEach { segment ->
            when (segment) {
                is DecodedIncomingSegment.InvalidPacket -> {
                    val frame = segment.frame
                    if (surfacedPacketTypes.add("invalid:${frame.rawType}:${frame.reason}")) {
                        Log.w(
                            TAG,
                            "Malformed XK packet type=${frame.rawType} reason=${frame.reason}",
                        )
                    }
                }

                is DecodedIncomingSegment.Text -> {
                    sink.flushPendingRoomRefresh()
                    sink.appendTranscript(segment.text)
                }

                is DecodedIncomingSegment.RoomPacket -> {
                    if (!packetGate.accept(segment.packet.messageType(), segment.packet.meta)) {
                        return@forEach
                    }
                    sink.stageRoomRefreshPacket(segment.packet)
                }

                is DecodedIncomingSegment.Packet -> {
                    val batchChildren = segment.preparedBatchChildren
                    if (batchChildren == null) {
                        handlePacketSafely(
                            segment.frame,
                            segment.preparedPayload,
                            connectionGeneration,
                        )
                    } else {
                        handlePreparedBatchSafely(
                            segment.frame,
                            batchChildren,
                            connectionGeneration,
                        )
                    }
                }
            }
        }
    }

    fun handleBatchPacket(
        payload: JsonObject,
        connectionGeneration: Long? = null,
    ) {
        val children = payload.prepareBatchChildrenIfNeeded(XkMessageType.BACH) ?: return
        applyPreparedBatch(children, connectionGeneration)
    }

    fun resetPacketGate() {
        packetGate.reset()
    }

    // ===== 诊断观测（原 ShellPacketDiagnosticsCoordinator） =====

    private val surfacedPacketTypes = linkedSetOf<String>()
    private var lastPortDroppedFrameCount = 0L
    private var lastPortProtocolDroppedFrameCount = 0L
    private var lastRepositoryDroppedFrameCount = 0L

    /** 三层 drop 可观测化：port/repository 任一计数变化即告警日志（插桩属主在 coordinator，VM 只喂值）。 */
    fun observeFrameDrop(
        portDropped: Long,
        portProtocolDropped: Long,
        repositoryDropped: Long,
    ) {
        if (portDropped != lastPortDroppedFrameCount ||
            portProtocolDropped != lastPortProtocolDroppedFrameCount ||
            repositoryDropped != lastRepositoryDroppedFrameCount
        ) {
            lastPortDroppedFrameCount = portDropped
            lastPortProtocolDroppedFrameCount = portProtocolDropped
            lastRepositoryDroppedFrameCount = repositoryDropped
            Log.w(
                TAG,
                "frame drop port=$portDropped " +
                    "portProtocol=$portProtocolDropped repository=$repositoryDropped",
            )
        }
    }

    /** 挂账⑤：gate 拒绝分布观测（正式包保留 Log.w，原因/投影/seq/epoch 可定位）。 */
    fun observeGateRejection(
        reason: String,
        meta: XkProtocolMeta,
    ) {
        Log.w(
            TAG,
            "gate reject reason=$reason " +
                "type=${meta.snapshotKind.ifBlank { "-" }} room=${meta.snapshotRoomId.ifBlank { "-" }} " +
                "snapProj=${meta.snapshotProjectionId.ifBlank { "-" }}@${meta.snapshotProjectionRevision ?: -1L} " +
                "seq=${meta.serverSeq ?: -1L} stream=${meta.stream.ifBlank { "-" }} epoch=${meta.epoch ?: -1L}",
        )
    }

    fun clearPacketTypes() {
        surfacedPacketTypes.clear()
    }

    fun handleMalformedPacket(
        frame: IncomingFrame.Packet,
        error: Throwable,
    ) {
        sink.flushPendingRoomRefresh()
        if (!isDebugBuild) {
            return
        }

        val rawType = frame.envelope.rawType.ifBlank { "????" }
        if (!surfacedPacketTypes.add("bad:$rawType")) {
            return
        }
        sink.appendTranscript(
            "协议异常，已跳过 $rawType：${error.message.orEmpty().ifBlank { error::class.java.simpleName }}",
        )
    }

    fun handleUnhandledPacket(frame: IncomingFrame.Packet) {
        val rawType = frame.envelope.rawType.ifBlank { "????" }
        if (!isDebugBuild) {
            if (surfacedPacketTypes.add(rawType)) {
                Log.w(TAG, "Unhandled XK packet type=$rawType")
            }
            return
        }
        if (!surfacedPacketTypes.add(rawType)) {
            return
        }
        val kind = if (frame.envelope.type == null) "未知" else "未接入"
        sink.appendTranscript(
            "[协议] ${kind}XK包 $rawType ${frame.envelope.payload.debugSummary()}".trim(),
        )
    }

    // ===== 帧处理 =====

    private fun handlePreparedBatchSafely(
        frame: IncomingFrame.Packet,
        children: List<DecodedBatchChild>,
        connectionGeneration: Long?,
    ) {
        if (!packetGate.accept(
                frame.envelope.type,
                frame.envelope.meta,
                frame.envelope.payload,
            )
        ) {
            return
        }
        runCatching {
            applyPreparedBatch(children, connectionGeneration)
        }.onFailure { error ->
            handleMalformedPacket(frame, error)
        }
    }

    private fun applyPreparedBatch(
        children: List<DecodedBatchChild>,
        connectionGeneration: Long?,
    ) {
        sink.beginRoomPacketBatch(connectionGeneration)
        try {
            children.forEach { child ->
                when (child) {
                    is DecodedBatchChild.RoomPacket -> {
                        if (packetGate.accept(child.packet.messageType(), child.packet.meta)) {
                            sink.stageRoomRefreshPacket(child.packet)
                        }
                    }

                    is DecodedBatchChild.Packet -> {
                        if (shouldDropStaleInteractiveResponse(child.frame, connectionGeneration)) {
                            return@forEach
                        }
                        handlePacketSafely(
                            child.frame,
                            child.preparedPayload,
                            connectionGeneration,
                        )
                    }
                }
            }
        } finally {
            if (sink.endRoomPacketBatchAndShouldFlush()) {
                sink.flushPendingRoomRefresh()
            }
        }
    }

    private fun handlePacketSafely(
        frame: IncomingFrame.Packet,
        preparedPayload: PreparedProtocolPayload? = null,
        connectionGeneration: Long? = null,
    ) {
        if (!packetGate.accept(
                frame.envelope.type,
                frame.envelope.meta,
                frame.envelope.payload,
            )
        ) {
            return
        }
        if (shouldDropStaleInteractiveResponse(frame, connectionGeneration)) {
            return
        }
        runCatching {
            handlePacket(
                normalizeResponseAssociation(frame, connectionGeneration),
                preparedPayload,
            )
        }.onFailure { error ->
            handleMalformedPacket(frame, error)
        }
    }

    private fun shouldDropStaleInteractiveResponse(
        frame: IncomingFrame.Packet,
        connectionGeneration: Long? = null,
    ): Boolean {
        val responseId = frame.envelope.commandResponseId
        return responseId.isNotBlank() &&
            frame.envelope.type == XkMessageType.DLOG &&
            !sink.acceptsCommandResponse(responseId, connectionGeneration)
    }

    private fun normalizeResponseAssociation(
        frame: IncomingFrame.Packet,
        connectionGeneration: Long? = null,
    ): IncomingFrame.Packet {
        val responseId = frame.envelope.commandResponseId
        if (responseId.isBlank() ||
            sink.acceptsCommandResponse(responseId, connectionGeneration) ||
            frame.envelope.type == XkMessageType.SKLI
        ) {
            return frame
        }
        return frame.copy(envelope = frame.envelope.copy(commandResponseId = ""))
    }

    private fun handlePacket(
        frame: IncomingFrame.Packet,
        preparedPayload: PreparedProtocolPayload?,
    ) {
        if (sink.stageRoomRefreshPacket(frame)) {
            return
        }

        sink.flushPendingRoomRefresh()
        sink.dispatchPacket(frame, preparedPayload)
    }
}

private const val TAG = "XkxPacketDiagnostics"

internal fun List<DecodedIncomingSegment>.dropLegacyEchoesBeforeMessageEvents(): List<DecodedIncomingSegment> {
    if (size < 2) {
        return this
    }
    var retained: MutableList<DecodedIncomingSegment>? = null
    var index = 0
    while (index < size) {
        val current = this[index]
        val next = getOrNull(index + 1)
        if (current is DecodedIncomingSegment.Text && next is DecodedIncomingSegment.Packet &&
            next.isMessageEventEchoOf(current.text)
        ) {
            if (retained == null) {
                retained = ArrayList(size - 1)
                retained.addAll(subList(0, index))
            }
            index += 1
            continue
        }
        retained?.add(current)
        index += 1
    }
    return retained ?: this
}

private fun DecodedIncomingSegment.Packet.isMessageEventEchoOf(text: String): Boolean {
    if (frame.envelope.type != XkMessageType.MSGE) {
        return false
    }
    val message =
        (preparedPayload as? PreparedProtocolPayload.MessageEvent)?.packet
            ?: runCatching { MessageEventPacket.fromJson(frame.envelope.payload) }.getOrNull()
            ?: return false
    if (message.displayMode == "silent_state" || message.text.isBlank()) {
        return false
    }
    return text.normalizeInteractionLine() == message.text.normalizeInteractionLine()
}
