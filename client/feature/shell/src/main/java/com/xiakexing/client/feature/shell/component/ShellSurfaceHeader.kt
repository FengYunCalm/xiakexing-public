package com.xiakexing.client.feature.shell.component

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.xiakexing.client.feature.shell.ui.ShellChrome
import com.xiakexing.client.feature.shell.ui.ShellPalette
import com.xiakexing.client.feature.shell.ui.ShellType

const val SHELL_SURFACE_HEADER_TAG = "shell_surface_header"
const val SHELL_SURFACE_HEADER_BACK_TAG = "shell_surface_header_back"
const val SHELL_SURFACE_HEADER_CLOSE_TAG = "shell_surface_header_close"

/**
 * 统一表面头部：1dp 顶部间距 + 40dp 内容行（带标题/无标题一致）。
 * 返回/关闭均为 42dp 宽 × 40dp 高触控层 + 16dp 图标，图标居中后
 * 上=右=左=13dp 全弹窗统一；无返回历史时左侧保留空位，保证标题
 * 始终几何居中（光学平衡）。无标题模式经 headerText 渲染纯文本信息于
 * 中间槽（背包容量/技能上限/任务数）。
 */
@Composable
internal fun ShellSurfaceHeader(
    title: String,
    modifier: Modifier = Modifier,
    showTitle: Boolean = true,
    onBack: (() -> Unit)? = null,
    onClose: (() -> Unit)? = null,
    closeContentDescription: String = "关闭",
    backContentDescription: String = "返回",
    headerTestTag: String = SHELL_SURFACE_HEADER_TAG,
    backTestTag: String = SHELL_SURFACE_HEADER_BACK_TAG,
    titleTestTag: String? = null,
    closeTestTag: String = SHELL_SURFACE_HEADER_CLOSE_TAG,
    headerText: String = "",
) {
    Column(
        modifier =
            modifier
                .fillMaxWidth()
                .height(ShellChrome.SurfaceHeaderHeight)
                .testTag(headerTestTag),
    ) {
        Box(modifier = Modifier.height(ShellChrome.SurfaceHeaderTopInset))
        Row(
            modifier =
                Modifier
                    .fillMaxWidth()
                    .height(ShellChrome.SurfaceHeaderContentHeight),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Box(
                modifier =
                    Modifier
                        .width(ShellChrome.SurfaceHeaderActionSize)
                        .fillMaxHeight(),
                contentAlignment = Alignment.CenterStart,
            ) {
                if (onBack != null) {
                    UnifiedButtonSurface(
                        modifier =
                            Modifier
                                .width(ShellChrome.SurfaceHeaderActionSize)
                                .fillMaxHeight()
                                .testTag(backTestTag),
                        onClick = onBack,
                        fillColor = Color.Transparent,
                        borderColor = Color.Transparent,
                        minHeight = ShellChrome.SurfaceHeaderContentHeight,
                        horizontalPadding = 0.dp,
                        verticalPadding = 0.dp,
                        semanticDescription = backContentDescription,
                    ) {
                        BackArrowIcon(
                            color = ShellPalette.PaperMuted,
                            modifier = Modifier.size(ShellChrome.SurfaceHeaderIconSize),
                        )
                    }
                }
            }
            if (showTitle) {
                val titleModifier =
                    if (titleTestTag == null) {
                        Modifier.weight(1f)
                    } else {
                        Modifier
                            .weight(1f)
                            .testTag(titleTestTag)
                    }
                Text(
                    text = title,
                    color = ShellPalette.Paper,
                    style = ShellType.Title,
                    fontWeight = FontWeight.SemiBold,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    textAlign = TextAlign.Center,
                    modifier = titleModifier,
                )
            } else if (headerText.isNotBlank()) {
                Text(
                    text = headerText,
                    color = ShellPalette.PaperMuted,
                    style = MaterialTheme.typography.labelMedium,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    textAlign = TextAlign.Center,
                    modifier = Modifier.weight(1f),
                )
            } else {
                Box(modifier = Modifier.weight(1f))
            }
            Box(
                modifier =
                    Modifier
                        .width(ShellChrome.SurfaceHeaderActionSize)
                        .fillMaxHeight(),
                contentAlignment = Alignment.CenterEnd,
            ) {
                if (onClose != null) {
                    SurfaceHeaderCloseButton(
                        onClick = onClose,
                        testTag = closeTestTag,
                        contentDescriptionText = closeContentDescription,
                        modifier =
                            Modifier
                                .width(ShellChrome.SurfaceHeaderActionSize)
                                .fillMaxHeight(),
                    )
                }
            }
        }
    }
}
