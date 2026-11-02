package com.xiakexing.client.feature.shell

import androidx.compose.runtime.Immutable
import com.xiakexing.client.core.protocol.payload.ProjectionIdentity
import com.xiakexing.client.core.protocol.payload.QuestEntryPacket
import com.xiakexing.client.core.protocol.payload.RoutePreviewPacket

internal const val TRACK_QUEST_PREFIX = "ui://quest/track/"
internal const val UNTRACK_QUEST_COMMAND = "ui://quest/untrack"
internal const val ROUTE_PREVIEW_TIMEOUT_MILLIS = 5_000L

@Immutable
data class TrackedObjectiveState(
    val questId: String = "",
    val questName: String = "",
    val guideText: String = "",
    val progressText: String = "",
    val recommendedCommand: String = "",
    val targetRoomId: String = "",
    val targetRoomName: String = "",
    val targetEntityId: String = "",
    val targetEntityName: String = "",
    val routeCommands: List<String> = emptyList(),
    val routeRooms: List<String> = emptyList(),
    val routeError: String = "",
    val maxSteps: Int = 20,
    val sourceKind: String = "quest",
    val objectiveId: String = "",
    val acceptanceId: String = "",
    val acceptanceRevision: Long = 0L,
    val autoTrack: Boolean = false,
    val trackingPriority: Int = 0,
    val manuallySelected: Boolean = false,
    val canNavigate: Boolean = false,
) {
    val active: Boolean
        get() = objectiveId.isNotBlank() || questId.isNotBlank()

    val nextDirection: String
        get() = routeCommands.firstOrNull().orEmpty().toCanonicalTravelDirection()

    val routeRoomSet: Set<String>
        get() = routeRooms.filter { it.isNotBlank() }.toSet()
}

typealias TrackedQuestState = TrackedObjectiveState

internal data class RoutePreviewRequest(
    val requestId: String,
    val projection: ProjectionIdentity,
    val sourceKind: String,
    val objectiveId: String,
    val acceptanceId: String,
    val acceptanceRevision: Long,
    val startRoomId: String,
    val targetRoomId: String,
)

@Immutable
internal data class QuestGuideLaneState(
    val active: Boolean = false,
    val questId: String = "",
    val sourceKind: String = "",
    val objectiveId: String = "",
    val questName: String = "",
    val progressText: String = "",
    val progressFraction: Float? = null,
    val actionText: String = "",
    val openCommand: String = "",
    val idleText: String = "",
)

internal fun QuestEntryPacket.toTrackedQuestState(
    previous: TrackedQuestState = TrackedQuestState(),
): TrackedQuestState {
    val progressText = progressLabel()
    val normalizedObjectiveId = objectiveId.ifBlank { "legacy:quest:$questId" }
    val normalizedAcceptanceId = acceptanceId.ifBlank { "legacy:quest:$questId" }
    val keepsRoute =
        previous.sourceKind == sourceKind &&
            previous.objectiveId == normalizedObjectiveId &&
            previous.acceptanceId == normalizedAcceptanceId &&
            previous.acceptanceRevision == acceptanceRevision &&
            previous.targetRoomId == targetRoomId
    return TrackedQuestState(
        questId = questId,
        questName = displayName().toDisplaySafeText(),
        guideText = toGuideText(),
        progressText = progressText,
        recommendedCommand = recommendedCommand,
        targetRoomId = targetRoomId,
        targetRoomName = targetRoomName,
        targetEntityId = targetEntityId,
        targetEntityName = targetEntityName,
        routeCommands = if (keepsRoute) previous.routeCommands else emptyList(),
        routeRooms = if (keepsRoute) previous.routeRooms else emptyList(),
        routeError = if (keepsRoute) previous.routeError else "",
        maxSteps = previous.maxSteps,
        sourceKind = sourceKind,
        objectiveId = normalizedObjectiveId,
        acceptanceId = normalizedAcceptanceId,
        acceptanceRevision = acceptanceRevision,
        autoTrack = autoTrack,
        trackingPriority = trackingPriority,
        // A target-room change invalidates only the route, not the player's manual priority.
        manuallySelected =
            previous.manuallySelected && previous.sourceKind == sourceKind &&
                previous.objectiveId == normalizedObjectiveId &&
                previous.acceptanceId == normalizedAcceptanceId &&
                previous.acceptanceRevision == acceptanceRevision,
        canNavigate = canNavigate,
    )
}

