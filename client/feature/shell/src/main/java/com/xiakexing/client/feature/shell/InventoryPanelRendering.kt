package com.xiakexing.client.feature.shell

import com.xiakexing.client.core.protocol.payload.DialogPacket
import com.xiakexing.client.core.protocol.payload.DialogTargetPacket
import com.xiakexing.client.core.protocol.payload.InventoryItemSnapshot
import com.xiakexing.client.core.protocol.payload.InventorySummary

internal const val OPEN_INVENTORY_ITEM_DETAIL_PREFIX = "ui://panel/inventory/item/"
internal const val OPEN_INVENTORY_PANEL_COMMAND = "ui://panel/character/inventory"
internal const val OPEN_INVENTORY_CATEGORY_PREFIX = "ui://panel/inventory/category/"

/** 背包默认容量：最多 200 种不同物品（服务端 INVENTORY_CAPACITY 同值，客户端 fallback）。 */
internal const val DEFAULT_INVENTORY_CAPACITY = 200

internal data class InventoryItemDisplay(
    val id: String,
    val name: String,
    val commandTarget: String = name,
    val typeLabel: String,
    val quantity: Int = 1,
    val stateLabel: String = "",
    val effectText: String,
    val defaultActions: List<ShellAction> = emptyList(),
    val detailSource: PopupSource = PopupSource.DOCK_BUTTON,
    val detailSubtitle: String = typeLabel,
    val detailTargetKind: InteractionObjectKind? = null,
)

/** 数量后缀单一真源：数量 >1 时统一拼「N 个」。 */
private fun String.withQuantitySuffix(quantity: Int): String =
    if (quantity > 1) "$this $quantity 个" else this

internal fun InventoryItemDisplay.displayTitle(): String = name.withQuantitySuffix(quantity)

internal fun InventoryItemSnapshot.displayTitle(): String =
    name.ifBlank { id }.withQuantitySuffix(quantity)

/**
 * 物品实例路由键：instance_id 优先（同名不同实例可分别打开详情），
 * 旧数据无 instance_id 时回退到模板 id。
 */
internal fun InventoryItemSnapshot.routeKey(): String {
    return instanceId.ifBlank { id.ifBlank { name } }
}

/** 绑定策略的玩家语言标签；未绑定或空字段不生成标签。 */
internal fun InventoryItemSnapshot.bindingLabel(): String? {
    return when (bindingPolicy.trim().lowercase()) {
        "character" -> "已绑定"
        "account" -> "已绑定账号"
        else -> null
    }
}

/** 来源标识到玩家语言的映射；未知来源不生成标签，避免暴露内部标识。 */
internal fun InventoryItemSnapshot.provenanceLabel(): String? {
    val source = provenance["source"]?.trim().orEmpty()
    if (source.isBlank()) {
        return null
    }
    return when (source) {
        "quest_reward" -> "任务奖励"
        "npc_gift" -> "赠礼"
        "combat_loot", "scene_claim" -> "战斗掉落"
        "npc_shop" -> "商店购买"
        "gather" -> "采集"
        "craft" -> "打造"
        "trade" -> "交易"
        else -> null
    }
}

/** 物品只显示仍有效的交易模式标签。 */
internal fun InventoryItemSnapshot.usageLabel(): String? {
    val extraModes =
        usageModes
            .map { it.trim().lowercase() }
            .filter { it.isNotBlank() }
            .distinct()
    return extraModes
        .mapNotNull { mode ->
            when (mode) {
                "trade" -> "交易"
                else -> null
            }
        }
        .takeIf { it.isNotEmpty() }
        ?.joinToString("、")
}

/** 品质标签：1 为普通，其余映射常见品质档位；未知档位保留数字。 */
internal fun InventoryItemSnapshot.qualityLabel(): String {
    return when (quality) {
        1 -> "品质：普通"
        2 -> "品质：优良"
        3 -> "品质：精良"
        4 -> "品质：稀有"
        5 -> "品质：传说"
        else -> "品质：$quality"
    }
}

/** 归属行：owner_id 非空时按绑定策略说明归属角色。 */
internal fun InventoryItemSnapshot.ownerLabel(): String? {
    if (ownerId.isBlank()) {
        return null
    }
    return when (bindingPolicy.trim().lowercase()) {
        "account" -> "归属：当前账号"
        else -> "归属：当前角色"
    }
}

