package com.xiakexing.client.feature.shell.popup

import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.rememberLazyGridState
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.xiakexing.client.feature.shell.DockWorkspaceSection
import com.xiakexing.client.feature.shell.ShellAction
import com.xiakexing.client.feature.shell.ShellPanelCard
import com.xiakexing.client.feature.shell.ShellPanelMode
import com.xiakexing.client.feature.shell.ShellSheetEmptyPresentation
import com.xiakexing.client.feature.shell.ShellSheetItem
import com.xiakexing.client.feature.shell.ShellSheetKind
import com.xiakexing.client.feature.shell.ShellSheetState
import com.xiakexing.client.feature.shell.component.AdaptiveSingleLineText
import com.xiakexing.client.feature.shell.component.ShellEmpty
import com.xiakexing.client.feature.shell.isMailDetailSheet
import com.xiakexing.client.feature.shell.isMailSheet
import com.xiakexing.client.feature.shell.isNpcQuestDetailSheet
import com.xiakexing.client.feature.shell.isNpcQuestListSheet
import com.xiakexing.client.feature.shell.isQuestDetailSheet
import com.xiakexing.client.feature.shell.isQuestListSheet
import com.xiakexing.client.feature.shell.ui.ShellChrome
import com.xiakexing.client.feature.shell.ui.ShellLines
import com.xiakexing.client.feature.shell.ui.ShellPalette
import androidx.compose.foundation.lazy.grid.itemsIndexed as gridItemsIndexed

internal const val DENSE_PANEL_DETAIL_LIST_TAG = "dense_panel_detail_list"
internal const val DENSE_PANEL_CARD_TAG_PREFIX = "dense_panel_card:"
internal const val DENSE_PANEL_RAIL_TAG = "dense_panel_rail"

private const val DOCK_INVENTORY_GRID_COLUMNS = 3
private const val INVENTORY_GRID_COLUMNS = 6
private const val SKILL_GRID_COLUMNS = 3
private const val COMBAT_GRID_COLUMNS = 4
private const val SHOP_GRID_COLUMNS = 3
private const val DEFAULT_GRID_COLUMNS = 2
private val DENSE_PANEL_CARD_HEIGHT = 58.dp
private val DENSE_PANEL_SINGLE_LINE_CARD_HEIGHT = 40.dp
private val DENSE_PANEL_RAIL_ITEM_HEIGHT = 40.dp

