package com.xiakexing.client.feature.shell.component

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.ExperimentalComposeApi
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.NonSkippableComposable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Constraints
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.TextUnit
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.xiakexing.client.feature.shell.TranscriptLine
import com.xiakexing.client.feature.shell.ui.ShellChrome
import com.xiakexing.client.feature.shell.ui.ShellLines
import com.xiakexing.client.feature.shell.ui.ShellPalette
import com.xiakexing.client.feature.shell.ui.ShellType
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.launch

internal fun Modifier.shellSurfaceChrome(
    fillColor: Color,
    borderColor: Color = ShellLines.SurfaceBoundary,
    shape: Shape = ShellChrome.ControlShape,
    shadowElevation: Dp = ShellChrome.ControlShadowElevation,
    borderWidth: Dp = ShellChrome.DividerThickness,
): Modifier {
    val shadowModifier =
        if (shadowElevation > 0.dp) {
            Modifier.shadow(elevation = shadowElevation, shape = shape, clip = false)
        } else {
            Modifier
        }
    return this
        .then(shadowModifier)
        .background(fillColor, shape)
        .then(
            if (borderWidth > 0.dp && borderColor.alpha > 0f) {
                Modifier.border(borderWidth, borderColor, shape)
            } else {
                Modifier
            },
        )
        .clip(shape)
}

// 移动刷新修复（311414902）：同 SketchShellLayout，勿删。
@OptIn(ExperimentalComposeApi::class)
@NonSkippableComposable
@Composable
internal fun ScrollableLineFeed(
    lines: List<TranscriptLine>,
    emptyText: String,
    modifier: Modifier = Modifier,
    lineSpacing: Dp,
    paragraphSpacing: Dp = lineSpacing,
    lineColor: (TranscriptLine) -> Color,
    forceScrollToken: Int = 0,
) {
    val listState = rememberLazyListState()
    val coroutineScope = rememberCoroutineScope()
    val currentLineCount by rememberUpdatedState(lines.size)
    var autoFollowEnabled by remember { mutableStateOf(true) }
    var pausedLineCount by remember { mutableIntStateOf(lines.size) }
    var lastHandledForceScrollToken by remember { mutableIntStateOf(forceScrollToken) }
    var lastAutoScrolledLineKey by remember { mutableStateOf(lines.lastOrNull()?.key) }
    var programmaticScrollInProgress by remember { mutableStateOf(false) }
    val isAtLatestItem by remember(listState) {
        derivedStateOf { listState.isAtLatestItem() }
    }
    // derivedStateOf 的 key 只放「lambda 捕获的普通参数」；state 委托变量在
    // lambda 内读取总是最新值，作为 key 反而把细粒度失效架空成整块重建。
    val hasPendingForceScroll by remember(forceScrollToken) {
        derivedStateOf { forceScrollToken > lastHandledForceScrollToken }
    }
    val showScrollToLatestButton by remember(lines) {
        derivedStateOf {
            lines.isNotEmpty() && !isAtLatestItem && !autoFollowEnabled && !hasPendingForceScroll
        }
    }
    val pendingNewLineCount by remember(lines) {
        derivedStateOf { (lines.size - pausedLineCount).coerceAtLeast(0) }
    }

    LaunchedEffect(listState) {
        snapshotFlow { listState.isScrollInProgress }
            .distinctUntilChanged()
            .collect { isScrolling ->
                if (isScrolling) {
                    if (!programmaticScrollInProgress) {
                        autoFollowEnabled = false
                        pausedLineCount = currentLineCount
                    }
                } else {
                    autoFollowEnabled = listState.isAtLatestItem()
                    if (autoFollowEnabled) {
                        pausedLineCount = currentLineCount
                    }
                    programmaticScrollInProgress = false
                }
            }
    }

    LaunchedEffect(lines.lastOrNull()?.key, forceScrollToken, autoFollowEnabled) {
        if (lines.isEmpty()) {
            return@LaunchedEffect
        }

        val latestLineKey = lines.last().key
        val shouldForceScroll = forceScrollToken > lastHandledForceScrollToken
        val shouldFollowLatest = autoFollowEnabled && latestLineKey != lastAutoScrolledLineKey

        if (!shouldForceScroll && !shouldFollowLatest) {
            return@LaunchedEffect
        }

        if (!isAtLatestItem) {
            programmaticScrollInProgress = true
            listState.scrollToItem(lines.lastIndex)
            programmaticScrollInProgress = false
        }

        autoFollowEnabled = true
        pausedLineCount = lines.size
        lastAutoScrolledLineKey = latestLineKey
        if (shouldForceScroll) {
            lastHandledForceScrollToken = forceScrollToken
        }
    }

    Box(
        modifier =
            modifier
                .clipToBounds()
                .fillMaxSize(),
    ) {
        Box(modifier = Modifier.fillMaxSize()) {
            LazyColumn(
                modifier =
                    Modifier
                        .fillMaxHeight()
                        .fillMaxWidth(),
                state = listState,
                verticalArrangement = Arrangement.Top,
            ) {
                if (lines.isEmpty()) {
                    item(key = "empty") {
                        Box(modifier = Modifier.fillMaxWidth()) {
                            Text(
                                text = emptyText,
                                color = ShellPalette.PaperMuted,
                                style = MaterialTheme.typography.bodyMedium,
                            )
                        }
                    }
                } else {
                    itemsIndexed(items = lines, key = { _, line -> line.key }) { lineIndex, line ->
                        val followsSameBlock = lineIndex > 0 && lines[lineIndex - 1].blockKey == line.blockKey
                        Box(
                            modifier =
                                Modifier
                                    .fillMaxWidth()
                                    .padding(
                                        top =
                                            if (lineIndex == 0) {
                                                0.dp
                                            } else if (followsSameBlock) {
                                                lineSpacing
                                            } else {
                                                paragraphSpacing
                                            },
                                    ),
                        ) {
                            if (line.isDivider) {
                                // 块边界分隔线：铺满主文本区整行宽度（服务端 `-----`
                                // 标记的呈现形态），颜色随块着色，不渲染标记字符本身。
                                Canvas(
                                    modifier =
                                        Modifier
                                            .fillMaxWidth()
                                            .height(9.dp),
                                ) {
                                    val y = size.height / 2f
                                    drawLine(
                                        color = lineColor(line),
                                        start = Offset(0f, y),
                                        end = Offset(size.width, y),
                                        strokeWidth = 1.dp.toPx(),
                                    )
                                }
                            } else {
                                Text(
                                    text = line.text,
                                    color = lineColor(line),
                                    style = ShellType.Narrative,
                                    softWrap = true,
                                    modifier = Modifier.fillMaxWidth(),
                                )
                            }
                        }
                    }
                }
            }
        }

        if (showScrollToLatestButton) {
            Box(
                modifier =
                    Modifier
                        .align(Alignment.BottomCenter)
                        .padding(bottom = 1.dp)
                        .widthIn(min = 96.dp)
                        .height(44.dp)
                        .clickable(role = Role.Button) {
                            coroutineScope.launch {
                                autoFollowEnabled = true
                                programmaticScrollInProgress = true
                                listState.animateScrollToItem(lines.lastIndex)
                                programmaticScrollInProgress = false
                                pausedLineCount = lines.size
                            }
                        },
                contentAlignment = Alignment.BottomCenter,
            ) {
                Box(
                    modifier =
                        Modifier
                            .widthIn(min = 92.dp)
                            .height(24.dp)
                            .shellSurfaceChrome(
                                fillColor = ShellPalette.CanvasInset.copy(alpha = 0.68f),
                                borderColor =
                                    ShellPalette.DividerLine.copy(
                                        alpha = ShellChrome.DividerLineAlpha,
                                    ),
                                shadowElevation = 2.dp,
                            ),
                    contentAlignment = Alignment.Center,
                ) {
                    Text(
                        text =
                            if (pendingNewLineCount > 0) {
                                "↓ ${pendingNewLineCount}条新消息"
                            } else {
                                "↓ 回到最新"
                            },
                        color = ShellPalette.Accent,
                        style = MaterialTheme.typography.labelSmall,
                        modifier = Modifier.padding(horizontal = 8.dp),
                    )
                }
            }
        }
    }
}

