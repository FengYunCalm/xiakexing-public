/**
 * @file    numeric_rule_d.c
 * @brief   底层数值规则服务真源



 *
 * @par Description:
 * 集中承载技能上限、潜能成本、基础/特殊技能约束与通用数值边界。
 * 本服务只提供框架级公式，不声明具体门派武学、怪物或剧情内容。
 */
#pragma strict_types
#pragma save_types

#include <game/skill.h>
#include <system/services.h>
#include <game_constants.h>
#include "numeric_rule/defs.h"

inherit "/lib/framework/daemon";
inherit "/daemons/services/gameplay/numeric_rule/combat_tempo";
inherit "/daemons/services/gameplay/numeric_rule/combat_runtime";
inherit "/daemons/services/gameplay/numeric_rule/combat_pvp";
inherit "/daemons/services/gameplay/numeric_rule/equipment_boss";
inherit "/daemons/services/gameplay/numeric_rule/status_effect";
inherit "/daemons/services/gameplay/numeric_rule/reward";
inherit "/daemons/services/gameplay/numeric_rule/action_budget";
inherit "/daemons/services/gameplay/numeric_rule/npc_combat";
inherit "/daemons/services/gameplay/numeric_rule/equipment_budget";
inherit "/daemons/services/gameplay/numeric_rule/pve_encounter";
inherit "/daemons/services/gameplay/numeric_rule/attribute";
inherit "/daemons/services/gameplay/numeric_rule/skill_metadata";
inherit "/daemons/services/gameplay/numeric_rule/progression";
inherit "/daemons/services/gameplay/numeric_rule/skill_definition";
inherit "/daemons/services/gameplay/numeric_rule/skill_breakthrough";
inherit "/daemons/services/gameplay/numeric_rule/skill_upgrade";

nosave mapping _numeric_config;
nosave int _numeric_config_generation;
nosave mapping _action_budget_base_specs;
nosave mapping _skill_numeric_metadata_cache;
nosave mapping _progression_snapshot_cache;
nosave int *_progression_snapshot_cache_order;

int query_exp_required_for_skill_cap(int wanted_skill_cap);
mapping query_progression_snapshot_by_exp(int combat_exp);
mapping query_player_progression_snapshot(object player);
varargs mapping query_numeric_effects(object player, string domain, object target);
int scale_budget_value(int value, int factor_bp);
string normalize_combat_attribute_key(string key);
mapping query_skill_numeric_metadata(string skill_id);
void reset_progression_snapshot_cache();
mapping merge_action_budget_resource_costs(mapping defaults, mapping type_config, mapping overrides);
mapping merge_action_budget_resource_costs_from_base(mapping base, mapping overrides);
void merge_breakthrough_numeric_metadata(mapping domains, mapping resource_impact, mixed raw_breakthroughs);
void merge_combat_resource_impact(mapping result, mapping effects);
mapping query_action_budget_framework();
int query_action_budget_int(mapping type_config, mapping defaults, mapping overrides, string key, int default_value);
mapping query_grandmaster_experience_effect(object player, string domain);
varargs mapping query_skill_breakthrough_numeric_effect(object player, string skill_id, string domain, object target);
mapping query_skill_cap_gate_config();
string query_skill_category_from_definition(mapping def);
string query_skill_kind_from_definition(mapping def);
mapping query_skill_numeric_effect(object player, string skill_id, string domain);

private mapping query_numeric_config()
{
    mapping config;

    config = CONFIG_D->query_full_config("numeric");
    return mapp(config) ? config : ([]);
}
private void load_numeric_config()
{
    _numeric_config = query_numeric_config();
    _action_budget_base_specs = ([]);
    _numeric_config_generation++;
    reset_progression_snapshot_cache();
}
void on_config_reloaded(string key, string config_name)
{
    if (key != "numeric" && config_name != "numeric")
        return;

    load_numeric_config();
}

