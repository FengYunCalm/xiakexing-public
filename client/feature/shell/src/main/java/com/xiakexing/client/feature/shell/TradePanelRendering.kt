package com.xiakexing.client.feature.shell

import com.xiakexing.client.core.protocol.payload.InventoryItemSnapshot
import com.xiakexing.client.core.protocol.payload.TradeOfferItemPacket
import com.xiakexing.client.core.protocol.payload.TradeResultPacket
import com.xiakexing.client.core.protocol.payload.TradeSessionPacket

internal const val TRADE_MONEY_INPUT_COMMAND = "ui://trade/money-input"
internal const val TRADE_MONEY_SUBMIT_COMMAND = "ui://trade/money-submit"
internal const val TRADE_MONEY_PREFIX = "ui://trade/money/"

internal fun buildTradeMoneyPopup(): ShellPopupState {
    return buildInteractionPopup(
        source = PopupSource.PROTOCOL,
        title = "现金报价",
        message = "输入要加入本次交易的铜钱数。",
        primaryLabel = "",
        inputPlaceholder = "金额（文）",
        inputAction =
            ShellAction(
                label = "提交报价",
                command = TRADE_MONEY_SUBMIT_COMMAND,
                dismissAfterInvoke = false,
            ),
        actions =
            listOf(
                ShellAction("100文", TRADE_MONEY_PREFIX + "100", dismissAfterInvoke = false),
                ShellAction("500文", TRADE_MONEY_PREFIX + "500", dismissAfterInvoke = false),
                ShellAction("1000文", TRADE_MONEY_PREFIX + "1000", dismissAfterInvoke = false),
                ShellAction("清空", TRADE_MONEY_PREFIX + "0", dismissAfterInvoke = false),
            ),
        kind = ShellPopupKind.QUANTITY_INPUT,
    )
}

internal fun buildTradeSessionSheetState(
    packet: TradeSessionPacket,
    inventoryItems: List<InventoryItemSnapshot>,
): ShellSheetState {
    val active = packet.status == "active"
    val terminal = packet.isTerminalTradeSession()
    val items =
        buildList {
            packet.selfOffer.items.forEach { item ->
                add(item.toTradeSheetItem("我方报价", active, remove = true))
            }
            add(
                ShellSheetItem(
                    title = "我方现金",
                    trailing = formatTradeMoney(packet.selfOffer.money),
                    subtitle = if (packet.selfOffer.confirmed) "已确认" else "待确认",
                    role = ShellSheetItemRole.INFO,
                    section = "我方报价",
                    selected = packet.selfOffer.confirmed,
                ),
            )
            packet.counterpartyOffer.items.forEach { item ->
                add(item.toTradeSheetItem("对方报价", active = false, remove = false))
            }
            add(
                ShellSheetItem(
                    title = "对方现金",
                    trailing = formatTradeMoney(packet.counterpartyOffer.money),
                    subtitle = if (packet.counterpartyOffer.confirmed) "已确认" else "待确认",
                    role = ShellSheetItemRole.INFO,
                    section = "对方报价",
                    selected = packet.counterpartyOffer.confirmed,
                ),
            )
            if (active) {
                val offeredKeys =
                    packet.selfOffer.items.flatMap { item ->
                        listOf(item.id, item.name, item.templateId)
                    }.filter { it.isNotBlank() }.toSet()
                val addableItems =
                    inventoryItems.filter { item ->
                        item.canGive && item.location != "room" &&
                            item.name.ifBlank { item.id }.isNotBlank() &&
                            listOf(item.id, item.name, item.templateId).none { it in offeredKeys }
                    }
                addableItems.forEach { item ->
                    val target = item.name.ifBlank { item.id }
                    add(
                        ShellSheetItem(
                            title = target,
                            subtitle = item.typeLabel,
                            trailing = if (item.quantity > 1) "${item.quantity}件" else "加入",
                            command = "trade add $target",
                            role = ShellSheetItemRole.OPTION,
                            section = "可加入报价",
                        ),
                    )
                }
                if (addableItems.isEmpty()) {
                    add(
                        ShellSheetItem(
                            title = "暂无可加入的背包物品",
                            subtitle = "先打开背包刷新，或使用命令加入指定物品。",
                            role = ShellSheetItemRole.INFO,
                            section = "可加入报价",
                        ),
                    )
                }
            }
        }
    val actions = buildTradeActions(packet, terminal)
    val detailLines =
        buildList {
            add("状态：${tradeStatusLabel(packet.status)}")
            if (packet.counterparty.isNotBlank()) add("对方：${packet.counterparty}")
            if (packet.revision > 0) add("报价版本：${packet.revision}")
            if (!terminal && packet.expiresAt > 0) {
                val remaining =
                    (packet.expiresAt - System.currentTimeMillis() / 1000L).coerceAtLeast(
                        0L,
                    )
                add("剩余：${remaining}秒")
            }
            if (packet.terminalResult.isNotBlank()) add(packet.terminalResult)
        }
    return buildInfoListSheet(
        source = PopupSource.PROTOCOL,
        title = "交易${packet.counterparty.takeIf { it.isNotBlank() }?.let { "·$it" }.orEmpty()}",
        subtitle = if (terminal) "本次会话已结束" else "报价工作台",
        dialogSource = "trade",
        detailLines = detailLines,
        items = items,
        actions = actions,
        panelStats =
            listOf(
                "我方",
                formatTradeMoney(packet.selfOffer.money),
                "对方",
                formatTradeMoney(packet.counterpartyOffer.money),
            ),
        panelEmptyText = "暂无交易内容",
        emptyPresentation = ShellSheetEmptyPresentation.MESSAGE,
        loading = false,
        navigation = SheetNavigationMeta(routeKey = "trade/${packet.sessionId}"),
    )
}

