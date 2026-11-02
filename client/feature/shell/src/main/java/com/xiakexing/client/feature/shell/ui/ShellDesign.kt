package com.xiakexing.client.feature.shell.ui

import androidx.compose.animation.EnterTransition
import androidx.compose.animation.ExitTransition
import androidx.compose.animation.core.Easing
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutVertically
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shadow
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.Density
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/**
 * XiaKeXing APK「墨卷」设计 token 真源（品质轮：淡雅暗色系 · 武侠水墨）。
 *
 * 依据：用户裁决：
 * 精致典雅、淡雅暗色、克制、武侠风格——深黛青灰基调（不纯黑不褐），
 * 宣纸米白文字，黛青/藤黄/朱砂少量点缀，一屏两色。
 */
object ShellPalette {
    // ===== 墨阶（深黛青灰，E0..E4 海拔 = 明度阶；干净不脏、不硬不淡）=====

    /** E0 App 底色、主壳底 */
    val Ink0 = Color(0xFF121518)

    /** E1 主表面、按钮底 */
    val Ink1 = Color(0xFF181B20)

    /** 主界面专用底色，比登录页底色更明亮。 */
    val MainSurface = Color(0xFF171B1F)

    /** 聊天层与所有弹窗共用的抬升表面。 */
    val PopupSurface = Color(0xFF242A30)
    val ChatSurface = PopupSurface

    /** E2 面板、卡片底 */
    val Ink2 = Color(0xFF22262B)

    /** E3 选中底、抬升卡 */
    val Ink3 = Color(0xFF2A2F35)

    /** E4 弹层面（配唯一一级投影 ShellElevation.Popup） */
    val Ink4 = Color(0xFF32373D)

    /** 内嵌凹陷面（vitals 底、输入框底） */
    val InkInset = Color(0xFF0E1114)

    /** 墨线：分割线/描边（配合 DividerLineAlpha 使用） */
    val InkLine = Color(0xFF626C76)

    // ===== 纸色（文字）=====

    /** 宣纸米白：主文字（微暖，不刺眼） */
    val Paper = Color(0xFFE4DFD4)

    /** 次要文字 */
    val PaperMuted = Color(0xFFA19D93)

    /** 占位/禁用/弱化（仅装饰与非关键信息） */
    val PaperFaint = Color(0xFF77736B)

    /** 叙事正文米色 */
    val NarrativePaper = Color(0xFFD2CDC2)

    // ===== 丹青（语义强调，一屏两色原则；克制使用）=====

    /** 黛青：选中/信息/链接/进行中（青花瓷感，去荧光） */
    val Accent = Color(0xFF8FB4C4)

    /** 黛青柔：次级信息 */
    val AccentSoft = Color(0xFF6E8B99)

    /** 藤黄：任务/目标/追踪/路线 */
    val QuestAmber = Color(0xFFC9A35F)

    /** 任务路线出口的统一浅色底，九宫格与特殊出口共用。 */
    val QuestRouteFill = QuestAmber.copy(alpha = 0.07f)

    /** 朱砂：危险/错误/攻击 */
    val Cinnabar = Color(0xFFBC7266)

    /** 青玉：成功/完成/正向 */
    val JadeGreen = Color(0xFF87A58A)

    /** 藤黄浅：警告/将过期 */
    val WarnAmber = Color(0xFFB9A06A)

    // ===== 系统色（战斗 vitals，不参与墨卷和谐）=====
    val VitalHp = Color(0xFFB34D56)
    val VitalNeili = Color(0xFF285B7A)
    val VitalJing = Color(0xFF4F8742)

    // ===== 遮罩（全 App 唯一遮罩族）=====

    /** 弹层遮罩：#0E1014 @ 72% */
    val Scrim = Color(0xB80E1014)

    /** 顶部覆盖层下垫（聊天）/ 重连过渡：#0E1014 @ 40% */
    val ScrimLight = Color(0x660E1014)

