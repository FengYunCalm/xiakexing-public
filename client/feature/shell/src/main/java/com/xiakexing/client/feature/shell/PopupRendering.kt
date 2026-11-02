package com.xiakexing.client.feature.shell

private fun buildPopup(
    source: PopupSource,
    title: String,
    message: String = "",
    subtitle: String = "",
    actions: List<ShellAction> = emptyList(),
    meta: List<String> = emptyList(),
    primaryLabel: String = "",
    inputPlaceholder: String = "",
    inputAction: ShellAction? = null,
    kind: ShellPopupKind = ShellPopupKind.INFORMATION,
): ShellPopupState {
    return ShellPopupState(
        source = source,
        title = title,
        subtitle = subtitle,
        message = message,
        actions = actions,
        meta = meta,
        primaryLabel = primaryLabel,
        inputPlaceholder = inputPlaceholder,
        inputAction = inputAction,
        kind = kind,
    )
}

internal fun buildDockPopup(
    title: String,
    message: String = "",
    subtitle: String = "",
    actions: List<ShellAction> = emptyList(),
    meta: List<String> = emptyList(),
    primaryLabel: String = "",
    kind: ShellPopupKind = ShellPopupKind.INFORMATION,
): ShellPopupState {
    return buildPopup(
        source = PopupSource.DOCK_BUTTON,
        title = title,
        message = message,
        subtitle = subtitle,
        actions = actions,
        meta = meta,
        primaryLabel = primaryLabel,
        kind = kind,
    )
}

internal fun buildProtocolPopup(
    title: String,
    message: String = "",
    subtitle: String = "",
    actions: List<ShellAction> = emptyList(),
    meta: List<String> = emptyList(),
    primaryLabel: String = "",
    kind: ShellPopupKind = ShellPopupKind.INFORMATION,
): ShellPopupState {
    return buildPopup(
        source = PopupSource.PROTOCOL,
        title = title,
        message = message,
        subtitle = subtitle,
        actions = actions,
        meta = meta,
        primaryLabel = primaryLabel,
        kind = kind,
    )
}

internal fun buildInteractionPopup(
    source: PopupSource,
    title: String,
    message: String = "",
    subtitle: String = "",
    actions: List<ShellAction> = emptyList(),
    meta: List<String> = emptyList(),
    primaryLabel: String = "",
    inputPlaceholder: String = "",
    inputAction: ShellAction? = null,
    kind: ShellPopupKind = ShellPopupKind.INFORMATION,
): ShellPopupState {
    return buildPopup(
        source = source,
        title = title,
        message = message,
        subtitle = subtitle,
        actions = actions,
        meta = meta,
        primaryLabel = primaryLabel,
        inputPlaceholder = inputPlaceholder,
        inputAction = inputAction,
        kind = kind,
    )
}
