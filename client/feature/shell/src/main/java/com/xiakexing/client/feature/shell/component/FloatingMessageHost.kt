package com.xiakexing.client.feature.shell.component

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutVertically
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.xiakexing.client.feature.shell.FloatingMessageState
import com.xiakexing.client.feature.shell.ui.LocalShellReduceMotion
import com.xiakexing.client.feature.shell.ui.ShellChrome
import com.xiakexing.client.feature.shell.ui.ShellPalette
import com.xiakexing.client.feature.shell.withoutExpired
import kotlinx.coroutines.delay

@Composable
internal fun FloatingMessageHost(
    messages: List<FloatingMessageState>,
    modifier: Modifier = Modifier,
) {
    // 第五轮 R7：状态层已按 createdAt 驱逐过期消息；此处只显最新一条（新消息顶替，
    // 用户拍板「不要附带旧消息」）。R2'：侧边距 72→24dp 使 widthIn 上限真实生效。
    val visibleMessages =
        remember(messages) {
            messages.withoutExpired()
                .asReversed()
                .distinctBy { "${it.level.trim().lowercase()}:${it.text.trim()}" }
                .take(1)
                .asReversed()
        }
    Column(
        modifier =
            modifier
                .fillMaxWidth()
                .padding(horizontal = 24.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        visibleMessages.forEach { message ->
            FloatingMessageChip(message = message)
        }
    }
}

@Composable
private fun FloatingMessageChip(message: FloatingMessageState) {
    var visible by remember(message.id) { mutableStateOf(true) }

    LaunchedEffect(message.id, message.ttlMs) {
        delay(message.ttlMs)
        visible = false
    }

    AnimatedVisibility(
        visible = visible,
        enter =
            if (LocalShellReduceMotion.current) {
                fadeIn(animationSpec = tween(durationMillis = 100))
            } else {
                fadeIn(animationSpec = tween(durationMillis = 140, easing = FastOutSlowInEasing)) +
                    slideInVertically(
                        animationSpec = tween(durationMillis = 180, easing = FastOutSlowInEasing),
                        initialOffsetY = { height -> height / 3 },
                    )
            },
        exit =
            if (LocalShellReduceMotion.current) {
                fadeOut(animationSpec = tween(durationMillis = 100))
            } else {
                fadeOut(animationSpec = tween(durationMillis = 420, easing = FastOutSlowInEasing)) +
                    slideOutVertically(
                        animationSpec = tween(durationMillis = 520, easing = FastOutSlowInEasing),
                        targetOffsetY = { height -> -height },
                    )
            },
    ) {
        Text(
            text = message.text,
            color = message.level.floatingTextColor(),
            textAlign = TextAlign.Center,
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
            style = MaterialTheme.typography.bodyMedium.copy(fontWeight = FontWeight.Medium),
            modifier =
                Modifier
                    .widthIn(max = 480.dp) // 第五轮 R2'：配合侧边距收窄，上限真实生效
                    .semantics {
                        liveRegion =
                            if (message.level.isUrgentFloatingMessage()) {
                                LiveRegionMode.Assertive
                            } else {
                                LiveRegionMode.Polite
                            }
                    }
                    .shellSurfaceChrome(
                        fillColor = message.level.floatingFillColor(),
                        borderColor = message.level.floatingBorderColor(),
                        shape = ShellChrome.ControlShape,
                        shadowElevation = 8.dp,
                    )
                    .padding(horizontal = 14.dp, vertical = 8.dp),
        )
    }
}

private fun String.isUrgentFloatingMessage(): Boolean {
    return trim().lowercase() in setOf("warning", "error", "high")
}

private fun String.floatingFillColor(): Color {
    return when (trim().lowercase()) {
        "quest" -> ShellPalette.CanvasRaised.copy(alpha = 0.94f)
        "success" -> ShellPalette.CanvasRaised.copy(alpha = 0.94f)
        "warning", "error", "high" -> ShellPalette.CanvasRaised.copy(alpha = 0.96f)
        else -> ShellPalette.CanvasRaised.copy(alpha = 0.92f)
    }
}

private fun String.floatingBorderColor(): Color {
    return when (trim().lowercase()) {
        "quest" -> ShellPalette.QuestAccent.copy(alpha = 0.48f)
        "success" -> ShellPalette.AccentSoft.copy(alpha = 0.42f)
        "warning", "error", "high" -> ShellPalette.Scarlet.copy(alpha = 0.48f)
        else -> ShellPalette.DividerLine.copy(alpha = ShellChrome.FloatingBorderAlpha)
    }
}

private fun String.floatingTextColor(): Color {
    return when (trim().lowercase()) {
        "quest" -> ShellPalette.QuestAccent.copy(alpha = 0.88f)
        else -> ShellPalette.Paper
    }
}
