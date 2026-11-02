package com.xiakexing.client.core.protocol.payload

import com.google.gson.JsonArray
import com.google.gson.JsonElement
import com.google.gson.JsonObject
import java.math.BigDecimal

internal fun JsonObject.payloadString(key: String): String {
    return if (has(key) && !get(key).isJsonNull && get(key).isJsonPrimitive) {
        get(key).asString
    } else {
        ""
    }
}

internal fun JsonObject.payloadStrictString(key: String): String? {
    if (!has(key) || get(key).isJsonNull || !get(key).isJsonPrimitive) return null
    val primitive = get(key).asJsonPrimitive
    return primitive.takeIf { it.isString }?.asString?.trim()
}

internal fun JsonObject.payloadStrictLong(key: String): Long? {
    if (!has(key) || get(key).isJsonNull || !get(key).isJsonPrimitive) return null
    val primitive = get(key).asJsonPrimitive
    if (!primitive.isNumber) return null
    return primitive.asString.trim().toLongOrNull()
}

internal fun JsonObject.payloadStrictBoolean(key: String): Boolean? {
    if (!has(key) || get(key).isJsonNull || !get(key).isJsonPrimitive) return null
    val primitive = get(key).asJsonPrimitive
    if (primitive.isBoolean) return primitive.asBoolean
    if (!primitive.isNumber) return null
    return when (primitive.asString.trim()) {
        "0" -> false
        "1" -> true
        else -> null
    }
}

internal fun JsonObject.payloadInt(
    key: String,
    default: Int = 0,
): Int {
    return payloadLong(
        key,
        default.toLong(),
    ).coerceIn(Int.MIN_VALUE.toLong(), Int.MAX_VALUE.toLong()).toInt()
}

internal fun JsonObject.payloadLong(
    key: String,
    default: Long = 0L,
): Long {
    return payloadOptionalLong(key) ?: default
}

internal fun JsonObject.payloadOptionalLong(key: String): Long? {
    if (!has(key) || get(key).isJsonNull || !get(key).isJsonPrimitive) {
        return null
    }
    return get(key).payloadLongOrNull()
}

internal fun JsonObject.payloadBoolean(key: String): Boolean {
    if (!has(key) || get(key).isJsonNull || !get(key).isJsonPrimitive) {
        return false
    }
    val value = get(key).asJsonPrimitive
    return if (value.isBoolean) value.asBoolean else (value.payloadLongOrNull() ?: 0L) != 0L
}

internal fun JsonObject.payloadArray(key: String): JsonArray {
    return if (has(key) && get(key).isJsonArray) getAsJsonArray(key) else JsonArray()
}

internal fun JsonObject.payloadObject(key: String): JsonObject {
    return if (has(key) && get(key).isJsonObject) getAsJsonObject(key) else JsonObject()
}

internal fun JsonObject.payloadIntMap(key: String): Map<String, Int> {
    return payloadLongMap(key).mapValues {
            (_, value) ->
        value.coerceIn(Int.MIN_VALUE.toLong(), Int.MAX_VALUE.toLong()).toInt()
    }
}

internal fun JsonObject.payloadLongMap(key: String): Map<String, Long> {
    if (!has(key) || !get(key).isJsonObject) return emptyMap()
    return getAsJsonObject(key).entrySet().mapNotNull { (name, value) ->
        value.payloadLongOrNull()?.let { parsed -> name to parsed }
    }.toMap()
}

internal fun JsonObject.payloadStringArray(key: String): List<String> {
    return payloadArray(key).mapNotNull { element ->
        element.takeIf { !it.isJsonNull }?.asString?.trim()?.takeIf { value -> value.isNotBlank() }
    }
}

internal fun JsonObject.payloadObjectArray(key: String): List<JsonObject> {
    return payloadArray(key).mapNotNull {
            element ->
        element.takeIf { it.isJsonObject }?.asJsonObject
    }
}

private fun JsonElement.payloadLongOrNull(): Long? {
    if (isJsonNull || !isJsonPrimitive) return null
    val primitive = asJsonPrimitive
    if (primitive.isBoolean) return if (primitive.asBoolean) 1L else 0L
    return runCatching {
        BigDecimal(primitive.asString.trim()).toBigInteger().coerceInLongRange().toLong()
    }.getOrNull()
}

private fun java.math.BigInteger.coerceInLongRange(): java.math.BigInteger {
    val min = java.math.BigInteger.valueOf(Long.MIN_VALUE)
    val max = java.math.BigInteger.valueOf(Long.MAX_VALUE)
    return when {
        this < min -> min
        this > max -> max
        else -> this
    }
}
