/**
 * @file    numeric_rule/equipment_budget.c
 * @brief   装备词条与模板预算
 *
 * @par Description:
 * 含 {normalize_equipment_budget_stat, query_equipment_stat_budget_weight, query_equipment_effect_budget_score, query_equipment_quality_factor_bp, query_equipment_slot_base_budget, query_equipment_enhance_effects, query_equipment_template_budget}。
 *
 * 本模块是 equipment_budget 域纯规则片段，由 `numeric_rule_d.c` 继承；
 * 不持有 daemon 状态（nosave 全部留在 facade），
 * 配置经 facade 的 query_section/query_section_int 读取。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>
#include <game/skill.h>
#include <game_constants.h>
#include "defs.h"
mapping query_equipment_budget_framework();
int query_exp_required_for_skill_cap(int wanted_skill_cap);
int query_mapping_int(mapping source, string key, int default_value);
private string normalize_equipment_budget_stat(string stat)
{
    switch (stat) {
    case "defense":
        return "armor";
    default:
        return stat;
    }
}
int query_equipment_stat_budget_weight(string stat)
{
    mapping framework;
    mapping weights;
    int weight;

    stat = normalize_equipment_budget_stat(stat);
    if (!stringp(stat) || stat == "")
        return 0;

    framework = query_equipment_budget_framework();
    weights = framework["stat_weights"];
    weight = mapp(weights) ? (weights[stat] || 0) : 0;
    return weight > 0 ? weight : 0;
}
int query_equipment_effect_budget_score(mapping effects)
{
    string stat;
    int score;
    int value;
    int weight;

    foreach (stat, value in effects || ([ ])) {
        if (!stringp(stat) || !intp(value))
            continue;
        weight = query_equipment_stat_budget_weight(stat);
        if (weight <= 0)
            continue;
        if (value < 0)
            value = -value;
        score += value * weight;
    }
    return score;
}
private int query_equipment_quality_factor_bp(int quality)
{
    mapping factors;
    mapping framework;
    int factor;

    framework = query_equipment_budget_framework();
    factors = framework["quality_factor_bp"];
    factor = mapp(factors) ? (factors[quality] || 0) : 0;
    return factor > 0 ? factor : BP_BASE;
}
private int query_equipment_slot_base_budget(string item_type, string sub_type)
{
    mapping framework;
    mapping slot_budgets;
    mapping type_budgets;
    int budget;

    framework = query_equipment_budget_framework();
    slot_budgets = framework["slot_budgets"];
    type_budgets = mapp(slot_budgets) ? slot_budgets[item_type] : 0;
    if (!mapp(type_budgets))
        return 0;

    budget = stringp(sub_type) && sub_type != "" ? (type_budgets[sub_type] || 0) : 0;
    if (budget <= 0)
        budget = type_budgets["*"] || 0;
    return budget > 0 ? budget : 0;
}
private mapping query_equipment_enhance_effects(string item_type, int enhance_level)
{
    mapping effects;
    mapping framework;
    mapping per_level;
    string stat;
    int value;

    if (!stringp(item_type) || item_type == "" || enhance_level <= 0)
        return ([]);

    framework = query_equipment_budget_framework();
    per_level = mapp(framework["enhance_effects_per_level"])
        ? framework["enhance_effects_per_level"][item_type]
        : 0;
    if (!mapp(per_level))
        return ([]);

    effects = ([]);
    foreach (stat, value in per_level) {
        if (stringp(stat) && intp(value))
            effects[stat] = value * enhance_level;
    }
    return effects;
}
mapping query_equipment_template_budget(string item_id, int enhance_level)
{
    mapping defaults;
    mapping effects;
    mapping enhance_effects;
    mapping framework;
    mapping requirements;
    mapping template;
    string item_type;
    string sub_type;
    int affix_factor;
    int affix_limit;
    int base_budget;
    int base_score;
    int enhance_score;
    int skill_cap_factor;
    int quality;
    int required_skill_cap;
    int total_budget;

    if (!stringp(item_id) || item_id == "")
        return ([ "ok": 0, "error": "invalid_item_id" ]);

    template = ITEM_D->query_item_template(item_id);
    if (!mapp(template))
        return ([ "ok": 0, "error": "unknown_item", "item_id": item_id ]);

    item_type = template["type"] || "misc";
    if (item_type != "weapon" && item_type != "armor")
        return ([ "ok": 0, "error": "not_equipment", "item_id": item_id, "type": item_type ]);

    sub_type = template["armor_type"] || template["sub_type"] || "";
    quality = template["quality"] || 1;
    if (quality <= 0)
        quality = 1;
    requirements = mapp(template["requirements"]) ? template["requirements"] : ([]);
    required_skill_cap = requirements["skill_cap"] || 100;
    if (required_skill_cap < 100)
        required_skill_cap = 100;
    if (enhance_level < 0)
        enhance_level = 0;

    framework = query_equipment_budget_framework();
    defaults = mapp(framework["defaults"]) ? framework["defaults"] : ([]);
    base_budget = query_equipment_slot_base_budget(item_type, sub_type);
    skill_cap_factor = BP_BASE + (required_skill_cap / 100 - 1)
        * query_mapping_int(defaults, "skill_cap_factor_bp_per_100_cap", 250);
    total_budget = base_budget * query_equipment_quality_factor_bp(quality) / BP_BASE;
    total_budget = total_budget * skill_cap_factor / BP_BASE;
    effects = mapp(template["effects"]) ? template["effects"] : ([]);
    enhance_effects = query_equipment_enhance_effects(item_type, enhance_level);
    base_score = query_equipment_effect_budget_score(effects);
    enhance_score = query_equipment_effect_budget_score(enhance_effects);
    affix_factor = query_mapping_int(defaults, "affix_budget_factor_bp", 3500);
    affix_limit = total_budget * affix_factor / BP_BASE;

    return ([
        "ok": 1,
        "item_id": item_id,
        "type": item_type,
        "sub_type": sub_type,
        "quality": quality,
        "required_skill_cap": required_skill_cap,
        "required_combat_exp": query_exp_required_for_skill_cap(required_skill_cap),
        "enhance_level": enhance_level,
        "base_budget": base_budget,
        "total_budget": total_budget,
        "base_effect_score": base_score,
        "enhance_effect_score": enhance_score,
        "used_budget": base_score + enhance_score,
        "remaining_budget": total_budget > base_score + enhance_score ? total_budget - base_score - enhance_score : 0,
        "affix_budget_limit": affix_limit,
        "max_affixes": query_mapping_int(defaults, "max_affixes", 4),
        "enhance_effects": enhance_effects,
        "budget_only": 1,
    ]);
}
