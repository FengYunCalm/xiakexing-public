package com.xiakexing.client.feature.shell

import com.xiakexing.client.core.protocol.payload.MapGridPacket

/**
 * MAPS 落地：出口/特殊出口/九宫格/sheet 更新。
 * 返回 (新状态, 出口是否变化)——出口变化才允许触发路线刷新。
 */
internal fun ShellUiState.withMapGridApplied(mapGrid: MapGridPacket): Pair<ShellUiState, Boolean> {
    val nextExits = exits.withQuestMarkersFromMap(mapGrid)
    val exitsChanged = nextExits !== exits
    val nextExitPartition = if (nextExits === exits) null else nextExits.partitionForMainShell()
    val nextMapSheet =
        if (
            sheet?.source == PopupSource.TITLE_ACTION &&
            sheet.resolvedSheetKind() == ShellSheetKind.MAP
        ) {
            if (mapGrid.isFullMapAtlas()) {
                buildMapSheetState(roomTitle, nextExits, mapGrid, trackedQuest)
            } else {
                sheet
            }
        } else {
            sheet
        }
    return copy(
        latestMapGrid = mapGrid,
        exits = nextExits,
        specialExits = nextExitPartition?.first ?: specialExits,
        gridExits = nextExitPartition?.second ?: gridExits,
        sheet = nextMapSheet,
    ) to exitsChanged
}
