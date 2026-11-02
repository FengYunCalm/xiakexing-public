package com.xiakexing.client.feature.shell.popup

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.scaleIn
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.paneTitle
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.xiakexing.client.feature.shell.CLIENT_UPDATE_POPUP_META
import com.xiakexing.client.feature.shell.ShellAction
import com.xiakexing.client.feature.shell.ShellPopupKind
import com.xiakexing.client.feature.shell.ShellPopupProgress
import com.xiakexing.client.feature.shell.ShellPopupState
import com.xiakexing.client.feature.shell.allowsOutsideDismiss
import com.xiakexing.client.feature.shell.component.ShellSurfaceHeader
import com.xiakexing.client.feature.shell.ui.LocalShellReduceMotion
import com.xiakexing.client.feature.shell.ui.ShellChrome
import com.xiakexing.client.feature.shell.ui.ShellMotion
import com.xiakexing.client.feature.shell.ui.ShellPalette

const val SHELL_POPUP_DISMISS_AREA_TAG = "shell_popup_dismiss_area"
const val SHELL_POPUP_PANEL_AREA_TAG = "shell_popup_panel_area"
const val SHELL_POPUP_PARENT_PANEL_AREA_TAG = "shell_popup_parent_panel_area"
const val SHELL_POPUP_PROGRESS_TAG = "shell_popup_progress"

private const val POPUP_PANEL_WIDTH_FRACTION = 0.90f
private const val POPUP_COMPACT_DECISION_WIDTH_FRACTION = 0.5f
private const val POPUP_CONFIRMATION_WIDTH_FRACTION = 0.6667f
private val POPUP_PLAIN_HORIZONTAL_PADDING = 14.dp
private val POPUP_CONTENT_HORIZONTAL_PADDING = 12.dp
private val POPUP_PLAIN_TOP_PADDING = 0.dp
private val POPUP_CONTENT_TOP_PADDING = 2.dp
private val POPUP_PLAIN_BOTTOM_PADDING = 6.dp
private val POPUP_CONTENT_BOTTOM_PADDING = 2.dp
private val POPUP_PLAIN_SPACING = 8.dp
private val POPUP_CONTENT_SPACING = 6.dp
private val POPUP_PROGRESS_HEIGHT = 6.dp
private val POPUP_INPUT_MIN_HEIGHT = 48.dp
private val POPUP_FOOTER_HORIZONTAL_PADDING = 10.dp
private val POPUP_FOOTER_TOP_INSET = 2.dp
private val POPUP_FOOTER_ROW_TOP_INSET = 4.dp
private val POPUP_FOOTER_ACTION_SPACING = 4.dp

@Composable
fun ShellPopupHost(
    popup: ShellPopupState?,
    onDismiss: () -> Unit,
    onActionClick: (ShellAction) -> Unit,
    onInputChange: (String) -> Unit,
    modifier: Modifier = Modifier,
) {
    if (popup == null) {
        return
    }
    Dialog(
        onDismissRequest = { if (popup.allowsOutsideDismiss()) onDismiss() },
        properties =
            DialogProperties(
                usePlatformDefaultWidth = false,
                decorFitsSystemWindows = false,
                dismissOnClickOutside = false,
            ),
    ) {
        ShellPopupContent(popup, onDismiss, onActionClick, onInputChange, modifier)
    }
}