/** 详情身份行：仅在有对应字段时生成，空字段不产生空行。 */
internal fun InventoryItemSnapshot.identityDetailLines(): List<String> {
    return buildList {
        add(qualityLabel())
        bindingLabel()?.let(::add)
        ownerLabel()?.let(::add)
        provenanceLabel()?.let { label -> add("来源：$label") }
        usageLabel()?.let { label -> add("用途：$label") }
        if (instanceId.isNotBlank()) {
            add("独立实例")
        }
    }
}

internal fun InventoryItemSnapshot.toInventoryItemDisplay(): InventoryItemDisplay {
    return InventoryItemDisplay(
        id = routeKey(),
        name = name.ifBlank { id },
        commandTarget = id.ifBlank { name },
        typeLabel = itemTypeLabel(),
        quantity = quantity,
        stateLabel = if (equipped) "已装备" else "",
        effectText = description.toParagraphs().joinToString("\n").ifBlank { "暂无效果说明。" },
        defaultActions = buildInventorySnapshotFallbackActions(this),
    )
}

internal fun buildInventorySheetState(
    inventoryItems: List<InventoryItemSnapshot>,
    categoryId: String = "all",
    loading: Boolean = false,
    inventorySummary: InventorySummary? = null,
): ShellSheetState {
    val displayItems =
        inventoryItems.stackInventoryItemsForDisplay()
            .sortedWith(
                compareByDescending<InventoryItemSnapshot> { it.equipped }
                    .thenBy { it.itemTypeLabel() }
                    .thenBy { it.name.ifBlank { it.id } },
            )
    val resolvedCapacity = inventorySummary?.capacity ?: DEFAULT_INVENTORY_CAPACITY
    val resolvedUsedSlots = inventorySummary?.usedSlots ?: displayItems.size
    val categories =
        displayItems.map {
            it.itemTypeLabel()
        }.filter { it.isNotBlank() }.distinct().sorted()
    val selectedCategory = categoryId.takeIf { it == "all" || it in categories } ?: "all"
    val visibleItems =
        if (selectedCategory == "all") {
            displayItems
        } else {
            displayItems.filter {
                it.itemTypeLabel() == selectedCategory
            }
        }
    return buildGridSheet(
        source = PopupSource.DOCK_BUTTON,
        title = "背包",
        showHeaderTitle = false,
        gridMode = ShellPanelMode.INVENTORY_GRID,
        panelCards = visibleItems.map { item -> item.toInventoryPanelCard() },
        panelTabs =
            listOf(ShellAction("全部", "${OPEN_INVENTORY_CATEGORY_PREFIX}all", dismissAfterInvoke = false)) +
                categories.map {
                        category ->
                    ShellAction(category, "$OPEN_INVENTORY_CATEGORY_PREFIX$category", dismissAfterInvoke = false)
                },
        panelSelectedTabId = selectedCategory,
        panelEmptyText = "背包空空如也\n可从场景物品栏拾取物品",
        emptyPresentation = ShellSheetEmptyPresentation.MESSAGE,
        loading = loading,
        navigation =
            SheetNavigationMeta(
                dockSection = DockWorkspaceSection.INVENTORY,
                routeKey = "inventory",
            ),
        kind = ShellSheetKind.INVENTORY,
        // "共X项" = 物品种类数（与容量口径一致），非堆叠数量总和（铜钱 100 个算 1 项）。
        headerText = "容量 $resolvedUsedSlots/$resolvedCapacity 共${resolvedUsedSlots}项",
    )
}

internal fun buildEquipmentSheetState(
    inventoryItems: List<InventoryItemSnapshot>,
    loading: Boolean = false,
): ShellSheetState {
    return buildInfoListSheet(
        source = PopupSource.DOCK_BUTTON,
        title = "装备",
        items =
            inventoryItems.filter {
                    item ->
                item.equipped
            }.map { item -> item.toEquipmentSheetItem() },
        panelEmptyText = "无装备",
        loading = loading,
        kind = ShellSheetKind.EQUIPMENT,
    )
}

