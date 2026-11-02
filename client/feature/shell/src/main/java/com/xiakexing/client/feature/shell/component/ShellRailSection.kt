package com.xiakexing.client.feature.shell.component

import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.key
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.xiakexing.client.feature.shell.InteractionObject
import com.xiakexing.client.feature.shell.InteractionObjectKind
import com.xiakexing.client.feature.shell.InteractionSheetMode
import com.xiakexing.client.feature.shell.ShellAction
import com.xiakexing.client.feature.shell.ShellSheetState
import com.xiakexing.client.feature.shell.fixedNpcPrimaryActions
import com.xiakexing.client.feature.shell.popup.UnifiedPopupButton
import com.xiakexing.client.feature.shell.popup.interactionShortLabelForDisplay
import com.xiakexing.client.feature.shell.popup.unifiedPopupPanelChrome
import com.xiakexing.client.feature.shell.ui.ShellChrome
import com.xiakexing.client.feature.shell.ui.ShellLines
import com.xiakexing.client.feature.shell.ui.ShellPalette
import com.xiakexing.client.feature.shell.visibleActions

internal const val HORIZONTAL_INTERACTION_ROW_TAG_PREFIX = "horizontal-interaction-row-"
internal const val LEFT_EXIT_ROW_TAG_PREFIX = "left-exit-row-"
internal const val ANCHORED_INTERACTION_ACTION_STRIP_TAG = "anchored-interaction-action-strip"
internal const val ANCHORED_INTERACTION_ACTION_BUTTON_TAG_PREFIX = "anchored-interaction-action-button-"

private val TOPIC_ACTION_ITEM_HEIGHT = 27.dp
private val TOPIC_ACTION_ITEM_SPACING = 5.dp
private val PRIMARY_ACTION_ITEM_HEIGHT = 28.dp
private val PRIMARY_ACTION_ITEM_SPACING = 4.dp
private val ANCHORED_ACTION_BUTTON_MIN_HEIGHT = 27.dp
private val ANCHORED_ACTION_BUTTON_TEXT_SIZE = 11.sp
private const val ANCHORED_ACTION_ROW_DEFAULT_VISIBLE_SLOTS = 4
private const val PRIMARY_ACTION_VISIBLE_SLOTS = 5

@Composable
internal fun LeftRailWithDivider(
    modifier: Modifier = Modifier,
    railWidth: Dp = ShellChrome.RailWidth,
    dividerTag: String? = null,
    content: @Composable () -> Unit,
) {
    val dividerModifier = dividerTag?.let { Modifier.testTag(it) } ?: Modifier
    Row(
        modifier = modifier.width(railWidth + ShellChrome.DividerThickness),
    ) {
        Box(
            modifier =
                Modifier
                    .width(railWidth)
                    .fillMaxHeight(),
        ) {
            content()
        }
        SectionRuleVertical(
            modifier = Modifier.fillMaxHeight().then(dividerModifier),
        )
    }
}

private data class InteractionPanelRefreshContent(
    val interactionObjects: List<InteractionObject>,
    val showDungeonEntry: Boolean,
)

@Composable
internal fun HorizontalPlainListRow(
    title: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    selected: Boolean = false,
) {
    UnifiedButtonSurface(
        modifier = modifier,
        onClick = onClick,
        fillColor =
            if (selected) {
                ShellPalette.CanvasLighter.copy(
                    alpha = ShellChrome.MainControlRaisedFillAlpha,
                )
            } else {
                ShellPalette.CanvasSoft.copy(alpha = ShellChrome.MainControlFillAlpha)
            },
        borderColor = if (selected) ShellLines.SelectedBorder else ShellLines.ControlStateBorder,
        minHeight = ShellChrome.RailExitControlHeight,
        horizontalPadding = ShellChrome.RailControlHorizontalPadding,
        verticalPadding = ShellChrome.RailControlVerticalPadding,
        shape = ShellChrome.ControlShape,
    ) {
        Box(modifier = Modifier.fillMaxWidth(), contentAlignment = Alignment.Center) {
            Text(
                text = title,
                modifier = Modifier.width(ShellChrome.RailScrollableLabelWidth),
                color = if (selected) ShellPalette.Accent else ShellPalette.Paper,
                style =
                    MaterialTheme.typography.bodyMedium.copy(
                        fontSize = 12.sp,
                        lineHeight = 15.sp,
                    ),
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                softWrap = false,
                fontWeight = FontWeight.Medium,
                textAlign = TextAlign.Center,
            )
        }
    }
}

