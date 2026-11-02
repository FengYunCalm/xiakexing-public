package com.xiakexing.client.feature.shell.popup

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.xiakexing.client.feature.shell.ui.ShellPalette

@Composable
internal fun PopupTitleMark(
    title: String,
    titleStyle: TextStyle,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier = modifier,
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(4.dp),
    ) {
        Text(
            text = title,
            color = ShellPalette.Paper,
            style =
                titleStyle.copy(
                    fontWeight = FontWeight.SemiBold,
                    letterSpacing = 0.sp,
                ),
            modifier = Modifier.fillMaxWidth().semantics { heading() },
            textAlign = TextAlign.Center,
            maxLines = 1,
        )
    }
}
