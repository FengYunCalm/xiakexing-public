package com.xiakexing.client.feature.shell

internal const val OPEN_BANK_ROOT_COMMAND = "ui://bank/root"
internal const val OPEN_BANK_DEPOSIT_COMMAND = "ui://bank/deposit"
internal const val OPEN_BANK_WITHDRAW_COMMAND = "ui://bank/withdraw"

/** 方案 R-29：银行数量 popup（数字键盘 + 快捷金额 chip） */
internal const val BANK_QUANTITY_PREFIX = "ui://bank/quantity/"
internal const val BANK_QUANTITY_INPUT_COMMAND = "ui://bank/quantity-input"

internal fun buildBankQuantityPopup(operation: BankOperation): ShellPopupState {
    val isDeposit = operation == BankOperation.DEPOSIT
    return buildInteractionPopup(
        source = PopupSource.PROTOCOL,
        title = if (isDeposit) "存钱" else "取钱",
        message = "",
        primaryLabel = "",
        inputPlaceholder = "数量",
        inputAction =
            ShellAction(
                if (isDeposit) "存入" else "取出",
                BANK_QUANTITY_INPUT_COMMAND,
                dismissAfterInvoke = false,
            ),
        actions =
            listOf(
                ShellAction("全部", BANK_QUANTITY_PREFIX + "all", dismissAfterInvoke = false),
                ShellAction("100文", BANK_QUANTITY_PREFIX + "100", dismissAfterInvoke = false),
                ShellAction("500文", BANK_QUANTITY_PREFIX + "500", dismissAfterInvoke = false),
                ShellAction("1000文", BANK_QUANTITY_PREFIX + "1000", dismissAfterInvoke = false),
            ),
        kind = ShellPopupKind.QUANTITY_INPUT,
    )
}

internal const val BANK_PANEL_TITLE = "银行"
internal const val BANK_DIALOG_SOURCE = "bank"

internal enum class BankOperation {
    DEPOSIT,
    WITHDRAW,
}

internal fun buildBankRootSheetState(): ShellSheetState {
    return buildInfoListSheet(
        source = PopupSource.PROTOCOL,
        title = BANK_PANEL_TITLE,
        dialogSource = BANK_DIALOG_SOURCE,
        detailLines = listOf("请选择要办理的业务。"),
        actions = bankRootActions(),
        panelStats = listOf("存取", "查账", "领息"),
        panelEmptyText = "暂无可办理业务",
        loading = false,
        navigation = SheetNavigationMeta(routeKey = "bank"),
    )
}

internal fun buildBankAmountSheetState(operation: BankOperation): ShellSheetState {
    val isDeposit = operation == BankOperation.DEPOSIT
    val title = if (isDeposit) "存钱" else "取钱"
    val commandPrefix = if (isDeposit) "bank cun" else "bank qu"
    return buildInfoListSheet(
        source = PopupSource.PROTOCOL,
        title = title,
        dialogSource = BANK_DIALOG_SOURCE,
        detailLines = listOf("请选择金额。"),
        actions =
            listOf(
                ShellAction("全部", "$commandPrefix all", dismissAfterInvoke = false),
                ShellAction("100文", "$commandPrefix 100", dismissAfterInvoke = false),
                ShellAction("500文", "$commandPrefix 500", dismissAfterInvoke = false),
                ShellAction("1000文", "$commandPrefix 1000", dismissAfterInvoke = false),
            ),
        panelStats = listOf(if (isDeposit) "存入" else "取出", "铜钱"),
        panelEmptyText = "暂无可选金额",
        loading = false,
        navigation =
            SheetNavigationMeta(
                routeKey = "bank/${operation.name.lowercase()}",
                depth = 1,
            ),
    )
}

private fun bankRootActions(): List<ShellAction> {
    return listOf(
        ShellAction("存钱", OPEN_BANK_DEPOSIT_COMMAND, dismissAfterInvoke = false),
        ShellAction("取钱", OPEN_BANK_WITHDRAW_COMMAND, dismissAfterInvoke = false),
        ShellAction("查账", "bank check", dismissAfterInvoke = false),
        ShellAction("领息", "bank interest", dismissAfterInvoke = false),
    )
}
