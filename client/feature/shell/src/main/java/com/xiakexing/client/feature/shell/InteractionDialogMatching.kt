package com.xiakexing.client.feature.shell

import com.xiakexing.client.core.protocol.payload.DialogPacket

internal fun DialogPacket.belongsToInteractionSession(): Boolean {
    return source == "look_item" ||
        source.startsWith("npc_") ||
        speakerId.isNotBlank() ||
        target != null
}

internal fun DialogPacket.matchesInteractionSession(
    interactionTarget: InteractionObject?,
    currentSheet: ShellSheetState?,
): Boolean {
    if (matchesInteractionTarget(interactionTarget)) {
        return true
    }

    if (currentSheet?.source != PopupSource.INTERACTION_OBJECT) {
        return false
    }

    val normalizedDialogId = dialogId.routingToken()
    val normalizedSource = source.routingToken()
    val currentDialogId = currentSheet.dialogId.routingToken()
    val currentDialogSource = currentSheet.dialogSource.routingToken()
    return normalizedDialogId.isNotBlank() &&
        normalizedSource.isNotBlank() &&
        normalizedDialogId == currentDialogId &&
        normalizedSource == currentDialogSource
}

internal fun DialogPacket.matchesInteractionTarget(interactionTarget: InteractionObject?): Boolean {
    if (interactionTarget == null) {
        return false
    }
    if (hasConflictingExplicitTargetIdentity(interactionTarget)) {
        return false
    }

    val targetId = interactionTarget.id.routingToken()
    val rawTargetId =
        if (interactionTarget.kind == InteractionObjectKind.ITEM &&
            targetId.startsWith(ROOM_ITEM_INTERACTION_PREFIX)
        ) {
            targetId.removePrefix(ROOM_ITEM_INTERACTION_PREFIX)
        } else {
            targetId
        }
    val targetTitle = interactionTarget.title.routingToken()
    val speakerToken = speakerId.routingToken()
    val sourceToken = source.routingToken()
    val packetTargetId = target?.id.orEmpty().routingToken()
    val packetTargetName = target?.name.orEmpty().routingToken()
    val packetTemplateId = target?.templateId.orEmpty().routingToken()
    val packetTargetLocation = target?.location.orEmpty().routingToken()
    val providerToken = providerId.routingToken()
    val providerNpcToken = providerId.npcCommandToken()
    val targetNpcToken = interactionTarget.commandTarget().npcCommandToken()
    val packetTitle = title.routingToken()

    if (interactionTarget.kind == InteractionObjectKind.ITEM && targetId.startsWith(ROOM_ITEM_INTERACTION_PREFIX)) {
        return sourceToken == "look_item" &&
            packetTargetLocation == "room" &&
            (
                packetTargetId == rawTargetId ||
                    packetTemplateId == rawTargetId ||
                    packetTargetName == targetTitle ||
                    packetTitle == targetTitle
            )
    }

    if (targetId.isNotBlank()) {
        if (speakerToken == targetId) {
            return true
        }
        if (packetTargetId == rawTargetId || packetTemplateId == rawTargetId) {
            return true
        }
        if (providerToken == rawTargetId || providerToken == targetId) {
            return true
        }
        if (interactionTarget.kind == InteractionObjectKind.NPC &&
            providerNpcToken.isNotBlank() &&
            providerNpcToken == targetNpcToken
        ) {
            return true
        }
        if (sourceToken.removePrefix("npc_") == targetId) {
            return true
        }
    }

    if (targetTitle.isNotBlank()) {
        if (packetTargetName == targetTitle || packetTitle == targetTitle) {
            return true
        }
    }

    return interactionTarget.kind != InteractionObjectKind.NPC &&
        sourceToken == "look_item" &&
        (packetTargetId.isNotBlank() || packetTargetName == targetTitle || packetTitle == targetTitle)
}

private fun DialogPacket.hasConflictingExplicitTargetIdentity(
    interactionTarget: InteractionObject,
): Boolean {
    val expectedTargetId = interactionTarget.id.routingToken()
    val packetTargetId = target?.id.orEmpty().routingToken()
    if (expectedTargetId.isBlank() || packetTargetId.isBlank()) {
        return false
    }
    val expectedIds =
        setOf(
            expectedTargetId,
            if (expectedTargetId.startsWith(ROOM_ITEM_INTERACTION_PREFIX)) {
                expectedTargetId.removePrefix(ROOM_ITEM_INTERACTION_PREFIX)
            } else {
                expectedTargetId
            },
        ).filter(String::isNotBlank)
    if (packetTargetId !in expectedIds) {
        return true
    }
    if (interactionTarget.kind == InteractionObjectKind.NPC && providerId.isNotBlank()) {
        val providerToken = providerId.routingToken().npcCommandToken()
        val expectedNpcToken = interactionTarget.commandTarget().routingToken().npcCommandToken()
        if (providerToken.isNotBlank() &&
            expectedNpcToken.isNotBlank() &&
            providerToken != expectedNpcToken
        ) {
            return true
        }
    }
    return false
}
