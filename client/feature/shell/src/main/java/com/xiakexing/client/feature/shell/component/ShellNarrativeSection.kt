package com.xiakexing.client.feature.shell.component

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutVertically
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.ExperimentalComposeApi
import androidx.compose.runtime.NonSkippableComposable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.xiakexing.client.core.protocol.payload.InlineAction
import com.xiakexing.client.feature.shell.ShellAction
import com.xiakexing.client.feature.shell.ShellSheetItem
import com.xiakexing.client.feature.shell.ShellSheetState
import com.xiakexing.client.feature.shell.TranscriptLine
import com.xiakexing.client.feature.shell.popup.InlineInteractionSheet
import com.xiakexing.client.feature.shell.ui.ShellPalette

// 移动刷新修复（311414902）：同 SketchShellLayout，勿删。
@OptIn(ExperimentalComposeApi::class)
@NonSkippableComposable
@Composable
internal fun NarrativePanel(
    visibleTranscript: List<TranscriptLine>,
    scrollRequest: Int,
    modifier: Modifier = Modifier,
    interactionSheet: ShellSheetState? = null,
    renderInteractionSheet: Boolean = true,
    onInteractionSheetDismiss: () -> Unit = {},
    onInteractionSheetBack: () -> Unit = {},
    onInteractionSheetActionClick: (ShellAction) -> Unit = {},
    onInteractionSheetItemClick: (ShellSheetItem) -> Unit = {},
    inlineActions: List<InlineAction> = emptyList(),
    onInlineActionClick: (String) -> Unit = {},
) {
    Box(
        modifier = modifier,
    ) {
        Column(
            modifier =
                Modifier
                    .fillMaxWidth()
                    .fillMaxHeight()
                    .then(
                        if (interactionSheet != null) Modifier.clearAndSetSemantics { } else Modifier,
                    )
                    .padding(horizontal = 8.dp, vertical = 5.dp),
        ) {
            ScrollableLineFeed(
                lines = visibleTranscript,
                emptyText = "",
                forceScrollToken = scrollRequest,
                modifier =
                    Modifier
                        .weight(1f)
                        .fillMaxWidth(),
                // 主文本中的结构化任务反馈可能跨多个协议包；两种包边界都使用同一行距，
                // 避免奖励、进度和完成文本出现不一致的垂直节奏。
                lineSpacing = 5.dp,
                paragraphSpacing = 5.dp,
                lineColor = { line ->
                    when {
                        line.isFeedbackBlock -> ShellPalette.QuestAmber
                        line.text.startsWith(">") -> ShellPalette.Accent
                        else -> ShellPalette.NarrativePaper
                    }
                },
            )
            if (inlineActions.isNotEmpty()) {
                InlineActionRow(
                    actions = inlineActions,
                    onActionClick = onInlineActionClick,
                    modifier =
                        Modifier
                            .fillMaxWidth()
                            .padding(top = 8.dp),
                )
            }
        }

        if (renderInteractionSheet) {
            val hasSheet = interactionSheet != null
            AnimatedVisibility(
                visible = hasSheet,
                enter =
                    slideInVertically(
                        initialOffsetY = { fullHeight -> -(fullHeight * 0.18f).toInt() },
                        animationSpec = tween(durationMillis = 300, easing = FastOutSlowInEasing),
                    ) + fadeIn(),
                exit =
                    slideOutVertically(
                        targetOffsetY = { fullHeight -> -(fullHeight * 0.14f).toInt() },
                        animationSpec = tween(durationMillis = 200, easing = FastOutSlowInEasing),
                    ) + fadeOut(),
                modifier =
                    Modifier
                        .align(Alignment.TopCenter)
                        .fillMaxWidth(),
            ) {
                interactionSheet?.let { sheet ->
                    InlineInteractionSheet(
                        sheet = sheet,
                        onDismiss = onInteractionSheetDismiss,
                        onBack = onInteractionSheetBack,
                        onActionClick = onInteractionSheetActionClick,
                        onItemClick = onInteractionSheetItemClick,
                        modifier =
                            Modifier
                                .fillMaxWidth()
                                .fillMaxHeight(sheet.mainTextOverlayHeightFraction()),
                    )
                }
            }
        }
    }
}

@Composable
internal fun InlineActionRow(
    actions: List<InlineAction>,
    onActionClick: (String) -> Unit,
    modifier: Modifier = Modifier,
) {
    Row(
        modifier = modifier,
        horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        actions.forEach { action ->
            Surface(
                onClick = { onActionClick(action.cmd) },
                shape = RoundedCornerShape(6.dp),
                color = ShellPalette.AccentSoft.copy(alpha = 0.18f),
                contentColor = ShellPalette.Accent,
                border = BorderStroke(1.dp, ShellPalette.Accent.copy(alpha = 0.45f)),
            ) {
                Text(
                    text = action.label,
                    style = MaterialTheme.typography.bodyMedium,
                    fontWeight = FontWeight.Medium,
                    modifier = Modifier.padding(horizontal = 12.dp, vertical = 6.dp),
                )
            }
        }
    }
}
