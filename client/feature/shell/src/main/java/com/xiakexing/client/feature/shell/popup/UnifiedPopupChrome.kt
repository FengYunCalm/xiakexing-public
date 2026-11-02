package com.xiakexing.client.feature.shell.popup

import androidx.compose.animation.core.LinearOutSlowInEasing
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.sizeIn
import androidx.compose.foundation.layout.widthIn
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.TextUnit
import androidx.compose.ui.unit.dp
import com.xiakexing.client.feature.shell.ui.LocalShellReduceMotion
import com.xiakexing.client.feature.shell.ui.ShellChrome
import com.xiakexing.client.feature.shell.ui.ShellLines
import com.xiakexing.client.feature.shell.ui.ShellMotion
import com.xiakexing.client.feature.shell.ui.ShellPalette

internal val UnifiedPopupColor: Color = ShellPalette.ChatSurface
internal val UnifiedPopupBorderColor: Color = ShellLines.SurfaceBoundary
internal val UnifiedPopupButtonBorderColor: Color = ShellLines.ControlStateBorder
internal val UnifiedPopupShape = ShellChrome.PanelShape
internal val UnifiedPopupControlShape = ShellChrome.ControlShape
const val POPUP_ACTION_VISUAL_TAG_PREFIX = "popup_action_visual:"
private val UNIFIED_POPUP_BUTTON_HORIZONTAL_PADDING = 10.dp

internal enum class UnifiedPopupButtonTone {
    Primary,
    Secondary,
    Danger,
    Amber,
    Quiet,
}

internal fun Modifier.unifiedPopupPanelChrome(
    shadowElevation: Dp = ShellChrome.PanelShadowElevation,
): Modifier {
    return this
        .shadow(elevation = shadowElevation, shape = UnifiedPopupShape, clip = false)
        .background(UnifiedPopupColor, UnifiedPopupShape)
        .border(
            width = ShellChrome.DividerThickness,
            color = UnifiedPopupBorderColor,
            shape = UnifiedPopupShape,
        )
}

@Composable
internal fun UnifiedPopupMetaText(
    text: String,
    modifier: Modifier = Modifier,
    accent: Boolean = false,
) {
    Text(
        text = text,
        modifier = modifier.padding(horizontal = 2.dp, vertical = 1.dp),
        color = ShellPalette.PaperMuted,
        style =
            MaterialTheme.typography.labelLarge.copy(
                fontWeight = if (accent) FontWeight.Medium else FontWeight.Normal,
            ),
        maxLines = 1,
        overflow = TextOverflow.Ellipsis,
    )
}