    // ===== 旧 token 迁移别名（禁止新增引用；改造阶段逐步清理）=====
    val InkBlack get() = Ink0
    val InkSurface get() = Ink1
    val CanvasRaised get() = Ink2
    val CanvasSoft get() = Ink3
    val CanvasLighter get() = Ink4
    val CanvasInset get() = InkInset

    /** 危险：旧名 → 朱砂 */
    val Scarlet get() = Cinnabar

    /** 旧灰占位 → 藤黄浅（警告语义） */
    val Amber get() = WarnAmber

    /** 任务橙：旧名 → 藤黄 */
    val QuestAccent get() = QuestAmber
    val DividerLine get() = InkLine
    val Overlay get() = Scrim
}

/** 圆角规则：S=按钮/chip/输入框，M=卡片，L=弹层（品质轮：4→6 / 8→10 / 12→16） */
object ShellRadius {
    val S = RoundedCornerShape(4.dp)
    val M = RoundedCornerShape(6.dp)
    val L = RoundedCornerShape(8.dp)
}

/** 海拔：仅弹层家族允许真实投影，其余 0 阴影 + 墨线 */
object ShellElevation {
    val None = 0.dp
    val Popup = 8.dp
}

/** 线条只表达结构、控件状态或业务语义，普通内容层级优先由留白和表面明度建立。 */
object ShellLines {
    val StructuralDivider = ShellPalette.InkLine.copy(alpha = 0.94f)
    val SurfaceBoundary = ShellPalette.InkLine.copy(alpha = 0.92f)
    val ControlStateBorder = ShellPalette.InkLine.copy(alpha = 0.96f)
    val SelectedBorder = ShellPalette.Accent.copy(alpha = 0.82f)
    val QuestBorder = ShellPalette.QuestAmber.copy(alpha = 0.86f)
    val DangerBorder = ShellPalette.Cinnabar.copy(alpha = 0.82f)
    val MapConnector = ShellPalette.AccentSoft.copy(alpha = 0.64f)
}

/** 动效 token（方案 §17）；降级模式统一接入 */
object ShellMotion {
    const val MicroMs = 140
    const val SurfaceEnterMs = 200
    const val SurfaceExitMs = 170
    const val EmphasisMs = 300
    const val ReduceMotionMs = 100
    const val PressedScale = 0.98f
    const val PressedAlpha = 0.98f

    val EnterEasing: Easing = FastOutSlowInEasing
}

/**
 * 字号阶梯（方案 §5.2 / R-04 品质轮）：唯一字号真源，新组件禁止现场 copy(fontSize=...)。
 * 全部层级统一使用主题提供的 Maple Mono NL CN；主字号阶梯最小 12sp，窄栏可按固定四字宽度使用 9sp。
 */
object ShellType {
    /** 18/24 sheet 标题。 */
    val Title: TextStyle
        @Composable get() = MaterialTheme.typography.titleLarge

    /** 22/30 场景栏房间名。 */
    val SceneTitle: TextStyle
        @Composable get() = MaterialTheme.typography.headlineMedium

    /** 13/20 主文本叙事正文 */
    val Narrative: TextStyle
        @Composable get() =
            MaterialTheme.typography.bodySmall.copy(
                fontSize = 13.sp,
                lineHeight = 20.sp,
            )

    /** 状态条姓名槽的四字参考样式；独立于正文，避免正文变更联动槽宽。 */
    val VitalsNameMeasure: TextStyle
        @Composable get() =
            MaterialTheme.typography.bodySmall.copy(
                fontFamily = ShellFontFamily,
                fontWeight = FontWeight.Normal,
                fontSize = 13.sp,
                lineHeight = 20.sp,
            )

