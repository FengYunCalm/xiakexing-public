package com.xiakexing.client.feature.shell

import com.xiakexing.client.core.protocol.payload.ActiveSkillSlotPacket
import com.xiakexing.client.core.protocol.payload.PlayerStatusSnapshot
import com.xiakexing.client.core.protocol.payload.SkillBreakthroughChoicePacket
import com.xiakexing.client.core.protocol.payload.SkillBreakthroughEntryPacket
import com.xiakexing.client.core.protocol.payload.SkillEventPacket
import com.xiakexing.client.core.protocol.payload.SkillInfoPacket
import com.xiakexing.client.core.protocol.payload.SkillSummaryPacket

internal const val OPEN_SKILL_PANEL_CATEGORY_PREFIX = "ui://panel/skills/category/"
internal const val OPEN_SKILL_DETAIL_PREFIX = "ui://panel/skills/item/"
internal const val OPEN_SKILL_BREAKTHROUGH_PREFIX = "ui://panel/skills/breakthrough/"
internal const val SKILL_DETAIL_DIALOG_SOURCE = "skill_detail"
internal const val SKILL_BREAKTHROUGH_DIALOG_SOURCE = "skill_breakthrough"
internal const val DEFAULT_SKILL_PANEL_CATEGORY = "all"
internal const val SKILL_PANEL_TITLE = "技能"

internal fun ShellSheetState.isSkillWorkspaceRoot(): Boolean {
    return navigation.dockSection == DockWorkspaceSection.SKILL && navigation.routeKey == "skills"
}

internal fun buildSkillsSheetState(
    skills: List<SkillSummaryPacket>,
    categoryId: String,
    loading: Boolean = false,
    activeSkillSlots: Map<String, ActiveSkillSlotPacket> = emptyMap(),
    statusSnapshot: PlayerStatusSnapshot? = null,
): ShellSheetState {
    return buildSkillsSheetState(
        model =
            buildSkillPanelModel(
                skills = skills,
                highlightedSkillIds = activeSkillSlots.highlightedSkillIds(),
            ),
        categoryId = categoryId,
        loading = loading,
        statusSnapshot = statusSnapshot,
    )
}

internal fun buildSkillsSheetState(
    model: SkillPanelModel,
    categoryId: String,
    loading: Boolean = false,
    statusSnapshot: PlayerStatusSnapshot? = null,
): ShellSheetState {
    val categories = model.categories
    val selectedCategory =
        categories.firstOrNull { it.id == categoryId }
            ?: categories.firstOrNull()
    return buildGridSheet(
        source = PopupSource.DOCK_BUTTON,
        title = SKILL_PANEL_TITLE,
        showHeaderTitle = false,
        gridMode = ShellPanelMode.SKILL_GRID,
        panelTabs =
            categories.map { category ->
                ShellAction(
                    label = category.label,
                    command = "$OPEN_SKILL_PANEL_CATEGORY_PREFIX${category.id}",
                    dismissAfterInvoke = false,
                )
            },
        panelSelectedTabId = selectedCategory?.id.orEmpty(),
        panelCards = selectedCategory?.cards.orEmpty(),
        panelStats = emptyList(),
        panelEmptyText =
            if (model.allSkillCount == 0) {
                "尚未收录任何武学或技艺"
            } else {
                "当前分类暂无武学记录"
            },
        emptyPresentation = ShellSheetEmptyPresentation.MESSAGE,
        loading = loading,
        navigation =
            SheetNavigationMeta(
                dockSection = DockWorkspaceSection.SKILL,
                routeKey = "skills",
            ),
        kind = ShellSheetKind.SKILL,
        headerText = buildSkillHeaderText(statusSnapshot),
    )
}

// 取舍：头部在面板打开时取最新 status 快照（打开时新鲜）；打开期间 status 更新
// 不重建已打开面板，需重新打开或刷新才看到新值（用户已确认该口径）。
private fun buildSkillHeaderText(statusSnapshot: PlayerStatusSnapshot?): String {
    // 潜能/技能上限恒显示数值（服务端恒发，null 兜底 0），不显示 "--"。
    val skillCap = (statusSnapshot?.skillCap ?: 0L).toString()
    val potential = (statusSnapshot?.potential ?: 0L).toString()
    return "等级上限：$skillCap 潜能：$potential"
}

