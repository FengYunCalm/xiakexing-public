package com.xiakexing.client.feature.shell

import androidx.compose.runtime.Immutable
import com.xiakexing.client.core.protocol.payload.QuestEntryPacket

/** Projection-bound objective aggregate consumed by the fixed Activity surface. */
@Immutable
data class ObjectiveHubState(
    val quests: List<QuestEntryPacket> = emptyList(),
    val trackedObjective: TrackedObjectiveState = TrackedObjectiveState(),
)

internal fun ObjectiveHubState.clearProjectionBoundState(): ObjectiveHubState {
    return ObjectiveHubState()
}

internal fun ShellSheetState?.rebuildObjectiveHubIfVisible(
    objectiveHub: ObjectiveHubState,
): ShellSheetState? {
    val current = this ?: return null
    return if (current.source == PopupSource.DOCK_BUTTON &&
        current.resolvedSheetKind() == ShellSheetKind.ACTIVITY
    ) {
        buildActivitySheetState(objectiveHub)
    } else {
        current
    }
}
