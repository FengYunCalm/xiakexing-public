package com.xiakexing.client.feature.shell.popup

import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.xiakexing.client.feature.shell.OPEN_INTERACTION_ROOT_COMMAND
import com.xiakexing.client.feature.shell.ShellAction
import com.xiakexing.client.feature.shell.ShellActionGroup
import com.xiakexing.client.feature.shell.ui.ShellChrome

internal const val ACTION_ROWS_SCROLL_HINT_TAG = "action_rows_scroll_hint"

@Composable
internal fun ActionButtonRows(
    actions: List<ShellAction>,
    onActionClick: (ShellAction) -> Unit,
    modifier: Modifier = Modifier,
    isActionEnabled: (ShellAction) -> Boolean = { true },
    alignEnd: Boolean = false,
) {
    val visibleActions =
        remember(actions) {
            actions
                .dedupeActionButtons()
                .filterNot { it.isRedundantDetailAction() }
        }
    if (visibleActions.isEmpty()) {
        return
    }

    CompactActionStrip(
        actions = visibleActions,
        onActionClick = onActionClick,
        modifier = modifier,
        isActionEnabled = isActionEnabled,
        alignEnd = alignEnd,
    )
}

@Composable
internal fun ActionGroupRows(
    groups: List<ShellActionGroup>,
    onActionClick: (ShellAction) -> Unit,
    modifier: Modifier = Modifier,
    isActionEnabled: (ShellAction) -> Boolean = { true },
) {
    val visibleGroups = remember(groups) { groups.filter { it.actions.isNotEmpty() } }
    Column(
        modifier = modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        visibleGroups.forEach { group ->
            Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                ActionButtonRows(
                    actions = group.actions,
                    onActionClick = onActionClick,
                    isActionEnabled = isActionEnabled,
                )
            }
        }
    }
}

@Composable
internal fun ConversationTopicRow(
    actions: List<ShellAction>,
    onActionClick: (ShellAction) -> Unit,
    modifier: Modifier = Modifier,
) {
    ActionButtonGrid(
        actions = actions,
        tone = UnifiedPopupButtonTone.Quiet,
        onActionClick = onActionClick,
        modifier = modifier,
    )
}

@Composable
internal fun NpcRootActionRows(
    sections: NpcRootActionSections,
    onActionClick: (ShellAction) -> Unit,
    modifier: Modifier = Modifier,
    isActionEnabled: (ShellAction) -> Boolean = { true },
) {
    Column(
        modifier = modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        /* 第八轮：任务步骤指引（道谢/交付类）置顶且 Amber 高亮——非常驻、
         * 只在任务到达该步时出现的动作，优先级高于常驻动作（per-action 分类器
         * popupActionTone 对 guided 同样产出 Amber，区级 override 保持一致）。 */
        if (sections.guidanceActions.isNotEmpty()) {
            ActionButtonGrid(
                actions = sections.guidanceActions,
                tone = UnifiedPopupButtonTone.Amber,
                onActionClick = onActionClick,
                isActionEnabled = isActionEnabled,
            )
        }
        if (sections.baseActions.isNotEmpty()) {
            ActionButtonGrid(
                actions = sections.baseActions,
                tone = UnifiedPopupButtonTone.Secondary,
                onActionClick = onActionClick,
                useActionTone = true,
                isActionEnabled = isActionEnabled,
            )
        }
        if (sections.topicActions.isNotEmpty()) {
            ActionButtonGrid(
                actions = sections.topicActions,
                tone = UnifiedPopupButtonTone.Quiet,
                onActionClick = onActionClick,
                isActionEnabled = isActionEnabled,
            )
        }
        if (sections.extensionActions.isNotEmpty()) {
            ActionButtonGrid(
                actions = sections.extensionActions,
                tone = UnifiedPopupButtonTone.Secondary,
                onActionClick = onActionClick,
                isActionEnabled = isActionEnabled,
            )
        }
    }
}

@Composable
internal fun ConversationActionRows(
    actions: List<ShellAction>,
    onActionClick: (ShellAction) -> Unit,
    modifier: Modifier = Modifier,
    isActionEnabled: (ShellAction) -> Boolean = { true },
) {
    val topicActions = remember(actions) { actions.dedupeActionButtons() }
    Column(
        modifier = modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(7.dp),
    ) {
        ActionButtonGrid(
            actions = topicActions,
            tone = UnifiedPopupButtonTone.Quiet,
            onActionClick = onActionClick,
            isActionEnabled = isActionEnabled,
        )
    }
}

@Composable
internal fun InteractionActionButton(
    label: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    tone: UnifiedPopupButtonTone = UnifiedPopupButtonTone.Secondary,
    enabled: Boolean = true,
) {
    UnifiedPopupButton(
        label = label,
        onClick = onClick,
        modifier = modifier.heightIn(min = ShellChrome.PopupActionTouchHeight),
        minHeight = ShellChrome.PopupActionTouchHeight,
        visualHeight = ShellChrome.PopupActionVisualHeight,
        tone = tone,
        enabled = enabled,
        semanticDescription = "动作 $label",
    )
}

@Composable
private fun ActionButtonGrid(
    actions: List<ShellAction>,
    tone: UnifiedPopupButtonTone,
    onActionClick: (ShellAction) -> Unit,
    modifier: Modifier = Modifier,
    useActionTone: Boolean = false,
    isActionEnabled: (ShellAction) -> Boolean = { true },
) {
    val visibleActions = remember(actions) { actions.dedupeActionButtons() }
    if (visibleActions.isEmpty()) {
        return
    }
    CompactActionStrip(
        actions = visibleActions,
        onActionClick = onActionClick,
        modifier = modifier,
        isActionEnabled = isActionEnabled,
        tone = tone.takeUnless { useActionTone },
    )
}

