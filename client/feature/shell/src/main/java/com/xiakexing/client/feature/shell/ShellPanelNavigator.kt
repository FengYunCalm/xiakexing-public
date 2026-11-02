package com.xiakexing.client.feature.shell

import com.xiakexing.client.core.protocol.payload.ExitSnapshot
import com.xiakexing.client.core.protocol.payload.MapGridPacket
import com.xiakexing.client.core.protocol.payload.PlayerStatusSnapshot

internal class ShellPanelNavigator {
    fun help(categoryId: String): ShellPanelNavigationPlan {
        return panel(buildHelpSheetState(categoryId))
    }

    fun character(statusSnapshot: PlayerStatusSnapshot?): ShellPanelNavigationPlan {
        return panel(buildCharacterSheetState(statusSnapshot))
    }

    fun attribute(statusSnapshot: PlayerStatusSnapshot?): ShellPanelNavigationPlan {
        return panel(buildAttributeSheetState(statusSnapshot))
    }

    fun worldShopGuide(): ShellPanelNavigationPlan {
        return panel(buildWorldShopGuideSheetState())
    }

    fun function(): ShellPanelNavigationPlan {
        return panel(buildFunctionSheetState())
    }

    fun settings(reduceMotion: Boolean): ShellPanelNavigationPlan {
        return panel(buildSettingsSheetState(reduceMotion))
    }

    fun message(recentLines: List<String>): ShellPanelNavigationPlan {
        return panel(buildMessageSheetState(recentLines))
    }

    fun activity(objectiveHub: ObjectiveHubState): ShellPanelNavigationPlan {
        return panel(buildActivitySheetState(objectiveHub))
    }

    fun map(
        roomTitle: String,
        exits: List<ExitSnapshot>,
        mapGrid: MapGridPacket?,
        trackedQuest: TrackedQuestState,
        loading: Boolean = false,
    ): ShellPanelNavigationPlan {
        return panel(buildMapSheetState(roomTitle, exits, mapGrid, trackedQuest, loading))
    }

    private fun panel(sheet: ShellSheetState): ShellPanelNavigationPlan {
        return ShellPanelNavigationPlan(sheet = sheet)
    }
}

internal data class ShellPanelNavigationPlan(
    val sheet: ShellSheetState,
)

internal fun ShellUiState.applyPanelNavigation(plan: ShellPanelNavigationPlan): ShellUiState {
    return copy(
        sheet = plan.sheet,
        dialog = null,
    )
}