@Composable
internal fun DenseSheetPanel(
    sheet: ShellSheetState,
    onActionClick: (ShellAction) -> Unit,
    onItemClick: (ShellSheetItem) -> Unit,
    layoutMode: InlineSheetLayoutMode = InlineSheetLayoutMode.CONTEXT,
    modifier: Modifier = Modifier,
) {
    val isFunctionPage = layoutMode == InlineSheetLayoutMode.FUNCTION_PAGE
    val isDockInventoryGrid =
        sheet.kind == ShellSheetKind.INVENTORY &&
            sheet.navigation.dockSection == DockWorkspaceSection.INVENTORY
    if (sheet.loading && !sheet.hasVisibleSheetContent()) {
        InitialSheetLoadingState(modifier = modifier)
        return
    }
    if (sheet.presentation != com.xiakexing.client.feature.shell.ShellSheetPresentation.DEFAULT) {
        PresentationSheetPanel(
            sheet = sheet,
            onActionClick = onActionClick,
            onItemClick = onItemClick,
            modifier = modifier,
        )
        return
    }
    if (sheet.isQuestDetailSheet() || sheet.isNpcQuestDetailSheet()) {
        QuestWorkbenchPanel(
            sheet = sheet,
            fillAvailableHeight = true,
            modifier = modifier,
        )
        return
    }
    if (sheet.isMailDetailSheet()) {
        MailDetailWorkbenchPanel(
            sheet = sheet,
            onItemClick = onItemClick,
            fillAvailableHeight = false,
            modifier = modifier,
        )
        return
    }

    val showPanelContent =
        sheet.panelMode != ShellPanelMode.INFO_LIST ||
            sheet.panelTabs.isNotEmpty() ||
            sheet.panelCards.isNotEmpty() ||
            sheet.items.isNotEmpty() ||
            sheet.detailLines.isEmpty()
    val mailDetailOnly =
        sheet.panelMode == ShellPanelMode.INFO_LIST &&
            sheet.isMailSheet() &&
            sheet.detailLines.isNotEmpty() &&
            !showPanelContent
    val functionDetailOnly =
        isFunctionPage &&
            sheet.panelMode == ShellPanelMode.INFO_LIST &&
            sheet.detailLines.isNotEmpty() &&
            !showPanelContent
    Column(
        modifier = modifier,
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        if (sheet.panelMode == ShellPanelMode.INFO_LIST && sheet.detailLines.isNotEmpty()) {
            LazyColumn(
                modifier =
                    Modifier
                        .fillMaxWidth()
                        .testTag(DENSE_PANEL_DETAIL_LIST_TAG)
                        .then(
                            if (mailDetailOnly || functionDetailOnly) {
                                Modifier.weight(1f, fill = true)
                            } else {
                                Modifier.heightIn(max = 132.dp)
                            },
                        ),
                verticalArrangement = Arrangement.spacedBy(6.dp),
                contentPadding = PaddingValues(bottom = 2.dp),
            ) {
                items(sheet.detailLines) { line ->
                    DetailLine(text = line)
                }
            }
        }
        if (showPanelContent && sheet.panelMode == ShellPanelMode.MAP_ATLAS) {
            InlineSheetMapPanel(
                sheet = sheet,
                onActionClick = onActionClick,
                modifier =
                    Modifier
                        .fillMaxWidth()
                        .weight(1f, fill = true),
            )
        } else if (showPanelContent) {
            if (isFunctionPage && sheet.usesVerticalCategoryRail()) {
                RailCategoryWorkspace(
                    sheet = sheet,
                    onActionClick = onActionClick,
                    onItemClick = onItemClick,
                    isDockInventoryGrid = isDockInventoryGrid,
                    modifier = Modifier.weight(1f, fill = true),
                )
            } else if (isFunctionPage) {
                DensePanelGrid(
                    panelMode = sheet.panelMode,
                    cards = sheet.panelCards,
                    items = sheet.items,
                    emptyText =
                        if (!sheet.loading && sheet.emptyPresentation == ShellSheetEmptyPresentation.MESSAGE) {
                            sheet.panelEmptyText
                        } else {
                            ""
                        },
                    useQuestTaskRows = sheet.isQuestListSheet(),
                    useNpcQuestTaskRows = sheet.isNpcQuestListSheet(),
                    onActionClick = onActionClick,
                    onItemClick = onItemClick,
                    enabled = !sheet.loading,
                    isDockInventoryGrid = isDockInventoryGrid,
                    modifier =
                        Modifier
                            .fillMaxWidth()
                            .weight(1f, fill = true),
                )
            } else {
                RailCategoryWorkspace(
                    sheet = sheet,
                    onActionClick = onActionClick,
                    onItemClick = onItemClick,
                    isDockInventoryGrid = isDockInventoryGrid,
                    modifier =
                        Modifier.weight(
                            1f,
                            fill =
                                sheet.isQuestListSheet() ||
                                    sheet.isNpcQuestListSheet() ||
                                    sheet.panelMode == ShellPanelMode.COMBAT_GRID ||
                                    sheet.panelMode == ShellPanelMode.SKILL_GRID,
                        ),
                )
            }
        }
    }
}

internal fun ShellSheetState.hasVisibleSheetContent(): Boolean {
    return detailLines.isNotEmpty() || interactionLines.isNotEmpty() || items.isNotEmpty() ||
        panelCards.isNotEmpty() || panelAllCards.isNotEmpty() || panelStats.isNotEmpty()
}

