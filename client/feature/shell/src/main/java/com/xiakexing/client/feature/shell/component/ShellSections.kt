package com.xiakexing.client.feature.shell.component

import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.AnimatedContentScope
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.togetherWith
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.semantics.SemanticsPropertyKey
import androidx.compose.ui.semantics.SemanticsPropertyReceiver
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.xiakexing.client.core.protocol.payload.ExitSnapshot
import com.xiakexing.client.feature.shell.InteractionObjectKind
import com.xiakexing.client.feature.shell.InteractionSheetMode
import com.xiakexing.client.feature.shell.PopupSource
import com.xiakexing.client.feature.shell.ShellPanelMode
import com.xiakexing.client.feature.shell.ShellSheetKind
import com.xiakexing.client.feature.shell.ShellSheetState
import com.xiakexing.client.feature.shell.isNpcNarrativeConversationSheet
import com.xiakexing.client.feature.shell.isNpcQuestInteractionSheet
import com.xiakexing.client.feature.shell.resolvedSheetKind
import com.xiakexing.client.feature.shell.ui.LocalShellReduceMotion
import com.xiakexing.client.feature.shell.ui.ShellLines
import com.xiakexing.client.feature.shell.ui.ShellMotion

internal val gridRows =
    listOf(
        listOf("northwest", "north", "northeast"),
        listOf("west", "center", "east"),
        listOf("southwest", "south", "southeast"),
    )

internal val gridDirectionSlots =
    mapOf(
        "northwest" to (0 to 0),
        "north" to (0 to 1),
        "northeast" to (0 to 2),
        "west" to (1 to 0),
        "east" to (1 to 2),
        "southwest" to (2 to 0),
        "south" to (2 to 1),
        "southeast" to (2 to 2),
    )

internal val specialExitDirections = listOf("up", "down", "enter", "out")
internal val ShellSectionLineColor = ShellLines.StructuralDivider

internal const val EXIT_GRID_CONNECTIONS_TAG = "exit-grid-connections"
internal const val EXIT_GRID_CONTENT_TAG = "exit-grid-content"
internal const val EXIT_MOVE_FEEDBACK_TAG = "exit-move-feedback"
internal val ExitGridRevealProgressKey = SemanticsPropertyKey<Float>("ExitGridRevealProgress")
internal var SemanticsPropertyReceiver.exitGridRevealProgress by ExitGridRevealProgressKey
internal val ExitGridDisplayedRefreshSerialKey =
    SemanticsPropertyKey<Long>("ExitGridDisplayedRefreshSerial")
internal var SemanticsPropertyReceiver.exitGridDisplayedRefreshSerial by ExitGridDisplayedRefreshSerialKey
internal val ExitMoveFeedbackProgressKey = SemanticsPropertyKey<Float>("ExitMoveFeedbackProgress")
internal var SemanticsPropertyReceiver.exitMoveFeedbackProgress by ExitMoveFeedbackProgressKey
internal const val EXIT_OVERLAY_DISMISS_AREA_TAG = "exit-overlay-dismiss-area"
internal const val EXIT_OVERLAY_PANEL_AREA_TAG = "exit-overlay-panel-area"
internal const val EXIT_OVERLAY_BUTTON_TAG_PREFIX = "exit-overlay-button-"
internal const val EXIT_OVERLAY_HEADER_TAG = "exit-overlay-header"
internal const val EXIT_OVERLAY_CLOSE_BUTTON_TAG = "exit-overlay-close-button"
internal const val COMBAT_EXIT_OVERLAY_CELL_TAG_PREFIX = "combat-exit-overlay-cell-"
internal const val DIRECTION_REFRESH_FADE_OUT_MS = 170
internal const val DIRECTION_REFRESH_FADE_DELAY_MS = 50
internal const val DIRECTION_REFRESH_FADE_IN_MS = 190
internal const val EXIT_MOVE_FEEDBACK_MS = 150
internal const val EXIT_MOVE_FEEDBACK_START_PROGRESS = 0.08f
internal const val EXIT_GRID_REFRESH_REVEAL_MS = 170
internal const val LEFT_TO_RIGHT_APPEAR_MS = 140

internal fun visibleGridConnectionDirections(exitDirections: Set<String>): List<String> {
    val normalizedDirections = exitDirections.map { it.lowercase() }.toSet()
    return gridDirectionSlots.keys.filter { direction -> direction in normalizedDirections }
}

internal data class ExitGridRefreshContent(
    val roomTitle: String,
    val exits: List<ExitSnapshot>,
    val refreshSerial: Long = 0L,
)

internal fun exitRefreshKey(
    roomTitle: String,
    specialExits: List<ExitSnapshot>,
    gridExits: List<ExitSnapshot> = emptyList(),
): String {
    fun List<ExitSnapshot>.signature(): String {
        return joinToString(separator = "|") { exit ->
            "${exit.direction.lowercase()}:${exit.name}"
        }
    }
    return "$roomTitle#${specialExits.signature()}#${gridExits.signature()}"
}

