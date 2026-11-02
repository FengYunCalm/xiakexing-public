package com.xiakexing.client.core.protocol.payload

import com.google.gson.JsonObject

data class TradeOfferItemPacket(
    val id: String,
    val name: String,
    val templateId: String,
    val quantity: Int,
)

data class TradeOfferPacket(
    val items: List<TradeOfferItemPacket>,
    val money: Int,
    val confirmed: Boolean,
) {
    companion object {
        fun fromJson(payload: JsonObject): TradeOfferPacket {
            return TradeOfferPacket(
                items =
                    payload.payloadObjectArray("items").map { item ->
                        TradeOfferItemPacket(
                            id = item.string("id"),
                            name = item.string("name"),
                            templateId = item.string("template_id"),
                            quantity = item.int("quantity").coerceAtLeast(1),
                        )
                    }.filter { item -> item.id.isNotBlank() || item.name.isNotBlank() },
                money = payload.int("money").coerceAtLeast(0),
                confirmed = payload.bool("confirmed"),
            )
        }
    }
}

data class TradeSessionPacket(
    val action: String,
    val sessionId: String,
    val revision: Int,
    val status: String,
    val counterpartyId: String,
    val counterparty: String,
    val selfOffer: TradeOfferPacket,
    val counterpartyOffer: TradeOfferPacket,
    val availableActions: List<String>,
    val expiresAt: Long,
    val terminalResult: String,
    val terminalSuccess: Boolean,
) {
    companion object {
        fun fromJson(payload: JsonObject): TradeSessionPacket {
            return TradeSessionPacket(
                action = payload.string("action"),
                sessionId = payload.string("session_id"),
                revision = payload.int("revision", 1).coerceAtLeast(1),
                status = payload.string("status"),
                counterpartyId = payload.string("counterparty_id"),
                counterparty = payload.string("counterparty"),
                selfOffer = TradeOfferPacket.fromJson(payload.payloadObject("self_offer")),
                counterpartyOffer =
                    TradeOfferPacket.fromJson(payload.payloadObject("counterparty_offer")),
                availableActions = payload.payloadStringArray("available_actions"),
                expiresAt = payload.payloadLong("expires_at").coerceAtLeast(0L),
                terminalResult = payload.string("terminal_result"),
                terminalSuccess = payload.bool("terminal_success"),
            )
        }
    }
}

data class TradeResultPacket(
    val action: String,
    val item: String,
    val price: Int,
    val priceLabel: String,
    val success: Boolean,
    val balance: Int,
    val balanceLabel: String,
    val message: String,
    val enhanceLevel: Int,
    val itemId: String,
    val itemType: String,
    val subType: String,
    val quality: Int,
    val requiredSkillCap: Int,
    val durability: Int,
    val maxDurability: Int,
) {
    companion object {
        fun fromJson(payload: JsonObject): TradeResultPacket {
            return TradeResultPacket(
                action = payload.string("action"),
                item = payload.string("item"),
                price = payload.int("price"),
                priceLabel = payload.string("price_label"),
                success = payload.bool("success"),
                balance = payload.int("balance"),
                balanceLabel = payload.string("balance_label"),
                message = payload.string("message"),
                enhanceLevel = payload.int("enhance_level"),
                itemId = payload.string("item_id"),
                itemType = payload.string("type"),
                subType = payload.string("sub_type"),
                quality = payload.int("quality"),
                requiredSkillCap = payload.int("required_skill_cap"),
                durability = payload.int("durability"),
                maxDurability = payload.int("max_durability"),
            )
        }
    }
}

private fun JsonObject.string(key: String): String {
    return payloadString(key)
}

private fun JsonObject.int(
    key: String,
    default: Int = 0,
): Int {
    return payloadInt(key, default)
}

private fun JsonObject.bool(key: String): Boolean {
    if (!has(key) || get(key).isJsonNull) {
        return false
    }
    val value = get(key).asJsonPrimitive
    return if (value.isBoolean) value.asBoolean else payloadLong(key) != 0L
}
