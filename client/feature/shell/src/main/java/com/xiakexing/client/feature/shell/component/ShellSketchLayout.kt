package com.xiakexing.client.feature.shell.component

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.scaleIn
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.waitForUpOrCancellation
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.items
import androidx.compose.runtime.Composable
import androidx.compose.runtime.ExperimentalComposeApi
import androidx.compose.runtime.NonSkippableComposable
import androidx.compose.runtime.State
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.pointer.PointerEventPass
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.dp
import com.xiakexing.client.core.protocol.payload.PlayerStatusSnapshot
import com.xiakexing.client.feature.shell.COMBAT_FLEE_COMMAND
import com.xiakexing.client.feature.shell.ExitOverlayType
import com.xiakexing.client.feature.shell.MainShellSceneState
import com.xiakexing.client.feature.shell.OPEN_WORLD_MAP_COMMAND
import com.xiakexing.client.feature.shell.PopupSource
import com.xiakexing.client.feature.shell.ShellAction
import com.xiakexing.client.feature.shell.ShellIntent
import com.xiakexing.client.feature.shell.ShellSheetState
import com.xiakexing.client.feature.shell.isNpcNarrativeConversationSheet
import com.xiakexing.client.feature.shell.popup.isNpcRootMode
import com.xiakexing.client.feature.shell.ui.LocalShellReduceMotion
import com.xiakexing.client.feature.shell.ui.ShellChrome

internal const val SKETCH_CHAT_SECTION_TAG = "sketch-chat-section"
internal const val SKETCH_SCENE_BAR_TAG = "sketch-scene-bar"
internal const val SKETCH_SCENE_ROOM_TITLE_TAG = "sketch-scene-room-title"
internal const val SKETCH_SCENE_GAME_HOUR_TAG = "sketch-scene-game-hour"
internal const val SKETCH_OBJECT_RAIL_TAG = "sketch-object-rail"
internal const val SKETCH_OBJECT_RAIL_DIVIDER_TAG = "sketch-object-rail-divider"
internal const val SKETCH_MAIN_EXIT_DIVIDER_TAG = "sketch-main-exit-divider"
internal const val SKETCH_SPECIAL_EXIT_RAIL_TAG = "sketch-special-exit-rail"
internal const val SKETCH_SPECIAL_EXIT_RAIL_DIVIDER_TAG = "sketch-special-exit-rail-divider"
internal const val SKETCH_NARRATIVE_AREA_TAG = "sketch-narrative-area"
internal const val SKETCH_MAIN_TEXT_AREA_TAG = "sketch-main-text-area"
internal const val SKETCH_ACTION_MENU_TAG = "sketch-action-menu"
internal const val SKETCH_NPC_CARD_ROW_TAG = "sketch-npc-card-row"
internal const val SKETCH_NPC_CARD_TAG_PREFIX = "sketch-npc-card-"
internal const val SKETCH_MAP_AREA_TAG = "sketch-map-area"
internal const val SKETCH_MAP_NAV_BUTTON_TAG = "sketch-map-nav-button"
internal const val SKETCH_COMBAT_FLEE_BUTTON_TAG = "sketch-combat-flee-button"
internal const val SKETCH_QUEST_LANE_TAG = "sketch-quest-lane"
internal const val SKETCH_QUEST_NAME_TAG = "sketch-quest-name"
internal const val SKETCH_QUEST_PROGRESS_TAG = "sketch-quest-progress"
internal const val SKETCH_QUEST_ACTION_TAG = "sketch-quest-action"
internal const val SKETCH_QUEST_DIVIDER_TAG = "sketch-quest-divider"
internal const val SKETCH_DOCK_BUTTON_TAG_PREFIX = "sketch-dock-button-"
internal const val SKETCH_DOCK_ATTRIBUTE_DIVIDER_TAG = "sketch-dock-attribute-divider"
internal const val SKETCH_DOCK_LEFT_BORDER_TAG = "sketch-dock-left-border"
internal const val SKETCH_DOCK_RIGHT_BORDER_TAG = "sketch-dock-right-border"
internal const val SKETCH_FUNCTION_SHEET_AREA_TAG = "sketch-function-sheet-area"
internal const val SKETCH_NARRATIVE_OUTSIDE_DISMISS_TAG = "sketch-narrative-outside-dismiss"
internal const val SKETCH_COMMON_EXIT_ROLLOUT_TAG = "sketch-common-exit-rollout"
internal const val SKETCH_COMMON_EXIT_ROLLOUT_ACTION_TAG_PREFIX = "sketch-common-exit-rollout-action-"

