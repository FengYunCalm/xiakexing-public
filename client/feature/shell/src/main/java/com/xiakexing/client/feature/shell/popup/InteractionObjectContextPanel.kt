package com.xiakexing.client.feature.shell.popup

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.graphics.RectangleShape
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.zIndex
import com.xiakexing.client.feature.shell.InteractionObjectKind
import com.xiakexing.client.feature.shell.ShellAction
import com.xiakexing.client.feature.shell.ShellSheetState
import com.xiakexing.client.feature.shell.component.ShellSurfaceHeader
import com.xiakexing.client.feature.shell.component.UnifiedButtonSurface
import com.xiakexing.client.feature.shell.component.narrativeOverlayEdges
import com.xiakexing.client.feature.shell.ui.ShellChrome
import com.xiakexing.client.feature.shell.ui.ShellLines
import com.xiakexing.client.feature.shell.ui.ShellPalette
import com.xiakexing.client.feature.shell.ui.ShellType
import com.xiakexing.client.feature.shell.ui.shellEnterTransition
import com.xiakexing.client.feature.shell.ui.shellExitTransition
import kotlin.math.max

internal const val PERSON_CONTEXT_PANEL_TAG = "person-context-panel"
internal const val PERSON_CONTEXT_DESCRIPTION_TAG = "person-context-description"
internal const val PERSON_CONTEXT_TOPIC_AREA_TAG = "person-context-topic-area"
internal const val PERSON_CONTEXT_ACTION_AREA_TAG = "person-context-action-area"
internal const val PERSON_CONTEXT_TOPIC_TAG_PREFIX = "person-context-topic-"
internal const val PERSON_CONTEXT_ACTION_TAG_PREFIX = "person-context-action-"
internal const val ITEM_CONTEXT_PANEL_TAG = "item-context-panel"
internal const val ITEM_CONTEXT_DESCRIPTION_TAG = "item-context-description"
internal const val ITEM_CONTEXT_ACTION_TAG_PREFIX = "item-context-action-"
internal const val CONTEXT_PANEL_CLOSE_TAG = "interaction-context-close"
internal const val CONTEXT_PANEL_BOTTOM_BORDER_TAG = "interaction-context-bottom-border"

private const val PERSON_ACTION_COLUMNS = 3
private const val ITEM_ACTION_COLUMNS = 3
private const val PERSON_ACTION_AREA_MAX_HEIGHT_FRACTION = 0.56f
private const val PERSON_DESCRIPTION_MAX_HEIGHT_FRACTION = 0.32f
private const val ITEM_ACTION_AREA_MAX_HEIGHT_FRACTION = 0.58f
private const val ITEM_DESCRIPTION_MAX_HEIGHT_FRACTION = 0.34f
private val ContextActionHeight = 38.dp
private val ContextActionSpacing = 4.dp

@Composable
internal fun InteractionObjectContextPanelOverlay(
    sheet: ShellSheetState?,
    onDismiss: () -> Unit,
    onOutsideDismiss: () -> Unit = onDismiss,
    onBack: () -> Unit = {},
    onActionClick: (ShellAction) -> Unit,
    modifier: Modifier = Modifier,
) {
    BoxWithConstraints(modifier = modifier.fillMaxSize()) {
        if (sheet != null) {
            Box(
                modifier =
                    Modifier
                        .fillMaxSize()
                        .clickable(
                            interactionSource = remember { MutableInteractionSource() },
                            indication = null,
                            onClick = onOutsideDismiss,
                        ),
            )
        }
        AnimatedVisibility(
            visible = sheet != null,
            enter = shellEnterTransition(offsetFraction = -0.08f, durationMs = 180),
            exit = shellExitTransition(offsetFraction = -0.06f, durationMs = 150),
            modifier =
                Modifier
                    .align(Alignment.TopCenter)
                    .fillMaxWidth()
                    .zIndex(1f),
        ) {
            sheet?.let { visibleSheet ->
                when (visibleSheet.interactionTargetKind) {
                    InteractionObjectKind.NPC ->
                        PersonContextPanel(
                            sheet = visibleSheet,
                            maxPanelHeight = maxHeight,
                            onDismiss = onDismiss,
                            onBack = onBack,
                            onActionClick = onActionClick,
                        )
                    InteractionObjectKind.ITEM,
                    InteractionObjectKind.SCENE,
                    null,
                    ->
                        ItemContextPanel(
                            sheet = visibleSheet,
                            maxPanelHeight = maxHeight,
                            onDismiss = onDismiss,
                            onBack = onBack,
                            onActionClick = onActionClick,
                        )
                }
            }
        }
    }
}