    /** 状态条姓名与三项数值的实际排版样式。 */
    val Vitals: TextStyle
        @Composable get() =
            MaterialTheme.typography.labelSmall.copy(
                fontSize = 9.5.sp,
                lineHeight = 11.sp,
                letterSpacing = (-0.3f).sp,
                fontFamily = ShellFontFamily,
                fontWeight = FontWeight.SemiBold,
                shadow =
                    Shadow(
                        color = Color.Black.copy(alpha = 0.62f),
                        offset = Offset(0.5f, 0.5f),
                        blurRadius = 1.5f,
                    ),
            )

    /** 12/16 默认紧凑标签；固定宽度左栏与出口格按各自几何使用 9/10/11sp。 */
    val Caption: TextStyle
        @Composable get() = MaterialTheme.typography.labelSmall

    /** 10/16 战斗 perform 招式名，保证窄屏实际五列单格可容纳四个汉字。 */
    val CombatAction: TextStyle
        @Composable get() =
            Narrative.copy(
                fontSize = 10.sp,
                lineHeight = 16.sp,
            )
}

/**
 * 减少动态效果（降级总开关）：位移/缩放动画退化为 ≤100ms 淡变。
 * 由主壳按 ShellUiState.reduceMotion 提供；弹层 Dialog 内容在同一组合树内自动继承。
 */
internal val LocalShellReduceMotion = staticCompositionLocalOf { false }

/** 表面进入过渡：降级时仅 ≤100ms 淡入，否则上滑 + 淡入（方案 §17.2）。 */
@Composable
internal fun shellEnterTransition(
    offsetFraction: Float,
    durationMs: Int = ShellMotion.SurfaceEnterMs,
): EnterTransition {
    if (LocalShellReduceMotion.current) {
        return fadeIn(animationSpec = tween(durationMillis = 100))
    }
    return slideInVertically(
        initialOffsetY = { fullHeight -> (fullHeight * offsetFraction).toInt() },
        animationSpec = tween(durationMillis = durationMs, easing = ShellMotion.EnterEasing),
    ) + fadeIn(animationSpec = tween(durationMillis = durationMs))
}

/** 表面退出过渡：降级时仅 ≤100ms 淡出，否则下滑 + 淡出。 */
@Composable
internal fun shellExitTransition(
    offsetFraction: Float,
    durationMs: Int = ShellMotion.SurfaceExitMs,
): ExitTransition {
    if (LocalShellReduceMotion.current) {
        return fadeOut(animationSpec = tween(durationMillis = 100))
    }
    return slideOutVertically(
        targetOffsetY = { fullHeight -> (fullHeight * offsetFraction).toInt() },
        animationSpec = tween(durationMillis = durationMs, easing = ShellMotion.EnterEasing),
    ) + fadeOut(animationSpec = tween(durationMillis = durationMs))
}

object ShellChrome {
    val PanelShape = ShellRadius.L
    val PopupPanelMaxWidth = 376.dp
    val PopupPanelMaxHeight = 440.dp

    /** 更新说明是单条日志；短内容随内容收缩，长内容仅在此上限内滚动。 */
    val UpdatePopupMaxHeight = 360.dp

    /** 场景物品详情保持紧凑，长说明在正文区域内滚动。 */
    val CompactItemSheetMaxHeight = 184.dp
    val ControlShape = ShellRadius.S

    /** 卡片（网格卡/列表卡）统一圆角 */
    val CardShape = ShellRadius.M

    val ScreenPadding = 12.dp
    val FramePadding = 0.dp
    val SectionGap = 10.dp
    val RailInset = 4.dp
    val RailWidth = 62.dp

    /** rail 的最小操作宽度；仅在理想 Dock 槽低于该值时生效。 */
    val RailNarrowWidth = 52.dp

    /** 手机窗口中左栏最多只比原 rail 基准增加 8dp，避免平板地图区被过度挤压。 */
    val DockAlignedRailMaxExpansion = 8.dp

