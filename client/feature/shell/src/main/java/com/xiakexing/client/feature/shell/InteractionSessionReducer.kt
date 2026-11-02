package com.xiakexing.client.feature.shell

import com.xiakexing.client.core.network.CommandRequestIdentity
import com.xiakexing.client.core.protocol.payload.DialogPacket
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlin.coroutines.EmptyCoroutineContext

internal const val COMBAT_START_TIMEOUT_MS = 4_000L
internal const val INTERACTION_SETTLE_MS = 48L
private const val CLOSED_COMMAND_REQUEST_TOMBSTONE_CAPACITY = 32

/*
 * InteractionSession 域纯逻辑层——目标解析、会话保留判定与
 * capture 状态（pending 请求 + 行收集 + 调度 Job）。编排在
 * InteractionSessionOrchestration.kt，不持有 sink（scope 由构造注入）。
 */

internal fun resolveInteractionTarget(
    pending: PendingInteractionRequest?,
    currentSheet: ShellSheetState?,
    interactionObjects: List<InteractionObject>,
): InteractionObject? {
    pending?.target?.let { target ->
        return interactionObjects.findInteractionObjectByKey(target.uiKey.ifBlank { target.id }) ?: target
    }
    if (currentSheet?.source != PopupSource.INTERACTION_OBJECT) {
        return null
    }

    val targetId = currentSheet.interactionTargetId.takeIf { it.isNotBlank() } ?: return null
    val targetKey = currentSheet.interactionTargetUiKey.takeIf { it.isNotBlank() } ?: targetId
    return interactionObjects.findInteractionObjectByKey(targetKey)
        ?: InteractionObject(
            id = targetId,
            uiKey = currentSheet.interactionTargetUiKey.ifBlank { targetId },
            title = currentSheet.title,
            subtitle = currentSheet.subtitle,
            summary = currentSheet.detailLines.firstOrNull().orEmpty(),
            kind = currentSheet.interactionTargetKind ?: InteractionObjectKind.SCENE,
        )
}

internal fun List<InteractionObject>.findInteractionObjectByKey(key: String): InteractionObject? {
    val normalizedKey = key.trim()
    if (normalizedKey.isBlank()) {
        return null
    }
    return firstOrNull { target -> target.uiKey == normalizedKey }
        ?: firstOrNull { target -> target.id == normalizedKey }
}

/** 第六轮真机反馈：冷到达 provider 面板帧的目标解析——优先按 speaker_id
 * 匹配可见 NPC，否则按包身份构造（服务端 present_interaction 恒填 speaker_id）。 */
internal fun DialogPacket.resolveColdArrivalTarget(
    interactionObjects: List<InteractionObject>,
): InteractionObject {
    return interactionObjects.findInteractionObjectByKey(speakerId)
        ?: InteractionObject(
            id = speakerId.ifBlank { providerId },
            title = title.ifBlank { "任务" },
            subtitle = "人物",
            summary = "",
            kind = InteractionObjectKind.NPC,
        )
}

internal fun shouldStayInInteractionSheet(
    pending: PendingInteractionRequest?,
    currentSheet: ShellSheetState?,
): Boolean {
    if (currentSheet?.source == PopupSource.INTERACTION_OBJECT || currentSheet?.source == PopupSource.INTERACTION_LEARN) {
        return true
    }
    return pending != null && !pending.keepSheetClosedOnResponse
}

private val roomRefreshClosableInteractionSources =
    setOf(
        PopupSource.INTERACTION_OBJECT,
        PopupSource.INTERACTION_GIVE,
        PopupSource.INTERACTION_LEARN,
    )

internal fun shouldCloseInteractionSheetForRoomRefresh(
    currentSheet: ShellSheetState?,
    nextInteractionObjects: List<InteractionObject>,
    isRoomSwitch: Boolean,
): Boolean {
    if (currentSheet == null) {
        return false
    }
    if (currentSheet.source !in roomRefreshClosableInteractionSources) {
        return false
    }
    if (isRoomSwitch) {
        return true
    }
    val targetId = currentSheet.interactionTargetId.takeIf { it.isNotBlank() } ?: return false
    val targetKey = currentSheet.interactionTargetUiKey.takeIf { it.isNotBlank() } ?: targetId
    return nextInteractionObjects.findInteractionObjectByKey(targetKey) == null
}

