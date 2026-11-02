package com.xiakexing.client.feature.shell

import com.xiakexing.client.core.protocol.payload.ExitSnapshot
import com.xiakexing.client.core.protocol.payload.MapGridPacket
import com.xiakexing.client.core.protocol.payload.PlayerStatusSnapshot

internal data class RoomRefreshApplyResult(
    val state: ShellUiState,
    val enteredMainShell: Boolean,
    val roomChanged: Boolean,
)

internal fun ShellUiState.clearRoomProjectionForProjectionSwitch(): ShellUiState {
    return copy(
        roomId = "",
        roomTitle = "",
        roomZone = "",
        roomRefreshSerial = roomRefreshSerial + 1L,
        exits = emptyList(),
        specialExits = emptyList(),
        gridExits = emptyList(),
        npcs = emptyList(),
        items = emptyList(),
        inventoryItems = emptyList(),
        inventoryRefreshPending = false,
        statusSnapshot = null,
        interactionObjects = emptyList(),
        visibleTranscript = emptyList(),
        combatActive = false,
        combatOpponent = "",
        combatOpponentVitals = CombatOpponentVitals(),
        combatOpponents = emptyList(),
        combatStatusChips = emptyList(),
        combatSelfBuffs = emptyList(),
        combatSelfBusySeconds = 0,
        combatSelfBusyCapturedAtMillis = 0L,
        combatActionCooldowns = emptyMap(),
        activeSkillSlots = emptyMap(),
        commandInput = "",
        dialog = null,
        commandPalette = null,
        exitOverlay = null,
        trackedQuest = TrackedQuestState(),
        objectiveHub = objectiveHub.clearProjectionBoundState(),
        floatingMessages =
            floatingMessages.filterNot { message ->
                message.level == "quest"
            },
        session = ShellSessionReducer.reduce(session, ShellEvent.WorldPacketsCleared),
    ).withQuestSheet(null)
}

internal fun ShellUiState.canApplyRoomStatusImmediately(): Boolean {
    return appScene == ShellScene.MAIN_SHELL
}