internal fun TrackedQuestState.withRoutePreview(packet: RoutePreviewPacket): TrackedQuestState {
    if (!active || packet.targetRoomId.isNotBlank() && packet.targetRoomId != targetRoomId) {
        return this
    }
    return copy(
        targetRoomName = packet.targetRoomName.ifBlank { targetRoomName },
        routeCommands =
            if (packet.ok) {
                packet.commands.map {
                    it.toCanonicalTravelDirection()
                }
            } else {
                emptyList()
            },
        routeRooms = if (packet.ok) packet.rooms else emptyList(),
        routeError = if (packet.ok) "" else packet.error,
        maxSteps = packet.maxSteps.takeIf { it > 0 } ?: maxSteps,
    )
}

internal fun TrackedQuestState.toQuestGuideLaneState(): QuestGuideLaneState {
    if (!active) {
        return QuestGuideLaneState()
    }
    return QuestGuideLaneState(
        active = true,
        questId = questId,
        sourceKind = sourceKind,
        objectiveId = objectiveId,
        questName = questName.trim().ifBlank { questId },
        progressText = progressText.trim(),
        progressFraction = progressText.toProgressFractionOrNull(),
        actionText = guideActionText(),
        openCommand = objectiveOpenCommand(),
    )
}

private fun String.toProgressFractionOrNull(): Float? {
    val (current, total) =
        trim().split('/', limit = 2).mapNotNull(String::toFloatOrNull)
            .takeIf { it.size == 2 } ?: return null
    if (total <= 0f) return null
    return (current / total).coerceIn(0f, 1f)
}

private fun TrackedQuestState.objectiveOpenCommand(): String {
    return questId.takeIf { it.isNotBlank() }
        ?.let { "$OPEN_QUEST_DETAIL_PREFIX$it" }
        .orEmpty()
}

private fun TrackedQuestState.guideActionText(): String {
    val fallback =
        when {
            targetEntityName.isNotBlank() -> "与${targetEntityName}交互"
            targetEntityId.isNotBlank() -> "与${targetEntityId}交互"
            targetRoomName.isNotBlank() -> "前往$targetRoomName"
            targetRoomId.isNotBlank() -> "前往目标地点"
            else -> questName.ifBlank { questId }
        }
    return guideText.ifBlank { fallback }.withoutProgressSuffix(progressText)
}

/** 传输损坏的展示文本（含替换字符 U+FFFD 或控制字符）不参与展示：
 * 协议数据损坏的残留曾在追踪栏表现为乱码。 */
internal fun String.toDisplaySafeText(fallback: String = "任务"): String =
    if (any { it == '\uFFFD' || it.code < 32 }) fallback else this

private fun QuestEntryPacket.toGuideText(): String {
    val base =
        when {
            canTurnIn ->
                providerName.takeIf { it.isNotBlank() }?.let { "回去找${it}交付" }
                    ?: "回去交付${displayName().toDisplaySafeText()}"
            nextStepLabel.isNotBlank() -> nextStepLabel
            targetEntityName.isNotBlank() -> "与${targetEntityName}交互"
            targetRoomName.isNotBlank() -> "前往$targetRoomName"
            targetEntityId.isNotBlank() -> "与${targetEntityId}交互"
            targetRoomId.isNotBlank() -> "前往目标地点"
            else -> ""
        }.orEmpty()
    val progressText = progressLabel()
    return base.withProgressSuffix(progressText).toDisplaySafeText("")
}

private fun String.withProgressSuffix(progressText: String): String {
    val normalized = trim()
    if (normalized.isBlank() || progressText.isBlank() || normalized.contains(progressText)) {
        return normalized
    }
    return "$normalized（$progressText）"
}

private fun String.withoutProgressSuffix(progressText: String): String {
    val normalized = trim()
    val normalizedProgress = progressText.trim()
    if (normalized.isBlank() || normalizedProgress.isBlank()) {
        return normalized
    }
    return normalized.removeSuffix("（$normalizedProgress）").trimEnd()
}

internal fun String.toCanonicalTravelDirection(): String {
    return when (trim().lowercase()) {
        "n" -> "north"
        "s" -> "south"
        "e" -> "east"
        "w" -> "west"
        "ne" -> "northeast"
        "nw" -> "northwest"
        "se" -> "southeast"
        "sw" -> "southwest"
        "u" -> "up"
        "d" -> "down"
        else -> trim().lowercase()
    }
}

private fun String.toDirectionLabel(): String {
    return when (toCanonicalTravelDirection()) {
        "north" -> "北"
        "south" -> "南"
        "east" -> "东"
        "west" -> "西"
        "northeast" -> "东北"
        "northwest" -> "西北"
        "southeast" -> "东南"
        "southwest" -> "西南"
        "up" -> "上"
        "down" -> "下"
        "enter" -> "进入"
        "out" -> "出去"
        else -> this
    }
}