internal fun buildGiveSheetState(
    target: InteractionObject,
    targetCommand: String,
    inventoryItems: List<InventoryItemSnapshot>,
): ShellSheetState {
    return buildGridSheet(
        source = PopupSource.INTERACTION_GIVE,
        title = "给予",
        showHeaderTitle = false,
        subtitle = target.title,
        gridMode = ShellPanelMode.INVENTORY_GRID,
        panelCards = buildInteractionGiveCards(targetCommand, inventoryItems),
        panelEmptyText = "无物品",
        interactionTargetId = target.id,
        interactionTargetUiKey = target.uiKey,
        interactionTargetKind = target.kind,
    )
}

internal fun buildInventoryDetailSheet(
    fallback: InventoryItemDisplay,
    title: String = fallback.displayTitle(),
    detailLines: List<String> = fallback.effectText.toParagraphs(),
    actions: List<ShellAction> = fallback.defaultActions,
    loading: Boolean = false,
    snapshot: InventoryItemSnapshot? = null,
): ShellSheetState {
    val isInteractionSurface = fallback.detailSource == PopupSource.INTERACTION_OBJECT

    return buildInfoListSheet(
        source = fallback.detailSource,
        title = title,
        dialogSource = "inventory_detail",
        // 两阶段渲染根除：loading 帧保持空白（不回退 effectText），数据到达后一次性渲染，
        // 避免首帧尺寸偏大再闪烁变小的两段式体验。
        detailLines =
            if (loading) {
                emptyList()
            } else {
                detailLines.ifEmpty {
                    fallback.effectText.toParagraphs()
                }
            },
        actions = actions,
        panelStats =
            buildList {
                add(fallback.typeLabel)
                if (fallback.quantity > 1) add("数量 ${fallback.quantity}")
                fallback.stateLabel.takeIf { it.isNotBlank() }?.let(::add)
                // 协议快照身份行：仅在有快照来源时渲染，离线 demo 与场景物详情不显示。
                snapshot?.identityDetailLines()?.let(::addAll)
            },
        panelEmptyText = "暂无物品说明",
        interactionTargetId = if (isInteractionSurface) fallback.id else "",
        interactionTargetKind = if (isInteractionSurface) fallback.detailTargetKind else null,
        compactPanel = true,
        loading = loading,
        navigation =
            if (isInteractionSurface) {
                SheetNavigationMeta(
                    routeKey = "interaction/${fallback.detailTargetKind?.name?.lowercase().orEmpty()}/${fallback.id}/detail",
                    depth = 1,
                )
            } else {
                SheetNavigationMeta(
                    dockSection = DockWorkspaceSection.INVENTORY,
                    routeKey = "inventory/item/${fallback.id}",
                    depth = 1,
                )
            },
    )
}

internal fun ShellSheetState?.refreshInventorySnapshot(
    inventoryItems: List<InventoryItemSnapshot>,
    summary: InventorySummary?,
): ShellSheetState? {
    val current = this ?: return null
    return when {
        current.source == PopupSource.DOCK_BUTTON && current.resolvedSheetKind() == ShellSheetKind.INVENTORY ->
            buildInventorySheetState(
                inventoryItems,
                current.panelSelectedTabId,
                inventorySummary = summary,
            )
        current.source == PopupSource.DOCK_BUTTON && current.resolvedSheetKind() == ShellSheetKind.EQUIPMENT ->
            buildEquipmentSheetState(inventoryItems)
        current.source == PopupSource.DOCK_BUTTON && current.dialogSource == "inventory_detail" -> {
            val itemKey = current.navigation.routeKey.removePrefix("inventory/item/")
            val item = inventoryItems.firstOrNull { it.routeKey() == itemKey } ?: return null
            buildInventoryDetailSheet(item.toInventoryItemDisplay(), snapshot = item)
        }
        current.source == PopupSource.INTERACTION_GIVE && current.interactionTargetId.isNotBlank() ->
            current.copy(
                panelCards =
                    buildInteractionGiveCards(
                        current.interactionTargetId.commandTargetFromInteractionId(),
                        inventoryItems,
                    ),
                loading = false,
            )
        else -> current
    }
}

internal fun buildInteractionGiveItems(
    targetId: String,
    inventoryItems: List<InventoryItemSnapshot>,
): List<ShellSheetItem> {
    if (targetId.isBlank()) {
        return emptyList()
    }

    return inventoryItems
        .filter { item ->
            item.canGive && item.location != "room" && item.id.ifBlank { item.name }.isNotBlank()
        }
        .sortedWith(
            compareBy<InventoryItemSnapshot> {
                it.itemTypeLabel()
            }.thenBy { it.name.ifBlank { it.id } },
        )
        .map { item ->
            val commandTarget = item.id.ifBlank { item.name }
            ShellSheetItem(
                title = item.name.ifBlank { item.id },
                trailing = if (item.quantity > 1) "${item.quantity} 个" else "",
                command = "give $commandTarget to $targetId",
                dismissAfterInvoke = true,
            )
        }
}