@Composable
internal fun ShellPopupContent(
    popup: ShellPopupState,
    onDismiss: () -> Unit,
    onActionClick: (ShellAction) -> Unit,
    onInputChange: (String) -> Unit,
    modifier: Modifier = Modifier,
) {
    val hapticFeedback = LocalHapticFeedback.current
    Box(
        modifier =
            modifier
                .fillMaxSize()
                .background(ShellPalette.Scrim),
        contentAlignment = Alignment.Center,
    ) {
        Box(
            modifier =
                Modifier
                    .fillMaxSize()
                    .testTag(SHELL_POPUP_DISMISS_AREA_TAG)
                    .clickable(onClick = { if (popup.allowsOutsideDismiss()) onDismiss() }),
        )
        val popupEnter =
            if (LocalShellReduceMotion.current) {
                fadeIn(animationSpec = tween(durationMillis = 100))
            } else {
                fadeIn(animationSpec = tween(durationMillis = ShellMotion.MicroMs, easing = ShellMotion.EnterEasing)) +
                    scaleIn(
                        initialScale = 0.96f,
                        animationSpec = tween(durationMillis = ShellMotion.MicroMs, easing = ShellMotion.EnterEasing),
                    )
            }
        AnimatedVisibility(
            visible = true,
            enter = popupEnter,
        ) {
            val isUpdatePopup =
                popup.meta.contains(CLIENT_UPDATE_POPUP_META) ||
                    popup.actions.any {
                        it.label.contains("下载") || it.command.contains("download") || it.command.contains("UPDATE_DOWNLOAD")
                    }
            Box(contentAlignment = Alignment.Center) {
                popup.parent?.let { parent ->
                    PopupPanel(
                        popup = parent,
                        isUpdatePopup = parent.meta.contains(CLIENT_UPDATE_POPUP_META),
                        usePlainTextLayout =
                            parent.usesPlainTextPopupLayout(
                                parent.meta.contains(CLIENT_UPDATE_POPUP_META),
                            ),
                        onDismiss = {},
                        onActionClick = {},
                        onInputChange = {},
                        enabled = false,
                        modifier = Modifier.graphicsLayer { alpha = 0.50f },
                        panelTestTag = SHELL_POPUP_PARENT_PANEL_AREA_TAG,
                    )
                }
                PopupPanel(
                    popup = popup,
                    isUpdatePopup = isUpdatePopup,
                    usePlainTextLayout = popup.usesPlainTextPopupLayout(isUpdatePopup),
                    onDismiss = onDismiss,
                    onActionClick = { action ->
                        if (action.popupActionTone() == UnifiedPopupButtonTone.Danger) {
                            hapticFeedback.performHapticFeedback(HapticFeedbackType.LongPress)
                        }
                        onActionClick(action)
                    },
                    onInputChange = onInputChange,
                )
            }
        }
    }
}