internal fun PendingRoomRefresh.applyToShellState(
    currentState: ShellUiState,
    currentMapGrid: MapGridPacket?,
): RoomRefreshApplyResult {
    val authoritativeRoomId =
        roomId?.takeIf { it.isNotBlank() }
            ?: snapshotRoomId?.takeIf { it.isNotBlank() }
    val isInitialRoom =
        currentState.roomId.isBlank() &&
            currentState.roomTitle.isBlank() &&
            (authoritativeRoomId != null || roomTitle != null || exits != null)
    val isRoomSwitch =
        when {
            authoritativeRoomId != null && currentState.roomId.isNotBlank() ->
                authoritativeRoomId != currentState.roomId
            roomTitle != null && currentState.roomTitle.isNotBlank() ->
                roomTitle != currentState.roomTitle
            else -> false
        }
    val authoritativeEntities = isAuthoritativeEntitySnapshot() && !snapshotInconsistent
    /*
     * 实体替换只发生在完整/空壳快照：空集合是权威空集合。
     * 导航快照（含旧服务器无 snapshot 元数据的 TITL/EXIT）必须保留当前实体，
     * 否则“移动后返回原房间”会被导航子包误清空。
     */
    val nextNpcs =
        when {
            snapshotInconsistent -> currentState.npcs
            authoritativeEntities -> npcs ?: emptyList()
            else -> npcs ?: currentState.npcs
        }
    val nextItems =
        when {
            snapshotInconsistent -> currentState.items
            authoritativeEntities -> items ?: emptyList()
            else -> items ?: currentState.items
        }
    val nextInventoryItems = inventoryItems ?: currentState.inventoryItems
    val nextInventorySummary = inventorySummary ?: currentState.inventorySummary
    var nextExits = exits ?: currentState.exits
    var nextExitPartition = exits?.partitionForMainShell()
    val nextInteractionObjects =
        if (authoritativeEntities || npcs != null || items != null || isRoomSwitch) {
            buildInteractionObjects(nextNpcs, nextItems)
        } else {
            currentState.interactionObjects
        }

    var nextSheet = currentState.sheet
    var nextDialog = currentState.dialog
    if (inventoryItems != null) {
        nextSheet = nextSheet.refreshInventorySnapshot(nextInventoryItems, nextInventorySummary)
    }
    if (!isRoomSwitch && !snapshotInconsistent && nextSheet?.source == PopupSource.INTERACTION_OBJECT &&
        (
            authoritativeEntities ||
                (nextSheet.interactionTargetKind == InteractionObjectKind.NPC && npcs != null) ||
                (nextSheet.interactionTargetKind != InteractionObjectKind.NPC && items != null)
        )
    ) {
        val targetId = nextSheet.interactionTargetId
        val targetKey = nextSheet.interactionTargetUiKey.ifBlank { targetId }
        val target = nextInteractionObjects.findInteractionObjectByKey(targetKey)
        if (target != null) {
            nextSheet =
                nextSheet.refreshInteractionTarget(
                    target,
                    currentState.copy(inventoryItems = nextInventoryItems).hasGiveableInventoryItems(),
                )
        }
    }

    val effectiveStatusSnapshot = statusSnapshot?.mergeWithPrevious(currentState.statusSnapshot)

    if (effectiveStatusSnapshot != null) {
        val currentSheet = nextSheet
        nextSheet =
            when (currentSheet?.resolvedSheetKind()) {
                ShellSheetKind.CHARACTER -> buildCharacterSheetState(effectiveStatusSnapshot)
                ShellSheetKind.ATTRIBUTE -> buildAttributeSheetState(effectiveStatusSnapshot)
                else -> currentSheet
            }
    }

    if (shouldCloseInteractionSheetForRoomRefresh(
            nextSheet,
            nextInteractionObjects,
            isRoomSwitch,
        )
    ) {
        nextSheet = null
    }
    // 房间切换时打开中的地图 sheet 关闭（旧房间地图已失效；
    // 重开面板走 map xk 重建，避免停留 loading 或展示陈旧图）。
    if (isRoomSwitch && nextSheet?.resolvedSheetKind() == ShellSheetKind.MAP) {
        nextSheet = null
    }
    if (shouldCloseInteractionDialogForRoomRefresh(
            nextDialog,
            currentState.sheet,
            nextInteractionObjects,
            isRoomSwitch,
        )
    ) {
        nextDialog = null
    }

    var nextSession = currentState.session
    receivedWorldPackets.forEach { packet ->
        nextSession = ShellSessionReducer.reduce(nextSession, ShellEvent.WorldPacketReceived(packet))
    }

    val nextStatusSnapshot = effectiveStatusSnapshot ?: currentState.statusSnapshot
    val hasRoomSnapshot = roomTitle != null || exits != null || currentState.roomTitle.isNotBlank() || currentState.exits.isNotEmpty()
    val enteringMainShell = currentState.appScene != ShellScene.MAIN_SHELL && hasRoomSnapshot && nextStatusSnapshot != null
    val shouldRefreshRoomGrid =
        isInitialRoom || isRoomSwitch ||
            (exits != null && nextExits != currentState.exits)

    return RoomRefreshApplyResult(
        state =
            currentState.copy(
                appScene = if (enteringMainShell) ShellScene.MAIN_SHELL else currentState.appScene,
                roomId = authoritativeRoomId ?: currentState.roomId,
                roomTitle = roomTitle ?: currentState.roomTitle,
                roomZone = roomZone ?: currentState.roomZone,
                // 仅 TITL 携带副本上下文；单独的 EXIT/STAT 增量不得擦除当前房间上下文。
                instanceKind =
                    if (WorldPacket.TITLE in receivedWorldPackets) {
                        instanceKind ?: ""
                    } else {
                        currentState.instanceKind
                    },
                dungeonType =
                    if (WorldPacket.TITLE in receivedWorldPackets) {
                        dungeonType ?: ""
                    } else {
                        currentState.dungeonType
                    },
                dungeonGate =
                    if (WorldPacket.TITLE in receivedWorldPackets) {
                        dungeonGate ?: ""
                    } else {
                        currentState.dungeonGate
                    },
                roomRefreshSerial =
                    if (shouldRefreshRoomGrid) {
                        currentState.roomRefreshSerial + 1L
                    } else {
                        currentState.roomRefreshSerial
                    },
                exits = nextExits,
                specialExits = nextExitPartition?.first ?: currentState.specialExits,
                gridExits = nextExitPartition?.second ?: currentState.gridExits,
                npcs = nextNpcs,
                items = nextItems,
                inventoryItems = nextInventoryItems,
                inventorySummary = nextInventorySummary,
                inventoryRefreshPending = if (inventoryItems != null) false else currentState.inventoryRefreshPending,
                statusSnapshot = nextStatusSnapshot,
                interactionObjects = nextInteractionObjects,
                sheet = nextSheet,
                dialog = nextDialog,
                visibleTranscript = if (enteringMainShell) emptyList() else currentState.visibleTranscript,
                topNoticeLines = if (enteringMainShell) emptyList() else currentState.topNoticeLines,
                loginErrorMessage = if (WorldPacket.TITLE in receivedWorldPackets) "" else currentState.loginErrorMessage,
                session = nextSession,
                latestMapGrid = if (isRoomSwitch) null else currentMapGrid,
            ),
        enteredMainShell = enteringMainShell,
        roomChanged = isRoomSwitch,
    )
}

