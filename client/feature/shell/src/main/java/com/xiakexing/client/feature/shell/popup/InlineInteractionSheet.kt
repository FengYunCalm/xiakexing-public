package com.xiakexing.client.feature.shell.popup

import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.paneTitle
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.zIndex
import com.xiakexing.client.feature.shell.InteractionObjectKind
import com.xiakexing.client.feature.shell.InteractionSheetMode
import com.xiakexing.client.feature.shell.PopupSource
import com.xiakexing.client.feature.shell.ShellAction
import com.xiakexing.client.feature.shell.ShellPanelMode
import com.xiakexing.client.feature.shell.ShellSheetEmptyPresentation
import com.xiakexing.client.feature.shell.ShellSheetItem
import com.xiakexing.client.feature.shell.ShellSheetState
import com.xiakexing.client.feature.shell.component.ShellEmpty
import com.xiakexing.client.feature.shell.component.ShellSurfaceHeader
import com.xiakexing.client.feature.shell.component.SurfaceHeaderCloseButton
import com.xiakexing.client.feature.shell.component.narrativeOverlayEdges
import com.xiakexing.client.feature.shell.isMailDetailSheet
import com.xiakexing.client.feature.shell.isNpcNarrativeConversationSheet
import com.xiakexing.client.feature.shell.isNpcQuestDetailSheet
import com.xiakexing.client.feature.shell.isNpcQuestInteractionSheet
import com.xiakexing.client.feature.shell.isNpcQuestListSheet
import com.xiakexing.client.feature.shell.isQuestDetailSheet
import com.xiakexing.client.feature.shell.isQuestListSheet
import com.xiakexing.client.feature.shell.ui.ShellChrome
import com.xiakexing.client.feature.shell.ui.ShellPalette

const val INLINE_INTERACTION_SHEET_DISMISS_AREA_TAG = "inline_interaction_sheet_dismiss_area"
const val INLINE_INTERACTION_SHEET_HEADER_TAG = "inline_interaction_sheet_header"
const val INLINE_INTERACTION_SHEET_HEADER_BACK_TAG = "inline_interaction_sheet_header_back"
const val INLINE_INTERACTION_SHEET_HEADER_TITLE_TAG = "inline_interaction_sheet_header_title"
const val INLINE_INTERACTION_SHEET_HEADER_CLOSE_TAG = "inline_interaction_sheet_header_close"

private val INLINE_SHEET_FUNCTION_HORIZONTAL_PADDING = 5.dp
private val INLINE_SHEET_PANEL_HORIZONTAL_PADDING = 8.dp
private val INLINE_SHEET_PLAIN_HORIZONTAL_PADDING = 7.dp
private val INLINE_SHEET_CONTENT_TOP_PADDING = 0.dp
private val INLINE_SHEET_FUNCTION_CONTENT_SPACING = 6.dp
private val INLINE_SHEET_DEFAULT_CONTENT_SPACING = 4.dp
private val INLINE_SHEET_FLOATING_CLOSE_TOP_INSET = 2.dp
private val NPC_NARRATIVE_BODY_MAX_HEIGHT = 188.dp

enum class InlineSheetLayoutMode {
    CONTEXT,
    FUNCTION_PAGE,
}

private data class InlineSheetPresentationState(
    val isInteractionSheet: Boolean,
    val isNpcInteractionSheet: Boolean,
    val isNpcRootMode: Boolean,
    val isNpcQuestPanel: Boolean,
    val isNpcSecondaryMode: Boolean,
    val isNpcNarrativeConversation: Boolean,
    val isGiveMode: Boolean,
    val hasDensePanel: Boolean,
    val densePanelFillsAvailable: Boolean,
    val npcSecondaryActions: List<ShellAction>,
    val hasNarrativeConversationBody: Boolean,
)

