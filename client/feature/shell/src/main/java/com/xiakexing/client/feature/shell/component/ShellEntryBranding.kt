package com.xiakexing.client.feature.shell.component

import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import com.xiakexing.client.feature.shell.ShellPhase
import com.xiakexing.client.feature.shell.ui.ShellPalette

/**
 * 方案 §7.2 / R-17：入口场景品牌区。
 * 游戏名 display 样式 + 一行 slogan；不做插画堆砌，品牌字以文字承载。
 */
@Composable
internal fun EntryBrandHeader(
    modifier: Modifier = Modifier,
    title: String = "侠客行",
    subtitle: String = "",
) {
    Column(
        modifier = modifier.fillMaxWidth(),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Text(
            text = title,
            color = ShellPalette.Paper,
            style = MaterialTheme.typography.displayMedium,
            textAlign = TextAlign.Center,
        )
        if (subtitle.isNotBlank()) {
            Spacer(modifier = Modifier.height(6.dp))
            Text(
                text = subtitle,
                color = ShellPalette.PaperMuted,
                style = MaterialTheme.typography.bodyMedium,
                textAlign = TextAlign.Center,
            )
        }
    }
}

/**
 * 世界加载三段的阶段索引（方案 §7.4 / R-19）。
 * 映射现有 phase 机，不新增协议事实：0=接通山门 1=验明身份 2=踏入江湖。
 */
private fun worldLoadingStageIndex(phase: ShellPhase): Int =
    when (phase) {
        ShellPhase.IDLE,
        ShellPhase.CONNECTING,
        ShellPhase.WAITING_LOGIN_PROMPT,
        -> 0
        ShellPhase.AUTHENTICATING,
        ShellPhase.REGISTERING,
        -> 1
        ShellPhase.WAITING_WORLD,
        ShellPhase.INTERACTIVE,
        -> 2
        ShellPhase.FAILED,
        ShellPhase.SUSPENDED,
        ShellPhase.DISCONNECTED,
        -> 1 // 失败/中断归到「验明身份」行内呈现错误色
    }

private val worldLoadingStageLabels = listOf("接通山门", "验明身份", "踏入江湖")

/**
 * 方案 §7.4 / R-19：世界加载三段进度叙事。
 * 完成行青玉 ✓、当前行石青脉冲、未到行纸灰；失败行转朱砂。
 */
@Composable
internal fun WorldLoadingStageList(
    phase: ShellPhase,
    modifier: Modifier = Modifier,
) {
    val currentIndex = worldLoadingStageIndex(phase)
    val failed =
        phase == ShellPhase.FAILED ||
            phase == ShellPhase.DISCONNECTED ||
            phase == ShellPhase.SUSPENDED

    Column(
        modifier = modifier.fillMaxWidth(),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        worldLoadingStageLabels.forEachIndexed { index, label ->
            WorldLoadingStageRow(
                label = label,
                done = !failed && index < currentIndex,
                current = !failed && index == currentIndex,
                failed = failed && index == currentIndex,
            )
            if (index < worldLoadingStageLabels.lastIndex) {
                Spacer(modifier = Modifier.height(8.dp))
            }
        }
    }
}

@Composable
private fun WorldLoadingStageRow(
    label: String,
    done: Boolean,
    current: Boolean,
    failed: Boolean,
) {
    val pulseTransition = rememberInfiniteTransition(label = "world_stage_pulse")
    val pulseAlpha by pulseTransition.animateFloat(
        initialValue = 0.3f,
        targetValue = 1f,
        animationSpec =
            infiniteRepeatable(
                animation = tween(durationMillis = 900),
                repeatMode = RepeatMode.Reverse,
            ),
        label = "world_stage_dot_alpha",
    )
    Row(
        verticalAlignment = Alignment.CenterVertically,
    ) {
        // 纯文字阶段行：完成=青玉、当前=石青脉冲、失败=朱砂、未到=纸灰（无图标）
        Box(
            modifier = Modifier.size(18.dp),
            contentAlignment = Alignment.Center,
        ) {
            when {
                done ->
                    Box(
                        modifier =
                            Modifier
                                .size(6.dp)
                                .background(ShellPalette.JadeGreen, CircleShape),
                    )
                current ->
                    Box(
                        modifier =
                            Modifier
                                .size(10.dp)
                                .background(
                                    ShellPalette.Accent.copy(alpha = pulseAlpha),
                                    CircleShape,
                                ),
                    )
                failed ->
                    Box(
                        modifier =
                            Modifier
                                .size(6.dp)
                                .background(ShellPalette.Cinnabar, CircleShape),
                    )
                else ->
                    Box(
                        modifier =
                            Modifier
                                .size(8.dp)
                                .background(
                                    ShellPalette.PaperFaint.copy(alpha = 0.4f),
                                    CircleShape,
                                ),
                    )
            }
        }
        Spacer(modifier = Modifier.width(10.dp))
        Text(
            text = label,
            color =
                when {
                    failed -> ShellPalette.Cinnabar
                    done -> ShellPalette.JadeGreen
                    current -> ShellPalette.Accent
                    else -> ShellPalette.PaperFaint
                },
            style = MaterialTheme.typography.bodyMedium,
        )
    }
}
