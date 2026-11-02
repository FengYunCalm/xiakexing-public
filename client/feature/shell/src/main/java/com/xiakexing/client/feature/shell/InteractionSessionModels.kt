package com.xiakexing.client.feature.shell

/*
 * InteractionSession 域模型——capture 收集结果与 pending 请求
 * （原 InteractionCaptureCoordinator.kt / ShellProtocolCaptureCoordinator.kt 尾部）。
 */

internal data class InteractionCollectionResult(
    val handled: Boolean = false,
    val detailLines: List<String> = emptyList(),
    val interactionLines: List<String> = emptyList(),
    val activeActionGroupId: String = "",
    val feedbackLine: String = "",
    val resetInteractionToRoot: Boolean = false,
    val shouldSettle: Boolean = false,
    val shouldScheduleSettle: Boolean = false,
)

internal data class ProtocolSheetCollectionResult(
    val handled: Boolean = false,
    val sheetIdentity: String = "",
    val command: String = "",
    val lines: List<String> = emptyList(),
    val shouldSettle: Boolean = false,
    val shouldScheduleSettle: Boolean = false,
)

internal data class ProtocolPopupCollectionResult(
    val handled: Boolean = false,
    val popupIdentity: String = "",
    val message: String = "",
    val shouldSettle: Boolean = false,
    val shouldScheduleSettle: Boolean = false,
)

internal enum class PendingErrorCapture {
    None,
    ProtocolPopup,
    ProtocolSheet,
    ProtocolSheetNotice,
    Interaction,
    InteractionNotice,
    CombatNotice,
}

internal enum class InteractionResponseSection {
    DETAIL,
    INTERACTION,
}

/** Identifies a visible Dock page so delayed protocol text cannot overwrite a newer route. */
internal data class WorkspaceRequestIdentity(
    val dockSection: DockWorkspaceSection? = null,
    val routeKey: String = "",
    val targetId: String = "",
)

internal data class PendingInteractionRequest(
    val target: InteractionObject,
    val command: String = "",
    val fallbackActions: List<ShellAction>,
    val descriptionLines: MutableList<String>,
    val interactionLines: MutableList<String>,
    val seenDetailLines: MutableSet<String>,
    val seenInteractionLines: MutableSet<String>,
    val activeActionGroupId: String = "",
    val keepCollectingUntilPrompt: Boolean = false,
    val keepSheetClosedOnResponse: Boolean = false,
    val requestGeneration: Long = 0L,
    val commandRequestId: String = "",
    val commandRequestGeneration: Long = 0L,
    var narrativeLeadLineEmitted: Boolean = false,
)

internal fun PendingInteractionRequest.shouldKeepErrorOutOfInteractionSheet(): Boolean {
    val normalizedCommand = command.trim().lowercase()
    return target.kind != InteractionObjectKind.NPC &&
        (normalizedCommand.isLookInteractionCommand() || normalizedCommand.startsWith("get "))
}

internal fun PendingInteractionRequest.isNpcQuestRequest(): Boolean {
    val normalizedCommand = command.trim().lowercase()
    return target.kind == InteractionObjectKind.NPC &&
        (activeActionGroupId == "quest" || normalizedCommand.startsWith("quest"))
}

internal fun PendingInteractionRequest.isCombatStartRequest(): Boolean {
    val normalized = command.trim().lowercase()
    return target.kind == InteractionObjectKind.NPC && (
        normalized == "fight" || normalized.startsWith("fight ") ||
            normalized == "kill" || normalized.startsWith("kill ")
    )
}

internal fun String.isCombatStartFailureMessage(): Boolean {
    val actionRelated = contains("战斗") || contains("切磋") || contains("攻击") || contains("动武")
    val denied = listOf("禁止", "不能", "无法", "不可", "不允许", "不准", "不宜").any(::contains)
    return actionRelated && denied
}

