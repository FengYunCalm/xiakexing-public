/**
 * @file    numeric_rule/npc_combat.c
 * @brief   NPC 战斗模板与技能
 *
 * @par Description:
 * 含 {query_npc_combat_template_type_config, query_npc_template_int, query_npc_template_factor, query_npc_template_string, clamp_minimum_template_value, query_npc_combat_template, query_npc_combat_skill_defaults, query_npc_combat_skill_budget_config, query_npc_combat_skill_preset, query_npc_skill_mapping_int, query_npc_skill_mapping_string, normalize_npc_combat_skill_definition, query_npc_skill_type_factor_bp, query_npc_combat_skill_budget_score, query_actor_hp_ratio_bp, query_npc_combat_skill_condition_report, query_npc_combat_skill_budget_report, query_npc_skill_cooldown_remaining, query_npc_combat_skill_runtime_snapshot, query_npc_combat_skill_trigger, record_npc_combat_skill_trigger}。
 *
 * 本模块是 npc_combat 域纯规则片段，由 `numeric_rule_d.c` 继承；
 * 不持有 daemon 状态（nosave 全部留在 facade），
 * 配置经 facade 的 query_section/query_section_int 读取。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>
#include <game/skill.h>
#include <game_constants.h>
#include "defs.h"
int absolute_status_effect_value(int value);
mapping query_boss_phase_budget_report(mapping context);
mapping query_combat_status_effect_budget_report(mapping effect);
varargs mapping query_derived_combat_attributes(object player, object target);
int query_mapping_int(mapping source, string key, int default_value);
mapping query_npc_combat_skill_framework();
mapping query_npc_combat_template_framework();
int query_player_max_resource(object player, string resource);
mapping query_pve_ttk_budget(object player, string encounter_type);
int scale_budget_value(int value, int factor_bp);
mapping query_npc_combat_template_type_config(string template_type)
{
    mapping framework;
    mapping type_config;
    mapping types;

    if (!stringp(template_type) || template_type == "")
        return ([]);

    framework = query_npc_combat_template_framework();
    types = mapp(framework["types"]) ? framework["types"] : ([]);
    type_config = types[template_type];
    return mapp(type_config) ? copy(type_config) : ([]);
}
private int query_npc_template_int(mapping type_config, mapping defaults,
    string key, int default_value)
{
    if (mapp(type_config) && intp(type_config[key]))
        return type_config[key];
    if (mapp(defaults) && intp(defaults[key]))
        return defaults[key];
    return default_value;
}
private int query_npc_template_factor(mapping type_config, mapping defaults,
    string new_key, int default_value)
{
    return query_npc_template_int(type_config, defaults, new_key, default_value);
}
private string query_npc_template_string(mapping type_config, mapping defaults,
    string key, string default_value)
{
    if (mapp(type_config) && stringp(type_config[key]) && type_config[key] != "")
        return type_config[key];
    if (mapp(defaults) && stringp(defaults[key]) && defaults[key] != "")
        return defaults[key];
    return default_value;
}
private int clamp_minimum_template_value(int value, int minimum)
{
    return value < minimum ? minimum : value;
}
mapping query_npc_combat_template(object player, string template_type)
{
    mapping attrs;
    mapping budget;
    mapping combat;
    mapping defaults;
    mapping enemy;
    mapping framework;
    mapping npc_attributes;
    mapping type_config;
    string encounter_type;
    int attack;
    int defense;
    int max_hp;
    int max_mp;
    int parry;

    if (!objectp(player))
        return ([ "ok": 0, "error": "invalid_player" ]);

    framework = query_npc_combat_template_framework();
    defaults = mapp(framework["defaults"]) ? framework["defaults"] : ([]);
    if (!stringp(template_type) || template_type == "")
        template_type = defaults["template_type"] || "normal";
    type_config = query_npc_combat_template_type_config(template_type);
    if (!mapp(type_config) || sizeof(keys(type_config)) == 0)
        return ([ "ok": 0, "error": "unknown_template_type", "template_type": template_type ]);

    encounter_type = query_npc_template_string(type_config, defaults,
        "encounter_type", "normal");
    budget = query_pve_ttk_budget(player, encounter_type);
    if (!mapp(budget) || !budget["ok"])
        return ([ "ok": 0, "error": "budget_failed", "template_type": template_type, "budget": budget ]);

    enemy = budget["recommended_enemy"] || ([]);
    attrs = budget["player_attrs"] || query_derived_combat_attributes(player);
    max_hp = scale_budget_value(enemy["max_hp"] || 1,
        query_npc_template_int(type_config, defaults, "hp_factor_bp", BP_BASE));
    attack = scale_budget_value(enemy["attack"] || 1,
        query_npc_template_factor(type_config, defaults, "attack_factor_bp", BP_BASE));
    defense = scale_budget_value(enemy["defense"] || 1,
        query_npc_template_factor(type_config, defaults, "defense_factor_bp", BP_BASE));
    parry = scale_budget_value((attack + defense) / 2,
        query_npc_template_factor(type_config, defaults, "parry_factor_bp", 8000));
    max_mp = scale_budget_value(parry,
        query_npc_template_int(type_config, defaults, "max_mp_factor_bp", 5000));

    combat = ([
        "max_hp": clamp_minimum_template_value(max_hp, 1),
        "hp": clamp_minimum_template_value(max_hp, 1),
        "max_mp": clamp_minimum_template_value(max_mp, 0),
        "mp": clamp_minimum_template_value(max_mp, 0),
        "attack": clamp_minimum_template_value(attack, 1),
        "defense": clamp_minimum_template_value(defense, 0),
        "parry": clamp_minimum_template_value(parry, 0),
    ]);
    npc_attributes = ([
        "strength": clamp_minimum_template_value(attack / 3, 1),
        "dexterity": clamp_minimum_template_value(defense / 2, 1),
        "constitution": clamp_minimum_template_value(max_hp / 20, 1),
        "intelligence": clamp_minimum_template_value((attrs["hit"] || 0) / 5 + 5, 1),
    ]);

    return ([
        "ok": 1,
        "template_type": template_type,
        "encounter_type": encounter_type,
        "skill_cap": budget["skill_cap"] || 0,
        "tier_id": budget["tier_id"] || "",
        "target_ttk_rounds": budget["target_ttk_rounds"] || 0,
        "player_survival_rounds": budget["player_survival_rounds"] || 0,
        "combat": combat,
        "attributes": npc_attributes,
        "aggressive": query_npc_template_int(type_config, defaults, "aggressive", 0),
        "reward_factor_bp": query_npc_template_int(type_config, defaults, "reward_factor_bp", BP_BASE),
        "boss_skill_chance": query_npc_template_int(type_config, defaults, "boss_skill_chance", 0),
        "source_budget": budget,
        "template_only": 1,
    ]);
}
private mapping query_npc_combat_skill_defaults()
{
    mapping defaults;
    mapping framework;

    framework = query_npc_combat_skill_framework();
    defaults = mapp(framework["defaults"]) ? framework["defaults"] : ([]);
    return defaults;
}
private mapping query_npc_combat_skill_budget_config()
{
    mapping budget;
    mapping framework;

    framework = query_npc_combat_skill_framework();
    budget = mapp(framework["budget"]) ? framework["budget"] : ([]);
    return budget;
}
mapping query_npc_combat_skill_preset(string skill_id)
{
    mapping config;
    mapping framework;
    mapping preset_skills;

    if (!stringp(skill_id) || skill_id == "")
        return ([]);
    framework = query_npc_combat_skill_framework();
    preset_skills = mapp(framework["preset_skills"]) ? framework["preset_skills"] : ([]);
    config = preset_skills[skill_id];
    return mapp(config) ? copy(config) : ([]);
}
private int query_npc_skill_mapping_int(mapping primary, mapping fallback, string key, int default_value)
{
    if (mapp(primary) && !undefinedp(primary[key]) && intp(primary[key]))
        return primary[key];
    if (mapp(fallback) && !undefinedp(fallback[key]) && intp(fallback[key]))
        return fallback[key];
    return default_value;
}
private string query_npc_skill_mapping_string(mapping primary, mapping fallback, string key, string default_value)
{
    if (mapp(primary) && stringp(primary[key]) && primary[key] != "")
        return primary[key];
    if (mapp(fallback) && stringp(fallback[key]) && fallback[key] != "")
        return fallback[key];
    return default_value;
}
mapping normalize_npc_combat_skill_definition(string skill_id, mapping raw_skill)
{
    mapping defaults;
    mapping preset;
    mapping skill;

    if (!stringp(skill_id) || skill_id == "")
        return 0;

    defaults = query_npc_combat_skill_defaults();
    preset = query_npc_combat_skill_preset(skill_id);
    skill = mapp(preset) ? copy(preset) : ([]);
    if (mapp(raw_skill))
        skill += raw_skill;
    skill["id"] = skill_id;
    skill["enabled"] = query_npc_skill_mapping_int(skill, defaults, "enabled", 1) ? 1 : 0;
    skill["type"] = query_npc_skill_mapping_string(skill, defaults, "type", "direct_damage");
    skill["chance_bp"] = query_npc_skill_mapping_int(skill, defaults, "chance_bp", 0);
    skill["cooldown_seconds"] = query_npc_skill_mapping_int(skill, defaults, "cooldown_seconds", 3);
    skill["budget_limit"] = query_npc_skill_mapping_int(skill, defaults, "budget_limit", 1200);
    skill["block_over_budget"] = query_npc_skill_mapping_int(skill, defaults, "block_over_budget", 0);
    skill["damage"] = query_npc_skill_mapping_int(skill, defaults, "damage", 0);
    skill["variance"] = query_npc_skill_mapping_int(skill, defaults, "variance", 0);
    skill["damage_factor_bp"] = query_npc_skill_mapping_int(skill, defaults, "damage_factor_bp", BP_BASE);
    skill["windup_rounds"] = query_npc_skill_mapping_int(skill, defaults, "windup_rounds", 0);
    skill["max_targets"] = query_npc_skill_mapping_int(skill, defaults, "max_targets", 1);
    skill["min_self_hp_ratio_bp"] = query_npc_skill_mapping_int(skill, defaults, "min_self_hp_ratio_bp", 0);
    skill["max_self_hp_ratio_bp"] = query_npc_skill_mapping_int(skill, defaults, "max_self_hp_ratio_bp", 0);
    skill["min_target_hp_ratio_bp"] = query_npc_skill_mapping_int(skill, defaults, "min_target_hp_ratio_bp", 0);
    skill["max_target_hp_ratio_bp"] = query_npc_skill_mapping_int(skill, defaults, "max_target_hp_ratio_bp", 0);
    skill["status_effects"] = arrayp(skill["status_effects"]) ? skill["status_effects"] : ({ });
    skill["prepare_status_effects"] = arrayp(skill["prepare_status_effects"])
        ? skill["prepare_status_effects"] : ({ });
    skill["resource_damage"] = mapp(skill["resource_damage"]) ? skill["resource_damage"] : ([ ]);
    return skill;
}
private int query_npc_skill_type_factor_bp(string skill_type)
{
    mapping budget;
    mapping factors;

    budget = query_npc_combat_skill_budget_config();
    factors = mapp(budget["type_factor_bp"]) ? budget["type_factor_bp"] : ([]);
    return query_mapping_int(factors, skill_type, BP_BASE);
}
int query_npc_combat_skill_budget_score(mapping skill)
{
    mapping budget;
    mapping status_report;
    mapping resource_damage;
    mixed raw_status;
    string resource;
    int score;
    int status_weight;
    int type_factor;

    if (!mapp(skill))
        return 0;

    budget = query_npc_combat_skill_budget_config();
    score += (skill["chance_bp"] || 0) * query_mapping_int(budget, "chance_weight", 100) / BP_BASE;
    score += (skill["damage"] || 0) * query_mapping_int(budget, "damage_weight", 20);
    score += (skill["variance"] || 0) * query_mapping_int(budget, "variance_weight", 10);
    status_weight = query_mapping_int(budget, "status_effect_weight", 1);
    foreach (raw_status in skill["status_effects"] || ({ })) {
        if (!mapp(raw_status))
            continue;
        status_report = query_combat_status_effect_budget_report(raw_status);
        score += (status_report["score"] || 0) * status_weight;
    }
    foreach (raw_status in skill["prepare_status_effects"] || ({ })) {
        if (!mapp(raw_status))
            continue;
        status_report = query_combat_status_effect_budget_report(raw_status);
        score += (status_report["score"] || 0) * status_weight;
    }
    resource_damage = skill["resource_damage"];
    foreach (resource, int value in resource_damage || ([ ])) {
        if (stringp(resource) && intp(value))
            score += absolute_status_effect_value(value) * 10;
    }
    score += absolute_status_effect_value((skill["damage_factor_bp"] || BP_BASE) - BP_BASE) / 10;
    if ((skill["max_targets"] || 1) > 1)
        score += ((skill["max_targets"] || 1) - 1) * 100;
    type_factor = query_npc_skill_type_factor_bp(skill["type"] || "direct_damage");
    return score * type_factor / BP_BASE;
}
private int query_actor_hp_ratio_bp(object actor)
{
    int current_hp;
    int max_hp;

    if (!objectp(actor))
        return 0;
    current_hp = actor->query("combat/hp") || 0;
    if (userp(actor))
        max_hp = query_player_max_resource(actor, "max_hp");
    else
        max_hp = actor->query("combat/max_hp") || 0;
    if (max_hp <= 0)
        max_hp = query_player_max_resource(actor, "max_hp");
    if (max_hp <= 0)
        return 0;
    if (current_hp >= max_hp)
        return BP_BASE;
    if (current_hp <= 0)
        return 0;
    return current_hp * BP_BASE / max_hp;
}
mapping query_npc_combat_skill_condition_report(object npc, object target, mapping skill)
{
    mixed *failures;
    int self_hp_ratio_bp;
    int target_hp_ratio_bp;

    if (!objectp(npc) || !objectp(target) || !mapp(skill))
        return ([ "ok": 0, "error": "invalid_arguments" ]);

    self_hp_ratio_bp = query_actor_hp_ratio_bp(npc);
    target_hp_ratio_bp = query_actor_hp_ratio_bp(target);
    failures = ({ });
    if ((skill["min_self_hp_ratio_bp"] || 0) > 0
        && self_hp_ratio_bp < (skill["min_self_hp_ratio_bp"] || 0))
        failures += ({ "self_hp_too_low" });
    if ((skill["max_self_hp_ratio_bp"] || 0) > 0
        && self_hp_ratio_bp > (skill["max_self_hp_ratio_bp"] || 0))
        failures += ({ "self_hp_too_high" });
    if ((skill["min_target_hp_ratio_bp"] || 0) > 0
        && target_hp_ratio_bp < (skill["min_target_hp_ratio_bp"] || 0))
        failures += ({ "target_hp_too_low" });
    if ((skill["max_target_hp_ratio_bp"] || 0) > 0
        && target_hp_ratio_bp > (skill["max_target_hp_ratio_bp"] || 0))
        failures += ({ "target_hp_too_high" });

    return ([
        "ok": 1,
        "valid": sizeof(failures) == 0,
        "self_hp_ratio_bp": self_hp_ratio_bp,
        "target_hp_ratio_bp": target_hp_ratio_bp,
        "failures": failures,
    ]);
}
mapping query_npc_combat_skill_budget_report(mapping skill)
{
    int limit;
    int score;

    if (!mapp(skill))
        return ([ "ok": 0, "error": "invalid_skill" ]);

    limit = skill["budget_limit"] || query_mapping_int(query_npc_combat_skill_defaults(), "budget_limit", 1200);
    score = query_npc_combat_skill_budget_score(skill);
    return ([
        "ok": 1,
        "valid": score <= limit,
        "id": skill["id"] || "",
        "type": skill["type"] || "direct_damage",
        "score": score,
        "limit": limit,
        "over_budget": score > limit ? score - limit : 0,
        "block_over_budget": skill["block_over_budget"] || 0,
    ]);
}
private int query_npc_skill_cooldown_remaining(object npc, string skill_id)
{
    int expires_at;

    if (!objectp(npc) || !stringp(skill_id) || skill_id == "")
        return 0;

    expires_at = npc->query_temp("numeric/npc_combat_skill/cooldown/" + skill_id) || 0;
    if (expires_at <= time())
        return 0;
    return expires_at - time();
}
mapping query_npc_combat_skill_runtime_snapshot(object npc, object target)
{
    mapping budget_report;
    mapping phase_report;
    mapping raw_config;
    mapping skill;
    mapping *skills;
    mixed raw_skill;
    mixed *raw_skills;
    string skill_id;
    int chance;
    int chance_bp;

    if (!objectp(npc))
        return ([ "ok": 0, "error": "invalid_npc" ]);

    raw_skills = npc->query("boss_skills");
    if (!arrayp(raw_skills))
        raw_skills = npc->query_temp("numeric/npc_combat_skill/skills");
    if (!arrayp(raw_skills))
        raw_skills = ({ });
    chance = npc->query("boss_skill_chance") || npc->query_temp("numeric/npc_combat_skill/chance");
    chance_bp = chance > 0 ? chance * 100 : 0;
    if (chance_bp > BP_BASE)
        chance_bp = BP_BASE;
    phase_report = query_boss_phase_budget_report(([
        "hp_ratio_bp": query_actor_hp_ratio_bp(npc),
        "skill_window_count": sizeof(raw_skills),
    ]));

    skills = ({ });
    foreach (raw_skill in raw_skills) {
        raw_config = ([ ]);
        if (stringp(raw_skill)) {
            skill_id = raw_skill;
        } else if (mapp(raw_skill)) {
            raw_config = copy(raw_skill);
            skill_id = raw_skill["id"] || "";
        } else {
            continue;
        }
        if (skill_id == "")
            continue;
        if (!intp(raw_config["chance_bp"]) || raw_config["chance_bp"] <= 0)
            raw_config["chance_bp"] = chance_bp;
        if (skill_id == "heavy_strike" && stringp(raw_skill)) {
            if (intp(npc->query("boss_strike_damage")))
                raw_config["damage"] = npc->query("boss_strike_damage");
            else if (intp(npc->query_temp("numeric/npc_combat_skill/damage")))
                raw_config["damage"] = npc->query_temp("numeric/npc_combat_skill/damage");
            if (intp(npc->query("boss_strike_variance")))
                raw_config["variance"] = npc->query("boss_strike_variance");
            else if (intp(npc->query_temp("numeric/npc_combat_skill/variance")))
                raw_config["variance"] = npc->query_temp("numeric/npc_combat_skill/variance");
        }
        skill = normalize_npc_combat_skill_definition(skill_id, raw_config);
        if (!mapp(skill) || !skill["enabled"])
            continue;
        budget_report = query_npc_combat_skill_budget_report(skill);
        skill["budget"] = budget_report;
        if (objectp(target))
            skill["conditions"] = query_npc_combat_skill_condition_report(npc, target, skill);
        skill["phase_budget"] = phase_report;
        skill["cooldown_remaining"] = query_npc_skill_cooldown_remaining(npc, skill_id);
        skills += ({ skill });
    }

    return ([
        "ok": 1,
        "npc": npc,
        "target": target,
        "chance_bp": chance_bp,
        "phase_budget": phase_report,
        "skills": skills,
        "skill_count": sizeof(skills),
    ]);
}
mapping query_npc_combat_skill_trigger(object npc, object target)
{
    mapping snapshot;
    mapping skill;
    mapping *candidates;
    mapping *skills;

    if (!objectp(npc) || !objectp(target))
        return ([ "ok": 0, "trigger": 0, "error": "invalid_arguments" ]);

    snapshot = query_npc_combat_skill_runtime_snapshot(npc, target);
    if (!mapp(snapshot) || !snapshot["ok"])
        return snapshot + ([ "trigger": 0 ]);
    skills = snapshot["skills"];
    if (!arrayp(skills) || sizeof(skills) == 0)
        return snapshot + ([ "trigger": 0, "reason": "no_skills" ]);

    candidates = ({ });
    foreach (skill in skills) {
        if (!mapp(skill) || (skill["cooldown_remaining"] || 0) > 0)
            continue;
        if (mapp(skill["budget"]) && !skill["budget"]["valid"] && skill["block_over_budget"])
            continue;
        if (mapp(skill["conditions"]) && !skill["conditions"]["valid"])
            continue;
        candidates += ({ skill });
    }
    if (sizeof(candidates) == 0)
        return snapshot + ([ "trigger": 0, "reason": "no_available_skill" ]);

    skill = candidates[random(sizeof(candidates))];
    if ((skill["chance_bp"] || 0) <= 0 || random(BP_BASE) >= (skill["chance_bp"] || 0))
        return snapshot + ([ "trigger": 0, "reason": "chance_miss", "selected": skill ]);

    return snapshot + ([ "trigger": 1, "selected": skill ]);
}
int record_npc_combat_skill_trigger(object npc, mapping trigger)
{
    mapping skill;
    string skill_id;
    int cooldown;

    if (!objectp(npc) || !mapp(trigger) || !trigger["trigger"])
        return 0;
    skill = trigger["selected"];
    if (!mapp(skill))
        return 0;
    skill_id = skill["id"] || "";
    if (skill_id == "")
        return 0;
    cooldown = skill["cooldown_seconds"] || 0;
    if (cooldown > 0)
        npc->set_temp("numeric/npc_combat_skill/cooldown/" + skill_id, time() + cooldown);
    return 1;
}