private fun ShellSheetState.toInlinePresentationState(): InlineSheetPresentationState {
    val visibleActionGroups = visibleInteractionActionGroups()
    val isInteractionSheet = source == PopupSource.INTERACTION_OBJECT
    val isNpcInteractionSheet = isInteractionSheet && interactionTargetKind == InteractionObjectKind.NPC
    val isNpcRootMode = isNpcInteractionSheet && isNpcRootMode()
    val isNpcSecondaryMode = isNpcInteractionSheet && !isNpcRootMode
    val isGiveMode = interactionMode == InteractionSheetMode.GIVE_ITEMS
    val npcSecondaryActions =
        if (isNpcSecondaryMode) {
            visibleActionGroups
                .filter { group -> group.id != "root" && group.surface != "root" }
                .flatMap { group -> group.actions }
        } else {
            emptyList()
        }
    return InlineSheetPresentationState(
        isInteractionSheet = isInteractionSheet,
        isNpcInteractionSheet = isNpcInteractionSheet,
        isNpcRootMode = isNpcRootMode,
        isNpcQuestPanel = isNpcQuestInteractionSheet(),
        isNpcSecondaryMode = isNpcSecondaryMode,
        isNpcNarrativeConversation = isNpcNarrativeConversationSheet(),
        isGiveMode = isGiveMode,
        hasDensePanel = panelMode != ShellPanelMode.NONE,
        densePanelFillsAvailable =
            !isQuestDetailSheet() && !isNpcQuestDetailSheet() && !isMailDetailSheet(),
        npcSecondaryActions = npcSecondaryActions,
        hasNarrativeConversationBody =
            interactionLines.isNotEmpty() || detailLines.isNotEmpty() ||
                (isGiveMode && items.isNotEmpty()),
    )
}

