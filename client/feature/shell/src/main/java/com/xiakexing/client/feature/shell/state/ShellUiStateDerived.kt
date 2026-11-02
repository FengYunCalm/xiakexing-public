package com.xiakexing.client.feature.shell

import androidx.compose.runtime.Immutable
import com.xiakexing.client.core.protocol.payload.InlineAction

@Immutable
data class ShellEntrySceneState(
    val username: String,
    val password: String,
    val commandInput: String,
    val sessionPhase: ShellPhase,
    val sessionDetail: String,
    val clientVersion: String = "",
    val registerPrompt: String,
    val loginErrorMessage: String,
    val topNoticeTitle: String,
    val topNoticeLines: List<String>,
    val canConnect: Boolean,
    val canSend: Boolean,
    val commandLabel: String,
    val commandPlaceholder: String,
    val sendButtonText: String,
    val registerOptions: List<String>,
)

@Immutable
internal data class MainShellSceneState(
    val floatingMessages: List<FloatingMessageState>,
    val questGuideLaneState: QuestGuideLaneState,
    val roomId: String,
    val roomTitle: String,
    val roomZone: String,
    val roomRefreshSerial: Long,
    val interactionObjects: List<InteractionObject>,
    val selectedInteractionObjectId: String,
    val trackedQuest: TrackedQuestState,
    val visibleTranscript: List<TranscriptLine>,
    val transcriptScrollRequest: Int,
    val inlineActionChips: List<InlineAction>,
    val combatActive: Boolean,
    val combatOpponents: List<CombatOpponentState>,
    val combatSelfBusySeconds: Int,
    val combatSelfBusyCapturedAtMillis: Long,
    val combatActionCooldowns: Map<String, CombatActionCooldown>,
    val interactionSheet: ShellSheetState?,
    val exitOverlay: ExitOverlayState?,
    val specialExits: List<com.xiakexing.client.core.protocol.payload.ExitSnapshot>,
    val gridExits: List<com.xiakexing.client.core.protocol.payload.ExitSnapshot>,
    val chatMessages: List<ChatMessageItem>,
    val chatPanelOpen: Boolean,
    val unreadChatChannels: Set<ChatChannel>,
    val activeChatChannel: ChatChannel,
    val projectionRuntime: ProjectionRuntimeState,
    val reduceMotion: Boolean,
    val reconnectOverlay: Boolean,
    val reconnectTimedOut: Boolean,
)

internal fun ShellUiState.toEntrySceneState(): ShellEntrySceneState {
    return ShellEntrySceneState(
        username = username,
        password = password,
        commandInput = commandInput,
        sessionPhase = session.phase,
        sessionDetail = session.detail,
        clientVersion = clientVersion,
        registerPrompt = session.registerPrompt,
        loginErrorMessage = loginErrorMessage,
        topNoticeTitle = topNoticeTitle,
        topNoticeLines = topNoticeLines,
        canConnect = canConnect,
        canSend = canSend,
        commandLabel = commandLabel,
        commandPlaceholder = commandPlaceholder,
        sendButtonText = sendButtonText,
        registerOptions = registerOptions,
    )
}

internal fun ShellUiState.toMainShellSceneState(): MainShellSceneState {
    return MainShellSceneState(
        floatingMessages = floatingMessages,
        questGuideLaneState = trackedQuest.toQuestGuideLaneState(),
        roomId = roomId,
        roomTitle = roomTitle,
        roomZone = roomZone,
        roomRefreshSerial = roomRefreshSerial,
        interactionObjects = interactionObjects,
        selectedInteractionObjectId = selectedInteractionObjectId,
        trackedQuest = trackedQuest,
        visibleTranscript = visibleTranscript,
        transcriptScrollRequest = transcriptScrollRequest,
        inlineActionChips = inlineActionChips,
        combatActive = combatActive,
        combatOpponents =
            combatOpponents.ifEmpty {
                combatOpponent.takeIf { it.isNotBlank() }?.let {
                    listOf(CombatOpponentState(it, combatOpponentVitals))
                }.orEmpty()
            },
        combatSelfBusySeconds = combatSelfBusySeconds,
        combatSelfBusyCapturedAtMillis = combatSelfBusyCapturedAtMillis,
        combatActionCooldowns = combatActionCooldowns,
        interactionSheet = sheet,
        exitOverlay = exitOverlay,
        specialExits = specialExits,
        gridExits = gridExits,
        chatMessages = chatMessages,
        chatPanelOpen = chatPanelOpen,
        unreadChatChannels = unreadChatChannels,
        activeChatChannel = activeChatChannel,
        projectionRuntime = projectionRuntime,
        reduceMotion = reduceMotion,
        reconnectOverlay = reconnectOverlay,
        reconnectTimedOut = reconnectTimedOut,
    )
}