internal fun ShellUiState.applyStandaloneStatusSnapshot(
    snapshot: PlayerStatusSnapshot,
): ShellUiState {
    val nextSnapshot = snapshot.mergeWithPrevious(statusSnapshot)
    val nextSheet =
        when (sheet?.resolvedSheetKind()) {
            ShellSheetKind.CHARACTER -> buildCharacterSheetState(nextSnapshot)
            ShellSheetKind.ATTRIBUTE -> buildAttributeSheetState(nextSnapshot)
            else -> sheet
        }
    return if (statusSnapshot == nextSnapshot && sheet === nextSheet) {
        this
    } else {
        copy(statusSnapshot = nextSnapshot, sheet = nextSheet)
    }
}

/* 出口标记源属主序：八方位以坐标派生为准（地图缺席即清除）；
 * 非八邻域方向（enter/up 等）坐标层无法表达，以服务端出口级
 * quest_available 为准。第六轮真机反馈落点。 */
internal fun List<ExitSnapshot>.withQuestMarkersFromMap(map: MapGridPacket): List<ExitSnapshot> {
    if (isEmpty()) {
        return this
    }
    val questIdsByDirection = map.questIdsByVisibleExitDirection()
    val authoritativeSnapshot = map.projectionId.isNotBlank() && map.roomId.isNotBlank()
    if (questIdsByDirection.isEmpty() && !authoritativeSnapshot) {
        return this
    }
    var changed = false
    val exits =
        mapNotNull { exit ->
            val direction = exit.direction.trim().lowercase()
            val markerPresent = direction.allowsQuestExitMarker() && questIdsByDirection.containsKey(direction)
            val questIds = questIdsByDirection[direction].orEmpty()
            if (authoritativeSnapshot) {
            /* 第六轮真机反馈：服务端 exits[] 已带出口级 quest_available/available_quest_ids
             * （如 enter 出口的室内入口 marker），坐标派生的 markerPresent 只覆盖八方位；
             * 并集救援仅作用于坐标层结构性无法表达的方向（enter/up 等非八邻域）——
             * 八方位维持「地图缺席即清除」，防陈旧标记在整个驻留期无法被地图层清除。 */
                val gridDerivable = direction in GRID_DERIVABLE_DIRECTIONS
                val effectivePresent =
                    markerPresent ||
                        (!gridDerivable && exit.questAvailable)
                // ids 跟随 effectivePresent 门控：不亮标就不挂 ids，防「标记灭但数据残留」。
                val effectiveQuestIds =
                    if (!effectivePresent) {
                        emptyList()
                    } else if (questIds.isNotEmpty()) {
                        questIds
                    } else {
                        exit.availableQuestIds
                    }
                if (exit.questMarkerOnly && !effectivePresent) {
                    changed = true
                    return@mapNotNull null
                }
                if (exit.questAvailable == effectivePresent && exit.availableQuestIds == effectiveQuestIds) {
                    return@mapNotNull exit
                }
                changed = true
                return@mapNotNull exit.copy(
                    questAvailable = effectivePresent,
                    availableQuestIds = effectiveQuestIds,
                )
            }
            if (!markerPresent || exit.questAvailable) {
                return@mapNotNull exit
            }
            changed = true
            exit.copy(
                questAvailable = true,
                availableQuestIds = questIds.ifEmpty { exit.availableQuestIds },
            )
        }
    return if (changed) exits else this
}

