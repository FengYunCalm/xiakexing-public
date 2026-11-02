package com.xiakexing.client.feature.shell.popup

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.layout.wrapContentHeight
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.xiakexing.client.feature.shell.ShellSheetEmptyPresentation
import com.xiakexing.client.feature.shell.ShellSheetItem
import com.xiakexing.client.feature.shell.ShellSheetState
import com.xiakexing.client.feature.shell.ui.ShellLines
import com.xiakexing.client.feature.shell.ui.ShellPalette

const val STRUCTURED_WORKBENCH_TAG = "structured_sheet_workbench"
const val STRUCTURED_WORKBENCH_CONTEXT_TAG = "structured_sheet_workbench_context"
const val STRUCTURED_WORKBENCH_CONTENT_TAG = "structured_sheet_workbench_content"

@Composable
internal fun QuestWorkbenchPanel(
    sheet: ShellSheetState,
    fillAvailableHeight: Boolean = true,
    modifier: Modifier = Modifier,
) {
    StructuredSheetWorkbench(
        contextLines = sheet.panelStats.filterNot { it == "进行中" },
        contentTitle = "",
        fillContent = fillAvailableHeight,
        modifier = modifier,
    ) {
        Box(
            modifier =
                if (fillAvailableHeight) {
                    Modifier
                        .fillMaxWidth()
                        .heightIn(min = 48.dp)
                } else {
                    workbenchBodyModifier(fillAvailableHeight)
                }
                    .background(ShellPalette.Ink3.copy(alpha = 0.42f), UnifiedPopupControlShape)
                    .border(1.dp, ShellLines.ControlStateBorder, UnifiedPopupControlShape)
                    .padding(10.dp),
        ) {
            WorkbenchDetailList(
                detailLines = sheet.detailLines,
                emptyText = sheet.visibleEmptyText(),
                modifier =
                    if (fillAvailableHeight) {
                        Modifier.fillMaxWidth()
                    } else {
                        Modifier
                            .fillMaxWidth()
                            .heightIn(max = 240.dp)
                    },
            )
        }
    }
}

@Composable
internal fun MailDetailWorkbenchPanel(
    sheet: ShellSheetState,
    onItemClick: (ShellSheetItem) -> Unit,
    fillAvailableHeight: Boolean = true,
    modifier: Modifier = Modifier,
) {
    StructuredSheetWorkbench(
        contextLines = sheet.panelStats,
        contentTitle = "",
        fillContent = fillAvailableHeight,
        modifier = modifier,
    ) {
        Column(
            modifier = workbenchBodyModifier(fillAvailableHeight),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            LazyColumn(
                modifier =
                    Modifier
                        .fillMaxWidth()
                        .then(
                            if (fillAvailableHeight) {
                                Modifier.weight(1f, fill = true)
                            } else {
                                Modifier.heightIn(max = 240.dp)
                            },
                        )
                        .background(ShellPalette.Ink3.copy(alpha = 0.46f), UnifiedPopupControlShape)
                        .border(1.dp, ShellLines.ControlStateBorder, UnifiedPopupControlShape)
                        .padding(8.dp)
                        .testTag(DENSE_PANEL_DETAIL_LIST_TAG),
                verticalArrangement = Arrangement.spacedBy(6.dp),
                contentPadding = PaddingValues(bottom = 8.dp),
            ) {
                if (sheet.detailLines.isEmpty() && sheet.visibleEmptyText().isNotBlank()) {
                    item(key = "mail-empty") {
                        Text(
                            text = sheet.visibleEmptyText(),
                            color = ShellPalette.PaperMuted,
                            style = MaterialTheme.typography.bodyMedium,
                        )
                    }
                } else {
                    items(sheet.detailLines) { line ->
                        DetailLine(text = line)
                    }
                }
            }
            if (sheet.items.isNotEmpty()) {
                MailAttachmentList(
                    items = sheet.items,
                    onItemClick = onItemClick,
                    modifier = Modifier.fillMaxWidth(),
                )
            }
        }
    }
}

private fun workbenchBodyModifier(fillAvailableHeight: Boolean): Modifier {
    return if (fillAvailableHeight) {
        Modifier.fillMaxSize()
    } else {
        Modifier
            .fillMaxWidth()
            .heightIn(max = 240.dp)
            .wrapContentHeight()
    }
}

@Composable
internal fun StructuredSheetWorkbench(
    contextLines: List<String>,
    contentTitle: String,
    modifier: Modifier = Modifier,
    detailTitle: String = "说明",
    detailLines: List<String> = emptyList(),
    fillContent: Boolean = true,
    content: @Composable () -> Unit,
) {
    Column(
        modifier =
            modifier
                .fillMaxWidth()
                .testTag(STRUCTURED_WORKBENCH_TAG),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        WorkbenchContextRow(
            lines = contextLines,
            modifier = Modifier.fillMaxWidth(),
        )
        if (detailLines.isNotEmpty()) {
            WorkbenchDetailSection(
                title = detailTitle,
                detailLines = detailLines,
                modifier =
                    Modifier
                        .fillMaxWidth()
                        .heightIn(max = 132.dp),
            )
        }
        WorkbenchContentFrame(
            title = contentTitle,
            fillContent = fillContent,
            modifier =
                if (fillContent) {
                    Modifier
                        .weight(1f, fill = false)
                        .fillMaxWidth()
                } else {
                    Modifier.fillMaxWidth()
                },
            content = content,
        )
    }
}

