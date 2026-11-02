package com.xiakexing.client.feature.shell.component

import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectTapGestures
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
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.ExperimentalComposeApi
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.NonSkippableComposable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextLayoutResult
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.xiakexing.client.core.protocol.payload.ExitSnapshot
import com.xiakexing.client.feature.shell.ExitOverlayState
import com.xiakexing.client.feature.shell.ExitOverlayType
import com.xiakexing.client.feature.shell.ShellAction
import com.xiakexing.client.feature.shell.TrackedQuestState
import com.xiakexing.client.feature.shell.toCanonicalTravelDirection
import com.xiakexing.client.feature.shell.ui.ShellChrome
import com.xiakexing.client.feature.shell.ui.ShellPalette
import com.xiakexing.client.feature.shell.ui.ShellType
import kotlinx.coroutines.delay

// 移动刷新修复（311414902）：同 SketchShellLayout，勿删。
@OptIn(ExperimentalComposeApi::class)
@NonSkippableComposable
@Composable
internal fun GridExitPanel(
    roomTitle: String,
    exits: List<ExitSnapshot>,
    onExitClick: (String) -> Unit,
    modifier: Modifier = Modifier,
    roomRefreshSerial: Long = 0L,
    trackedQuest: TrackedQuestState = TrackedQuestState(),
    currentRoomQuestAvailable: Boolean = false,
    outerPadding: Dp = 10.dp,
    gridSpacing: Dp = 8.dp,
    cellInset: Dp = 1.dp,
    outerCellInset: Dp = 2.dp,
) {
    val resolvedRoomTitle = roomTitle
    var moveFeedback by remember { mutableStateOf<ExitMoveFeedback?>(null) }
    var moveFeedbackToken by remember { mutableIntStateOf(0) }
    var pendingRevealMoves by remember { mutableIntStateOf(0) }
    val refreshContent =
        remember(resolvedRoomTitle, exits, roomRefreshSerial) {
            ExitGridRefreshContent(resolvedRoomTitle, exits, roomRefreshSerial)
        }
    var displayedContent by remember { mutableStateOf(refreshContent) }
    var revealRefreshSerial by remember { mutableStateOf<Long?>(null) }
    val refreshRevealProgress = remember { Animatable(1f) }

    fun shouldRevealNextGrid(nextRefreshSerial: Long): Boolean {
        if (pendingRevealMoves <= 0 || nextRefreshSerial == displayedContent.refreshSerial) {
            return false
        }
        pendingRevealMoves -= 1
        return true
    }

    fun startGridMove(
        direction: String,
        content: ExitGridRefreshContent,
    ) {
        val normalizedDirection = direction.trim().lowercase()
        if (normalizedDirection in gridDirectionSlots) {
            pendingRevealMoves += 1
        }
        moveFeedbackToken += 1
        moveFeedback =
            ExitMoveFeedback(
                direction = normalizedDirection,
                content = content,
                token = moveFeedbackToken,
            )
        onExitClick(normalizedDirection)
    }

    fun sendGridExit(direction: String) {
        val normalizedDirection = direction.trim().lowercase()
        startGridMove(normalizedDirection, displayedContent)
    }

    fun finishMoveFeedback(token: Int) {
        if (moveFeedback?.token != token) {
            return
        }
        moveFeedback = null
    }

    LaunchedEffect(moveFeedback?.token) {
        val token = moveFeedback?.token ?: return@LaunchedEffect
        delay(EXIT_MOVE_FEEDBACK_MS.toLong())
        finishMoveFeedback(token)
    }

    LaunchedEffect(refreshContent) {
        if (refreshContent != displayedContent) {
            if (shouldRevealNextGrid(refreshContent.refreshSerial)) {
                revealRefreshSerial = refreshContent.refreshSerial
            }
            displayedContent = refreshContent
        }
    }

    LaunchedEffect(displayedContent.refreshSerial, revealRefreshSerial) {
        if (revealRefreshSerial == displayedContent.refreshSerial) {
            refreshRevealProgress.snapTo(0f)
            refreshRevealProgress.animateTo(
                targetValue = 1f,
                animationSpec =
                    tween(
                        durationMillis = EXIT_GRID_REFRESH_REVEAL_MS,
                        easing = FastOutSlowInEasing,
                    ),
            )
            revealRefreshSerial = null
        } else {
            refreshRevealProgress.snapTo(1f)
        }
    }

    Column(
        modifier =
            modifier
                .padding(
                    start = outerPadding,
                    end = outerPadding,
                    top = outerPadding,
                    bottom = outerPadding,
                ),
    ) {
        Box(modifier = Modifier.fillMaxSize()) {
            val revealProgress = refreshRevealProgress.value

            Box(modifier = Modifier.fillMaxSize()) {
                GridExitContent(
                    content = displayedContent,
                    trackedQuest = trackedQuest,
                    currentRoomQuestAvailable = currentRoomQuestAvailable,
                    gridSpacing = gridSpacing,
                    cellInset = cellInset,
                    outerCellInset = outerCellInset,
                    revealProgress = revealProgress,
                    onExitClick = { direction ->
                        sendGridExit(direction)
                    },
                    modifier = Modifier.fillMaxSize(),
                )
                GridExitMoveFeedbackLayer(
                    feedback = moveFeedback,
                    gridSpacing = gridSpacing,
                    cellInset = cellInset,
                    outerCellInset = outerCellInset,
                    onFeedbackFinished = ::finishMoveFeedback,
                    modifier = Modifier.fillMaxSize(),
                )
            }
        }
    }
}