@Composable
private fun RailCategoryWorkspace(
    sheet: ShellSheetState,
    onActionClick: (ShellAction) -> Unit,
    onItemClick: (ShellSheetItem) -> Unit,
    isDockInventoryGrid: Boolean,
    modifier: Modifier = Modifier,
) {
    val categoryRail: (@Composable RowScope.() -> Unit)? =
        if (sheet.panelTabs.isNotEmpty()) {
            {
                DensePanelRail(
                    actions = sheet.panelTabs,
                    selectedId = sheet.panelSelectedTabId,
                    onActionClick = onActionClick,
                )
            }
        } else {
            null
        }
    CategoryRailLayout(
        rail = categoryRail,
        modifier = modifier,
    ) {
        DensePanelGrid(
            panelMode = sheet.panelMode,
            cards = sheet.panelCards,
            items = sheet.items,
            emptyText =
                if (!sheet.loading && sheet.emptyPresentation == ShellSheetEmptyPresentation.MESSAGE) {
                    sheet.panelEmptyText
                } else {
                    ""
                },
            useQuestTaskRows = sheet.isQuestListSheet(),
            useNpcQuestTaskRows = sheet.isNpcQuestListSheet(),
            onActionClick = onActionClick,
            onItemClick = onItemClick,
            enabled = !sheet.loading,
            isDockInventoryGrid = isDockInventoryGrid,
            modifier = Modifier.weight(1f),
        )
    }
}

/**
 * 弹窗左侧竖列分类 + 正文的组合骨架：rail 与内容间 6dp 间距，不绘制竖线分割。
 * 背包/技能/聊天三处共用，参数由本组件构造保证一致。
 * 屏幕层 rail（主界面左栏）走 LeftRailWithDivider（ShellRailSection），不共用本组件。
 */
@Composable
internal fun CategoryRailLayout(
    rail: (@Composable RowScope.() -> Unit)?,
    modifier: Modifier = Modifier,
    content: @Composable RowScope.() -> Unit,
) {
    Row(
        modifier = modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        if (rail != null) {
            rail()
        }
        content()
    }
}

/** 背包/技能网格使用左侧竖列分类（每标签 1dp 边框，无竖线分割）；带分类的面板才触发。 */
internal fun ShellSheetState.usesVerticalCategoryRail(): Boolean {
    return panelTabs.isNotEmpty() &&
        (panelMode == ShellPanelMode.INVENTORY_GRID || panelMode == ShellPanelMode.SKILL_GRID)
}

/**
 * 左侧竖列分类：每个标签带 1dp 边框，选中项加高亮底色；
 * 标签列与正文之间无竖线分割（6dp 间距呼吸）。
 * unreadCommands 中的标签在文字后追加小圆点。屏幕 rail 走 LeftRailWithDivider。
 */
