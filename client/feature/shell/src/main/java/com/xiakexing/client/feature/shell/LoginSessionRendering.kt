package com.xiakexing.client.feature.shell

import com.xiakexing.client.core.protocol.payload.LoginPrompt
import com.xiakexing.client.core.protocol.payload.PlayerStatusSnapshot
import com.xiakexing.client.core.protocol.payload.PromptPacket

internal const val LOGIN_ACTION_PROMPT = "prompt"
internal const val LOGIN_ACTION_BOOTSTRAP_COMPLETE = "bootstrap_complete"

internal fun LoginPrompt.normalizedAction(): String = action.trim().lowercase()

internal fun LoginPrompt.requestsCredentials(): Boolean {
    return normalizedAction() in setOf("", LOGIN_ACTION_PROMPT)
}

internal fun String.matchesCurrentPlayer(
    snapshot: PlayerStatusSnapshot,
    username: String,
): Boolean {
    val target = trim()
    if (target.isBlank()) {
        return false
    }
    return target == "你" ||
        target == snapshot.name.trim() ||
        target == username.trim()
}

internal fun String.toLoginFailureMessageOrNull(): String? {
    val message = normalizeInteractionLine() ?: trim()
    if (message.isBlank()) {
        return null
    }
    val failureKeywords = listOf("账号或密码错误", "密码错误", "登录失败", "进入游戏失败", "连接切换失败", "当前连接已被限制登录")
    return message.takeIf { text -> failureKeywords.any { keyword -> text.contains(keyword) } }
}

internal fun String.isLoginSuccessStatusLine(): Boolean {
    val normalized = normalizeInteractionLine()?.trim().orEmpty().ifBlank { trim() }
    if (normalized.isBlank()) {
        return false
    }
    val compact =
        normalized.filterNot { char ->
            char.isWhitespace() || char in "[]【】（）()。！？!，,：:；;·"
        }
    return compact.contains("登录成功") || compact.contains("已进入江湖")
}

internal fun String.isPromptOnlyLine(): Boolean {
    val normalized = trim()
    return normalized == ">" || normalized.matches(LOGIN_PROMPT_REGEX)
}

private val LOGIN_PROMPT_REGEX = Regex("^[^\\s]{0,12}>$")

internal fun ShellUiState.isWaitingForLoginResult(): Boolean {
    return appScene != ShellScene.REGISTER &&
        session.phase in setOf(ShellPhase.AUTHENTICATING, ShellPhase.WAITING_WORLD) &&
        session.worldPackets.isEmpty()
}

internal fun ShellUiState.shouldStopAutomaticLoginRetry(): Boolean {
    if (loginErrorMessage.isNotBlank()) {
        return true
    }
    return appScene != ShellScene.REGISTER &&
        session.phase in setOf(ShellPhase.AUTHENTICATING, ShellPhase.WAITING_WORLD, ShellPhase.FAILED) &&
        session.worldPackets.isEmpty()
}

internal fun PromptPacket.isLoginPromptLike(): Boolean {
    val normalizedPrompt = prompt.trim().lowercase()
    if (normalizedPrompt.isBlank()) {
        return false
    }
    return normalizedPrompt.contains("login") ||
        normalizedPrompt.contains("账号") ||
        normalizedPrompt.contains("密码")
}