internal fun buildSkillDetailPopup(packet: SkillInfoPacket): ShellPopupState {
    return buildDockPopup(
        title = packet.name.ifBlank { packet.id },
        message = packet.detailMessage(),
        actions = packet.detailActions(),
        meta = packet.detailMeta(),
        primaryLabel = "",
    )
}

internal fun buildSkillDetailSheetState(packet: SkillInfoPacket): ShellSheetState {
    return buildInfoListSheet(
        source = PopupSource.DOCK_BUTTON,
        title = packet.name.ifBlank { packet.id },
        dialogId = packet.id.ifBlank { packet.name },
        dialogSource = SKILL_DETAIL_DIALOG_SOURCE,
        detailLines = packet.detailLines(),
        actions = packet.detailActions(),
        panelEmptyText = "暂无武学详情",
        loading = false,
        navigation =
            SheetNavigationMeta(
                dockSection = DockWorkspaceSection.SKILL,
                routeKey = "skills/detail/${packet.id.ifBlank { packet.name }}",
                depth = 1,
            ),
        kind = ShellSheetKind.SKILL,
    )
}

internal fun buildSkillBreakthroughSheetState(packet: SkillInfoPacket): ShellSheetState {
    return buildInfoListSheet(
        source = PopupSource.DOCK_BUTTON,
        title = "突破选择",
        subtitle = packet.name.ifBlank { packet.id },
        dialogSource = SKILL_BREAKTHROUGH_DIALOG_SOURCE,
        detailLines = packet.breakthroughOverviewLines(),
        items = packet.breakthroughChoiceItems(),
        panelEmptyText = "无可选突破",
        loading = false,
        navigation =
            SheetNavigationMeta(
                dockSection = DockWorkspaceSection.SKILL,
                routeKey = "skills/breakthrough/${packet.id.ifBlank { packet.name }}",
                depth = 2,
            ),
        kind = ShellSheetKind.SKILL,
    )
}

internal fun Map<String, Int>.formatSkillIntMap(): String {
    return entries
        .filter { (_, value) -> value != 0 }
        .joinToString("、") { (key, value) -> "${key.skillNumericLabel()} $value" }
}

internal fun Map<String, Long>.formatSkillLongMap(): String {
    return entries
        .filter { (_, value) -> value != 0L }
        .joinToString("、") { (key, value) -> "${key.skillNumericLabel()} $value" }
}

internal fun StringBuilder.appendSkillEventCosts(packet: SkillEventPacket) {
    packet.resourceCosts.formatSkillLongMap().takeIf { it.isNotBlank() }?.let { costs ->
        append("，消耗 $costs")
    }
}

internal fun StringBuilder.appendSkillEventEffects(packet: SkillEventPacket) {
    packet.effects.formatSkillLongMap().takeIf { it.isNotBlank() }?.let { effects ->
        append("，效果 $effects")
    }
    if (packet.statusEffects.isNotEmpty()) {
        append("，状态 ${packet.statusEffects.size}项")
    }
}

internal data class SkillPanelModel(
    val categories: List<SkillPanelCategory>,
    val allSkillCount: Int,
)

internal data class SkillPanelCategory(
    val id: String,
    val label: String,
    val cards: List<ShellPanelCard>,
)

internal fun buildSkillPanelModel(
    skills: List<SkillSummaryPacket>,
    highlightedSkillIds: Set<String> = emptySet(),
): SkillPanelModel {
    return SkillPanelModel(
        categories = buildSkillPanelCategories(skills, highlightedSkillIds),
        allSkillCount = skills.size,
    )
}