internal fun String.allowsQuestExitMarker(): Boolean {
    return trim().lowercase() != "out"
}

/** 八邻域方向：坐标派生层可表达，地图缺席即清除；enter/up 等不在内。 */
private val GRID_DERIVABLE_DIRECTIONS =
    setOf(
        "north",
        "south",
        "east",
        "west",
        "northeast",
        "northwest",
        "southeast",
        "southwest",
    )

private fun MapGridPacket.questIdsByVisibleExitDirection(): Map<String, List<String>> {
    val markers = linkedMapOf<String, LinkedHashSet<String>>()
    questMarkers.forEach { coord ->
        coord.toGridDirectionOrNull()?.let {
                direction ->
            markers.getOrPut(direction) { linkedSetOf() }
        }
    }
    questMarkerDetails.forEach { detail ->
        detail.coord.toGridDirectionOrNull()?.let { direction ->
            markers.getOrPut(direction) { linkedSetOf() }.addAll(detail.questIds)
        }
        detail.entryDirection.trim().lowercase().takeIf { it.isNotBlank() }?.let { direction ->
            markers.getOrPut(direction) { linkedSetOf() }.addAll(detail.questIds)
        }
    }
    return markers.mapValues { (_, questIds) -> questIds.toList() }
}

private fun String.toGridDirectionOrNull(): String? {
    val parts = split(',', limit = 2).map { it.trim().toIntOrNull() }
    val x = parts.getOrNull(0) ?: return null
    val y = parts.getOrNull(1) ?: return null
    return when (x to y) {
        -1 to -1 -> "northwest"
        0 to -1 -> "north"
        1 to -1 -> "northeast"
        -1 to 0 -> "west"
        1 to 0 -> "east"
        -1 to 1 -> "southwest"
        0 to 1 -> "south"
        1 to 1 -> "southeast"
        else -> null
    }
}

internal fun PlayerStatusSnapshot.mergeWithPrevious(
    previous: PlayerStatusSnapshot?,
): PlayerStatusSnapshot {
    if (!partial || previous == null) {
        return this
    }

    return previous.copy(
        name = name.ifBlank { previous.name },
        hp = hp,
        effHp = previous.effHp.coerceAtLeast(hp),
        mp = mp,
        effMp = previous.effMp.coerceAtLeast(mp),
        ep = ep,
        neili = neili,
        maxNeili = maxNeili.takeIf { it > 0L } ?: previous.maxNeili,
        potential = potential ?: previous.potential,
        strength = strength.takeIf { it > 0L } ?: previous.strength,
        dexterity = dexterity.takeIf { it > 0L } ?: previous.dexterity,
        constitution = constitution.takeIf { it > 0L } ?: previous.constitution,
        intelligence = intelligence.takeIf { it > 0L } ?: previous.intelligence,
        partial = previous.partial,
    )
}
