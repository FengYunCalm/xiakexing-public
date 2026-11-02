package com.xiakexing.client.feature.shell.popup

import androidx.compose.animation.core.LinearOutSlowInEasing
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.xiakexing.client.feature.shell.ShellSheetItem
import com.xiakexing.client.feature.shell.ShellSheetItemRole
import com.xiakexing.client.feature.shell.questActionPending
import com.xiakexing.client.feature.shell.ui.LocalShellReduceMotion
import com.xiakexing.client.feature.shell.ui.ShellChrome
import com.xiakexing.client.feature.shell.ui.ShellLines
import com.xiakexing.client.feature.shell.ui.ShellMotion
import com.xiakexing.client.feature.shell.ui.ShellPalette

const val INTERACTION_SUMMARY_BLOCK_TAG = "interaction_summary_block"
const val NPC_QUEST_TASK_ROW_TAG = "npc_quest_task_row"
const val NPC_QUEST_TASK_ACTION_TAG = "npc_quest_task_action"
private const val TASK_LIST_ROW_TAG_PREFIX = "task_list_row:"

private data class TaskListRowModel(
    val family: String,
    val supportingText: String,
    val statusLabel: String = "",
    val actionLabel: String = "",
    val actionTone: UnifiedPopupButtonTone = UnifiedPopupButtonTone.Quiet,
    val accentColor: Color,
    val actionEnabled: Boolean,
    val legacyRowTag: String = "",
    val actionTag: String = "",
)

@Composable
internal fun InteractionSummaryBlock(
    lines: List<String>,
    compact: Boolean = false,
) {
    val summaryText = lines.joinToString("\n\n").trim()
    if (summaryText.isBlank()) {
        return
    }
    val horizontalPadding = if (compact) 8.dp else 10.dp
    val verticalPadding = if (compact) 4.dp else 6.dp
    Box(
        modifier =
            Modifier
                .fillMaxWidth()
                .testTag(INTERACTION_SUMMARY_BLOCK_TAG)
                .padding(horizontal = horizontalPadding, vertical = verticalPadding),
    ) {
        Text(
            text = summaryText,
            color = ShellPalette.PaperMuted,
            style = MaterialTheme.typography.bodyMedium,
        )
    }
}

@Composable
internal fun InteractionNarrativeText(
    lines: List<String>,
    emphasized: Boolean = false,
    compact: Boolean = true,
    npcDialogue: Boolean = false,
) {
    val narrativeText = lines.joinToString("\n\n").trim()
    if (narrativeText.isBlank()) {
        return
    }
    val horizontalPadding = if (compact) 8.dp else 10.dp
    val verticalPadding = if (compact) 2.dp else 4.dp
    Column(
        modifier =
            Modifier
                .fillMaxWidth()
                .padding(horizontal = horizontalPadding, vertical = verticalPadding),
        verticalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        if (npcDialogue) HorizontalDivider(color = ShellPalette.QuestAmber)
        Text(
            text = narrativeText,
            color =
                when {
                    npcDialogue -> ShellPalette.QuestAmber
                    emphasized -> ShellPalette.Paper
                    else -> ShellPalette.PaperMuted
                },
            style = MaterialTheme.typography.bodyMedium,
            modifier = Modifier.fillMaxWidth(),
        )
        if (npcDialogue) HorizontalDivider(color = ShellPalette.QuestAmber)
    }
}

@Composable
internal fun DetailLine(text: String) {
    Box(
        modifier =
            Modifier
                .fillMaxWidth()
                .padding(horizontal = 10.dp, vertical = 6.dp),
    ) {
        Text(
            text = text,
            color = ShellPalette.PaperMuted,
            style = MaterialTheme.typography.bodyMedium,
        )
    }
}

