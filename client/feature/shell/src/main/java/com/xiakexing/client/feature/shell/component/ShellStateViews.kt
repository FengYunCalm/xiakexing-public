package com.xiakexing.client.feature.shell.component

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.xiakexing.client.feature.shell.ui.ShellChrome
import com.xiakexing.client.feature.shell.ui.ShellLines
import com.xiakexing.client.feature.shell.ui.ShellPalette
import com.xiakexing.client.feature.shell.ui.ShellRadius
// 方案 §5.11 全局状态组件：加载 / 空态 / 错误行 / 状态徽标。
// 纯渲染 + 回调，不持有业务状态、不发 Intent；各表面禁止再自造同类组件。

/**
 * 重连覆盖层（方案 §7.5 / R-20）：顶层全屏 ScrimLight + 居中 Ink4 卡。
 * 期间主壳保留但被遮罩防穿透；5s 未成功后展示「重试 / 返回登录」。
 */
@Composable
internal fun ReconnectOverlay(
    timedOut: Boolean,
    onRetry: () -> Unit,
    onBackToLogin: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Box(
        modifier = modifier.background(ShellPalette.ScrimLight),
        contentAlignment = Alignment.Center,
    ) {
        Column(
            modifier =
                Modifier
                    .fillMaxWidth(0.82f)
                    .widthIn(max = 320.dp)
                    .background(ShellPalette.Ink4, ShellRadius.L)
                    .padding(20.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Text(
                text = "连接中断，正在重连…",
                color = ShellPalette.Paper,
                style = MaterialTheme.typography.bodyMedium.copy(fontWeight = FontWeight.SemiBold),
            )
            if (timedOut) {
                Row(
                    modifier =
                        Modifier
                            .fillMaxWidth()
                            .padding(top = 16.dp),
                    horizontalArrangement = Arrangement.spacedBy(10.dp),
                ) {
                    ShellButton(
                        text = "重试",
                        onClick = onRetry,
                        tier = ShellButtonTier.SECONDARY,
                        modifier = Modifier.weight(1f),
                    )
                    ShellButton(
                        text = "返回登录",
                        onClick = onBackToLogin,
                        tier = ShellButtonTier.SECONDARY,
                        modifier = Modifier.weight(1f),
                    )
                }
            }
        }
    }
}

/** 空态：一行解释下一有用事实；可选图标与一个恢复动作。 */
@Composable
internal fun ShellEmpty(
    text: String,
    modifier: Modifier = Modifier,
    actionLabel: String? = null,
    onAction: (() -> Unit)? = null,
) {
    Column(
        modifier =
            modifier
                .fillMaxWidth()
                .heightIn(min = 48.dp)
                .padding(vertical = 12.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        Text(
            text = text,
            color = ShellPalette.PaperMuted,
            style = MaterialTheme.typography.bodyMedium,
            textAlign = TextAlign.Center,
            maxLines = 2,
            overflow = TextOverflow.Ellipsis,
        )
        if (actionLabel != null && onAction != null) {
            UnifiedButtonSurface(
                modifier = Modifier.padding(top = 8.dp),
                onClick = onAction,
                fillColor = ShellPalette.Ink2.copy(alpha = ShellChrome.ControlFillAlpha),
                borderColor = ShellLines.ControlStateBorder,
                minHeight = 36.dp,
            ) {
                Text(
                    text = actionLabel,
                    color = ShellPalette.Accent,
                    style = MaterialTheme.typography.labelMedium,
                )
            }
        }
    }
}

/** 面板内联错误行：朱砂竖条 + 玩家语言文案 + 可选重试。 */
@Composable
internal fun ShellErrorRow(
    text: String,
    modifier: Modifier = Modifier,
    retryLabel: String? = null,
    onRetry: (() -> Unit)? = null,
) {
    Row(
        modifier =
            modifier
                .fillMaxWidth()
                .background(ShellPalette.Ink2.copy(alpha = 0.6f), ShellRadius.M)
                .padding(vertical = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(
            modifier =
                Modifier
                    .width(3.dp)
                    .height(28.dp)
                    .background(ShellPalette.Cinnabar),
        )
        Text(
            text = text,
            color = ShellPalette.Paper,
            style = MaterialTheme.typography.bodyMedium,
            modifier =
                Modifier
                    .weight(1f)
                    .padding(horizontal = 12.dp),
        )
        if (retryLabel != null && onRetry != null) {
            UnifiedButtonSurface(
                modifier = Modifier.padding(end = 10.dp),
                onClick = onRetry,
                fillColor = ShellPalette.Ink2.copy(alpha = ShellChrome.ControlFillAlpha),
                borderColor = ShellLines.ControlStateBorder,
                minHeight = 36.dp,
            ) {
                Text(
                    text = retryLabel,
                    color = ShellPalette.Cinnabar,
                    style = MaterialTheme.typography.labelMedium,
                )
            }
        }
    }
}

/** 状态徽标语义（方案 §5.8）：进行中/可交/已完成/已过期/警告。 */
internal enum class ShellStatusTone {
    PROGRESS,
    CLAIMABLE,
    DONE,
    EXPIRED,
    WARN,
}

private fun ShellStatusTone.color(): Color =
    when (this) {
        ShellStatusTone.PROGRESS -> ShellPalette.Accent
        ShellStatusTone.CLAIMABLE -> ShellPalette.QuestAmber
        ShellStatusTone.DONE -> ShellPalette.JadeGreen
        ShellStatusTone.EXPIRED -> ShellPalette.PaperFaint
        ShellStatusTone.WARN -> ShellPalette.WarnAmber
    }

/** 无边框状态 chip：颜色 + 文字双编码。 */
@Composable
internal fun ShellStatusChip(
    text: String,
    tone: ShellStatusTone,
    modifier: Modifier = Modifier,
) {
    val toneColor = tone.color()
    Box(
        modifier =
            modifier
                .background(toneColor.copy(alpha = 0.14f), ShellRadius.S)
                .padding(horizontal = 6.dp, vertical = 2.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            text = text,
            color = toneColor,
            style = MaterialTheme.typography.labelSmall,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
    }
}