@Composable
internal fun PersonContextPanel(
    sheet: ShellSheetState,
    maxPanelHeight: Dp,
    onDismiss: () -> Unit,
    onBack: () -> Unit = {},
    onActionClick: (ShellAction) -> Unit,
    modifier: Modifier = Modifier,
) {
    val sections = remember(sheet) { sheet.npcRootActionSections() }
    val topics = sections.topicActions
    val actions =
        remember(sections) {
            (sections.guidanceActions + sections.baseActions + sections.extensionActions)
                .distinctContextActions()
        }
    val actionRows = remember(actions) { actions.chunked(PERSON_ACTION_COLUMNS) }
    val actionAreaHeight =
        contextActionAreaHeight(
            itemCount = max(topics.size, actionRows.size),
            maxHeight = maxPanelHeight * PERSON_ACTION_AREA_MAX_HEIGHT_FRACTION,
        )
    val descriptionLines = sheet.contextDescriptionLines()

    ContextPanelFrame(
        title = sheet.title,
        maxPanelHeight = maxPanelHeight,
        onDismiss = onDismiss,
        onBack = onBack.takeIf { sheet.navigation.depth > 0 },
        modifier = modifier.testTag(PERSON_CONTEXT_PANEL_TAG),
    ) {
        ContextDescription(
            lines = descriptionLines,
            maxHeight = maxPanelHeight * PERSON_DESCRIPTION_MAX_HEIGHT_FRACTION,
            testTag = PERSON_CONTEXT_DESCRIPTION_TAG,
        )
        if (actionAreaHeight > 0.dp) {
            if (topics.isNotEmpty()) {
                Row(
                    modifier =
                        Modifier
                            .fillMaxWidth()
                            .height(actionAreaHeight)
                            .padding(horizontal = 6.dp, vertical = 4.dp),
                ) {
                    LazyColumn(
                        modifier =
                            Modifier
                                .weight(1f)
                                .fillMaxHeight()
                                .testTag(PERSON_CONTEXT_TOPIC_AREA_TAG)
                                .clipToBounds(),
                        verticalArrangement = Arrangement.spacedBy(ContextActionSpacing),
                    ) {
                        itemsIndexed(
                            items = topics,
                            key = {
                                    index,
                                    action,
                                ->
                                "topic:$index:${action.command}:${action.label}"
                            },
                        ) { _, action ->
                            ContextActionButton(
                                action = action,
                                onClick = { onActionClick(action) },
                                modifier =
                                    Modifier
                                        .fillMaxWidth()
                                        .height(ContextActionHeight)
                                        .testTag(
                                            "$PERSON_CONTEXT_TOPIC_TAG_PREFIX${action.interactionShortLabelForDisplay()}",
                                        ),
                            )
                        }
                    }
                    Spacer(modifier = Modifier.width(8.dp))
                    PersonActionGrid(
                        actionRows = actionRows,
                        onActionClick = onActionClick,
                        modifier =
                            Modifier
                                .weight(3f)
                                .fillMaxHeight(),
                    )
                }
            } else {
                PersonActionGrid(
                    actionRows = actionRows,
                    onActionClick = onActionClick,
                    modifier =
                        Modifier
                            .fillMaxWidth()
                            .height(actionAreaHeight)
                            .padding(horizontal = 6.dp, vertical = 4.dp),
                )
            }
        }
    }
}

