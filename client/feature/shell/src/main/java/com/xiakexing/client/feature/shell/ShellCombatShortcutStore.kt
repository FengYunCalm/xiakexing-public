package com.xiakexing.client.feature.shell

import android.content.Context
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

internal data class StoredCombatShortcuts(
    val assignedActionRefs: List<String>,
    val removedActionRefs: Set<String>,
)

internal interface ShellCombatShortcutStore {
    suspend fun load(): StoredCombatShortcuts

    suspend fun save(
        assignedActionRefs: List<String>,
        removedActionRefs: Set<String>,
    )
}

internal class SharedPreferencesCombatShortcutStore(context: Context) : ShellCombatShortcutStore {
    private val preferences =
        context.applicationContext.getSharedPreferences(
            PREFERENCES_NAME,
            Context.MODE_PRIVATE,
        )

    override suspend fun load(): StoredCombatShortcuts {
        return withContext(Dispatchers.IO) {
            val storedAssignedRefs = preferences.getString(KEY_ASSIGNED_ACTION_REFS, null)
            val storedRemovedRefs = preferences.getString(KEY_REMOVED_ACTION_REFS, null)
            val legacyAssignedRefs =
                preferences.getString(KEY_ASSIGNED_PERFORM_IDS, "")
                    .orEmpty()
                    .decodeAssignedSlots()
            val legacyRemovedRefs =
                preferences.getString(KEY_REMOVED_PERFORM_IDS, "")
                    .orEmpty()
                    .decodeIds()
                    .toSet()
            StoredCombatShortcuts(
                assignedActionRefs =
                    if (preferences.contains(KEY_ASSIGNED_ACTION_REFS)) {
                        storedAssignedRefs.orEmpty().decodeAssignedSlots()
                    } else {
                        legacyAssignedRefs
                    },
                removedActionRefs =
                    if (preferences.contains(KEY_REMOVED_ACTION_REFS)) {
                        storedRemovedRefs.orEmpty().decodeIds().toSet()
                    } else {
                        legacyRemovedRefs
                    },
            )
        }
    }

    override suspend fun save(
        assignedActionRefs: List<String>,
        removedActionRefs: Set<String>,
    ) {
        withContext(Dispatchers.IO) {
            val saved =
                preferences.edit()
                    .putString(KEY_ASSIGNED_ACTION_REFS, assignedActionRefs.encodeAssignedSlots())
                    .putString(
                        KEY_REMOVED_ACTION_REFS,
                        removedActionRefs.toList().sorted().encodeIds(),
                    )
                    .remove(KEY_ASSIGNED_PERFORM_IDS)
                    .remove(KEY_REMOVED_PERFORM_IDS)
                    .commit()
            check(saved) { "unable to persist combat shortcuts" }
        }
    }

    private fun String.decodeAssignedSlots(): List<String> {
        if (isBlank()) {
            return emptyList()
        }
        return split(ID_SEPARATOR)
            .take(COMBAT_PERFORM_SLOT_COUNT)
            .map { it.toCombatShortcutActionRefOrBlank() }
    }

    private fun String.decodeIds(): List<String> {
        return split(ID_SEPARATOR)
            .map { it.toCombatShortcutActionRefOrBlank() }
            .filter { it.isNotBlank() }
            .distinct()
            .take(COMBAT_PERFORM_SLOT_COUNT)
    }

    private fun List<String>.encodeIds(): String {
        return map { it.toCombatShortcutActionRefOrBlank() }
            .filter { it.isNotBlank() }
            .distinct()
            .take(COMBAT_PERFORM_SLOT_COUNT)
            .joinToString(ID_SEPARATOR)
    }

    private fun List<String>.encodeAssignedSlots(): String {
        return take(COMBAT_PERFORM_SLOT_COUNT)
            .joinToString(ID_SEPARATOR) { it.toCombatShortcutActionRefOrBlank() }
    }

    private companion object {
        const val PREFERENCES_NAME = "shell_combat_shortcuts"
        const val KEY_ASSIGNED_ACTION_REFS = "assigned_action_refs"
        const val KEY_REMOVED_ACTION_REFS = "removed_action_refs"
        const val KEY_ASSIGNED_PERFORM_IDS = "assigned_perform_ids"
        const val KEY_REMOVED_PERFORM_IDS = "removed_perform_ids"
        const val ID_SEPARATOR = ","
    }
}
