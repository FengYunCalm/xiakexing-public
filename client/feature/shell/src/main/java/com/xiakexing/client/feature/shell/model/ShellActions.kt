package com.xiakexing.client.feature.shell

import androidx.compose.runtime.Immutable

@Immutable
data class ServerOption(
    val id: String,
    val label: String,
    val description: String,
    val host: String,
    val port: Int,
    val path: String,
    val secure: Boolean = true,
) {
    val endpointSummary: String
        get() = "$host:$port$path"
}

@Immutable
data class InteractionObject(
    val id: String,
    val uiKey: String = id,
    val title: String,
    val subtitle: String,
    val summary: String,
    val kind: InteractionObjectKind,
    val description: String = "",
    val questAvailable: Boolean = false,
    val questMarkerOnly: Boolean = false,
    val availableQuestIds: List<String> = emptyList(),
    val actionGroups: List<ShellActionGroup> = emptyList(),
)

@Immutable
data class TitleActionItem(
    val type: TitleActionType,
    val label: String,
)

@Immutable
data class ShellAction(
    val label: String,
    val command: String,
    val dismissAfterInvoke: Boolean = true,
    val kind: String = "",
    val surface: String = "",
    val role: String = "",
    val targetGroup: String = "",
    val supportingLabel: String = "",
    val longClickCommand: String = "",
    val commandSequence: List<String> = emptyList(),
    /** 服务端声明的当前步骤独立动作；交物仍使用固定给予入口。 */
    val guided: Boolean = false,
)

/** 交物只进入给予选择器，不形成独立任务动作。 */
val ShellAction.isGiveAction: Boolean
    get() = kind.equals("give", ignoreCase = true) || command.trim().substringBefore(' ').equals("give", ignoreCase = true)

/** 步骤指引的单一判定，供动作分区、组作用域和任务面板共同使用。 */
val ShellAction.isStepGuidance: Boolean
    get() = guided && command.isNotBlank() && !isGiveAction

@Immutable
data class ShellActionGroup(
    val id: String,
    val title: String,
    val surface: String,
    val actions: List<ShellAction> = emptyList(),
)
