package com.xiakexing.client.feature.shell.component

import androidx.compose.animation.core.LinearOutSlowInEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.sizeIn
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.xiakexing.client.feature.shell.ui.LocalShellReduceMotion
import com.xiakexing.client.feature.shell.ui.ShellChrome
import com.xiakexing.client.feature.shell.ui.ShellLines
import com.xiakexing.client.feature.shell.ui.ShellMotion
import com.xiakexing.client.feature.shell.ui.ShellPalette
import com.xiakexing.client.feature.shell.ui.ShellType

@Composable
internal fun UnifiedButtonSurface(
    modifier: Modifier = Modifier,
    onClick: (() -> Unit)? = null,
    fillColor: Color = ShellPalette.CanvasSoft.copy(alpha = ShellChrome.ControlFillAlpha),
    borderColor: Color = ShellLines.ControlStateBorder,
    minHeight: Dp = ShellChrome.ControlMinHeight,
    horizontalPadding: Dp = ShellChrome.ControlHorizontalPadding,
    verticalPadding: Dp = ShellChrome.ControlVerticalPadding,
    shape: Shape = ShellChrome.ControlShape,
    contentAlignment: Alignment = Alignment.Center,
    showClickIndication: Boolean = false,
    semanticDescription: String = "",
    content: @Composable () -> Unit,
) {
    val interactionSource = remember { MutableInteractionSource() }
    val isPressed by interactionSource.collectIsPressedAsState()
    val reduceMotion = LocalShellReduceMotion.current
    val scale by animateFloatAsState(
        targetValue =
            if (!reduceMotion && isPressed && onClick != null) {
                ShellMotion.PressedScale
            } else {
                1.0f
            },
        animationSpec =
            tween(
                durationMillis = ShellMotion.ReduceMotionMs,
                easing = LinearOutSlowInEasing,
            ),
        label = "button_scale",
    )
    val pressedAlpha by animateFloatAsState(
        targetValue =
            if (reduceMotion && isPressed && onClick != null) {
                ShellMotion.PressedAlpha
            } else {
                1.0f
            },
        animationSpec =
            tween(
                durationMillis = ShellMotion.ReduceMotionMs,
                easing = LinearOutSlowInEasing,
            ),
        label = "button_pressed_alpha",
    )

    val semanticsModifier =
        if (semanticDescription.isBlank()) {
            modifier
        } else {
            modifier.semantics { contentDescription = semanticDescription }
        }

    val chrome =
        semanticsModifier
            .graphicsLayer {
                scaleX = scale
                scaleY = scale
                alpha = pressedAlpha
            }
            .sizeIn(minHeight = minHeight)
            .shellSurfaceChrome(
                fillColor = fillColor,
                borderColor = borderColor,
                shape = shape,
                shadowElevation = 0.dp,
            )

    Box(
        modifier =
            if (onClick != null) {
                chrome.clickable(
                    interactionSource = interactionSource,
                    indication = if (showClickIndication) androidx.compose.foundation.LocalIndication.current else null,
                    role = Role.Button,
                    onClick = onClick,
                )
            } else {
                chrome
            },
        contentAlignment = contentAlignment,
    ) {
        Box(
            modifier = Modifier.padding(horizontal = horizontalPadding, vertical = verticalPadding),
            contentAlignment = contentAlignment,
        ) {
            content()
        }
    }
}

