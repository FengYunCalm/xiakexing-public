package com.xiakexing.client.feature.shell.component

import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.EnterTransition
import androidx.compose.animation.ExitTransition
import androidx.compose.animation.SizeTransform
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.snap
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.ScrollState
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.State
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveableStateHolder
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.RectangleShape
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.xiakexing.client.core.protocol.payload.PlayerStatusSnapshot
import com.xiakexing.client.feature.shell.ChatDirection
import com.xiakexing.client.feature.shell.ChatMessageItem
import com.xiakexing.client.feature.shell.DockWorkspaceSection
import com.xiakexing.client.feature.shell.PopupSource
import com.xiakexing.client.feature.shell.QuestGuideLaneState
import com.xiakexing.client.feature.shell.ShellAction
import com.xiakexing.client.feature.shell.ShellSheetItem
import com.xiakexing.client.feature.shell.ShellSheetKind
import com.xiakexing.client.feature.shell.ShellSheetState
import com.xiakexing.client.feature.shell.popup.InlineInteractionSheet
import com.xiakexing.client.feature.shell.popup.InlineSheetLayoutMode
import com.xiakexing.client.feature.shell.resolvedSheetKind
import com.xiakexing.client.feature.shell.ui.LocalShellReduceMotion
import com.xiakexing.client.feature.shell.ui.ShellChrome
import com.xiakexing.client.feature.shell.ui.ShellLines
import com.xiakexing.client.feature.shell.ui.ShellMotion
import com.xiakexing.client.feature.shell.ui.ShellPalette
import com.xiakexing.client.feature.shell.ui.shellEnterTransition
import com.xiakexing.client.feature.shell.ui.shellExitTransition

@Composable
internal fun SketchInteractionSheetOverlay(
    sheet: ShellSheetState?,
    onDismiss: () -> Unit,
    onOutsideDismiss: () -> Unit,
    onBack: () -> Unit,
    onActionClick: (ShellAction) -> Unit,
    onItemClick: (ShellSheetItem) -> Unit,
    expandedFunctionLayout: Boolean = false,
    modifier: Modifier = Modifier,
) {
    val hasSheet = sheet != null
    val reduceMotion = LocalShellReduceMotion.current
    val dockWorkspaceStateHolder = rememberSaveableStateHolder()
    Box(modifier = modifier) {
        AnimatedVisibility(
            visible = hasSheet,
            enter = shellEnterTransition(offsetFraction = 0.12f),
            exit = shellExitTransition(offsetFraction = 0.10f),
            modifier = Modifier.matchParentSize(),
        ) {
            Box(
                modifier = Modifier.matchParentSize(),
                contentAlignment = if (sheet?.compactPanel == true) Alignment.TopCenter else Alignment.Center,
            ) {
                sheet?.let { visibleSheet ->
                    if (visibleSheet.navigation.dockSection != null) {
                        AnimatedContent(
                            targetState = visibleSheet,
                            contentKey = { it.navigation.routeKey },
                            transitionSpec = {
                                if (!shouldAnimateDockWorkspaceRoute(
                                        initial = initialState.navigation,
                                        target = targetState.navigation,
                                    )
                                ) {
                                    EnterTransition.None togetherWith ExitTransition.None
                                } else if (reduceMotion) {
                                    (
                                        fadeIn(animationSpec = tween(durationMillis = ShellMotion.ReduceMotionMs)) togetherWith
                                            fadeOut(animationSpec = tween(durationMillis = ShellMotion.ReduceMotionMs))
                                    ).using(
                                        SizeTransform(
                                            clip = false,
                                            sizeAnimationSpec = { _, _ -> snap() },
                                        ),
                                    )
                                } else if (targetState.navigation.depth > initialState.navigation.depth) {
                                    (
                                        slideInHorizontally(
                                            initialOffsetX = { width -> width / 5 },
                                            animationSpec = tween(durationMillis = 180, easing = FastOutSlowInEasing),
                                        ) + fadeIn()
                                    ) togetherWith slideOutHorizontally(
                                        targetOffsetX = { width -> -width / 6 },
                                        animationSpec = tween(durationMillis = 160, easing = FastOutSlowInEasing),
                                    ) + fadeOut()
                                } else {
                                    (
                                        slideInHorizontally(
                                            initialOffsetX = { width -> -width / 6 },
                                            animationSpec = tween(durationMillis = 180, easing = FastOutSlowInEasing),
                                        ) + fadeIn()
                                    ) togetherWith slideOutHorizontally(
                                        targetOffsetX = { width -> width / 5 },
                                        animationSpec = tween(durationMillis = 160, easing = FastOutSlowInEasing),
                                    ) + fadeOut()
                                }
                            },
                            label = "dock_workspace_route",
                        ) { routeSheet ->
                            val saveableStateKey = routeSheet.navigation.rootDockSaveableStateKey()
                            if (saveableStateKey != null) {
                                dockWorkspaceStateHolder.SaveableStateProvider(saveableStateKey) {
                                    SketchInteractionSheetContent(
                                        sheet = routeSheet,
                                        onDismiss = onDismiss,
                                        onOutsideDismiss = onOutsideDismiss,
                                        onBack = onBack,
                                        onActionClick = onActionClick,
                                        onItemClick = onItemClick,
                                        expandedFunctionLayout = expandedFunctionLayout,
                                    )
                                }
                            } else {
                                SketchInteractionSheetContent(
                                    sheet = routeSheet,
                                    onDismiss = onDismiss,
                                    onOutsideDismiss = onOutsideDismiss,
                                    onBack = onBack,
                                    onActionClick = onActionClick,
                                    onItemClick = onItemClick,
                                    expandedFunctionLayout = expandedFunctionLayout,
                                )
                            }
                        }
                    } else {
                        SketchInteractionSheetContent(
                            sheet = visibleSheet,
                            onDismiss = onDismiss,
                            onOutsideDismiss = onOutsideDismiss,
                            onBack = onBack,
                            onActionClick = onActionClick,
                            onItemClick = onItemClick,
                            expandedFunctionLayout = expandedFunctionLayout,
                        )
                    }
                }
            }
        }
    }
}