@Composable
internal fun Modifier.leftToRightAppear(
    key: String,
    distance: Dp = 6.dp,
): Modifier {
    var visible by remember(key) { mutableStateOf(false) }
    LaunchedEffect(key) {
        visible = true
    }
    val reduceMotion = LocalShellReduceMotion.current
    val initialOffsetPx = with(LocalDensity.current) { distance.toPx() }
    val alpha by animateFloatAsState(
        targetValue = if (visible) 1f else 0f,
        animationSpec =
            tween(
                durationMillis =
                    if (reduceMotion) ShellMotion.ReduceMotionMs else LEFT_TO_RIGHT_APPEAR_MS,
                easing = FastOutSlowInEasing,
            ),
        label = "left_to_right_appear_alpha",
    )
    val offsetX by animateFloatAsState(
        targetValue = if (reduceMotion || visible) 0f else -initialOffsetPx,
        animationSpec =
            tween(
                durationMillis =
                    if (reduceMotion) ShellMotion.ReduceMotionMs else LEFT_TO_RIGHT_APPEAR_MS,
                easing = FastOutSlowInEasing,
            ),
        label = "left_to_right_appear_offset",
    )
    return graphicsLayer {
        this.alpha = alpha
        translationX = offsetX
    }
}

@Composable
internal fun <T> DirectionRefreshAnimatedContent(
    targetState: T,
    modifier: Modifier = Modifier,
    label: String,
    contentAlignment: Alignment = Alignment.TopStart,
    content: @Composable AnimatedContentScope.(T) -> Unit,
) {
    AnimatedContent(
        targetState = targetState,
        modifier = modifier,
        transitionSpec = {
            fadeIn(
                animationSpec =
                    tween(
                        durationMillis = DIRECTION_REFRESH_FADE_IN_MS,
                        delayMillis = DIRECTION_REFRESH_FADE_OUT_MS + DIRECTION_REFRESH_FADE_DELAY_MS,
                        easing = FastOutSlowInEasing,
                    ),
            ) togetherWith
                fadeOut(
                    animationSpec =
                        tween(
                            durationMillis = DIRECTION_REFRESH_FADE_OUT_MS,
                            easing = FastOutSlowInEasing,
                        ),
                )
        },
        contentAlignment = contentAlignment,
        label = label,
        content = content,
    )
}

internal fun ShellSheetState.isCombatExitSheet(): Boolean {
    return source == PopupSource.DOCK_BUTTON &&
        resolvedSheetKind() == ShellSheetKind.COMBAT &&
        panelMode == ShellPanelMode.COMBAT_GRID
}

internal fun ShellSheetState.isAnchoredInteractionMenu(): Boolean {
    return source == PopupSource.INTERACTION_OBJECT &&
        panelMode == ShellPanelMode.NONE &&
        interactionMode == InteractionSheetMode.DETAIL &&
        activeActionGroupId.ifBlank { "root" } == "root" &&
        detailLines.isEmpty() &&
        interactionLines.isEmpty() &&
        !loading
}

internal fun ShellSheetState.isNpcInlineInteractionSheet(): Boolean {
    return source == PopupSource.INTERACTION_OBJECT &&
        interactionTargetKind == InteractionObjectKind.NPC &&
        !isNpcQuestInteractionSheet()
}

internal fun ShellSheetState.mainTextOverlayHeightFraction(): Float {
    if (isNpcNarrativeConversationSheet()) {
        return 0.28f
    }
    return when (source) {
        PopupSource.INTERACTION_OBJECT ->
            when {
                panelMode.isScrollableInteractionWorkspace() -> 0.72f
                else -> compactInteractionObjectHeightFraction()
            }
        PopupSource.INTERACTION_GIVE,
        PopupSource.INTERACTION_LEARN,
        -> 0.72f
        PopupSource.DOCK_BUTTON,
        PopupSource.TITLE_ACTION,
        PopupSource.PROTOCOL,
        ->
            when {
                panelMode.isScrollableInteractionWorkspace() -> 0.78f
                items.size + panelCards.size > 4 -> 0.78f
                else -> 0.64f
            }
    }
}

private fun ShellSheetState.compactInteractionObjectHeightFraction(): Float {
    val contentLineCount = detailLines.size + interactionLines.size
    return when {
        contentLineCount <= 2 -> 0.30f
        contentLineCount <= 5 -> 0.36f
        else -> 0.44f
    }
}

private fun ShellPanelMode.isScrollableInteractionWorkspace(): Boolean {
    return this == ShellPanelMode.SHOP_GRID ||
        this == ShellPanelMode.SKILL_GRID ||
        this == ShellPanelMode.INVENTORY_GRID ||
        this == ShellPanelMode.COMBAT_GRID ||
        this == ShellPanelMode.MAP_ATLAS
}

internal fun ShellSheetState.blocksMainTextOverlayDismissArea(): Boolean {
    return !isNpcNarrativeConversationSheet()
}
