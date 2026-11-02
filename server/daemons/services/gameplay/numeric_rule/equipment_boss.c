/**
 * @file    numeric_rule/equipment_boss.c
 * @brief   装备耐久与首领阶段预算
 *
 * @par Description:
 * 含 {query_equipment_durability_budget_framework, query_equipment_durability_budget_report, query_boss_phase_budget_framework, query_boss_phase_budget_report}。
 *
 * 本模块是 equipment_boss 域纯规则片段，由 `numeric_rule_d.c` 继承；
 * 不持有 daemon 状态（nosave 全部留在 facade），
 * 配置经 facade 的 query_section/query_section_int 读取。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>
#include <game/skill.h>
#include <game_constants.h>
#include "defs.h"
int query_mapping_int(mapping source, string key, int default_value);
int query_numeric_budget_int(mapping defaults, mapping context, string key, int default_value);
mapping query_section(string section_name);
mapping query_equipment_durability_budget_framework()
{
    mapping framework;

    framework = query_section("equipment_durability_budget");
    return mapp(framework) ? copy(framework) : ([]);
}
mapping query_equipment_durability_budget_report(mapping context)
{
    mapping defaults;
    mapping framework;
    mapping weights;
    string action_type;
    int armor_loss;
    int armor_loss_total;
    int armor_slots;
    int block_over_budget;
    int limit;
    int missing;
    int repair_cost;
    int repair_cost_per_point;
    int score;
    int weapon_loss;

    if (!mapp(context))
        context = ([]);

    framework = query_equipment_durability_budget_framework();
    defaults = mapp(framework["defaults"]) ? framework["defaults"] : ([]);
    weights = mapp(framework["weights"]) ? framework["weights"] : ([]);
    action_type = stringp(context["action_type"]) && context["action_type"] != "" ? context["action_type"] : "combat_hit";
    weapon_loss = query_numeric_budget_int(defaults, context, "weapon_loss_per_hit", 1);
    armor_loss = query_numeric_budget_int(defaults, context, "armor_loss_per_hit", 1);
    /* L-174：执行侧对每个实装防具槽各磨损 armor_loss_per_hit 一次，
     * 预算按实装槽数计入总磨损；无 context 时按 1 个槽的旧口径兼容。 */
    armor_slots = intp(context["armor_slots"]) && context["armor_slots"] > 0
        ? context["armor_slots"] : 1;
    missing = query_numeric_budget_int(defaults, context, "repair_missing_durability", 0);
    repair_cost_per_point = query_numeric_budget_int(defaults, context, "repair_cost_per_missing_durability", 1);
    limit = query_numeric_budget_int(defaults, context, "budget_limit", 1200);
    block_over_budget = query_numeric_budget_int(defaults, context, "block_over_budget", 0);
    if (weapon_loss < 0) weapon_loss = 0;
    if (armor_loss < 0) armor_loss = 0;
    if (missing < 0) missing = 0;
    if (repair_cost_per_point < 0) repair_cost_per_point = 0;
    armor_loss_total = armor_loss * armor_slots;
    repair_cost = missing * repair_cost_per_point;

    score = weapon_loss * query_mapping_int(weights, "weapon_loss_weight", 120)
        + armor_loss_total * query_mapping_int(weights, "armor_loss_weight", 80)
        + repair_cost * query_mapping_int(weights, "repair_cost_weight", 4)
        + missing * query_mapping_int(weights, "missing_durability_weight", 2);

    return ([
        "ok": 1,
        "valid": limit <= 0 || score <= limit,
        "action_type": action_type,
        "weapon_loss_per_hit": weapon_loss,
        "armor_loss_per_hit": armor_loss,
        "armor_slots": armor_slots,
        "armor_loss_total": armor_loss_total,
        "repair_missing_durability": missing,
        "repair_cost_per_missing_durability": repair_cost_per_point,
        "repair_cost": repair_cost,
        "score": score,
        "limit": limit,
        "over_budget": limit > 0 && score > limit ? score - limit : 0,
        "budget_unit": defaults["budget_unit"] || "durability_economy_score",
        "block_apply": limit > 0 && score > limit && block_over_budget ? 1 : 0,
    ]);
}
mapping query_boss_phase_budget_framework()
{
    mapping framework;

    framework = query_section("boss_phase_budget");
    return mapp(framework) ? copy(framework) : ([]);
}
mapping query_boss_phase_budget_report(mapping context)
{
    mapping defaults;
    mapping framework;
    mapping weights;
    mixed *thresholds;
    int active_phase;
    int block_over_budget;
    int hp_ratio_bp;
    int limit;
    int max_phase_count;
    int reward_factor_bp;
    int score;
    int skill_window_count;
    int threshold;

    if (!mapp(context))
        context = ([]);

    framework = query_boss_phase_budget_framework();
    defaults = mapp(framework["defaults"]) ? framework["defaults"] : ([]);
    weights = mapp(framework["weights"]) ? framework["weights"] : ([]);
    thresholds = arrayp(context["phase_thresholds_bp"]) ? context["phase_thresholds_bp"] : defaults["phase_thresholds_bp"];
    if (!arrayp(thresholds))
        thresholds = ({ 7000, 4000, 1000 });
    hp_ratio_bp = query_numeric_budget_int(defaults, context, "hp_ratio_bp", BP_BASE);
    max_phase_count = query_numeric_budget_int(defaults, context, "max_phase_count", 4);
    skill_window_count = query_numeric_budget_int(defaults, context, "skill_window_count", 1);
    reward_factor_bp = query_numeric_budget_int(defaults, context, "reward_factor_bp", BP_BASE);
    limit = query_numeric_budget_int(defaults, context, "budget_limit", 1600);
    block_over_budget = query_numeric_budget_int(defaults, context, "block_over_budget", 0);
    if (hp_ratio_bp < 0) hp_ratio_bp = 0;
    if (hp_ratio_bp > BP_BASE) hp_ratio_bp = BP_BASE;
    if (max_phase_count < 1) max_phase_count = 1;
    if (skill_window_count < 0) skill_window_count = 0;

    active_phase = 1;
    foreach (threshold in thresholds) {
        if (intp(threshold) && hp_ratio_bp <= threshold)
            active_phase++;
    }
    if (active_phase > max_phase_count)
        active_phase = max_phase_count;

    score = max_phase_count * query_mapping_int(weights, "phase_count_weight", 200)
        + skill_window_count * query_mapping_int(weights, "skill_window_weight", 300)
        + (active_phase > 1 ? query_mapping_int(weights, "low_hp_phase_weight", 200) : 0)
        + reward_factor_bp * query_mapping_int(weights, "reward_factor_weight", 1) / BP_BASE;

    return ([
        "ok": 1,
        "valid": limit <= 0 || score <= limit,
        "hp_ratio_bp": hp_ratio_bp,
        "active_phase": active_phase,
        "max_phase_count": max_phase_count,
        "skill_window_count": skill_window_count,
        "reward_factor_bp": reward_factor_bp,
        "phase_thresholds_bp": thresholds,
        "score": score,
        "limit": limit,
        "over_budget": limit > 0 && score > limit ? score - limit : 0,
        "budget_unit": defaults["budget_unit"] || "boss_phase_score",
        "block_apply": limit > 0 && score > limit && block_over_budget ? 1 : 0,
    ]);
}