internal fun shouldClearInteractionPendingForRoomRefresh(
    pending: PendingInteractionRequest?,
    nextInteractionObjects: List<InteractionObject>,
    isRoomSwitch: Boolean,
): Boolean {
    val target = pending?.target ?: return false
    if (isRoomSwitch) {
        return true
    }
    return nextInteractionObjects.findInteractionObjectByKey(target.uiKey) == null
}

internal fun shouldCloseInteractionDialogForRoomRefresh(
    currentDialog: ShellPopupState?,
    currentSheet: ShellSheetState?,
    nextInteractionObjects: List<InteractionObject>,
    isRoomSwitch: Boolean,
): Boolean {
    if (currentDialog == null) {
        return false
    }
    if (currentDialog.source !in roomRefreshClosableInteractionSources) {
        return false
    }
    if (isRoomSwitch) {
        return true
    }
    return shouldCloseInteractionSheetForRoomRefresh(
        currentSheet = currentSheet,
        nextInteractionObjects = nextInteractionObjects,
        isRoomSwitch = false,
    )
}

// ===== capture 状态（原 InteractionCaptureCoordinator 类） =====

/**
 * InteractionSession 域会话状态——pending 请求（interaction/sheet/popup）与
 * 行收集逻辑。Coordinator 持有本状态，经方法读写；不持有 sink/scope。
 */