@Composable
internal fun SheetRow(
    item: ShellSheetItem,
    onClick: () -> Unit,
    enabled: Boolean = true,
    modifier: Modifier = Modifier,
) {
    val resolvedRole =
        when {
            item.role != ShellSheetItemRole.AUTO -> item.role
            item.command.isBlank() -> ShellSheetItemRole.INFO
            else -> ShellSheetItemRole.ACTION
        }
    val fallbackTrailing =
        when {
            item.selected -> "当前"
            resolvedRole in setOf(ShellSheetItemRole.ACTION, ShellSheetItemRole.NAVIGATION, ShellSheetItemRole.DANGER) -> "›"
            else -> ""
        }
    val trailingText = item.trailing.ifBlank { fallbackTrailing }
    val hasTrailing = trailingText.isNotBlank()
    val hasSubtitle = item.subtitle.isNotBlank()
    val isClickable = item.command.isNotBlank() && enabled

    val interactionSource = remember { MutableInteractionSource() }
    val isPressed by interactionSource.collectIsPressedAsState()
    val reduceMotion = LocalShellReduceMotion.current
    val scale by animateFloatAsState(
        targetValue =
            if (!reduceMotion && isPressed && isClickable) {
                ShellMotion.PressedScale
            } else {
                1.0f
            },
        animationSpec =
            tween(
                durationMillis = ShellMotion.ReduceMotionMs,
                easing = LinearOutSlowInEasing,
            ),
        label = "sheet_row_scale",
    )
    val pressedAlpha by animateFloatAsState(
        targetValue =
            if (reduceMotion && isPressed && isClickable) {
                ShellMotion.PressedAlpha
            } else {
                1.0f
            },
        animationSpec =
            tween(
                durationMillis = ShellMotion.ReduceMotionMs,
                easing = LinearOutSlowInEasing,
            ),
        label = "sheet_row_pressed_alpha",
    )

    val rowBackground =
        when {
            resolvedRole == ShellSheetItemRole.DANGER -> ShellPalette.Scarlet.copy(alpha = 0.07f)
            item.selected -> ShellPalette.QuestAmber.copy(alpha = 0.12f)
            isPressed -> ShellPalette.Paper.copy(alpha = 0.055f)
            else -> ShellPalette.Ink2.copy(alpha = 0.52f)
        }
    val borderColor =
        when {
            resolvedRole == ShellSheetItemRole.DANGER -> ShellLines.DangerBorder
            item.selected -> ShellLines.QuestBorder
            else -> ShellLines.ControlStateBorder
        }

    Box(
        modifier =
            modifier
                .fillMaxWidth()
                .heightIn(min = ShellChrome.TaskRowMinHeight)
                .graphicsLayer {
                    scaleX = scale
                    scaleY = scale
                    alpha = pressedAlpha
                }
                .background(rowBackground, UnifiedPopupControlShape)
                .border(1.dp, borderColor, UnifiedPopupControlShape)
                .semantics {
                    if (item.command.isNotBlank()) role = Role.Button
                    if (resolvedRole == ShellSheetItemRole.OPTION) selected = item.selected
                }
                .clickable(
                    enabled = isClickable,
                    interactionSource = interactionSource,
                    indication = null,
                    onClick = onClick,
                ),
        contentAlignment = Alignment.Center,
    ) {
        if (hasTrailing || hasSubtitle) {
            Row(
                modifier =
                    Modifier
                        .fillMaxWidth()
                        .padding(horizontal = 12.dp, vertical = 5.dp),
                horizontalArrangement = Arrangement.spacedBy(12.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Column(
                    modifier = Modifier.weight(1f),
                    verticalArrangement = Arrangement.spacedBy(3.dp),
                ) {
                    Text(
                        text = item.title,
                        color = ShellPalette.Paper,
                        style =
                            MaterialTheme.typography.titleMedium.copy(
                                fontWeight = FontWeight.Medium,
                                fontSize = if (item.title.length > 12) 13.sp else 15.sp,
                            ),
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                    if (hasSubtitle) {
                        Text(
                            text = item.subtitle,
                            color = ShellPalette.PaperMuted,
                            style = MaterialTheme.typography.bodySmall.copy(lineHeight = 17.sp),
                            maxLines = 2,
                            overflow = TextOverflow.Ellipsis,
                        )
                    }
                }
                if (hasTrailing) {
                    Text(
                        text = trailingText,
                        color =
                            when {
                                resolvedRole == ShellSheetItemRole.DANGER -> ShellPalette.Scarlet
                                item.selected -> ShellPalette.Accent
                                else -> ShellPalette.PaperMuted
                            },
                        style = MaterialTheme.typography.labelLarge,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                }
            }
        } else {
            Text(
                text = item.title,
                color = ShellPalette.Paper,
                textAlign = TextAlign.Center,
                style = MaterialTheme.typography.titleMedium,
                modifier =
                    Modifier
                        .fillMaxWidth()
                        .padding(horizontal = 12.dp, vertical = 8.dp),
            )
        }
    }
}

@Composable
internal fun QuestTaskRow(
    item: ShellSheetItem,
    onClick: () -> Unit,
    enabled: Boolean = true,
    exposeFamilyTag: Boolean = true,
    modifier: Modifier = Modifier,
) {
    TaskListRow(
        item = item,
        model =
            TaskListRowModel(
                family = "quest",
                supportingText = "",
                statusLabel = item.trailing,
                accentColor = ShellPalette.QuestAccent,
                actionEnabled = false,
            ),
        enabled = enabled,
        exposeFamilyTag = exposeFamilyTag,
        onClick = onClick,
        onActionClick = {},
        modifier = modifier,
    )
}

@Composable
internal fun NpcQuestTaskRow(
    item: ShellSheetItem,
    onClick: () -> Unit,
    onActionClick: () -> Unit,
    enabled: Boolean = true,
    exposeFamilyTag: Boolean = true,
    modifier: Modifier = Modifier,
) {
    val pending = item.questActionPending
    val actionLabel = item.trailing.takeIf { item.actionCommand.isNotBlank() }.orEmpty()
    TaskListRow(
        item = item,
        model =
            TaskListRowModel(
                family = "npc_quest",
                supportingText = "",
                actionLabel = actionLabel,
                actionTone = UnifiedPopupButtonTone.Primary,
                accentColor = ShellPalette.QuestAccent,
                actionEnabled = enabled && !pending,
                legacyRowTag = NPC_QUEST_TASK_ROW_TAG,
                actionTag = NPC_QUEST_TASK_ACTION_TAG,
            ),
        enabled = enabled,
        exposeFamilyTag = exposeFamilyTag,
        onClick = onClick,
        onActionClick = onActionClick,
        modifier = modifier,
    )
}

@Composable
private fun TaskListRow(
    item: ShellSheetItem,
    model: TaskListRowModel,
    enabled: Boolean,
    onClick: () -> Unit,
    onActionClick: () -> Unit,
    exposeFamilyTag: Boolean = true,
    modifier: Modifier = Modifier,
) {
    Box(
        modifier =
            modifier
                .fillMaxWidth()
                .then(
                    if (model.legacyRowTag.isNotBlank()) {
                        Modifier.testTag(
                            model.legacyRowTag,
                        )
                    } else {
                        Modifier
                    },
                ),
    ) {
        Row(
            modifier =
                Modifier
                    .fillMaxWidth()
                    .then(
                        if (exposeFamilyTag) {
                            Modifier.testTag("$TASK_LIST_ROW_TAG_PREFIX${model.family}")
                        } else {
                            Modifier
                        },
                    )
                    .heightIn(min = ShellChrome.TaskListRowMinHeight)
                    .background(
                        color =
                            if (item.selected) {
                                ShellPalette.QuestAmber.copy(
                                    alpha = 0.12f,
                                )
                            } else {
                                ShellPalette.Ink2.copy(alpha = 0.52f)
                            },
                        shape = UnifiedPopupControlShape,
                    )
                    .border(
                        width = 1.dp,
                        color = if (item.selected) ShellLines.QuestBorder else ShellLines.ControlStateBorder,
                        shape = UnifiedPopupControlShape,
                    )
                    .clickable(enabled = enabled && item.command.isNotBlank(), onClick = onClick)
                    .padding(
                        start = ShellChrome.TaskRowHorizontalPadding,
                        top = ShellChrome.TaskRowVerticalPadding,
                        end = 8.dp,
                        bottom = ShellChrome.TaskRowVerticalPadding,
                    ),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(
                modifier = Modifier.weight(1f),
                verticalArrangement = Arrangement.spacedBy(4.dp),
            ) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Text(
                        text = item.title,
                        color = ShellPalette.Paper,
                        style =
                            MaterialTheme.typography.titleMedium.copy(
                                fontWeight = FontWeight.SemiBold,
                                fontSize = if (item.title.length > 12) 13.sp else 15.sp,
                            ),
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                        modifier = Modifier.weight(1f),
                    )
                    if (model.statusLabel.isNotBlank()) {
                        Text(
                            text = model.statusLabel,
                            color = ShellPalette.PaperMuted,
                            style = MaterialTheme.typography.labelSmall,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis,
                        )
                    }
                }
                if (model.supportingText.isNotBlank()) {
                    Text(
                        text = model.supportingText,
                        color = model.accentColor,
                        style = MaterialTheme.typography.labelMedium,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                }
            }
            if (model.actionLabel.isNotBlank()) {
                UnifiedPopupButton(
                    label = model.actionLabel,
                    onClick = onActionClick,
                    tone = model.actionTone,
                    enabled = model.actionEnabled,
                    modifier =
                        if (model.actionTag.isNotBlank()) {
                            Modifier.testTag(
                                model.actionTag,
                            )
                        } else {
                            Modifier
                        },
                )
            }
        }
    }
}

private fun Modifier.drawBottomDivider(): Modifier =
    drawBehind {
        drawLine(
            color = ShellLines.StructuralDivider,
            start = Offset(0f, size.height - 1f),
            end = Offset(size.width, size.height - 1f),
            strokeWidth = 1f,
        )
    }

internal fun ShellSheetItem.asNpcQuestActionItem(): ShellSheetItem {
    val action = actionCommand.trim()
    if (action.isBlank()) {
        return this
    }
    return copy(command = action, dismissAfterInvoke = false)
}