@Composable
fun InlineInteractionSheet(
    sheet: ShellSheetState,
    onDismiss: () -> Unit,
    onBack: () -> Unit = {},
    onActionClick: (ShellAction) -> Unit,
    onItemClick: (ShellSheetItem) -> Unit,
    layoutMode: InlineSheetLayoutMode = InlineSheetLayoutMode.CONTEXT,
    modifier: Modifier = Modifier,
    drawPanelChrome: Boolean = true,
) {
    val visibleActionGroups = remember(sheet) { sheet.visibleInteractionActionGroups() }
    val presentation = remember(sheet) { sheet.toInlinePresentationState() }
    val isFunctionPage = layoutMode == InlineSheetLayoutMode.FUNCTION_PAGE
    val npcRootSections =
        remember(sheet, presentation.isNpcRootMode) {
            if (presentation.isNpcRootMode) sheet.npcRootActionSections() else null
        }

    val sheetModifier =
        if (drawPanelChrome) {
            modifier
                .testTag(INLINE_INTERACTION_SHEET_DISMISS_AREA_TAG)
                .semantics { paneTitle = sheet.title.ifBlank { "操作面板" } }
                .unifiedPopupPanelChrome()
                .narrativeOverlayEdges()
        } else {
            modifier
                .testTag(INLINE_INTERACTION_SHEET_DISMISS_AREA_TAG)
                .semantics { paneTitle = sheet.title.ifBlank { "操作面板" } }
        }
    val contentPadding =
        when {
            isFunctionPage ->
                PaddingValues(
                    start = INLINE_SHEET_FUNCTION_HORIZONTAL_PADDING,
                    top = INLINE_SHEET_CONTENT_TOP_PADDING,
                    end = INLINE_SHEET_FUNCTION_HORIZONTAL_PADDING,
                    bottom = ShellChrome.PopupFooterBottomInset,
                )
            drawPanelChrome ->
                PaddingValues(
                    start = INLINE_SHEET_PANEL_HORIZONTAL_PADDING,
                    top = INLINE_SHEET_CONTENT_TOP_PADDING,
                    end = INLINE_SHEET_PANEL_HORIZONTAL_PADDING,
                    bottom = ShellChrome.PopupFooterBottomInset,
                )
            else ->
                PaddingValues(
                    start = INLINE_SHEET_PLAIN_HORIZONTAL_PADDING,
                    top = INLINE_SHEET_CONTENT_TOP_PADDING,
                    end = INLINE_SHEET_PLAIN_HORIZONTAL_PADDING,
                    bottom = ShellChrome.PopupFooterBottomInset,
                )
        }
    val contentSpacing =
        if (isFunctionPage) {
            INLINE_SHEET_FUNCTION_CONTENT_SPACING
        } else {
            INLINE_SHEET_DEFAULT_CONTENT_SPACING
        }
    val contentTopInset =
        if (sheet.showHeaderTitle) {
            ShellChrome.SurfaceHeaderHeight + contentSpacing
        } else {
            // 无标题 Sheet 的紧凑头部已在布局流内占位，内容不再需要顶部偏移。
            0.dp
        }

    val fillsAvailableHeight = !presentation.isNpcNarrativeConversation && !sheet.compactPanel
    val sheetContentModifier =
        if (fillsAvailableHeight) {
            Modifier.fillMaxSize()
        } else {
            Modifier.fillMaxWidth()
        }

    // #1 紧凑悬浮头：面板状态声明 floatingClose（如属性面板）时不再占 40dp 空头行，
    // 关闭按钮悬浮右上（触控层与几何与其他弹窗一致），内容直接贴顶布局。
    val floatingClose = sheet.floatingClose

    Box(
        modifier =
            sheetModifier.clickable(
                interactionSource = remember { MutableInteractionSource() },
                indication = null,
                onClick = {},
            ),
    ) {
        Box(modifier = sheetContentModifier) {
            if (floatingClose) {
                Box(
                    modifier =
                        Modifier
                            .align(Alignment.TopEnd)
                            .width(ShellChrome.SurfaceHeaderActionSize)
                            .height(ShellChrome.SurfaceHeaderContentHeight),
                    contentAlignment = Alignment.Center,
                ) {
                    SurfaceHeaderCloseButton(
                        onClick = onDismiss,
                        testTag = INLINE_INTERACTION_SHEET_HEADER_CLOSE_TAG,
                        contentDescriptionText = "关闭弹窗",
                        modifier =
                            Modifier
                                .width(ShellChrome.SurfaceHeaderActionSize)
                                .height(ShellChrome.SurfaceHeaderContentHeight),
                    )
                }
            }
            Column(modifier = sheetContentModifier) {
                if (!sheet.showHeaderTitle && !floatingClose) {
                    // 无标题头部不套水平 padding：返回/关闭图标与面板边缘距离
                    // 与其他弹窗（聊天等）统一为 13dp。
                    InlineInteractionSheetHeader(
                        sheet = sheet,
                        onBack = onBack,
                        onClose = onDismiss,
                    )
                }
                Column(
                    modifier =
                        Modifier
                            .fillMaxWidth()
                            .padding(contentPadding)
                            .padding(
                                top =
                                    when {
                                        sheet.showHeaderTitle -> contentTopInset
                                        floatingClose -> INLINE_SHEET_FLOATING_CLOSE_TOP_INSET
                                        else -> contentSpacing
                                    },
                            ),
                    verticalArrangement = Arrangement.spacedBy(contentSpacing),
                ) {
                    SheetFeedbackStatus(
                        sheet = sheet,
                        onRetry = { action -> onActionClick(action) },
                    )

                    if (presentation.hasDensePanel) {
                        DenseSheetPanel(
                            sheet = sheet,
                            onActionClick = onActionClick,
                            onItemClick = onItemClick,
                            layoutMode = layoutMode,
                            modifier =
                                when {
                                    fillsAvailableHeight && presentation.densePanelFillsAvailable ->
                                        Modifier
                                            .weight(1f, fill = true)
                                            .fillMaxWidth()
                                    fillsAvailableHeight && (sheet.isQuestDetailSheet() || sheet.isNpcQuestDetailSheet()) ->
                                        Modifier
                                            .weight(1f, fill = false)
                                            .fillMaxWidth()
                                    else -> Modifier.fillMaxWidth()
                                },
                        )
                    } else if (presentation.isNpcSecondaryMode && presentation.isNpcNarrativeConversation) {
                        if (presentation.hasNarrativeConversationBody) {
                            CompactNarrativeConversationBody(
                                sheet = sheet,
                                isGiveMode = presentation.isGiveMode,
                                onItemClick = onItemClick,
                                modifier =
                                    Modifier
                                        .fillMaxWidth(),
                            )
                        }
                        ConversationSecondaryActionRow(
                            actions = presentation.npcSecondaryActions,
                            onActionClick = onActionClick,
                        )
                    } else if (presentation.isNpcSecondaryMode) {
                        Column(
                            modifier =
                                Modifier
                                    .weight(1f, fill = false)
                                    .fillMaxWidth(),
                            verticalArrangement = Arrangement.spacedBy(6.dp),
                        ) {
                            InlineSheetBodyList(
                                sheet = sheet,
                                isInteractionSheet = presentation.isInteractionSheet,
                                isGiveMode = presentation.isGiveMode,
                                showInteractionSummary = sheet.interactionLines.isEmpty(),
                                boxInteractionLines = true,
                                bottomPadding = 0.dp,
                                onItemClick = onItemClick,
                                modifier =
                                    Modifier
                                        .weight(1f, fill = false)
                                        .fillMaxWidth(),
                            )
                            ConversationSecondaryActionRow(
                                actions = presentation.npcSecondaryActions,
                                onActionClick = onActionClick,
                            )
                        }
                    } else {
                        InlineSheetBodyList(
                            sheet = sheet,
                            isInteractionSheet = presentation.isInteractionSheet,
                            isGiveMode = presentation.isGiveMode,
                            showInteractionSummary = true,
                            onItemClick = onItemClick,
                            modifier =
                                Modifier
                                    .weight(
                                        1f,
                                        fill =
                                            sheet.isQuestListSheet() || sheet.isNpcQuestListSheet(),
                                    )
                                    .fillMaxWidth(),
                        )
                    }

                    InlineSheetActionFooter(
                        sheet = sheet,
                        isNpcInteractionSheet = presentation.isNpcInteractionSheet,
                        isNpcRootMode = presentation.isNpcRootMode,
                        isNpcQuestPanel = presentation.isNpcQuestPanel,
                        isInteractionSheet = presentation.isInteractionSheet,
                        visibleActionGroups = visibleActionGroups,
                        npcRootSections = npcRootSections,
                        onActionClick = onActionClick,
                    )
                }
            }
            if (sheet.showHeaderTitle) {
                InlineInteractionSheetHeader(
                    sheet = sheet,
                    onBack = onBack,
                    onClose = onDismiss,
                    modifier =
                        Modifier
                            .align(Alignment.TopCenter)
                            .zIndex(1f),
                )
            }
        }
    }
}

