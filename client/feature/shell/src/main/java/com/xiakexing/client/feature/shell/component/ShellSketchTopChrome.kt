package com.xiakexing.client.feature.shell.component

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.ExperimentalComposeApi
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.NonSkippableComposable
import androidx.compose.runtime.State
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.RectangleShape
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.zIndex
import com.xiakexing.client.core.protocol.payload.InlineAction
import com.xiakexing.client.core.protocol.payload.PlayerStatusSnapshot
import com.xiakexing.client.feature.shell.ChatMessageItem
import com.xiakexing.client.feature.shell.CombatOpponentState
import com.xiakexing.client.feature.shell.DockSlotKind
import com.xiakexing.client.feature.shell.DockWorkspaceSection
import com.xiakexing.client.feature.shell.ExitOverlayState
import com.xiakexing.client.feature.shell.ExitOverlayType
import com.xiakexing.client.feature.shell.InteractionObject
import com.xiakexing.client.feature.shell.MainShellSceneState
import com.xiakexing.client.feature.shell.QuestGuideLaneState
import com.xiakexing.client.feature.shell.SheetNavigationMeta
import com.xiakexing.client.feature.shell.ShellAction
import com.xiakexing.client.feature.shell.ShellIntent
import com.xiakexing.client.feature.shell.ShellSheetKind
import com.xiakexing.client.feature.shell.ShellSheetState
import com.xiakexing.client.feature.shell.TranscriptLine
import com.xiakexing.client.feature.shell.advanceShichen
import com.xiakexing.client.feature.shell.popup.InteractionObjectContextPanelOverlay
import com.xiakexing.client.feature.shell.shellDockSpecs
import com.xiakexing.client.feature.shell.shichenIndexOf
import com.xiakexing.client.feature.shell.sortedForVitals
import com.xiakexing.client.feature.shell.testTagPrefix
import com.xiakexing.client.feature.shell.ui.ShellChrome
import com.xiakexing.client.feature.shell.ui.ShellLines
import com.xiakexing.client.feature.shell.ui.ShellPalette
import com.xiakexing.client.feature.shell.ui.ShellType