    /** 弹层分类 rail 宽度：背包/技能/聊天共用（CategoryRailLayout） */
    val PopupRailWidth = 56.dp
    val RailControlWidth = 54.dp
    val RailExitStartInset = 4.dp
    val RailExitControlHeight = 40.dp
    val RailScrollableLabelWidth = 44.dp
    val RailInnerHorizontalInset = 0.dp
    val ObjectRailControlHeight = 36.dp
    val ObjectRailHorizontalInset = 2.dp
    val ObjectRailVerticalSpacing = 2.dp
    val ObjectRailVerticalInset = ObjectRailVerticalSpacing
    val ObjectRailMapBottomInset = ObjectRailVerticalSpacing
    val PanelTopInset = 8.dp
    val PanelBottomInset = 8.dp
    val ExitTopInset = 6.dp
    val ExitBottomInset = 8.dp
    val QuestGuideLaneHeight = 34.dp

    /** 顶部聊天预览收紧 8dp，等量让给出口区。 */
    val SketchChatHeight = 116.dp

    /** dock 保持 44dp（方案 §5.6 触摸目标下限） */
    val SketchPrimaryBarHeight = 44.dp

    /** 场景栏向上扩展至 50dp，只保留单行房间标题。 */
    val SketchSceneBarHeight = 50.dp
    val SketchActionMenuHeight = 50.dp
    val SketchShortHeightBreakpoint = 640.dp
    val SketchCompactLandscapeHeightBreakpoint = 480.dp
    val SketchNarrativeHeightThreshold = 180.dp
    val SketchShortMapAreaHeight = 112.dp
    val SketchMapHeightFraction = 19f / 79f
    val SketchPrimaryActionVisibleSlots = 6

    /** 任务引导条：32 -> 30dp */
    val SketchQuestLaneHeight = 30.dp
    val SketchCompactButtonHeight = 28.dp
    val VitalsStripHeight = 18.dp
    val VitalsNameReferenceText = "甲乙丙丁"
    val VitalsNameWidthExtra = 6.dp
    val VitalsTextHorizontalInset = 1.dp

    /** 出口区收紧后，combat perform 两行按钮不再被拉伸到整块地图高度。 */
    val CombatActionRowMaxHeight = 48.dp
    val SketchMapAreaMaxHeight = 120.dp

    /** 地图入口移入对象栏后，出口网格只保留基础水平留白。 */
    val SketchMapGridHorizontalClearance = 10.dp

    /** 底部多留 4dp，使九宫格在加高后的出口区内整体略微上提。 */
    val SketchMapGridBottomClearance = 4.dp
    val SketchMapGridPadding = 2.dp
    val SketchMapGridSpacing = 2.dp
    val ExitOverlayRowMaxHeight = 44.dp
    val TextHorizontalPadding = 6.dp
    val TextVerticalPadding = 12.dp
    val ControlHorizontalPadding = 8.dp
    val ControlVerticalPadding = 5.dp
    val CompactControlHorizontalPadding = 4.dp
    val CompactControlVerticalPadding = 2.dp
    val RailControlHorizontalPadding = 2.dp
    val RailControlVerticalPadding = 0.dp
    val GridControlHorizontalPadding = 0.dp
    val GridControlVerticalPadding = 0.dp
    val ControlMinHeight = 52.dp

    /** 弹层动作保持 44dp 触控下限，视觉按钮收敛为手游工具条尺度。 */
    val PopupActionTouchHeight = 44.dp
    val PopupActionVisualHeight = 30.dp
    val PopupActionMinWidth = 56.dp
    val PopupActionMaxWidth = 132.dp
    val PopupDecisionMaxWidth = 300.dp
    val PopupContentMaxWidth = 340.dp
    val TaskRowMinHeight = 44.dp
    val TaskRowHorizontalPadding = 12.dp
    val TaskRowVerticalPadding = 8.dp
    val TaskListRowMinHeight = PopupActionTouchHeight + TaskRowVerticalPadding * 2
    val TitleActionMinWidth = 48.dp
    val TitleActionMinHeight = 38.dp
    val DockControlMinHeight = SketchPrimaryBarHeight
    val RailControlMinHeight = 48.dp
    val GridControlMinHeight = 56.dp
    val DividerThickness = 1.dp
    val PrimaryDockActionCount = 6