@Composable
private fun InlineInteractionSheetHeader(
    sheet: ShellSheetState,
    onBack: () -> Unit,
    onClose: () -> Unit,
    modifier: Modifier = Modifier,
) {
    ShellSurfaceHeader(
        title = sheet.title,
        showTitle = sheet.showHeaderTitle,
        onBack = onBack.takeIf { sheet.navigation.depth > 0 },
        onClose = onClose,
        modifier = modifier,
        closeContentDescription = "关闭弹窗",
        headerTestTag = INLINE_INTERACTION_SHEET_HEADER_TAG,
        backTestTag = INLINE_INTERACTION_SHEET_HEADER_BACK_TAG,
        titleTestTag = INLINE_INTERACTION_SHEET_HEADER_TITLE_TAG,
        closeTestTag = INLINE_INTERACTION_SHEET_HEADER_CLOSE_TAG,
        headerText = if (!sheet.showHeaderTitle) sheet.headerText else "",
    )
}

@Composable
private fun ConversationSecondaryActionRow(
    actions: List<ShellAction>,
    onActionClick: (ShellAction) -> Unit,
    modifier: Modifier = Modifier,
) {
    ConversationActionRows(
        actions = actions,
        onActionClick = onActionClick,
        modifier = modifier,
    )
}

@Composable
private fun CompactNarrativeConversationBody(
    sheet: ShellSheetState,
    isGiveMode: Boolean,
    onItemClick: (ShellSheetItem) -> Unit,
    modifier: Modifier = Modifier,
) {
    val scrollState = rememberScrollState()
    Column(
        modifier =
            modifier
                .heightIn(max = NPC_NARRATIVE_BODY_MAX_HEIGHT)
                .verticalScroll(scrollState),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        val narrativeLines = sheet.interactionLines.ifEmpty { sheet.detailLines }
        if (narrativeLines.isNotEmpty()) {
            InteractionNarrativeText(
                lines = narrativeLines,
                emphasized = sheet.interactionLines.isNotEmpty(),
                compact = true,
                npcDialogue =
                    sheet.interactionTargetKind == InteractionObjectKind.NPC &&
                        sheet.interactionLines.isNotEmpty() && !isGiveMode,
            )
        }
        if (isGiveMode) {
            if (sheet.items.isEmpty() && sheet.emptyPresentation == ShellSheetEmptyPresentation.MESSAGE) {
                ShellEmpty(
                    text = sheet.panelEmptyText.ifBlank { "背包里没有可给予的物品" },
                )
            } else {
                sheet.items.forEach { item ->
                    SheetRow(
                        item = item,
                        enabled = !sheet.loading || item.command.startsWith("ui://", ignoreCase = true),
                        onClick = { onItemClick(item) },
                    )
                }
            }
        }
    }
}