@Composable
private fun CompactActionStrip(
    actions: List<ShellAction>,
    onActionClick: (ShellAction) -> Unit,
    modifier: Modifier = Modifier,
    isActionEnabled: (ShellAction) -> Boolean = { true },
    tone: UnifiedPopupButtonTone? = null,
    alignEnd: Boolean = false,
) {
    val orderedActions =
        remember(actions, tone) {
            if (tone != null) {
                actions
            } else {
                val (plain, emphasis) =
                    actions.partition { action ->
                        action.popupActionTone() !in
                            setOf(
                                UnifiedPopupButtonTone.Primary,
                                UnifiedPopupButtonTone.Danger,
                            )
                    }
                plain + emphasis
            }
        }
    Row(
        modifier =
            modifier
                .fillMaxWidth()
                .horizontalScroll(rememberScrollState()),
        horizontalArrangement =
            Arrangement.spacedBy(
                4.dp,
                if (alignEnd) Alignment.End else Alignment.Start,
            ),
    ) {
        orderedActions.forEach { action ->
            InteractionActionButton(
                label = action.interactionShortLabelForDisplay(),
                onClick = { onActionClick(action) },
                modifier =
                    Modifier.widthIn(
                        min = ShellChrome.PopupActionMinWidth,
                        max = ShellChrome.PopupActionMaxWidth,
                    ),
                tone = tone ?: action.popupActionTone(),
                enabled = isActionEnabled(action),
            )
        }
    }
}

private fun List<ShellAction>.dedupeActionButtons(): List<ShellAction> {
    val seenKeys = linkedSetOf<String>()
    return filter { action ->
        val commandKey = action.command.trim().lowercase().takeIf { it.isNotBlank() }?.let { "cmd:$it" }
        val label = action.interactionShortLabelForDisplay().trim().ifBlank { action.label.trim() }
        val labelKey = label.lowercase().takeIf { it.isNotBlank() }?.let { "label:$it" }
        val keys = listOfNotNull(commandKey, labelKey)
        if (keys.isEmpty() || keys.any { it in seenKeys }) {
            false
        } else {
            seenKeys.addAll(keys)
            true
        }
    }
}

internal fun ShellAction.popupActionTone(): UnifiedPopupButtonTone {
    val normalizedRole = role.trim().lowercase()
    val normalizedKind = kind.trim().lowercase()
    val normalizedCommand = command.trim().lowercase()
    val normalizedLabel = label.trim()
    return when {
        /* 第八轮：guided 步骤指引（道谢/交付）与追踪按钮统一 Amber 高亮——
         * 业务判定收敛在分类器单点，chrome 不再嗅探 label。 */
        guided -> UnifiedPopupButtonTone.Amber
        normalizedCommand.startsWith("ui://quest/track/") || normalizedCommand == "ui://quest/untrack" ->
            UnifiedPopupButtonTone.Amber
        normalizedCommand == "fight" || normalizedCommand.startsWith("fight ") -> UnifiedPopupButtonTone.Secondary
        normalizedRole == "danger" -> UnifiedPopupButtonTone.Danger
        normalizedRole == "primary" -> UnifiedPopupButtonTone.Primary
        normalizedCommand.startsWith("quest complete ") -> UnifiedPopupButtonTone.Primary
        normalizedCommand.startsWith("route ") -> UnifiedPopupButtonTone.Primary
        normalizedCommand.startsWith("dungeon enter ") -> UnifiedPopupButtonTone.Primary
        normalizedCommand.startsWith("go ") -> UnifiedPopupButtonTone.Primary
        normalizedCommand.startsWith("eat ") -> UnifiedPopupButtonTone.Primary
        normalizedCommand.startsWith("drink ") -> UnifiedPopupButtonTone.Primary
        normalizedCommand.startsWith("equip ") -> UnifiedPopupButtonTone.Primary
        normalizedCommand.startsWith("unequip ") -> UnifiedPopupButtonTone.Primary
        normalizedCommand.startsWith("get ") -> UnifiedPopupButtonTone.Primary
        normalizedKind == "back" -> UnifiedPopupButtonTone.Quiet
        normalizedRole == "secondary" -> UnifiedPopupButtonTone.Secondary
        normalizedCommand.startsWith("drop ") -> UnifiedPopupButtonTone.Danger
        normalizedCommand.startsWith("quest abandon ") -> UnifiedPopupButtonTone.Danger
        normalizedCommand.startsWith("kill ") -> UnifiedPopupButtonTone.Danger
        normalizedCommand == "guild leave" -> UnifiedPopupButtonTone.Danger
        normalizedCommand == "guild dismiss" -> UnifiedPopupButtonTone.Danger
        normalizedCommand == "team leave" -> UnifiedPopupButtonTone.Danger
        normalizedCommand == "team dismiss" -> UnifiedPopupButtonTone.Danger
        normalizedCommand.startsWith("team kick") -> UnifiedPopupButtonTone.Danger
        normalizedCommand.startsWith("quest info ") -> UnifiedPopupButtonTone.Quiet
        normalizedCommand == OPEN_INTERACTION_ROOT_COMMAND.lowercase() -> UnifiedPopupButtonTone.Quiet
        normalizedLabel in setOf("返回", "退出", "关闭", "取消", "稍后", "知道了", "刷新") -> UnifiedPopupButtonTone.Quiet
        else -> UnifiedPopupButtonTone.Secondary
    }
}

internal fun ShellAction.isRedundantDetailAction(): Boolean {
    val normalizedCommand = command.trim().lowercase()
    val normalizedLabel = label.trim()
    return (normalizedCommand.startsWith("quest info ") && normalizedLabel in setOf("详情", "刷新")) ||
        (normalizedCommand == "quest" && normalizedLabel == "刷新")
}
