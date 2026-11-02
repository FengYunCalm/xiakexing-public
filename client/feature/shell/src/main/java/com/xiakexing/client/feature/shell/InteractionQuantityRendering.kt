package com.xiakexing.client.feature.shell

internal const val GIVE_QUANTITY_PREFIX = "ui://interaction/give-quantity/"
internal const val GIVE_QUANTITY_INPUT_COMMAND = "ui://interaction/give-quantity-input"
internal const val GIVE_QUANTITY_CONFIRM_ALL_COMMAND = "ui://interaction/give-confirm-all"
internal const val LEARN_QUANTITY_PREFIX = "ui://interaction/learn-quantity/"

internal fun buildGiveQuantityPopup(
    itemName: String,
    availableQuantity: Int,
): ShellPopupState {
    return buildInteractionPopup(
        source = PopupSource.INTERACTION_GIVE,
        title = "给予数量",
        subtitle = itemName,
        // 第六轮 §2.3：去掉「给 xxx · 最多 N 个」提示行与输入框占位文案（用户规格）。
        actions =
            listOf(
                ShellAction("给1个", GIVE_QUANTITY_PREFIX + "1", dismissAfterInvoke = false),
                ShellAction("给10个", GIVE_QUANTITY_PREFIX + "10", dismissAfterInvoke = false),
                ShellAction(
                    "全部给",
                    GIVE_QUANTITY_PREFIX + availableQuantity,
                    dismissAfterInvoke = false,
                ),
            ),
        primaryLabel = "",
        inputPlaceholder = "",
        inputAction = ShellAction("给予", GIVE_QUANTITY_INPUT_COMMAND, dismissAfterInvoke = false),
        kind = ShellPopupKind.QUANTITY_INPUT,
    )
}

internal fun buildInvalidGiveQuantityPopup(): ShellPopupState {
    return buildInteractionPopup(
        source = PopupSource.INTERACTION_GIVE,
        title = "数量无效",
        message = "数量需大于 0",
        primaryLabel = "",
        inputPlaceholder = "",
        inputAction = ShellAction("给予", GIVE_QUANTITY_INPUT_COMMAND, dismissAfterInvoke = false),
        kind = ShellPopupKind.QUANTITY_INPUT,
    )
}

internal fun buildGiveAllConfirmationPopup(availableQuantity: Int): ShellPopupState {
    return buildInteractionPopup(
        source = PopupSource.INTERACTION_GIVE,
        title = "数量不足",
        message = "最多 $availableQuantity 个，是否全部给？",
        actions =
            listOf(
                ShellAction("全部给", GIVE_QUANTITY_CONFIRM_ALL_COMMAND, dismissAfterInvoke = false),
            ),
        primaryLabel = "",
        kind = ShellPopupKind.CONFIRMATION,
    )
}

internal fun buildLearnQuantityPopup(
    skillName: String,
    targetName: String,
): ShellPopupState {
    return buildInteractionPopup(
        source = PopupSource.INTERACTION_LEARN,
        title = "学习等级",
        subtitle = skillName,
        message = "向 $targetName 学习 · 输入级数",
        actions =
            listOf(
                ShellAction("学1级", LEARN_QUANTITY_PREFIX + "1", dismissAfterInvoke = false),
                ShellAction("学10级", LEARN_QUANTITY_PREFIX + "10", dismissAfterInvoke = false),
                ShellAction("学100级", LEARN_QUANTITY_PREFIX + "100", dismissAfterInvoke = false),
                ShellAction("全部学", LEARN_QUANTITY_PREFIX + "all", dismissAfterInvoke = false),
            ),
        primaryLabel = "",
        kind = ShellPopupKind.QUANTITY_INPUT,
    )
}

internal fun buildInvalidLearnQuantityPopup(): ShellPopupState {
    return buildInteractionPopup(
        source = PopupSource.INTERACTION_LEARN,
        title = "数量无效",
        message = "请输入要学习的级数",
        primaryLabel = "",
        kind = ShellPopupKind.QUANTITY_INPUT,
    )
}