@OptIn(ExperimentalComposeApi::class)
@NonSkippableComposable
@Composable
internal fun SketchTopComposite(
    roomTitle: String,
    roomIdentity: String,
    interactionObjects: List<InteractionObject>,
    selectedInteractionObjectId: String,
    visibleTranscript: List<TranscriptLine>,
    transcriptScrollRequest: Int,
    inlineActionChips: List<InlineAction>,
    floatingMessages: List<com.xiakexing.client.feature.shell.FloatingMessageState>,
    interactionSheet: ShellSheetState?,
    surfaces: SketchInteractionSurfaces,
    chatPanelOpen: Boolean,
    chatMessages: List<ChatMessageItem>,
    chatInputState: State<String>,
    exitOverlay: ExitOverlayState?,
    questGuideLaneState: QuestGuideLaneState,
    railWidth: Dp,
    isShort: Boolean,
    isCompactLandscape: Boolean,
    statusSnapshotState: State<PlayerStatusSnapshot?>,
    combatOpponents: List<CombatOpponentState>,
    combatActive: Boolean,
    combatSelfBusySeconds: Int,
    combatSelfBusyCapturedAtMillis: Long,
    onIntent: (ShellIntent) -> Unit,
    modifier: Modifier = Modifier,
) {
    val sortedCombatOpponents = remember(combatOpponents) { combatOpponents.sortedForVitals() }
    Box(modifier = modifier.fillMaxWidth()) {
        Column(modifier = Modifier.matchParentSize()) {
            if (!isCompactLandscape) {
                ChatTickerSection(
                    messages = chatMessages,
                    onClick = { onIntent(ShellIntent.ChatBubbleClicked) },
                    compact = isShort,
                    modifier =
                        Modifier
                            .fillMaxWidth()
                            .height(ShellChrome.SketchChatHeight),
                )
                SectionRule()
            }
            SceneFunctionBar(
                roomTitle = roomTitle,
                gameHour = statusSnapshotState.value?.shichen.orEmpty(),
                modifier =
                    Modifier
                        .fillMaxWidth()
                        .height(ShellChrome.SketchSceneBarHeight),
            )
            SectionRule()
            Row(modifier = Modifier.fillMaxWidth().weight(1f)) {
                LeftRailWithDivider(
                    modifier = Modifier.fillMaxHeight(),
                    railWidth = railWidth,
                    dividerTag = SKETCH_OBJECT_RAIL_DIVIDER_TAG,
                ) {
                    SketchObjectInteractionRail(
                        roomIdentity = roomIdentity,
                        interactionObjects = interactionObjects,
                        selectedInteractionObjectId = selectedInteractionObjectId,
                        onInteractionObjectClick = { objectId ->
                            onIntent(ShellIntent.InteractionObjectClicked(objectId))
                        },
                        modifier = Modifier.fillMaxSize(),
                    )
                }
                // #3 主文本区改为 Box 容器：人物/物品交互弹窗贴主文本区顶，
                // 不再随任务指引条下沉。
                Box(
                    modifier =
                        Modifier
                            .weight(1f)
                            .fillMaxHeight()
                            .testTag(SKETCH_MAIN_TEXT_AREA_TAG),
                ) {
                    Column(modifier = Modifier.matchParentSize()) {
                        Box(
                            modifier =
                                Modifier
                                    .fillMaxWidth()
                                    .weight(1f)
                                    .testTag(SKETCH_NARRATIVE_AREA_TAG),
                        ) {
                            Column(modifier = Modifier.matchParentSize()) {
                                if (questGuideLaneState.active) {
                                    SketchQuestGuideLane(
                                        state = questGuideLaneState,
                                        onClick =
                                            questGuideLaneState.takeIf { it.active }?.let { quest ->
                                                {
                                                    onIntent(
                                                        ShellIntent.SheetPrimaryActionClicked(
                                                            ShellAction(
                                                                label = quest.questName.ifBlank { "目标详情" },
                                                                command = quest.openCommand,
                                                                dismissAfterInvoke = false,
                                                            ),
                                                        ),
                                                    )
                                                }
                                            },
                                        modifier =
                                            Modifier
                                                .fillMaxWidth()
                                                .height(ShellChrome.SketchQuestLaneHeight),
                                    )
                                    SectionRule(
                                        modifier = Modifier.testTag(SKETCH_QUEST_DIVIDER_TAG),
                                    )
                                }
                                if (combatActive && sortedCombatOpponents.isNotEmpty()) {
                                    Column(
                                        modifier =
                                            Modifier
                                                .fillMaxWidth()
                                                .testTag(COMBAT_OPPONENT_VITALS_SECTION_TAG),
                                    ) {
                                        sortedCombatOpponents.forEachIndexed { index, opponent ->
                                            CombatOpponentVitalsStrip(
                                                index = index,
                                                opponent = opponent,
                                                modifier =
                                                    Modifier
                                                        .fillMaxWidth()
                                                        .height(ShellChrome.VitalsStripHeight),
                                            )
                                        }
                                    }
                                    SectionRule(
                                        modifier =
                                            Modifier.testTag(
                                                COMBAT_OPPONENT_VITALS_BOTTOM_DIVIDER_TAG,
                                            ),
                                    )
                                }
                                Box(
                                    modifier =
                                        Modifier
                                            .fillMaxWidth()
                                            .weight(1f),
                                ) {
                                    NarrativePanel(
                                        visibleTranscript = visibleTranscript,
                                        scrollRequest = transcriptScrollRequest,
                                        interactionSheet = interactionSheet,
                                        renderInteractionSheet = false,
                                        onInteractionSheetDismiss = {
                                            onIntent(
                                                ShellIntent.SheetDismissed,
                                            )
                                        },
                                        onInteractionSheetBack = {
                                            onIntent(
                                                ShellIntent.SheetBackClicked,
                                            )
                                        },
                                        onInteractionSheetActionClick = {
                                                action ->
                                            onIntent(ShellIntent.SheetPrimaryActionClicked(action))
                                        },
                                        onInteractionSheetItemClick = {
                                                item ->
                                            onIntent(ShellIntent.SheetItemClicked(item))
                                        },
                                        inlineActions = inlineActionChips,
                                        onInlineActionClick = {
                                                command ->
                                            onIntent(ShellIntent.InlineActionClicked(command))
                                        },
                                        modifier = Modifier.fillMaxSize(),
                                    )
                                    SketchActionMenuStrip(
                                        sheet = surfaces.objectAnchoredMenu,
                                        onActionClick = {
                                                action ->
                                            onIntent(ShellIntent.SheetPrimaryActionClicked(action))
                                        },
                                        modifier =
                                            Modifier
                                                .align(Alignment.BottomCenter)
                                                .fillMaxWidth()
                                                .height(ShellChrome.SketchActionMenuHeight)
                                                .zIndex(SketchZIndex.ACTION_MENU),
                                    )
                                }
                            }
                        }
                        SectionRule(modifier = Modifier.testTag(PLAYER_VITALS_TOP_DIVIDER_TAG))
                        PlayerVitalsStripState(
                            statusSnapshotState = statusSnapshotState,
                            busySeconds = combatSelfBusySeconds,
                            busyCapturedAtMillis = combatSelfBusyCapturedAtMillis,
                            modifier =
                                Modifier
                                    .fillMaxWidth()
                                    .height(ShellChrome.VitalsStripHeight),
                        )
                    }
                    InteractionObjectContextPanelOverlay(
                        sheet = surfaces.contextPanelSheet,
                        onDismiss = { onIntent(ShellIntent.SheetDismissed) },
                        onOutsideDismiss = { onIntent(ShellIntent.SheetBackClicked) },
                        onBack = { onIntent(ShellIntent.SheetBackClicked) },
                        onActionClick = {
                                action ->
                            onIntent(ShellIntent.SheetPrimaryActionClicked(action))
                        },
                        modifier =
                            Modifier
                                .align(Alignment.TopCenter)
                                .fillMaxSize()
                                .zIndex(SketchZIndex.CONTEXT_PANEL),
                    )
                    ChatPanelOverlay(
                        open = chatPanelOpen,
                        input = chatInputState.value,
                        onInputChanged = { value -> onIntent(ShellIntent.ChatInputChanged(value)) },
                        onSend = { onIntent(ShellIntent.ChatSendClicked) },
                        modifier =
                            Modifier
                                .align(Alignment.TopCenter)
                                .zIndex(SketchZIndex.CHAT_BAR),
                    )
                    SketchInteractionSheetOverlay(
                        sheet = surfaces.inlineInteractionSheet,
                        onDismiss = { onIntent(ShellIntent.SheetDismissed) },
                        onOutsideDismiss = { onIntent(ShellIntent.SheetBackClicked) },
                        onBack = { onIntent(ShellIntent.SheetBackClicked) },
                        onActionClick = {
                                action ->
                            onIntent(ShellIntent.SheetPrimaryActionClicked(action))
                        },
                        onItemClick = { item -> onIntent(ShellIntent.SheetItemClicked(item)) },
                        modifier =
                            Modifier
                                .align(Alignment.TopCenter)
                                .fillMaxSize()
                                .zIndex(SketchZIndex.INTERACTION_SHEET),
                    )
                    FloatingMessageHost(
                        messages = floatingMessages,
                        modifier =
                            Modifier
                                .align(Alignment.TopCenter)
                                .padding(top = 6.dp)
                                .zIndex(SketchZIndex.FLOATING_MESSAGE),
                    )
                }
            }
        }
        if (exitOverlay != null) {
            Box(
                modifier =
                    Modifier
                        .fillMaxSize()
                        .testTag(EXIT_OVERLAY_DISMISS_AREA_TAG)
                        .clickable(
                            interactionSource = remember { MutableInteractionSource() },
                            indication = null,
                            onClick = { onIntent(ShellIntent.ExitOverlayDismissed) },
                        ),
            )
        }
    }
}