@Composable
private fun WorkbenchContextRow(
    lines: List<String>,
    modifier: Modifier = Modifier,
) {
    if (lines.isEmpty()) {
        return
    }
    val scrollState = rememberScrollState()
    Row(
        modifier =
            modifier
                .testTag(STRUCTURED_WORKBENCH_CONTEXT_TAG)
                .horizontalScroll(scrollState),
        horizontalArrangement = Arrangement.spacedBy(6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        lines.forEachIndexed { index, line ->
            UnifiedPopupMetaText(text = line, accent = index == 0)
        }
    }
}

@Composable
private fun WorkbenchDetailSection(
    title: String,
    detailLines: List<String>,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier = modifier,
        verticalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        if (title.isNotBlank()) {
            Text(
                text = title,
                color = ShellPalette.PaperMuted,
                style = MaterialTheme.typography.labelLarge,
                fontWeight = FontWeight.SemiBold,
            )
        }
        LazyColumn(
            modifier = Modifier.fillMaxWidth(),
            verticalArrangement = Arrangement.spacedBy(6.dp),
            contentPadding = PaddingValues(bottom = 2.dp),
        ) {
            items(detailLines) { line ->
                DetailLine(text = line)
            }
        }
    }
}

@Composable
private fun WorkbenchContentFrame(
    title: String,
    fillContent: Boolean,
    modifier: Modifier = Modifier,
    content: @Composable () -> Unit,
) {
    Column(
        modifier =
            modifier
                .testTag(STRUCTURED_WORKBENCH_CONTENT_TAG),
        verticalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        if (title.isNotBlank()) {
            Text(
                text = title,
                color = ShellPalette.PaperMuted,
                style = MaterialTheme.typography.labelLarge,
                fontWeight = FontWeight.SemiBold,
            )
        }
        Box(
            modifier =
                if (fillContent) {
                    Modifier
                        .fillMaxWidth()
                        .weight(1f, fill = false)
                        .padding(bottom = 2.dp)
                } else {
                    Modifier
                        .fillMaxWidth()
                        .heightIn(min = 48.dp)
                        .padding(bottom = 2.dp)
                },
        ) {
            content()
        }
    }
}

@Composable
private fun MailAttachmentList(
    items: List<ShellSheetItem>,
    onItemClick: (ShellSheetItem) -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier =
            modifier
                .heightIn(max = 132.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        LazyColumn(
            modifier = Modifier.fillMaxWidth(),
            verticalArrangement = Arrangement.spacedBy(6.dp),
            contentPadding = PaddingValues(bottom = 2.dp),
        ) {
            items(items) { item ->
                SheetRow(
                    item = item.copy(subtitle = "", trailing = "", command = ""),
                    onClick = { onItemClick(item) },
                )
            }
        }
    }
}

@Composable
private fun WorkbenchDetailList(
    detailLines: List<String>,
    emptyText: String,
    modifier: Modifier = Modifier,
) {
    if (detailLines.isEmpty()) {
        Box(
            modifier = modifier,
            contentAlignment = Alignment.Center,
        ) {
            if (emptyText.isNotBlank()) {
                Text(
                    text = emptyText,
                    color = ShellPalette.PaperMuted,
                    style = MaterialTheme.typography.bodyMedium,
                )
            }
        }
    } else {
        LazyColumn(
            modifier = modifier.testTag(DENSE_PANEL_DETAIL_LIST_TAG),
            verticalArrangement = Arrangement.spacedBy(6.dp),
            contentPadding = PaddingValues(bottom = 14.dp),
        ) {
            items(detailLines) { line ->
                WorkbenchDetailLine(text = line)
            }
        }
    }
}

@Composable
private fun WorkbenchDetailLine(text: String) {
    val label =
        when {
            text.startsWith("目标：") -> "目标"
            text.startsWith("奖励：") -> "奖励"
            else -> ""
        }
    if (label.isBlank()) {
        Text(
            text = text,
            color = ShellPalette.PaperMuted,
            style = MaterialTheme.typography.bodyMedium,
            modifier =
                Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 4.dp, vertical = 4.dp),
        )
        return
    }
    Row(
        modifier =
            Modifier
                .fillMaxWidth()
                .padding(horizontal = 4.dp, vertical = 4.dp),
        horizontalArrangement = Arrangement.spacedBy(6.dp),
        verticalAlignment = Alignment.Top,
    ) {
        Text(
            text = "$label：",
            color = ShellPalette.PaperMuted,
            style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.SemiBold),
            modifier = Modifier.widthIn(min = 44.dp),
        )
        Text(
            text = text.substringAfter('：').trim(),
            color = ShellPalette.Paper,
            style = MaterialTheme.typography.bodyMedium,
            modifier = Modifier.weight(1f),
        )
    }
}

private fun ShellSheetState.visibleEmptyText(): String {
    return panelEmptyText.takeIf { emptyPresentation == ShellSheetEmptyPresentation.MESSAGE }.orEmpty()
}
