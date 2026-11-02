package com.xiakexing.client.feature.shell.ui

import androidx.compose.foundation.Indication
import androidx.compose.foundation.IndicationInstance
import androidx.compose.foundation.LocalIndication
import androidx.compose.foundation.interaction.InteractionSource
import androidx.compose.material3.LocalTextStyle
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.graphics.drawscope.ContentDrawScope
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.Font
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.sp
import com.xiakexing.client.feature.shell.R

private val ShellDarkColors =
    darkColorScheme(
        primary = ShellPalette.Paper,
        onPrimary = ShellPalette.Ink0,
        primaryContainer = ShellPalette.Ink3,
        onPrimaryContainer = ShellPalette.Paper,
        secondary = ShellPalette.PaperMuted,
        onSecondary = ShellPalette.Ink0,
        tertiary = ShellPalette.AccentSoft,
        onTertiary = ShellPalette.Ink0,
        background = ShellPalette.Ink0,
        onBackground = ShellPalette.Paper,
        surface = ShellPalette.Ink1,
        onSurface = ShellPalette.Paper,
        surfaceVariant = ShellPalette.Ink2,
        onSurfaceVariant = ShellPalette.PaperMuted,
        outline = ShellPalette.InkLine,
        error = ShellPalette.Cinnabar,
        onError = ShellPalette.Paper,
    )

internal val ShellFontFamily =
    FontFamily(
        Font(R.font.maple_mono_nl_cn_regular, weight = FontWeight.Normal),
        Font(R.font.maple_mono_nl_cn_semibold, weight = FontWeight.Medium),
        Font(R.font.maple_mono_nl_cn_semibold, weight = FontWeight.SemiBold),
    )

private fun shellTextStyle(
    fontWeight: FontWeight = FontWeight.Normal,
    fontSize: Int,
    lineHeight: Int,
): TextStyle {
    return TextStyle(
        fontWeight = fontWeight,
        fontSize = fontSize.sp,
        lineHeight = lineHeight.sp,
        fontFamily = ShellFontFamily,
    )
}

/**
 * 全客户端统一使用 Maple Mono NL CN；Regular 承载正文，SemiBold 承载中粗及以上层级。
 */
private val ShellTypography =
    Typography(
        displayLarge =
            shellTextStyle(
                fontWeight = FontWeight.SemiBold,
                fontSize = 36,
                lineHeight = 44,
            ),
        displayMedium =
            shellTextStyle(
                fontWeight = FontWeight.SemiBold,
                fontSize = 30,
                lineHeight = 38,
            ),
        displaySmall =
            shellTextStyle(
                fontWeight = FontWeight.SemiBold,
                fontSize = 26,
                lineHeight = 34,
            ),
        headlineLarge =
            shellTextStyle(
                fontWeight = FontWeight.SemiBold,
                fontSize = 26,
                lineHeight = 34,
            ),
        headlineMedium =
            shellTextStyle(
                fontWeight = FontWeight.SemiBold,
                fontSize = 22,
                lineHeight = 30,
            ),
        headlineSmall =
            shellTextStyle(
                fontWeight = FontWeight.SemiBold,
                fontSize = 19,
                lineHeight = 26,
            ),
        titleLarge =
            shellTextStyle(
                fontWeight = FontWeight.SemiBold,
                fontSize = 18,
                lineHeight = 24,
            ),
        titleMedium =
            shellTextStyle(
                fontWeight = FontWeight.Medium,
                fontSize = 15,
                lineHeight = 21,
            ),
        titleSmall =
            shellTextStyle(
                fontWeight = FontWeight.SemiBold,
                fontSize = 13,
                lineHeight = 18,
            ),
        bodyLarge = shellTextStyle(fontSize = 16, lineHeight = 24),
        bodyMedium = shellTextStyle(fontSize = 15, lineHeight = 23),
        bodySmall = shellTextStyle(fontSize = 13, lineHeight = 19),
        labelLarge = shellTextStyle(fontWeight = FontWeight.Medium, fontSize = 15, lineHeight = 22),
        labelMedium =
            shellTextStyle(
                fontWeight = FontWeight.Medium,
                fontSize = 13,
                lineHeight = 18,
            ),
        labelSmall =
            shellTextStyle(
                fontWeight = FontWeight.Medium,
                fontSize = 12,
                lineHeight = 16,
            ),
    )

private object NoRippleIndication : Indication {
    @Composable
    override fun rememberUpdatedInstance(interactionSource: InteractionSource): IndicationInstance {
        return NoRippleIndicationInstance
    }
}

private object NoRippleIndicationInstance : IndicationInstance {
    override fun ContentDrawScope.drawIndication() {
        drawContent()
    }
}

@Composable
fun ShellChromeTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = ShellDarkColors,
        typography = ShellTypography,
        content = {
            CompositionLocalProvider(
                LocalIndication provides NoRippleIndication,
                LocalTextStyle provides LocalTextStyle.current.copy(fontFamily = ShellFontFamily),
            ) {
                content()
            }
        },
    )
}
