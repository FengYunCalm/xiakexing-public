package com.xiakexing.client.core.protocol.payload

import com.google.gson.JsonObject

data class LoginPrompt(
    val action: String,
) {
    companion object {
        fun fromJson(payload: JsonObject): LoginPrompt {
            return LoginPrompt(action = payload.string("action"))
        }
    }
}

data class RegisterPrompt(
    val stage: String,
    val message: String,
) {
    companion object {
        fun fromJson(payload: JsonObject): RegisterPrompt {
            return RegisterPrompt(
                stage = payload.string("stage"),
                message = payload.string("message"),
            )
        }
    }
}

private fun JsonObject.string(key: String): String {
    return if (has(key) && !get(key).isJsonNull) get(key).asString else ""
}
