package com.xiakexing.client.feature.shell

import com.xiakexing.client.core.protocol.payload.MapGridPacket
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

internal const val MAP_INITIAL_LOAD_TIMEOUT_MS = 6_500L
internal const val MAP_ACK_RETRY_DELAY_MS = 5_100L
internal const val MAP_RETRY_TIMEOUT_MS = 4_000L

internal class PanelCoordinator(
    private val scope: CoroutineScope,
    private val sink: Sink,
) {
    interface Sink {
        fun currentState(): ShellUiState

        fun latestMapGrid(): MapGridPacket?

        fun sendDirectCommand(
            command: String,
            suppressImmediateScroll: Boolean,
        )

        fun sendMapCommand()

        fun flushPendingRoomRefresh()

        fun clearPendingInteractionRequest()

        fun showCommonExitOverlay(buttons: List<ShellAction>)

        fun updateState(transform: ShellUiState.() -> ShellUiState)
    }

    private val panelNavigator = ShellPanelNavigator()
    private var mapLoadGeneration = 0L
    private var mapRetryCount = 0
    private var mapLoadJob: Job? = null

    fun showHelpPanel(categoryId: String = DEFAULT_HELP_PANEL_CATEGORY) {
        sink.clearPendingInteractionRequest()
        sink.updateState {
            copy(
                sheet = buildHelpSheetState(categoryId),
                dialog = null,
            )
        }
    }

    fun showHelpDetail(entryId: String) {
        val entry = helpEntriesById[entryId] ?: return
        sink.updateState {
            copy(
                sheet = entry.toHelpDetailSheet(),
                dialog = null,
            )
        }
    }

    fun showCharacterPanel() {
        sink.flushPendingRoomRefresh()
        sink.clearPendingInteractionRequest()
        val snapshot = sink.currentState().statusSnapshot
        sink.updateState { applyPanelNavigation(panelNavigator.character(snapshot)) }
        if (snapshot == null || snapshot.partial) {
            sink.sendDirectCommand("score xk", suppressImmediateScroll = true)
        }
    }

    fun showCharacterHubPanel() {
        sink.flushPendingRoomRefresh()
        sink.clearPendingInteractionRequest()
        sink.showCommonExitOverlay(characterHubOverlayButtons())
    }

    fun showAttributePanel() {
        sink.flushPendingRoomRefresh()
        sink.clearPendingInteractionRequest()
        val snapshot = sink.currentState().statusSnapshot
        sink.updateState { applyPanelNavigation(panelNavigator.attribute(snapshot)) }
        if (snapshot == null || snapshot.partial) {
            sink.sendDirectCommand("score xk", suppressImmediateScroll = true)
        }
    }

    fun showFunctionPanel() {
        sink.clearPendingInteractionRequest()
        sink.updateState { applyPanelNavigation(panelNavigator.function()) }
    }

    fun showDungeonPanel() {
        val current = sink.currentState()
        if (!current.hasDungeonEntry) {
            return
        }
        sink.clearPendingInteractionRequest()
        sink.updateState {
            copy(
                sheet = null,
                dialog = buildDungeonPopup(current.isInDungeonRoom),
                commandPalette = null,
                exitOverlay = null,
            )
        }
    }

    fun showSettingsPanel() {
        sink.clearPendingInteractionRequest()
        sink.updateState {
            applyPanelNavigation(panelNavigator.settings(reduceMotion))
        }
    }

    fun showMailPanel() {
        sink.clearPendingInteractionRequest()
        sink.updateState {
            copy(
                sheet = buildMailLoadingSheetState(),
                dialog = null,
                exitOverlay = null,
            )
        }
        sink.sendDirectCommand("mail", suppressImmediateScroll = true)
    }

    fun showMessagePanel() = showMailPanel()

    fun showActivityPanel() {
        sink.clearPendingInteractionRequest()
        sink.updateState {
            applyPanelNavigation(
                panelNavigator.activity(sink.currentState().objectiveHub),
            )
        }
    }

    fun showMapPanel() {
        sink.flushPendingRoomRefresh()
        sink.clearPendingInteractionRequest()
        val current = sink.currentState()
        val cachedMap = sink.latestMapGrid()?.takeIf { map -> map.matchesMapContext(current) }
        mapLoadGeneration += 1L
        mapRetryCount = 0
        mapLoadJob?.cancel()
        sink.updateState {
            applyPanelNavigation(
                panelNavigator.map(
                    roomTitle = current.roomTitle,
                    exits = current.exits,
                    mapGrid = cachedMap,
                    trackedQuest = current.trackedQuest,
                    loading = cachedMap == null,
                ),
            )
        }
        if (cachedMap == null) {
            sink.sendMapCommand()
            scheduleMapFailure(mapLoadGeneration, MAP_INITIAL_LOAD_TIMEOUT_MS)
        }
    }

    fun onMapGridAccepted(map: MapGridPacket) {
        val state = sink.currentState()
        if (!map.matchesMapContext(state)) return
        mapLoadJob?.cancel()
        mapLoadJob = null
    }

    fun onMapAlreadyLatest(): Boolean {
        val state = sink.currentState()
        val currentSheet = state.sheet
        if (currentSheet?.resolvedSheetKind() != ShellSheetKind.MAP || !currentSheet.loading) return false
        if (sink.latestMapGrid()?.matchesMapContext(state) == true) return true
        val generation = mapLoadGeneration
        if (mapRetryCount > 0) return true
        mapRetryCount = 1
        mapLoadJob?.cancel()
        mapLoadJob =
            scope.launch {
                delay(MAP_ACK_RETRY_DELAY_MS)
                if (!isCurrentLoadingMap(generation)) return@launch
                sink.sendMapCommand()
                scheduleMapFailure(generation, MAP_RETRY_TIMEOUT_MS)
            }
        return true
    }

    private fun scheduleMapFailure(
        generation: Long,
        timeoutMs: Long,
    ) {
        mapLoadJob?.cancel()
        mapLoadJob =
            scope.launch {
                delay(timeoutMs)
                if (!isCurrentLoadingMap(generation)) return@launch
                sink.updateState {
                    val currentSheet = sheet ?: return@updateState this
                    copy(
                        sheet =
                            currentSheet.copy(
                                loading = false,
                                panelEmptyText = "地图暂时无法加载",
                                errorMessage = "地图暂时无法加载，请稍后重试",
                                retryAction = ShellAction("重试", OPEN_WORLD_MAP_COMMAND, dismissAfterInvoke = false),
                            ),
                    )
                }
            }
    }

    private fun isCurrentLoadingMap(generation: Long): Boolean {
        val state = sink.currentState()
        val sheet = state.sheet
        return generation == mapLoadGeneration && sheet?.resolvedSheetKind() == ShellSheetKind.MAP &&
            sheet.loading
    }

    fun showWorldHubPanel() {
        sink.flushPendingRoomRefresh()
        sink.clearPendingInteractionRequest()
        sink.showCommonExitOverlay(buildWorldHubOverlayButtons())
    }

    private fun characterHubOverlayButtons(): List<ShellAction> {
        return listOf(
            ShellAction("状态", OPEN_CHARACTER_STATUS_COMMAND, dismissAfterInvoke = false),
            ShellAction("背包", OPEN_INVENTORY_PANEL_COMMAND, dismissAfterInvoke = false),
            ShellAction("装备", OPEN_EQUIPMENT_PANEL_COMMAND, dismissAfterInvoke = false),
            ShellAction("武学", "skills", dismissAfterInvoke = false),
            ShellAction("属性", OPEN_CHARACTER_ATTRIBUTES_COMMAND, dismissAfterInvoke = false),
        )
    }
}