@Composable
internal fun HorizontalInteractionObjectPanel(
    interactionObjects: List<InteractionObject>,
    selectedInteractionObjectId: String,
    onInteractionObjectClick: (String) -> Unit,
    modifier: Modifier = Modifier,
    showDungeonEntry: Boolean = false,
    onDungeonClick: () -> Unit = {},
) {
    val refreshContent =
        remember(interactionObjects, showDungeonEntry) {
            InteractionPanelRefreshContent(interactionObjects, showDungeonEntry)
        }
    Box(
        modifier =
            modifier
                .fillMaxWidth()
                .padding(
                    start = ShellChrome.RailExitStartInset,
                    end = 10.dp,
                    top = 6.dp,
                    bottom = 6.dp,
                ),
        contentAlignment = Alignment.CenterStart,
    ) {
        val listState = rememberLazyListState()
        LazyRow(
            modifier = Modifier.fillMaxWidth(),
            state = listState,
            horizontalArrangement = Arrangement.spacedBy(6.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            itemsIndexed(
                items = refreshContent.interactionObjects,
                key = { index, target -> "${target.kind.name}:${target.id}:$index" },
            ) { _, target ->
                HorizontalInteractionAnimatedRow(
                    key = target.uiKey,
                    title = target.title,
                    selected = target.uiKey == selectedInteractionObjectId,
                    questAvailable = target.questAvailable,
                    onClick = { onInteractionObjectClick(target.uiKey) },
                    modifier =
                        Modifier
                            .size(ShellChrome.RailControlWidth, ShellChrome.RailExitControlHeight)
                            .testTag("$HORIZONTAL_INTERACTION_ROW_TAG_PREFIX${target.id}"),
                )
            }
            if (refreshContent.showDungeonEntry) {
                item(key = "dungeon") {
                    HorizontalInteractionAnimatedRow(
                        key = "dungeon",
                        title = "副本",
                        selected = false,
                        onClick = onDungeonClick,
                        modifier =
                            Modifier
                                .size(
                                    ShellChrome.RailControlWidth,
                                    ShellChrome.RailExitControlHeight,
                                )
                                .testTag("${HORIZONTAL_INTERACTION_ROW_TAG_PREFIX}dungeon"),
                    )
                }
            }
        }
    }
}

@Composable
internal fun HorizontalInteractionActionStrip(
    sheet: ShellSheetState,
    onActionClick: (ShellAction) -> Unit,
    modifier: Modifier = Modifier,
    fixedPrimaryVisibleItems: Int? = null,
    showPanelChrome: Boolean = true,
) {
    val content = remember(sheet) { sheet.anchoredInteractionStripContent() }
    if (content.primaryActions.isEmpty() && content.topicActions.isEmpty()) {
        return
    }

    val stripModifier =
        if (showPanelChrome) {
            modifier
                .fillMaxWidth()
                .unifiedPopupPanelChrome(shadowElevation = ShellChrome.ControlShadowElevation)
                .testTag(ANCHORED_INTERACTION_ACTION_STRIP_TAG)
        } else {
            modifier
                .fillMaxWidth()
                .testTag(ANCHORED_INTERACTION_ACTION_STRIP_TAG)
        }

    Box(
        modifier = stripModifier,
    ) {
        Column(
            modifier =
                Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 7.dp, vertical = 5.dp),
            verticalArrangement = Arrangement.spacedBy(5.dp),
        ) {
            if (content.topicActions.isNotEmpty()) {
                AnchoredInteractionActionRow(
                    actions = content.topicActions,
                    onActionClick = onActionClick,
                    itemHeight = TOPIC_ACTION_ITEM_HEIGHT,
                    itemSpacing = TOPIC_ACTION_ITEM_SPACING,
                )
            }
            val primaryVisibleItems = fixedPrimaryVisibleItems ?: PRIMARY_ACTION_VISIBLE_SLOTS
            AnchoredInteractionActionRow(
                actions = content.primaryActions,
                onActionClick = onActionClick,
                itemHeight = PRIMARY_ACTION_ITEM_HEIGHT,
                itemSpacing = PRIMARY_ACTION_ITEM_SPACING,
                maxVisibleItems = primaryVisibleItems,
                useFixedVisibleSlots = fixedPrimaryVisibleItems != null,
            )
        }
    }
}

