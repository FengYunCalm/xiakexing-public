package com.xiakexing.client.feature.shell

import com.xiakexing.client.core.protocol.payload.ExitSnapshot
import com.xiakexing.client.core.protocol.payload.MapGridPacket

internal const val MAP_PANEL_TITLE = "地图"
internal const val FULL_MAP_GRID_SIZE = 41

internal fun buildMapSheetState(
    roomTitle: String,
    exits: List<ExitSnapshot>,
    map: MapGridPacket?,
    trackedQuest: TrackedQuestState = TrackedQuestState(),
    loading: Boolean = false,
): ShellSheetState {
    val resolvedTitle = map?.title?.takeIf { it.isNotBlank() } ?: roomTitle
    val detailLines =
        buildList {
            add(resolvedTitle)
            map?.zone?.takeIf { it.isNotBlank() }?.let { zone -> add("区域：$zone") }
            if (trackedQuest.active) {
                val targetName = trackedQuest.targetRoomName.ifBlank { trackedQuest.targetRoomId }
                add("追踪：${trackedQuest.questName} → $targetName")
                trackedQuest.routeError.takeIf { it.isNotBlank() }?.let { add("路线：$it") }
            }
        }
    return buildGridSheet(
        source = PopupSource.TITLE_ACTION,
        title = MAP_PANEL_TITLE,
        gridMode = ShellPanelMode.MAP_ATLAS,
        detailLines = detailLines,
        items =
            exits.map { exit ->
                ShellSheetItem(
                    title = exit.name,
                    trailing = exit.direction,
                    command = exit.direction,
                    dismissAfterInvoke = true,
                )
            },
        mapContent = buildMapPanelContent(map, trackedQuest),
        panelEmptyText =
            if (map?.rows.isNullOrEmpty()) {
                "当前房间暂无可查看的地图"
            } else {
                "当前地图未附带可交互出口"
            },
        loading = loading,
        kind = ShellSheetKind.MAP,
    )
}

private fun buildMapPanelContent(
    map: MapGridPacket?,
    trackedQuest: TrackedQuestState,
): ShellMapPanelContent {
    return ShellMapPanelContent(
        width = map?.width ?: 0,
        height = map?.height ?: 0,
        rows = map?.rows.orEmpty(),
        legend = map?.legend.orEmpty(),
        paths = map?.paths.orEmpty(),
        rooms = map?.rooms.orEmpty(),
        questMarkers = map?.questMarkers.orEmpty(),
        questMarkerDetails =
            map?.questMarkerDetails.orEmpty().map { detail ->
                ShellMapQuestMarkerDetail(
                    coord = detail.coord,
                    entryRoomId = detail.entryRoomId,
                    targetRoomId = detail.targetRoomId,
                    entryCommand = detail.entryCommand,
                    entryDirection = detail.entryDirection,
                    label = detail.label,
                    questIds = detail.questIds,
                )
            },
        trackedRooms = trackedQuest.trackedRouteRooms().toSet(),
    )
}

private fun TrackedQuestState.trackedRouteRooms(): List<String> {
    if (!active) {
        return emptyList()
    }
    return (routeRooms + targetRoomId)
        .map { roomId -> roomId.trim() }
        .filter { roomId -> roomId.isNotBlank() }
        .distinct()
}

internal fun ShellSheetState.mapPanelRows(): List<String> {
    return resolvedMapPanelContent().rows
}

internal fun ShellSheetState.mapPanelLegend(): List<String> {
    return resolvedMapPanelContent().legend
}

internal fun ShellSheetState.mapPanelPaths(): Map<String, List<String>> {
    return resolvedMapPanelContent().paths
}

internal fun ShellSheetState.mapPanelRooms(): Map<String, String> {
    return resolvedMapPanelContent().rooms
}

internal fun ShellSheetState.mapPanelQuestMarkers(): Set<String> {
    return resolvedMapPanelContent().questMarkers
}

internal fun ShellSheetState.mapPanelQuestMarkerDetails(): List<String> {
    return resolvedMapPanelContent().questMarkerDetails.map(::encodeLegacyQuestMarkerDetail)
}

internal fun ShellSheetState.mapPanelQuestMarkerCells(): Set<String> {
    return resolvedMapPanelContent().questMarkerCells
}

internal fun ShellSheetState.mapPanelTrackedRooms(): Set<String> {
    return resolvedMapPanelContent().trackedRooms
}

internal fun ShellSheetState.mapPanelSizeLabel(): String {
    if (mapContent == null && panelStats.none { stat -> stat.startsWith("size=") }) {
        return ""
    }
    val content = resolvedMapPanelContent()
    return "${content.width}×${content.height}"
}

internal fun ShellSheetState.resolvedMapPanelContent(): ShellMapPanelContent {
    return mapContent ?: decodeLegacyMapPanelContent(panelStats)
}