internal fun buildInteractionGiveCards(
    targetId: String,
    inventoryItems: List<InventoryItemSnapshot>,
): List<ShellPanelCard> {
    return buildInteractionGiveItems(targetId, inventoryItems).map { item ->
        ShellPanelCard(
            id = "give:$targetId:${item.title}",
            title = item.title,
            bottomLabel = item.trailing,
            command = item.command,
            dismissAfterInvoke = true,
        )
    }
}

internal fun InventoryItemSnapshot.toInventoryPanelCard(): ShellPanelCard {
    return ShellPanelCard(
        id = routeKey(),
        title = formatBracketedLabel(name.ifBlank { id }, quantity),
        badges =
            buildList {
                add(itemTypeLabel())
                // 附加标签按 绑定 > 限制 > 用途 > 来源 的优先级保留，最多两个。
                bindingLabel()?.let(::add)
                if (size < 3) {
                    restrictionLabel()?.let(::add)
                }
                if (size < 3) {
                    usageLabel()?.let(::add)
                }
                if (size < 3) {
                    provenanceLabel()?.let(::add)
                }
            },
        bottomLabel = "",
        metadata =
            buildMap {
                if (equipped) put("state", "已装备")
            },
        command = "$OPEN_INVENTORY_ITEM_DETAIL_PREFIX${routeKey()}",
    )
}

/** 限制标签：不可交易（非装备状态且服务端标记不可给予/丢弃时）。 */
internal fun InventoryItemSnapshot.restrictionLabel(): String? {
    return if (!canGive && !equipped && location != "room") {
        "不可交易"
    } else {
        null
    }
}

internal fun List<InventoryItemSnapshot>.stackInventoryItemsForDisplay(): List<InventoryItemSnapshot> {
    val result = mutableListOf<InventoryItemSnapshot>()
    val indexByKey = linkedMapOf<InventoryStackKey, Int>()
    forEach { item ->
        if (!item.canStackForInventoryDisplay()) {
            result += item
            return@forEach
        }

        val key = item.inventoryStackKey()
        val existingIndex = indexByKey[key]
        if (existingIndex == null) {
            indexByKey[key] = result.size
            result += item
        } else {
            val existing = result[existingIndex]
            result[existingIndex] = existing.copy(quantity = existing.quantity + item.quantity)
        }
    }
    return result
}

private fun InventoryItemSnapshot.canStackForInventoryDisplay(): Boolean {
    return !equipped && !canEquip && id.ifBlank { name }.isNotBlank()
}

private fun InventoryItemSnapshot.inventoryStackKey(): InventoryStackKey {
    return InventoryStackKey(
        id = id.ifBlank { name },
        instanceId = instanceId,
        name = name,
        description = description,
        type = type,
        canUse = canUse,
        canGive = canGive,
        location = location,
        consumeAction = consumeAction,
        summary = summary,
        typeLabel = typeLabel,
    )
}

private fun InventoryItemSnapshot.toEquipmentSheetItem(): ShellSheetItem {
    return ShellSheetItem(
        title = name.ifBlank { id },
        subtitle = itemTypeLabel().takeIf { it.isNotBlank() }.orEmpty(),
        trailing = "已装备",
        command = "$OPEN_INVENTORY_ITEM_DETAIL_PREFIX${routeKey()}",
    )
}

internal fun InventoryItemSnapshot.itemTypeLabel(): String {
    if (typeLabel.isNotBlank()) {
        return typeLabel
    }
    return typeToItemTypeLabel(type)
}

/** 物品类型 → 展示标签：背包快照与对话目标共用同一映射真源。 */
private fun typeToItemTypeLabel(type: String): String {
    return when (type.trim().lowercase()) {
        "consumable" -> "药品"
        "weapon", "armor" -> "装备"
        else -> "器具"
    }
}

