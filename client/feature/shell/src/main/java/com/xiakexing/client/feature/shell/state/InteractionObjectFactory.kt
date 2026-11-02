package com.xiakexing.client.feature.shell

import com.xiakexing.client.core.protocol.payload.DialogActionGroupPacket
import com.xiakexing.client.core.protocol.payload.NamedEntity

internal const val ROOM_ITEM_INTERACTION_PREFIX = "room:"

internal fun buildInteractionObjects(
    npcs: List<NamedEntity>,
    items: List<NamedEntity>,
): List<InteractionObject> {
    val itemOccurrences = mutableMapOf<String, Int>()
    return npcs.map { entity ->
        InteractionObject(
            id = entity.id,
            uiKey = entity.id,
            title = entity.name,
            subtitle = entity.typeLabel.ifBlank { "人物" },
            summary =
                entity.summary.ifBlank {
                    entity.description.ifBlank { "此人暂无额外描述。" }
                },
            kind = InteractionObjectKind.NPC,
            description =
                entity.description.ifBlank {
                    entity.summary.ifBlank { "此人暂无额外描述。" }
                },
            questAvailable = entity.questAvailable,
            questMarkerOnly = entity.questMarkerOnly,
            availableQuestIds = entity.availableQuestIds,
            actionGroups = entity.actionGroups.toShellActionGroups(),
        )
    } +
        items.map { entity ->
            val kind = entity.type.toInteractionKind()
            val id =
                if (kind == InteractionObjectKind.ITEM) {
                    ROOM_ITEM_INTERACTION_PREFIX + entity.id
                } else {
                    entity.id
                }
            val occurrence = itemOccurrences.getOrDefault(id, 0)
            itemOccurrences[id] = occurrence + 1
            InteractionObject(
                id = id,
                uiKey = "$id#$occurrence",
                title = entity.name,
                subtitle = entity.typeLabel.ifBlank { entity.type.toInteractionLabel() },
                summary =
                    entity.summary.ifBlank {
                        entity.description.ifBlank { "此处可交互对象暂无额外描述。" }
                    },
                kind = kind,
                description =
                    entity.description.ifBlank {
                        entity.summary.ifBlank { "此处可交互对象暂无额外描述。" }
                    },
                questAvailable = entity.questAvailable,
                questMarkerOnly = entity.questMarkerOnly,
                availableQuestIds = entity.availableQuestIds,
                actionGroups = entity.actionGroups.toShellActionGroups(),
            )
        }
}

private fun String.toInteractionKind(): InteractionObjectKind {
    return when (lowercase()) {
        "npc" -> InteractionObjectKind.NPC
        "scene", "scenery", "prop", "object", "furniture", "environment" -> InteractionObjectKind.SCENE
        else -> InteractionObjectKind.ITEM
    }
}

private fun String.toInteractionLabel(): String {
    return when (toInteractionKind()) {
        InteractionObjectKind.NPC -> "人物"
        InteractionObjectKind.ITEM -> "物品"
        InteractionObjectKind.SCENE -> "场景"
    }
}

private fun List<DialogActionGroupPacket>.toShellActionGroups(): List<ShellActionGroup> {
    return map { group ->
        ShellActionGroup(
            id = group.id,
            title = group.title,
            surface = group.surface,
            actions =
                group.actions.map { action ->
                    ShellAction(
                        label = action.label,
                        command = action.command,
                        dismissAfterInvoke = false,
                        kind = action.kind,
                        surface = action.surface.ifBlank { group.surface },
                        role = action.role,
                        targetGroup = action.targetGroup,
                        guided = action.guided,
                    )
                },
        )
    }.filter { it.actions.isNotEmpty() }
}