@Composable
internal fun ItemContextPanel(
    sheet: ShellSheetState,
    maxPanelHeight: Dp,
    onDismiss: () -> Unit,
    onBack: () -> Unit = {},
    onActionClick: (ShellAction) -> Unit,
    modifier: Modifier = Modifier,
) {
    val actions = remember(sheet) { sheet.contextItemActions() }
    val actionRows = remember(actions) { actions.chunked(ITEM_ACTION_COLUMNS) }
    val actionAreaHeight =
        contextActionAreaHeight(
            itemCount = actionRows.size,
            maxHeight = maxPanelHeight * ITEM_ACTION_AREA_MAX_HEIGHT_FRACTION,
        )
    ContextPanelFrame(
        title = sheet.title,
        maxPanelHeight = maxPanelHeight,
        onDismiss = onDismiss,
        onBack = onBack.takeIf { sheet.navigation.depth > 0 },
        modifier = modifier.testTag(ITEM_CONTEXT_PANEL_TAG),
    ) {
        ContextDescription(
            lines = sheet.contextDescriptionLines(),
            maxHeight = maxPanelHeight * ITEM_DESCRIPTION_MAX_HEIGHT_FRACTION,
            testTag = ITEM_CONTEXT_DESCRIPTION_TAG,
        )
        if (actionAreaHeight > 0.dp) {
            LazyColumn(
                modifier =
                    Modifier
                        .fillMaxWidth()
                        .height(actionAreaHeight)
                        .padding(horizontal = 6.dp, vertical = 4.dp)
                        .clipToBounds(),
                verticalArrangement = Arrangement.spacedBy(ContextActionSpacing),
            ) {
                itemsIndexed(
                    items = actionRows,
                    key = { index, row -> "item-row:$index:${row.joinToString { it.command }}" },
                ) { _, rowActions ->
                    Row(
                        modifier =
                            Modifier
                                .fillMaxWidth()
                                .height(ContextActionHeight),
                        horizontalArrangement = Arrangement.spacedBy(ContextActionSpacing),
                    ) {
                        rowActions.forEach { action ->
                            ContextActionButton(
                                action = action,
                                onClick = { onActionClick(action) },
                                modifier =
                                    Modifier
                                        .weight(1f)
                                        .fillMaxHeight()
                                        .testTag(
                                            "$ITEM_CONTEXT_ACTION_TAG_PREFIX${action.interactionShortLabelForDisplay()}",
                                        ),
                            )
                        }
                        repeat(ITEM_ACTION_COLUMNS - rowActions.size) {
                            Spacer(modifier = Modifier.weight(1f).fillMaxHeight())
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun PersonActionGrid(
    actionRows: List<List<ShellAction>>,
    onActionClick: (ShellAction) -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier =
            modifier
                .testTag(PERSON_CONTEXT_ACTION_AREA_TAG)
                .clipToBounds(),
        verticalArrangement = Arrangement.spacedBy(ContextActionSpacing),
    ) {
        actionRows.forEach { rowActions ->
            Row(
                modifier =
                    Modifier
                        .fillMaxWidth()
                        .height(ContextActionHeight),
                horizontalArrangement = Arrangement.spacedBy(ContextActionSpacing),
            ) {
                rowActions.forEach { action ->
                    ContextActionButton(
                        action = action,
                        onClick = { onActionClick(action) },
                        modifier =
                            Modifier
                                .weight(1f)
                                .fillMaxHeight()
                                .testTag(
                                    "$PERSON_CONTEXT_ACTION_TAG_PREFIX${action.interactionShortLabelForDisplay()}",
                                ),
                    )
                }
                repeat(PERSON_ACTION_COLUMNS - rowActions.size) {
                    Spacer(modifier = Modifier.weight(1f).fillMaxHeight())
                }
            }
        }
    }
}

@Composable
private fun ContextPanelFrame(
    title: String,
    maxPanelHeight: Dp,
    onDismiss: () -> Unit,
    onBack: (() -> Unit)?,
    modifier: Modifier = Modifier,
    content: @Composable () -> Unit,
) {
    Column(
        modifier =
            modifier
                .fillMaxWidth()
                .heightIn(max = maxPanelHeight)
                .background(ShellPalette.ChatSurface)
                .narrativeOverlayEdges(drawLeft = false)
                .clickable(
                    interactionSource = remember { MutableInteractionSource() },
                    indication = null,
                    onClick = {},
                ),
    ) {
        ShellSurfaceHeader(
            title = title,
            onBack = onBack,
            onClose = onDismiss,
            closeContentDescription = "关闭交互弹窗",
            closeTestTag = CONTEXT_PANEL_CLOSE_TAG,
        )
        content()
        Spacer(modifier = Modifier.height(ShellChrome.PopupFooterBottomInset))
        Spacer(
            modifier =
                Modifier
                    .fillMaxWidth()
                    .height(ShellChrome.DividerThickness)
                    .background(ShellLines.StructuralDivider)
                    .testTag(CONTEXT_PANEL_BOTTOM_BORDER_TAG),
        )
    }
}

@Composable
private fun ContextDescription(
    lines: List<String>,
    maxHeight: Dp,
    testTag: String,
) {
    if (lines.isEmpty()) {
        return
    }
    Column(
        modifier =
            Modifier
                .fillMaxWidth()
                .heightIn(max = maxHeight)
                .padding(horizontal = 8.dp, vertical = 3.dp)
                .verticalScroll(rememberScrollState())
                .testTag(testTag),
        verticalArrangement = Arrangement.spacedBy(2.dp),
    ) {
        lines.forEach { line ->
            Text(
                text = line,
                color = ShellPalette.PaperMuted,
                style = ShellType.Narrative.copy(fontSize = 12.sp, lineHeight = 18.sp),
            )
        }
    }
}

@Composable
private fun ContextActionButton(
    action: ShellAction,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    UnifiedButtonSurface(
        modifier = modifier,
        onClick = onClick,
        minHeight = ContextActionHeight,
        horizontalPadding = 4.dp,
        verticalPadding = 0.dp,
        fillColor = ShellPalette.CanvasRaised.copy(alpha = ShellChrome.MainControlFillAlpha),
        borderColor = ShellLines.ControlStateBorder,
        shape = RectangleShape,
    ) {
        Text(
            text = action.interactionShortLabelForDisplay(),
            color = ShellPalette.Paper,
            style = MaterialTheme.typography.labelMedium.copy(fontSize = 13.sp, lineHeight = 16.sp),
            fontWeight = FontWeight.Medium,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            textAlign = TextAlign.Center,
        )
    }
}

private fun contextActionAreaHeight(
    itemCount: Int,
    maxHeight: Dp,
): Dp {
    if (itemCount <= 0) {
        return 0.dp
    }
    val desiredHeight = ContextActionHeight * itemCount + ContextActionSpacing * (itemCount - 1) + 8.dp
    return minOf(desiredHeight, maxHeight)
}

private fun ShellSheetState.contextDescriptionLines(): List<String> {
    return interactionLines.ifEmpty { detailLines }
        .map { line -> line.trim() }
        .filter { line -> line.isNotBlank() }
}

private fun ShellSheetState.contextItemActions(): List<ShellAction> {
    val activeGroup = activeActionGroupId.ifBlank { "root" }
    val groupedActions =
        actionGroups
            .filter { group -> group.id == activeGroup || group.surface == activeGroup }
            .flatMap { group -> group.actions }
    return actions.ifEmpty { groupedActions }.distinctContextActions()
}

private fun List<ShellAction>.distinctContextActions(): List<ShellAction> {
    val seen = linkedSetOf<String>()
    return filter { action ->
        val command = action.command.trim().lowercase()
        val label = action.interactionShortLabelForDisplay().trim()
        seen.add(command.ifBlank { label })
    }
}
