package com.xiakexing.client.core.protocol

object AnsiSanitizer {
    private val ansiRegex = Regex("\\u001B\\[[;\\d]*[ -/]*[@-~]")
    private val carriageReturnRegex = Regex("\\r")

    fun sanitize(text: String): String {
        return text
            .replace(ansiRegex, "")
            .replace(carriageReturnRegex, "")
            .trim()
    }
}