    /**
     * 计算地图区左栏分割线应使用的理想宽度。
     * 理想 Dock 槽宽为 `(窗口宽度 - 5 × 分割线) / 6`；rail 仅在窄窗口与宽窗口边界处
     * 分别限制为至少 52dp、至多比 62dp 基准宽 8dp，避免破坏窄屏操作区或挤压宽窗口正文。
     * 因此在 317dp..425dp 的支持范围内，rail 分割线与六等分 Dock 的“属性”右边界重合。
     */
    fun railWidthAlignedToPrimaryDock(contentWidth: Dp): Dp {
        val dockSlotWidth =
            (contentWidth - DividerThickness * (PrimaryDockActionCount - 1)) /
                PrimaryDockActionCount
        return maxOf(RailNarrowWidth, minOf(dockSlotWidth, RailWidth + DockAlignedRailMaxExpansion))
    }

    /**
     * 用 Dock 已取整的物理像素边界布局 rail，避免 Dp rail 与 weight 槽各自取整造成 1px 漂移。
     * 限幅也在像素域执行，支持区间内首槽右边界与 rail 分割线使用同一个像素列。
     */
    fun railWidthAlignedToPrimaryDock(
        contentWidth: Dp,
        density: Density,
    ): Dp {
        val contentWidthPx = with(density) { contentWidth.roundToPx() }
        val dividerWidthPx = with(density) { DividerThickness.roundToPx() }
        val dockSlotWidthPx =
            (contentWidthPx - dividerWidthPx * (PrimaryDockActionCount - 1)) /
                PrimaryDockActionCount
        val minRailWidthPx = with(density) { RailNarrowWidth.roundToPx() }
        val maxRailWidthPx = with(density) { (RailWidth + DockAlignedRailMaxExpansion).roundToPx() }
        return with(density) {
            dockSlotWidthPx.coerceIn(minRailWidthPx, maxRailWidthPx).toDp()
        }
    }

    val TitleDividerInset = 22.dp
    val SurfaceHeaderTopInset = 1.dp
    val SurfaceHeaderContentHeight = 40.dp
    val SurfaceHeaderHeight = SurfaceHeaderTopInset + SurfaceHeaderContentHeight

    /** 头部返回/关闭触控层：42dp 宽 × 40dp 高（fillMaxHeight 于 40dp 行内），图标 16dp 居中后上=右=左=13dp 全弹窗统一。 */
    val SurfaceHeaderActionSize = 42.dp
    val SurfaceHeaderIconSize = 16.dp

    /** 所有弹层的末行操作与底边保持一致的呼吸距离。 */
    val PopupFooterBottomInset = 9.dp

    /** 主壳控件 0 阴影（弹层用 PopupShadowElevation） */
    val ControlShadowElevation = ShellElevation.None

    /** 弹层家族唯一投影 */
    val PanelShadowElevation = ShellElevation.Popup
    val FrameShadowElevation = ShellElevation.Popup

    const val FrameAlpha = 0.92f
    const val ControlFillAlpha = 0.42f
    const val ControlRaisedFillAlpha = 0.72f
    const val BackgroundImageScrimAlpha = 0.62f
    const val MainSurfaceFillAlpha = 0.70f
    const val MainSurfaceBorderAlpha = 0.58f
    const val MainControlFillAlpha = 0.54f
    const val MainControlRaisedFillAlpha = 0.75f
    const val DividerAlpha = 0.88f
    const val DividerLineAlpha = 0.90f
    const val MainControlBorderAlpha = 0.90f
    const val MainSelectedBorderAlpha = 0.82f
    const val SketchSelectedFillAlpha = 0.36f
    const val SketchSelectedBorderAlpha = 0.84f
    const val ControlBorderAlpha = 0.90f
    const val FloatingBorderAlpha = 0.84f
}

val ShellBackgroundColor = ShellPalette.Ink0
