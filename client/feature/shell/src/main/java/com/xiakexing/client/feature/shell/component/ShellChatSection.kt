package com.xiakexing.client.feature.shell.component

import android.app.Activity
import android.content.Context
import android.content.ContextWrapper
import android.view.inputmethod.InputMethodManager
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import com.xiakexing.client.feature.shell.ui.ShellChrome
import com.xiakexing.client.feature.shell.ui.ShellLines
import com.xiakexing.client.feature.shell.ui.ShellPalette

internal const val CHAT_PANEL_TAG = "chat_panel"
internal const val CHAT_INPUT_TAG = "chat_input"
internal const val CHAT_BAR_TAG = "chat_bar"
internal const val CHAT_SEND_TAG = "chat_send"

/**
 * 聊天输入浮层：
 * - 位置固定屏幕中央（水平垂直居中），与键盘完全解耦——键盘弹出、收起、
 *   高度变化都不移动输入栏；打开即在最终位置直接出现，关闭直接消失；
 * - 键盘在输入栏出现后才拉起；关闭面板即收键盘并清除焦点；
 * - 未发送草稿由 ShellUiState.chatInput 持有，仅发送时清空，
 *   面板开关不丢内容，存活至客户端进程退出。
 */
@Composable
internal fun ChatPanelOverlay(
    open: Boolean,
    input: String,
    onInputChanged: (String) -> Unit,
    onSend: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val view = LocalView.current
    val focusManager = LocalFocusManager.current
    val focusRequester = remember { FocusRequester() }

    LaunchedEffect(open) {
        if (open) {
            runCatching { focusRequester.requestFocus() }
            runCatching {
                val activity =
                    generateSequence(view.context) { ctx ->
                        (ctx as? ContextWrapper)?.baseContext
                    }.filterIsInstance<Activity>().firstOrNull()
                val window =
                    activity?.window
                        ?: error("context chain has no Activity: " + view.context)
                WindowCompat.getInsetsController(window, view)
                    .show(WindowInsetsCompat.Type.ime())
            }.onFailure {
                (
                    view.context.getSystemService(Context.INPUT_METHOD_SERVICE)
                        as? InputMethodManager
                )?.showSoftInput(view, 0)
            }
        } else {
            focusManager.clearFocus(force = true)
            runCatching {
                val activity =
                    generateSequence(view.context) { ctx ->
                        (ctx as? ContextWrapper)?.baseContext
                    }.filterIsInstance<Activity>().firstOrNull()
                val window = activity?.window ?: return@runCatching
                WindowCompat.getInsetsController(window, view)
                    .hide(WindowInsetsCompat.Type.ime())
            }
        }
    }

    if (!open) return

    Box(
        modifier =
            modifier
                .fillMaxSize()
                .testTag(CHAT_PANEL_TAG),
        contentAlignment = Alignment.Center,
    ) {
        Row(
            modifier =
                Modifier
                    .fillMaxWidth()
                    .testTag(CHAT_BAR_TAG)
                    .padding(horizontal = 14.dp),
            horizontalArrangement = Arrangement.spacedBy(10.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            ChatInputField(
                value = input,
                onValueChange = onInputChanged,
                modifier =
                    Modifier
                        .weight(1f)
                        .height(52.dp)
                        .focusRequester(focusRequester),
            )
            UnifiedButtonSurface(
                modifier =
                    Modifier
                        .width(86.dp)
                        .height(52.dp)
                        .testTag(CHAT_SEND_TAG),
                onClick = if (input.isNotBlank()) onSend else null,
                minHeight = 52.dp,
                horizontalPadding = 9.dp,
                fillColor =
                    ShellPalette.CanvasSoft.copy(
                        alpha = if (input.isNotBlank()) 0.76f else 0.46f,
                    ),
                borderColor = ShellSectionLineColor,
            ) {
                Text(
                    text = "发送",
                    color = if (input.isNotBlank()) ShellPalette.Paper else ShellPalette.PaperMuted,
                    style =
                        MaterialTheme.typography.bodyMedium.copy(
                            fontSize = 13.sp,
                            lineHeight = 16.sp,
                        ),
                )
            }
        }
    }
}

@Composable
private fun ChatInputField(
    value: String,
    onValueChange: (String) -> Unit,
    modifier: Modifier = Modifier,
) {
    BasicTextField(
        value = value,
        onValueChange = onValueChange,
        singleLine = true,
        textStyle =
            MaterialTheme.typography.bodyMedium.copy(
                color = ShellPalette.Paper,
                fontSize = 13.sp,
                lineHeight = 16.sp,
            ),
        cursorBrush = SolidColor(ShellPalette.Paper),
        modifier =
            modifier
                .testTag(CHAT_INPUT_TAG),
        decorationBox = { innerTextField ->
            Box(
                modifier =
                    Modifier
                        .fillMaxSize()
                        .clip(RoundedCornerShape(10.dp))
                        .background(ShellPalette.CanvasSoft.copy(alpha = 0.55f))
                        .border(
                            width = ShellChrome.DividerThickness,
                            color = ShellLines.StructuralDivider,
                            shape = RoundedCornerShape(10.dp),
                        )
                        .padding(horizontal = 12.dp),
                contentAlignment = Alignment.CenterStart,
            ) {
                innerTextField()
            }
        },
    )
}
