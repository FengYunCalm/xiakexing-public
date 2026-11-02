package com.xiakexing.client.feature.shell

import com.xiakexing.client.core.protocol.IncomingFrame
import com.xiakexing.client.core.protocol.XkMessageType
import com.xiakexing.client.core.protocol.payload.CombatDamagePacket
import com.xiakexing.client.core.protocol.payload.CombatEndPacket
import com.xiakexing.client.core.protocol.payload.CombatRoundPacket
import com.xiakexing.client.core.protocol.payload.CombatRuntimeSnapshotPacket
import com.xiakexing.client.core.protocol.payload.CombatStartPacket
import com.xiakexing.client.core.protocol.payload.DialogPacket
import com.xiakexing.client.core.protocol.payload.MessageEventPacket
import com.xiakexing.client.core.protocol.payload.QuestListPacket
import com.xiakexing.client.core.protocol.payload.QuestUpdatePacket
import com.xiakexing.client.core.protocol.payload.RoutePreviewPacket
import com.xiakexing.client.core.protocol.payload.ShopPacket
import com.xiakexing.client.core.protocol.payload.SkillEventPacket
import com.xiakexing.client.core.protocol.payload.SkillListPacket
import com.xiakexing.client.core.protocol.payload.TextPacket

/**
 * packet type -> owner 的不可变声明表。
 * 实现收敛为中央表（buildDefaultPacketOwnerRoute，组合根 buildShellGraph
 * 构造一次；登录语义留在 ShellPacketHandler，BACH 走 ingress 独立路径）。
 * 重复类型在构建时 fail loud；ShellPacketHandler 只查表调用，不保留 when 链。
 */
internal data class PacketRouteEntry(
    val ownerId: String,
    val dispatch: (frame: IncomingFrame.Packet, preparedPayload: PreparedProtocolPayload?) -> Unit,
)

internal class PacketOwnerRoute private constructor(
    private val entries: Map<XkMessageType, PacketRouteEntry>,
) {
    fun resolve(type: XkMessageType?): PacketRouteEntry? = entries[type]

    fun ownerCount(): Int = entries.size

    class Builder {
        private val entries = linkedMapOf<XkMessageType, PacketRouteEntry>()

        fun register(
            ownerId: String,
            type: XkMessageType,
            dispatch: (
                frame: IncomingFrame.Packet,
                preparedPayload: PreparedProtocolPayload?,
            ) -> Unit,
        ): Builder =
            apply {
                check(entries.put(type, PacketRouteEntry(ownerId, dispatch)) == null) {
                    "packet type 重复注册: $type (owner=$ownerId)"
                }
            }

        fun build(): PacketOwnerRoute = PacketOwnerRoute(entries.toMap())
    }
}

/**
 * 中央声明表：全部非登录 packet type -> owner 路由。
 * （计划"各 Bindings 登记"的实现收敛——登录语义留在 ShellPacketHandler，
 * 本表由组合根 buildShellGraph 构造一次。）
 */