@Composable
internal fun UnifiedPopupButton(
    label: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    accent: Boolean = false,
    tone: UnifiedPopupButtonTone = if (accent) UnifiedPopupButtonTone.Primary else UnifiedPopupButtonTone.Secondary,
    enabled: Boolean = true,
    minHeight: Dp = ShellChrome.PopupActionTouchHeight,
    visualHeight: Dp = ShellChrome.PopupActionVisualHeight,
    fillControlWidth: Boolean = false,
    semanticDescription: String = "",
    textSize: TextUnit = TextUnit.Unspecified,
) {
    val interactionSource = remember { MutableInteractionSource() }
    val isPressed by interactionSource.collectIsPressedAsState()
    val reduceMotion = LocalShellReduceMotion.current
    val scale by animateFloatAsState(
        targetValue =
            if (!reduceMotion && isPressed && enabled) {
                ShellMotion.PressedScale
            } else {
                1.0f
            },
        animationSpec =
            tween(
                durationMillis = ShellMotion.ReduceMotionMs,
                easing = LinearOutSlowInEasing,
            ),
        label = "popup_button_scale",
    )
    val pressedAlpha by animateFloatAsState(
        targetValue =
            if (reduceMotion && isPressed && enabled) {
                ShellMotion.PressedAlpha
            } else {
                1.0f
            },
        animationSpec =
            tween(
                durationMillis = ShellMotion.ReduceMotionMs,
                easing = LinearOutSlowInEasing,
            ),
        label = "popup_button_pressed_alpha",
    )
    val semanticsModifier =
        if (semanticDescription.isBlank()) {
            modifier
        } else {
            modifier.semantics { contentDescription = semanticDescription }
        }
    val borderColor =
        when (tone) {
            UnifiedPopupButtonTone.Primary -> ShellLines.SelectedBorder
            UnifiedPopupButtonTone.Secondary -> ShellLines.ControlStateBorder
            UnifiedPopupButtonTone.Danger -> ShellLines.DangerBorder
            UnifiedPopupButtonTone.Amber -> ShellLines.QuestBorder
            UnifiedPopupButtonTone.Quiet -> Color.Transparent
        }
    val backgroundColor =
        when (tone) {
            // 方案 §5.7：Primary=Ink3 底 + Accent 描边 + Accent 文字；Secondary=Ink2 底 + 墨线描边 + 宣纸文字
            UnifiedPopupButtonTone.Primary -> ShellPalette.Accent.copy(alpha = 0.11f)
            UnifiedPopupButtonTone.Secondary -> Color.Transparent
            UnifiedPopupButtonTone.Danger -> ShellPalette.Cinnabar.copy(alpha = 0.08f)
            UnifiedPopupButtonTone.Amber -> ShellPalette.QuestAmber.copy(alpha = 0.10f)
            UnifiedPopupButtonTone.Quiet -> Color.Transparent
        }
    val textColor =
        when (tone) {
            UnifiedPopupButtonTone.Primary -> ShellPalette.Accent
            UnifiedPopupButtonTone.Secondary -> ShellPalette.Paper
            UnifiedPopupButtonTone.Danger -> ShellPalette.Cinnabar
            UnifiedPopupButtonTone.Amber -> ShellPalette.QuestAmber
            UnifiedPopupButtonTone.Quiet -> ShellPalette.PaperMuted
        }
    val textWeight =
        when (tone) {
            UnifiedPopupButtonTone.Primary -> FontWeight.SemiBold
            UnifiedPopupButtonTone.Secondary -> FontWeight.Medium
            UnifiedPopupButtonTone.Danger -> FontWeight.Medium
            UnifiedPopupButtonTone.Amber -> FontWeight.SemiBold
            UnifiedPopupButtonTone.Quiet -> FontWeight.Medium
        }

    Box(
        modifier =
            semanticsModifier
                .graphicsLayer {
                    scaleX = scale
                    scaleY = scale
                    alpha = pressedAlpha
                }
                .sizeIn(minHeight = minHeight)
                .clickable(
                    enabled = enabled,
                    interactionSource = interactionSource,
                    indication = null,
                    role = Role.Button,
                    onClick = onClick,
                ),
        contentAlignment = Alignment.Center,
    ) {
        Box(
            modifier =
                Modifier
                    .height(visualHeight)
                    .then(
                        if (fillControlWidth) {
                            Modifier.fillMaxWidth()
                        } else {
                            Modifier.widthIn(min = ShellChrome.PopupActionMinWidth)
                        },
                    )
                    .then(
                        if (semanticDescription.isBlank()) {
                            Modifier
                        } else {
                            Modifier.testTag("$POPUP_ACTION_VISUAL_TAG_PREFIX$label")
                        },
                    )
                    .background(backgroundColor, UnifiedPopupControlShape)
                    .then(
                        if (borderColor.alpha > 0f) {
                            Modifier.border(
                                width = ShellChrome.DividerThickness,
                                color = borderColor,
                                shape = UnifiedPopupControlShape,
                            )
                        } else {
                            Modifier
                        },
                    )
                    .padding(horizontal = UNIFIED_POPUP_BUTTON_HORIZONTAL_PADDING),
            contentAlignment = Alignment.Center,
        ) {
            Text(
                text = label,
                color = textColor.copy(alpha = if (enabled) 1f else 0.42f),
                textAlign = TextAlign.Center,
                style =
                    MaterialTheme.typography.labelMedium.copy(
                        fontWeight = textWeight,
                        fontSize = textSize,
                    ),
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }
    }
}

@Composable
internal fun unifiedPopupTextFieldColors() =
    OutlinedTextFieldDefaults.colors(
        focusedContainerColor = UnifiedPopupColor,
        unfocusedContainerColor = UnifiedPopupColor,
        focusedBorderColor = ShellLines.SelectedBorder,
        unfocusedBorderColor = UnifiedPopupButtonBorderColor,
        focusedTextColor = ShellPalette.Paper,
        unfocusedTextColor = ShellPalette.Paper,
        cursorColor = ShellPalette.Accent,
    )