@Composable
private fun GridExitContent(
    content: ExitGridRefreshContent,
    trackedQuest: TrackedQuestState,
    currentRoomQuestAvailable: Boolean,
    gridSpacing: Dp,
    cellInset: Dp,
    outerCellInset: Dp,
    revealProgress: Float,
    onExitClick: (String) -> Unit,
    modifier: Modifier = Modifier,
) {
    val density = LocalDensity.current
    val exitMap = remember(content.exits) { content.exits.associateBy { it.direction.lowercase() } }
    val connectionDirections =
        remember(content.exits) { visibleGridConnectionDirections(exitMap.keys) }
    val revealOffsetPx = with(density) { 2.dp.toPx() }
    val revealVisualProgress = revealProgress.coerceIn(0f, 1f)
    val revealOffsetFraction = 1f - revealVisualProgress
    val revealAlpha = 0.92f + 0.08f * revealVisualProgress

    Box(
        modifier =
            modifier
                .testTag(EXIT_GRID_CONTENT_TAG)
                .semantics {
                    exitGridRevealProgress = revealVisualProgress
                    exitGridDisplayedRefreshSerial = content.refreshSerial
                },
    ) {
        ExitGridConnections(
            directions = connectionDirections,
            gridSpacing = gridSpacing,
            cellInset = cellInset,
            outerCellInset = outerCellInset,
            alpha = revealAlpha,
            modifier =
                Modifier
                    .fillMaxSize()
                    .testTag(EXIT_GRID_CONNECTIONS_TAG),
        )
        Column(
            modifier = Modifier.fillMaxSize(),
            verticalArrangement = Arrangement.spacedBy(gridSpacing),
        ) {
            gridRows.forEach { rowDirections ->
                Row(
                    modifier =
                        Modifier
                            .fillMaxWidth()
                            .weight(1f),
                    horizontalArrangement = Arrangement.spacedBy(gridSpacing),
                ) {
                    rowDirections.forEach { slot ->
                        val slotPosition = gridDirectionSlots[slot]
                        val cellRevealModifier =
                            if (slotPosition != null) {
                                Modifier.graphicsLayer {
                                    alpha = revealAlpha
                                    translationX = (1 - slotPosition.second) * revealOffsetPx * revealOffsetFraction
                                    translationY = (1 - slotPosition.first) * revealOffsetPx * revealOffsetFraction
                                }
                            } else {
                                Modifier
                            }
                        Box(
                            modifier =
                                Modifier
                                    .weight(1f)
                                    .fillMaxSize(),
                            contentAlignment = Alignment.Center,
                        ) {
                            when (slot) {
                                "center" ->
                                    RoomAnchorCell(
                                        title = content.roomTitle,
                                        questAvailable = currentRoomQuestAvailable,
                                        modifier =
                                            Modifier
                                                .fillMaxSize()
                                                .padding(cellInset),
                                    )

                                else -> {
                                    val exit = exitMap[slot]
                                    if (exit != null) {
                                        val highlighted =
                                            trackedQuest.active &&
                                                exit.direction.toCanonicalTravelDirection() == trackedQuest.nextDirection
                                        GridExitCell(
                                            destination = exit.name,
                                            onClick = { onExitClick(exit.direction) },
                                            highlighted = highlighted,
                                            questAvailable = exit.questAvailable,
                                            modifier =
                                                Modifier
                                                    .fillMaxSize()
                                                    .padding(cellInset + outerCellInset)
                                                    .then(cellRevealModifier),
                                        )
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

@Composable
internal fun GridExitMoveFeedbackLayer(
    feedback: ExitMoveFeedback?,
    gridSpacing: Dp,
    cellInset: Dp,
    outerCellInset: Dp,
    modifier: Modifier = Modifier,
    onFeedbackFinished: (Int) -> Unit = {},
) {
    val feedbackToken = feedback?.token
    val progress =
        remember(feedbackToken) {
            Animatable(if (feedbackToken == null) 1f else EXIT_MOVE_FEEDBACK_START_PROGRESS)
        }
    LaunchedEffect(feedbackToken) {
        val current = feedback ?: return@LaunchedEffect
        progress.animateTo(
            targetValue = 1f,
            animationSpec =
                tween(
                    durationMillis = EXIT_MOVE_FEEDBACK_MS,
                    easing = FastOutSlowInEasing,
                ),
        )
        onFeedbackFinished(current.token)
    }
    val progressValue = progress.value
    Canvas(
        modifier =
            modifier
                .testTag(EXIT_MOVE_FEEDBACK_TAG)
                .semantics { exitMoveFeedbackProgress = progressValue },
    ) {
        val current = feedback ?: return@Canvas
        val content = current.content
        if (content.exits.none {
                    exit ->
                exit.direction.equals(current.direction, ignoreCase = true)
            }
        ) {
            return@Canvas
        }
        val slot = gridDirectionSlots[current.direction] ?: return@Canvas
        if (progressValue >= 1f) {
            return@Canvas
        }
        val spacingPx = gridSpacing.toPx()
        val cellInsetPx = cellInset.toPx()
        val outerCellInsetPx = (cellInset + outerCellInset).toPx()
        val cellWidth = (size.width - spacingPx * 2f) / 3f
        val cellHeight = (size.height - spacingPx * 2f) / 3f
        val centerHalfWidth = gridButtonHalfSize(cellWidth, cellInsetPx)
        val centerHalfHeight = gridButtonHalfSize(cellHeight, cellInsetPx)
        val outerHalfWidth = gridButtonHalfSize(cellWidth, outerCellInsetPx)
        val outerHalfHeight = gridButtonHalfSize(cellHeight, outerCellInsetPx)

        fun centerOf(
            row: Int,
            column: Int,
        ): Offset {
            return Offset(
                x = column * (cellWidth + spacingPx) + cellWidth / 2f,
                y = row * (cellHeight + spacingPx) + cellHeight / 2f,
            )
        }
        val center = centerOf(row = 1, column = 1)
        val target = centerOf(row = slot.first, column = slot.second)
        val start =
            gridButtonEdgePoint(center, current.direction, centerHalfWidth, centerHalfHeight)
        val end =
            gridButtonEdgePoint(
                target,
                current.direction.oppositeGridDirection(),
                outerHalfWidth,
                outerHalfHeight,
            )
        val trailStartProgress = (progressValue - 0.2f).coerceAtLeast(0f)
        val head = start + (end - start) * progressValue
        val tail = start + (end - start) * trailStartProgress
        val alpha = 1f
        drawLine(
            color = ShellPalette.Accent.copy(alpha = 0.82f * alpha),
            start = tail,
            end = head,
            strokeWidth = 2.dp.toPx(),
            cap = StrokeCap.Round,
        )
        drawLine(
            color = ShellPalette.Accent.copy(alpha = 0.22f * alpha),
            start = start + (end - start) * (progressValue - 0.28f).coerceAtLeast(0f),
            end = head,
            strokeWidth = 1.dp.toPx(),
            cap = StrokeCap.Round,
        )
    }
}

internal data class ExitMoveFeedback(
    val direction: String,
    val content: ExitGridRefreshContent,
    val token: Int,
)

@Composable
private fun ExitGridConnections(
    directions: List<String>,
    gridSpacing: Dp,
    cellInset: Dp,
    outerCellInset: Dp,
    modifier: Modifier = Modifier,
    alpha: Float = 1f,
) {
    Canvas(modifier = modifier) {
        val lineAlpha = alpha.coerceIn(0f, 1f)
        if (directions.isEmpty() || lineAlpha <= 0f) {
            return@Canvas
        }
        val spacingPx = gridSpacing.toPx()
        val cellInsetPx = cellInset.toPx()
        val outerCellInsetPx = (cellInset + outerCellInset).toPx()
        val cellWidth = (size.width - spacingPx * 2f) / 3f
        val cellHeight = (size.height - spacingPx * 2f) / 3f
        val centerHalfWidth = gridButtonHalfSize(cellWidth, cellInsetPx)
        val centerHalfHeight = gridButtonHalfSize(cellHeight, cellInsetPx)
        val outerHalfWidth = gridButtonHalfSize(cellWidth, outerCellInsetPx)
        val outerHalfHeight = gridButtonHalfSize(cellHeight, outerCellInsetPx)

        fun centerOf(
            row: Int,
            column: Int,
        ): Offset {
            return Offset(
                x = column * (cellWidth + spacingPx) + cellWidth / 2f,
                y = row * (cellHeight + spacingPx) + cellHeight / 2f,
            )
        }
        val center = centerOf(row = 1, column = 1)
        directions.forEach { direction ->
            val slot = gridDirectionSlots[direction] ?: return@forEach
            val target = centerOf(row = slot.first, column = slot.second)
            drawLine(
                color = ShellPalette.Accent.copy(alpha = 0.28f * lineAlpha),
                start = gridButtonEdgePoint(center, direction, centerHalfWidth, centerHalfHeight),
                end =
                    gridButtonEdgePoint(
                        target,
                        direction.oppositeGridDirection(),
                        outerHalfWidth,
                        outerHalfHeight,
                    ),
                strokeWidth = 1.35.dp.toPx(),
                cap = StrokeCap.Round,
            )
        }
    }
}

private fun gridButtonHalfSize(
    cellSize: Float,
    cellInsetPx: Float,
): Float {
    return (cellSize / 2f - cellInsetPx).coerceAtLeast(0f)
}

private fun gridButtonEdgePoint(
    center: Offset,
    direction: String,
    halfWidth: Float,
    halfHeight: Float,
): Offset {
    val normalized = direction.trim().lowercase()
    val horizontal =
        when {
            normalized.contains("west") -> -1f
            normalized.contains("east") -> 1f
            else -> 0f
        }
    val vertical =
        when {
            normalized.contains("north") -> -1f
            normalized.contains("south") -> 1f
            else -> 0f
        }
    return Offset(
        x = center.x + horizontal * halfWidth,
        y = center.y + vertical * halfHeight,
    )
}

private fun String.oppositeGridDirection(): String {
    return when (trim().lowercase()) {
        "northwest" -> "southeast"
        "north" -> "south"
        "northeast" -> "southwest"
        "west" -> "east"
        "east" -> "west"
        "southwest" -> "northeast"
        "south" -> "north"
        "southeast" -> "northwest"
        else -> ""
    }
}

@Composable
internal fun ExitOverlayPage(
    overlay: ExitOverlayState,
    onButtonClick: (ShellAction) -> Unit,
    modifier: Modifier = Modifier,
    onDismiss: () -> Unit = {},
    cooldownSecondsByCommand: Map<String, Int> = emptyMap(),
) {
    if (overlay.type == ExitOverlayType.COMBAT) {
        CombatExitOverlayPage(
            overlay = overlay,
            onButtonClick = onButtonClick,
            modifier = modifier,
            cooldownSecondsByCommand = cooldownSecondsByCommand,
        )
        return
    }
    // 方案 R-34：以下 COMMON 分支为「仅快照遗产」。
    // 运行时 COMMON「更多」唯一渲染路径是 SketchCommonExitRollout（ShellSketchLayout），
    // 此处仅保留供既有快照/几何测试引用，不参与任何运行时表面；新代码不得依赖本分支。
    val (columnCount, actionRows) =
        remember(overlay.buttons) {
            val visibleActions = overlay.buttons.filter { it.label.isNotBlank() }.take(12)
            val columnCount =
                when {
                    visibleActions.isEmpty() -> 1
                    visibleActions.size <= 6 -> visibleActions.size
                    else -> ((visibleActions.size + 1) / 2).coerceAtMost(6)
                }
            columnCount to visibleActions.chunked(columnCount).take(2)
        }
    BoxWithConstraints(
        modifier =
            modifier
                .background(ShellPalette.CanvasRaised.copy(alpha = 0.78f)),
    ) {
        val expandedRowsHeight =
            when (actionRows.size) {
                0 -> 0.dp
                1 -> ShellChrome.ExitOverlayRowMaxHeight
                else -> ShellChrome.ExitOverlayRowMaxHeight + ShellChrome.ExitOverlayRowMaxHeight
            }
        val expandedRequiredHeight =
            EXIT_OVERLAY_HEADER_HEIGHT +
                ShellChrome.DividerThickness +
                EXIT_OVERLAY_EXPANDED_CONTENT_TOP_PADDING +
                EXIT_OVERLAY_EXPANDED_CONTENT_BOTTOM_PADDING +
                expandedRowsHeight +
                if (actionRows.size > 1) 8.dp else 0.dp
        val compact = actionRows.isNotEmpty() && maxHeight < expandedRequiredHeight

        Column(
            modifier =
                Modifier
                    .fillMaxSize(),
        ) {
            if (!compact) {
                Row(
                    modifier =
                        Modifier
                            .fillMaxWidth()
                            .height(EXIT_OVERLAY_HEADER_HEIGHT)
                            .padding(start = 12.dp, end = EXIT_OVERLAY_CLOSE_TARGET_SIZE)
                            .testTag(EXIT_OVERLAY_HEADER_TAG),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Text(
                        text = "更多",
                        color = ShellPalette.PaperMuted,
                        style =
                            MaterialTheme.typography.labelLarge.copy(
                                fontWeight = FontWeight.Medium,
                            ),
                    )
                }
                SectionRule()
            }
            Column(
                modifier =
                    Modifier
                        .fillMaxWidth()
                        .weight(1f)
                        .padding(
                            start = 8.dp,
                            end = if (compact) 52.dp else 8.dp,
                            top = if (compact) 4.dp else EXIT_OVERLAY_EXPANDED_CONTENT_TOP_PADDING,
                            bottom = if (compact) 4.dp else EXIT_OVERLAY_EXPANDED_CONTENT_BOTTOM_PADDING,
                        ),
                verticalArrangement =
                    Arrangement.spacedBy(
                        space = if (compact) 4.dp else 8.dp,
                        alignment = Alignment.CenterVertically,
                    ),
            ) {
                actionRows.forEach { rowActions ->
                    val sideWeight = ((columnCount - rowActions.size).coerceAtLeast(0)) / 2f
                    Row(
                        modifier =
                            if (compact) {
                                Modifier
                                    .fillMaxWidth()
                                    .weight(1f)
                            } else {
                                Modifier
                                    .fillMaxWidth()
                                    .heightIn(
                                        min = ShellChrome.ExitOverlayRowMaxHeight,
                                        max = ShellChrome.ExitOverlayRowMaxHeight,
                                    )
                            },
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                    ) {
                        if (sideWeight > 0f) Spacer(modifier = Modifier.weight(sideWeight))
                        rowActions.forEach { action ->
                            val label = action.label
                            UnifiedButtonSurface(
                                modifier =
                                    Modifier
                                        .weight(1f)
                                        .fillMaxHeight()
                                        .testTag("$EXIT_OVERLAY_BUTTON_TAG_PREFIX$label"),
                                onClick = { onButtonClick(action) },
                                fillColor = ShellPalette.CanvasInset.copy(alpha = 0.38f),
                                borderColor = ShellSectionLineColor,
                                minHeight = if (compact) 0.dp else ShellChrome.ControlMinHeight,
                                horizontalPadding =
                                    if (compact) {
                                        ShellChrome.CompactControlHorizontalPadding
                                    } else {
                                        ShellChrome.ControlHorizontalPadding
                                    },
                                verticalPadding =
                                    if (compact) {
                                        ShellChrome.CompactControlVerticalPadding
                                    } else {
                                        ShellChrome.ControlVerticalPadding
                                    },
                            ) {
                                Text(
                                    text = label,
                                    color = ShellPalette.PaperMuted,
                                    style = MaterialTheme.typography.bodyMedium,
                                    textAlign = TextAlign.Center,
                                    maxLines = 1,
                                )
                            }
                        }
                        if (sideWeight > 0f) Spacer(modifier = Modifier.weight(sideWeight))
                    }
                }
            }
        }
        CommonExitOverlayCloseButton(
            onDismiss = onDismiss,
            modifier = Modifier.align(Alignment.TopEnd),
        )
    }
}

@Composable
private fun CommonExitOverlayCloseButton(
    onDismiss: () -> Unit,
    modifier: Modifier = Modifier,
) {
    UnifiedButtonSurface(
        modifier =
            modifier
                .size(EXIT_OVERLAY_CLOSE_TARGET_SIZE)
                .testTag(EXIT_OVERLAY_CLOSE_BUTTON_TAG)
                .semantics { contentDescription = "关闭更多面板" },
        onClick = onDismiss,
        fillColor = Color.Transparent,
        borderColor = Color.Transparent,
        minHeight = EXIT_OVERLAY_CLOSE_TARGET_SIZE,
        horizontalPadding = 0.dp,
        verticalPadding = 0.dp,
    ) {
        Canvas(
            modifier =
                Modifier
                    .size(13.dp)
                    .graphicsLayer { translationY = -4.dp.toPx() },
        ) {
            val strokeWidth = 1.5.dp.toPx()
            drawLine(
                color = ShellPalette.PaperMuted,
                start = Offset(1.dp.toPx(), 1.dp.toPx()),
                end = Offset(size.width - 1.dp.toPx(), size.height - 1.dp.toPx()),
                strokeWidth = strokeWidth,
                cap = StrokeCap.Round,
            )
            drawLine(
                color = ShellPalette.PaperMuted,
                start = Offset(size.width - 1.dp.toPx(), 1.dp.toPx()),
                end = Offset(1.dp.toPx(), size.height - 1.dp.toPx()),
                strokeWidth = strokeWidth,
                cap = StrokeCap.Round,
            )
        }
    }
}

@Composable
private fun CombatExitOverlayPage(
    overlay: ExitOverlayState,
    onButtonClick: (ShellAction) -> Unit,
    modifier: Modifier = Modifier,
    cooldownSecondsByCommand: Map<String, Int> = emptyMap(),
) {
    BoxWithConstraints(
        modifier =
            modifier
                .background(ShellPalette.CanvasRaised.copy(alpha = 0.78f))
                .padding(horizontal = 4.dp, vertical = 6.dp),
    ) {
        val columnCount = COMBAT_EXIT_GRID_COLUMN_COUNT
        val rowCount = COMBAT_EXIT_GRID_ROW_COUNT
        val rowHeight =
            ((maxHeight - COMBAT_EXIT_GRID_ROW_SPACING) / rowCount)
                .coerceAtLeast(0.dp)
                .coerceAtMost(ShellChrome.CombatActionRowMaxHeight)
        Column(
            modifier = Modifier.fillMaxSize(),
            verticalArrangement =
                Arrangement.spacedBy(COMBAT_EXIT_GRID_ROW_SPACING, Alignment.CenterVertically),
        ) {
            repeat(rowCount) { rowIndex ->
                Row(
                    modifier =
                        Modifier
                            .fillMaxWidth()
                            .height(rowHeight),
                    horizontalArrangement = Arrangement.spacedBy(4.dp),
                ) {
                    repeat(columnCount) { columnIndex ->
                        val slotIndex = rowIndex * columnCount + columnIndex
                        if (slotIndex < overlay.buttons.size) {
                            val action = overlay.buttons[slotIndex]
                            CombatExitOverlayCell(
                                action = action,
                                onClick = onButtonClick,
                                cooldownSeconds = action.command.let { cooldownSecondsByCommand[it] }?.takeIf { it > 0 },
                                modifier =
                                    Modifier
                                        .weight(1f)
                                        .fillMaxHeight()
                                        .testTag("$COMBAT_EXIT_OVERLAY_CELL_TAG_PREFIX$slotIndex"),
                            )
                        } else {
                            Spacer(
                                modifier =
                                    Modifier
                                        .weight(1f)
                                        .fillMaxHeight(),
                            )
                        }
                    }
                }
            }
        }
    }
}

private const val COMBAT_EXIT_GRID_COLUMN_COUNT = 5
private const val COMBAT_EXIT_GRID_ROW_COUNT = 2
private val COMBAT_EXIT_GRID_ROW_SPACING = 6.dp
private val EXIT_OVERLAY_HEADER_HEIGHT = 36.dp
private val EXIT_OVERLAY_CLOSE_TARGET_SIZE = 44.dp
private val EXIT_OVERLAY_EXPANDED_CONTENT_TOP_PADDING = 8.dp
private val EXIT_OVERLAY_EXPANDED_CONTENT_BOTTOM_PADDING = 6.dp

/**
 * Perform labels are measured against the real narrow map viewport, not a standalone full-width panel.
 * At 320dp, the 52dp narrow rail plus divider leaves 267dp for the right overlay;
 * with a 2x5 grid, 4dp outer/gap spacing, and 4dp cell padding, the text width is 40dp.
 * Four CJK glyphs therefore require the 10sp style below. Keep this contract in sync with
 * ShellActionOverlaySnapshotTest; raising the size or spacing reintroduces three-glyph ellipsis.
 */
@Composable
internal fun CombatActionLabel(
    text: String,
    modifier: Modifier = Modifier,
    onTextLayout: (TextLayoutResult) -> Unit = {},
) {
    Text(
        text = text,
        color = ShellPalette.Paper,
        style = ShellType.CombatAction,
        maxLines = 1,
        overflow = TextOverflow.Ellipsis,
        onTextLayout = onTextLayout,
        textAlign = TextAlign.Center,
        modifier = modifier,
    )
}

@Composable
private fun CombatExitOverlayCell(
    action: ShellAction?,
    onClick: (ShellAction) -> Unit,
    modifier: Modifier = Modifier,
    cooldownSeconds: Int? = null,
) {
    val command = action?.command.orEmpty()
    val longClickCommand = action?.longClickCommand.orEmpty()
    val clickableModifier =
        if (action == null || (command.isBlank() && longClickCommand.isBlank())) {
            Modifier
        } else {
            Modifier.pointerInput(action) {
                detectTapGestures(
                    onTap = {
                        if (command.isNotBlank()) {
                            onClick(action)
                        }
                    },
                    onLongPress = {
                        if (longClickCommand.isNotBlank()) {
                            onClick(action.copy(command = longClickCommand))
                        }
                    },
                )
            }
        }
    UnifiedButtonSurface(
        modifier = modifier.then(clickableModifier),
        fillColor = ShellPalette.CanvasInset.copy(alpha = 0.44f),
        borderColor = ShellSectionLineColor,
        minHeight = 0.dp,
        horizontalPadding = 4.dp,
        verticalPadding = 4.dp,
    ) {
        Box(modifier = Modifier.fillMaxSize()) {
            Column(
                modifier = Modifier.fillMaxSize(),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.spacedBy(3.dp, Alignment.CenterVertically),
            ) {
                CombatActionLabel(
                    text = action?.label.orEmpty(),
                    modifier = Modifier.fillMaxWidth(),
                )
                if (!action?.supportingLabel.isNullOrBlank()) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.Center,
                    ) {
                        Text(
                            text = action!!.supportingLabel,
                            color = ShellPalette.Accent,
                            style = ShellType.Narrative,
                            textAlign = TextAlign.Center,
                        )
                    }
                }
            }
            // 方案 R-31：冷却=格面 Ink0 α0.6 罩 + 剩余秒 numeric 居中（颜色之外的冷却编码）
            if (cooldownSeconds != null) {
                Box(
                    modifier =
                        Modifier
                            .matchParentSize()
                            .background(ShellPalette.Ink0.copy(alpha = 0.60f)),
                    contentAlignment = Alignment.Center,
                ) {
                    Text(
                        text = cooldownSeconds.toString(),
                        color = ShellPalette.Paper,
                        style = ShellType.Narrative,
                    )
                }
            }
        }
    }
}