internal data class PendingProtocolSheetRequest(
    val sheetIdentity: String,
    val workspaceIdentity: WorkspaceRequestIdentity = WorkspaceRequestIdentity(),
    val command: String,
    val lines: MutableList<String>,
    val seenLines: MutableSet<String>,
    val commandRequestId: String = "",
    val commandRequestGeneration: Long = 0L,
)

internal fun ShellSheetState.workspaceRequestIdentity(): WorkspaceRequestIdentity =
    WorkspaceRequestIdentity(
        dockSection = navigation.dockSection,
        routeKey = navigation.routeKey,
        targetId = interactionTargetId.ifBlank { dialogId },
    )

internal fun ShellSheetState.matchesWorkspaceRequest(
    pending: PendingProtocolSheetRequest,
): Boolean {
    val expected = pending.workspaceIdentity
    return expected.dockSection == null || expected.routeKey.isBlank() || workspaceRequestIdentity() == expected
}

internal data class PendingProtocolPopupRequest(
    val popupIdentity: String,
    val baseMessage: String,
    val lines: MutableList<String>,
    val seenLines: MutableSet<String>,
    val command: String = "",
    val commandRequestId: String = "",
    val commandRequestGeneration: Long = 0L,
) {
    fun renderedMessage(): String {
        return listOf(
            baseMessage.takeIf {
                it.isNotBlank()
            },
            lines.joinToString("\n").takeIf { it.isNotBlank() },
        )
            .filterNotNull()
            .joinToString("\n\n")
    }
}

internal fun buildPendingInteractionRequest(
    target: InteractionObject,
    fallbackActions: List<ShellAction>,
    command: String = "",
    detailLines: List<String> = target.summary.toParagraphs(),
    interactionLines: List<String> = emptyList(),
    activeActionGroupId: String = "",
    keepCollectingUntilPrompt: Boolean = false,
    keepSheetClosedOnResponse: Boolean = false,
): PendingInteractionRequest {
    val normalizedDetailLines = detailLines.mapNotNull { it.normalizeInteractionLine() }
    val normalizedInteractionLines = interactionLines.mapNotNull { it.normalizeInteractionLine() }
    return PendingInteractionRequest(
        target = target,
        command = command,
        fallbackActions = fallbackActions,
        descriptionLines = normalizedDetailLines.toMutableList(),
        interactionLines = normalizedInteractionLines.toMutableList(),
        seenDetailLines = linkedSetOf<String>().apply { addAll(normalizedDetailLines) },
        seenInteractionLines = linkedSetOf<String>().apply { addAll(normalizedInteractionLines) },
        activeActionGroupId = activeActionGroupId,
        keepCollectingUntilPrompt = keepCollectingUntilPrompt,
        keepSheetClosedOnResponse = keepSheetClosedOnResponse,
    )
}

// ===== capture 工具（原 ShellProtocolCaptureCoordinator.kt 尾部） =====

internal fun PendingInteractionRequest?.lookFailureNoticeText(): String {
    return when (this?.target?.kind) {
        InteractionObjectKind.ITEM -> "这个物品已不在这里"
        InteractionObjectKind.SCENE -> "这个对象暂时无法查看"
        else -> "暂时无法查看这个目标"
    }
}

internal fun ShellSheetState?.targetsInvalidatedInteraction(
    targetId: String,
    targetUiKey: String,
): Boolean {
    val currentTargetId = this?.interactionTargetId.orEmpty()
    val currentTargetUiKey = this?.interactionTargetUiKey.orEmpty()
    return (currentTargetUiKey.isNotBlank() && currentTargetUiKey == targetUiKey) ||
        (currentTargetUiKey.isBlank() && currentTargetId.isNotBlank() && currentTargetId == targetId)
}

internal fun ShellSheetState.shouldPreserveProtocolSheetSurface(command: String): Boolean {
    val normalized = command.trim().lowercase()
    return isQuestPanelSheet() ||
        normalized == "quest" ||
        normalized.startsWith("quest ")
}
