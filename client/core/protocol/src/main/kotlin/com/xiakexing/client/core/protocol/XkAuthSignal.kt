package com.xiakexing.client.core.protocol

/** Authentication lifecycle signals recognized at the shared protocol boundary. */
enum class XkAuthSignal {
    LOGIN_PROMPT,
    BOOTSTRAP_COMPLETE,
}

/**
 * Classifies only the transport-level LOGN actions needed by the process connection owner.
 * Gameplay payloads remain owned by the shell protocol reducer.
 */
fun IncomingFrame.Packet.authSignalOrNull(): XkAuthSignal? {
    if (envelope.type != XkMessageType.LOGN) {
        return null
    }
    val action =
        envelope.payload
            .get("action")
            ?.takeIf { !it.isJsonNull && it.isJsonPrimitive }
            ?.asString
            ?.trim()
            ?.lowercase()
            .orEmpty()
    return when (action) {
        "", "prompt" -> XkAuthSignal.LOGIN_PROMPT
        "bootstrap_complete" -> XkAuthSignal.BOOTSTRAP_COMPLETE
        else -> null
    }
}
