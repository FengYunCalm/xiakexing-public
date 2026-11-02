package com.xiakexing.client.feature.shell

internal class ShellSurfaceCommandRouter {
    fun routeCommand(command: String): ShellSurfaceCommandRoute? {
        val normalizedCommand = command.trim()
        return when {
            normalizedCommand == OPEN_INTERACTION_GIVE_ITEMS_COMMAND -> ShellSurfaceCommandRoute.InteractionGiveItems
            normalizedCommand == OPEN_INTERACTION_OBJECT_ROOT_COMMAND -> ShellSurfaceCommandRoute.InteractionObjectRoot
            normalizedCommand == OPEN_INTERACTION_ROOT_COMMAND -> ShellSurfaceCommandRoute.InteractionRoot
            normalizedCommand == OPEN_INTERACTION_APPRENTICE_UNAVAILABLE_COMMAND -> ShellSurfaceCommandRoute.InteractionApprenticeUnavailable
            normalizedCommand.startsWith(OPEN_INTERACTION_UNAVAILABLE_ACTION_PREFIX) -> {
                ShellSurfaceCommandRoute.InteractionUnavailableAction(
                    normalizedCommand.removePrefix(OPEN_INTERACTION_UNAVAILABLE_ACTION_PREFIX),
                )
            }
            normalizedCommand == OPEN_BANK_ROOT_COMMAND -> ShellSurfaceCommandRoute.BankRoot
            normalizedCommand == OPEN_BANK_DEPOSIT_COMMAND -> ShellSurfaceCommandRoute.BankDeposit
            normalizedCommand == OPEN_BANK_WITHDRAW_COMMAND -> ShellSurfaceCommandRoute.BankWithdraw
            normalizedCommand == OPEN_QUEST_LIST_COMMAND -> ShellSurfaceCommandRoute.QuestList
            normalizedCommand == OPEN_ACTIVITY_PANEL_COMMAND -> ShellSurfaceCommandRoute.ActivityPanel
            normalizedCommand.startsWith(OPEN_QUEST_DETAIL_PREFIX) -> {
                ShellSurfaceCommandRoute.QuestDetail(
                    normalizedCommand.removePrefix(OPEN_QUEST_DETAIL_PREFIX),
                )
            }
            normalizedCommand.startsWith(TRACK_QUEST_PREFIX) -> {
                ShellSurfaceCommandRoute.TrackQuest(
                    normalizedCommand.removePrefix(TRACK_QUEST_PREFIX),
                )
            }
            normalizedCommand == UNTRACK_QUEST_COMMAND -> ShellSurfaceCommandRoute.ClearTrackedQuest
            normalizedCommand == SET_REDUCE_MOTION_COMMAND -> ShellSurfaceCommandRoute.SetReduceMotion
            normalizedCommand == OPEN_CHARACTER_STATUS_COMMAND -> ShellSurfaceCommandRoute.CharacterPanel
            normalizedCommand == OPEN_CHARACTER_ATTRIBUTES_COMMAND -> ShellSurfaceCommandRoute.AttributePanel
            normalizedCommand == OPEN_INVENTORY_PANEL_COMMAND -> ShellSurfaceCommandRoute.InventoryPanel
            normalizedCommand == OPEN_EQUIPMENT_PANEL_COMMAND -> ShellSurfaceCommandRoute.EquipmentPanel
            normalizedCommand == OPEN_WORLD_MAP_COMMAND -> ShellSurfaceCommandRoute.MapPanel
            normalizedCommand == OPEN_WORLD_MAIL_COMMAND || normalizedCommand == OPEN_WORLD_MESSAGES_COMMAND -> ShellSurfaceCommandRoute.MessagePanel
            normalizedCommand == OPEN_WORLD_SHOP_COMMAND -> ShellSurfaceCommandRoute.WorldShopGuidePanel
            normalizedCommand == OPEN_SETTINGS_PANEL_COMMAND -> ShellSurfaceCommandRoute.SettingsPanel
            normalizedCommand == OPEN_COMBAT_SHORTCUTS_COMMAND -> ShellSurfaceCommandRoute.ActionHubPanel
            normalizedCommand == OPEN_COMBAT_SKILLS_COMMAND -> ShellSurfaceCommandRoute.SkillsPanel
            normalizedCommand.startsWith(OPEN_COMBAT_PERFORM_PICKER_COMMAND) -> {
                ShellSurfaceCommandRoute.CombatPerformPicker(
                    normalizedCommand.removePrefix(OPEN_COMBAT_PERFORM_PICKER_COMMAND).toIntOrNull() ?: 0,
                )
            }
            normalizedCommand.startsWith(OPEN_COMBAT_PERFORM_SKILL_PICKER_COMMAND) -> {
                val parts =
                    normalizedCommand.removePrefix(
                        OPEN_COMBAT_PERFORM_SKILL_PICKER_COMMAND,
                    ).split("/", limit = 2)
                ShellSurfaceCommandRoute.CombatPerformSkillPicker(
                    slotIndex = parts.getOrNull(0)?.toIntOrNull() ?: 0,
                    skillId = parts.getOrNull(1).orEmpty(),
                )
            }
            normalizedCommand.startsWith(ASSIGN_COMBAT_PERFORM_PREFIX) -> {
                ShellSurfaceCommandRoute.AssignCombatPerform(normalizedCommand)
            }
            normalizedCommand.startsWith(REMOVE_COMBAT_PERFORM_PREFIX) -> {
                ShellSurfaceCommandRoute.RemoveCombatPerform(
                    normalizedCommand.removePrefix(REMOVE_COMBAT_PERFORM_PREFIX),
                )
            }
            normalizedCommand.equals(
                "skills",
                ignoreCase = true,
            ) -> ShellSurfaceCommandRoute.SkillsPanel
            normalizedCommand.startsWith(OPEN_HELP_PANEL_CATEGORY_PREFIX) -> {
                ShellSurfaceCommandRoute.HelpCategory(
                    normalizedCommand.removePrefix(OPEN_HELP_PANEL_CATEGORY_PREFIX),
                )
            }
            normalizedCommand.startsWith(OPEN_HELP_ITEM_DETAIL_PREFIX) -> {
                ShellSurfaceCommandRoute.HelpDetail(
                    normalizedCommand.removePrefix(OPEN_HELP_ITEM_DETAIL_PREFIX),
                )
            }
            normalizedCommand.startsWith(OPEN_INVENTORY_ITEM_DETAIL_PREFIX) -> {
                ShellSurfaceCommandRoute.InventoryItemDetail(
                    normalizedCommand.removePrefix(OPEN_INVENTORY_ITEM_DETAIL_PREFIX),
                )
            }
            normalizedCommand.startsWith(
                OPEN_INVENTORY_CATEGORY_PREFIX,
            ) ->
                ShellSurfaceCommandRoute.InventoryCategory(
                    normalizedCommand.removePrefix(OPEN_INVENTORY_CATEGORY_PREFIX),
                )
            normalizedCommand.startsWith(OPEN_LEARNING_SKILL_CATEGORY_PREFIX) -> {
                ShellSurfaceCommandRoute.LearningSkillCategory(
                    normalizedCommand.removePrefix(OPEN_LEARNING_SKILL_CATEGORY_PREFIX),
                )
            }
            normalizedCommand.startsWith(OPEN_SKILL_PANEL_CATEGORY_PREFIX) -> {
                ShellSurfaceCommandRoute.SkillCategory(
                    normalizedCommand.removePrefix(OPEN_SKILL_PANEL_CATEGORY_PREFIX),
                )
            }
            normalizedCommand.startsWith(OPEN_SKILL_BREAKTHROUGH_PREFIX) -> {
                ShellSurfaceCommandRoute.SkillBreakthrough(
                    normalizedCommand.removePrefix(OPEN_SKILL_BREAKTHROUGH_PREFIX),
                )
            }
            normalizedCommand.startsWith(OPEN_SKILL_DETAIL_PREFIX) -> {
                ShellSurfaceCommandRoute.SkillDetail(
                    normalizedCommand.removePrefix(OPEN_SKILL_DETAIL_PREFIX),
                )
            }
            else -> null
        }
    }