void on_config_reload_rollback(string key, string config_name)
{
    if (key != "numeric" && config_name != "numeric")
        return;

    load_numeric_config();
    if (_numeric_config_generation > 1)
        _numeric_config_generation -= 2;
    if (_numeric_config_generation < 1)
        _numeric_config_generation = 1;
}
void create()
{
    ::create();

    if (!mapp(_numeric_config))
        _numeric_config = ([]);
    if (!mapp(_skill_numeric_metadata_cache))
        _skill_numeric_metadata_cache = ([]);

    load_numeric_config();
    CONFIG_D->compat_register_config_callback("numeric", "on_config_reloaded", this_object());
}
int query_numeric_config_generation()
{
    return _numeric_config_generation > 0 ? _numeric_config_generation : 1;
}
void reset_progression_snapshot_cache()
{
    _progression_snapshot_cache = ([]);
    _progression_snapshot_cache_order = ({ });
}
 mapping query_section(string section_name)
{
    mapping section;

    if (!mapp(_numeric_config))
        load_numeric_config();

    section = _numeric_config[section_name];
    return mapp(section) ? section : ([]);
}
 int query_section_int(string section_name, string key, int default_value)
{
    mixed value;

    value = query_section(section_name)[key];
    return intp(value) ? value : default_value;
}
 int query_mapping_int(mapping source, string key, int default_value)
{
    mixed value;

    value = mapp(source) ? source[key] : 0;
    return intp(value) ? value : default_value;
}
 string normalize_combat_attribute_key(string key)
{
    if (!stringp(key) || key == "")
        return "";

    switch (key) {
    case "damage_bonus":
        return "attack";
    case "hit_bonus":
    case "debuff_bonus":
    case "interrupt_bonus":
    case "poison_bonus":
        return "hit";
    case "dodge_bonus":
    case "move_speed":
    case "terrain_bonus":
    case "stealth_bonus":
    case "cleanse_bonus":
    case "lock_break_bonus":
    case "detection_pressure":
    case "negative_movement":
        return "dodge";
    case "parry_bonus":
    case "counter_bonus":
        return "parry";
    case "defense_bonus":
    case "guard_bonus":
    case "shield_bonus":
    case "field_bonus":
        return "defense";
    case "critical_bonus":
        return "critical";
    case "armor_break_bonus":
        return "armor_break";
    case "control_resist":
        return "control_resist_bp";
    default:
        return key;
    }
}
int query_max_skill_level()
{
    return query_section_int("limits", "max_skill_level", SKILL_MAX_LEVEL);
}
int query_max_resource_value()
{
    return query_section_int("limits", "max_resource", 999999);
}
int query_debate_action_timeout_seconds()
{
    return query_section_int("timing", "debate_action_timeout_seconds", 10);
}
int query_busy_unit_seconds()
{
    return query_section_int("timing", "busy_unit_seconds", 1);
}
mapping query_switch_cost(string slot)
{
    mapping costs;
    mapping cost;

    costs = _numeric_config["switch_costs"];
    cost = mapp(costs) && stringp(slot) ? costs[slot] : 0;
    if (!mapp(cost))
        cost = ([ "busy": 1, "debate_turns": 1 ]);
    return copy(cost);
}
 int clamp_outcome_bp(int value)
{
    if (value < 0)
        return 0;
    if (value > BP_BASE)
        return BP_BASE;
    return value;
}
 int query_numeric_budget_int(mapping defaults, mapping context,
    string key, int default_value)
{
    if (mapp(context) && !undefinedp(context[key]) && intp(context[key]))
        return context[key];
    if (mapp(defaults) && !undefinedp(defaults[key]) && intp(defaults[key]))
        return defaults[key];
    return default_value;
}
mapping query_numeric_framework_validation_report()
{
    mapping framework;
    mapping section;
    string *missing;
    string section_name;
    string *required;

    framework = query_section("numeric_framework_validation");
    required = arrayp(framework["required_sections"]) ? framework["required_sections"] : ({ });
    missing = ({ });
    foreach (section_name in required) {
        if (!stringp(section_name) || section_name == "")
            continue;
        section = query_section(section_name);
        if (!mapp(section) || sizeof(keys(section)) == 0)
            missing += ({ section_name });
    }

    return ([
        "ok": sizeof(missing) == 0,
        "valid": sizeof(missing) == 0,
        "required_count": sizeof(required),
        "missing_count": sizeof(missing),
        "missing_sections": missing,
    ]);
}
mapping query_action_budget_spec(string action_type, string action_id, mapping overrides)
{
    mapping base;
    mapping spec;
    string key;

    if (!stringp(action_type) || action_type == "")
        return ([ "ok": 0, "error": "invalid_action_type" ]);
    if (!stringp(action_id) || action_id == "")
        action_id = "*";

    if (!mapp(_action_budget_base_specs))
        _action_budget_base_specs = ([]);
    base = _action_budget_base_specs[action_type];
    if (!mapp(base)) {
        mapping defaults;
        mapping framework;
        mapping type_config;
        mapping types;

        /* Retain the public framework normalization path. The cache removes
         * repeat copies on hot action commands without giving it a second
         * configuration interpretation. */
        framework = query_action_budget_framework();
        defaults = mapp(framework["defaults"]) ? framework["defaults"] : ([]);
        types = mapp(framework["types"]) ? framework["types"] : ([]);
        type_config = mapp(types[action_type]) ? types[action_type] : ([]);
        base = ([
            "ok": 1,
            "action_type": action_type,
            "enabled": query_action_budget_int(type_config, defaults, ([]),
                "enabled", 1) ? 1 : 0,
            "busy": query_action_budget_int(type_config, defaults, ([]),
                "busy", 0),
            "cooldown": query_action_budget_int(type_config, defaults, ([]),
                "cooldown", 0),
            "min_interval_seconds": query_action_budget_int(type_config,
                defaults, ([]), "min_interval_seconds", 0),
            "fatigue_window_seconds": query_action_budget_int(type_config,
                defaults, ([]), "fatigue_window_seconds", 300),
            "fatigue_recovery_bp_per_second": query_action_budget_int(
                type_config, defaults, ([]), "fatigue_recovery_bp_per_second", 100),
            "fatigue_per_use_bp": query_action_budget_int(type_config,
                defaults, ([]), "fatigue_per_use_bp", 0),
            "max_fatigue_bp": query_action_budget_int(type_config, defaults,
                ([]), "max_fatigue_bp", 5000),
            "resource_costs": merge_action_budget_resource_costs(defaults,
                type_config, ([])),
        ]);
        _action_budget_base_specs[action_type] = base;
    }

    spec = copy(base);
    spec["action_id"] = action_id;
    foreach (key in ({ "enabled", "busy", "cooldown",
        "min_interval_seconds", "fatigue_window_seconds",
        "fatigue_recovery_bp_per_second", "fatigue_per_use_bp",
        "max_fatigue_bp" }))
        if (mapp(overrides) && !undefinedp(overrides[key])
            && intp(overrides[key]))
            spec[key] = overrides[key];
    spec["enabled"] = spec["enabled"] ? 1 : 0;
    spec["resource_costs"] = merge_action_budget_resource_costs_from_base(
        base["resource_costs"], overrides);
    return spec;
}
 int scale_budget_value(int value, int factor_bp)
{
    if (factor_bp <= 0)
        factor_bp = BP_BASE;
    return value * factor_bp / BP_BASE;
}
 mapping query_skill_numeric_metadata(string skill_id)
{
    mapping breakthrough_domains;
    mapping breakthrough_resource_impact;
    mapping cache;
    mapping def;
    int skill_generation;

    if (!stringp(skill_id) || skill_id == "")
        return ([]);
    if (!mapp(_skill_numeric_metadata_cache))
        _skill_numeric_metadata_cache = ([]);

    skill_generation = SKILL_D->query_skill_config_generation();
    cache = _skill_numeric_metadata_cache[skill_id];
    if (mapp(cache) && cache["skill_generation"] == skill_generation
        && mapp(cache["definition"]))
        return cache;

    def = SKILL_D->get_skill_definition(skill_id);
    if (!mapp(def)) {
        map_delete(_skill_numeric_metadata_cache, skill_id);
        return ([]);
    }
    breakthrough_domains = ([]);
    breakthrough_resource_impact = ([ "hp": 0, "mp": 0, "ep": 0 ]);
    merge_breakthrough_numeric_metadata(breakthrough_domains,
        breakthrough_resource_impact, def["breakthroughs"]);
    cache = ([
        "skill_generation": skill_generation,
        "definition": def,
        "category": query_skill_category_from_definition(def),
        "kind": query_skill_kind_from_definition(def),
        "breakthrough_numeric_domains": breakthrough_domains,
        "breakthrough_resource_impact": breakthrough_resource_impact,
    ]);
    _skill_numeric_metadata_cache[skill_id] = cache;
    return cache;
}
mapping query_skill_resource_impact(string skill_id)
{
    mapping config;
    mapping def;
    mapping domains;
    mapping domain_weights;
    mapping grandmaster;
    mapping metadata;
    mapping resource_impact;
    string category;
    int numeric_generation;

    resource_impact = ([ "hp": 0, "mp": 0, "ep": 0 ]);
    if (!stringp(skill_id) || skill_id == "")
        return resource_impact;

    metadata = query_skill_numeric_metadata(skill_id);
    numeric_generation = query_numeric_config_generation();
    if (metadata["resource_impact_numeric_generation"] == numeric_generation
        && mapp(metadata["resource_impact"]))
        return copy(metadata["resource_impact"]);
    def = mapp(metadata["definition"]) ? metadata["definition"] : 0;
    category = stringp(metadata["category"]) ? metadata["category"] : "";
    config = query_section("derived_attributes");
    merge_combat_resource_impact(resource_impact,
        mapp(config["skill_category_weights"])
            ? config["skill_category_weights"][category] : 0);

    domains = mapp(def) ? def["numeric_effects"] : 0;
    merge_combat_resource_impact(resource_impact,
        mapp(domains) ? domains["combat_attributes"] : 0);
    merge_combat_resource_impact(resource_impact,
        metadata["breakthrough_resource_impact"]);

    grandmaster = query_section("grandmaster_experience");
    domains = mapp(grandmaster) ? grandmaster["domain_weights"] : 0;
    domain_weights = mapp(domains) ? domains["combat_attributes"] : 0;
    merge_combat_resource_impact(resource_impact,
        mapp(domain_weights) ? domain_weights[category] : 0);
    metadata["resource_impact_numeric_generation"] = numeric_generation;
    metadata["resource_impact"] = copy(resource_impact);
    _skill_numeric_metadata_cache[skill_id] = metadata;
    return copy(resource_impact);
}
int query_skill_cap_by_exp(int combat_exp)
{
    mapping rule;
    mixed *rules;
    int cap;
    int max_level;

    rules = _numeric_config["skill_cap_by_exp"];
    max_level = query_max_skill_level();
    cap = 0;
    if (!arrayp(rules))
        return max_level;

    foreach (rule in rules) {
        if (!mapp(rule))
            continue;
        if (combat_exp >= (rule["exp"] || 0) && (rule["skill_cap"] || 0) > cap)
            cap = rule["skill_cap"];
    }

    if (cap <= 0)
        cap = 100;
    return cap > max_level ? max_level : cap;
}
int query_exp_required_for_skill_cap(int wanted_skill_cap)
{
    mapping rule;
    mixed *rules;
    int best_exp;
    int best_skill_cap;
    int exp;
    int skill_cap;

    rules = _numeric_config["skill_cap_by_exp"];
    if (!arrayp(rules) || wanted_skill_cap <= 0)
        return 0;

    best_exp = -1;
    best_skill_cap = 0;
    foreach (rule in rules) {
        if (!mapp(rule))
            continue;
        skill_cap = rule["skill_cap"] || 0;
        exp = rule["exp"] || 0;
        if (skill_cap >= wanted_skill_cap && (best_exp < 0 || exp < best_exp)) {
            best_exp = exp;
            best_skill_cap = skill_cap;
        }
    }

    if (best_exp >= 0)
        return best_exp;

    foreach (rule in rules) {
        if (!mapp(rule))
            continue;
        skill_cap = rule["skill_cap"] || 0;
        exp = rule["exp"] || 0;
        if (skill_cap > best_skill_cap) {
            best_skill_cap = skill_cap;
            best_exp = exp;
        }
    }

    return best_exp > 0 ? best_exp : 0;
}
mapping query_progression_snapshot_by_exp(int combat_exp)
{
    mapping cache_entry;
    mapping cached_snapshot;
    mapping config;
    mapping snapshot;
    int base_cap;
    int cap;
    int max_progression_rank;
    int numeric_generation;
    int progression_rank;
    int step;

    numeric_generation = query_numeric_config_generation();
    if (!mapp(_progression_snapshot_cache))
        reset_progression_snapshot_cache();
    cache_entry = _progression_snapshot_cache[combat_exp];
    cached_snapshot = mapp(cache_entry) ? cache_entry["snapshot"] : 0;
    if (mapp(cache_entry)
        && cache_entry["numeric_generation"] == numeric_generation
        && cache_entry["combat_exp"] == combat_exp
        && mapp(cached_snapshot))
        return copy(cached_snapshot);

    config = query_skill_cap_gate_config();
    base_cap = intp(config["base_skill_cap"]) ? config["base_skill_cap"] : 100;
    step = intp(config["skill_cap_per_progression_rank"])
        ? config["skill_cap_per_progression_rank"] : 10;
    max_progression_rank = intp(config["max_progression_rank"])
        ? config["max_progression_rank"] : 100;
    if (base_cap < 0)
        base_cap = 0;
    if (step <= 0)
        step = 10;
    if (max_progression_rank <= 0)
        max_progression_rank = 100;

    cap = query_skill_cap_by_exp(combat_exp);
    progression_rank = cap <= base_cap ? 1
        : 1 + (cap - base_cap) / step;
    if (progression_rank > max_progression_rank)
        progression_rank = max_progression_rank;
    snapshot = ([
        "combat_exp": combat_exp,
        "skill_cap": cap,
        "progression_rank": progression_rank,
    ]);
    if (sizeof(_progression_snapshot_cache_order)
        >= PROGRESSION_SNAPSHOT_CACHE_LIMIT) {
        map_delete(_progression_snapshot_cache,
            _progression_snapshot_cache_order[0]);
        _progression_snapshot_cache_order =
            _progression_snapshot_cache_order[1..];
    }
    _progression_snapshot_cache[combat_exp] = ([
        "numeric_generation": numeric_generation,
        "combat_exp": combat_exp,
        "snapshot": copy(snapshot),
    ]);
    _progression_snapshot_cache_order += ({ combat_exp });
    return copy(snapshot);
}
mapping query_player_progression_snapshot(object player)
{
    int combat_exp;

    if (!objectp(player))
        return ([ "combat_exp": 0, "skill_cap": 0, "progression_rank": 1 ]);
    combat_exp = player->query("combat_exp") || 0;
    return query_progression_snapshot_by_exp(combat_exp);
}
 int query_level_cost(int target_level)
{
    mapping band;
    mixed *bands;

    bands = _numeric_config["potential_cost_bands"];
    if (!arrayp(bands))
        return 1;

    foreach (band in bands) {
        if (!mapp(band))
            continue;
        if (target_level >= (band["min"] || 0) && target_level <= (band["max"] || 0))
            return band["cost"] || 1;
    }

    return 1;
}
varargs mapping query_numeric_effects(object player, string domain, object target)
{
    mapping skills;
    mapping partial;
    mapping result;
    string skill_id;
    string key;
    int value;

    if (!objectp(player) || !stringp(domain) || domain == "")
        return ([]);

    result = ([]);
    skills = SKILL_D->query_player_skills(player);
    foreach (skill_id in keys(skills || ([]))) {
        partial = query_skill_numeric_effect(player, skill_id, domain);
        foreach (key, value in partial || ([]))
            result[key] = (result[key] || 0) + value;
        partial = query_skill_breakthrough_numeric_effect(player, skill_id, domain, target);
        foreach (key, value in partial || ([]))
            result[key] = (result[key] || 0) + value;
    }

    partial = query_grandmaster_experience_effect(player, domain);
    foreach (key, value in partial || ([]))
        result[key] = (result[key] || 0) + value;

    return result;
}