private fun decodeLegacyMapPanelContent(stats: List<String>): ShellMapPanelContent {
    val legacyFields =
        stats.filter { stat ->
            stat.startsWith("size=") ||
                stat.startsWith("row=") ||
                stat.startsWith("legend=") ||
                stat.startsWith("path=") ||
                stat.startsWith("room=") ||
                stat.startsWith("quest=") ||
                stat.startsWith("quest_detail=") ||
                stat.startsWith("track_room=")
        }
    if (legacyFields.isNotEmpty()) {
        LegacyCompatibilityDiagnostics.record("map.panel_stats")
    }
    var width = 0
    var height = 0
    val rows = mutableListOf<String>()
    val legend = mutableListOf<String>()
    val paths = linkedMapOf<String, List<String>>()
    val rooms = linkedMapOf<String, String>()
    val questMarkers = linkedSetOf<String>()
    val questMarkerDetails = mutableListOf<ShellMapQuestMarkerDetail>()
    val trackedRooms = linkedSetOf<String>()

    stats.forEach { stat ->
        when {
            stat.startsWith("size=") -> {
                val dimensions = stat.removePrefix("size=").split('x', limit = 2)
                width = dimensions.getOrNull(0)?.toIntOrNull() ?: 0
                height = dimensions.getOrNull(1)?.toIntOrNull() ?: 0
            }
            stat.startsWith("row=") -> rows += stat.removePrefix("row=")
            stat.startsWith("legend=") -> legend += stat.removePrefix("legend=")
            stat.startsWith("path=") -> {
                val parts = stat.removePrefix("path=").split("|", limit = 2)
                val key = parts.getOrNull(0).orEmpty().trim()
                val path =
                    parts.getOrNull(
                        1,
                    ).orEmpty().split(';').map(String::trim).filter(String::isNotBlank)
                if (key.isNotBlank() && path.isNotEmpty()) paths[key] = path
            }
            stat.startsWith("room=") -> {
                val parts = stat.removePrefix("room=").split("|", limit = 2)
                val key = parts.getOrNull(0).orEmpty().trim()
                val roomId = parts.getOrNull(1).orEmpty().trim()
                if (key.isNotBlank() && roomId.isNotBlank()) rooms[key] = roomId
            }
            stat.startsWith("quest=") ->
                stat.removePrefix("quest=").trim()
                    .takeIf(String::isNotBlank)?.let(questMarkers::add)
            stat.startsWith("quest_detail=") ->
                decodeLegacyQuestMarkerDetail(stat.removePrefix("quest_detail="))
                    ?.let(questMarkerDetails::add)
            stat.startsWith("track_room=") ->
                stat.removePrefix("track_room=").trim()
                    .takeIf(String::isNotBlank)?.let(trackedRooms::add)
        }
    }

    return ShellMapPanelContent(
        width = width,
        height = height,
        rows = rows,
        legend = legend,
        paths = paths,
        rooms = rooms,
        questMarkers = questMarkers,
        questMarkerDetails = questMarkerDetails,
        trackedRooms = trackedRooms,
    )
}

private fun encodeLegacyQuestMarkerDetail(detail: ShellMapQuestMarkerDetail): String {
    return listOf(
        detail.coord,
        detail.entryRoomId,
        detail.targetRoomId,
        detail.entryDirection,
        detail.entryCommand,
        detail.label,
        detail.questIds.joinToString(","),
    ).joinToString("|") { value -> value.replace("|", " ") }
}

private fun decodeLegacyQuestMarkerDetail(value: String): ShellMapQuestMarkerDetail? {
    val parts = value.split("|")
    val coord = parts.getOrNull(0).orEmpty().trim()
    if (coord.isBlank()) {
        return null
    }
    return ShellMapQuestMarkerDetail(
        coord = coord,
        entryRoomId = parts.getOrNull(1).orEmpty(),
        targetRoomId = parts.getOrNull(2).orEmpty(),
        entryDirection = parts.getOrNull(3).orEmpty(),
        entryCommand = parts.getOrNull(4).orEmpty(),
        label = parts.getOrNull(5).orEmpty(),
        questIds =
            parts.getOrNull(6).orEmpty()
                .split(',')
                .map(String::trim)
                .filter(String::isNotBlank),
    )
}

internal fun MapGridPacket.isFullMapAtlas(): Boolean {
    return width >= FULL_MAP_GRID_SIZE && height >= FULL_MAP_GRID_SIZE && rows.size >= FULL_MAP_GRID_SIZE
}

/** 投影 + 房间匹配（不含全图校验）：地图渲染与包处理共用同一真源。 */
internal fun MapGridPacket.matchesProjectionAndRoom(state: ShellUiState): Boolean {
    val projection = state.projectionRuntime.projection?.projection
    val projectionMatches =
        when {
            projection?.isV2 == true ->
                projectionId == projection.projectionId &&
                    projectionRevision == projection.projectionRevision
            projectionId.isBlank() -> true
            else -> projection != null && projectionId == projection.projectionId
        }
    val roomMatches =
        if (projection?.isV2 == true) {
            roomId.isNotBlank() && roomId == state.roomId
        } else {
            roomId.isBlank() || roomId == state.roomId
        }
    return projectionMatches && roomMatches
}

internal fun MapGridPacket.matchesMapContext(state: ShellUiState): Boolean {
    if (!isFullMapAtlas()) return false
    return matchesProjectionAndRoom(state)
}
