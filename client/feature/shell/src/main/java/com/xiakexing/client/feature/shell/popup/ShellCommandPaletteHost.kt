package com.xiakexing.client.feature.shell.popup

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.State
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.withFrameNanos
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.paneTitle
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.xiakexing.client.feature.shell.CommandPaletteState
import com.xiakexing.client.feature.shell.component.ShellSurfaceHeader
import com.xiakexing.client.feature.shell.ui.ShellChrome
import com.xiakexing.client.feature.shell.ui.ShellPalette

const val COMMAND_PALETTE_DISMISS_AREA_TAG = "command_palette_dismiss_area"
const val COMMAND_PALETTE_PANEL_AREA_TAG = "command_palette_panel_area"

private val COMMAND_PALETTE_SURFACE_HORIZONTAL_MARGIN = 6.dp
private val COMMAND_PALETTE_ROW_HORIZONTAL_PADDING = 8.dp
private val COMMAND_PALETTE_ROW_TOP_PADDING = 8.dp
private val COMMAND_PALETTE_ROW_SPACING = 8.dp
private val COMMAND_PALETTE_SEND_MIN_HEIGHT = 52.dp

@Composable
fun ShellCommandPaletteHost(
    palette: CommandPaletteState?,
    draftCommand: String,
    onDismiss: () -> Unit,
    onCommandChanged: (String) -> Unit,
    onSubmit: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val draftCommandState = rememberUpdatedState(draftCommand)
    ShellCommandPaletteHost(
        palette = palette,
        draftCommandState = draftCommandState,
        onDismiss = onDismiss,
        onCommandChanged = onCommandChanged,
        onSubmit = onSubmit,
        modifier = modifier,
    )
}

@Composable
internal fun ShellCommandPaletteHost(
    palette: CommandPaletteState?,
    draftCommandState: State<String>,
    onDismiss: () -> Unit,
    onCommandChanged: (String) -> Unit,
    onSubmit: () -> Unit,
    modifier: Modifier = Modifier,
) {
    if (palette == null) {
        return
    }

    Dialog(
        onDismissRequest = onDismiss,
        properties =
            DialogProperties(
                usePlatformDefaultWidth = false,
                decorFitsSystemWindows = false,
            ),
    ) {
        CommandPaletteContent(
            draftCommandState = draftCommandState,
            onDismiss = onDismiss,
            onCommandChanged = onCommandChanged,
            onSubmit = onSubmit,
            modifier = modifier,
        )
    }
}

@Composable
internal fun CommandPaletteContent(
    draftCommand: String,
    onDismiss: () -> Unit,
    onCommandChanged: (String) -> Unit,
    onSubmit: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val draftCommandState = rememberUpdatedState(draftCommand)
    CommandPaletteContent(
        draftCommandState = draftCommandState,
        onDismiss = onDismiss,
        onCommandChanged = onCommandChanged,
        onSubmit = onSubmit,
        modifier = modifier,
    )
}

@Composable
private fun CommandPaletteContent(
    draftCommandState: State<String>,
    onDismiss: () -> Unit,
    onCommandChanged: (String) -> Unit,
    onSubmit: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val draftCommand = draftCommandState.value
    val inputFocusRequester = remember { FocusRequester() }
    val canSubmit = draftCommand.trim().isNotBlank()
    LaunchedEffect(Unit) {
        withFrameNanos { }
        runCatching { inputFocusRequester.requestFocus() }
    }
    Box(
        modifier =
            modifier
                .fillMaxSize()
                .testTag(COMMAND_PALETTE_DISMISS_AREA_TAG)
                .background(ShellPalette.Overlay.copy(alpha = 0.72f))
                .clickable(onClick = onDismiss)
                .safeDrawingPadding()
                .imePadding(),
        contentAlignment = Alignment.Center,
    ) {
        Surface(
            modifier =
                Modifier
                    .padding(horizontal = COMMAND_PALETTE_SURFACE_HORIZONTAL_MARGIN)
                    .widthIn(max = ShellChrome.PopupPanelMaxWidth)
                    .fillMaxWidth()
                    .semantics { paneTitle = "输入消息" }
                    .unifiedPopupPanelChrome()
                    .clickable(
                        interactionSource = remember { MutableInteractionSource() },
                        indication = null,
                        onClick = {},
                    ),
            color = UnifiedPopupColor,
            shape = UnifiedPopupShape,
            shadowElevation = 0.dp,
        ) {
            Column(
                modifier =
                    Modifier
                        .fillMaxWidth()
                        .testTag(COMMAND_PALETTE_PANEL_AREA_TAG),
            ) {
                ShellSurfaceHeader(
                    title = "命令",
                    onClose = onDismiss,
                    closeContentDescription = "关闭命令面板",
                )
                Row(
                    modifier =
                        Modifier.padding(
                            start = COMMAND_PALETTE_ROW_HORIZONTAL_PADDING,
                            top = COMMAND_PALETTE_ROW_TOP_PADDING,
                            end = COMMAND_PALETTE_ROW_HORIZONTAL_PADDING,
                            bottom = ShellChrome.PopupFooterBottomInset,
                        ),
                    horizontalArrangement = Arrangement.spacedBy(COMMAND_PALETTE_ROW_SPACING),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    OutlinedTextField(
                        value = draftCommand,
                        onValueChange = onCommandChanged,
                        modifier =
                            Modifier
                                .weight(1f)
                                .focusRequester(inputFocusRequester),
                        singleLine = true,
                        placeholder = { Text(text = "说点什么", color = ShellPalette.PaperMuted) },
                        shape = UnifiedPopupControlShape,
                        keyboardOptions =
                            KeyboardOptions(
                                capitalization = KeyboardCapitalization.Sentences,
                                imeAction = ImeAction.Send,
                            ),
                        keyboardActions =
                            KeyboardActions(
                                onSend = { if (canSubmit) onSubmit() },
                            ),
                        colors = unifiedPopupTextFieldColors(),
                    )
                    UnifiedPopupButton(
                        label = "发送",
                        enabled = canSubmit,
                        onClick = onSubmit,
                        minHeight = COMMAND_PALETTE_SEND_MIN_HEIGHT,
                    )
                }
            }
        }
    }
}
