package com.xiakexing.client.feature.shell.component

import com.xiakexing.client.feature.shell.InteractionObject
import com.xiakexing.client.feature.shell.InteractionObjectKind
import java.text.Collator
import java.util.Locale

internal fun List<InteractionObject>.sortedForObjectRail(): List<InteractionObject> {
    val titleCollator =
        Collator.getInstance(Locale.SIMPLIFIED_CHINESE).apply {
            strength = Collator.PRIMARY
            decomposition = Collator.CANONICAL_DECOMPOSITION
        }
    return sortedWith { left, right ->
        compareValues(left.kind.objectRailGroup, right.kind.objectRailGroup)
            .takeUnless { it == 0 }
            ?: compareValues(left.title.displayCharacterCount(), right.title.displayCharacterCount())
                .takeUnless { it == 0 }
            ?: titleCollator.compare(left.title.trim(), right.title.trim())
                .takeUnless { it == 0 }
            ?: left.uiKey.compareTo(right.uiKey)
    }
}

internal class ObjectRailOrderState {
    private var orderedKeys: List<String> = emptyList()
    private var initialized = false

    fun update(objects: List<InteractionObject>): List<InteractionObject> {
        if (!initialized) {
            initialized = true
            val initial = objects.sortedForObjectRail()
            orderedKeys = initial.map(InteractionObject::uiKey)
            return initial
        }

        val objectsByKey = objects.associateBy(InteractionObject::uiKey)
        val retainedKeys = orderedKeys.filter(objectsByKey::containsKey)
        val retainedKeySet = retainedKeys.toHashSet()
        val newKeys =
            objects.asSequence()
                .map(InteractionObject::uiKey)
                .filterNot(retainedKeySet::contains)
                .distinct()
                .toList()
        orderedKeys = newKeys + retainedKeys
        return orderedKeys.mapNotNull(objectsByKey::get)
    }
}

private val InteractionObjectKind.objectRailGroup: Int
    get() = if (this == InteractionObjectKind.NPC) 0 else 1

private fun String.displayCharacterCount(): Int {
    val displayText = trim()
    return displayText.codePointCount(0, displayText.length)
}