@Composable
private fun PopupPanel(
    popup: ShellPopupState,
    isUpdatePopup: Boolean,
    usePlainTextLayout: Boolean,
    onDismiss: () -> Unit,
    onActionClick: (ShellAction) -> Unit,
    onInputChange: (String) -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    panelTestTag: String = SHELL_POPUP_PANEL_AREA_TAG,
) {
    val isWidenedConfirmationPopup = popup.isConfirmationPopup()
    val isCompactDecisionPopup = popup.isCompactDecisionPopup()
    val maxWidth =
        if (popup.inputAction != null || isUpdatePopup) {
            ShellChrome.PopupContentMaxWidth
        } else {
            ShellChrome.PopupDecisionMaxWidth
        }
    val maxHeight =
        if (isUpdatePopup) {
            ShellChrome.UpdatePopupMaxHeight
        } else {
            ShellChrome.PopupPanelMaxHeight
        }
    val scrollState = rememberScrollState()
    val badgeLines =
        buildList {
            if (popup.badge.isNotBlank()) add(popup.badge)
            if (popup.subtitle.isNotBlank()) add(popup.subtitle)
        }

    Surface(
        modifier =
            modifier
                .fillMaxWidth(
                    when {
                        isWidenedConfirmationPopup -> POPUP_CONFIRMATION_WIDTH_FRACTION
                        isCompactDecisionPopup -> POPUP_COMPACT_DECISION_WIDTH_FRACTION
                        else -> POPUP_PANEL_WIDTH_FRACTION
                    },
                )
                .widthIn(max = maxWidth)
                .testTag(panelTestTag)
                .semantics { paneTitle = popup.title.ifBlank { "弹窗" } }
                .unifiedPopupPanelChrome()
                .clickable(enabled = enabled, onClick = {}),
        color = UnifiedPopupColor,
        shape = UnifiedPopupShape,
        shadowElevation = 0.dp,
    ) {
        Box(modifier = Modifier.fillMaxWidth()) {
            Column(
                modifier =
                    Modifier
                        .heightIn(max = maxHeight)
                        .fillMaxWidth(),
            ) {
                PopupHeader(
                    title = popup.title,
                    badges = badgeLines,
                    usePlainTextLayout = usePlainTextLayout,
                    onClose = onDismiss.takeIf { enabled && popup.shouldShowHeaderClose() },
                )
                Column(
                    modifier =
                        Modifier
                            .weight(1f, fill = false)
                            .verticalScroll(scrollState)
                            .padding(
                                start =
                                    if (usePlainTextLayout) {
                                        POPUP_PLAIN_HORIZONTAL_PADDING
                                    } else {
                                        POPUP_CONTENT_HORIZONTAL_PADDING
                                    },
                                top =
                                    if (usePlainTextLayout) {
                                        POPUP_PLAIN_TOP_PADDING
                                    } else {
                                        POPUP_CONTENT_TOP_PADDING
                                    },
                                end =
                                    if (usePlainTextLayout) {
                                        POPUP_PLAIN_HORIZONTAL_PADDING
                                    } else {
                                        POPUP_CONTENT_HORIZONTAL_PADDING
                                    },
                                bottom =
                                    if (usePlainTextLayout) {
                                        POPUP_PLAIN_BOTTOM_PADDING
                                    } else {
                                        POPUP_CONTENT_BOTTOM_PADDING
                                    },
                            ),
                    verticalArrangement =
                        Arrangement.spacedBy(
                            if (usePlainTextLayout) POPUP_PLAIN_SPACING else POPUP_CONTENT_SPACING,
                        ),
                ) {
                    if (popup.message.isNotBlank()) {
                        PopupBodyBlock(
                            text = popup.message,
                            highlight = isUpdatePopup,
                            usePlainTextLayout = usePlainTextLayout,
                        )
                    }
                    popup.progress?.let { progress ->
                        PopupProgressBlock(progress)
                    }
                    val inputAction = popup.inputAction
                    if (inputAction != null) {
                        PopupInputField(
                            popup = popup,
                            onInputChange = onInputChange,
                        )
                    }
                }
                PopupActions(
                    popup = popup,
                    onDismiss = onDismiss,
                    onActionClick = onActionClick,
                )
            }
        }
    }
}

@Composable
private fun PopupProgressBlock(progress: ShellPopupProgress) {
    val modifier =
        Modifier
            .fillMaxWidth()
            .height(POPUP_PROGRESS_HEIGHT)
            .testTag(SHELL_POPUP_PROGRESS_TAG)
    val fraction = progress.fraction
    if (fraction == null) {
        LinearProgressIndicator(
            modifier = modifier,
            color = ShellPalette.Accent,
            trackColor = ShellPalette.InkLine,
        )
    } else {
        LinearProgressIndicator(
            progress = { fraction.coerceIn(0f, 1f) },
            modifier = modifier,
            color = ShellPalette.Accent,
            trackColor = ShellPalette.InkLine,
        )
    }
}

@Composable
private fun PopupHeader(
    title: String,
    badges: List<String>,
    usePlainTextLayout: Boolean,
    onClose: (() -> Unit)?,
) {
    Column(modifier = Modifier.fillMaxWidth()) {
        ShellSurfaceHeader(
            title = title,
            onClose = onClose,
            closeContentDescription = "关闭弹窗",
        )
        if (badges.isNotEmpty()) {
            Row(
                modifier =
                    Modifier
                        .fillMaxWidth()
                        .horizontalScroll(rememberScrollState())
                        .padding(
                            start = if (usePlainTextLayout) 20.dp else 16.dp,
                            end = if (usePlainTextLayout) 20.dp else 16.dp,
                            bottom = 6.dp,
                        ),
                horizontalArrangement = Arrangement.spacedBy(6.dp),
            ) {
                badges.forEachIndexed { index, badge ->
                    UnifiedPopupMetaText(text = badge, accent = index == 0)
                }
            }
        }
    }
}

