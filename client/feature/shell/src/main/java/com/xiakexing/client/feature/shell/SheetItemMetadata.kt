package com.xiakexing.client.feature.shell

/** Stable keys used by sheet builders when protocol-backed rows need extra display data. */
internal object SheetItemMetadata {
    const val QuestActionPending = "quest_action_pending"
    const val QuestActionPendingCommand = "quest_action_pending_command"
    const val TargetRoomId = "target_room_id"
    const val TargetRoomName = "target_room_name"
    const val SourceKind = "source_kind"
    const val ObjectiveId = "objective_id"
    const val AcceptanceId = "acceptance_id"
    const val AcceptanceRevision = "acceptance_revision"
    const val AutoTrack = "auto_track"
    const val TrackingPriority = "tracking_priority"
    const val RecommendedCommand = "recommended_command"
    const val CanNavigate = "can_navigate"
}

internal val ShellSheetItem.questActionPending: Boolean
    get() = metadata[SheetItemMetadata.QuestActionPending] == "1"

internal val ShellSheetItem.questActionPendingCommand: String
    get() = metadata[SheetItemMetadata.QuestActionPendingCommand].orEmpty()

internal val ShellSheetItem.targetRoomId: String
    get() = metadata[SheetItemMetadata.TargetRoomId].orEmpty()

internal val ShellPanelCard.targetRoomId: String
    get() = metadata[SheetItemMetadata.TargetRoomId].orEmpty()

internal val ShellPanelCard.targetRoomName: String
    get() = metadata[SheetItemMetadata.TargetRoomName].orEmpty()