@Composable
internal fun SketchPrimaryDockBar(
    selectedLabel: String?,
    onIntent: (ShellIntent) -> Unit,
    modifier: Modifier = Modifier,
) {
    /*
     * 六槽位固定合同来自 shellDockSpecs（ShellChromeContract）：
     * label、命令入口 ID、test tag、点击语义不随模式变化。
     */
    val actions =
        remember(onIntent) {
            shellDockSpecs.map { spec ->
                SketchDockAction(spec.label) {
                    when (spec.kind) {
                        DockSlotKind.CHAT -> onIntent(ShellIntent.ChatBubbleClicked)
                        DockSlotKind.SHEET ->
                            onIntent(
                                ShellIntent.SheetPrimaryActionClicked(
                                    ShellAction(
                                        spec.label,
                                        spec.command,
                                        dismissAfterInvoke = false,
                                    ),
                                ),
                            )
                    }
                }
            }
        }
    Box(
        modifier =
            modifier.background(
                ShellPalette.CanvasRaised.copy(alpha = ShellChrome.MainControlFillAlpha),
            ),
    ) {
        Row(
            modifier = Modifier.fillMaxSize(),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            // 六个 Dock 槽保持等宽；rail 只在支持范围内与第一条内部线对齐。
            actions.forEachIndexed { index, action ->
                CompactSketchTextButton(
                    label = action.label,
                    selected = action.label == selectedLabel,
                    onClick = action.onClick,
                    modifier =
                        Modifier
                            .weight(1f)
                            .fillMaxHeight()
                            .testTag(shellDockSpecs[index].testTagPrefix),
                )
                if (index < actions.lastIndex) {
                    val dividerModifier =
                        if (index == 0) {
                            Modifier.testTag(SKETCH_DOCK_ATTRIBUTE_DIVIDER_TAG)
                        } else {
                            Modifier
                        }
                    SectionRuleVertical(
                        modifier = Modifier.fillMaxHeight().then(dividerModifier),
                    )
                }
            }
        }
        SectionRule(
            modifier =
                Modifier
                    .align(Alignment.TopStart)
                    .fillMaxWidth()
                    .zIndex(SketchZIndex.DOCK_BORDER),
        )
        SectionRuleVertical(
            modifier =
                Modifier
                    .align(Alignment.CenterStart)
                    .fillMaxHeight()
                    .testTag(SKETCH_DOCK_LEFT_BORDER_TAG)
                    .zIndex(SketchZIndex.DOCK_BORDER),
        )
        SectionRuleVertical(
            modifier =
                Modifier
                    .align(Alignment.CenterEnd)
                    .fillMaxHeight()
                    .testTag(SKETCH_DOCK_RIGHT_BORDER_TAG)
                    .zIndex(SketchZIndex.DOCK_BORDER),
        )
    }
}

