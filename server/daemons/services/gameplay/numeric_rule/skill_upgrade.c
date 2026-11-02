/**
 * @file    numeric_rule/skill_upgrade.c
 * @brief   技能升级消耗与宗师效果
 *
 * @par Description:
 * 含 {query_skill_action_framework, query_skill_action_type_config, normalize_skill_action_definition, query_skill_action_effects, clamp_resource_value, query_action_int, query_action_string, query_action_mapping, query_skill_effective_count_units, query_skill_breadth_factor_bp, query_comprehension_discount_bp, query_skill_upgrade_cost, query_skill_train_status, query_skill_level_signature, query_weighted_category_skill_level_map, query_weighted_category_skill_level, is_outer_skill_category, special_skill_is_activated, query_skill_passive_weight_bp, insert_descending_level, query_grandmaster_required_skill_level, query_grandmaster_exp_floor, query_grandmaster_raw_experience_units, query_grandmaster_experience_units, query_grandmaster_skill_kind_weight_bp, query_grandmaster_experience_effect, query_grandmaster_experience_state, query_skill_numeric_effect}。
 *
 * 本模块是 skill_upgrade 域纯规则片段，由 `numeric_rule_d.c` 继承；
 * 不持有 daemon 状态（nosave 全部留在 facade），
 * 配置经 facade 的 query_section/query_section_int 读取。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>
#include <game/skill.h>
#include <game_constants.h>
#include "defs.h"
mapping apply_numeric_context_factor(mapping values, mapping spec, object actor, object target);
int query_effective_skill_level(object player, string skill_id);
int query_exp_required_for_skill_cap(int wanted_skill_cap);
int query_level_cost(int target_level);
int query_max_resource_value();
int query_max_skill_level();
mapping query_section(string section_name);
int query_section_int(string section_name, string key, int default_value);
string query_skill_category(string skill_id);
string query_skill_kind(string skill_id);
mapping query_skill_numeric_definition(string skill_id);
int query_skill_potential_factor_bp(string skill_id);
int query_skill_training_cap(object player, string skill_id);
int query_skill_passive_weight_bp(object player, string skill_id);
int query_weighted_category_skill_level(object player, string category);
mapping query_weighted_category_skill_level_map(object player);
mapping query_skill_action_framework()
{
    mapping framework;

    framework = query_section("skill_action");
    return mapp(framework) ? copy(framework) : ([]);
}
mapping query_skill_action_type_config(string action_type)
{
    mapping framework;
    mapping types;
    mapping type_config;

    if (!stringp(action_type) || action_type == "")
        return ([]);

    framework = query_skill_action_framework();
    types = mapp(framework["types"]) ? framework["types"] : ([]);
    type_config = types[action_type];
    return mapp(type_config) ? copy(type_config) : ([]);
}
private int query_action_int(mapping action, mapping defaults, mapping type_config,
    string key, int default_value)
{
    if (mapp(action) && intp(action[key]))
        return action[key];
    if (mapp(type_config) && intp(type_config[key]))
        return type_config[key];
    if (mapp(defaults) && intp(defaults[key]))
        return defaults[key];
    return default_value;
}
private string query_action_string(mapping action, mapping type_config, string key,
    string default_value)
{
    if (mapp(action) && stringp(action[key]) && action[key] != "")
        return action[key];
    if (mapp(type_config) && stringp(type_config[key]) && type_config[key] != "")
        return type_config[key];
    return default_value;
}
private mapping query_action_mapping(mapping action, mapping defaults, string key)
{
    if (mapp(action) && mapp(action[key]))
        return copy(action[key]);
    if (mapp(defaults) && mapp(defaults[key]))
        return copy(defaults[key]);
    return ([ ]);
}
mapping normalize_skill_action_definition(string action_type, string skill_id, mapping raw_action)
{
    mapping action;
    mapping defaults;
    mapping effects;
    mapping framework;
    mapping type_config;

    if (!stringp(action_type) || action_type == "" || !stringp(skill_id) || skill_id == "" || !mapp(raw_action))
        return 0;

    framework = query_skill_action_framework();
    defaults = mapp(framework["defaults"]) ? framework["defaults"] : ([]);
    type_config = query_skill_action_type_config(action_type);
    action = copy(raw_action);
    action["action_type"] = action_type;
    action["skill_id"] = skill_id;
    action["unlock_type"] = query_action_string(action, type_config, "unlock_type", action_type);
    action["effect_domain"] = query_action_string(action, type_config, "effect_domain", action_type);
    action["target_policy"] = query_action_string(action, type_config, "target_policy", "self");
    action["busy"] = query_action_int(action, defaults, type_config, "busy", 1);
    action["cooldown"] = query_action_int(action, defaults, type_config, "cooldown", 0);
    action["pvp_factor_bp"] = query_action_int(action, defaults, type_config, "pvp_factor_bp", BP_BASE);
    action["pve_factor_bp"] = query_action_int(action, defaults, type_config, "pve_factor_bp", BP_BASE);
    action["resource_costs"] = query_action_mapping(action, defaults, "resource_costs");
    action["status_effects"] = arrayp(action["status_effects"]) ? action["status_effects"] : ({ });

    effects = mapp(action["effects"]) ? copy(action["effects"]) : ([]);
    action["effects"] = effects;
    return action;
}
mapping query_skill_action_effects(mapping action, object actor, object target)
{
    if (!mapp(action) || !mapp(action["effects"]))
        return ([]);
    return apply_numeric_context_factor(action["effects"], action, actor, target);
}
int clamp_resource_value(int value)
{
    int max_value;

    max_value = query_max_resource_value();
    if (value < 0)
        return 0;
    if (value > max_value)
        return max_value;
    return value;
}
private int query_skill_effective_count_units(object player, string wanted_category, int special_only)
{
    mapping skills;
    mapping data;
    string skill_id;
    string category;
    int full_level;
    int half_level;
    int level;
    int units;

    if (!objectp(player))
        return 0;

    full_level = query_section_int("breadth", "effective_level_full", 100);
    half_level = query_section_int("breadth", "effective_level_half", 50);
    skills = SKILL_D->query_player_skills(player);
    foreach (skill_id, data in skills || ([])) {
        if (!mapp(data))
            continue;
        if (special_only && query_skill_kind(skill_id) != "special")
            continue;
        category = query_skill_category(skill_id);
        if (stringp(wanted_category) && wanted_category != "" && category != wanted_category)
            continue;
        level = data["level"] || 0;
        if (level >= full_level)
            units += 2;
        else if (level >= half_level)
            units += 1;
    }

    return units;
}
int query_skill_breadth_factor_bp(object player, string skill_id)
{
    string category;
    int same_units;
    int global_units;
    int same_extra_units;
    int global_extra_units;
    int same_step;
    int global_step;
    int global_free;
    int factor;

    if (!objectp(player) || query_skill_kind(skill_id) != "special")
        return BP_BASE;

    category = query_skill_category(skill_id);
    same_units = query_skill_effective_count_units(player, category, 1);
    global_units = query_skill_effective_count_units(player, "", 1);
    same_extra_units = same_units > 2 ? same_units - 2 : 0;
    global_free = query_section_int("breadth", "global_free_special_count", 6);
    global_extra_units = global_units > global_free * 2 ? global_units - global_free * 2 : 0;
    same_step = query_section_int("breadth", "same_category_step_bp", 1200);
    global_step = query_section_int("breadth", "global_step_bp", 300);

    factor = BP_BASE;
    factor += same_extra_units * same_step / 2;
    factor += global_extra_units * global_step / 2;
    return factor;
}
int query_comprehension_discount_bp(object player)
{
    int intelligence;
    int discount;

    if (!objectp(player))
        return 0;

    intelligence = player->query("attributes/intelligence") || player->query("int") || 10;
    discount = intelligence * BP_BASE / 4000;
    if (discount > 3000)
        discount = 3000;
    return discount < 0 ? 0 : discount;
}
int query_skill_upgrade_cost(object player, string skill_id)
{
    int current_level;
    int target_level;
    int cap;
    int amount;
    int factor_bp;
    int breadth_bp;
    int discount_bp;

    if (!objectp(player) || !stringp(skill_id) || skill_id == "")
        return 0;

    current_level = SKILL_D->query_player_skill_level(player, skill_id);
    target_level = current_level + 1;
    cap = query_skill_training_cap(player, skill_id);
    if (target_level > cap)
        return 0;

    amount = query_level_cost(target_level);
    factor_bp = query_skill_potential_factor_bp(skill_id);
    breadth_bp = query_skill_breadth_factor_bp(player, skill_id);
    discount_bp = query_comprehension_discount_bp(player);
    amount = amount * factor_bp / BP_BASE;
    amount = amount * breadth_bp / BP_BASE;
    amount = amount * (BP_BASE - discount_bp) / BP_BASE;
    return amount < 1 ? 1 : amount;
}
mapping query_skill_train_status(object player, string skill_id)
{
    mapping breakthrough;
    int current_level;
    int cap;

    if (!objectp(player) || !stringp(skill_id) || skill_id == "")
        return ([ "ok": 0, "error": "invalid_arguments" ]);

    current_level = SKILL_D->query_player_skill_level(player, skill_id);
    cap = query_skill_training_cap(player, skill_id);
    breakthrough = SKILL_BREAKTHROUGH_D->query_breakthrough_state(player, skill_id);
    return ([
        "ok": 1,
        "skill_id": skill_id,
        "kind": query_skill_kind(skill_id),
        "category": query_skill_category(skill_id),
        "level": current_level,
        "cap": cap,
        "breakthrough_tier": mapp(breakthrough) ? (breakthrough["unlocked_tier"] || 0) : 0,
        "next_breakthrough_level": mapp(breakthrough) ? (breakthrough["next_level"] || 0) : 0,
        "breakthrough_pending_count": mapp(breakthrough) ? (breakthrough["pending_count"] || 0) : 0,
        "can_improve": current_level < cap,
        "next_cost": current_level < cap ? query_skill_upgrade_cost(player, skill_id) : 0,
    ]);
}
private int *insert_descending_level(int *levels, int level)
{
    int i;

    if (!arrayp(levels) || sizeof(levels) == 0)
        return ({ level });

    for (i = 0; i < sizeof(levels); i++) {
        if (level > levels[i]) {
            if (i == 0)
                return ({ level }) + levels;
            return levels[0..i - 1] + ({ level }) + levels[i..];
        }
    }

    return levels + ({ level });
}
private string query_skill_level_signature(mapping skills)
{
    string *skill_ids;
    string skill_id;
    string signature;
    mapping data;

    if (!mapp(skills) || sizeof(skills) == 0)
        return "empty";

    skill_ids = sort_array(keys(skills), 1);
    signature = "";
    foreach (skill_id in skill_ids) {
        data = mapp(skills[skill_id]) ? skills[skill_id] : ([]);
        signature += sprintf("%s:%d;", skill_id, data["level"] || 0);
    }
    return signature;
}
 mapping query_weighted_category_skill_level_map(object player)
{
    mapping cache;
    mapping category_levels;
    mapping data;
    mapping result;
    mapping skills;
    mixed *weights;
    string category;
    string signature;
    string skill_id;
    int cap;
    int i;
    int level;
    int *levels;
    int value;

    result = ([ ]);
    if (!objectp(player))
        return result;

    skills = SKILL_D->query_player_skills(player);
    signature = query_skill_level_signature(skills);
    cache = player->query_temp("numeric/weighted_category_skill_levels");
    if (mapp(cache) && cache["signature"] == signature && mapp(cache["levels"]))
        return cache["levels"];

    category_levels = ([ ]);
    foreach (skill_id, data in skills || ([ ])) {
        if (!mapp(data))
            continue;
        level = data["level"] || 0;
        if (level <= 0)
            continue;
        category = query_skill_category(skill_id);
        if (!stringp(category) || category == "")
            continue;
        levels = arrayp(category_levels[category]) ? category_levels[category] : ({ });
        category_levels[category] = insert_descending_level(levels, level);
    }

    weights = query_section("breadth")["passive_weights_bp"];
    if (!arrayp(weights))
        weights = ({ BP_BASE });
    foreach (category, levels in category_levels || ([ ])) {
        value = 0;
        for (i = 0; i < sizeof(levels) && i < sizeof(weights); i++)
            value += levels[i] * (weights[i] || 0) / BP_BASE;
        if (sizeof(levels) > 0) {
            cap = levels[0] * query_section_int("breadth", "same_category_passive_cap_bp", 17000) / BP_BASE;
            if (value > cap)
                value = cap;
        }
        result[category] = value;
    }

    player->set_temp("numeric/weighted_category_skill_levels", ([
        "signature": signature,
        "levels": result,
    ]));
    return result;
}
int query_weighted_category_skill_level(object player, string category)
{
    if (!objectp(player) || !stringp(category) || category == "")
        return 0;
    return query_weighted_category_skill_level_map(player)[category] || 0;
}
private int is_outer_skill_category(string category)
{
    return member_array(category, ({
        "blade", "claw", "club", "cuff", "finger", "leg", "palm", "spear",
        "sword", "throwing"
    })) != -1;
}
private int special_skill_is_activated(object player, string skill_id)
{
    mapping active;
    string category;

    if (!objectp(player) || !stringp(skill_id) || skill_id == "")
        return 0;

    category = query_skill_category(skill_id);
    active = SKILL_BEHAVIOR_D->query_active_skill_slots(player);
    if (category == "force")
        return active["force"] == skill_id;
    if (category == "dodge")
        return active["dodge"] == skill_id;
    if (!is_outer_skill_category(category))
        return 0;

    return SKILL_BEHAVIOR_D->is_active_skill_for_slot(player, "outer", skill_id)
        || active["parry"] == skill_id;
}
int query_skill_passive_weight_bp(object player, string skill_id)
{
    mapping skills;
    mapping data;
    mixed *weights;
    string other_skill;
    string category;
    int level;
    int other_level;
    int rank;

    if (!objectp(player) || !stringp(skill_id) || skill_id == "")
        return 0;
    if (query_skill_kind(skill_id) == "basic")
        return BP_BASE;
    if (!special_skill_is_activated(player, skill_id))
        return 0;

    category = query_skill_category(skill_id);
    level = query_effective_skill_level(player, skill_id);
    if (level <= 0 || category == "")
        return 0;

    skills = SKILL_D->query_player_skills(player);
    foreach (other_skill, data in skills || ([])) {
        if (other_skill == skill_id || !mapp(data))
            continue;
        if (query_skill_category(other_skill) != category)
            continue;
        other_level = data["level"] || 0;
        if (other_level > level)
            rank++;
    }

    weights = query_section("breadth")["passive_weights_bp"];
    if (!arrayp(weights) || sizeof(weights) == 0)
        return BP_BASE;
    if (rank >= sizeof(weights))
        return weights[<1] || 0;
    return weights[rank] || 0;
}
private int query_grandmaster_required_skill_level()
{
    return query_section_int("grandmaster_experience", "required_skill_level", query_max_skill_level());
}
private int query_grandmaster_exp_floor()
{
    int floor;

    floor = query_section_int("grandmaster_experience", "exp_floor", 0);
    if (floor > 0)
        return floor;
    return query_exp_required_for_skill_cap(query_grandmaster_required_skill_level());
}
private int query_grandmaster_raw_experience_units(object player)
{
    int exp;
    int floor;
    int unit;

    if (!objectp(player) || !query_section_int("grandmaster_experience", "enabled", 1))
        return 0;

    exp = player->query("combat_exp") || 0;
    floor = query_grandmaster_exp_floor();
    unit = query_section_int("grandmaster_experience", "exp_unit", 500000);
    if (unit <= 0 || exp <= floor)
        return 0;
    return (exp - floor) / unit;
}
int query_grandmaster_experience_units(object player)
{
    int cap;
    int decay;
    int raw_units;
    int units;

    raw_units = query_grandmaster_raw_experience_units(player);
    if (raw_units <= 0)
        return 0;

    decay = query_section_int("grandmaster_experience", "decay_bp_per_unit", 250);
    if (decay > 0)
        units = raw_units * BP_BASE / (BP_BASE + (raw_units - 1) * decay);
    else
        units = raw_units;

    cap = query_section_int("grandmaster_experience", "max_effective_units", 100);
    if (cap > 0 && units > cap)
        units = cap;
    return units < 0 ? 0 : units;
}
private int query_grandmaster_skill_kind_weight_bp(string skill_id)
{
    mapping weights;
    mixed weight;

    weights = query_section("grandmaster_experience")["skill_kind_weights_bp"];
    weight = mapp(weights) ? weights[query_skill_kind(skill_id)] : 0;
    return intp(weight) && weight > 0 ? weight : BP_BASE;
}
mapping query_grandmaster_experience_effect(object player, string domain)
{
    mapping category_weights;
    mapping data;
    mapping domain_weights;
    mapping result;
    mapping skills;
    mapping weights;
    string category;
    string key;
    string skill_id;
    int kind_weight_bp;
    int passive_weight_bp;
    int required_level;
    int units;
    int value;

    if (!objectp(player) || !stringp(domain) || domain == "")
        return ([]);

    units = query_grandmaster_experience_units(player);
    if (units <= 0)
        return ([]);

    domain_weights = query_section("grandmaster_experience")["domain_weights"];
    category_weights = mapp(domain_weights) ? domain_weights[domain] : 0;
    if (!mapp(category_weights))
        return ([]);

    required_level = query_grandmaster_required_skill_level();
    result = ([]);
    skills = SKILL_D->query_player_skills(player);
    foreach (skill_id, data in skills || ([])) {
        if (!mapp(data) || query_effective_skill_level(player, skill_id) < required_level)
            continue;
        category = query_skill_category(skill_id);
        weights = stringp(category) ? category_weights[category] : 0;
        if (!mapp(weights))
            continue;

        kind_weight_bp = query_grandmaster_skill_kind_weight_bp(skill_id);
        passive_weight_bp = query_skill_passive_weight_bp(player, skill_id);
        if (kind_weight_bp <= 0 || passive_weight_bp <= 0)
            continue;
        foreach (key, value in weights) {
            if (!stringp(key) || !intp(value))
                continue;
            result[key] = (result[key] || 0)
                + units * value * kind_weight_bp / BP_BASE * passive_weight_bp / BP_BASE;
        }
    }

    return result;
}
mapping query_grandmaster_experience_state(object player)
{
    if (!objectp(player))
        return ([ "ok": 0, "error": "invalid_arguments" ]);

    return ([
        "ok": 1,
        "enabled": query_section_int("grandmaster_experience", "enabled", 1),
        "required_skill_level": query_grandmaster_required_skill_level(),
        "exp_floor": query_grandmaster_exp_floor(),
        "exp_unit": query_section_int("grandmaster_experience", "exp_unit", 500000),
        "raw_units": query_grandmaster_raw_experience_units(player),
        "effective_units": query_grandmaster_experience_units(player),
        "combat_attributes": query_grandmaster_experience_effect(player, "combat_attributes"),
    ]);
}
mapping query_skill_numeric_effect(object player, string skill_id, string domain)
{
    mapping def;
    mapping domains;
    mapping spec;
    mapping result;
    string key;
    int per_level;
    int level;
    int weight_bp;

    if (!objectp(player) || !stringp(skill_id) || skill_id == "" || !stringp(domain) || domain == "")
        return ([]);

    def = query_skill_numeric_definition(skill_id);
    domains = mapp(def) ? def["numeric_effects"] : 0;
    spec = mapp(domains) ? domains[domain] : 0;
    if (!mapp(spec))
        return ([]);

    level = query_effective_skill_level(player, skill_id);
    weight_bp = query_skill_passive_weight_bp(player, skill_id);
    if (level <= 0 || weight_bp <= 0)
        return ([]);

    result = ([]);
    foreach (key, per_level in spec) {
        if (!stringp(key) || !intp(per_level))
            continue;
        result[key] = per_level * level * weight_bp / BP_BASE;
    }

    return result;
}