@Composable
internal fun CloseIcon(
    color: Color,
    modifier: Modifier = Modifier,
) {
    Canvas(modifier = modifier) {
        val strokeWidth = 2.dp.toPx()
        val left = size.width * 0.24f
        val right = size.width * 0.76f
        val top = size.height * 0.24f
        val bottom = size.height * 0.76f
        drawLine(
            color = color,
            start = Offset(left, top),
            end = Offset(right, bottom),
            strokeWidth = strokeWidth,
            cap = StrokeCap.Round,
        )
        drawLine(
            color = color,
            start = Offset(right, top),
            end = Offset(left, bottom),
            strokeWidth = strokeWidth,
            cap = StrokeCap.Round,
        )
    }
}

@Composable
internal fun BackArrowIcon(
    color: Color,
    modifier: Modifier = Modifier,
) {
    Canvas(modifier = modifier) {
        val strokeWidth = 2.dp.toPx()
        val midY = size.height / 2f
        val tipX = size.width * 0.28f
        val tailX = size.width * 0.86f
        val wingX = size.width * 0.52f
        val wingTopY = size.height * 0.26f
        val wingBottomY = size.height * 0.74f
        drawLine(
            color = color,
            start = Offset(tipX, midY),
            end = Offset(tailX, midY),
            strokeWidth = strokeWidth,
            cap = StrokeCap.Round,
        )
        drawLine(
            color = color,
            start = Offset(tipX, midY),
            end = Offset(wingX, wingTopY),
            strokeWidth = strokeWidth,
            cap = StrokeCap.Round,
        )
        drawLine(
            color = color,
            start = Offset(tipX, midY),
            end = Offset(wingX, wingBottomY),
            strokeWidth = strokeWidth,
            cap = StrokeCap.Round,
        )
    }
}

/**
 * 弹窗头部关闭按钮的共用配方：透明底 UnifiedButtonSurface + CloseIcon，
 * 尺寸由调用方 modifier 决定（头部行内撑满高 / 悬浮模式固定高）。
 */