@Composable
internal fun SketchCommonExitRollout(
    overlay: ExitOverlayState,
    onButtonClick: (ShellAction) -> Unit,
    modifier: Modifier = Modifier,
) {
    val actions =
        remember(overlay.buttons) { overlay.buttons.filter { it.label.isNotBlank() }.take(12) }
    Column(
        modifier =
            modifier
                .testTag(SKETCH_COMMON_EXIT_ROLLOUT_TAG)
                .shellSurfaceChrome(
                    fillColor = ShellPalette.CanvasRaised.copy(alpha = 0.98f),
                    borderColor = ShellLines.ControlStateBorder,
                    shape = RoundedCornerShape(8.dp),
                    shadowElevation = 0.dp,
                )
                .padding(1.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Column(
            modifier =
                Modifier
                    .fillMaxWidth()
                    .heightIn(max = SKETCH_COMMON_ROLLOUT_ROWS_VIEWPORT_HEIGHT)
                    .verticalScroll(rememberScrollState()),
        ) {
            actions.forEachIndexed { index, action ->
                if (index > 0) {
                    SectionRule(modifier = Modifier.fillMaxWidth())
                }
                UnifiedButtonSurface(
                    modifier =
                        Modifier
                            .fillMaxWidth()
                            .height(ShellChrome.SketchCompactButtonHeight)
                            .testTag(
                                "$SKETCH_COMMON_EXIT_ROLLOUT_ACTION_TAG_PREFIX${action.label}",
                            ),
                    onClick = { onButtonClick(action) },
                    fillColor = ShellPalette.CanvasInset.copy(alpha = 0.24f),
                    borderColor = Color.Transparent,
                    shape = RectangleShape,
                    minHeight = ShellChrome.SketchCompactButtonHeight,
                    horizontalPadding = ShellChrome.CompactControlHorizontalPadding,
                    verticalPadding = ShellChrome.CompactControlVerticalPadding,
                ) {
                    Text(
                        text = action.label,
                        color = ShellPalette.PaperMuted,
                        style = MaterialTheme.typography.labelMedium,
                        fontWeight = FontWeight.Medium,
                        textAlign = TextAlign.Center,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                }
            }
        }
        Box(
            modifier =
                Modifier
                    .fillMaxWidth()
                    .height(4.dp)
                    .background(ShellPalette.CanvasRaised.copy(alpha = 0.98f)),
        )
    }
}

@Composable
internal fun SceneFunctionBar(
    roomTitle: String,
    gameHour: String,
    modifier: Modifier = Modifier,
) {
    /* 字号降一档（Headline→Title）、padding 收紧整体上移，
     * 标题下加实时游戏时辰行（STAT 包附载 + 每分钟本地插值、新包重置基准）。 */
    var displayHour by androidx.compose.runtime.remember {
        androidx.compose.runtime.mutableStateOf(gameHour)
    }
    androidx.compose.runtime.LaunchedEffect(gameHour) {
        displayHour = gameHour
        while (true) {
            kotlinx.coroutines.delay(60_000)
            if (shichenIndexOf(displayHour) >= 0) {
                displayHour = advanceShichen(displayHour, 1)
            }
        }
    }
    val shownHour: String = if (shichenIndexOf(gameHour) >= 0 || gameHour.isBlank()) displayHour else gameHour
    Box(
        modifier =
            modifier
                .testTag(SKETCH_SCENE_BAR_TAG)
                .padding(horizontal = 10.dp, vertical = 2.dp),
    ) {
        Row(
            modifier =
                Modifier
                    .fillMaxWidth()
                    .align(Alignment.CenterStart),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(
                text = roomTitle,
                color = ShellPalette.Paper,
                style = ShellType.SceneTitle,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                modifier = Modifier.testTag(SKETCH_SCENE_ROOM_TITLE_TAG),
            )
            if (shownHour.isNotBlank()) {
                Box(
                    modifier =
                        Modifier
                            .align(Alignment.Bottom)
                            .padding(start = 2.dp)
                            .testTag(SKETCH_SCENE_GAME_HOUR_TAG),
                    contentAlignment = Alignment.BottomStart,
                ) {
                    Text(
                        text = shownHour,
                        color = ShellPalette.PaperMuted,
                        style = ShellType.Caption,
                        maxLines = 1,
                    )
                }
            }
        }
    }
}

@Composable
internal fun SketchActionMenuStrip(
    sheet: ShellSheetState?,
    onActionClick: (ShellAction) -> Unit,
    modifier: Modifier = Modifier,
) {
    if (sheet == null) {
        return
    }
    Box(
        modifier =
            modifier
                .testTag(SKETCH_ACTION_MENU_TAG)
                .padding(horizontal = 6.dp, vertical = 4.dp),
        contentAlignment = Alignment.Center,
    ) {
        HorizontalInteractionActionStrip(
            sheet = sheet,
            onActionClick = onActionClick,
            modifier = Modifier.fillMaxSize(),
            fixedPrimaryVisibleItems = ShellChrome.SketchPrimaryActionVisibleSlots,
            showPanelChrome = false,
        )
    }
}

internal fun sketchDockButtonWidth(contentWidth: Dp): Dp {
    val actionCount = ShellChrome.PrimaryDockActionCount
    val dividerWidth = ShellChrome.DividerThickness * (actionCount - 1)
    return ((contentWidth - dividerWidth) / actionCount.toFloat()).coerceAtLeast(0.dp)
}

internal fun DockWorkspaceSection.sketchDockLabel(): String =
    when (this) {
        DockWorkspaceSection.ATTRIBUTE -> "属性"
        DockWorkspaceSection.INVENTORY -> "背包"
        DockWorkspaceSection.SKILL -> "技能"
        DockWorkspaceSection.ACTIVITY -> "任务"
    }

internal fun MainShellSceneState.activeSketchDockLabel(): String {
    return when {
        chatPanelOpen -> "聊天"
        interactionSheet?.navigation?.dockSection != null -> interactionSheet.navigation.dockSection.sketchDockLabel()
        interactionSheet?.kind == ShellSheetKind.COMBAT ||
            exitOverlay?.type == ExitOverlayType.COMBAT ||
            combatActive -> "战斗"
        else -> ""
    }
}

internal fun SheetNavigationMeta.rootDockSaveableStateKey(): String? {
    // 同一 section 的不同路由页（quest/activity 等 depth-0 面）
    // 若共用裸 section 名，AnimatedContent 过渡期双方同 key 并发会抛
    // "Key used multiple times"——拼上 routeKey 保证唯一，同页面往返仍保状态。
    val section = dockSection?.takeIf { depth == 0 } ?: return null
    return if (routeKey.isBlank()) section.name else section.name + "/" + routeKey
}

internal fun shouldAnimateDockWorkspaceRoute(
    initial: SheetNavigationMeta,
    target: SheetNavigationMeta,
): Boolean {
    return initial.dockSection == target.dockSection && initial.depth != target.depth
}
