/**
 * @file    numeric_rule/status_effect.c
 * @brief   状态效果预算与抗性
 *
 * @par Description:
 * 含 {query_combat_status_effect_framework, query_combat_status_effect_budget_framework, query_combat_status_effect_resistance_framework, query_status_effect_budget_int, query_status_effect_int, query_status_effect_type_factor_bp, query_status_effect_resource_weight, query_status_effect_stat_weight, absolute_status_effect_value, scale_status_effect_value, scale_status_effect_mapping, query_combat_status_effect_budget_score, query_combat_status_effect_budget_report, query_status_effect_resist_attribute, query_combat_status_effect_resistance_state, query_status_resist_factor_bp, query_status_effect_resisted_factor, apply_combat_status_effect_resistance}。
 *
 * 本模块是 status_effect 域纯规则片段，由 `numeric_rule_d.c` 继承；
 * 不持有 daemon 状态（nosave 全部留在 facade），
 * 配置经 facade 的 query_section/query_section_int 读取。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>
#include <game/skill.h>
#include <game_constants.h>
#include "defs.h"
varargs mapping query_derived_combat_attributes(object player, object target);
int query_mapping_int(mapping source, string key, int default_value);
mapping query_section(string section_name);
mapping query_combat_status_effect_framework()
{
    mapping framework;

    framework = query_section("combat_status_effect");
    return mapp(framework) ? copy(framework) : ([]);
}
mapping query_combat_status_effect_budget_framework()
{
    mapping budget;
    mapping framework;

    framework = query_combat_status_effect_framework();
    budget = mapp(framework) ? framework["budget"] : 0;
    return mapp(budget) ? copy(budget) : ([]);
}
mapping query_combat_status_effect_resistance_framework()
{
    mapping framework;
    mapping resistance;

    framework = query_combat_status_effect_framework();
    resistance = mapp(framework) ? framework["resistance"] : 0;
    return mapp(resistance) ? copy(resistance) : ([]);
}
private int query_status_effect_budget_int(string key, int default_value)
{
    mapping budget;

    budget = query_combat_status_effect_budget_framework();
    return query_mapping_int(budget, key, default_value);
}
private int query_status_effect_int(mapping effect, mapping defaults, string key, int default_value)
{
    if (mapp(effect) && intp(effect[key]))
        return effect[key];
    if (mapp(defaults) && intp(defaults[key]))
        return defaults[key];
    return default_value;
}
private int query_status_effect_type_factor_bp(string effect_type)
{
    mapping budget;
    mapping factors;

    budget = query_combat_status_effect_budget_framework();
    factors = mapp(budget["type_factor_bp"]) ? budget["type_factor_bp"] : ([]);
    return query_mapping_int(factors, effect_type, BP_BASE);
}
private int query_status_effect_resource_weight(string resource)
{
    mapping budget;
    mapping weights;

    budget = query_combat_status_effect_budget_framework();
    weights = mapp(budget["resource_weights"]) ? budget["resource_weights"] : ([]);
    return query_mapping_int(weights, resource,
        query_status_effect_budget_int("damage_weight", 20));
}
private int query_status_effect_stat_weight(string key)
{
    mapping budget;
    mapping weights;

    budget = query_combat_status_effect_budget_framework();
    weights = mapp(budget["stat_weights"]) ? budget["stat_weights"] : ([]);
    return query_mapping_int(weights, key,
        query_status_effect_budget_int("default_stat_weight", 100));
}
 int absolute_status_effect_value(int value)
{
    return value < 0 ? -value : value;
}
private int scale_status_effect_value(int value, int factor_bp)
{
    return value * factor_bp / BP_BASE;
}
private mapping scale_status_effect_mapping(mapping values, int factor_bp)
{
    mapping result;
    string key;
    int value;

    result = ([]);
    foreach (key, value in values || ([])) {
        if (!stringp(key) || !intp(value))
            continue;
        result[key] = scale_status_effect_value(value, factor_bp);
    }
    return result;
}
int query_combat_status_effect_budget_score(mapping effect)
{
    mapping defaults;
    mapping resource_damage;
    mapping stat_effects;
    string effect_type;
    string key;
    int base_score;
    int declared_score;
    int duration;
    int duration_factor;
    int max_stacks;
    int score;
    int stack_factor;
    int tick_interval;
    int type_factor;
    int value;

    if (!mapp(effect))
        return 0;

    defaults = query_combat_status_effect_framework()["defaults"] || ([]);
    effect_type = effect["type"] || "debuff";
    duration = query_status_effect_int(effect, defaults, "duration_rounds", 1);
    if (intp(effect["remaining_rounds"]))
        duration = effect["remaining_rounds"];
    if (duration < 1)
        duration = 1;
    tick_interval = query_status_effect_int(effect, defaults, "tick_interval_rounds", 1);
    if (tick_interval < 1)
        tick_interval = 1;
    max_stacks = query_status_effect_int(effect, defaults, "max_stacks", 1);
    if (max_stacks < 1)
        max_stacks = 1;

    base_score = (effect["damage"] || 0) * query_status_effect_budget_int("damage_weight", 20) / tick_interval;
    resource_damage = effect["resource_damage"];
    foreach (key, value in resource_damage || ([])) {
        if (stringp(key) && intp(value))
            base_score += absolute_status_effect_value(value) * query_status_effect_resource_weight(key) / tick_interval;
    }
    stat_effects = mapp(effect["stat_effects"]) ? effect["stat_effects"] : effect["effects"];
    foreach (key, value in stat_effects || ([])) {
        if (stringp(key) && intp(value))
            base_score += absolute_status_effect_value(value) * query_status_effect_stat_weight(key);
    }
    if (effect["action_block"] || effect_type == "control")
        base_score += query_status_effect_budget_int("action_block_score", 800);

    duration_factor = BP_BASE + (duration - 1)
        * query_status_effect_budget_int("duration_score_factor_bp", BP_BASE);
    stack_factor = BP_BASE + (max_stacks - 1)
        * query_status_effect_budget_int("stack_score_factor_bp", 5000);
    type_factor = query_status_effect_type_factor_bp(effect_type);
    score = base_score * duration_factor / BP_BASE;
    score = score * stack_factor / BP_BASE;
    score = score * type_factor / BP_BASE;
    declared_score = intp(effect["budget_score"]) ? effect["budget_score"] : 0;
    return declared_score > score ? declared_score : score;
}
mapping query_combat_status_effect_budget_report(mapping effect)
{
    string effect_type;
    int limit;
    int score;
    int type_factor;

    if (!mapp(effect))
        return ([ "ok": 0, "error": "invalid_effect" ]);

    effect_type = effect["type"] || "debuff";
    type_factor = query_status_effect_type_factor_bp(effect_type);
    limit = query_status_effect_budget_int("base_budget", 500) * type_factor / BP_BASE;
    score = query_combat_status_effect_budget_score(effect);
    return ([
        "ok": 1,
        "valid": score <= limit,
        "type": effect_type,
        "score": score,
        "limit": limit,
        "over_budget": score > limit ? score - limit : 0,
        "budget_unit": "effect_score",
    ]);
}
private string query_status_effect_resist_attribute(string effect_type, mapping effect)
{
    mapping attributes;
    mapping resistance;
    string attribute;

    if (mapp(effect) && stringp(effect["resist_attribute"]) && effect["resist_attribute"] != "")
        return effect["resist_attribute"];
    resistance = query_combat_status_effect_resistance_framework();
    attributes = mapp(resistance["type_resist_attributes"]) ? resistance["type_resist_attributes"] : ([]);
    attribute = attributes[effect_type];
    return stringp(attribute) ? attribute : "";
}
mapping query_combat_status_effect_resistance_state(object target, string effect_type, mapping effect)
{
    mapping attrs;
    mapping resistance;
    string attribute;
    int max_resist;
    int resist_bp;

    if (!stringp(effect_type) || effect_type == "")
        effect_type = mapp(effect) && stringp(effect["type"]) ? effect["type"] : "debuff";

    resistance = query_combat_status_effect_resistance_framework();
    max_resist = query_mapping_int(resistance, "max_resist_bp", 7000);
    attribute = query_status_effect_resist_attribute(effect_type, effect);
    if (objectp(target) && attribute != "") {
        attrs = query_derived_combat_attributes(target);
        resist_bp = attrs[attribute] || target->query("combat/" + attribute) || target->query(attribute) || 0;
    }
    if (resist_bp < 0)
        resist_bp = 0;
    if (max_resist > 0 && resist_bp > max_resist)
        resist_bp = max_resist;
    return ([
        "ok": 1,
        "type": effect_type,
        "attribute": attribute,
        "resist_bp": resist_bp,
        "max_resist_bp": max_resist,
    ]);
}
private int query_status_resist_factor_bp(mapping resistance, string key, int default_value)
{
    int factor;

    factor = query_mapping_int(resistance, key, default_value);
    if (factor < 0)
        factor = 0;
    return factor;
}
private int query_status_effect_resisted_factor(int resist_bp, int resist_factor_bp)
{
    int effective_resist;

    effective_resist = resist_bp * resist_factor_bp / BP_BASE;
    if (effective_resist < 0)
        effective_resist = 0;
    if (effective_resist > BP_BASE)
        effective_resist = BP_BASE;
    return BP_BASE - effective_resist;
}
mapping apply_combat_status_effect_resistance(mapping effect, object target)
{
    mapping adjusted;
    mapping resistance;
    mapping state;
    string effect_type;
    int damage_factor;
    int duration;
    int duration_factor;
    int stat_factor;

    if (!mapp(effect))
        return ([]);

    adjusted = copy(effect);
    effect_type = adjusted["type"] || "debuff";
    state = query_combat_status_effect_resistance_state(target, effect_type, adjusted);
    resistance = query_combat_status_effect_resistance_framework();
    duration_factor = query_status_effect_resisted_factor(state["resist_bp"] || 0,
        query_status_resist_factor_bp(resistance, "duration_resist_factor_bp", BP_BASE));
    damage_factor = query_status_effect_resisted_factor(state["resist_bp"] || 0,
        query_status_resist_factor_bp(resistance, "damage_resist_factor_bp", BP_BASE));
    stat_factor = query_status_effect_resisted_factor(state["resist_bp"] || 0,
        query_status_resist_factor_bp(resistance, "stat_resist_factor_bp", BP_BASE));

    duration = adjusted["remaining_rounds"] || adjusted["duration_rounds"] || 1;
    if (duration > 1) {
        duration = duration * duration_factor / BP_BASE;
        if (duration < 1)
            duration = 1;
        adjusted["remaining_rounds"] = duration;
    }
    if (intp(adjusted["damage"]))
        adjusted["damage"] = scale_status_effect_value(adjusted["damage"], damage_factor);
    if (mapp(adjusted["resource_damage"]))
        adjusted["resource_damage"] = scale_status_effect_mapping(adjusted["resource_damage"], damage_factor);
    if (mapp(adjusted["stat_effects"])) {
        adjusted["stat_effects"] = scale_status_effect_mapping(adjusted["stat_effects"], stat_factor);
        adjusted["effects"] = adjusted["stat_effects"];
    } else if (mapp(adjusted["effects"])) {
        adjusted["effects"] = scale_status_effect_mapping(adjusted["effects"], stat_factor);
    }
    adjusted["resistance"] = state;
    return adjusted;
}