    fun routeExitOverlayLabel(label: String): ShellSurfaceCommandRoute? {
        return when (label.trim()) {
            "背包", "包裹" -> ShellSurfaceCommandRoute.InventoryPanel
            "装备" -> ShellSurfaceCommandRoute.EquipmentPanel
            "人物" -> ShellSurfaceCommandRoute.CharacterPanel
            "属性" -> ShellSurfaceCommandRoute.AttributePanel
            "邮箱" -> ShellSurfaceCommandRoute.MessagePanel
            "商店" -> ShellSurfaceCommandRoute.WorldShopGuidePanel
            "活动" -> ShellSurfaceCommandRoute.ActivityPanel
            "地图" -> ShellSurfaceCommandRoute.MapPanel
            "功能" -> ShellSurfaceCommandRoute.FunctionPanel
            else -> null
        }
    }
}

internal sealed interface ShellSurfaceCommandRoute {
    data object InteractionGiveItems : ShellSurfaceCommandRoute

    data object InteractionObjectRoot : ShellSurfaceCommandRoute

    data object InteractionRoot : ShellSurfaceCommandRoute

    data object InteractionApprenticeUnavailable : ShellSurfaceCommandRoute

    data class InteractionUnavailableAction(val actionKey: String) : ShellSurfaceCommandRoute

