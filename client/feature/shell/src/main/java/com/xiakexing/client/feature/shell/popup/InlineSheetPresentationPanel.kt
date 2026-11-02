package com.xiakexing.client.feature.shell.popup

import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.rememberScrollState
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.xiakexing.client.feature.shell.ShellAction
import com.xiakexing.client.feature.shell.ShellSheetItem
import com.xiakexing.client.feature.shell.ShellSheetPresentation
import com.xiakexing.client.feature.shell.ShellSheetState
import com.xiakexing.client.feature.shell.component.ShellErrorRow
import com.xiakexing.client.feature.shell.ui.ShellChrome
import com.xiakexing.client.feature.shell.ui.ShellFontFamily
import com.xiakexing.client.feature.shell.ui.ShellLines
import com.xiakexing.client.feature.shell.ui.ShellPalette

internal const val SHEET_LOADING_STATE_TAG = "sheet_loading_state"
internal const val SHEET_ERROR_STATE_TAG = "sheet_error_state"
internal const val SHEET_KEY_VALUE_GRID_TAG = "sheet_key_value_grid"
internal const val SHEET_KEY_VALUE_CELL_TAG_PREFIX = "sheet_key_value_cell"
internal const val SHEET_SETTINGS_ROW_TAG = "sheet_settings_row"

@Composable
internal fun SheetFeedbackStatus(
    sheet: ShellSheetState,
    onRetry: (ShellAction) -> Unit,
) {
    if (sheet.errorMessage.isNotBlank()) {
        ShellErrorRow(
            text = sheet.errorMessage,
            retryLabel = sheet.retryAction?.let { "重试" },
            onRetry = sheet.retryAction?.let { action -> { onRetry(action) } },
            modifier = Modifier.testTag(SHEET_ERROR_STATE_TAG),
        )
    }
    // 两阶段渲染根除：loading 且有内容时直接显示已有内容，不显示"正在刷新"行；
    // 数据到达后由协调器 emit 新 state 原位静默 patch。
}

@Composable
internal fun InitialSheetLoadingState(modifier: Modifier = Modifier) {
    // 两阶段渲染根除：loading 且无内容时显示空白占位，不显示"正在加载"文字；
    // 数据到达后由协调器 emit 新 state 一次性渲染。
    Box(
        modifier =
            modifier
                .fillMaxWidth()
                .heightIn(min = 64.dp)
                .padding(horizontal = 12.dp)
                .testTag(SHEET_LOADING_STATE_TAG),
    )
}

@Composable
internal fun PresentationSheetPanel(
    sheet: ShellSheetState,
    onActionClick: (ShellAction) -> Unit,
    onItemClick: (ShellSheetItem) -> Unit,
    modifier: Modifier = Modifier,
) {
    when (sheet.presentation) {
        ShellSheetPresentation.KEY_VALUE_GRID ->
            KeyValueGrid(
                items = sheet.items,
                modifier = modifier,
            )
        ShellSheetPresentation.SETTINGS -> SettingsPanel(sheet, onItemClick, modifier)
        ShellSheetPresentation.MANAGEMENT_LIST -> ManagementPanel(sheet, onItemClick, modifier)
        ShellSheetPresentation.HELP_INDEX ->
            HelpIndexPanel(
                sheet,
                onActionClick,
                onItemClick,
                modifier,
            )
        ShellSheetPresentation.DEFAULT -> Unit
    }
}

@Composable
private fun KeyValueGrid(
    items: List<ShellSheetItem>,
    modifier: Modifier,
) {
    // 键值网格内容固定且紧凑（两列单元格），全部渲染不滚动，
    // 信息始终限制在弹窗内。按 section 布局分组（组间分隔线），
    // 不绘制背景色块或小标题。
    val groups =
        remember(items) {
            items.withIndex().groupBy { (_, item) -> item.section.ifBlank { "" } }
        }
    Column(
        modifier = modifier.fillMaxWidth().testTag(SHEET_KEY_VALUE_GRID_TAG),
        verticalArrangement = Arrangement.spacedBy(2.dp),
    ) {
        var first = true
        groups.forEach { (_, sectionItems) ->
            if (!first) {
                // 组间分隔：1dp 分隔线 + 上下留白，不绘制背景色块。
                Spacer(modifier = Modifier.height(3.dp))
                Box(
                    modifier =
                        Modifier
                            .fillMaxWidth()
                            .height(ShellChrome.DividerThickness)
                            .background(ShellLines.StructuralDivider),
                )
                Spacer(modifier = Modifier.height(3.dp))
            }
            first = false
            val rows = sectionItems.map { (_, item) -> item }.chunked(2)
            rows.forEachIndexed { _, rowItems ->
                KeyValueRow(
                    rowItems = rowItems,
                )
            }
        }
    }
}

