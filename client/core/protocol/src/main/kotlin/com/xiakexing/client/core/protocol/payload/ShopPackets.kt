package com.xiakexing.client.core.protocol.payload

import com.google.gson.JsonObject

data class ShopItemPacket(
    val id: String,
    val name: String,
    val price: Int,
    val priceLabel: String,
    val stock: Int,
    val requiredSkillCap: Int,
)

data class ShopPacket(
    val shopName: String,
    val npcId: String,
    val items: List<ShopItemPacket>,
) {
    companion object {
        fun fromJson(payload: JsonObject): ShopPacket {
            val items =
                payload.array("items").map { element ->
                    ShopItemPacket(
                        id = element.obj().string("id"),
                        name = element.obj().string("name"),
                        price = element.obj().int("price"),
                        priceLabel = element.obj().string("price_label"),
                        stock = element.obj().int("stock", -1),
                        requiredSkillCap = element.obj().int("required_skill_cap"),
                    )
                }.filter { it.id.isNotBlank() || it.name.isNotBlank() }

            return ShopPacket(
                shopName = payload.string("shop_name"),
                npcId = payload.string("npc_id"),
                items = items,
            )
        }
    }
}

private fun JsonObject.array(key: String) = payloadArray(key)

private fun JsonObject.string(key: String): String = payloadString(key)

private fun JsonObject.int(
    key: String,
    default: Int = 0,
): Int = payloadInt(key, default)

private fun com.google.gson.JsonElement.obj(): JsonObject = asJsonObject