private fun buildSkillPanelCategories(
    skills: List<SkillSummaryPacket>,
    highlightedSkillIds: Set<String>,
): List<SkillPanelCategory> {
    if (skills.isEmpty()) {
        return emptyList()
    }

    val groupedSkills = linkedMapOf<String, MutableList<SkillSummaryPacket>>()
    groupedSkills["all"] = skills.toMutableList()
    skills.forEach { skill ->
        groupedSkills.getOrPut(skill.panelCategoryId()) { mutableListOf() } += skill
    }

    return groupedSkills.entries
        .sortedBy { (categoryId, _) -> skillCategorySortOrder(categoryId) }
        .map { (categoryId, entries) ->
            SkillPanelCategory(
                id = categoryId,
                label = skillCategoryLabel(categoryId),
                cards =
                    entries
                        .sortedWith(
                            compareByDescending<SkillSummaryPacket> { it.level }.thenBy { it.name },
                        )
                        .map { skill -> skill.toPanelCard(highlightedSkillIds) },
            )
        }
}

private fun SkillSummaryPacket.panelCategoryId(): String {
    resolveCombatSkillCategory(category, sourceTypes)?.let { categoryId ->
        return categoryId
    }
    if (!type.equals("basic", ignoreCase = true) && !type.equals("special", ignoreCase = true)) {
        val normalizedType = type.trim().lowercase()
        return normalizedType.ifBlank { "life" }
    }

    return sourceTypes.firstNotNullOfOrNull { sourceType -> sourceType.toCombatSkillCategoryId() } ?: "combat"
}

internal fun formatSkillCardTitle(
    displayName: String,
    level: Int,
): String = formatBracketedLabel(displayName, level)

private fun SkillSummaryPacket.toPanelCard(highlightedSkillIds: Set<String>): ShellPanelCard {
    val displayName = name.ifBlank { id }
    return ShellPanelCard(
        id = id.ifBlank { name },
        // 单行“名字[等级]”（如 吐纳法[100]）：数据层组装，渲染层保持哑。
        title = formatSkillCardTitle(displayName, level),
        bottomLabel = "",
        badges = emptyList(),
        highlighted = id.isNotBlank() && id in highlightedSkillIds,
        command = "$OPEN_SKILL_DETAIL_PREFIX${id.ifBlank { name }}",
        actionLabel = "详情",
    )
}

private fun Map<String, ActiveSkillSlotPacket>.highlightedSkillIds(): Set<String> {
    return values
        .asSequence()
        .map { slot -> slot.skillId.trim() }
        .filter { skillId -> skillId.isNotBlank() }
        .toSet()
}

private fun SkillInfoPacket.detailMessage(): String {
    return listOfNotNull(
        description.trim(),
        detailDescription.trim(),
        skillIntro?.summary?.trim(),
    )
        .filter { it.isNotBlank() }
        .joinToString("\n\n")
        .ifBlank { "这门武学当前等级 ${level}${if (cap > 0) "/$cap" else ""}。" }
}

private fun SkillInfoPacket.detailLines(): List<String> {
    val metaLine = detailMeta().joinToString(" · ")
    return listOf(metaLine).filter { it.isNotBlank() } + detailMessage().toParagraphs()
}

private fun SkillBreakthroughEntryPacket.breakthroughLabel(): String {
    val base = description.ifBlank { id }
    val effectsText = effects.formatSkillIntMap()
    return if (effectsText.isBlank()) base else "$base（$effectsText）"
}

private fun SkillInfoPacket.breakthroughChoiceItems(): List<ShellSheetItem> {
    val commandSkillId = id.ifBlank { name }
    return breakthroughChoices
        .sortedBy { choice -> choice.tier }
        .flatMap { choice -> choice.toBreakthroughChoiceItems(commandSkillId) }
}

private fun SkillBreakthroughChoicePacket.toBreakthroughChoiceItems(
    skillId: String,
): List<ShellSheetItem> {
    return candidates.map { entry ->
        ShellSheetItem(
            title = entry.breakthroughLabel(),
            trailing = "等级 $tier",
            command = "breakthrough $skillId $tier ${entry.id}",
            dismissAfterInvoke = true,
        )
    }
}