    data object BankRoot : ShellSurfaceCommandRoute

    data object BankDeposit : ShellSurfaceCommandRoute

    data object BankWithdraw : ShellSurfaceCommandRoute

    data object QuestList : ShellSurfaceCommandRoute

    data object ActivityPanel : ShellSurfaceCommandRoute

    data class QuestDetail(val questId: String) : ShellSurfaceCommandRoute

    data class TrackQuest(val questId: String) : ShellSurfaceCommandRoute

    data object ClearTrackedQuest : ShellSurfaceCommandRoute

    /** 设置内「减少动态效果」开关 */
    data object SetReduceMotion : ShellSurfaceCommandRoute

    data object CharacterPanel : ShellSurfaceCommandRoute

    data object AttributePanel : ShellSurfaceCommandRoute

    data object InventoryPanel : ShellSurfaceCommandRoute

    data object EquipmentPanel : ShellSurfaceCommandRoute

    data object FunctionPanel : ShellSurfaceCommandRoute

    data object MapPanel : ShellSurfaceCommandRoute

    data object MessagePanel : ShellSurfaceCommandRoute

    data object WorldShopGuidePanel : ShellSurfaceCommandRoute

    data object SettingsPanel : ShellSurfaceCommandRoute

    data object ActionHubPanel : ShellSurfaceCommandRoute

    data object SkillsPanel : ShellSurfaceCommandRoute

    data class CombatPerformPicker(val slotIndex: Int) : ShellSurfaceCommandRoute

    data class CombatPerformSkillPicker(val slotIndex: Int, val skillId: String) : ShellSurfaceCommandRoute

    data class AssignCombatPerform(val command: String) : ShellSurfaceCommandRoute

    data class RemoveCombatPerform(val skillId: String) : ShellSurfaceCommandRoute

    data class HelpCategory(val categoryId: String) : ShellSurfaceCommandRoute

    data class HelpDetail(val entryId: String) : ShellSurfaceCommandRoute

    data class InventoryItemDetail(val itemId: String) : ShellSurfaceCommandRoute

    data class InventoryCategory(val categoryId: String) : ShellSurfaceCommandRoute

    data class SkillCategory(val categoryId: String) : ShellSurfaceCommandRoute

    data class SkillDetail(val skillId: String) : ShellSurfaceCommandRoute

    data class SkillBreakthrough(val skillId: String) : ShellSurfaceCommandRoute

    data class LearningSkillCategory(val categoryId: String) : ShellSurfaceCommandRoute
}
