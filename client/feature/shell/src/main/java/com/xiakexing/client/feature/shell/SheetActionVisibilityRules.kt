package com.xiakexing.client.feature.shell

internal fun ShellSheetState.shouldKeepSheetAfterAction(): Boolean {
    if (
        dialogSource == "inventory_detail" ||
        dialogSource == SKILL_DETAIL_DIALOG_SOURCE ||
        dialogSource == SKILL_BREAKTHROUGH_DIALOG_SOURCE
    ) {
        return false
    }
    return source == PopupSource.PROTOCOL || panelMode == ShellPanelMode.INFO_LIST
}