/** 技能核心属性行（等级/进度/伤害/消耗/冷却/条件）：详情与突破总览共用同一真源。 */
private fun SkillInfoPacket.coreStatLines(): List<String> {
    return buildList {
        add(if (cap > 0) "等级 $level/$cap" else "等级 $level")
        if (maxExp > 0) {
            add("进度 $exp/$maxExp")
        } else if (exp > 0) {
            add("经验 $exp")
        }
        if (damage.isNotBlank()) {
            add("伤害 $damage")
        }
        if (cost.isNotBlank()) {
            add("消耗 $cost")
        }
        if (cooldown > 0) {
            add("冷却 ${cooldown}秒")
        }
        if (requirements.isNotBlank()) {
            add("条件 $requirements")
        }
    }
}

private fun SkillInfoPacket.breakthroughOverviewLines(): List<String> = coreStatLines()

private fun SkillInfoPacket.detailMeta(): List<String> = coreStatLines()

private fun SkillInfoPacket.detailActions(): List<ShellAction> {
    val actions =
        buildList {
            if (breakthroughChoices.isNotEmpty()) {
                add(
                    ShellAction(
                        label = "选择突破",
                        command = "$OPEN_SKILL_BREAKTHROUGH_PREFIX${id.ifBlank { name }}",
                        dismissAfterInvoke = false,
                    ),
                )
            }
            if (canPractice) {
                add(
                    ShellAction(
                        label = "练习",
                        command = "practice $id",
                        dismissAfterInvoke = true,
                    ),
                )
            }
            activationOptions.values
                .sortedBy { option -> option.slot.activationSlotSortOrder() }
                .forEach { option ->
                    add(
                        ShellAction(
                            label = option.label.ifBlank { option.command.skillCommandLabel() },
                            command = option.command.trim(),
                            dismissAfterInvoke = true,
                        ),
                    )
                }
        }
    return actions
}

private fun String.skillCommandLabel(): String {
    return when {
        startsWith("activate outer ", ignoreCase = true) -> "激发"
        startsWith("activate parry ", ignoreCase = true) -> "激发招架"
        startsWith("activate force ", ignoreCase = true) -> "激发内功"
        startsWith("activate dodge ", ignoreCase = true) -> "激发轻功"
        startsWith("practice ", ignoreCase = true) -> "练习"
        startsWith("perform ", ignoreCase = true) -> "施展"
        startsWith("exert ", ignoreCase = true) -> "运功"
        else -> "执行"
    }
}

private fun String.activationSlotSortOrder(): Int {
    return when (trim().lowercase()) {
        "outer" -> 0
        "parry" -> 1
        "force" -> 2
        "dodge" -> 3
        else -> 9
    }
}

private fun SkillInfoPacket.panelCategoryId(): String {
    resolveCombatSkillCategory(category, sourceTypes)?.let { categoryId ->
        return categoryId
    }
    if (!type.equals("basic", ignoreCase = true) && !type.equals("special", ignoreCase = true)) {
        val normalizedType = type.trim().lowercase()
        return normalizedType.ifBlank { "life" }
    }

    return sourceTypes.firstNotNullOfOrNull { sourceType -> sourceType.toCombatSkillCategoryId() } ?: "combat"
}

private fun SkillSummaryPacket.isCombatSkillKind(): Boolean {
    return type.equals("combat", ignoreCase = true) ||
        type.equals("basic", ignoreCase = true) ||
        type.equals("special", ignoreCase = true)
}

private fun skillCategorySortOrder(categoryId: String): Int {
    return when (categoryId) {
        "all" -> 0
        "inner" -> 1
        "light" -> 2
        "fist" -> 3
        "cuff" -> 4
        "palm" -> 5
        "finger" -> 6
        "claw" -> 7
        "leg" -> 8
        "sword" -> 9
        "blade" -> 10
        "spear" -> 11
        "halberd" -> 12
        "club" -> 13
        "staff" -> 14
        "whip" -> 15
        "hammer" -> 16
        "axe" -> 17
        "dagger" -> 18
        "throwing" -> 19
        "archery" -> 20
        "hook" -> 21
        "weapon" -> 22
        "parry" -> 23
        "combat" -> 24
        "gathering" -> 30
        "alchemy" -> 31
        "crafting" -> 32
        "tailoring" -> 33
        "life" -> 34
        else -> 99
    }
}