@Composable
private fun PopupBodyBlock(
    text: String,
    highlight: Boolean,
    usePlainTextLayout: Boolean,
) {
    Text(
        modifier =
            Modifier
                .fillMaxWidth()
                .padding(horizontal = 0.dp),
        text = text,
        color = if (highlight) ShellPalette.Paper else ShellPalette.PaperMuted,
        style =
            if (highlight || usePlainTextLayout) {
                MaterialTheme.typography.bodyLarge
            } else {
                MaterialTheme.typography.bodyMedium
            },
    )
}

@Composable
private fun PopupInputField(
    popup: ShellPopupState,
    onInputChange: (String) -> Unit,
) {
    OutlinedTextField(
        value = popup.inputValue,
        onValueChange = onInputChange,
        modifier =
            Modifier
                .fillMaxWidth()
                // 第六轮 §2.3：固定 48dp 会裁切文字下半（尤其字体放大时），改最小高度自适应。
                .heightIn(min = POPUP_INPUT_MIN_HEIGHT)
                .padding(top = 2.dp),
        singleLine = true,
        shape = UnifiedPopupControlShape,
        placeholder =
            popup.inputPlaceholder.takeIf { it.isNotBlank() }?.let { hint ->
                {
                    Text(text = hint, color = ShellPalette.PaperMuted)
                }
            },
        colors = unifiedPopupTextFieldColors(),
    )
}

@Composable
private fun PopupActions(
    popup: ShellPopupState,
    onDismiss: () -> Unit,
    onActionClick: (ShellAction) -> Unit,
    modifier: Modifier = Modifier,
) {
    val visibleActions = popup.actions.filterNot { it.isRedundantDetailAction() }
    val isCompactDecisionPopup = popup.isCompactDecisionPopup()
    val dismissLabel =
        popup.primaryLabel
            .takeIf { it.isNotBlank() }
            ?.takeUnless { label -> visibleActions.any { action -> action.label == label } }
            ?.takeUnless { label -> popup.inputAction?.label == label }
    val footerActions =
        buildList {
            addAll(visibleActions)
            popup.inputAction?.let(::add)
        }.let { actions ->
            if (popup.isCompactDecisionPopup()) {
                actions.sortedBy { action -> if (action.isPopupDismissAction()) 1 else 0 }
            } else {
                actions.sortedBy { action -> popup.footerTone(action).sortOrder }
            }
        }
    val compactFooterActionCount = footerActions.size + if (dismissLabel != null) 1 else 0
    val useFullWidthDecisionFooter = popup.isConfirmationPopup() && compactFooterActionCount == 2
    if (footerActions.isEmpty() && dismissLabel == null) {
        return
    }

    Column(
        modifier =
            modifier
                .fillMaxWidth()
                .padding(top = POPUP_FOOTER_TOP_INSET),
    ) {
        Row(
            modifier =
                Modifier
                    .fillMaxWidth()
                    .then(
                        if (useFullWidthDecisionFooter) {
                            Modifier
                        } else {
                            Modifier.horizontalScroll(rememberScrollState())
                        },
                    )
                    .padding(
                        start = POPUP_FOOTER_HORIZONTAL_PADDING,
                        top = POPUP_FOOTER_ROW_TOP_INSET,
                        end = POPUP_FOOTER_HORIZONTAL_PADDING,
                        bottom = ShellChrome.PopupFooterBottomInset,
                    ),
            horizontalArrangement =
                Arrangement.spacedBy(
                    POPUP_FOOTER_ACTION_SPACING,
                    Alignment.End,
                ),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            val footerButtonModifier =
                if (useFullWidthDecisionFooter) Modifier.weight(1f) else Modifier
            if (!isCompactDecisionPopup && dismissLabel != null) {
                PopupFooterButton(
                    label = dismissLabel,
                    tone = UnifiedPopupButtonTone.Quiet,
                    onClick = onDismiss,
                    modifier = footerButtonModifier,
                    fillAvailableWidth = useFullWidthDecisionFooter,
                )
            }
            footerActions.forEach { action ->
                PopupFooterButton(
                    label = action.label,
                    tone = popup.footerTone(action),
                    onClick = { onActionClick(action) },
                    modifier = footerButtonModifier,
                    fillAvailableWidth = useFullWidthDecisionFooter,
                )
            }
            if (isCompactDecisionPopup && dismissLabel != null) {
                PopupFooterButton(
                    label = dismissLabel,
                    tone = UnifiedPopupButtonTone.Primary,
                    onClick = onDismiss,
                    modifier = footerButtonModifier,
                    fillAvailableWidth = useFullWidthDecisionFooter,
                )
            }
        }
    }
}