@Composable
private fun InlineSheetBodyList(
    sheet: ShellSheetState,
    isInteractionSheet: Boolean,
    isGiveMode: Boolean,
    showInteractionSummary: Boolean,
    onItemClick: (ShellSheetItem) -> Unit,
    modifier: Modifier = Modifier,
    boxInteractionLines: Boolean = false,
    bottomPadding: Dp = 4.dp,
) {
    LazyColumn(
        modifier = modifier,
        contentPadding = PaddingValues(top = 0.dp, bottom = bottomPadding),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        if (isInteractionSheet) {
            if (showInteractionSummary) {
                item(key = "interaction-summary") {
                    InteractionSummaryBlock(lines = sheet.detailLines)
                }
            }
            if (boxInteractionLines && sheet.interactionLines.isNotEmpty()) {
                item(key = "interaction-lines-box") {
                    InteractionSummaryBlock(lines = sheet.interactionLines, compact = true)
                }
            } else {
                items(sheet.interactionLines) { line ->
                    Text(
                        text = line,
                        color = ShellPalette.Paper,
                        style = MaterialTheme.typography.bodyMedium,
                    )
                }
            }
            if (isGiveMode) {
                if (sheet.items.isEmpty() && sheet.emptyPresentation == ShellSheetEmptyPresentation.MESSAGE) {
                    item(key = "empty-give") {
                        ShellEmpty(
                            text = sheet.panelEmptyText.ifBlank { "背包里没有可给予的物品" },
                        )
                    }
                } else {
                    items(sheet.items) { item ->
                        SheetRow(
                            item = item,
                            enabled = !sheet.loading || item.command.startsWith("ui://", ignoreCase = true),
                            onClick = { onItemClick(item) },
                        )
                    }
                }
            }
        } else {
            items(sheet.detailLines) { line ->
                DetailLine(text = line)
            }
            items(sheet.interactionLines) { line ->
                Text(
                    text = line,
                    color = ShellPalette.Paper,
                    style = MaterialTheme.typography.bodyMedium,
                )
            }
            items(sheet.items) { item ->
                SheetRow(
                    item = item,
                    enabled = !sheet.loading || item.command.startsWith("ui://", ignoreCase = true),
                    onClick = { onItemClick(item) },
                )
            }
        }
    }
}

@Composable
private fun InlineSheetActionFooter(
    sheet: ShellSheetState,
    isNpcInteractionSheet: Boolean,
    isNpcRootMode: Boolean,
    isNpcQuestPanel: Boolean,
    isInteractionSheet: Boolean,
    visibleActionGroups: List<com.xiakexing.client.feature.shell.ShellActionGroup>,
    npcRootSections: NpcRootActionSections?,
    onActionClick: (ShellAction) -> Unit,
) {
    val isActionEnabled: (ShellAction) -> Boolean = { action ->
        action.kind != "quest_pending" &&
            (
                !sheet.loading || action.kind.equals("back", ignoreCase = true) ||
                    action.command.startsWith("ui://", ignoreCase = true)
            )
    }
    if (isNpcInteractionSheet) {
        if (!isNpcRootMode) {
            if (isNpcQuestPanel) {
                ActionButtonRows(
                    actions = sheet.actions,
                    onActionClick = onActionClick,
                    isActionEnabled = isActionEnabled,
                )
            }
            return
        }

        if (npcRootSections?.isEmpty == false) {
            npcRootSections.let { sections ->
                NpcRootActionRows(
                    sections = sections,
                    onActionClick = onActionClick,
                    isActionEnabled = isActionEnabled,
                )
            }
        }
    } else if (isInteractionSheet && visibleActionGroups.isNotEmpty()) {
        ActionGroupRows(
            groups = visibleActionGroups,
            onActionClick = onActionClick,
            isActionEnabled = isActionEnabled,
        )
    } else {
        val footerActions =
            buildList {
                addAll(sheet.actions)
                sheet.primaryAction
                    ?.takeUnless {
                            action ->
                        sheet.actions.isNotEmpty() && action.label.trim() == "刷新"
                    }
                    ?.let(::add)
            }
        if (footerActions.isNotEmpty()) {
            ActionButtonRows(
                actions = footerActions,
                onActionClick = onActionClick,
                isActionEnabled = isActionEnabled,
            )
        }
    }
}
