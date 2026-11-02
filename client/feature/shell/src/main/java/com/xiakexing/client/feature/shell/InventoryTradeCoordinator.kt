package com.xiakexing.client.feature.shell

import com.google.gson.JsonObject
import com.xiakexing.client.core.protocol.payload.DialogPacket
import com.xiakexing.client.core.protocol.payload.InventoryItemSnapshot
import com.xiakexing.client.core.protocol.payload.InventorySummary
import com.xiakexing.client.core.protocol.payload.ShopPacket
import com.xiakexing.client.core.protocol.payload.TradeResultPacket
import com.xiakexing.client.core.protocol.payload.TradeSessionPacket

internal class InventoryTradeCoordinator(
    private val interactionSessionCoordinator: InteractionSessionCoordinator,
    private val sink: Sink,
) {
    interface Sink {
        fun currentState(): ShellUiState

        fun flushPendingRoomRefresh()

        fun clearPendingInteractionRequest()

        fun clearPendingProtocolSheetRequest()

        fun prepareProtocolSheetCommand(
            currentSheet: ShellSheetState?,
            command: String,
        )

        fun sendDirectCommand(
            command: String,
            suppressImmediateScroll: Boolean,
        )

        fun scheduleInteractionRequestSettle()

        fun appendTranscript(line: String)

        fun updateState(transform: ShellUiState.() -> ShellUiState)
    }

    private var pendingInventoryInspectItem: InventoryItemDisplay? = null
    private var pendingInventoryInspectSnapshot: InventoryItemSnapshot? = null
    private var currentTradeSession: TradeSessionPacket? = null

    fun clearPendingInventoryInspectItem() {
        pendingInventoryInspectItem = null
        pendingInventoryInspectSnapshot = null
    }

    fun handleShopPacket(payload: JsonObject) {
        handleShopPacket(ShopPacket.fromJson(payload))
    }

    fun handleShopPacket(packet: ShopPacket) {
        val pending = interactionSessionCoordinator.pendingInteractionRequest()
        val state = sink.currentState()
        val currentSheet = state.sheet
        val pendingTarget =
            resolveInteractionTarget(pending, currentSheet, state.interactionObjects)
        val shouldStayInline = shouldStayInInteractionSheet(pending, currentSheet)
        val pendingSheetLines =
            interactionSessionCoordinator.pendingProtocolSheetRequest()
                ?.takeIf { currentSheet != null && currentSheet.sheetIdentity() == it.sheetIdentity }
                ?.lines
                ?.toList()
                .orEmpty()
        sink.clearPendingProtocolSheetRequest()
        val shopCards =
            packet.items.map { item ->
                ShellPanelCard(
                    id = item.id.ifBlank { item.name },
                    title = item.name.ifBlank { item.id },
                    bottomLabel = item.priceLabel.ifBlank { formatMoneyAmount(item.price) },
                    command = "buy ${item.name.ifBlank { item.id }}",
                )
            }
        sink.updateState {
            copy(
                sheet =
                    buildShopSheetState(
                        source = if (shouldStayInline) PopupSource.INTERACTION_OBJECT else PopupSource.PROTOCOL,
                        title = packet.shopName.ifBlank { pendingTarget?.title ?: "商铺" },
                        detailLines =
                            if (shouldStayInline) {
                                pending?.descriptionLines.orEmpty()
                            } else {
                                pendingSheetLines
                            },
                        interactionLines = pending?.interactionLines.orEmpty(),
                        shopCards = shopCards,
                        interactionTargetId = pendingTarget?.id.orEmpty(),
                        interactionTargetUiKey = pendingTarget?.uiKey.orEmpty(),
                        interactionTargetKind = pendingTarget?.kind,
                    ),
                dialog = null,
            )
        }
        if (shouldStayInline) {
            sink.scheduleInteractionRequestSettle()
        } else {
            sink.clearPendingInteractionRequest()
        }
    }

    fun handleTradeResultPacket(payload: JsonObject) {
        val action =
            payload.takeIf { it.has("action") && !it.get("action").isJsonNull }
                ?.get("action")
                ?.asString
                .orEmpty()
                .trim()
                .lowercase()
        if (action == "session" && payload.has("session_id")) {
            handleTradeSessionPacket(TradeSessionPacket.fromJson(payload))
            return
        }

        val packet = TradeResultPacket.fromJson(payload)
        val resolvedMessage = packet.message.trim().ifBlank { packet.defaultTradeFeedback() }
        val feedbackLine = packet.toTradeFeedbackLine(resolvedMessage)
        val currentSheet = sink.currentState().sheet
        val capturedTradeSheet = currentSheet?.takeIf { it.canCaptureTradeResult() }
        if (capturedTradeSheet != null) {
            sink.updateState {
                copy(
                    sheet =
                        capturedTradeSheet.copy(
                            detailLines =
                                prependSheetFeedbackLine(
                                    capturedTradeSheet.detailLines,
                                    feedbackLine,
                                ),
                            loading = false,
                        ),
                )
            }
            sink.clearPendingInteractionRequest()
        }
        if (capturedTradeSheet == null) {
            sink.appendTranscript(feedbackLine)
        }
    }

    private fun handleTradeSessionPacket(packet: TradeSessionPacket) {
        val previous = currentTradeSession
        if (previous != null && previous.sessionId == packet.sessionId &&
            packet.revision < previous.revision
        ) {
            return
        }
        val state = sink.currentState()
        if (packet.isTerminalTradeSession()) {
            currentTradeSession = null
            sink.updateState {
                copy(
                    sheet = null,
                    dialog = null,
                )
            }
            val result =
                packet.terminalResult.trim().ifBlank {
                    tradeStatusLabel(packet.status)
                }
            sink.appendTranscript("交易：$result")
            sink.clearPendingInteractionRequest()
            sink.sendDirectCommand("inventory", suppressImmediateScroll = true)
            return
        }
        currentTradeSession = packet
        val nextSheet = buildTradeSessionSheetState(packet, state.inventoryItems)
        sink.updateState {
            copy(
                sheet = nextSheet,
                dialog = null,
            )
        }
        sink.clearPendingInteractionRequest()
        if (packet.status == "active" && state.inventoryItems.isEmpty()) {
            sink.sendDirectCommand("inventory", suppressImmediateScroll = true)
        }
    }

    fun showInventoryPanel(categoryId: String = "all") {
        sink.flushPendingRoomRefresh()
        sink.clearPendingInteractionRequest()
        clearPendingInventoryInspectItem()
        val currentState = sink.currentState()
        val currentInventoryItems = currentState.inventoryItems
        val currentInventorySummary = currentState.inventorySummary
        val localCategory = categoryId.takeIf { it != "all" }
        val nextSheet =
            buildInventorySheetState(
                currentInventoryItems,
                categoryId,
                loading = localCategory == null,
                inventorySummary = currentInventorySummary,
            )
        sink.updateState {
            copy(
                sheet = nextSheet,
                dialog = null,
            )
        }
        if (localCategory == null) {
            sink.prepareProtocolSheetCommand(nextSheet, command = "inventory")
            sink.sendDirectCommand("inventory", suppressImmediateScroll = true)
        }
    }

    fun showEquipmentPanel() {
        sink.flushPendingRoomRefresh()
        sink.clearPendingInteractionRequest()
        clearPendingInventoryInspectItem()
        val currentInventoryItems = sink.currentState().inventoryItems
        val nextSheet = buildEquipmentSheetState(currentInventoryItems, loading = true)
        sink.updateState {
            copy(
                sheet = nextSheet,
                dialog = null,
            )
        }
        sink.prepareProtocolSheetCommand(nextSheet, command = "inventory")
        sink.sendDirectCommand("inventory", suppressImmediateScroll = true)
    }

    fun requestInventoryItemDetail(itemId: String) {
        val matched =
            sink.currentState().inventoryItems.firstOrNull { snapshot ->
                snapshot.instanceId == itemId || snapshot.id == itemId || snapshot.name == itemId
            }
        val item = matched?.toInventoryItemDisplay()
        if (item == null) {
            clearPendingInventoryInspectItem()
            sink.clearPendingProtocolSheetRequest()
            sink.updateState {
                copy(
                    sheet = null,
                    sheetNavigation = SheetNavigationState(),
                    dialog = null,
                    inventoryRefreshPending = true,
                )
            }
            sink.appendTranscript("该物品已不在当前背包快照中，正在刷新背包。")
            sink.sendDirectCommand("inventory", suppressImmediateScroll = true)
            return
        }
        requestProtocolItemDetail(item, matched)
    }

    fun requestSceneItemDetail(target: InteractionObject) {
        val fallbackActions = buildObjectDetailActions(target, emptyList())
        sink.clearPendingInteractionRequest()
        clearPendingInventoryInspectItem()
        sink.updateState {
            copy(
                sheet =
                    buildInventoryDetailSheet(
                        InventoryItemDisplay(
                            id = target.id,
                            name = target.title,
                            commandTarget = target.scopedCommandTarget(),
                            typeLabel = target.subtitle,
                            effectText = target.description.ifBlank { target.summary },
                            defaultActions = fallbackActions,
                            detailSource = PopupSource.INTERACTION_OBJECT,
                            detailSubtitle = target.subtitle,
                            detailTargetKind = target.kind,
                        ),
                        loading = false,
                    ),
                dialog = null,
            )
        }
    }

    private fun requestProtocolItemDetail(
        item: InventoryItemDisplay,
        snapshot: InventoryItemSnapshot? = null,
    ) {
        pendingInventoryInspectItem = item
        pendingInventoryInspectSnapshot = snapshot
        val loadingSheet = buildInventoryDetailSheet(item, loading = true, snapshot = snapshot)
        sink.updateState {
            copy(
                sheet = loadingSheet,
                dialog = null,
            )
        }
        sink.prepareProtocolSheetCommand(loadingSheet, command = "look ${item.commandTarget}")
        sink.sendDirectCommand("look ${item.commandTarget}", suppressImmediateScroll = true)
    }

    fun handleInventoryItemDialog(packet: DialogPacket): Boolean {
        val requestedItem = pendingInventoryInspectItem ?: return false
        if (packet.source != "look_item") {
            return false
        }

        val state = sink.currentState()
        if (state.sheet?.navigation?.routeKey != "inventory/item/${requestedItem.id}") {
            clearPendingInventoryInspectItem()
            return true
        }
        val packetTarget = packet.target?.id.orEmpty()
        if (packetTarget.isNotBlank() && packetTarget != requestedItem.commandTarget) {
            return true
        }
        val latestItem = state.inventoryItems.firstOrNull { it.routeKey() == requestedItem.id }
        if (latestItem != pendingInventoryInspectSnapshot) {
            sink.clearPendingProtocolSheetRequest()
            clearPendingInventoryInspectItem()
            return true
        }
        sink.clearPendingProtocolSheetRequest()
        val itemActions = buildInventoryDialogActions(packet, requestedItem)
        sink.updateState {
            copy(
                sheet =
                    buildInventoryDetailSheet(
                        fallback = requestedItem,
                        title = buildInventoryDialogTitle(packet, requestedItem),
                        detailLines =
                            buildInventoryDialogMessage(
                                packet,
                                requestedItem,
                            ).toParagraphs(),
                        actions = itemActions,
                        loading = false,
                        snapshot = pendingInventoryInspectSnapshot,
                    ),
                dialog = null,
            )
        }
        clearPendingInventoryInspectItem()
        return true
    }

    fun handleInventoryPacket(payload: JsonObject) {
        val nextInventoryItems =
            InventoryItemSnapshot.listFrom(
                payload,
            ).stackInventoryItemsForDisplay()
        val nextInventorySummary = InventorySummary.fromJson(payload)
        if (sink.currentState().sheet?.resolvedSheetKind() == ShellSheetKind.INVENTORY) {
            sink.clearPendingProtocolSheetRequest()
        }
        sink.updateState {
            val currentSheet = sheet
            val refreshedTradeSheet =
                currentTradeSession
                    ?.takeIf { currentSheet?.isTradeSessionSheet() == true }
                    ?.let { packet -> buildTradeSessionSheetState(packet, nextInventoryItems) }
            copy(
                inventoryItems = nextInventoryItems,
                inventorySummary = nextInventorySummary ?: inventorySummary,
                inventoryRefreshPending = false,
                sheet =
                    refreshedTradeSheet
                        ?: currentSheet.refreshInventorySnapshot(
                            nextInventoryItems,
                            nextInventorySummary ?: inventorySummary,
                        ),
            )
        }
    }

    fun showWorldShopGuidePanel() {
        sink.clearPendingInteractionRequest()
        sink.updateState {
            copy(
                sheet = buildWorldShopGuideSheetState(),
                dialog = null,
            )
        }
    }
}
