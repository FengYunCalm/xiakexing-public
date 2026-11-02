package com.xiakexing.client.feature.shell.component

import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.runtime.Composable
import androidx.compose.runtime.ExperimentalComposeApi
import androidx.compose.runtime.NonSkippableComposable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.xiakexing.client.core.protocol.payload.ExitSnapshot
import com.xiakexing.client.feature.shell.CombatActionCooldown
import com.xiakexing.client.feature.shell.ExitOverlayState
import com.xiakexing.client.feature.shell.ShellAction
import com.xiakexing.client.feature.shell.ShellSheetItem
import com.xiakexing.client.feature.shell.ShellSheetState
import com.xiakexing.client.feature.shell.TrackedQuestState
import com.xiakexing.client.feature.shell.remainingAt
import com.xiakexing.client.feature.shell.ui.ShellChrome

@OptIn(ExperimentalComposeApi::class)
@NonSkippableComposable
@Composable
internal fun SketchMapArea(
    combatExitSheet: ShellSheetState?,
    exitOverlay: ExitOverlayState?,
    specialExits: List<ExitSnapshot>,
    gridExits: List<ExitSnapshot>,
    roomTitle: String,
    roomRefreshSerial: Long,
    trackedQuest: com.xiakexing.client.feature.shell.TrackedQuestState,
    currentRoomQuestAvailable: Boolean,
    combatActive: Boolean,
    combatActionCooldowns: Map<String, CombatActionCooldown>,
    railWidth: Dp = ShellChrome.RailWidth,
    onInteractionSheetDismiss: () -> Unit,
    onInteractionSheetBack: () -> Unit,
    onInteractionSheetActionClick: (ShellAction) -> Unit,
    onInteractionSheetItemClick: (ShellSheetItem) -> Unit,
    onExitOverlayButtonClick: (ShellAction) -> Unit,
    onExitOverlayDismiss: () -> Unit,
    onExitClick: (String) -> Unit,
    onCombatFleeClick: () -> Unit,
    onMapClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    // 方案 R-31：战斗 2×5 槽冷却罩数据（按命令取剩余秒）
    val cooldownSecondsByCommand =
        remember(combatActionCooldowns) {
            combatActionCooldowns.mapValues { (_, cooldown) ->
                cooldown.remainingAt(System.currentTimeMillis())
            }
        }
    Box(
        modifier =
            modifier
                .testTag(SKETCH_MAP_AREA_TAG),
    ) {
        Row(modifier = Modifier.fillMaxSize()) {
            LeftRailWithDivider(
                modifier =
                    Modifier
                        .fillMaxHeight()
                        .testTag(SKETCH_SPECIAL_EXIT_RAIL_TAG),
                railWidth = railWidth,
                dividerTag = SKETCH_SPECIAL_EXIT_RAIL_DIVIDER_TAG,
            ) {
                BottomObjectRailActions(
                    specialExits = specialExits,
                    trackedQuest = trackedQuest,
                    combatActive = combatActive,
                    onExitClick = onExitClick,
                    onCombatFleeClick = onCombatFleeClick,
                    onMapClick = onMapClick,
                    modifier = Modifier.fillMaxSize(),
                )
            }
            Box(
                modifier =
                    Modifier
                        .fillMaxHeight()
                        .weight(1f),
                contentAlignment = Alignment.Center,
            ) {
                if (exitOverlay != null) {
                    Box(
                        modifier =
                            Modifier
                                .fillMaxSize()
                                .testTag(EXIT_OVERLAY_PANEL_AREA_TAG)
                                .clickable(
                                    interactionSource = remember { MutableInteractionSource() },
                                    indication = null,
                                    onClick = {},
                                ),
                    ) {
                        ExitOverlayPage(
                            overlay = exitOverlay,
                            onButtonClick = onExitOverlayButtonClick,
                            onDismiss = onExitOverlayDismiss,
                            cooldownSecondsByCommand = cooldownSecondsByCommand,
                            modifier = Modifier.fillMaxSize(),
                        )
                    }
                } else {
                    GridExitPanel(
                        roomTitle = roomTitle,
                        exits = gridExits,
                        roomRefreshSerial = roomRefreshSerial,
                        trackedQuest = trackedQuest,
                        currentRoomQuestAvailable = currentRoomQuestAvailable,
                        onExitClick = onExitClick,
                        outerPadding = ShellChrome.SketchMapGridPadding,
                        gridSpacing = ShellChrome.SketchMapGridSpacing,
                        cellInset = 0.dp,
                        modifier =
                            Modifier
                                .fillMaxSize()
                                .padding(
                                    start = ShellChrome.SketchMapGridHorizontalClearance,
                                    end = ShellChrome.SketchMapGridHorizontalClearance,
                                    bottom = ShellChrome.SketchMapGridBottomClearance,
                                ),
                    )
                }
            }
        }
        SketchCombatInteractionSheetOverlay(
            sheet = combatExitSheet,
            onDismiss = onInteractionSheetDismiss,
            onBack = onInteractionSheetBack,
            onActionClick = onInteractionSheetActionClick,
            onItemClick = onInteractionSheetItemClick,
            modifier = Modifier.matchParentSize(),
        )
    }
}

@Composable
internal fun SketchMapNavigationButton(
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    minHeight: Dp = ShellChrome.ObjectRailControlHeight,
) {
    ObjectRailButton(
        modifier = modifier,
        label = "地图",
        selected = false,
        onClick = onClick,
        semanticDescription = "打开大地图，当前位置",
        minHeight = minHeight,
    )
}

@Composable
internal fun SketchMainExitDivider(
    railWidth: Dp,
    modifier: Modifier = Modifier,
) {
    Row(
        modifier =
            modifier
                .fillMaxWidth()
                .height(ShellChrome.DividerThickness),
    ) {
        Spacer(modifier = Modifier.width(railWidth))
        SectionRuleVertical(modifier = Modifier.fillMaxHeight())
        SectionRule(
            modifier =
                Modifier
                    .weight(1f)
                    .testTag(SKETCH_MAIN_EXIT_DIVIDER_TAG),
        )
    }
}
