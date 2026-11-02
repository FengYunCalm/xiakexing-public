package com.xiakexing.client.feature.shell

import com.google.gson.JsonObject
import com.xiakexing.client.core.protocol.AnsiSanitizer
import com.xiakexing.client.core.protocol.IncomingFrame
import com.xiakexing.client.core.protocol.XkMessageType
import com.xiakexing.client.core.protocol.XkPacketParser
import com.xiakexing.client.core.protocol.XkStreamDecoder
import com.xiakexing.client.core.protocol.payload.CombatDamagePacket
import com.xiakexing.client.core.protocol.payload.CombatEndPacket
import com.xiakexing.client.core.protocol.payload.CombatRoundPacket
import com.xiakexing.client.core.protocol.payload.CombatRuntimeSnapshotPacket
import com.xiakexing.client.core.protocol.payload.CombatStartPacket
import com.xiakexing.client.core.protocol.payload.DialogPacket
import com.xiakexing.client.core.protocol.payload.LoginPrompt
import com.xiakexing.client.core.protocol.payload.MessageEventPacket
import com.xiakexing.client.core.protocol.payload.QuestListPacket
import com.xiakexing.client.core.protocol.payload.QuestUpdatePacket
import com.xiakexing.client.core.protocol.payload.ShopPacket
import com.xiakexing.client.core.protocol.payload.SkillEventPacket
import com.xiakexing.client.core.protocol.payload.SkillListPacket

internal sealed interface PreparedProtocolPayload {
    data class Dialog(val packet: DialogPacket) : PreparedProtocolPayload

    data class SkillList(val packet: SkillListPacket) : PreparedProtocolPayload

    data class Shop(val packet: ShopPacket) : PreparedProtocolPayload

    data class QuestList(val packet: QuestListPacket) : PreparedProtocolPayload

    data class QuestUpdate(val packet: QuestUpdatePacket) : PreparedProtocolPayload

    data class CombatStart(val packet: CombatStartPacket) : PreparedProtocolPayload

    data class CombatRound(val packet: CombatRoundPacket) : PreparedProtocolPayload

    data class SkillEvent(val packet: SkillEventPacket) : PreparedProtocolPayload

    data class CombatRuntime(val packet: CombatRuntimeSnapshotPacket) : PreparedProtocolPayload

    data class CombatDamage(val packet: CombatDamagePacket) : PreparedProtocolPayload

    data class CombatEnd(val packet: CombatEndPacket) : PreparedProtocolPayload

    data class MessageEvent(val packet: MessageEventPacket) : PreparedProtocolPayload
}

internal sealed interface DecodedIncomingSegment {
    data class Text(val text: String) : DecodedIncomingSegment

    data class InvalidPacket(val frame: IncomingFrame.InvalidPacket) : DecodedIncomingSegment

    data class RoomPacket(val packet: StagedRoomPacket) : DecodedIncomingSegment

    data class Packet(
        val frame: IncomingFrame.Packet,
        val preparedBatchChildren: List<DecodedBatchChild>? = null,
        val preparedPayload: PreparedProtocolPayload? = null,
    ) : DecodedIncomingSegment
}

internal sealed interface DecodedBatchChild {
    data class RoomPacket(val packet: StagedRoomPacket) : DecodedBatchChild

    data class Packet(
        val frame: IncomingFrame.Packet,
        val preparedPayload: PreparedProtocolPayload? = null,
    ) : DecodedBatchChild
}

internal const val MAX_BATCH_CHILDREN = 256

internal fun XkStreamDecoder.decodeIncomingMessage(
    rawMessage: String,
): List<DecodedIncomingSegment> {
    return feed(rawMessage).mapNotNull { segment ->
        when (segment) {
            is XkStreamDecoder.Segment.RawText -> segment.toDecodedIncomingText()
            is XkStreamDecoder.Segment.XkPacket -> segment.toDecodedIncomingSegment()
            is XkStreamDecoder.Segment.InvalidPacket ->
                DecodedIncomingSegment.InvalidPacket(
                    IncomingFrame.InvalidPacket(rawType = "stream", reason = segment.reason),
                )
        }
    }
}

internal fun List<DecodedIncomingSegment>.containsLoginPromptFrame(): Boolean {
    return any { segment ->
        segment is DecodedIncomingSegment.Packet &&
            segment.frame.envelope.type == XkMessageType.LOGN &&
            LoginPrompt.fromJson(segment.frame.envelope.payload).requestsCredentials()
    }
}

