package com.xiakexing.client.feature.shell.popup

import android.os.SystemClock
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.gestures.detectTransformGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Stable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.onClick
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.xiakexing.client.feature.shell.ShellAction
import com.xiakexing.client.feature.shell.ShellMapQuestMarkerDetail
import com.xiakexing.client.feature.shell.ShellSheetEmptyPresentation
import com.xiakexing.client.feature.shell.ShellSheetItem
import com.xiakexing.client.feature.shell.ShellSheetState
import com.xiakexing.client.feature.shell.component.QuestMarkerBadge
import com.xiakexing.client.feature.shell.component.UnifiedButtonSurface
import com.xiakexing.client.feature.shell.resolvedMapPanelContent
import com.xiakexing.client.feature.shell.ui.ShellChrome
import com.xiakexing.client.feature.shell.ui.ShellLines
import com.xiakexing.client.feature.shell.ui.ShellPalette
import com.xiakexing.client.feature.shell.ui.ShellRadius
import kotlin.math.abs
import kotlin.math.roundToInt

internal const val MAP_CELL_TEST_TAG_PREFIX = "map-cell-"
internal const val MAP_CONNECTIONS_TEST_TAG = "map-connections"
internal const val MAP_GRID_TEST_TAG = "map-grid"

/* 服务端按半径 20 发送 41×41 网格（世界内 ±20 格）。
 * 默认视图就是整个半径 20：zoom=1 时整张图铺满面板宽度。
 * 这个尺度下格子只有几 dp，房名画出来只会糊成一团，因此按缩放分级显示：
 * 格宽达到 MAP_LABEL_MIN_CELL 才画房名，否则只画格子与连线。 */
private const val MAP_RADIUS = 20
private const val MAP_DIAMETER = MAP_RADIUS * 2 + 1
private const val MAP_MAX_ZOOM = 8f
private val MAP_BASE_CELL_SIZE = 26.dp
private val MAP_BASE_CELL_SPACING = 6.dp
private val MAP_LABEL_MIN_CELL = 15.dp
private val MAP_GRID_EDGE_PADDING = 8.dp

@Composable
fun InlineSheetMapPanel(
    sheet: ShellSheetState,
    onActionClick: (ShellAction) -> Unit,
    modifier: Modifier = Modifier,
) {
    val content = remember(sheet.mapContent, sheet.panelStats) { sheet.resolvedMapPanelContent() }
    val mapModel =
        remember(content, sheet.items) {
            buildCenteredMapModel(
                rows = content.rows,
                exits = sheet.items,
                explicitPaths = content.paths,
                roomsByCell = content.rooms,
                questMarkerCells = content.questMarkerCells,
                questMarkerDetails = content.questMarkerDetails,
                trackedRouteRooms = content.trackedRooms,
            )
        }
    val viewport = remember(mapModel) { MapViewportState() }

    Column(modifier = modifier) {
        Box(
            modifier =
                Modifier
                    .fillMaxWidth()
                    .weight(1f, fill = true),
        ) {
            if (content.rows.isEmpty() && sheet.items.isEmpty()) {
                MapEmptyState(
                    text =
                        sheet.panelEmptyText.takeIf {
                            sheet.emptyPresentation == ShellSheetEmptyPresentation.MESSAGE
                        }.orEmpty(),
                )
            } else {
                CenteredMapGrid(
                    mapModel = mapModel,
                    onActionClick = onActionClick,
                    viewport = viewport,
                    modifier = Modifier.fillMaxSize(),
                )
            }
        }
        if (content.rows.isNotEmpty() || sheet.items.isNotEmpty()) {
            Box(
                modifier =
                    Modifier
                        .fillMaxWidth()
                        .height(44.dp),
                contentAlignment = Alignment.CenterEnd,
            ) {
                MapZoomControls(viewport = viewport)
            }
        }
    }
}

@Composable
private fun MapZoomControls(
    viewport: MapViewportState,
    modifier: Modifier = Modifier,
) {
    Row(
        modifier = modifier,
        horizontalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        MapZoomButton(
            label = "+",
            description = "放大地图",
            onClick = { viewport.zoom = (viewport.zoom * 1.25f).coerceAtMost(MAP_MAX_ZOOM) },
        )
        MapZoomButton(
            label = "-",
            description = "缩小地图",
            onClick = { viewport.zoom = (viewport.zoom / 1.25f).coerceAtLeast(1f) },
        )
    }
}