internal fun ShellSheetState.isTradeSessionSheet(): Boolean {
    return dialogSource == "trade" && navigation.routeKey.startsWith("trade/")
}

internal fun TradeSessionPacket.isTerminalTradeSession(): Boolean {
    return terminalResult.isNotBlank() ||
        status in setOf("completed", "failed", "cancelled", "rejected", "expired")
}

private fun buildTradeActions(
    packet: TradeSessionPacket,
    terminal: Boolean,
): List<ShellAction> {
    if (terminal) return emptyList()
    return packet.availableActions.mapNotNull { action ->
        when (action.trim().lowercase()) {
            "accept" -> ShellAction("接受交易", "trade accept", dismissAfterInvoke = false)
            "reject" ->
                ShellAction(
                    "拒绝交易",
                    "trade reject",
                    dismissAfterInvoke = false,
                    role = "danger",
                )
            "money" -> ShellAction("设置现金", TRADE_MONEY_INPUT_COMMAND, dismissAfterInvoke = false)
            "unmoney" -> ShellAction("清空现金", "trade unmoney", dismissAfterInvoke = false)
            "confirm" ->
                ShellAction(
                    "确认报价",
                    "trade confirm",
                    dismissAfterInvoke = false,
                    role = "primary",
                )
            "cancel" ->
                ShellAction(
                    "取消交易",
                    "trade cancel",
                    dismissAfterInvoke = false,
                    role = "danger",
                )
            else -> null
        }
    }.distinctBy { it.command }
}

private fun TradeOfferItemPacket.toTradeSheetItem(
    section: String,
    active: Boolean,
    remove: Boolean,
): ShellSheetItem {
    val target = name.ifBlank { templateId.ifBlank { id } }
    return ShellSheetItem(
        title = target,
        subtitle = if (quantity > 1) "数量 $quantity" else "",
        trailing = if (active && remove) "移除" else "",
        command = if (active && remove) "trade remove $target" else "",
        role = if (active && remove) ShellSheetItemRole.ACTION else ShellSheetItemRole.INFO,
        section = section,
    )
}

private fun formatTradeMoney(amount: Int): String {
    return formatMoneyAmount(amount).ifBlank { "无" }
}

internal fun tradeStatusLabel(status: String): String {
    return when (status.trim().lowercase()) {
        "requested" -> "待接受"
        "active" -> "进行中"
        "completed" -> "已完成"
        "failed" -> "未完成"
        "cancelled" -> "已取消"
        "rejected" -> "已拒绝"
        "expired" -> "已过期"
        else -> status.ifBlank { "未知" }
    }
}

internal fun TradeResultPacket.toTradeFeedbackLine(message: String): String {
    return "交易：$message"
}

internal fun TradeResultPacket.successBannerTitle(): String {
    return when (action.trim().lowercase()) {
        "buy" -> "购买成功"
        "sell" -> "出售成功"
        "heal" -> "治疗完成"
        "repair" -> "修理完成"
        "forge" -> "强化完成"
        else -> "交易成功"
    }
}

internal fun TradeResultPacket.failureBannerTitle(): String {
    return when (action.trim().lowercase()) {
        "buy" -> "购买失败"
        "sell" -> "出售失败"
        "heal" -> "治疗失败"
        "repair" -> "修理失败"
        "forge" -> "强化失败"
        else -> "交易失败"
    }
}

internal fun TradeResultPacket.defaultTradeFeedback(): String {
    val resolvedItem = item.ifBlank { "物品" }
    val priceLabel = this.priceLabel.ifBlank { formatMoneyAmount(price) }
    val balanceLabel =
        this.balanceLabel.ifBlank {
            formatMoneyAmount(balance).takeIf { it.isNotBlank() }?.let { "余额 $it" }.orEmpty()
        }
    val enhanceLabel =
        enhanceLevel.takeIf { it > 0 }?.let {
                level ->
            "$resolvedItem +$level"
        }.orEmpty()
    val actionLabel =
        when (action.trim().lowercase()) {
            "buy" -> if (success) "购入$resolvedItem" else "未能购入$resolvedItem"
            "sell" -> if (success) "卖出$resolvedItem" else "未能卖出$resolvedItem"
            "heal" -> if (success) "${resolvedItem}完成" else "${resolvedItem}失败"
            "repair" -> if (success) "${resolvedItem}修理完成" else "${resolvedItem}修理失败"
            "forge" -> if (success) enhanceLabel.ifBlank { "${resolvedItem}强化完成" } else "${resolvedItem}强化失败"
            else -> if (success) "${resolvedItem}处理成功" else "${resolvedItem}处理失败"
        }
    return listOf(
        actionLabel,
        priceLabel.takeIf {
            it.isNotBlank()
        },
        balanceLabel.takeIf { it.isNotBlank() },
    )
        .joinToString("，")
}