internal fun IncomingFrame.toDecodedIncomingSegment(): DecodedIncomingSegment? {
    return when (this) {
        is IncomingFrame.InvalidPacket -> DecodedIncomingSegment.InvalidPacket(this)
        is IncomingFrame.PlainText ->
            AnsiSanitizer.sanitize(text)
                .takeIf { it.isNotBlank() }
                ?.let(DecodedIncomingSegment::Text)

        is IncomingFrame.Packet ->
            runCatching { toStagedRoomPacketOrNull() }.getOrNull()
                ?.let(DecodedIncomingSegment::RoomPacket)
                ?: DecodedIncomingSegment.Packet(
                    frame = this,
                    preparedBatchChildren = envelope.payload.prepareBatchChildrenIfNeeded(envelope.type),
                    preparedPayload = prepareProtocolPayloadOrNull(),
                )
    }
}

private fun XkStreamDecoder.Segment.RawText.toDecodedIncomingText(): DecodedIncomingSegment? =
    IncomingFrame.PlainText(text).toDecodedIncomingSegment()

private fun XkStreamDecoder.Segment.XkPacket.toDecodedIncomingSegment(): DecodedIncomingSegment? =
    XkPacketParser.parse(text).toDecodedIncomingSegment()

internal fun JsonObject.prepareBatchChildrenIfNeeded(
    type: XkMessageType?,
): List<DecodedBatchChild>? {
    if (type != XkMessageType.BACH) {
        return null
    }
    val messages = get("messages")?.takeIf { it.isJsonArray }?.asJsonArray ?: return emptyList()
    return messages.asSequence()
        .take(MAX_BATCH_CHILDREN)
        .mapNotNull { child ->
            child.toBatchStagedRoomPacketOrNull()
                ?.let(DecodedBatchChild::RoomPacket)
                ?: child.toBatchFrameOrNull()?.let { frame ->
                    DecodedBatchChild.Packet(
                        frame = frame,
                        preparedPayload = frame.prepareProtocolPayloadOrNull(),
                    )
                }
        }
        .toList()
}

private fun IncomingFrame.Packet.prepareProtocolPayloadOrNull(): PreparedProtocolPayload? {
    return runCatching {
        when (envelope.type) {
            XkMessageType.DLOG ->
                PreparedProtocolPayload.Dialog(
                    DialogPacket.fromJson(envelope.payload),
                )
            XkMessageType.SKLS ->
                PreparedProtocolPayload.SkillList(
                    SkillListPacket.fromJson(envelope.payload),
                )
            XkMessageType.SHOP ->
                PreparedProtocolPayload.Shop(
                    ShopPacket.fromJson(envelope.payload),
                )
            XkMessageType.QUES ->
                PreparedProtocolPayload.QuestList(
                    QuestListPacket.fromJson(envelope.payload),
                )
            XkMessageType.QUEU ->
                PreparedProtocolPayload.QuestUpdate(
                    QuestUpdatePacket.fromJson(envelope.payload),
                )
            XkMessageType.CBST ->
                PreparedProtocolPayload.CombatStart(
                    CombatStartPacket.fromJson(envelope.payload),
                )
            XkMessageType.CBRD ->
                PreparedProtocolPayload.CombatRound(
                    CombatRoundPacket.fromJson(envelope.payload),
                )
            XkMessageType.CBSK ->
                PreparedProtocolPayload.SkillEvent(
                    SkillEventPacket.fromJson(envelope.payload),
                )
            XkMessageType.CBRT ->
                PreparedProtocolPayload.CombatRuntime(
                    CombatRuntimeSnapshotPacket.fromJson(envelope.payload),
                )
            XkMessageType.CBDM ->
                PreparedProtocolPayload.CombatDamage(
                    CombatDamagePacket.fromJson(envelope.payload),
                )
            XkMessageType.CBED ->
                PreparedProtocolPayload.CombatEnd(
                    CombatEndPacket.fromJson(envelope.payload),
                )
            XkMessageType.MSGE ->
                PreparedProtocolPayload.MessageEvent(
                    MessageEventPacket.fromJson(envelope.payload),
                )
            else -> null
        }
    }.getOrNull()
}