@Composable
private fun PopupFooterButton(
    label: String,
    tone: UnifiedPopupButtonTone,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    fillAvailableWidth: Boolean = false,
) {
    UnifiedPopupButton(
        label = label,
        onClick = onClick,
        modifier =
            if (fillAvailableWidth) {
                modifier.fillMaxWidth()
            } else {
                modifier.widthIn(
                    min = ShellChrome.PopupActionMinWidth,
                    max = ShellChrome.PopupActionMaxWidth,
                )
            },
        tone = tone,
        fillControlWidth = fillAvailableWidth,
        semanticDescription = "动作 $label",
    )
}

private fun ShellPopupState.footerTone(action: ShellAction): UnifiedPopupButtonTone {
    val baseTone = action.popupActionTone()
    if (action == inputAction) return UnifiedPopupButtonTone.Primary
    if (action.isPopupDismissAction()) {
        return if (isCompactDecisionPopup()) {
            UnifiedPopupButtonTone.Primary
        } else {
            UnifiedPopupButtonTone.Quiet
        }
    }
    if (kind == ShellPopupKind.DANGER_CONFIRMATION && baseTone != UnifiedPopupButtonTone.Quiet) {
        return UnifiedPopupButtonTone.Danger
    }
    if (action.label == primaryLabel) return UnifiedPopupButtonTone.Primary
    if (
        kind in setOf(ShellPopupKind.CONFIRMATION, ShellPopupKind.UPDATE_OPTIONAL, ShellPopupKind.UPDATE_REQUIRED) &&
        actions.count { !it.isPopupDismissAction() } == 1
    ) {
        return UnifiedPopupButtonTone.Primary
    }
    return baseTone
}

private val UnifiedPopupButtonTone.sortOrder: Int
    get() =
        when (this) {
            UnifiedPopupButtonTone.Quiet -> 0
            UnifiedPopupButtonTone.Secondary -> 1
            UnifiedPopupButtonTone.Primary -> 2
            UnifiedPopupButtonTone.Amber -> 3
            UnifiedPopupButtonTone.Danger -> 4
        }

private fun ShellAction.isPopupDismissAction(): Boolean {
    val normalizedLabel = label.trim()
    val normalizedCommand = command.trim().lowercase()
    return normalizedLabel in setOf("取消", "返回", "关闭", "稍后", "知道了") ||
        normalizedCommand.contains("cancel") ||
        normalizedCommand.endsWith("/back")
}

private fun ShellPopupState.shouldShowHeaderClose(): Boolean {
    if (kind in setOf(ShellPopupKind.UPDATE_REQUIRED, ShellPopupKind.BLOCKING_PROGRESS)) return false
    val hasFooterDismiss =
        primaryLabel
            .takeIf { it.isNotBlank() }
            ?.takeUnless {
                    label ->
                actions.any { it.label == label } || inputAction?.label == label
            } != null
    return !hasFooterDismiss && actions.none { it.isPopupDismissAction() }
}

private fun ShellPopupState.isConfirmationPopup(): Boolean {
    return kind == ShellPopupKind.CONFIRMATION || kind == ShellPopupKind.DANGER_CONFIRMATION
}

private fun ShellPopupState.isCompactDecisionPopup(): Boolean {
    return isConfirmationPopup() || kind == ShellPopupKind.UPDATE_OPTIONAL
}

private fun ShellPopupState.usesPlainTextPopupLayout(isUpdatePopup: Boolean): Boolean {
    return !isUpdatePopup &&
        inputAction == null &&
        badge.isBlank() &&
        subtitle.isBlank() &&
        meta.isEmpty()
}