@Composable
internal fun GridExitCell(
    destination: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    highlighted: Boolean = false,
    questAvailable: Boolean = false,
) {
    Box(modifier = modifier, contentAlignment = Alignment.Center) {
        UnifiedButtonSurface(
            modifier = Modifier.fillMaxSize(),
            onClick = onClick,
            fillColor =
                if (highlighted) {
                    ShellPalette.QuestRouteFill
                } else {
                    ShellPalette.Ink2.copy(alpha = 0.18f)
                },
            borderColor =
                if (highlighted) {
                    ShellLines.QuestBorder
                } else {
                    ShellLines.ControlStateBorder
                },
            minHeight = 36.dp,
            horizontalPadding = ShellChrome.GridControlHorizontalPadding,
            verticalPadding = ShellChrome.GridControlVerticalPadding,
            semanticDescription = "出口 $destination",
        ) {
            Box(modifier = Modifier.fillMaxWidth(), contentAlignment = Alignment.Center) {
                Text(
                    text = destination,
                    color = ShellPalette.Paper,
                    style = ShellType.Caption.copy(fontSize = 11.sp, lineHeight = 14.sp),
                    fontWeight = FontWeight.Medium,
                    textAlign = TextAlign.Center,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
            }
        }
        if (questAvailable) {
            QuestMarkerBadge(modifier = Modifier.align(Alignment.TopEnd))
        }
    }
}

@Composable
internal fun RoomAnchorCell(
    title: String,
    modifier: Modifier = Modifier,
    questAvailable: Boolean = false,
) {
    Box(modifier = modifier) {
        UnifiedButtonSurface(
            modifier = Modifier.fillMaxSize(),
            onClick = null,
            fillColor = ShellPalette.Accent.copy(alpha = 0.08f),
            borderColor = ShellPalette.Accent.copy(alpha = 0.52f),
            minHeight = 36.dp,
            horizontalPadding = ShellChrome.GridControlHorizontalPadding,
            verticalPadding = ShellChrome.GridControlVerticalPadding,
            semanticDescription = "当前位置 $title",
        ) {
            Box(modifier = Modifier.fillMaxWidth(), contentAlignment = Alignment.Center) {
                Text(
                    text = title,
                    color = ShellPalette.Accent,
                    style = ShellType.Caption.copy(fontSize = 12.sp, lineHeight = 15.sp),
                    fontWeight = FontWeight.SemiBold,
                    textAlign = TextAlign.Center,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
            }
        }
        if (questAvailable) {
            QuestMarkerBadge(modifier = Modifier.align(Alignment.TopEnd))
        }
    }
}

@Composable
internal fun QuestMarkerBadge(
    modifier: Modifier = Modifier,
    text: String = "!",
) {
    Box(
        modifier =
            modifier
                .size(14.dp)
                .background(ShellPalette.QuestAccent, CircleShape),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            text = text,
            color = ShellPalette.InkBlack,
            style = MaterialTheme.typography.labelSmall.copy(fontSize = 12.sp, lineHeight = 12.sp),
            fontWeight = FontWeight.Bold,
            textAlign = TextAlign.Center,
            maxLines = 1,
        )
    }
}

/** 方案 §5.7 按钮层级：每表面至多 1 个 Primary；危险操作单独远离主按钮。 */
internal enum class ShellButtonTier {
    PRIMARY,
    SECONDARY,
    DANGER,
}

/**
 * 全局统一按钮（方案 §5.7）。
 * - enabled=false：整体 α0.38，保布局尺寸，不响应点击；
 * - loading=true：文字后追加脉冲点并禁止点击（防重复提交）；
 * - 按压反馈 = 0.98 缩放（UnifiedButtonSurface 自带），无 ripple。
 */
@Composable
internal fun ShellButton(
    text: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    tier: ShellButtonTier = ShellButtonTier.SECONDARY,
    enabled: Boolean = true,
    loading: Boolean = false,
    minHeight: Dp = 44.dp,
    semanticDescription: String = "",
) {
    val actionable = enabled && !loading
    val fillColor =
        when (tier) {
            ShellButtonTier.PRIMARY ->
                ShellPalette.Ink3.copy(
                    alpha = ShellChrome.ControlRaisedFillAlpha,
                )
            ShellButtonTier.SECONDARY ->
                ShellPalette.Ink2.copy(
                    alpha = ShellChrome.ControlFillAlpha,
                )
            ShellButtonTier.DANGER -> ShellPalette.Ink2.copy(alpha = ShellChrome.ControlFillAlpha)
        }
    val borderColor =
        when (tier) {
            ShellButtonTier.PRIMARY -> ShellLines.SelectedBorder
            ShellButtonTier.SECONDARY -> ShellLines.ControlStateBorder
            ShellButtonTier.DANGER -> ShellLines.DangerBorder
        }
    val textColor =
        when (tier) {
            ShellButtonTier.PRIMARY -> ShellPalette.Accent
            ShellButtonTier.SECONDARY -> ShellPalette.Paper
            ShellButtonTier.DANGER -> ShellPalette.Cinnabar
        }
    val disabledAlpha = if (enabled) 1f else 0.38f
    UnifiedButtonSurface(
        modifier = modifier.graphicsLayer { alpha = disabledAlpha },
        onClick = if (actionable) onClick else null,
        fillColor = fillColor,
        borderColor = borderColor,
        minHeight = minHeight,
        semanticDescription = semanticDescription,
    ) {
        Row(
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.Center,
        ) {
            Text(
                text = text,
                color = textColor,
                style = MaterialTheme.typography.labelMedium,
                fontWeight = if (tier == ShellButtonTier.PRIMARY) FontWeight.SemiBold else FontWeight.Medium,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            if (loading) {
                ShellLoadingDot(
                    color = textColor,
                    modifier = Modifier.padding(start = 6.dp),
                )
            }
        }
    }
}

/** 按钮/行内复用的脉冲点（加载指示）。 */
@Composable
internal fun ShellLoadingDot(
    color: Color,
    modifier: Modifier = Modifier,
) {
    val pulseTransition = rememberInfiniteTransition(label = "shell_btn_dot")
    val pulseAlpha by pulseTransition.animateFloat(
        initialValue = 0.25f,
        targetValue = 1f,
        animationSpec =
            infiniteRepeatable(
                animation = tween(durationMillis = 900),
                repeatMode = RepeatMode.Reverse,
            ),
        label = "shell_btn_dot_alpha",
    )
    Box(
        modifier =
            modifier
                .size(8.dp)
                .graphicsLayer { alpha = pulseAlpha }
                .background(color, CircleShape),
    )
}