internal fun buildDefaultPacketOwnerRoute(
    room: RoomProjectionCoordinator,
    quest: QuestCoordinator,
    skill: SkillCoordinator,
    combat: CombatCoordinator,
    message: MessageCoordinator,
    interactionSession: InteractionSessionCoordinator,
    inventoryTrade: InventoryTradeCoordinator,
    projectionRuntime: ProjectionRuntimeCoordinator,
    login: LoginCoordinator,
): PacketOwnerRoute {
    val b = PacketOwnerRoute.Builder()

    b.register("RoomProjection", XkMessageType.BTNS) { frame, _ ->
        room.handleDynamicButtonPacket(frame.envelope.payload)
    }
    b.register("RoomProjection", XkMessageType.ACT1) { frame, _ ->
        room.handlePrimaryQuickActionGroupPacket(frame.envelope.payload)
    }
    b.register("RoomProjection", XkMessageType.ACT2) { frame, _ ->
        room.handleSecondaryQuickActionGroupPacket(frame.envelope.payload)
    }
    b.register("RoomProjection", XkMessageType.MAPS) { frame, _ ->
        room.handleMapGridPacket(frame.envelope.payload)
    }
    b.register("Quest", XkMessageType.ROUT) { frame, _ ->
        quest.handleRoutePreviewPacket(RoutePreviewPacket.fromJson(frame.envelope.payload))
    }
    b.register("Quest", XkMessageType.QUES) { frame, prepared ->
        val packet =
            (prepared as? PreparedProtocolPayload.QuestList)?.packet
                ?: QuestListPacket.fromJson(frame.envelope.payload)
        quest.handleQuestListPacket(packet)
    }
    b.register("Quest", XkMessageType.QUEU) { frame, prepared ->
        val packet =
            (prepared as? PreparedProtocolPayload.QuestUpdate)?.packet
                ?: QuestUpdatePacket.fromJson(frame.envelope.payload)
        quest.handleQuestUpdatePacket(packet)
    }
    b.register("Skill", XkMessageType.SKLS) { frame, prepared ->
        val packet =
            (prepared as? PreparedProtocolPayload.SkillList)?.packet
                ?: SkillListPacket.fromJson(frame.envelope.payload)
        skill.handleSkillListPacket(packet)
    }
    b.register("Skill", XkMessageType.SKLI) { frame, _ ->
        skill.handleSkillInfoPacket(
            frame.envelope.payload,
            frame.envelope.commandResponseId,
        )
    }
    b.register("Combat", XkMessageType.CBST) { frame, prepared ->
        val packet =
            (prepared as? PreparedProtocolPayload.CombatStart)?.packet
                ?: CombatStartPacket.fromJson(frame.envelope.payload)
        combat.handleCombatStartPacket(packet)
    }
    b.register("Combat", XkMessageType.CBRD) { frame, prepared ->
        val packet =
            (prepared as? PreparedProtocolPayload.CombatRound)?.packet
                ?: CombatRoundPacket.fromJson(frame.envelope.payload)
        combat.handleCombatRoundPacket(packet)
    }
    b.register("Skill", XkMessageType.CBSK) { frame, prepared ->
        val packet =
            (prepared as? PreparedProtocolPayload.SkillEvent)?.packet
                ?: SkillEventPacket.fromJson(frame.envelope.payload)
        // skill-first：技能激活事件归 Skill 域，未消费时 combat 兜底（旧 VM sink 语义）。
        if (!skill.handleSkillEventPacket(packet)) {
            combat.handleSkillEventPacket(packet)
        }
    }
    b.register("Combat", XkMessageType.CBRT) { frame, prepared ->
        val packet =
            (prepared as? PreparedProtocolPayload.CombatRuntime)?.packet
                ?: CombatRuntimeSnapshotPacket.fromJson(frame.envelope.payload)
        combat.handleCombatRuntimePacket(packet)
    }
    b.register("Combat", XkMessageType.CBDM) { frame, prepared ->
        val packet =
            (prepared as? PreparedProtocolPayload.CombatDamage)?.packet
                ?: CombatDamagePacket.fromJson(frame.envelope.payload)
        combat.handleCombatDamagePacket(packet)
    }
    b.register("Combat", XkMessageType.CBED) { frame, prepared ->
        val packet =
            (prepared as? PreparedProtocolPayload.CombatEnd)?.packet
                ?: CombatEndPacket.fromJson(frame.envelope.payload)
        combat.handleCombatEndPacket(packet)
    }
    b.register("Combat", XkMessageType.HPBR) { frame, _ ->
        combat.handleRealtimeStatusPacket("hp", frame.envelope.payload)
    }
    b.register("Combat", XkMessageType.MPBR) { frame, _ ->
        combat.handleRealtimeStatusPacket("mp", frame.envelope.payload)
    }
    b.register("Message", XkMessageType.TEXT) { frame, _ ->
        val packet = TextPacket.fromJson(frame.envelope.payload)
        if (packet.channel != "main" || packet.text.isBlank()) {
            return@register
        }
        message.appendTranscript(packet.text, frame.envelope.commandResponseId)
    }
    b.register("Message", XkMessageType.MSGE) { frame, prepared ->
        val packet =
            (prepared as? PreparedProtocolPayload.MessageEvent)?.packet
                ?: MessageEventPacket.fromJson(frame.envelope.payload)
        message.handleMessageEventPacket(packet, frame.envelope.commandResponseId)
    }
    b.register("Message", XkMessageType.SYST) { frame, _ ->
        message.handleSystemPacket(frame.envelope.payload)
    }
    b.register("Message", XkMessageType.CHAT) { frame, _ ->
        message.handleChatPacket(frame.envelope.payload)
    }
    b.register("Message", XkMessageType.PRMT) { frame, _ ->
        message.handlePromptPacket(
            frame.envelope.payload,
            frame.envelope.commandResponseId,
        )
    }
    b.register("Message", XkMessageType.ERRO) { frame, _ ->
        message.handleErrorPacket(
            frame.envelope.payload,
            frame.envelope.commandResponseId,
        )
    }
    b.register("InteractionSession", XkMessageType.DLOG) { frame, prepared ->
        val packet =
            (prepared as? PreparedProtocolPayload.Dialog)?.packet
                ?: DialogPacket.fromJson(frame.envelope.payload)
        interactionSession.handleDialogPacket(packet, frame.envelope.commandResponseId)
    }
    b.register("InventoryTrade", XkMessageType.SHOP) { frame, prepared ->
        val packet =
            (prepared as? PreparedProtocolPayload.Shop)?.packet
                ?: ShopPacket.fromJson(frame.envelope.payload)
        inventoryTrade.handleShopPacket(packet)
    }
    b.register("InventoryTrade", XkMessageType.TRDE) { frame, _ ->
        inventoryTrade.handleTradeResultPacket(frame.envelope.payload)
    }
    b.register("RuntimeScope", XkMessageType.REAL) { frame, _ ->
        projectionRuntime.handleProjectionState(frame.envelope.payload)
    }
    b.register("Login", XkMessageType.RPRT) { frame, _ ->
        login.handleReportPacket(frame.envelope.payload)
    }
    return b.build()
}
