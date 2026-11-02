package com.xiakexing.client.core.network

object LoginPayloadFormatter {
    fun formatTextLogin(
        username: String,
        password: String,
    ): String {
        return "${username.trim()}:${password.trim()}"
    }

    fun formatResumeLogin(resumeToken: String): String {
        return "XKRESM ${resumeToken.trim()}"
    }
}