private fun skillCategoryLabel(categoryId: String): String {
    return when (categoryId) {
        "all" -> "全部"
        "inner" -> "内功"
        "light" -> "轻功"
        "fist" -> "拳脚"
        "cuff" -> "拳法"
        "palm" -> "掌法"
        "finger" -> "指法"
        "claw" -> "爪法"
        "leg" -> "腿法"
        "sword" -> "剑法"
        "blade" -> "刀法"
        "spear" -> "枪法"
        "halberd" -> "戟法"
        "club" -> "棍法"
        "staff" -> "杖法"
        "whip" -> "鞭法"
        "hammer" -> "锤法"
        "axe" -> "斧法"
        "dagger" -> "短兵"
        "throwing" -> "暗器"
        "archery" -> "弓术"
        "hook" -> "钩法"
        "weapon" -> "兵器"
        "parry" -> "招架"
        "combat" -> "战斗"
        "gathering" -> "生活"
        "alchemy" -> "炼药"
        "crafting" -> "制造"
        "tailoring" -> "缝纫"
        "life" -> "生活"
        else -> categoryId.ifBlank { "其他" }
    }
}

private fun String.toCombatSkillCategoryId(): String? {
    return when (trim().lowercase()) {
        "force" -> "inner"
        "dodge" -> "light"
        "parry" -> "parry"
        "sword" -> "sword"
        "blade" -> "blade"
        "spear" -> "spear"
        "halberd" -> "halberd"
        "club" -> "club"
        "staff" -> "staff"
        "whip" -> "whip"
        "hammer" -> "hammer"
        "axe" -> "axe"
        "dagger" -> "dagger"
        "throwing" -> "throwing"
        "archery" -> "archery"
        "hook" -> "hook"
        "fist" -> "fist"
        "cuff" -> "cuff"
        "palm" -> "palm"
        "finger" -> "finger"
        "claw" -> "claw"
        "leg" -> "leg"
        else -> null
    }
}

private fun resolveCombatSkillCategory(
    category: String,
    sourceTypes: List<String>,
): String? {
    category.toCombatSkillCategoryId()?.let { return it }
    val categoryIsGeneric = category.isGenericWeaponCategory()
    if (category.isNotBlank() && !categoryIsGeneric) {
        return null
    }
    sourceTypes.firstNotNullOfOrNull {
            sourceType ->
        sourceType.toCombatSkillCategoryId()
    }?.let { return it }
    return "weapon".takeIf {
        categoryIsGeneric || sourceTypes.any { sourceType -> sourceType.isGenericWeaponCategory() }
    }
}

private fun String.isGenericWeaponCategory(): Boolean {
    return trim().lowercase() in setOf("weapon", "weapons", "arms", "armament")
}

private fun String.skillNumericLabel(): String {
    return when (trim().lowercase()) {
        "hp" -> "气血"
        "mp" -> "内力"
        "ep" -> "精力"
        "attack" -> "攻击"
        "defense" -> "防御"
        "parry" -> "招架"
        "hit" -> "命中"
        "dodge" -> "闪避"
        "critical" -> "暴击"
        "critical_resist" -> "抗暴"
        "armor_break" -> "破甲"
        "attack_speed" -> "攻速"
        "damage_bonus" -> "伤害加成"
        "hit_bonus" -> "命中加成"
        "parry_bonus" -> "招架加成"
        "shield_bonus" -> "护体加成"
        "control_bonus" -> "控制加成"
        "debuff_bonus" -> "减益加成"
        "dot_bonus" -> "持续伤害"
        "recover_hp" -> "恢复气血"
        "recover_mp" -> "恢复内力"
        "recover_ep" -> "恢复精力"
        else -> this
    }
}