/** 主界面浮层层序契约：同一 Box 内的绘制顺序由数值决定，插新层先查此表。 */
internal object SketchZIndex {
    /** dock 边框线等装饰性覆盖。 */
    const val DOCK_BORDER = 1f

    /** 底部动作条（场景物品/NPC 锚定菜单）。 */
    const val ACTION_MENU = 12f

    /** 人物/物品交互弹窗（贴主文本区顶）。 */
    const val CONTEXT_PANEL = 13f

    /** 二级交互弹窗（任务/背包/技能等）。 */
    const val INTERACTION_SHEET = 14f

    /** 聊天输入条。 */
    const val CHAT_BAR = 30f

    /** 飘字消息。 */
    const val FLOATING_MESSAGE = 40f
}

// 移动刷新修复（311414902）：strongSkipping 开启后本函数被跳过导致移动后画面不刷新，强制不跳过；勿删，删则冻结 bug 静默复发。

@OptIn(ExperimentalComposeApi::class)
@NonSkippableComposable
@Composable
internal fun SketchShellLayout(
    state: MainShellSceneState,
    statusSnapshotState: State<PlayerStatusSnapshot?>,
    chatInputState: State<String>,
    onIntent: (ShellIntent) -> Unit,
    modifier: Modifier = Modifier,
) {
    val combatExitSheet = state.interactionSheet?.takeIf { it.isCombatExitSheet() }
    val commonExitOverlay = state.exitOverlay?.takeIf { it.type == ExitOverlayType.COMMON }
    val combatExitOverlay = state.exitOverlay?.takeIf { it.type == ExitOverlayType.COMBAT }
    val narrativeSheet = if (combatExitSheet == null) state.interactionSheet else null
    // 聊天是独占信息流的临时表面；外部状态短暂同时带有 sheet 时也不让两层抢同一触点。
    val contextualSheet = narrativeSheet?.takeUnless { state.chatPanelOpen }
    val surfaces = remember(contextualSheet) { contextualSheet.toSketchInteractionSurfaces() }
    val interactionFocused = contextualSheet != null
    val hasDismissibleNarrativeSurface = state.chatPanelOpen || contextualSheet != null
    val outsideDismissAction =
        rememberUpdatedState<() -> Unit> {
            onIntent(
                if (state.chatPanelOpen) {
                    ShellIntent.ChatPanelDismissed
                } else {
                    ShellIntent.SheetBackClicked
                },
            )
        }
    BoxWithConstraints(
        modifier =
            modifier
                .fillMaxSize()
                .dismissOnUnconsumedTap(
                    enabled = hasDismissibleNarrativeSurface,
                    onDismiss = outsideDismissAction,
                ),
    ) {
        // 方案 §6.4/§21.1：窄屏（≤360dp）与矮屏（<640dp）适配
        val isShort = maxHeight < ShellChrome.SketchShortHeightBreakpoint
        val railWidth =
            ShellChrome.railWidthAlignedToPrimaryDock(
                contentWidth = maxWidth,
                density = LocalDensity.current,
            )
        val isCompactLandscape =
            maxWidth > maxHeight && maxHeight < ShellChrome.SketchCompactLandscapeHeightBreakpoint
        // vitals 固定在正文底部，地图比例仍按其固定高度预留。
        val fixedChromeHeight =
            ShellChrome.VitalsStripHeight +
                ShellChrome.SketchPrimaryBarHeight +
                ShellChrome.DividerThickness +
                ShellChrome.DividerThickness
        val flexibleHeight = (maxHeight - fixedChromeHeight).coerceAtLeast(0.dp)
        var mapAreaHeight =
            minOf(
                ShellChrome.SketchMapAreaMaxHeight,
                flexibleHeight * ShellChrome.SketchMapHeightFraction,
            )
        if (isCompactLandscape) {
            mapAreaHeight = ShellChrome.SketchMapAreaMaxHeight
        } else if (
            isShort &&
            (maxHeight - fixedChromeHeight - mapAreaHeight) < ShellChrome.SketchNarrativeHeightThreshold
        ) {
            // 矮屏：叙事区低于 180dp 时地图区让渡至收紧后的 112dp 下限
            mapAreaHeight = ShellChrome.SketchShortMapAreaHeight
        }
        Column(modifier = Modifier.fillMaxSize()) {
            Box(
                modifier =
                    Modifier
                        .fillMaxWidth()
                        .weight(1f)
                        .testTag(SKETCH_FUNCTION_SHEET_AREA_TAG),
            ) {
                Column(modifier = Modifier.fillMaxSize()) {
                    SketchTopComposite(
                        roomTitle = state.roomTitle,
                        roomIdentity = state.roomId.ifBlank { "${state.roomTitle}#${state.roomRefreshSerial}" },
                        interactionObjects = state.interactionObjects,
                        selectedInteractionObjectId = state.selectedInteractionObjectId,
                        visibleTranscript = state.visibleTranscript,
                        transcriptScrollRequest = state.transcriptScrollRequest,
                        inlineActionChips = state.inlineActionChips,
                        floatingMessages = state.floatingMessages,
                        interactionSheet = contextualSheet,
                        surfaces = surfaces,
                        chatPanelOpen = state.chatPanelOpen,
                        chatMessages = state.chatMessages,
                        chatInputState = chatInputState,
                        exitOverlay = state.exitOverlay,
                        questGuideLaneState = state.questGuideLaneState,
                        isShort = isShort,
                        isCompactLandscape = isCompactLandscape,
                        statusSnapshotState = statusSnapshotState,
                        combatOpponents = state.combatOpponents,
                        combatActive = state.combatActive,
                        combatSelfBusySeconds = state.combatSelfBusySeconds,
                        combatSelfBusyCapturedAtMillis = state.combatSelfBusyCapturedAtMillis,
                        railWidth = railWidth,
                        onIntent = onIntent,
                        modifier =
                            Modifier
                                .fillMaxWidth()
                                .weight(1f),
                    )
                }
            }
            SketchMainExitDivider(
                railWidth = railWidth,
            )
            SketchMapArea(
                combatExitSheet = combatExitSheet,
                exitOverlay = combatExitOverlay,
                specialExits = state.specialExits,
                gridExits = state.gridExits,
                roomTitle = state.roomTitle,
                roomRefreshSerial = state.roomRefreshSerial,
                trackedQuest = state.trackedQuest,
                currentRoomQuestAvailable =
                    state.interactionObjects.any { it.questAvailable } ||
                        state.specialExits.any { it.questAvailable },
                combatActive = state.combatActive,
                combatActionCooldowns = state.combatActionCooldowns,
                railWidth = railWidth,
                onInteractionSheetDismiss = { onIntent(ShellIntent.SheetDismissed) },
                onInteractionSheetBack = { onIntent(ShellIntent.SheetBackClicked) },
                onInteractionSheetActionClick = {
                        action ->
                    onIntent(ShellIntent.SheetPrimaryActionClicked(action))
                },
                onInteractionSheetItemClick = {
                        item ->
                    onIntent(ShellIntent.SheetItemClicked(item))
                },
                onExitOverlayButtonClick = {
                        action ->
                    onIntent(ShellIntent.ExitOverlayButtonClicked(action))
                },
                onExitOverlayDismiss = { onIntent(ShellIntent.ExitOverlayDismissed) },
                onExitClick = { direction -> onIntent(ShellIntent.ExitClicked(direction)) },
                onCombatFleeClick = {
                    onIntent(
                        ShellIntent.ExitOverlayButtonClicked(
                            ShellAction(
                                label = "脱离",
                                command = COMBAT_FLEE_COMMAND,
                                dismissAfterInvoke = false,
                            ),
                        ),
                    )
                },
                onMapClick = {
                    onIntent(
                        ShellIntent.SheetPrimaryActionClicked(
                            ShellAction("地图", OPEN_WORLD_MAP_COMMAND, dismissAfterInvoke = false),
                        ),
                    )
                },
                modifier =
                    Modifier
                        .fillMaxWidth()
                        .height(mapAreaHeight),
            )
            SketchPrimaryDockBar(
                selectedLabel = state.activeSketchDockLabel(),
                onIntent = onIntent,
                modifier =
                    Modifier
                        .fillMaxWidth()
                        .height(ShellChrome.SketchPrimaryBarHeight),
            )
        }
        if (!interactionFocused && commonExitOverlay != null) {
            // 方案 M04/§17.2：rollout 右下展开 150ms；降级模式仅淡入
            AnimatedVisibility(
                visible = true,
                enter =
                    if (LocalShellReduceMotion.current) {
                        fadeIn(animationSpec = tween(durationMillis = 100))
                    } else {
                        scaleIn(
                            initialScale = 0.92f,
                            animationSpec = tween(durationMillis = 150, easing = FastOutSlowInEasing),
                        ) + fadeIn(animationSpec = tween(durationMillis = 150))
                    },
                modifier =
                    Modifier
                        .align(Alignment.BottomEnd)
                        .padding(
                            bottom = ShellChrome.SketchPrimaryBarHeight - SKETCH_COMMON_ROLLOUT_JOIN_OVERLAP,
                        )
                        .width(sketchDockButtonWidth(maxWidth)),
            ) {
                SketchCommonExitRollout(
                    overlay = commonExitOverlay,
                    onButtonClick = {
                            action ->
                        onIntent(ShellIntent.ExitOverlayButtonClicked(action))
                    },
                    modifier = Modifier.fillMaxWidth(),
                )
            }
        }
    }
}

