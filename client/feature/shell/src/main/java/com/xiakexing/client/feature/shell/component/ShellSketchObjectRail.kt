package com.xiakexing.client.feature.shell.component

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.RectangleShape
import androidx.compose.ui.graphics.compositeOver
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.xiakexing.client.core.protocol.payload.ExitSnapshot
import com.xiakexing.client.feature.shell.InteractionObject
import com.xiakexing.client.feature.shell.TrackedQuestState
import com.xiakexing.client.feature.shell.allowsQuestExitMarker
import com.xiakexing.client.feature.shell.toCanonicalTravelDirection
import com.xiakexing.client.feature.shell.ui.ShellChrome
import com.xiakexing.client.feature.shell.ui.ShellLines
import com.xiakexing.client.feature.shell.ui.ShellPalette
import com.xiakexing.client.feature.shell.ui.ShellType

private val SPECIAL_EXIT_MIN_HEIGHT = 16.dp

@Composable
internal fun SketchObjectInteractionRail(
    roomIdentity: String,
    interactionObjects: List<InteractionObject>,
    selectedInteractionObjectId: String,
    onInteractionObjectClick: (String) -> Unit,
    modifier: Modifier = Modifier,
) {
    val orderState = remember(roomIdentity) { ObjectRailOrderState() }
    val objects = remember(orderState, interactionObjects) { orderState.update(interactionObjects) }
    Column(
        modifier =
            modifier
                .testTag(SKETCH_OBJECT_RAIL_TAG)
                .padding(vertical = ShellChrome.ObjectRailVerticalInset),
    ) {
        if (objects.isNotEmpty()) {
            LazyColumn(
                modifier =
                    Modifier
                        .weight(1f)
                        .fillMaxWidth()
                        .padding(horizontal = ShellChrome.ObjectRailHorizontalInset)
                        .testTag(SKETCH_NPC_CARD_ROW_TAG),
                verticalArrangement =
                    Arrangement.spacedBy(
                        ShellChrome.ObjectRailVerticalSpacing,
                    ),
            ) {
                itemsIndexed(
                    items = objects,
                    key = { index, target -> "${target.uiKey}#$index" },
                ) { _, target ->
                    ObjectRailEntry(
                        target = target,
                        selected = target.uiKey == selectedInteractionObjectId,
                        onClick = { onInteractionObjectClick(target.uiKey) },
                        modifier =
                            Modifier
                                .fillMaxWidth()
                                .height(ShellChrome.ObjectRailControlHeight),
                    )
                }
            }
        } else {
            Spacer(modifier = Modifier.weight(1f))
        }
    }
}

@Composable
internal fun ObjectRailEntry(
    target: InteractionObject,
    selected: Boolean,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Box(modifier = modifier.testTag("$SKETCH_NPC_CARD_TAG_PREFIX${target.uiKey}")) {
        ObjectRailButton(
            modifier = Modifier.fillMaxSize(),
            label = target.title,
            selected = selected,
            onClick = onClick,
        )
        if (target.questAvailable) {
            QuestMarkerBadge(modifier = Modifier.align(Alignment.TopEnd))
        }
    }
}

@Composable
internal fun ObjectRailButton(
    label: String,
    selected: Boolean,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    semanticDescription: String = "",
    minHeight: Dp = ShellChrome.ObjectRailControlHeight,
    questHighlighted: Boolean = false,
) {
    UnifiedButtonSurface(
        modifier = modifier,
        onClick = onClick,
        fillColor =
            when {
                questHighlighted -> ShellPalette.QuestRouteFill
                selected -> ShellPalette.Accent.copy(alpha = 0.12f).compositeOver(ShellPalette.Ink2)
                else -> ShellPalette.Ink2
            },
        borderColor = if (questHighlighted) ShellLines.QuestBorder else Color.Transparent,
        minHeight = minHeight,
        horizontalPadding = 0.dp,
        verticalPadding = 0.dp,
        shape = if (questHighlighted) ShellChrome.ControlShape else RectangleShape,
        semanticDescription = semanticDescription,
    ) {
        Text(
            text = label,
            color =
                when {
                    questHighlighted -> ShellPalette.Paper
                    selected -> ShellPalette.Accent
                    else -> ShellPalette.Paper
                },
            style =
                ShellType.Caption.copy(
                    fontSize = 10.sp,
                    lineHeight = 12.sp,
                    fontWeight = FontWeight.Medium,
                ),
            modifier = Modifier.fillMaxWidth(),
            textAlign = TextAlign.Center,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            softWrap = false,
        )
    }
}

@Composable
private fun SpecialExitButtonColumn(
    visibleExits: List<ExitSnapshot>,
    trackedQuest: TrackedQuestState,
    exitControlHeight: Dp,
    onExitClick: (String) -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier = modifier,
        verticalArrangement = Arrangement.Top,
    ) {
        visibleExits.forEach { exit ->
            val questHighlighted =
                trackedQuest.active &&
                    exit.direction.toCanonicalTravelDirection() == trackedQuest.nextDirection
            Spacer(modifier = Modifier.height(ShellChrome.ObjectRailVerticalSpacing))
            /* 第六轮真机反馈：特殊出口与 NPC 条目同款感叹号徽标，
             * 数据源为出口级 quest_available（服务端 EXIT 帧直发）。 */
            Box(
                modifier =
                    Modifier
                        .fillMaxWidth()
                        .padding(horizontal = ShellChrome.ObjectRailHorizontalInset)
                        .height(exitControlHeight),
            ) {
                ObjectRailButton(
                    label = exit.name.ifBlank { exit.direction },
                    selected = false,
                    onClick = { onExitClick(exit.direction) },
                    modifier =
                        Modifier
                            .fillMaxSize()
                            .testTag("$LEFT_EXIT_ROW_TAG_PREFIX${exit.direction.lowercase()}"),
                    semanticDescription =
                        buildString {
                            append("出口 ${exit.name.ifBlank { exit.direction }}")
                            if (exit.questAvailable) append("，有可接任务")
                            if (questHighlighted) append("，任务路线")
                        },
                    minHeight = exitControlHeight,
                    questHighlighted = questHighlighted,
                )
                if (exit.questAvailable && exit.direction.allowsQuestExitMarker()) {
                    QuestMarkerBadge(modifier = Modifier.align(Alignment.TopEnd))
                }
            }
        }
    }
}