@Composable
internal fun SketchInteractionSheetContent(
    sheet: ShellSheetState,
    onDismiss: () -> Unit,
    onOutsideDismiss: () -> Unit,
    onBack: () -> Unit,
    onActionClick: (ShellAction) -> Unit,
    onItemClick: (ShellSheetItem) -> Unit,
    expandedFunctionLayout: Boolean = false,
) {
    val usesWorkspaceLayout = expandedFunctionLayout || sheet.isFunctionWorkspace()
    if (sheet.blocksMainTextOverlayDismissArea()) {
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
    val panelModifier =
        if (sheet.compactPanel) {
            Modifier
                .fillMaxWidth()
                .heightIn(max = ShellChrome.CompactItemSheetMaxHeight)
        } else {
            Modifier.fillMaxSize()
        }
    InlineInteractionSheet(
        sheet = sheet,
        onDismiss = onDismiss,
        onBack = onBack,
        onActionClick = onActionClick,
        onItemClick = onItemClick,
        layoutMode =
            if (usesWorkspaceLayout) {
                InlineSheetLayoutMode.FUNCTION_PAGE
            } else {
                InlineSheetLayoutMode.CONTEXT
            },
        modifier =
            panelModifier
                .shellSurfaceChrome(
                    fillColor = ShellPalette.ChatSurface,
                    borderColor = Color.Transparent,
                    shape = RectangleShape,
                    shadowElevation = 0.dp,
                    borderWidth = 0.dp,
                )
                .narrativeOverlayEdges(drawLeft = false, drawBottom = false),
        drawPanelChrome = false,
    )
}

@Composable
internal fun SketchCombatInteractionSheetOverlay(
    sheet: ShellSheetState?,
    onDismiss: () -> Unit,
    onBack: () -> Unit,
    onActionClick: (ShellAction) -> Unit,
    onItemClick: (ShellSheetItem) -> Unit,
    modifier: Modifier = Modifier,
) {
    AnimatedVisibility(
        visible = sheet != null,
        enter = shellEnterTransition(offsetFraction = -0.18f, durationMs = ShellMotion.EmphasisMs),
        exit = shellExitTransition(offsetFraction = -0.14f),
        modifier = modifier,
    ) {
        sheet?.let { visibleSheet ->
            InlineInteractionSheet(
                sheet = visibleSheet,
                onDismiss = onDismiss,
                onBack = onBack,
                onActionClick = onActionClick,
                onItemClick = onItemClick,
                modifier = Modifier.fillMaxSize(),
            )
        }
    }
}

@Composable
internal fun CompactSketchButton(
    label: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    selected: Boolean = false,
) {
    UnifiedButtonSurface(
        modifier = modifier.semantics { this.selected = selected },
        onClick = onClick,
        minHeight = ShellChrome.SketchCompactButtonHeight,
        horizontalPadding = 4.dp,
        verticalPadding = 2.dp,
        fillColor =
            if (selected) {
                ShellPalette.Accent.copy(alpha = 0.08f)
            } else {
                ShellPalette.Ink2.copy(alpha = 0.30f)
            },
        borderColor =
            if (selected) {
                ShellLines.SelectedBorder
            } else {
                ShellLines.ControlStateBorder
            },
        shape = ShellChrome.ControlShape,
    ) {
        Text(
            text = label,
            color = if (selected) ShellPalette.Accent else ShellPalette.Paper,
            style = MaterialTheme.typography.bodySmall.copy(fontSize = 12.sp, lineHeight = 14.sp),
            fontWeight = FontWeight.Medium,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            textAlign = TextAlign.Center,
        )
    }
}

@Composable
internal fun CompactSketchTextButton(
    label: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    selected: Boolean = false,
) {
    UnifiedButtonSurface(
        modifier = modifier.semantics { this.selected = selected },
        onClick = onClick,
        minHeight = ShellChrome.SketchCompactButtonHeight,
        horizontalPadding = 2.dp,
        verticalPadding = 2.dp,
        fillColor =
            if (selected) {
                ShellPalette.CanvasLighter.copy(alpha = ShellChrome.SketchSelectedFillAlpha)
            } else {
                Color.Transparent
            },
        borderColor = Color.Transparent,
        shape = RectangleShape,
        semanticDescription = label,
    ) {
        Text(
            text = label,
            color = if (selected) ShellPalette.Accent else ShellPalette.Paper,
            style = MaterialTheme.typography.bodySmall.copy(fontSize = 12.sp, lineHeight = 15.sp),
            fontWeight = if (selected) FontWeight.SemiBold else FontWeight.Medium,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            textAlign = TextAlign.Center,
        )
    }
}

@Composable
internal fun TextActionButton(
    label: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    UnifiedButtonSurface(
        modifier = modifier,
        onClick = onClick,
        minHeight = 28.dp,
        horizontalPadding = 4.dp,
        verticalPadding = 2.dp,
        fillColor = Color.Transparent,
        borderColor = Color.Transparent,
    ) {
        Text(
            text = label,
            color = ShellPalette.Paper,
            style = MaterialTheme.typography.bodySmall.copy(fontSize = 12.sp, lineHeight = 14.sp),
            fontWeight = FontWeight.Medium,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            textAlign = TextAlign.Center,
        )
    }
}

@Composable
internal fun PlayerVitalsStripState(
    statusSnapshotState: State<PlayerStatusSnapshot?>,
    busySeconds: Int = 0,
    busyCapturedAtMillis: Long = 0L,
    modifier: Modifier = Modifier,
) {
    val statusSnapshot by statusSnapshotState
    PlayerVitalsStrip(
        status = statusSnapshot,
        busySeconds = busySeconds,
        busyCapturedAtMillis = busyCapturedAtMillis,
        modifier = modifier,
    )
}

@Composable
internal fun ChatTickerSection(
    messages: List<ChatMessageItem>,
    onClick: () -> Unit,
    compact: Boolean = false,
    modifier: Modifier = Modifier,
) {
    val visibleMessages = remember(messages) { messages.takeLast(8) }
    val listState = rememberLazyListState()
    LaunchedEffect(visibleMessages.lastOrNull()?.id) {
        if (visibleMessages.isNotEmpty()) {
            listState.scrollToItem(visibleMessages.lastIndex)
        }
    }
    // 方案 R-07：56dp 紧凑双行 + 底部渐隐；整行点击 = 打开聊天覆盖层（与 dock 聊天同一 Intent）
    Box(
        modifier =
            modifier
                .testTag(SKETCH_CHAT_SECTION_TAG)
                .clipToBounds()
                .clickable(
                    interactionSource = remember { MutableInteractionSource() },
                    indication = null,
                    role = Role.Button,
                    onClick = onClick,
                )
                .semantics { contentDescription = "聊天预览，点击打开聊天面板" },
    ) {
        LazyColumn(
            modifier =
                Modifier
                    .fillMaxSize()
                    .padding(horizontal = 10.dp, vertical = 4.dp),
            state = listState,
            verticalArrangement = Arrangement.spacedBy(if (compact) 1.dp else 2.dp),
        ) {
            items(visibleMessages, key = { message -> message.id }) { message ->
                Text(
                    text = "${message.channelName} · ${message.senderName}：${message.content}",
                    color = message.chatTickerColor(),
                    style =
                        MaterialTheme.typography.bodySmall.copy(
                            fontSize = 12.sp,
                            lineHeight = 15.sp,
                        ),
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
            }
        }
        Box(
            modifier =
                Modifier
                    .align(Alignment.BottomCenter)
                    .fillMaxWidth()
                    .height(6.dp)
                    .background(
                        Brush.verticalGradient(
                            0f to Color.Transparent,
                            1f to ShellPalette.Ink0.copy(alpha = 0.80f),
                        ),
                    ),
        )
    }
}

@Composable
internal fun SketchQuestGuideLane(
    state: QuestGuideLaneState,
    onClick: (() -> Unit)?,
    modifier: Modifier = Modifier,
) {
    val scrollState = remember(state.sourceKind, state.questId) { ScrollState(0) }
    val interactionModifier =
        if (onClick != null) {
            modifier.clickable(onClick = onClick)
        } else {
            modifier
        }
    Box(modifier = interactionModifier.testTag(SKETCH_QUEST_LANE_TAG)) {
        Row(
            modifier =
                Modifier.fillMaxSize().padding(horizontal = 10.dp).horizontalScroll(scrollState),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            val textStyle =
                MaterialTheme.typography.bodyMedium.copy(
                    fontSize = 12.sp,
                    lineHeight = 15.sp,
                )
            if (!state.active) {
                // 方案 R-10：idle 态由“近隐形”改为可读引导（PaperFaint 纯文字），点击打开任务列表
                Text(
                    text = state.idleText,
                    color = ShellPalette.PaperFaint,
                    style = textStyle,
                    maxLines = 1,
                    softWrap = false,
                )
                return@Row
            }
            val activeTextColor = ShellPalette.QuestAmber

            Text(
                text = state.questName,
                color = activeTextColor,
                style = textStyle,
                maxLines = 1,
                softWrap = false,
                modifier = Modifier.testTag(SKETCH_QUEST_NAME_TAG),
            )
            if (state.progressText.isNotBlank()) {
                Text(
                    text = "（${state.progressText}）",
                    color = ShellPalette.PaperFaint,
                    style = textStyle,
                    maxLines = 1,
                    modifier = Modifier.testTag(SKETCH_QUEST_PROGRESS_TAG),
                )
            }
            Spacer(modifier = Modifier.width(6.dp))
            Text(
                text = "·",
                color = ShellPalette.PaperFaint,
                style = textStyle,
                maxLines = 1,
            )
            Spacer(modifier = Modifier.width(6.dp))
            Text(
                text = state.actionText,
                color = activeTextColor,
                style = textStyle,
                maxLines = 1,
                softWrap = false,
                modifier = Modifier.testTag(SKETCH_QUEST_ACTION_TAG),
            )
        }
        state.progressFraction?.let { fraction ->
            Box(
                modifier =
                    Modifier
                        .align(Alignment.BottomStart)
                        .fillMaxWidth(fraction)
                        .height(2.dp)
                        .background(ShellPalette.QuestAmber),
            )
        }
    }
}

internal fun ChatMessageItem.chatTickerColor(): Color {
    return when (direction) {
        ChatDirection.SELF -> ShellPalette.QuestAccent
        ChatDirection.SYSTEM -> ShellPalette.PaperMuted
        ChatDirection.INCOMING ->
            when (channel.kind) {
                com.xiakexing.client.feature.shell.ChatChannelKind.WORLD -> ShellPalette.QuestAccent
                com.xiakexing.client.feature.shell.ChatChannelKind.PRIVATE -> ShellPalette.Accent
                com.xiakexing.client.feature.shell.ChatChannelKind.NEARBY -> ShellPalette.Paper
            }
    }
}

internal fun ShellSheetState.isFunctionWorkspace(): Boolean {
    if (navigation.dockSection != null && navigation.depth > 0) {
        return false
    }
    when (navigation.dockSection) {
        DockWorkspaceSection.ATTRIBUTE,
        DockWorkspaceSection.INVENTORY,
        DockWorkspaceSection.SKILL,
        DockWorkspaceSection.ACTIVITY,
        -> return true
        null -> Unit
    }
    if (kind == ShellSheetKind.MAP) {
        return true
    }
    if (source != PopupSource.DOCK_BUTTON) return false
    return when (resolvedSheetKind()) {
        ShellSheetKind.ATTRIBUTE,
        ShellSheetKind.INVENTORY,
        ShellSheetKind.SKILL,
        ShellSheetKind.QUEST,
        -> true
        else -> false
    }
}