@Composable
private fun MapZoomButton(
    label: String,
    description: String,
    onClick: () -> Unit,
) {
    UnifiedButtonSurface(
        modifier =
            Modifier
                .size(36.dp)
                .semantics { contentDescription = description },
        onClick = onClick,
        minHeight = 36.dp,
        horizontalPadding = 0.dp,
        verticalPadding = 0.dp,
        fillColor = ShellPalette.Ink2.copy(alpha = 0.92f),
        borderColor = ShellLines.ControlStateBorder,
        shape = ShellRadius.S,
    ) {
        Text(
            text = label,
            color = ShellPalette.Paper,
            style = MaterialTheme.typography.labelLarge,
            fontWeight = FontWeight.SemiBold,
            textAlign = TextAlign.Center,
        )
    }
}

@Composable
private fun CenteredMapGrid(
    mapModel: CenteredMapGridModel,
    onActionClick: (ShellAction) -> Unit,
    viewport: MapViewportState,
    modifier: Modifier = Modifier,
) {
    BoxWithConstraints(
        modifier =
            modifier
                .padding(MAP_GRID_EDGE_PADDING)
                .clipToBounds(),
    ) {
        val density = LocalDensity.current
        val cellSize = MAP_BASE_CELL_SIZE
        val gridSpacing = MAP_BASE_CELL_SPACING
        val cellSizePx = with(density) { cellSize.toPx() }
        val cellStepPx = with(density) { (cellSize + gridSpacing).toPx() }
        // zoom=1 时整张半径 20 恰好铺满面板宽度。
        val fitScale =
            if (cellStepPx > 0f) {
                with(density) { maxWidth.toPx() } / (cellStepPx * MAP_DIAMETER)
            } else {
                1f
            }
        val scale = fitScale * viewport.zoom
        // 格宽不够时只画格子，不画房名，避免文字叠成色块。
        val showLabels = cellSize * scale >= MAP_LABEL_MIN_CELL
        val fontSize =
            when {
                cellSize * scale >= 44.dp -> 13.sp
                else -> 11.sp
            }
        val cells = mapModel.cells
        Box(
            modifier =
                Modifier
                    .fillMaxSize()
                    .testTag(MAP_GRID_TEST_TAG)
                    .pointerInput(cells) {
                        detectTransformGestures { _, pan, zoom, _ ->
                            if (pan.getDistance() > 0.5f || abs(zoom - 1f) > 0.001f) {
                                viewport.lastTransformAtMillis = SystemClock.uptimeMillis()
                            }
                            viewport.zoom =
                                (viewport.zoom * zoom).coerceIn(1f, MAP_MAX_ZOOM)
                            viewport.offsetX += pan.x
                            viewport.offsetY += pan.y
                        }
                    }
                    .pointerInput(cells, cellStepPx, cellSizePx, scale) {
                        detectTapGestures { tap ->
                            if (SystemClock.uptimeMillis() - viewport.lastTransformAtMillis < 220L) {
                                return@detectTapGestures
                            }
                            findTappedMapCell(
                                tap = tap,
                                containerCenter = Offset(size.width / 2f, size.height / 2f),
                                cells = cells,
                                cellStepPx = cellStepPx,
                                cellSizePx = cellSizePx,
                                scale = scale,
                                offsetX = viewport.offsetX,
                                offsetY = viewport.offsetY,
                            )?.action?.let(onActionClick)
                        }
                    },
        ) {
            Box(
                modifier =
                    Modifier
                        .fillMaxSize()
                        .graphicsLayer {
                            translationX = viewport.offsetX
                            translationY = viewport.offsetY
                            scaleX = scale
                            scaleY = scale
                            transformOrigin = androidx.compose.ui.graphics.TransformOrigin.Center
                        },
                contentAlignment = Alignment.Center,
            ) {
                MapConnectionsLayer(
                    segments = mapModel.connectionSegments,
                    cellStepPx = cellStepPx,
                    cellSizePx = with(density) { cellSize.toPx() },
                    scale = scale,
                    modifier = Modifier.fillMaxSize(),
                )
                cells.forEachIndexed { rowIndex, row ->
                    row.forEachIndexed cellLoop@{ columnIndex, cell ->
                        if (!cell.hasVisualContent()) {
                            return@cellLoop
                        }
                        val deltaX = columnIndex - MAP_RADIUS
                        val deltaY = rowIndex - MAP_RADIUS
                        MapCellView(
                            cell = cell,
                            fontSize = fontSize,
                            showLabel = showLabels,
                            onActionClick = onActionClick,
                            modifier =
                                Modifier
                                    .offset {
                                        IntOffset(
                                            x = (deltaX * cellStepPx).roundToInt(),
                                            y = (deltaY * cellStepPx).roundToInt(),
                                        )
                                    }
                                    .size(cellSize),
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun MapConnectionsLayer(
    segments: List<MapConnectionSegment>,
    cellStepPx: Float,
    cellSizePx: Float,
    scale: Float,
    modifier: Modifier = Modifier,
) {
    Canvas(modifier = modifier.testTag(MAP_CONNECTIONS_TEST_TAG)) {
        if (segments.isEmpty()) {
            return@Canvas
        }
        val safeScale = scale.coerceAtLeast(0.01f)
        val center = Offset(size.width / 2f, size.height / 2f)

        fun point(
            row: Int,
            column: Int,
        ): Offset {
            return Offset(
                x = center.x + (column - MAP_RADIUS) * cellStepPx,
                y = center.y + (row - MAP_RADIUS) * cellStepPx,
            )
        }
        val cellInset = (cellSizePx / 2f).coerceAtMost(cellStepPx / 2f - 1f)
        segments.forEach { segment ->
            val start = point(segment.fromRow, segment.fromColumn)
            val end = point(segment.toRow, segment.toColumn)
            val dx = end.x - start.x
            val dy = end.y - start.y
            val length = kotlin.math.sqrt(dx * dx + dy * dy).takeIf { it > 0f } ?: 1f
            val unitX = dx / length
            val unitY = dy / length
            drawLine(
                color = ShellLines.MapConnector,
                start = Offset(start.x + unitX * cellInset, start.y + unitY * cellInset),
                end = Offset(end.x - unitX * cellInset, end.y - unitY * cellInset),
                strokeWidth = 1.65.dp.toPx() / safeScale,
                cap = StrokeCap.Round,
            )
        }
    }
}

@Composable
private fun MapCellView(
    cell: MapCell,
    fontSize: androidx.compose.ui.unit.TextUnit,
    showLabel: Boolean,
    onActionClick: (ShellAction) -> Unit,
    modifier: Modifier = Modifier,
) {
    val action = cell.action
    val label = if (showLabel) cell.label else ""
    when {
        cell.isCenter ->
            MapGridCell(
                label = "你",
                fontSize = fontSize,
                selected = true,
                questAvailable = cell.questAvailable,
                questEntry = cell.questEntry,
                trackedRoute = false,
                onClick = null,
                modifier = modifier,
            )

        action != null ->
            MapGridCell(
                label = label,
                fontSize = fontSize,
                selected = false,
                questAvailable = cell.questAvailable,
                questEntry = cell.questEntry,
                trackedRoute = cell.trackedRoute,
                semanticLabel = cell.label,
                onClick = { onActionClick(action) },
                modifier = modifier,
            )

        cell.label.isNotBlank() ->
            MapGridCell(
                label = label,
                fontSize = fontSize,
                selected = false,
                questAvailable = cell.questAvailable,
                questEntry = cell.questEntry,
                trackedRoute = cell.trackedRoute,
                semanticLabel = cell.label,
                onClick = null,
                modifier = modifier,
            )

        else ->
            MapGridCell(
                label = "",
                fontSize = fontSize,
                selected = false,
                questAvailable = cell.questAvailable,
                questEntry = cell.questEntry,
                trackedRoute = cell.trackedRoute,
                onClick = null,
                modifier = modifier,
            )
    }
}

@Composable
private fun MapGridCell(
    label: String,
    fontSize: androidx.compose.ui.unit.TextUnit,
    selected: Boolean,
    questAvailable: Boolean,
    questEntry: Boolean,
    trackedRoute: Boolean,
    semanticLabel: String = label,
    onClick: (() -> Unit)?,
    modifier: Modifier = Modifier,
) {
    val semanticModifier =
        modifier
            .testTag("$MAP_CELL_TEST_TAG_PREFIX$semanticLabel")
            .semantics {
                contentDescription =
                    buildString {
                        append("地图房间 $semanticLabel")
                        if (trackedRoute) append(" 追踪路线")
                        if (questAvailable && questEntry) {
                            append(" 任务入口")
                        } else if (questAvailable) {
                            append(" 有任务")
                        }
                    }
                if (onClick != null) {
                    role = Role.Button
                    onClick {
                        onClick()
                        true
                    }
                }
            }
    Box(modifier = semanticModifier) {
        MapCellSurface(
            label = label,
            fontSize = fontSize,
            selected = selected,
            trackedRoute = trackedRoute,
            modifier = Modifier.fillMaxSize(),
        )
        if (questAvailable) {
            QuestMarkerBadge(
                modifier = Modifier.align(Alignment.TopEnd),
                // 第六轮 §2.1：统一感叹号（原内部入口房显"进"与九宫格不一致）；
                // 入口语义保留在 contentDescription 的"任务入口"描述里。
                text = "!",
            )
        }
    }
}

@Composable
private fun MapCellSurface(
    label: String,
    fontSize: androidx.compose.ui.unit.TextUnit,
    selected: Boolean,
    trackedRoute: Boolean,
    modifier: Modifier = Modifier,
) {
    Box(
        modifier =
            modifier
                .background(
                    color =
                        when {
                            selected -> ShellPalette.Accent.copy(alpha = 0.10f)
                            trackedRoute -> ShellPalette.QuestAccent.copy(alpha = 0.08f)
                            else -> ShellPalette.Ink2.copy(alpha = 0.28f)
                        },
                    shape = UnifiedPopupControlShape,
                )
                .then(
                    Modifier.border(
                        width = ShellChrome.DividerThickness,
                        color =
                            when {
                                trackedRoute -> ShellLines.QuestBorder
                                selected -> ShellLines.SelectedBorder
                                else -> ShellLines.ControlStateBorder
                            },
                        shape = UnifiedPopupControlShape,
                    ),
                )
                .padding(horizontal = 2.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            text = label,
            color =
                when {
                    selected -> ShellPalette.Accent
                    trackedRoute -> ShellPalette.QuestAccent
                    else -> ShellPalette.PaperMuted
                },
            style =
                MaterialTheme.typography.labelLarge.copy(
                    fontSize = fontSize,
                    fontWeight = if (selected) FontWeight.SemiBold else FontWeight.Medium,
                    lineHeight = fontSize * 1.08f,
                ),
            textAlign = TextAlign.Center,
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
        )
    }
}

@Composable
private fun MapEmptyState(text: String) {
    Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
        if (text.isNotBlank()) {
            Text(
                text = text,
                color = ShellPalette.PaperMuted,
                style = MaterialTheme.typography.bodyMedium,
            )
        }
    }
}

private data class MapCell(
    val label: String = "",
    val action: ShellAction? = null,
    val isCenter: Boolean = false,
    val questAvailable: Boolean = false,
    val questEntry: Boolean = false,
    val trackedRoute: Boolean = false,
)

private fun MapCell.hasVisualContent(): Boolean {
    return isCenter || label.isNotBlank() || questAvailable || trackedRoute
}

private data class CenteredMapGridModel(
    val cells: List<List<MapCell>>,
    val connectionSegments: List<MapConnectionSegment>,
)

@Stable
private class MapViewportState {
    // zoom=1 = 整个半径 20 铺满面板（打开即全图）；放大看房名。
    var zoom by mutableFloatStateOf(1f)
    var offsetX by mutableFloatStateOf(0f)
    var offsetY by mutableFloatStateOf(0f)
    var lastTransformAtMillis: Long = -1_000L
}

private fun buildCenteredMapModel(
    rows: List<String>,
    exits: List<ShellSheetItem>,
    explicitPaths: Map<String, List<String>>,
    roomsByCell: Map<String, String>,
    questMarkerCells: Set<String>,
    questMarkerDetails: List<ShellMapQuestMarkerDetail>,
    trackedRouteRooms: Set<String>,
): CenteredMapGridModel {
    val sourceCenter = locateCenter(rows)
    val labelByPoint = buildLabelByPoint(rows, sourceCenter)
    val exitByPoint =
        exits.mapNotNull { item ->
            val offset = directionOffset(item.trailing)
            if (offset.first == Int.MIN_VALUE) null else MapPoint(offset.first, offset.second) to item
        }.toMap()
    // 任务入口标记：detail 带 entry_direction 的格子，NPC 在室内，需要进门寻找。
    val entryCoords =
        questMarkerDetails
            .filter { detail -> detail.entryDirection.isNotBlank() }
            .mapTo(linkedSetOf()) { detail -> detail.coord }
    val cells =
        List(MAP_DIAMETER) { gridY ->
            List(MAP_DIAMETER) { gridX ->
                val point = MapPoint(gridX - MAP_RADIUS, gridY - MAP_RADIUS)
                val isCenter = point.x == 0 && point.y == 0
                val exit = exitByPoint[point]
                val label = labelByPoint[point]
                val pointKey = point.toPathKey()
                val routePath =
                    explicitPaths[pointKey]
                        ?.let { path -> validateMapPath(pointKey, path) }
                val roomId = roomsByCell[pointKey].orEmpty()
                MapCell(
                    label =
                        when {
                            isCenter -> "你"
                            exit != null -> exit.title
                            !label.isNullOrBlank() -> label
                            else -> ""
                        },
                    action =
                        when {
                            isCenter -> null
                            exit != null ->
                                ShellAction(
                                    label = exit.title,
                                    command = exit.command,
                                    dismissAfterInvoke = exit.dismissAfterInvoke,
                                )
                            !label.isNullOrBlank() && routePath != null ->
                                routePath.toMapRouteAction(
                                    label,
                                )
                            else -> null
                        },
                    isCenter = isCenter,
                    questAvailable = pointKey in questMarkerCells,
                    questEntry = pointKey in entryCoords,
                    trackedRoute = !isCenter && roomId.isNotBlank() && roomId in trackedRouteRooms,
                )
            }
        }
    return CenteredMapGridModel(
        cells = cells,
        connectionSegments =
            buildMapConnectionSegments(
                explicitPaths = explicitPaths,
                directExitPoints = exitByPoint.keys,
                occupiedPoints =
                    buildSet {
                        add(MapPoint(0, 0))
                        addAll(labelByPoint.keys)
                        addAll(exitByPoint.keys)
                        addAll(roomsByCell.keys.mapNotNull(::parsePointKey))
                    },
                radius = MAP_RADIUS,
            ),
    )
}

private fun findTappedMapCell(
    tap: Offset,
    containerCenter: Offset,
    cells: List<List<MapCell>>,
    cellStepPx: Float,
    cellSizePx: Float,
    scale: Float,
    offsetX: Float,
    offsetY: Float,
): MapCell? {
    var bestCell: MapCell? = null
    var bestDistance = Float.MAX_VALUE
    val halfCell = cellSizePx * scale / 2f
    cells.forEachIndexed { rowIndex, row ->
        row.forEachIndexed cellLoop@{ columnIndex, cell ->
            if (cell.action != null) {
                val deltaX = columnIndex - MAP_RADIUS
                val deltaY = rowIndex - MAP_RADIUS
                val center =
                    Offset(
                        x = containerCenter.x + offsetX + deltaX * cellStepPx * scale,
                        y = containerCenter.y + offsetY + deltaY * cellStepPx * scale,
                    )
                // 命中实际卡片矩形（缩放后轴对齐）；网格空隙不产生动作。
                val hit =
                    abs(tap.x - center.x) <= halfCell &&
                        abs(tap.y - center.y) <= halfCell
                if (!hit) {
                    return@cellLoop
                }
                val distance = (tap - center).getDistance()
                if (distance < bestDistance) {
                    bestDistance = distance
                    bestCell = cell
                }
            }
        }
    }
    return bestCell
}

private fun buildLabelByPoint(
    rows: List<String>,
    sourceCenter: Pair<Int, Int>,
): Map<MapPoint, String> {
    return rows.mapIndexed { sourceY, row ->
        row.mapCellTokens().mapIndexedNotNull { sourceX, token ->
            val label = token.takeUnless { it == "·" || it == "你" || it == "@" }
            if (label.isNullOrBlank()) {
                null
            } else {
                MapPoint(sourceX - sourceCenter.second, sourceY - sourceCenter.first) to label
            }
        }
    }.flatten().toMap()
}

private fun List<String>.toMapRouteAction(label: String): ShellAction {
    return ShellAction(
        label = label,
        command = "route:$label",
        dismissAfterInvoke = true,
        commandSequence = map { direction -> "go $direction" },
    )
}

private val WALK_DIRECTIONS =
    listOf(
        "north" to MapPoint(0, -1),
        "south" to MapPoint(0, 1),
        "east" to MapPoint(1, 0),
        "west" to MapPoint(-1, 0),
        "northeast" to MapPoint(1, -1),
        "northwest" to MapPoint(-1, -1),
        "southeast" to MapPoint(1, 1),
        "southwest" to MapPoint(-1, 1),
    )

private fun locateCenter(rows: List<String>): Pair<Int, Int> {
    rows.forEachIndexed { rowIndex, row ->
        val index = row.mapCellTokens().indexOfFirst { token -> token == "你" || token == "@" }
        if (index >= 0) return rowIndex to index
    }
    return rows.size / 2 to (rows.maxOfOrNull { it.mapCellTokens().size } ?: 1) / 2
}

private fun String.mapCellTokens(): List<String> {
    return trim().split(Regex("\\s+")).filter { it.isNotBlank() }
}
