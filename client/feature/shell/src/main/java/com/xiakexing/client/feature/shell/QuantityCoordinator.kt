package com.xiakexing.client.feature.shell

internal class QuantityCoordinator(
    private val sink: Sink,
) {
    interface Sink {
        fun currentState(): ShellUiState

        fun prepareInteractionSheetCommand(command: String): Boolean

        fun dismissPopup()

        fun dismissSheet()

        fun sendDirectCommand(
            command: String,
            suppressImmediateScroll: Boolean,
        )

        fun updateState(transform: ShellUiState.() -> ShellUiState)
    }

    // give/learn pending 与 bank pending 同属本 owner（互斥：同一时刻只有一个数量 popup）。
    private var pendingGiveQuantity: PendingGiveQuantityRequest? = null
    private var pendingLearnQuantity: PendingLearnQuantityRequest? = null
    private var pendingTradeMoney: Boolean = false

    fun clear() {
        pendingGiveQuantity = null
        pendingLearnQuantity = null
        pendingTradeMoney = false
        pendingBankOperation = null
    }

    fun showBankRootSheet() {
        sink.updateState {
            copy(
                sheet = buildBankRootSheetState(),
                dialog = null,
            )
        }
    }

    fun showBankAmountSheet(operation: BankOperation) {
        sink.updateState {
            copy(
                sheet = buildBankAmountSheetState(operation),
                dialog = null,
            )
        }
    }

    // ===== 方案 R-29：存/取 → 数量 popup（数字键盘 IME + 快捷金额 chip）=====

    private var pendingBankOperation: BankOperation? = null

    fun showBankQuantityPopup(operation: BankOperation) {
        pendingBankOperation = operation
        sink.updateState {
            copy(
                dialog = buildBankQuantityPopup(operation).withParentPopup(dialog),
                sheet = null,
            )
        }
    }

    fun submitPendingBankQuantity(amount: String) {
        val operation = pendingBankOperation ?: return
        val normalized = amount.trim()
        val commandAmount =
            when {
                normalized == "all" || normalized == "全部" -> "all"
                normalized.toIntOrNull()?.takeIf { it > 0 } != null -> normalized
                else -> return
            }
        pendingBankOperation = null
        sink.dismissPopup()
        sink.sendDirectCommand(
            if (operation == BankOperation.DEPOSIT) {
                "bank cun $commandAmount"
            } else {
                "bank qu $commandAmount"
            },
            suppressImmediateScroll = false,
        )
    }

    fun handlePopupInputChanged(value: String) {
        sink.updateState {
            copy(dialog = dialog?.copy(inputValue = value.filter { it.isDigit() }.take(5)))
        }
    }

    fun showTradeMoneyPopup() {
        pendingTradeMoney = true
        sink.updateState {
            copy(dialog = buildTradeMoneyPopup().withParentPopup(dialog))
        }
    }

    fun submitPendingTradeMoney(amount: String) {
        if (!pendingTradeMoney) return
        val normalized = amount.trim()
        val value = normalized.toLongOrNull()?.takeIf { it >= 0L } ?: return
        pendingTradeMoney = false
        sink.dismissPopup()
        sink.sendDirectCommand(
            "trade money $value",
            suppressImmediateScroll = true,
        )
    }

    fun showGiveQuantityPopup(command: String) {
        val popup = prepareGiveQuantityPopup(command, sink.currentState()) ?: return
        sink.updateState {
            copy(dialog = popup.withParentPopup(dialog))
        }
    }

    fun submitPendingGiveQuantity(amount: Int) {
        applyQuantityCommandPlan(submitGiveQuantity(amount))
    }

    fun submitPendingGiveAll() {
        applyQuantityCommandPlan(submitGiveAll())
    }

    fun showLearnQuantityPopup(command: String) {
        val popup = prepareLearnQuantityPopup(command, sink.currentState()) ?: return
        sink.updateState {
            copy(dialog = popup.withParentPopup(dialog))
        }
    }

    fun submitPendingLearnQuantity(levels: String) {
        applyQuantityCommandPlan(submitLearnQuantity(levels))
    }

    // ===== give/learn pending 状态机（并入本 owner） =====

    private fun prepareGiveQuantityPopup(
        command: String,
        state: ShellUiState,
    ): ShellPopupState? {
        val request = buildPendingGiveQuantityRequest(command, state) ?: return null
        pendingGiveQuantity = request
        return buildGiveQuantityPopup(request.itemName, request.availableQuantity)
    }

    private fun submitGiveQuantity(amount: Int): QuantityCommandPlan {
        val request = pendingGiveQuantity ?: return QuantityCommandPlan.None
        if (amount <= 0) {
            return QuantityCommandPlan.ShowPopup(buildInvalidGiveQuantityPopup())
        }
        if (amount > request.availableQuantity) {
            return QuantityCommandPlan.ShowPopup(
                buildGiveAllConfirmationPopup(request.availableQuantity),
            )
        }
        return buildGiveCommandPlan(request, amount)
    }

    private fun submitGiveAll(): QuantityCommandPlan {
        val request = pendingGiveQuantity ?: return QuantityCommandPlan.None
        return buildGiveCommandPlan(request, request.availableQuantity)
    }

    private fun prepareLearnQuantityPopup(
        command: String,
        state: ShellUiState,
    ): ShellPopupState? {
        val request = buildPendingLearnQuantityRequest(command, state) ?: return null
        pendingLearnQuantity = request
        return buildLearnQuantityPopup(request.skillName, request.targetName)
    }

    private fun submitLearnQuantity(levels: String): QuantityCommandPlan {
        val request = pendingLearnQuantity ?: return QuantityCommandPlan.None
        val normalizedLevels = levels.trim().lowercase()
        val levelCount = normalizedLevels.toIntOrNull()
        val commandLevels =
            when {
                normalizedLevels == "all" || normalizedLevels == "全部" -> "all"
                levelCount != null && levelCount > 0 -> levelCount.toString()
                else -> return QuantityCommandPlan.ShowPopup(buildInvalidLearnQuantityPopup())
            }
        return buildLearnCommandPlan(request, commandLevels)
    }

    private fun buildPendingGiveQuantityRequest(
        command: String,
        state: ShellUiState,
    ): PendingGiveQuantityRequest? {
        val match =
            Regex("^give\\s+(.+)\\s+to\\s+(.+)$", RegexOption.IGNORE_CASE)
                .find(command.trim())
                ?: return null
        val itemTarget = match.groupValues[1].trim()
        val targetId = match.groupValues[2].trim()
        val item =
            state.inventoryItems.firstOrNull { snapshot ->
                snapshot.id == itemTarget || snapshot.name == itemTarget
            } ?: return null
        val targetName = state.sheet?.subtitle?.takeIf { it.isNotBlank() } ?: targetId
        return PendingGiveQuantityRequest(
            itemTarget = item.id.ifBlank { item.name },
            itemName = item.displayTitle(),
            itemDisplayName = item.name.ifBlank { item.id },
            targetId = targetId,
            targetName = targetName,
            availableQuantity = item.quantity.coerceAtLeast(1),
            isCurrency = item.type == "currency" || item.id.startsWith("item_coin_"),
        )
    }

    private fun buildGiveCommandPlan(
        request: PendingGiveQuantityRequest,
        amount: Int,
    ): QuantityCommandPlan.SendCommand {
        val itemPart =
            if (request.isCurrency) {
                "$amount${request.itemDisplayName}"
            } else {
                "$amount ${request.itemTarget}"
            }
        pendingGiveQuantity = null
        // 第五轮 R1：给予后关闭弹窗回主文本区——反馈走 msg_send 直达主区，
        // 不再进 interaction capture 吸收进 NPC 弹窗；GIVE 任务步同命令路径受益。
        return QuantityCommandPlan.SendCommand(
            command = "give $itemPart to ${request.targetId}",
            prepareInteractionSheet = false,
            dismissSheet = true,
        )
    }

    private fun buildPendingLearnQuantityRequest(
        command: String,
        state: ShellUiState,
    ): PendingLearnQuantityRequest? {
        val normalizedCommand = command.trim()
        val match =
            Regex("^learn\\s+(.+)\\s+from\\s+(.+)$", RegexOption.IGNORE_CASE)
                .find(normalizedCommand)
                ?: return null
        val skillId = match.groupValues[1].trim()
        val targetId = match.groupValues[2].trim()
        if (skillId.isBlank() || targetId.isBlank() || skillId.equals("list", ignoreCase = true)) {
            return null
        }
        val currentSheet = state.sheet
        val skillName =
            currentSheet?.panelAllCards?.ifEmpty { currentSheet.panelCards }
                ?.firstOrNull {
                        card ->
                    card.command.trim().equals(normalizedCommand, ignoreCase = true)
                }
                ?.title
                ?.takeIf { it.isNotBlank() }
                ?: skillId
        val targetName = currentSheet?.subtitle?.takeIf { it.isNotBlank() } ?: targetId
        return PendingLearnQuantityRequest(
            skillId = skillId,
            skillName = skillName,
            targetId = targetId,
            targetName = targetName,
        )
    }

    private fun buildLearnCommandPlan(
        request: PendingLearnQuantityRequest,
        levels: String,
    ): QuantityCommandPlan.SendCommand {
        pendingLearnQuantity = null
        return QuantityCommandPlan.SendCommand(
            command = "learn ${request.skillId} from ${request.targetId} $levels",
            dismissSheet = true,
        )
    }

    private fun applyQuantityCommandPlan(plan: QuantityCommandPlan) {
        when (plan) {
            QuantityCommandPlan.None -> Unit
            is QuantityCommandPlan.ShowPopup ->
                sink.updateState {
                    copy(dialog = plan.popup.withParentPopup(dialog))
                }
            is QuantityCommandPlan.SendCommand -> {
                sink.dismissPopup()
                if (plan.dismissSheet) {
                    sink.dismissSheet()
                }
                val suppressImmediateScroll =
                    if (plan.prepareInteractionSheet) {
                        sink.prepareInteractionSheetCommand(plan.command)
                    } else {
                        plan.suppressImmediateScroll
                    }
                sink.sendDirectCommand(
                    plan.command,
                    suppressImmediateScroll = suppressImmediateScroll,
                )
            }
        }
    }
}

internal sealed interface QuantityCommandPlan {
    data object None : QuantityCommandPlan

    data class ShowPopup(
        val popup: ShellPopupState,
    ) : QuantityCommandPlan

    data class SendCommand(
        val command: String,
        val suppressImmediateScroll: Boolean = false,
        val prepareInteractionSheet: Boolean = false,
        val dismissSheet: Boolean = false,
    ) : QuantityCommandPlan
}

private data class PendingGiveQuantityRequest(
    val itemTarget: String,
    val itemName: String,
    val itemDisplayName: String,
    val targetId: String,
    val targetName: String,
    val availableQuantity: Int,
    val isCurrency: Boolean,
)

private data class PendingLearnQuantityRequest(
    val skillId: String,
    val skillName: String,
    val targetId: String,
    val targetName: String,
)