internal fun buildInventoryDialogTitle(
    packet: DialogPacket,
    fallback: InventoryItemDisplay,
): String {
    val target = packet.target
    val baseTitle =
        target?.name?.ifBlank { packet.title.ifBlank { fallback.name } }
            ?: packet.title.ifBlank { fallback.name }
    val quantitySuffix =
        target?.quantity?.takeIf { it > 1 }?.let { count -> " $count 个" }
            ?: fallback.quantity.takeIf { it > 1 }?.let { count -> " $count 个" }
            ?: ""
    return baseTitle + quantitySuffix
}

internal fun buildInventoryDialogMessage(
    packet: DialogPacket,
    fallback: InventoryItemDisplay,
): String {
    val rawText =
        packet.target?.description
            ?.takeIf { text -> text.isNotBlank() }
            ?: packet.content.takeIf { text -> text.isNotBlank() }
            ?: fallback.effectText
    val normalizedLines =
        rawText.toParagraphs()
            .filterNot { line -> line == packet.title || line == fallback.name }
            .filterNot { line -> line.startsWith("数量") }
            .filterNot { line -> line == packet.target.itemTypeLabel() }
    return normalizedLines.joinToString("\n").ifBlank { fallback.effectText }
}

internal fun buildInventorySnapshotFallbackActions(item: InventoryItemSnapshot): List<ShellAction> {
    val commandTarget = item.id.ifBlank { item.name }
    if (commandTarget.isBlank()) {
        return emptyList()
    }

    return buildList {
        if (item.location == "room") {
            add(ShellAction("拾取", "get $commandTarget"))
            return@buildList
        }
        if (item.equipped) {
            add(ShellAction("卸下", "unequip $commandTarget"))
        } else if (item.canEquip) {
            add(ShellAction("装备", "equip $commandTarget"))
        }
        if (item.canUse) {
            add(
                ShellAction(
                    label = if (item.consumeAction == "drink") "饮用" else "使用",
                    command = if (item.consumeAction == "drink") "drink $commandTarget" else "eat $commandTarget",
                ),
            )
        }
        if (!item.equipped && item.location != "room") {
            add(ShellAction("丢弃", "drop $commandTarget"))
        }
    }
}

internal fun buildInventoryDialogActions(
    packet: DialogPacket,
    fallback: InventoryItemDisplay,
): List<ShellAction> {
    val protocolActions =
        packet.actionGroups.flatMap { group -> group.actions }
            .mapNotNull { action -> action.toShellActionOrNull() }
    if (packet.actionGroups.isNotEmpty()) {
        return protocolActions
    }

    val target = packet.target
    val commandTarget = target?.id?.takeIf { it.isNotBlank() } ?: fallback.commandTarget
    if (commandTarget.isBlank()) {
        return fallback.defaultActions
    }

    val fallbackActions = mutableListOf<ShellAction>()
    if (target != null) {
        if (target.location == "room") {
            fallbackActions += ShellAction("拾取", "get $commandTarget")
            return fallbackActions
        }
        if (target.equipped) {
            fallbackActions += ShellAction("卸下", "unequip $commandTarget")
        } else if (target.canEquip) {
            fallbackActions += ShellAction("装备", "equip $commandTarget")
        }
        if (target.canUse) {
            fallbackActions +=
                ShellAction(
                    label = if (target.consumeAction == "drink") "饮用" else "使用",
                    command = if (target.consumeAction == "drink") "drink $commandTarget" else "eat $commandTarget",
                )
        }
        if (!target.equipped && target.location != "room") {
            fallbackActions += ShellAction("丢弃", "drop $commandTarget")
        }
    }
    return fallbackActions.ifEmpty { fallback.defaultActions }
}

private fun DialogTargetPacket?.itemTypeLabel(): String {
    if (this == null) {
        return ""
    }
    return when (type.trim().lowercase()) {
        "consumable" -> "药品"
        "weapon", "armor" -> "装备"
        else -> "器具"
    }
}

private data class InventoryStackKey(
    val id: String,
    // 服务端可堆叠对象共享同一实例身份；不同实例即使展示字段相同也不合并，
    // 保证同名不同来源/绑定/归属的物品在列表中保持可见。
    val instanceId: String,
    val name: String,
    val description: String,
    val type: String,
    val canUse: Boolean,
    val canGive: Boolean,
    val location: String,
    val consumeAction: String,
    val summary: String,
    val typeLabel: String,
)