@Composable
internal fun SurfaceHeaderCloseButton(
    onClick: () -> Unit,
    testTag: String,
    contentDescriptionText: String,
    modifier: Modifier = Modifier,
) {
    UnifiedButtonSurface(
        modifier =
            modifier
                .testTag(testTag)
                .semantics { this.contentDescription = contentDescriptionText },
        onClick = onClick,
        fillColor = Color.Transparent,
        borderColor = Color.Transparent,
        minHeight = ShellChrome.SurfaceHeaderContentHeight,
        horizontalPadding = 0.dp,
        verticalPadding = 0.dp,
    ) {
        CloseIcon(
            color = ShellPalette.PaperMuted,
            modifier = Modifier.size(ShellChrome.SurfaceHeaderIconSize),
        )
    }
}

@Composable
private fun ChevronDownIcon(
    color: Color,
    modifier: Modifier = Modifier,
) {
    Canvas(modifier = modifier) {
        val strokeWidth = 2.dp.toPx()
        val midX = size.width / 2f
        val topY = size.height * 0.25f
        val bottomY = size.height * 0.75f
        val leftX = size.width * 0.18f
        val rightX = size.width * 0.82f

        drawLine(
            color = color,
            start = Offset(leftX, topY),
            end = Offset(midX, bottomY),
            strokeWidth = strokeWidth,
            cap = StrokeCap.Round,
        )
        drawLine(
            color = color,
            start = Offset(midX, bottomY),
            end = Offset(rightX, topY),
            strokeWidth = strokeWidth,
            cap = StrokeCap.Round,
        )
    }
}

private fun LazyListState.isAtLatestItem(): Boolean {
    val info = layoutInfo
    val totalItems = info.totalItemsCount
    if (totalItems == 0) {
        return true
    }

    val lastVisibleItem = info.visibleItemsInfo.lastOrNull() ?: return true
    val isLastItemVisible = lastVisibleItem.index >= totalItems - 1
    val reachedViewportBottom = lastVisibleItem.offset + lastVisibleItem.size <= info.viewportEndOffset + 4
    return isLastItemVisible && reachedViewportBottom
}

@Composable
internal fun AdaptiveSingleLineText(
    text: String,
    color: Color,
    style: TextStyle,
    modifier: Modifier = Modifier,
    textAlign: TextAlign = TextAlign.Start,
    fontWeight: FontWeight? = null,
    minFontSize: TextUnit = 12.sp,
) {
    val density = LocalDensity.current
    val textMeasurer = androidx.compose.ui.text.rememberTextMeasurer()

    BoxWithConstraints(modifier = modifier) {
        val maxWidthPx = with(density) { maxWidth.roundToPx() }.coerceAtLeast(1)
        val baseFontSize = if (style.fontSize != TextUnit.Unspecified) style.fontSize else 16.sp
        val fontSizes =
            remember(text, maxWidthPx, baseFontSize, minFontSize) {
                // 0.5sp 步长：避免整跳 1sp 留下过大空余；end 钳制到 start 防止空序列。
                val start = (baseFontSize.value * 2).toInt()
                val end = (minFontSize.value * 2).toInt().coerceAtMost(start)
                (start downTo end).map { (it / 2f).sp }
            }
        val fittedFontSize =
            remember(text, maxWidthPx, style, fontWeight, minFontSize, fontSizes) {
                fontSizes.firstOrNull { size ->
                    val result =
                        textMeasurer.measure(
                            text = AnnotatedString(text),
                            style = style.copy(fontSize = size, fontWeight = fontWeight ?: style.fontWeight),
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis,
                            constraints = Constraints(maxWidth = maxWidthPx),
                        )
                    !result.hasVisualOverflow
                } ?: minFontSize
            }

        Text(
            text = text,
            color = color,
            style =
                style.copy(
                    fontSize = fittedFontSize,
                    fontWeight = fontWeight ?: style.fontWeight,
                ),
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            textAlign = textAlign,
            // 不重放调用方 modifier：padding 等若同时作用于 BoxWithConstraints 与内层 Text
            // 会双吃，导致测量宽度大于实际绘制宽度。
            modifier = Modifier.fillMaxWidth(),
        )
    }
}

@Composable
internal fun SectionRule(modifier: Modifier = Modifier) {
    Box(
        modifier =
            modifier
                .fillMaxWidth()
                .height(ShellChrome.DividerThickness)
                .background(ShellLines.StructuralDivider),
    )
}

@Composable
internal fun SectionRuleVertical(modifier: Modifier = Modifier) {
    Box(
        modifier =
            modifier
                .width(ShellChrome.DividerThickness)
                .background(ShellLines.StructuralDivider),
    )
}

@Composable
internal fun EmptyState(text: String) {
    Box(
        modifier =
            Modifier
                .fillMaxWidth()
                .padding(vertical = 12.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            text = text,
            color = ShellPalette.PaperMuted,
            style = MaterialTheme.typography.bodyMedium,
            textAlign = TextAlign.Center,
        )
    }
}