@Composable
internal fun BottomObjectRailActions(
    specialExits: List<ExitSnapshot>,
    trackedQuest: TrackedQuestState,
    combatActive: Boolean,
    onExitClick: (String) -> Unit,
    onCombatFleeClick: () -> Unit,
    onMapClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val exitMap = remember(specialExits) { specialExits.associateBy { it.direction.lowercase() } }
    val visibleExits = remember(exitMap) { specialExitDirections.mapNotNull(exitMap::get) }
    BoxWithConstraints(modifier = modifier) {
        val density = LocalDensity.current
        val totalExitSpacing = ShellChrome.ObjectRailVerticalSpacing * visibleExits.size
        val rawExitControlHeight =
            if (combatActive || visibleExits.isEmpty()) {
                ShellChrome.ObjectRailControlHeight
            } else {
                with(density) {
                    val availableHeightPx =
                        (
                            maxHeight - ShellChrome.ObjectRailMapBottomInset -
                                ShellChrome.ObjectRailControlHeight - totalExitSpacing
                        ).roundToPx()
                    (availableHeightPx / visibleExits.size).toDp()
                }
            }
        val exitControlHeight =
            rawExitControlHeight
                .coerceAtMost(ShellChrome.ObjectRailControlHeight)
                .coerceAtLeast(SPECIAL_EXIT_MIN_HEIGHT)
        val combatFixedRailHeight =
            ShellChrome.ObjectRailControlHeight * 2 +
                ShellChrome.ObjectRailVerticalSpacing * 2 +
                ShellChrome.ObjectRailMapBottomInset
        val combatSpecialContentHeight =
            if (visibleExits.isEmpty()) {
                0.dp
            } else {
                (exitControlHeight + ShellChrome.ObjectRailVerticalSpacing) * visibleExits.size
            }
        val combatSpecialViewportHeight =
            combatSpecialContentHeight.coerceAtMost(
                (maxHeight - combatFixedRailHeight).coerceAtLeast(0.dp),
            )
        Column(
            modifier = Modifier.fillMaxSize(),
            verticalArrangement =
                if (combatActive) {
                    Arrangement.Bottom
                } else {
                    Arrangement.Top
                },
        ) {
            if (!combatActive) {
                Spacer(modifier = Modifier.weight(1f))
                SketchMapNavigationButton(
                    onClick = onMapClick,
                    modifier =
                        Modifier
                            .fillMaxWidth()
                            .padding(horizontal = ShellChrome.ObjectRailHorizontalInset)
                            .height(ShellChrome.ObjectRailControlHeight)
                            .testTag(SKETCH_MAP_NAV_BUTTON_TAG),
                )
            }
            if (combatActive) {
                // 内容不足时只占实际高度；出口过多时才收敛为明确的可滚动视口，
                // 不让 weighted viewport 把未使用高度夹在出口与地图之间。
                SpecialExitButtonColumn(
                    visibleExits = visibleExits,
                    trackedQuest = trackedQuest,
                    exitControlHeight = exitControlHeight,
                    onExitClick = onExitClick,
                    modifier =
                        Modifier
                            .fillMaxWidth()
                            .height(combatSpecialViewportHeight)
                            .verticalScroll(rememberScrollState()),
                )
            } else {
                SpecialExitButtonColumn(
                    visibleExits = visibleExits,
                    trackedQuest = trackedQuest,
                    exitControlHeight = exitControlHeight,
                    onExitClick = onExitClick,
                )
            }
            if (combatActive && visibleExits.isNotEmpty()) {
                Spacer(modifier = Modifier.height(ShellChrome.ObjectRailVerticalSpacing))
            }
            if (combatActive) {
                SketchMapNavigationButton(
                    onClick = onMapClick,
                    modifier =
                        Modifier
                            .fillMaxWidth()
                            .padding(horizontal = ShellChrome.ObjectRailHorizontalInset)
                            .height(ShellChrome.ObjectRailControlHeight)
                            .testTag(SKETCH_MAP_NAV_BUTTON_TAG),
                )
                Spacer(modifier = Modifier.height(ShellChrome.ObjectRailVerticalSpacing))
                ObjectRailButton(
                    label = "脱离",
                    selected = false,
                    onClick = onCombatFleeClick,
                    modifier =
                        Modifier
                            .fillMaxWidth()
                            .padding(horizontal = ShellChrome.ObjectRailHorizontalInset)
                            .height(ShellChrome.ObjectRailControlHeight)
                            .testTag(SKETCH_COMBAT_FLEE_BUTTON_TAG),
                    semanticDescription = "脱离当前战斗",
                    minHeight = ShellChrome.ObjectRailControlHeight,
                )
            }
            Spacer(modifier = Modifier.height(ShellChrome.ObjectRailMapBottomInset))
        }
    }
}
