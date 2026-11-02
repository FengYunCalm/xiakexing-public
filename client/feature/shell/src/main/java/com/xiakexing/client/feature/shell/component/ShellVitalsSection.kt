package com.xiakexing.client.feature.shell.component

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import com.xiakexing.client.core.protocol.payload.PlayerStatusSnapshot
import com.xiakexing.client.feature.shell.CombatOpponentState
import com.xiakexing.client.feature.shell.remainingSecondsAt
import com.xiakexing.client.feature.shell.ui.ShellChrome
import com.xiakexing.client.feature.shell.ui.ShellPalette
import com.xiakexing.client.feature.shell.ui.ShellType
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import java.math.BigDecimal
import java.math.RoundingMode

internal const val PLAYER_VITALS_STRIP_TAG = "player_vitals_strip"
internal const val PLAYER_VITALS_NAME_TAG = "player_vitals_name"
internal const val PLAYER_VITALS_HP_TAG = "player_vitals_hp"
internal const val PLAYER_VITALS_NEILI_TAG = "player_vitals_neili"
internal const val PLAYER_VITALS_JING_TAG = "player_vitals_jing"
internal const val COMBAT_OPPONENT_VITALS_SECTION_TAG = "combat_opponent_vitals_section"
internal const val COMBAT_OPPONENT_VITALS_BOTTOM_DIVIDER_TAG = "combat_opponent_vitals_bottom_divider"
internal const val COMBAT_OPPONENT_VITALS_STRIP_TAG_PREFIX = "combat_opponent_vitals_strip_"
internal const val COMBAT_OPPONENT_VITALS_NAME_TAG_PREFIX = "combat_opponent_vitals_name_"
internal const val PLAYER_VITALS_TOP_DIVIDER_TAG = "player_vitals_top_divider"

internal fun combatOpponentVitalsTag(
    index: Int,
    field: String,
): String = "combat_opponent_vitals_${index}_$field"

@Composable
internal fun PlayerVitalsStrip(
    status: PlayerStatusSnapshot?,
    busySeconds: Int = 0,
    busyCapturedAtMillis: Long = 0L,
    modifier: Modifier = Modifier,
) {
    val remainingBusySeconds = rememberBusySeconds(busySeconds, busyCapturedAtMillis)
    val busy = remainingBusySeconds > 0
    CombatVitalsStrip(
        name = if (busy) "忙乱：$remainingBusySeconds" else status?.name?.ifBlank { "--" } ?: "--",
        nameColor = if (busy) ShellPalette.WarnAmber else ShellPalette.Paper,
        hp = status?.hp ?: 0L,
        hpMax = status?.effHp ?: 0L,
        hpTrueMax = status?.maxHp ?: 0L,
        mp = status?.mp ?: 0L,
        mpMax = status?.effMp ?: 0L,
        mpTrueMax = status?.maxMp ?: 0L,
        ep = status?.ep ?: 0L,
        epMax = status?.maxEp ?: 0L,
        epTrueMax = status?.maxEp ?: 0L,
        stripTag = PLAYER_VITALS_STRIP_TAG,
        nameTag = PLAYER_VITALS_NAME_TAG,
        hpTag = PLAYER_VITALS_HP_TAG,
        mpTag = PLAYER_VITALS_NEILI_TAG,
        epTag = PLAYER_VITALS_JING_TAG,
        modifier = modifier,
    )
}

@Composable
internal fun CombatOpponentVitalsStrip(
    index: Int,
    opponent: CombatOpponentState,
    modifier: Modifier = Modifier,
) {
    CombatVitalsStrip(
        name = opponent.name,
        hp = opponent.vitals.hp ?: 0L,
        hpMax = opponent.vitals.maxHp ?: 0L,
        hpTrueMax = opponent.vitals.maxHp ?: 0L,
        mp = opponent.vitals.mp ?: 0L,
        mpMax = opponent.vitals.maxMp ?: 0L,
        mpTrueMax = opponent.vitals.maxMp ?: 0L,
        ep = opponent.vitals.ep ?: 0L,
        epMax = opponent.vitals.ep ?: 0L,
        epTrueMax = opponent.vitals.maxEp ?: 0L,
        stripTag = "$COMBAT_OPPONENT_VITALS_STRIP_TAG_PREFIX$index",
        nameTag = "$COMBAT_OPPONENT_VITALS_NAME_TAG_PREFIX$index",
        hpTag = combatOpponentVitalsTag(index, "hp"),
        mpTag = combatOpponentVitalsTag(index, "neili"),
        epTag = combatOpponentVitalsTag(index, "jing"),
        modifier = modifier,
    )
}