private fun Modifier.dismissOnUnconsumedTap(
    enabled: Boolean,
    onDismiss: State<() -> Unit>,
): Modifier {
    if (!enabled) return this
    return testTag(SKETCH_NARRATIVE_OUTSIDE_DISMISS_TAG)
        .pointerInput(enabled) {
            awaitEachGesture {
                val down =
                    awaitFirstDown(
                        requireUnconsumed = false,
                        pass = PointerEventPass.Final,
                    )
                if (!down.isConsumed && waitForUpOrCancellation(PointerEventPass.Final) != null) {
                    onDismiss.value()
                }
            }
        }
}

private fun ShellSheetState.isItemContextPanelSheet(): Boolean {
    return source == PopupSource.INTERACTION_OBJECT &&
        (
            interactionTargetKind == com.xiakexing.client.feature.shell.InteractionObjectKind.ITEM ||
                interactionTargetKind == com.xiakexing.client.feature.shell.InteractionObjectKind.SCENE
        ) &&
        items.isEmpty() &&
        panelCards.isEmpty()
}

private fun ShellSheetState?.toSketchInteractionSurfaces(): SketchInteractionSurfaces {
    if (this == null) {
        return SketchInteractionSurfaces(null, null, null)
    }
    val personContextSheet =
        takeIf {
            it.isNpcInlineInteractionSheet() && (it.isNpcRootMode() || it.isNpcNarrativeConversationSheet())
        }
    val npcInlineInteractionSheet =
        takeIf {
            it.isNpcInlineInteractionSheet() && !it.isNpcRootMode() && !it.isNpcNarrativeConversationSheet()
        }
    val nonNpcInteractionSheet = takeUnless { it.isNpcInlineInteractionSheet() }
    val itemContextSheet = nonNpcInteractionSheet?.takeIf { it.isItemContextPanelSheet() }
    val objectAnchoredMenu =
        nonNpcInteractionSheet?.takeIf {
            itemContextSheet == null && it.isAnchoredInteractionMenu()
        }
    val inlineInteractionSheet =
        npcInlineInteractionSheet ?: nonNpcInteractionSheet?.takeIf {
            itemContextSheet == null && !it.isAnchoredInteractionMenu()
        }
    return SketchInteractionSurfaces(
        contextPanelSheet = personContextSheet ?: itemContextSheet,
        objectAnchoredMenu = objectAnchoredMenu,
        inlineInteractionSheet = inlineInteractionSheet,
    )
}

internal const val SKETCH_COMMON_ROLLOUT_VISIBLE_ROWS = 8

internal val SKETCH_COMMON_ROLLOUT_ROWS_VIEWPORT_HEIGHT =
    ShellChrome.SketchCompactButtonHeight * SKETCH_COMMON_ROLLOUT_VISIBLE_ROWS +
        ShellChrome.DividerThickness * (SKETCH_COMMON_ROLLOUT_VISIBLE_ROWS - 1)
internal val SKETCH_COMMON_ROLLOUT_JOIN_OVERLAP = 1.dp

internal data class SketchInteractionSurfaces(
    val contextPanelSheet: ShellSheetState?,
    val objectAnchoredMenu: ShellSheetState?,
    val inlineInteractionSheet: ShellSheetState?,
)

internal data class SketchRailAction(
    val label: String,
    val onClick: () -> Unit,
)

internal data class SketchDockAction(
    val label: String,
    val onClick: () -> Unit,
)
