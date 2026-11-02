package com.xiakexing.client.feature.shell.popup

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import com.xiakexing.client.feature.shell.ui.ShellChrome
import com.xiakexing.client.feature.shell.ui.ShellLines
import com.xiakexing.client.feature.shell.ui.ShellPalette

/** 普通内容保持连续，仅高亮状态形成明确控件表面。 */
internal fun Modifier.sheetTextBoxChrome(highlighted: Boolean = false): Modifier {
    return this
        .background(
            color =
                if (highlighted) {
                    ShellPalette.Accent.copy(alpha = 0.07f)
                } else {
                    Color.Transparent
                },
            shape = UnifiedPopupControlShape,
        )
        .then(
            Modifier.border(
                width = ShellChrome.DividerThickness,
                color = if (highlighted) ShellLines.SelectedBorder else ShellLines.ControlStateBorder,
                shape = UnifiedPopupControlShape,
            ),
        )
}