@Composable
private fun CombatVitalsStrip(
    name: String,
    nameColor: Color = ShellPalette.Paper,
    hp: Long,
    hpMax: Long,
    hpTrueMax: Long,
    mp: Long,
    mpMax: Long,
    mpTrueMax: Long = mpMax,
    ep: Long,
    epMax: Long,
    epTrueMax: Long = epMax,
    stripTag: String,
    nameTag: String,
    hpTag: String,
    mpTag: String,
    epTag: String,
    modifier: Modifier = Modifier,
) {
    val nameWidth = rememberVitalsNameWidth()
    Row(
        modifier =
            modifier
                .testTag(stripTag)
                .semantics(mergeDescendants = true) {
                    contentDescription = "名字、气血、内力、精力状态"
                },
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(
            modifier =
                Modifier
                    .width(nameWidth)
                    .fillMaxHeight()
                    .background(ShellPalette.InkLine.copy(alpha = 0.28f))
                    .testTag(nameTag),
            contentAlignment = Alignment.Center,
        ) {
            Text(
                text = name,
                color = nameColor,
                style = ShellType.Vitals,
                textAlign = TextAlign.Center,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }
        VitalMeter(
            label = "气血",
            current = hp,
            recoverableMax = hpMax,
            trueMax = hpTrueMax,
            color = ShellPalette.VitalHp,
            testTag = hpTag,
            modifier = Modifier.weight(1f),
        )
        VitalMeter(
            label = "内力",
            current = mp,
            recoverableMax = mpMax,
            trueMax = mpTrueMax,
            color = ShellPalette.VitalNeili,
            testTag = mpTag,
            modifier = Modifier.weight(1f),
        )
        VitalMeter(
            label = "精神",
            current = ep,
            recoverableMax = epMax,
            trueMax = epTrueMax,
            color = ShellPalette.VitalJing,
            testTag = epTag,
            modifier = Modifier.weight(1f),
        )
    }
}

@Composable
private fun rememberVitalsNameWidth(): Dp {
    val density = LocalDensity.current
    val textMeasurer = rememberTextMeasurer()
    val widthPx =
        textMeasurer
            .measure(
                ShellChrome.VitalsNameReferenceText,
                style = ShellType.VitalsNameMeasure,
            ).size.width
    return with(density) { widthPx.toDp() + ShellChrome.VitalsNameWidthExtra }
}

@Composable
private fun rememberBusySeconds(
    initialSeconds: Int,
    capturedAtMillis: Long,
): Int {
    var nowMillis by remember(initialSeconds, capturedAtMillis) {
        mutableLongStateOf(System.currentTimeMillis())
    }
    LaunchedEffect(initialSeconds, capturedAtMillis) {
        if (initialSeconds <= 0) {
            nowMillis = System.currentTimeMillis()
            return@LaunchedEffect
        }
        while (isActive) {
            nowMillis = System.currentTimeMillis()
            if (remainingSecondsAt(initialSeconds, capturedAtMillis, nowMillis) <= 0) {
                break
            }
            delay(250L)
        }
    }
    return remainingSecondsAt(initialSeconds, capturedAtMillis, nowMillis)
}

@Composable
private fun VitalMeter(
    label: String,
    current: Long,
    recoverableMax: Long,
    trueMax: Long,
    color: Color,
    testTag: String,
    modifier: Modifier = Modifier,
) {
    val displayMax =
        when {
            recoverableMax > 0 -> recoverableMax
            trueMax > 0 -> trueMax
            else -> 0
        }
    val barMax = if (trueMax > 0) trueMax else displayMax
    // 填充宽度按当前占比收缩（低血不再脉冲闪烁，只用长度表达）。
    val fraction =
        if (barMax > 0) {
            (current.toDouble() / barMax.toDouble()).toFloat().coerceIn(0f, 1f)
        } else {
            0f
        }

    Box(
        modifier =
            modifier
                .fillMaxHeight()
                .background(color.copy(alpha = 0.46f))
                .testTag(testTag),
    ) {
        Box(
            modifier =
                Modifier
                    .fillMaxHeight()
                    .fillMaxWidth(fraction)
                    .testTag("$testTag-fill")
                    .background(color.copy(alpha = 0.72f)),
        )
        Box(
            modifier =
                Modifier
                    .fillMaxSize()
                    .padding(horizontal = ShellChrome.VitalsTextHorizontalInset)
                    .testTag("$testTag-value"),
            contentAlignment = Alignment.CenterStart,
        ) {
            Text(
                text = "$label：${formatVitalAmount(current)}",
                color = ShellPalette.Paper,
                style = ShellType.Vitals,
                maxLines = 1,
                softWrap = false,
                overflow = TextOverflow.Clip,
                modifier = Modifier.fillMaxWidth().testTag("$testTag-text"),
            )
        }
    }
}

internal fun formatVitalAmount(value: Long): String {
    return when {
        value < 10_000L -> value.toString()
        value < 100_000_000L -> formatScaledVitalAmount(value, 10_000L, "万")
        else -> formatScaledVitalAmount(value, 100_000_000L, "亿")
    }
}

private fun formatScaledVitalAmount(
    value: Long,
    divisor: Long,
    unit: String,
): String {
    val scaledValue =
        BigDecimal.valueOf(value)
            .divide(BigDecimal.valueOf(divisor), 2, RoundingMode.HALF_UP)
            .stripTrailingZeros()
            .toPlainString()
    return "$scaledValue$unit"
}