@Composable
private fun KeyValueRow(
    rowItems: List<ShellSheetItem>,
    modifier: Modifier = Modifier,
) {
    Row(
        modifier = modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        rowItems.forEach { item ->
            KeyValueCell(
                item = item,
                modifier = Modifier.weight(1f),
            )
        }
        if (rowItems.size == 1) {
            Spacer(modifier = Modifier.weight(1f))
        }
    }
}

@Composable
private fun KeyValueCell(
    item: ShellSheetItem,
    modifier: Modifier = Modifier,
) {
    Row(
        modifier =
            modifier
                .heightIn(min = 36.dp)
                .padding(horizontal = 4.dp, vertical = 2.dp)
                .testTag("$SHEET_KEY_VALUE_CELL_TAG_PREFIX:${item.title}"),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            text = "${item.title}：",
            color = ShellPalette.PaperMuted,
            style =
                MaterialTheme.typography.labelMedium.copy(
                    fontFamily = ShellFontFamily,
                    fontSize = 15.sp,
                    lineHeight = 19.sp,
                ),
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
        Text(
            text = item.trailing,
            color =
                if (item.trailing.isBlank() || item.trailing == "--") {
                    ShellPalette.PaperFaint
                } else {
                    ShellPalette.Paper
                },
            style =
                MaterialTheme.typography.bodyMedium.copy(
                    fontFamily = ShellFontFamily,
                    fontSize = 16.sp,
                    lineHeight = 19.sp,
                ),
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            modifier = Modifier.weight(1f),
        )
    }
}

@Composable
private fun SettingsPanel(
    sheet: ShellSheetState,
    onItemClick: (ShellSheetItem) -> Unit,
    modifier: Modifier,
) {
    Column(modifier = modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Text("界面密度", color = ShellPalette.PaperMuted, style = MaterialTheme.typography.labelLarge)
        Row(
            modifier = Modifier.fillMaxWidth().testTag(SHEET_SETTINGS_ROW_TAG),
            horizontalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            sheet.items.forEach { item ->
                UnifiedPopupButton(
                    label = item.title,
                    onClick = { onItemClick(item) },
                    enabled = !sheet.loading,
                    tone = if (item.selected) UnifiedPopupButtonTone.Primary else UnifiedPopupButtonTone.Secondary,
                    minHeight = 44.dp,
                    modifier =
                        Modifier
                            .weight(1f)
                            .semantics {
                                role = Role.RadioButton
                                selected = item.selected
                            },
                )
            }
        }
    }
}

@Composable
private fun ManagementPanel(
    sheet: ShellSheetState,
    onItemClick: (ShellSheetItem) -> Unit,
    modifier: Modifier,
) {
    val groups = remember(sheet.items) { sheet.items.groupBy { it.section.ifBlank { "操作" } } }
    LazyColumn(
        modifier = modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(5.dp),
    ) {
        groups.forEach { (section, sectionItems) ->
            item(key = "section:$section") {
                Text(
                    text = section,
                    color = if (section == "危险操作") ShellPalette.Scarlet else ShellPalette.PaperMuted,
                    style =
                        MaterialTheme.typography.labelLarge.copy(
                            fontWeight = FontWeight.SemiBold,
                        ),
                    modifier = Modifier.padding(start = 4.dp, top = 5.dp, bottom = 1.dp),
                )
            }
            itemsIndexed(
                sectionItems,
                key = {
                        itemIndex,
                        item,
                    ->
                    "${item.section}:${item.title}:${item.command}:$itemIndex"
                },
            ) { _, item ->
                SheetRow(item = item, enabled = !sheet.loading, onClick = { onItemClick(item) })
            }
        }
    }
}

@Composable
private fun HelpIndexPanel(
    sheet: ShellSheetState,
    onActionClick: (ShellAction) -> Unit,
    onItemClick: (ShellSheetItem) -> Unit,
    modifier: Modifier,
) {
    Column(modifier = modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Row(
            modifier =
                Modifier
                    .fillMaxWidth()
                    .horizontalScroll(rememberScrollState()),
            horizontalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            sheet.panelTabs.forEach { action ->
                val id = action.command.substringAfterLast('/')
                val selected = id == sheet.panelSelectedTabId
                UnifiedPopupButton(
                    label = action.label,
                    onClick = { onActionClick(action) },
                    enabled = !sheet.loading,
                    tone = if (selected) UnifiedPopupButtonTone.Primary else UnifiedPopupButtonTone.Quiet,
                    minHeight = 44.dp,
                    modifier =
                        Modifier.semantics {
                            role = Role.Tab
                            this.selected = selected
                        },
                )
            }
        }
        LazyColumn(
            modifier = Modifier.fillMaxWidth(),
            verticalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            itemsIndexed(
                sheet.items,
                key = { itemIndex, item -> "${item.command}:$itemIndex" },
            ) { _, item ->
                SheetRow(item = item, enabled = !sheet.loading, onClick = { onItemClick(item) })
            }
        }
    }
}
