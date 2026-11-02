package com.xiakexing.client.feature.shell

import com.google.gson.JsonObject
import com.xiakexing.client.core.protocol.payload.MapGridPacket
import com.xiakexing.client.core.protocol.payload.QuickActionGroupPacket

internal fun RoomProjectionCoordinator.handlePrimaryQuickActionGroupPacket(payload: JsonObject) {
    primaryQuickActionGroup = QuickActionGroupPacket.fromJson(payload, fallbackTitle = "第一组")
}

internal fun RoomProjectionCoordinator.handleSecondaryQuickActionGroupPacket(payload: JsonObject) {
    secondaryQuickActionGroup = QuickActionGroupPacket.fromJson(payload, fallbackTitle = "第二组")
}

internal fun RoomProjectionCoordinator.handleDynamicButtonPacket(payload: JsonObject) {
    dynamicButtonGroup = QuickActionGroupPacket.fromJson(payload, fallbackTitle = "动态按钮")
}

internal fun RoomProjectionCoordinator.handleMapGridPacket(payload: JsonObject) {
    val mapGrid = MapGridPacket.fromJson(payload)
    if (!mapGrid.matchesProjectionAndRoom(sink.currentState())) {
        return
    }
    val previousMap = sink.currentState().latestMapGrid
    if (previousMap != null &&
        previousMap.projectionId == mapGrid.projectionId &&
        previousMap.projectionRevision == mapGrid.projectionRevision &&
        previousMap.roomId == mapGrid.roomId &&
        previousMap.mapRevision > 0L && mapGrid.mapRevision > 0L &&
        mapGrid.mapRevision <= previousMap.mapRevision
    ) {
        return
    }
    var exitsChanged = false
    sink.updateState {
        val (next, changed) = withMapGridApplied(mapGrid)
        exitsChanged = changed
        next
    }
    if (exitsChanged) {
        sink.refreshTrackedQuestRoute()
    }
    sink.onMapGridAccepted(mapGrid)
}
