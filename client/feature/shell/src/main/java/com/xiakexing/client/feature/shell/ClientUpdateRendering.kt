package com.xiakexing.client.feature.shell

import com.xiakexing.client.feature.shell.update.ClientUpdateDownloadProgress
import com.xiakexing.client.feature.shell.update.ClientUpdateManifest
import com.xiakexing.client.feature.shell.update.UPDATE_DOWNLOAD_COMMAND

private fun buildUpdatePopup(
    title: String,
    message: String,
    actions: List<ShellAction> = emptyList(),
    primaryLabel: String = "",
    kind: ShellPopupKind = ShellPopupKind.INFORMATION,
    progress: ShellPopupProgress? = null,
): ShellPopupState {
    return buildDockPopup(
        title = title,
        message = message,
        actions = actions,
        meta = listOf(CLIENT_UPDATE_POPUP_META),
        primaryLabel = primaryLabel,
        kind = kind,
    ).copy(progress = progress)
}

internal const val CLIENT_UPDATE_POPUP_META = "kind=client-update"

internal fun buildUpdateCheckingPopup(): ShellPopupState {
    return buildUpdatePopup(
        title = "检查更新",
        message = "正在检查更新…",
        kind = ShellPopupKind.BLOCKING_PROGRESS,
    )
}

internal fun buildUpdateAvailablePopup(
    manifest: ClientUpdateManifest,
    required: Boolean = manifest.force,
): ShellPopupState {
    // 清单按最新发布优先排列；更新弹窗只呈现本次最新日志，避免把历史版本混入决策面。
    val notes = manifest.notes.firstOrNull { it.isNotBlank() }
    val message = notes?.let { note -> "• $note" } ?: "本次更新优化了游戏体验。"
    return buildUpdatePopup(
        title = manifest.title.ifBlank { "发现新版本" },
        message = message,
        actions =
            listOf(
                ShellAction("下载更新", UPDATE_DOWNLOAD_COMMAND, dismissAfterInvoke = false),
            ),
        primaryLabel = if (required) "" else "稍后",
        kind = if (required) ShellPopupKind.UPDATE_REQUIRED else ShellPopupKind.UPDATE_OPTIONAL,
    )
}

internal fun buildNoUpdatePopup(
    @Suppress("UNUSED_PARAMETER") currentVersionName: String,
): ShellPopupState {
    return buildUpdatePopup(
        title = "已是最新版本",
        message = "当前已是最新版本。",
        primaryLabel = "知道了",
    )
}

internal fun buildUpdateDownloadingPopup(progress: ClientUpdateDownloadProgress): ShellPopupState {
    return buildUpdatePopup(
        title = "下载更新",
        message = "",
        kind = ShellPopupKind.BLOCKING_PROGRESS,
        progress = ShellPopupProgress(progress.fraction),
    )
}

internal fun buildUpdateInstallPermissionPopup(required: Boolean = false): ShellPopupState {
    return buildUpdatePopup(
        title = "允许安装更新",
        message = "请允许安装更新后，再点一次下载。",
        actions = listOf(ShellAction("重新下载", UPDATE_DOWNLOAD_COMMAND, dismissAfterInvoke = false)),
        primaryLabel = if (required) "" else "稍后",
        kind = if (required) ShellPopupKind.UPDATE_REQUIRED else ShellPopupKind.UPDATE_OPTIONAL,
    )
}

internal fun buildUpdateErrorPopup(
    message: String,
    required: Boolean = false,
): ShellPopupState {
    return buildUpdatePopup(
        title = "更新失败",
        message = message,
        actions = listOf(ShellAction("重新检查", UPDATE_CHECK_COMMAND, dismissAfterInvoke = false)),
        primaryLabel = if (required) "" else "关闭",
        kind = if (required) ShellPopupKind.UPDATE_REQUIRED else ShellPopupKind.BLOCKING_ERROR,
    )
}