@Composable
internal fun DensePanelRail(
    actions: List<ShellAction>,
    /** 整命令或末段身份均可命中（聊天传整命令 "chat:WORLD:"，sheet 传段值 "all"）。 */
    selectedId: String,
    onActionClick: (ShellAction) -> Unit,
    modifier: Modifier = Modifier,
    unreadCommands: Set<String> = emptySet(),
    railWidth: Dp = ShellChrome.PopupRailWidth,
) {
    Column(
        modifier =
            modifier
                .width(railWidth)
                .testTag(DENSE_PANEL_RAIL_TAG)
                .fillMaxHeight()
                .verticalScroll(rememberScrollState()),
        verticalArrangement = Arrangement.spacedBy(2.dp),
    ) {
        actions.forEach { action ->
            val selected =
                action.command == selectedId ||
                    action.command.substringAfterLast('/', missingDelimiterValue = "") == selectedId
            Box(
                modifier =
                    Modifier
                        .fillMaxWidth()
                        .height(DENSE_PANEL_RAIL_ITEM_HEIGHT)
                        .clip(UnifiedPopupControlShape)
                        .background(
                            if (selected) {
                                ShellPalette.CanvasLighter.copy(
                                    alpha = ShellChrome.SketchSelectedFillAlpha,
                                )
                            } else {
                                Color.Transparent
                            },
                        )
                        .border(
                            width = ShellChrome.DividerThickness,
                            color =
                                if (selected) {
                                    ShellPalette.Accent.copy(
                                        alpha = 0.5f,
                                    )
                                } else {
                                    ShellLines.StructuralDivider
                                },
                            shape = UnifiedPopupControlShape,
                        )
                        .clickable { onActionClick(action) }
                        .semantics {
                            role = Role.Tab
                            this.selected = selected
                        },
                contentAlignment = Alignment.CenterStart,
            ) {
                Row(
                    modifier =
                        Modifier
                            .fillMaxWidth()
                            .padding(horizontal = 6.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(4.dp),
                ) {
                    Text(
                        text = action.label,
                        color = if (selected) ShellPalette.Paper else ShellPalette.PaperMuted,
                        style =
                            MaterialTheme.typography.labelMedium.copy(
                                fontSize = 12.sp,
                                fontWeight = if (selected) FontWeight.SemiBold else FontWeight.Normal,
                            ),
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                        textAlign = TextAlign.Center,
                        modifier = Modifier.weight(1f),
                    )
                    if (!selected && action.command in unreadCommands) {
                        Box(
                            modifier =
                                Modifier
                                    .width(5.dp)
                                    .height(5.dp)
                                    .background(
                                        ShellPalette.Accent,
                                        androidx.compose.foundation.shape.CircleShape,
                                    ),
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun DensePanelGrid(
    panelMode: ShellPanelMode,
    cards: List<ShellPanelCard>,
    items: List<ShellSheetItem>,
    emptyText: String,
    useQuestTaskRows: Boolean,
    useNpcQuestTaskRows: Boolean,
    onActionClick: (ShellAction) -> Unit,
    onItemClick: (ShellSheetItem) -> Unit,
    enabled: Boolean,
    isDockInventoryGrid: Boolean = false,
    modifier: Modifier = Modifier,
) {
    val gridState = rememberLazyGridState()
    if (cards.isEmpty() && items.isNotEmpty()) {
        DensePanelList(
            items = items,
            useQuestTaskRows = useQuestTaskRows,
            useNpcQuestTaskRows = useNpcQuestTaskRows,
            onItemClick = onItemClick,
            enabled = enabled,
            modifier = modifier,
        )
        return
    }

    if (cards.isEmpty()) {
        if (emptyText.isNotBlank()) {
            ShellEmpty(
                text = emptyText,
                modifier =
                    modifier
                        .fillMaxWidth()
                        .heightIn(min = 48.dp),
            )
        }
        return
    }

    val columnCount =
        when {
            // 背包 dock 面板走左侧竖列分类（rail 占位约 62dp），3 列保证物品名可读；
            // 其余 INVENTORY_GRID（给予等）保持 6 列紧凑布局。
            panelMode == ShellPanelMode.INVENTORY_GRID && isDockInventoryGrid -> DOCK_INVENTORY_GRID_COLUMNS
            panelMode == ShellPanelMode.INVENTORY_GRID -> INVENTORY_GRID_COLUMNS
            panelMode == ShellPanelMode.SKILL_GRID -> SKILL_GRID_COLUMNS
            panelMode == ShellPanelMode.COMBAT_GRID -> COMBAT_GRID_COLUMNS
            panelMode == ShellPanelMode.SHOP_GRID -> SHOP_GRID_COLUMNS
            else -> DEFAULT_GRID_COLUMNS
        }

    LazyVerticalGrid(
        columns = GridCells.Fixed(columnCount),
        state = gridState,
        modifier = modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(2.dp),
        verticalArrangement = Arrangement.spacedBy(2.dp),
        contentPadding = PaddingValues(bottom = 2.dp),
    ) {
        val isSkillGrid = panelMode == ShellPanelMode.SKILL_GRID
        val equippedCards =
            if (isDockInventoryGrid) {
                cards.filter {
                    it.metadata["state"] == "已装备"
                }
            } else {
                emptyList()
            }
        val highlightedSkillCards = if (isSkillGrid) cards.filter { it.highlighted } else emptyList()
        val regularCards =
            if (isDockInventoryGrid) {
                cards.filterNot { it.metadata["state"] == "已装备" }
            } else if (isSkillGrid) {
                cards.filterNot { it.highlighted }
            } else {
                cards
            }
        gridItemsIndexed(
            items = equippedCards,
            key = { index, card -> "card:$index:${card.id}:${card.command}" },
        ) { _, card ->
            DockInventoryCard(
                card = card,
                onActionClick = onActionClick,
                enabled = enabled,
            )
        }
        gridItemsIndexed(
            items = highlightedSkillCards,
            key = { index, card -> "card:highlighted:$index:${card.id}:${card.command}" },
        ) { _, card ->
            SkillGridCard(
                card = card,
                onActionClick = onActionClick,
                enabled = enabled,
            )
        }
        if (regularCards.isNotEmpty()) {
            gridItemsIndexed(
                items = regularCards,
                key = { index, card -> "card:regular:$index:${card.id}:${card.command}" },
            ) { _, card ->
                when {
                    isDockInventoryGrid ->
                        DockInventoryCard(
                            card = card,
                            onActionClick = onActionClick,
                            enabled = enabled,
                        )

                    isSkillGrid ->
                        SkillGridCard(
                            card = card,
                            onActionClick = onActionClick,
                            enabled = enabled,
                        )

                    else ->
                        DensePanelCard(
                            panelMode = panelMode,
                            card = card,
                            onActionClick = onActionClick,
                            enabled = enabled,
                        )
                }
            }
        }
    }
}

@Composable
private fun DensePanelList(
    items: List<ShellSheetItem>,
    useQuestTaskRows: Boolean,
    useNpcQuestTaskRows: Boolean,
    onItemClick: (ShellSheetItem) -> Unit,
    enabled: Boolean,
    modifier: Modifier = Modifier,
) {
    val listState = rememberLazyListState()
    LazyColumn(
        state = listState,
        modifier = modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(8.dp),
        contentPadding = PaddingValues(bottom = 8.dp),
    ) {
        itemsIndexed(
            items = items,
            key = { index, item -> "item:$index:${item.title}:${item.trailing}:${item.command}" },
        ) { index, item ->
            if (useQuestTaskRows) {
                QuestTaskRow(
                    item = item,
                    onClick = { onItemClick(item) },
                    enabled = enabled,
                    exposeFamilyTag = index == 0,
                )
            } else if (useNpcQuestTaskRows) {
                NpcQuestTaskRow(
                    item = item,
                    onClick = { onItemClick(item) },
                    onActionClick = { onItemClick(item.asNpcQuestActionItem()) },
                    enabled = enabled,
                    exposeFamilyTag = index == 0,
                )
            } else {
                SheetRow(
                    item = item,
                    enabled = enabled || item.command.startsWith("ui://", ignoreCase = true),
                    onClick = { onItemClick(item) },
                )
            }
        }
    }
}

@Composable
private fun DensePanelCard(
    panelMode: ShellPanelMode,
    card: ShellPanelCard,
    onActionClick: (ShellAction) -> Unit,
    enabled: Boolean,
) {
    val isCompactGrid = panelMode == ShellPanelMode.INVENTORY_GRID
    val isCombatGrid = panelMode == ShellPanelMode.COMBAT_GRID
    Box(
        modifier =
            Modifier
                .fillMaxWidth()
                .testTag("$DENSE_PANEL_CARD_TAG_PREFIX${card.id}")
                .semantics { selected = false }
                .then(
                    if (isCompactGrid || isCombatGrid) {
                        Modifier.height(DENSE_PANEL_CARD_HEIGHT)
                    } else {
                        Modifier.heightIn(min = DENSE_PANEL_CARD_HEIGHT)
                    },
                )
                .sheetTextBoxChrome(highlighted = false)
                .then(
                    if ((!enabled && !card.command.startsWith("ui://", ignoreCase = true)) ||
                        (card.command.isBlank() && card.longClickCommand.isBlank())
                    ) {
                        Modifier
                    } else {
                        Modifier.pointerInput(card.command, card.longClickCommand) {
                            detectTapGestures(
                                onTap = {
                                    if (card.command.isNotBlank()) {
                                        onActionClick(
                                            ShellAction(
                                                label = card.title,
                                                command = card.command,
                                                dismissAfterInvoke = card.dismissAfterInvoke,
                                            ),
                                        )
                                    }
                                },
                                onLongPress = {
                                    if (card.longClickCommand.isNotBlank()) {
                                        onActionClick(
                                            ShellAction(
                                                label = card.title,
                                                command = card.longClickCommand,
                                                dismissAfterInvoke = false,
                                            ),
                                        )
                                    }
                                },
                            )
                        }
                    },
                ),
        contentAlignment = Alignment.Center,
    ) {
        Column(
            modifier =
                Modifier
                    .fillMaxWidth()
                    .padding(
                        horizontal = if (isCompactGrid || isCombatGrid) 4.dp else 5.dp,
                        vertical = if (isCompactGrid || isCombatGrid) 4.dp else 5.dp,
                    ),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement =
                Arrangement.spacedBy(
                    if (isCompactGrid || isCombatGrid) 3.dp else 4.dp,
                    Alignment.CenterVertically,
                ),
        ) {
            if (card.badges.isNotEmpty()) {
                Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    card.badges.forEachIndexed { index, badge ->
                        UnifiedPopupMetaText(text = badge, accent = index == 0)
                    }
                }
            }
            Text(
                text = card.title,
                color = ShellPalette.Paper,
                style =
                    if (isCompactGrid || isCombatGrid) {
                        MaterialTheme.typography.titleSmall.copy(
                            fontWeight = FontWeight.SemiBold,
                            lineHeight = 17.sp,
                        )
                    } else {
                        MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.SemiBold)
                    },
                maxLines = if (isCompactGrid || isCombatGrid) 2 else Int.MAX_VALUE,
                textAlign = TextAlign.Center,
                modifier = Modifier.fillMaxWidth(),
            )
            if (card.bottomLabel.isNotBlank()) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.Center,
                ) {
                    Text(
                        text = card.bottomLabel,
                        color = ShellPalette.Accent,
                        style =
                            if (isCompactGrid || isCombatGrid) {
                                MaterialTheme.typography.labelMedium
                            } else {
                                MaterialTheme.typography.labelLarge
                            },
                        fontWeight = FontWeight.Medium,
                        textAlign = TextAlign.Center,
                    )
                }
            }
        }
    }
}

@Composable
private fun DockInventoryCard(
    card: ShellPanelCard,
    onActionClick: (ShellAction) -> Unit,
    enabled: Boolean,
) {
    val equipped = card.metadata["state"] == "已装备"
    val (onClick, onLongClick) = resolveSingleLineCardActions(card, enabled, onActionClick)
    SingleLineGridCard(
        title = card.title,
        highlighted = equipped,
        selected = false,
        onClickLabel = "查看${card.title}",
        onClick = onClick,
        onLongClick = onLongClick,
        testTag = "$DENSE_PANEL_CARD_TAG_PREFIX${card.id}",
    )
}

/** 技能网格单行卡：高亮同时驱动 chrome/文字色与 selected 语义。 */
@Composable
private fun SkillGridCard(
    card: ShellPanelCard,
    onActionClick: (ShellAction) -> Unit,
    enabled: Boolean,
) {
    val (onClick, onLongClick) = resolveSingleLineCardActions(card, enabled, onActionClick)
    SingleLineGridCard(
        title = card.title,
        highlighted = card.highlighted,
        selected = card.highlighted,
        onClickLabel = null,
        onClick = onClick,
        onLongClick = onLongClick,
        testTag = "$DENSE_PANEL_CARD_TAG_PREFIX${card.id}",
    )
}

/**
 * 单行卡交互策略单源：ui:// 本地路由在 loading 期间保持可点；
 * 空白命令过滤；返回 (onClick, onLongClick)，null 表示该方向不可交互。
 */
private fun resolveSingleLineCardActions(
    card: ShellPanelCard,
    enabled: Boolean,
    onActionClick: (ShellAction) -> Unit,
): Pair<(() -> Unit)?, (() -> Unit)?> {
    val allowsLocalNavigationWhileLoading = card.command.startsWith("ui://", ignoreCase = true)
    val interactionEnabled =
        (enabled || allowsLocalNavigationWhileLoading) &&
            (card.command.isNotBlank() || card.longClickCommand.isNotBlank())
    val onClick =
        if (interactionEnabled && card.command.isNotBlank()) {
            {
                onActionClick(
                    ShellAction(
                        label = card.title,
                        command = card.command,
                        dismissAfterInvoke = card.dismissAfterInvoke,
                    ),
                )
            }
        } else {
            null
        }
    val onLongClick =
        if (interactionEnabled && card.longClickCommand.isNotBlank()) {
            {
                onActionClick(
                    ShellAction(
                        label = card.title,
                        command = card.longClickCommand,
                        dismissAfterInvoke = false,
                    ),
                )
            }
        } else {
            null
        }
    return onClick to onLongClick
}

/**
 * 单行「名字[数值]」网格卡：技能（名字[等级]）与 dock 背包（名字[数量]）共用，
 * 参数统一（40dp 高 / 9–10sp 自适应字号 / 1dp 水平 padding），与左侧分类标签等高。
 * highlighted 承载 chrome 高亮（背包已装备/技能已激发同款）；selected 仅驱动 a11y 语义。
 */
@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun SingleLineGridCard(
    title: String,
    highlighted: Boolean,
    selected: Boolean,
    onClickLabel: String?,
    onClick: (() -> Unit)?,
    onLongClick: (() -> Unit)?,
    testTag: String,
) {
    Box(
        contentAlignment = Alignment.Center,
        modifier =
            Modifier
                .fillMaxWidth()
                .height(DENSE_PANEL_SINGLE_LINE_CARD_HEIGHT)
                .padding(horizontal = 1.dp)
                .testTag(testTag)
                .semantics { this.selected = selected }
                .sheetTextBoxChrome(highlighted = highlighted)
                .then(
                    when {
                        onClick != null ->
                            Modifier.combinedClickable(
                                interactionSource = remember { MutableInteractionSource() },
                                indication = null,
                                role = Role.Button,
                                onClickLabel = onClickLabel,
                                onLongClick = onLongClick,
                                onClick = onClick,
                            )

                        onLongClick != null ->
                            Modifier.pointerInput(onLongClick) {
                                detectTapGestures(
                                    onTap = null,
                                    onLongPress = { onLongClick() },
                                )
                            }

                        else -> Modifier
                    },
                ),
    ) {
        // 运行时自适应字号：Robolectric 快照与真机字体度量不同（真机已实测
        // 固定字号截断），固定字号必然有一端截断；按实际度量收缩到不省略。
        AdaptiveSingleLineText(
            text = title,
            color = ShellPalette.Paper,
            style =
                MaterialTheme.typography.titleMedium.copy(
                    fontSize = 10.sp,
                    fontWeight = FontWeight.SemiBold,
                    lineHeight = 12.5.sp,
                ),
            textAlign = TextAlign.Center,
            minFontSize = 9.sp,
            modifier = Modifier.fillMaxWidth(),
        )
    }
}