@Composable
private fun AnchoredInteractionActionRow(
    actions: List<ShellAction>,
    onActionClick: (ShellAction) -> Unit,
    itemHeight: Dp,
    itemSpacing: Dp,
    modifier: Modifier = Modifier,
    maxVisibleItems: Int = ANCHORED_ACTION_ROW_DEFAULT_VISIBLE_SLOTS,
    useFixedVisibleSlots: Boolean = false,
) {
    if (actions.isEmpty()) {
        return
    }
    BoxWithConstraints(modifier = modifier.fillMaxWidth()) {
        val visibleSlots =
            if (useFixedVisibleSlots) {
                maxVisibleItems.coerceAtLeast(1)
            } else {
                actions.size
                    .coerceAtMost(maxVisibleItems.coerceAtLeast(1))
                    .coerceAtLeast(1)
            }
        val buttonWidth = (maxWidth - itemSpacing * (visibleSlots - 1)) / visibleSlots.toFloat()
        Row(
            modifier =
                Modifier
                    .fillMaxWidth()
                    .horizontalScroll(rememberScrollState()),
            horizontalArrangement = Arrangement.spacedBy(itemSpacing),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            actions.forEach { action ->
                AnchoredInteractionActionButton(
                    action = action,
                    onActionClick = onActionClick,
                    modifier =
                        Modifier
                            .width(buttonWidth)
                            .height(itemHeight),
                )
            }
        }
    }
}

@Composable
private fun AnchoredInteractionActionButton(
    action: ShellAction,
    onActionClick: (ShellAction) -> Unit,
    modifier: Modifier = Modifier,
) {
    UnifiedPopupButton(
        label = action.interactionShortLabelForDisplay(),
        modifier =
            modifier.testTag(
                "$ANCHORED_INTERACTION_ACTION_BUTTON_TAG_PREFIX${action.label}",
            ),
        onClick = { onActionClick(action) },
        minHeight = ANCHORED_ACTION_BUTTON_MIN_HEIGHT,
        textSize = ANCHORED_ACTION_BUTTON_TEXT_SIZE,
    )
}

private data class AnchoredInteractionStripContent(
    val primaryActions: List<ShellAction>,
    val topicActions: List<ShellAction>,
)

private fun ShellSheetState.anchoredInteractionStripContent(): AnchoredInteractionStripContent {
    return if (interactionTargetKind == InteractionObjectKind.NPC) {
        val targetId = interactionTargetId.ifBlank { title }.trim()
        AnchoredInteractionStripContent(
            primaryActions = fixedNpcPrimaryActions(targetId),
            topicActions = if (showsAnchoredConversationTopics()) conversationTopicActions() else emptyList(),
        )
    } else {
        AnchoredInteractionStripContent(
            primaryActions =
                actionGroups.visibleActions(activeActionGroupId).ifEmpty { actions }
                    .filter { action -> action.command.isNotBlank() || action.kind.isNotBlank() }
                    .distinctBy {
                            action ->
                        action.command.trim().lowercase().ifBlank { action.label }
                    },
            topicActions = emptyList(),
        )
    }
}

private fun ShellSheetState.showsAnchoredConversationTopics(): Boolean {
    val activeGroup = activeActionGroupId.ifBlank { "root" }.trim().lowercase()
    return interactionMode == InteractionSheetMode.CONVERSATION ||
        activeGroup == "conversation" ||
        activeGroup == "talk"
}

private fun ShellSheetState.conversationTopicActions(): List<ShellAction> {
    return actionGroups
        .filter { group -> group.id == "conversation" || group.surface == "conversation" }
        .flatMap { group -> group.actions }
        .filter { action -> action.command.trim().lowercase().startsWith("ask ") }
        .distinctBy { action -> action.command.trim().lowercase().ifBlank { action.label } }
}

@Composable
private fun HorizontalInteractionAnimatedRow(
    key: String,
    title: String,
    selected: Boolean,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    questAvailable: Boolean = false,
) {
    Box(modifier = modifier.leftToRightAppear(key = "horizontal:$key:$title")) {
        HorizontalPlainListRow(
            title = title,
            selected = selected,
            onClick = onClick,
            modifier = Modifier.fillMaxSize(),
        )
        if (questAvailable) {
            QuestMarkerBadge(modifier = Modifier.align(Alignment.TopEnd))
        }
    }
}