internal class InteractionCaptureState(
    private val scope: CoroutineScope = CoroutineScope(EmptyCoroutineContext),
    private var onCombatStartTimeout: () -> Unit = {},
    private var applyInteractionSettled: (PendingInteractionRequest) -> Unit = {},
    private var applyProtocolSheetSettled: (PendingProtocolSheetRequest) -> Unit = {},
    private var applyProtocolPopupSettled: (PendingProtocolPopupRequest) -> Unit = {},
) {
    private var interactionRequestJob: Job? = null
    private var combatStartTimeoutJob: Job? = null
    private var protocolSheetRequestJob: Job? = null
    private var protocolPopupRequestJob: Job? = null
    private var interactionRequestEpoch = 0L
    private var protocolSheetRequestEpoch = 0L
    private var protocolPopupRequestEpoch = 0L
    private var npcQuestRequestGeneration = 0L
    private var settledNpcQuestRequest: SettledNpcQuestRequest? = null

    /** 无可见 capture 的直达命令也必须保留写入前冻结的回复身份。 */
    private var pendingOrdinaryCommandRequest: CommandRequestIdentity? = null

    /** 已关闭的请求 ID 只保留为短期 tombstone，阻止晚到的同一响应重新打开旧会话。 */
    private val closedCommandRequestIds = linkedSetOf<String>()

    var pendingInteractionRequest: PendingInteractionRequest? = null
        private set

    var pendingProtocolSheetRequest: PendingProtocolSheetRequest? = null
        private set

    var pendingProtocolPopupRequest: PendingProtocolPopupRequest? = null
        private set

    fun replacePendingInteractionRequest(request: PendingInteractionRequest?) {
        rememberPendingCommandRequestIds()
        interactionRequestEpoch += 1L
        interactionRequestJob?.cancel()
        interactionRequestJob = null
        combatStartTimeoutJob?.cancel()
        combatStartTimeoutJob = null
        pendingInteractionRequest =
            if (request?.isNpcQuestRequest() == true) {
                npcQuestRequestGeneration += 1L
                settledNpcQuestRequest = null
                request.copy(requestGeneration = npcQuestRequestGeneration)
            } else {
                request
            }
        if (request?.isCombatStartRequest() == true) {
            val requestEpoch = interactionRequestEpoch
            combatStartTimeoutJob =
                scope.launch {
                    delay(COMBAT_START_TIMEOUT_MS)
                    if (interactionRequestEpoch != requestEpoch) return@launch
                    if (pendingInteractionRequest?.isCombatStartRequest() == true) {
                        clearPendingInteractionRequest()
                        onCombatStartTimeout()
                    }
                }
        }
    }

    fun clearPendingInteractionRequest() {
        rememberPendingCommandRequestIds()
        interactionRequestEpoch += 1L
        pendingInteractionRequest = null
        interactionRequestJob?.cancel()
        interactionRequestJob = null
        combatStartTimeoutJob?.cancel()
        combatStartTimeoutJob = null
        clearPendingProtocolSheetRequest()
        clearPendingProtocolPopupRequest()
    }

    /** 第六轮真机纠偏：quest_hint 飘窗应答到达时结算等待中的 NPC 任务查询；
     * 非 quest 类 pending（其它交互动作）不被误清。 */
    fun settleIfAwaitingQuestAnswer() {
        if (pendingInteractionRequest?.isNpcQuestRequest() == true) {
            rememberCommandRequestId(pendingInteractionRequest?.commandRequestId)
            interactionRequestEpoch += 1L
            pendingInteractionRequest = null
            interactionRequestJob?.cancel()
            interactionRequestJob = null
            combatStartTimeoutJob?.cancel()
            combatStartTimeoutJob = null
        }
    }

    fun clearPendingProtocolSheetRequest() {
        rememberCommandRequestId(pendingProtocolSheetRequest?.commandRequestId)
        protocolSheetRequestEpoch += 1L
        pendingProtocolSheetRequest = null
        protocolSheetRequestJob?.cancel()
        protocolSheetRequestJob = null
    }

    fun clearPendingProtocolPopupRequest() {
        rememberCommandRequestId(pendingProtocolPopupRequest?.commandRequestId)
        protocolPopupRequestEpoch += 1L
        pendingProtocolPopupRequest = null
        protocolPopupRequestJob?.cancel()
        protocolPopupRequestJob = null
    }

    /** Binds the transport identity after capture preparation and before the write. */
    fun bindPendingCommandRequest(
        request: CommandRequestIdentity,
        command: String,
    ) {
        pendingInteractionRequest = pendingInteractionRequest?.takeIf {
            it.command.trim() == command.trim()
        }?.copy(
            commandRequestId = request.id,
            commandRequestGeneration = request.generation,
        ) ?: pendingInteractionRequest
        pendingProtocolSheetRequest = pendingProtocolSheetRequest?.takeIf {
            it.command.trim() == command.trim()
        }?.copy(
            commandRequestId = request.id,
            commandRequestGeneration = request.generation,
        ) ?: pendingProtocolSheetRequest
        pendingProtocolPopupRequest =
            pendingProtocolPopupRequest?.takeIf {
                it.command.isBlank() || it.command.trim() == command.trim()
            }?.copy(
                commandRequestId = request.id,
                commandRequestGeneration = request.generation,
            )
        if (hasPendingCapture()) {
            rememberCommandRequestId(pendingOrdinaryCommandRequest?.id)
            pendingOrdinaryCommandRequest = null
        } else {
            pendingOrdinaryCommandRequest = request
        }
    }

    /** Rolls back only the capture prepared for this rejected transport write. */
    fun rollbackPendingCommandRequest(request: CommandRequestIdentity) {
        if (pendingInteractionRequest?.commandRequestId == request.id) {
            rememberCommandRequestId(request.id)
            pendingInteractionRequest = null
            interactionRequestJob?.cancel()
            interactionRequestJob = null
            combatStartTimeoutJob?.cancel()
            combatStartTimeoutJob = null
        }
        if (pendingProtocolSheetRequest?.commandRequestId == request.id) {
            clearPendingProtocolSheetRequest()
        }
        if (pendingProtocolPopupRequest?.commandRequestId == request.id) {
            clearPendingProtocolPopupRequest()
        }
        if (pendingOrdinaryCommandRequest?.id == request.id) {
            pendingOrdinaryCommandRequest = null
        }
    }

    /** A tagged response may settle capture only when it belongs to the pending write. */
    fun acceptsCommandResponseForPending(
        commandResponseId: String,
        connectionGeneration: Long? = null,
    ): Boolean {
        if (commandResponseId.isBlank()) {
            // A capture prepared before the transport identity is bound (including
            // legacy/test-only preparation) still uses the existing untagged
            // protocol contract. Once a typed request is bound, only its tagged
            // response may settle the capture.
            return !hasTaggedPendingCommandRequest()
        }
        if (commandResponseId in closedCommandRequestIds) {
            return false
        }
        val ids = pendingCommandRequestIds()
        // The response must match the immutable identity bound before the write.
        // No source/title/latest-request fallback is allowed.
        if (ids.isEmpty() || commandResponseId !in ids) return false
        val expectedGeneration = pendingCommandRequestGeneration(commandResponseId)
        if (expectedGeneration > 0L && connectionGeneration != null &&
            expectedGeneration != connectionGeneration
        ) {
            return false
        }
        val encodedGeneration =
            commandResponseId
                .substringBefore("-r", missingDelimiterValue = "")
                .removePrefix("g")
                .toLongOrNull()
        return encodedGeneration == null || expectedGeneration <= 0L ||
            encodedGeneration == expectedGeneration
    }

    private fun pendingCommandRequestGeneration(commandResponseId: String): Long {
        return listOfNotNull(
            pendingInteractionRequest?.takeIf { it.commandRequestId == commandResponseId }
                ?.commandRequestGeneration,
            pendingProtocolSheetRequest?.takeIf { it.commandRequestId == commandResponseId }
                ?.commandRequestGeneration,
            pendingProtocolPopupRequest?.takeIf { it.commandRequestId == commandResponseId }
                ?.commandRequestGeneration,
            pendingOrdinaryCommandRequest?.takeIf { it.id == commandResponseId }
                ?.generation,
        ).firstOrNull { it > 0L } ?: 0L
    }

    fun hasTaggedPendingCommandRequest(): Boolean =
        listOfNotNull(
            pendingInteractionRequest?.commandRequestId?.takeIf { it.isNotBlank() },
            pendingProtocolSheetRequest?.commandRequestId?.takeIf { it.isNotBlank() },
            pendingProtocolPopupRequest?.commandRequestId?.takeIf { it.isNotBlank() },
            pendingOrdinaryCommandRequest?.id?.takeIf { it.isNotBlank() },
        ).isNotEmpty()

    fun pendingCommandResponseId(): String =
        listOfNotNull(
            pendingInteractionRequest?.commandRequestId?.takeIf { it.isNotBlank() },
            pendingProtocolSheetRequest?.commandRequestId?.takeIf { it.isNotBlank() },
            pendingProtocolPopupRequest?.commandRequestId?.takeIf { it.isNotBlank() },
            pendingOrdinaryCommandRequest?.id?.takeIf { it.isNotBlank() },
        ).firstOrNull().orEmpty()

    fun clearAll() {
        rememberPendingCommandRequestIds()
        pendingOrdinaryCommandRequest = null
        interactionRequestEpoch += 1L
        protocolSheetRequestEpoch += 1L
        protocolPopupRequestEpoch += 1L
        interactionRequestJob?.cancel()
        interactionRequestJob = null
        combatStartTimeoutJob?.cancel()
        combatStartTimeoutJob = null
        protocolSheetRequestJob?.cancel()
        protocolSheetRequestJob = null
        protocolPopupRequestJob?.cancel()
        protocolPopupRequestJob = null
        pendingInteractionRequest = null
        pendingProtocolSheetRequest = null
        pendingProtocolPopupRequest = null
        settledNpcQuestRequest = null
    }

    private fun hasPendingCapture(): Boolean =
        pendingInteractionRequest != null ||
            pendingProtocolSheetRequest != null ||
            pendingProtocolPopupRequest != null

    private fun pendingCommandRequestIds(): List<String> =
        listOfNotNull(
            pendingInteractionRequest?.commandRequestId?.takeIf { it.isNotBlank() },
            pendingProtocolSheetRequest?.commandRequestId?.takeIf { it.isNotBlank() },
            pendingProtocolPopupRequest?.commandRequestId?.takeIf { it.isNotBlank() },
            pendingOrdinaryCommandRequest?.id?.takeIf { it.isNotBlank() },
        ).distinct()

    private fun rememberPendingCommandRequestIds() {
        pendingCommandRequestIds().forEach(::rememberCommandRequestId)
    }

    private fun rememberCommandRequestId(requestId: String?) {
        if (requestId.isNullOrBlank()) return
        closedCommandRequestIds += requestId
        while (closedCommandRequestIds.size > CLOSED_COMMAND_REQUEST_TOMBSTONE_CAPACITY) {
            val iterator = closedCommandRequestIds.iterator()
            iterator.next()
            iterator.remove()
        }
    }

    /** 由 Coordinator 注入回调：combat 超时飘窗 + settled 状态应用（sink 包装）。 */
    fun attachSinks(
        onCombatTimeout: () -> Unit,
        applyInteraction: (PendingInteractionRequest) -> Unit,
        applyProtocolSheet: (PendingProtocolSheetRequest) -> Unit,
        applyProtocolPopup: (PendingProtocolPopupRequest) -> Unit,
    ) {
        onCombatStartTimeout = onCombatTimeout
        applyInteractionSettled = applyInteraction
        applyProtocolSheetSettled = applyProtocolSheet
        applyProtocolPopupSettled = applyProtocolPopup
    }

    fun keepCurrentInteractionPendingHidden() {
        pendingInteractionRequest = pendingInteractionRequest?.copy(keepSheetClosedOnResponse = true)
    }

    fun prepareProtocolSheetCommand(
        currentSheet: ShellSheetState?,
        command: String,
        preservedLinesOverride: List<String>? = null,
    ) {
        if (currentSheet == null) {
            return
        }
        val preservedLines =
            preservedLinesOverride ?: currentSheet.detailLines.takeIf { lines ->
                lines.isNotEmpty() && (currentSheet.title in persistentInfoSheetTitles || currentSheet.isQuestDetailSheet())
            }.orEmpty()
        protocolSheetRequestEpoch += 1L
        protocolSheetRequestJob?.cancel()
        protocolSheetRequestJob = null
        pendingProtocolSheetRequest =
            PendingProtocolSheetRequest(
                sheetIdentity = currentSheet.sheetIdentity(),
                workspaceIdentity = currentSheet.workspaceRequestIdentity(),
                command = command,
                lines = preservedLines.toMutableList(),
                seenLines = linkedSetOf<String>().apply { addAll(preservedLines) },
            )
    }

    fun prepareProtocolPopupCommand(
        currentDialog: ShellPopupState?,
        command: String = "",
    ) {
        if (currentDialog == null) {
            return
        }
        protocolPopupRequestEpoch += 1L
        protocolPopupRequestJob?.cancel()
        protocolPopupRequestJob = null
        pendingProtocolPopupRequest =
            PendingProtocolPopupRequest(
                popupIdentity = currentDialog.popupIdentity(),
                baseMessage = currentDialog.message,
                lines = mutableListOf(),
                seenLines = linkedSetOf(),
                command = command,
            )
    }

    fun collectPendingInteractionLine(line: String): InteractionCollectionResult {
        val normalized = line.normalizeInteractionLine() ?: return InteractionCollectionResult()
        val pending = pendingInteractionRequest
        if (pending == null) {
            return if (settledNpcQuestRequest != null && listOf(normalized).isNpcTaskEmptyFeedback()) {
                InteractionCollectionResult(handled = true)
            } else {
                InteractionCollectionResult()
            }
        }
        if (pending.isNarrativeConversationRequest()) {
            if (normalized == ">" || normalized.endsWith(">")) {
                return InteractionCollectionResult(
                    handled = true,
                    activeActionGroupId = pending.activeActionGroupId,
                    shouldSettle = true,
                )
            }
            if (!pending.seenInteractionLines.add(normalized)) {
                return InteractionCollectionResult(handled = true)
            }
            return InteractionCollectionResult()
        }
        if (normalized == ">" || normalized.endsWith(">")) {
            return if (pending.keepCollectingUntilPrompt) {
                InteractionCollectionResult(
                    handled = true,
                    activeActionGroupId = pending.activeActionGroupId,
                    shouldSettle = true,
                )
            } else {
                InteractionCollectionResult()
            }
        }
        if (normalized.startsWith("[")) {
            return InteractionCollectionResult()
        }
        if (pending.isNpcQuestRequest() && listOf(normalized).isNpcTaskEmptyFeedback()) {
            settleNpcQuestEmpty(pending)
            return InteractionCollectionResult(
                handled = true,
                activeActionGroupId = "root",
                feedbackLine = normalized,
                resetInteractionToRoot = true,
            )
        }
        val section = pending.target.classifyInteractionSection(normalized)
        val dedupeTarget =
            when (section) {
                InteractionResponseSection.DETAIL -> pending.seenDetailLines
                InteractionResponseSection.INTERACTION -> pending.seenInteractionLines
            }
        if (!dedupeTarget.add(normalized)) {
            return InteractionCollectionResult(handled = true)
        }
        when (section) {
            InteractionResponseSection.DETAIL -> pending.descriptionLines += normalized
            InteractionResponseSection.INTERACTION -> pending.interactionLines += normalized
        }
        return InteractionCollectionResult(
            handled = true,
            detailLines = pending.descriptionLines.toList(),
            interactionLines = pending.interactionLines.toList(),
            activeActionGroupId = pending.activeActionGroupId,
            shouldScheduleSettle = true,
        )
    }

    fun collectPendingProtocolSheetLine(line: String): ProtocolSheetCollectionResult {
        val pending = pendingProtocolSheetRequest ?: return ProtocolSheetCollectionResult()
        val normalized = line.normalizeInteractionLine() ?: return ProtocolSheetCollectionResult()
        if (normalized == ">" || normalized.endsWith(">")) {
            return ProtocolSheetCollectionResult(handled = true, shouldSettle = true)
        }
        if (!pending.seenLines.add(normalized)) {
            return ProtocolSheetCollectionResult(handled = true)
        }
        pending.lines += normalized
        return ProtocolSheetCollectionResult(
            handled = true,
            sheetIdentity = pending.sheetIdentity,
            command = pending.command,
            lines = pending.lines.toList(),
            shouldScheduleSettle = true,
        )
    }

    fun collectPendingProtocolPopupLine(line: String): ProtocolPopupCollectionResult {
        val pending = pendingProtocolPopupRequest ?: return ProtocolPopupCollectionResult()
        val normalized = line.normalizeInteractionLine() ?: return ProtocolPopupCollectionResult()
        if (normalized == ">" || normalized.endsWith(">")) {
            return ProtocolPopupCollectionResult(handled = true, shouldSettle = true)
        }
        if (!pending.seenLines.add(normalized)) {
            return ProtocolPopupCollectionResult(handled = true)
        }
        pending.lines += normalized
        return ProtocolPopupCollectionResult(
            handled = true,
            popupIdentity = pending.popupIdentity,
            message = pending.renderedMessage(),
            shouldScheduleSettle = true,
        )
    }

    fun capturePendingError(message: String): PendingErrorCapture {
        val pendingLine = message.toPendingErrorLine() ?: return PendingErrorCapture.None

        pendingProtocolPopupRequest?.let { pending ->
            if (pending.seenLines.add(pendingLine)) {
                pending.lines += pendingLine
            }
            return PendingErrorCapture.ProtocolPopup
        }

        pendingProtocolSheetRequest?.let { pending ->
            if (pending.command.isLookInteractionCommand()) {
                return PendingErrorCapture.ProtocolSheetNotice
            }
            if (pending.seenLines.add(pendingLine)) {
                pending.lines += pendingLine
            }
            return PendingErrorCapture.ProtocolSheet
        }

        pendingInteractionRequest?.let { pending ->
            if (pending.isCombatStartRequest()) {
                return PendingErrorCapture.CombatNotice
            }
            if (pending.shouldKeepErrorOutOfInteractionSheet()) {
                return PendingErrorCapture.InteractionNotice
            }
            if (pending.seenInteractionLines.add(pendingLine)) {
                pending.interactionLines += pendingLine
            }
            return PendingErrorCapture.Interaction
        }

        return PendingErrorCapture.None
    }

    fun shouldSettleInteractionOnPrompt(): Boolean {
        return pendingInteractionRequest?.keepCollectingUntilPrompt == true
    }

    fun hasPendingCombatStart(): Boolean = pendingInteractionRequest?.isCombatStartRequest() == true

    fun pendingCombatStartFailure(line: String): String? {
        if (!hasPendingCombatStart()) return null
        val normalized = line.normalizeInteractionLine() ?: return null
        return normalized.takeIf(String::isCombatStartFailureMessage)
    }

    fun settlePendingNpcQuestEmpty() {
        pendingInteractionRequest?.takeIf { it.isNpcQuestRequest() }?.let(::settleNpcQuestEmpty)
    }

    fun shouldConsumeSettledNpcQuestDialog(providerId: String): Boolean {
        val settled = settledNpcQuestRequest ?: return false
        val providerToken = providerId.npcCommandToken()
        return providerToken.isBlank() || providerToken in settled.targetTokens
    }

    private fun settleNpcQuestEmpty(pending: PendingInteractionRequest) {
        settledNpcQuestRequest =
            SettledNpcQuestRequest(
                generation = pending.requestGeneration,
                targetTokens =
                    setOf(
                        pending.target.id.npcCommandToken(),
                        pending.target.uiKey.npcCommandToken(),
                        pending.target.commandTarget().npcCommandToken(),
                    ).filter(String::isNotBlank).toSet(),
            )
    }

    fun scheduleInteractionRequestSettle() {
        interactionRequestEpoch += 1L
        val requestEpoch = interactionRequestEpoch
        interactionRequestJob?.cancel()
        interactionRequestJob =
            scope.launch {
                delay(INTERACTION_SETTLE_MS)
                if (interactionRequestEpoch != requestEpoch) return@launch
                val settled = pendingInteractionRequest ?: return@launch
                if (settled.keepCollectingUntilPrompt &&
                    settled.descriptionLines.isEmpty() &&
                    settled.interactionLines.isEmpty()
                ) {
                    interactionRequestJob = null
                    return@launch
                }
                settlePendingInteractionRequest(
                    clearAfterSettle = !settled.keepCollectingUntilPrompt,
                    expectedEpoch = requestEpoch,
                )
            }
    }

    fun cancelInteractionRequestSettle() {
        interactionRequestEpoch += 1L
        interactionRequestJob?.cancel()
    }

    fun clearInteractionRequestSettleJob() {
        interactionRequestEpoch += 1L
        interactionRequestJob = null
    }

    fun cancelProtocolSheetSettle() {
        protocolSheetRequestEpoch += 1L
        protocolSheetRequestJob?.cancel()
    }

    fun settlePendingInteractionRequest(
        clearAfterSettle: Boolean,
        expectedEpoch: Long? = null,
    ) {
        val requestEpoch = expectedEpoch ?: interactionRequestEpoch
        if (interactionRequestEpoch != requestEpoch) return
        val settled = pendingInteractionRequest ?: return
        applyInteractionSettled(settled)
        if (interactionRequestEpoch != requestEpoch) return
        if (clearAfterSettle) {
            clearPendingInteractionRequest()
        } else {
            interactionRequestJob = null
        }
    }

    fun settlePendingProtocolSheetRequest(
        clearAfterSettle: Boolean,
        expectedEpoch: Long? = null,
    ) {
        val requestEpoch = expectedEpoch ?: protocolSheetRequestEpoch
        if (protocolSheetRequestEpoch != requestEpoch) return
        val pending = pendingProtocolSheetRequest ?: return
        applyProtocolSheetSettled(pending)
        if (protocolSheetRequestEpoch != requestEpoch) return
        if (clearAfterSettle) {
            clearPendingProtocolSheetRequest()
        } else {
            protocolSheetRequestJob = null
        }
    }

    fun settlePendingProtocolPopupRequest(
        clearAfterSettle: Boolean,
        expectedEpoch: Long? = null,
    ) {
        val requestEpoch = expectedEpoch ?: protocolPopupRequestEpoch
        if (protocolPopupRequestEpoch != requestEpoch) return
        val pending = pendingProtocolPopupRequest ?: return
        applyProtocolPopupSettled(pending)
        if (protocolPopupRequestEpoch != requestEpoch) return
        if (clearAfterSettle) {
            clearPendingProtocolPopupRequest()
        } else {
            protocolPopupRequestJob = null
        }
    }

    fun scheduleProtocolSheetRequestSettle() {
        protocolSheetRequestEpoch += 1L
        val requestEpoch = protocolSheetRequestEpoch
        protocolSheetRequestJob?.cancel()
        protocolSheetRequestJob =
            scope.launch {
                delay(INTERACTION_SETTLE_MS)
                settlePendingProtocolSheetRequest(
                    clearAfterSettle = true,
                    expectedEpoch = requestEpoch,
                )
            }
    }

    fun scheduleProtocolPopupRequestSettle() {
        protocolPopupRequestEpoch += 1L
        val requestEpoch = protocolPopupRequestEpoch
        protocolPopupRequestJob?.cancel()
        protocolPopupRequestJob =
            scope.launch {
                delay(INTERACTION_SETTLE_MS)
                settlePendingProtocolPopupRequest(
                    clearAfterSettle = true,
                    expectedEpoch = requestEpoch,
                )
            }
    }
}

internal data class SettledNpcQuestRequest(
    val generation: Long,
    val targetTokens: Set<String>,
)
