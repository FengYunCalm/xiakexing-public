/**
 * @file    numeric_rule/skill_breakthrough.c
 * @brief   武学突破词条预算与投影
 *
 * @par Description:
 * 含 {query_skill_breakthrough_budget_framework, query_skill_breakthrough_conflict_framework, normalize_breakthrough_entry, query_breakthrough_budget_int, query_skill_breakthrough_entries, query_skill_breakthrough_levels, query_breakthrough_entry_schema_violations, query_breakthrough_status_limit_violations, query_breakthrough_action_modifier_violations, absolute_budget_value, query_skill_breakthrough_tier_budget_limit, query_breakthrough_domain_weight_bp, query_breakthrough_effect_weight, query_breakthrough_effect_budget_score, query_breakthrough_average_factor, query_breakthrough_action_average_factor, add_breakthrough_domain_score, query_breakthrough_action_modifier_weight, query_breakthrough_action_modifier_score, query_breakthrough_status_modifier_score, query_breakthrough_status_requirement_modifier_score, query_breakthrough_action_status_effect_score, query_breakthrough_action_behavior, query_breakthrough_base_status_effect, apply_breakthrough_status_modifier, entry_has_breakthrough_payload, query_skill_breakthrough_entry_budget, build_breakthrough_tier_budget_report, query_skill_breakthrough_entry_set_budget_report, query_skill_breakthrough_budget_report, add_breakthrough_numeric_values, scale_breakthrough_numeric_values, query_breakthrough_passive_effects, query_breakthrough_action_effects, query_breakthrough_action_modifiers, merge_breakthrough_status_modifier, add_breakthrough_status_modifiers, query_breakthrough_status_requirement_modifiers, add_breakthrough_status_requirement_modifiers, query_breakthrough_action_status_effects, add_breakthrough_action_status_effects, apply_breakthrough_action_context, project_skill_breakthrough_action, query_skill_breakthrough_action_projection, query_next_skill_breakthrough_level, query_skill_breakthrough_state, query_skill_breakthrough_numeric_effect, query_skill_breakthrough_action_modifiers}。
 *
 * 本模块是 skill_breakthrough 域纯规则片段，由 `numeric_rule_d.c` 继承；
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
mapping query_combat_status_effect_budget_report(mapping effect);
int query_combat_status_effect_budget_score(mapping effect);
mapping query_combat_status_effect_framework();
int query_mapping_int(mapping source, string key, int default_value);
mapping query_section(string section_name);
mapping *query_skill_breakthrough_definitions(string skill_id);
int query_skill_breakthrough_interval();
int query_skill_breakthrough_max_tier();
int query_skill_passive_weight_bp(object player, string skill_id);
int skill_breakthrough_numeric_domain_supported(string skill_id, string domain);
varargs mapping query_skill_breakthrough_action_modifiers(object player, string skill_id, string domain, object target, string action_id);
varargs mapping query_skill_breakthrough_action_projection(object player, string skill_id, string domain, object target, string action_id, string active_slot);
varargs mapping query_skill_breakthrough_numeric_effect(object player, string skill_id, string domain, object target);
mapping query_skill_breakthrough_budget_framework()
{
    mapping budget;
    mapping framework;

    framework = query_section("skill_breakthrough");
    budget = mapp(framework) ? framework["budget"] : 0;
    return mapp(budget) ? copy(budget) : ([]);
}
mapping query_skill_breakthrough_conflict_framework()
{
    mapping conflict;
    mapping framework;

    framework = query_section("skill_breakthrough");
    conflict = mapp(framework) ? framework["conflict"] : 0;
    return mapp(conflict) ? copy(conflict) : ([]);
}
int *query_skill_breakthrough_levels()
{
    mapping section;
    mixed levels;
    int *result;
    int i;

    section = query_section("skill_breakthrough");
    levels = mapp(section) ? section["breakthrough_levels"] : 0;
    if (!arrayp(levels) || sizeof(levels) == 0) {
        /* 旧配置兼容：无节点表时退化为 interval 均匀节点。 */
        int interval;
        int max_tier;
        int level;

        interval = query_skill_breakthrough_interval();
        max_tier = query_skill_breakthrough_max_tier();
        result = ({ });
        for (i = 1, level = interval; i <= max_tier; i++, level += interval)
            result += ({ level });
        return result;
    }
    result = ({ });
    for (i = 0; i < sizeof(levels); i++) {
        if (intp(levels[i]) && levels[i] > 0)
            result += ({ levels[i] });
    }
    return result;
}
 mapping normalize_breakthrough_entry(string skill_id, mixed raw_tier, mapping raw_entry)
{
    mapping entry;
    int tier;

    if (!stringp(skill_id) || skill_id == "" || !mapp(raw_entry))
        return 0;

    tier = intp(raw_entry["tier"]) ? raw_entry["tier"] : (intp(raw_tier) ? raw_tier : 0);
    if (tier <= 0 || tier > query_skill_breakthrough_max_tier())
        return 0;

    entry = copy(raw_entry);
    entry["skill_id"] = skill_id;
    entry["tier"] = tier;
    if (!stringp(entry["id"]) || entry["id"] == "")
        entry["id"] = sprintf("%s_breakthrough_%d", skill_id, tier);
    return entry;
}
private int query_breakthrough_budget_int(string key, int default_value)
{
    mapping budget;

    budget = query_skill_breakthrough_budget_framework();
    return query_mapping_int(budget, key, default_value);
}
private int absolute_budget_value(int value)
{
    return value < 0 ? -value : value;
}
int query_skill_breakthrough_tier_budget_limit(int tier)
{
    int base_budget;
    int growth;

    if (tier <= 0)
        tier = 1;
    base_budget = query_breakthrough_budget_int("base_budget", 600);
    growth = query_breakthrough_budget_int("budget_growth_per_tier", 250);
    return base_budget + (tier - 1) * growth;
}
private int query_breakthrough_domain_weight_bp(string domain)
{
    mapping budget;
    mapping weights;

    budget = query_skill_breakthrough_budget_framework();
    weights = mapp(budget["domain_weights_bp"]) ? budget["domain_weights_bp"] : ([]);
    return query_mapping_int(weights, domain, BP_BASE);
}
private int query_breakthrough_effect_weight(string key)
{
    mapping budget;
    mapping weights;

    budget = query_skill_breakthrough_budget_framework();
    weights = mapp(budget["effect_weights"]) ? budget["effect_weights"] : ([]);
    return query_mapping_int(weights, key,
        query_breakthrough_budget_int("default_effect_weight", 100));
}
int query_breakthrough_effect_budget_score(string domain, mapping effects)
{
    string key;
    int domain_weight;
    int score;
    int value;

    if (!stringp(domain) || domain == "" || !mapp(effects))
        return 0;

    domain_weight = query_breakthrough_domain_weight_bp(domain);
    foreach (key, value in effects || ([])) {
        if (!stringp(key) || !intp(value))
            continue;
        score += absolute_budget_value(value) * query_breakthrough_effect_weight(key)
            * domain_weight / BP_BASE;
    }
    return score;
}
private int query_breakthrough_average_factor(mapping spec)
{
    mapping framework;
    int pve_factor;
    int pvp_factor;

    framework = query_section("skill_breakthrough");
    pvp_factor = mapp(spec) && intp(spec["pvp_factor_bp"])
        ? spec["pvp_factor_bp"] : query_mapping_int(framework,
            "default_pvp_factor_bp", BP_BASE);
    pve_factor = mapp(spec) && intp(spec["pve_factor_bp"])
        ? spec["pve_factor_bp"] : query_mapping_int(framework,
            "default_pve_factor_bp", BP_BASE);
    if (pvp_factor <= 0)
        pvp_factor = BP_BASE;
    if (pve_factor <= 0)
        pve_factor = BP_BASE;
    return (pvp_factor + pve_factor) / 2;
}
private int query_breakthrough_action_average_factor(mapping entry,
    string action_id)
{
    mapping factors;
    mapping spec;

    spec = mapp(entry) ? copy(entry) : ([]);
    factors = mapp(entry) && mapp(entry["action_pvp_factor_bp"])
        ? entry["action_pvp_factor_bp"] : ([]);
    if (stringp(action_id) && intp(factors[action_id]))
        spec["pvp_factor_bp"] = factors[action_id];
    factors = mapp(entry) && mapp(entry["action_pve_factor_bp"])
        ? entry["action_pve_factor_bp"] : ([]);
    if (stringp(action_id) && intp(factors[action_id]))
        spec["pve_factor_bp"] = factors[action_id];
    return query_breakthrough_average_factor(spec);
}
private void add_breakthrough_domain_score(mapping scores, string domain, int score)
{
    if (!mapp(scores) || !stringp(domain) || domain == "" || score == 0)
        return;
    scores[domain] = (scores[domain] || 0) + score;
}
private int query_breakthrough_action_modifier_weight(string key, int fallback)
{
    mapping budget;
    mapping weights;
    string group;

    budget = query_skill_breakthrough_budget_framework();
    weights = mapp(budget["action_modifier_weights"])
        ? budget["action_modifier_weights"] : ([]);
    group = "";
    if (strsrch(key, "_cost_percent_delta") > 0)
        group = "resource_cost_percent";
    else if (strsrch(key, "_cost_delta") > 0)
        group = "resource_cost";
    else if (key == "busy_delta")
        group = "busy";
    else if (key == "cooldown_delta")
        group = "cooldown";
    else if (key == "fatigue_per_use_bp_delta")
        group = "fatigue";
    return group != "" ? query_mapping_int(weights, group, fallback) : fallback;
}
private int query_breakthrough_action_modifier_score(mapping modifiers)
{
    string key;
    int score;
    int value;

    foreach (key, value in modifiers || ([])) {
        if (!stringp(key) || !intp(value))
            continue;
        if (strsrch(key, "_cost_percent_delta") > 0
            || strsrch(key, "_cost_delta") > 0 || key == "busy_delta"
            || key == "cooldown_delta" || key == "fatigue_per_use_bp_delta")
            score -= value * query_breakthrough_action_modifier_weight(key, 50);
    }
    return score;
}
private int query_breakthrough_status_modifier_score(mapping modifier)
{
    int duration_weight;
    int score;
    int stack_weight;

    if (!mapp(modifier))
        return 0;
    duration_weight = query_breakthrough_budget_int(
        "status_modifier_duration_weight", 100);
    stack_weight = query_breakthrough_budget_int("status_modifier_stack_weight", 180);
    score += (modifier["duration_rounds_delta"] || 0) * duration_weight;
    score += (modifier["max_stacks_delta"] || 0) * stack_weight;
    score += query_breakthrough_effect_budget_score("debuff",
        mapp(modifier["stat_effects"]) ? modifier["stat_effects"] : ([]));
    return score;
}
private int query_breakthrough_status_requirement_modifier_score(mapping modifiers)
{
    mapping values;
    string requirement_key;
    string status_id;
    int score;
    int value;
    int weight;

    if (!mapp(modifiers))
        return 0;
    weight = query_breakthrough_budget_int("status_requirement_weight", 150);
    foreach (requirement_key in ({ "requires_self_status", "requires_target_status",
        "consume_self_status", "consume_target_status" })) {
        values = mapp(modifiers[requirement_key])
            ? modifiers[requirement_key] : ([]);
        foreach (status_id, value in values || ([])) {
            if (stringp(status_id) && intp(value))
                score -= value * weight;
        }
    }
    return score;
}
private int query_breakthrough_action_status_effect_score(mapping effect)
{
    int factor;
    int score;

    if (!mapp(effect))
        return 0;
    score = query_combat_status_effect_budget_score(effect);
    factor = query_breakthrough_average_factor(effect);
    score = score * factor / BP_BASE;
    return score * query_breakthrough_budget_int(
        "action_status_score_factor_bp", 2000) / BP_BASE;
}
private mapping query_breakthrough_action_behavior(string skill_id, string action_id)
{
    mapping action;
    mapping behavior;
    mapping *actions;
    string action_type;

    if (!stringp(skill_id) || skill_id == "" || !stringp(action_id)
        || action_id == "")
        return ([]);
    foreach (action_type in ({ "perform", "exert" })) {
        actions = SKILL_BEHAVIOR_D->query_skill_actions(action_type, skill_id);
        foreach (action in actions || ({ })) {
            if (!mapp(action) || action["id"] != action_id)
                continue;
            behavior = mapp(action["behavior"]) ? action["behavior"] : action;
            return copy(behavior);
        }
    }
    return ([]);
}
private mapping query_breakthrough_base_status_effect(string skill_id,
    string status_id)
{
    mapping action;
    mapping behavior;
    mapping effect;
    mixed *effects;
    mixed raw_effect;
    mapping *actions;
    string action_type;
    string field;

    if (!stringp(skill_id) || skill_id == "" || !stringp(status_id)
        || status_id == "")
        return ([]);
    foreach (action_type in ({ "perform", "exert" })) {
        actions = SKILL_BEHAVIOR_D->query_skill_actions(action_type, skill_id);
        foreach (action in actions || ({ })) {
            if (!mapp(action))
                continue;
            behavior = mapp(action["behavior"]) ? action["behavior"] : action;
            foreach (field in ({ "status_effects", "self_status_effects" })) {
                effects = arrayp(behavior[field]) ? behavior[field]
                    : (arrayp(action[field]) ? action[field] : ({ }));
                foreach (raw_effect in effects) {
                    effect = mapp(raw_effect) ? raw_effect : 0;
                    if (mapp(effect) && effect["id"] == status_id)
                        return copy(effect);
                }
            }
        }
    }
    return ([]);
}
private mapping *query_breakthrough_status_limit_violations(mapping effect,
    string location)
{
    mapping defaults;
    mapping framework;
    mapping limits;
    mapping violation;
    mapping *violations;
    mixed allowed_types;
    string effect_type;
    int duration;
    int max_duration;
    int max_stacks;
    int stack_limit;

    violations = ({ });
    if (!mapp(effect))
        return ({ ([ "type": "invalid_status_effect", "location": location ]) });
    framework = query_combat_status_effect_framework();
    defaults = mapp(framework["defaults"]) ? framework["defaults"] : ([]);
    limits = mapp(framework["limits"]) ? framework["limits"] : ([]);
    allowed_types = arrayp(framework["allowed_types"])
        ? framework["allowed_types"] : ({ });
    effect_type = stringp(effect["type"]) && effect["type"] != ""
        ? effect["type"] : "buff";
    duration = intp(effect["duration_rounds"]) ? effect["duration_rounds"]
        : (defaults["duration_rounds"] || 1);
    max_stacks = intp(effect["max_stacks"]) ? effect["max_stacks"]
        : (defaults["max_stacks"] || 1);
    max_duration = limits["max_duration_rounds"] || 6;
    stack_limit = limits["max_stacks"] || 3;
    if (!stringp(effect["id"]) || effect["id"] == "")
        violations += ({ ([ "type": "missing_status_id", "location": location ]) });
    if (sizeof(allowed_types) > 0 && member_array(effect_type, allowed_types) == -1)
        violations += ({ ([ "type": "invalid_status_type", "location": location,
            "status_type": effect_type ]) });
    if (duration < 1 || duration > max_duration) {
        violation = ([ "type": "status_duration_out_of_range", "location": location,
            "duration_rounds": duration, "limit": max_duration ]);
        violations += ({ violation });
    }
    if (max_stacks < 1 || max_stacks > stack_limit) {
        violation = ([ "type": "status_stacks_out_of_range", "location": location,
            "max_stacks": max_stacks, "limit": stack_limit ]);
        violations += ({ violation });
    }
    return violations;
}
private mapping apply_breakthrough_status_modifier(mapping base,
    mapping modifier)
{
    mapping effect;
    mapping stats;
    string key;
    int value;

    if (!mapp(base))
        return ([]);
    effect = copy(base);
    if (!mapp(modifier))
        return effect;
    effect["duration_rounds"] = (effect["duration_rounds"] || 1)
        + (modifier["duration_rounds_delta"] || 0);
    effect["max_stacks"] = (effect["max_stacks"] || 1)
        + (modifier["max_stacks_delta"] || 0);
    if (mapp(modifier["stat_effects"])) {
        stats = mapp(effect["stat_effects"]) ? copy(effect["stat_effects"]) : ([]);
        foreach (key, value in modifier["stat_effects"])
            if (stringp(key) && intp(value))
                stats[key] = (stats[key] || 0) + value;
        effect["stat_effects"] = stats;
    }
    return effect;
}
private mapping *query_breakthrough_action_modifier_violations(mapping entry,
    string action_id, mapping modifiers)
{
    mapping action;
    mapping costs;
    mapping violation;
    mapping *violations;
    string resource;
    int amount;
    int busy;
    int cooldown;
    int minimum_amount;
    int minimum_percent;
    int percent;

    violations = ({ });
    if (!mapp(modifiers))
        return violations;
    minimum_percent = query_breakthrough_budget_int(
        "minimum_resource_cost_percent", 10);
    if (minimum_percent < 1)
        minimum_percent = 1;
    foreach (resource in ({ "hp", "mp", "ep", "neili" })) {
        percent = modifiers[resource + "_cost_percent_delta"] || 0;
        if (percent < minimum_percent - 100)
            violations += ({ ([ "type": "resource_discount_below_floor",
                "action_id": action_id, "resource": resource,
                "percent_delta": percent, "minimum_percent": minimum_percent ]) });
    }
    action = query_breakthrough_action_behavior(entry["skill_id"] || "", action_id);
    if (!mapp(action))
        return violations;
    busy = (action["busy"] || 0) + (modifiers["busy_delta"] || 0);
    cooldown = (action["cooldown"] || 0) + (modifiers["cooldown_delta"] || 0);
    if (busy < query_breakthrough_budget_int("minimum_action_busy", 0))
        violations += ({ ([ "type": "busy_below_floor", "action_id": action_id,
            "busy": busy ]) });
    if (cooldown < query_breakthrough_budget_int("minimum_action_cooldown", 0))
        violations += ({ ([ "type": "cooldown_below_floor", "action_id": action_id,
            "cooldown": cooldown ]) });
    costs = mapp(action["resource_costs"]) ? action["resource_costs"] : ([]);
    foreach (resource, amount in costs || ([])) {
        if (!stringp(resource) || !intp(amount) || amount <= 0)
            continue;
        percent = modifiers[resource + "_cost_percent_delta"] || 0;
        amount = amount * (100 + percent) / 100;
        amount += modifiers[resource + "_cost_delta"] || 0;
        minimum_amount = ((costs[resource] || 0) * minimum_percent + 99) / 100;
        if (amount < minimum_amount) {
            violation = ([ "type": "resource_cost_below_floor", "action_id": action_id,
                "resource": resource, "cost": amount,
                "minimum_cost": minimum_amount ]);
            violations += ({ violation });
        }
    }
    return violations;
}
private int entry_has_breakthrough_payload(mapping entry)
{
    string field;

    foreach (field in ({ "effects", "numeric_effects", "passive_effects",
        "action_effects", "action_modifiers", "status_modifiers",
        "status_requirement_modifiers", "action_status_effects" }))
        if (mapp(entry[field]) && sizeof(entry[field]) > 0)
            return 1;
    return 0;
}
private mapping *query_breakthrough_entry_schema_violations(mapping entry)
{
    mapping framework;
    mapping schema;
    mapping *violations;
    mixed allowed_domains;
    mixed optional_fields;
    mixed required_fields;
    string field;
    string key;

    violations = ({ });
    if (!mapp(entry))
        return ({ ([ "type": "invalid_entry" ]) });
    framework = query_section("skill_breakthrough");
    schema = mapp(framework["entry_schema"]) ? framework["entry_schema"] : ([]);
    required_fields = arrayp(schema["required_fields"])
        ? schema["required_fields"] : ({ });
    optional_fields = arrayp(schema["optional_fields"])
        ? schema["optional_fields"] : ({ });
    allowed_domains = arrayp(schema["allowed_domains"])
        ? schema["allowed_domains"] : ({ });
    foreach (field in required_fields || ({ })) {
        if (!stringp(field) || undefinedp(entry[field]))
            violations += ({ ([ "type": "missing_required_field", "field": field ]) });
    }
    if (stringp(entry["domain"]) && sizeof(allowed_domains) > 0
        && member_array(entry["domain"], allowed_domains) == -1)
        violations += ({ ([ "type": "invalid_domain", "domain": entry["domain"] ]) });
    foreach (key in keys(entry)) {
        if (!stringp(key) || member_array(key, required_fields) != -1
            || member_array(key, optional_fields) != -1)
            continue;
        violations += ({ ([ "type": "unknown_entry_field", "field": key ]) });
    }
    if (!entry_has_breakthrough_payload(entry))
        violations += ({ ([ "type": "missing_effect_payload" ]) });
    return violations;
}
mapping query_skill_breakthrough_entry_budget(mapping entry)
{
    mapping action_effects;
    mapping action_modifiers;
    mapping action_status_effects;
    mapping action_status_report;
    mapping domains;
    mapping domain_scores;
    mapping effect;
    mapping passive_effects;
    mapping status_modifier;
    mapping status_modifiers;
    mapping status_requirement_modifiers;
    mapping *schema_violations;
    mapping *status_effect_reports;
    mapping *violations;
    mixed raw_domain;
    mixed raw_effects;
    mixed raw_status_effect;
    string domain;
    string action_id;
    string owner;
    string status_id;
    int declared_score;
    int factor;
    int score;
    int action_score;
    int tier;

    if (!mapp(entry))
        return ([ "ok": 0, "error": "invalid_entry" ]);

    domain_scores = ([]);
    violations = query_breakthrough_entry_schema_violations(entry);
    schema_violations = violations + ({ });
    status_effect_reports = ({ });
    domains = mapp(entry["numeric_effects"]) ? entry["numeric_effects"] : 0;
    if (mapp(domains)) {
        foreach (raw_domain, raw_effects in domains) {
            if (!stringp(raw_domain) || !mapp(raw_effects))
                continue;
            domain = raw_domain;
            action_score = query_breakthrough_effect_budget_score(domain, raw_effects);
            action_score = action_score * query_breakthrough_average_factor(entry) / BP_BASE;
            add_breakthrough_domain_score(domain_scores, "numeric:" + domain,
                action_score);
            score += action_score;
        }
    } else if (stringp(entry["domain"]) && mapp(entry["effects"])) {
        domain = entry["domain"];
        action_score = query_breakthrough_effect_budget_score(domain, entry["effects"]);
        action_score = action_score * query_breakthrough_average_factor(entry) / BP_BASE;
        add_breakthrough_domain_score(domain_scores, "legacy:" + domain,
            action_score);
        score += action_score;
    }

    passive_effects = mapp(entry["passive_effects"])
        ? entry["passive_effects"] : ([]);
    foreach (raw_domain, raw_effects in passive_effects || ([])) {
        if (!stringp(raw_domain) || !mapp(raw_effects))
            continue;
        action_score = query_breakthrough_effect_budget_score(raw_domain, raw_effects);
        action_score = action_score * query_breakthrough_average_factor(entry) / BP_BASE;
        add_breakthrough_domain_score(domain_scores, "passive:" + raw_domain,
            action_score);
        score += action_score;
    }

    action_effects = mapp(entry["action_effects"])
        ? entry["action_effects"] : ([]);
    foreach (action_id, raw_effects in action_effects || ([])) {
        if (!stringp(action_id) || !mapp(raw_effects))
            continue;
        factor = query_breakthrough_action_average_factor(entry, action_id);
        action_score = query_breakthrough_effect_budget_score("perform", raw_effects);
        action_score = action_score * factor / BP_BASE;
        add_breakthrough_domain_score(domain_scores, "action:" + action_id,
            action_score);
        score += action_score;
    }

    action_modifiers = mapp(entry["action_modifiers"])
        ? entry["action_modifiers"] : ([]);
    foreach (action_id, raw_effects in action_modifiers || ([])) {
        if (!stringp(action_id) || !mapp(raw_effects))
            continue;
        action_score = query_breakthrough_action_modifier_score(raw_effects);
        add_breakthrough_domain_score(domain_scores, "modifier:" + action_id,
            action_score);
        score += action_score;
        violations += query_breakthrough_action_modifier_violations(entry,
            action_id, raw_effects);
    }

    status_modifiers = mapp(entry["status_modifiers"])
        ? entry["status_modifiers"] : ([]);
    foreach (status_id, status_modifier in status_modifiers || ([])) {
        if (!stringp(status_id) || !mapp(status_modifier)) {
            violations += ({ ([ "type": "invalid_status_modifier",
                "status_id": status_id ]) });
            continue;
        }
        action_score = query_breakthrough_status_modifier_score(status_modifier);
        add_breakthrough_domain_score(domain_scores, "status:" + status_id,
            action_score);
        score += action_score;
        effect = query_breakthrough_base_status_effect(entry["skill_id"] || "",
            status_id);
        if (mapp(effect))
            violations += query_breakthrough_status_limit_violations(
                apply_breakthrough_status_modifier(effect, status_modifier),
                "status_modifier:" + status_id);
    }

    status_requirement_modifiers = mapp(entry["status_requirement_modifiers"])
        ? entry["status_requirement_modifiers"] : ([]);
    foreach (action_id, raw_effects in status_requirement_modifiers || ([])) {
        if (!stringp(action_id) || !mapp(raw_effects)) {
            violations += ({ ([ "type": "invalid_status_requirement_modifier",
                "action_id": action_id ]) });
            continue;
        }
        action_score = query_breakthrough_status_requirement_modifier_score(raw_effects);
        add_breakthrough_domain_score(domain_scores,
            "status_requirement:" + action_id, action_score);
        score += action_score;
    }

    action_status_effects = mapp(entry["action_status_effects"])
        ? entry["action_status_effects"] : ([]);
    foreach (action_id, raw_effects in action_status_effects || ([])) {
        if (!stringp(action_id) || !mapp(raw_effects)) {
            violations += ({ ([ "type": "invalid_action_status_effects",
                "action_id": action_id ]) });
            continue;
        }
        foreach (owner in ({ "self", "target" })) {
            foreach (raw_status_effect in raw_effects[owner] || ({ })) {
                effect = mapp(raw_status_effect) ? raw_status_effect : 0;
                if (!mapp(effect)) {
                    violations += ({ ([ "type": "invalid_action_status_effect",
                        "action_id": action_id, "owner": owner ]) });
                    continue;
                }
                action_score = query_breakthrough_action_status_effect_score(effect);
                add_breakthrough_domain_score(domain_scores,
                    "action_status:" + action_id, action_score);
                score += action_score;
                action_status_report = query_combat_status_effect_budget_report(effect);
                action_status_report["action_id"] = action_id;
                action_status_report["owner"] = owner;
                action_status_report["scaled_score"] = action_score;
                status_effect_reports += ({ action_status_report });
                violations += query_breakthrough_status_limit_violations(effect,
                    "action_status:" + action_id + ":" + owner);
            }
        }
    }

    declared_score = intp(entry["budget_score"]) ? entry["budget_score"] : 0;
    if (declared_score > score)
        score = declared_score;
    if (score < 0)
        score = 0;
    tier = intp(entry["tier"]) ? entry["tier"] : 0;

    return ([
        "ok": 1,
        "id": entry["id"] || "",
        "tier": tier,
        "score": score,
        "declared_score": declared_score,
        "domain_scores": domain_scores,
        "exclusive_group": entry["exclusive_group"] || "",
        "schema_violations": schema_violations,
        "status_effect_reports": status_effect_reports,
        "violations": violations,
    ]);
}
private mapping build_breakthrough_tier_budget_report(mapping tier_report, int tier)
{
    if (mapp(tier_report))
        return tier_report;
    return ([
        "tier": tier,
        "limit": query_skill_breakthrough_tier_budget_limit(tier),
        "used": 0,
        "max_entry_score": 0,
        "entry_count": 0,
        "entry_ids": ({ }),
        "over_budget": 0,
    ]);
}
mapping query_skill_breakthrough_entry_set_budget_report(string skill_id, mixed *entries)
{
    mapping budget;
    mapping conflict;
    mapping entry;
    mapping entry_budget;
    mapping entry_violation;
    mapping seen_ids;
    mapping tier_report;
    mapping tier_reports;
    mapping violation;
    mapping *entry_reports;
    mapping *violations;
    mixed raw_entry;
    string id;
    int max_entries;
    int tier;
    int total_score;

    if (!arrayp(entries))
        entries = ({ });

    budget = query_skill_breakthrough_budget_framework();
    conflict = query_skill_breakthrough_conflict_framework();
    max_entries = query_mapping_int(budget, "max_entries_per_tier", 3);
    entry_reports = ({ });
    seen_ids = ([]);
    tier_reports = ([]);
    violations = ({ });

    foreach (raw_entry in entries) {
        if (!mapp(raw_entry))
            continue;
        entry = raw_entry;
        entry_budget = query_skill_breakthrough_entry_budget(entry);
        entry_reports += ({ entry_budget });
        tier = entry_budget["tier"] || 0;
        id = entry_budget["id"] || "";
        total_score += entry_budget["score"] || 0;

        foreach (entry_violation in entry_budget["violations"] || ({ })) {
            if (!mapp(entry_violation))
                continue;
            violation = copy(entry_violation);
            if (!stringp(violation["id"]) || violation["id"] == "")
                violation["id"] = id;
            if (!intp(violation["tier"]) || violation["tier"] <= 0)
                violation["tier"] = tier;
            violations += ({ violation });
        }

        if (tier <= 0) {
            violations += ({ ([ "type": "invalid_tier", "id": id, "tier": tier ]) });
            continue;
        }
        tier_report = build_breakthrough_tier_budget_report(tier_reports[tier], tier);
        if ((entry_budget["score"] || 0) > (tier_report["max_entry_score"] || 0))
            tier_report["max_entry_score"] = entry_budget["score"] || 0;
        /* 兼容旧诊断字段：used 表示该 tier 的最大单候选分数，
         * 不表示候选池求和。 */
        tier_report["used"] = tier_report["max_entry_score"];
        tier_report["entry_count"] += 1;
        tier_report["entry_ids"] += ({ id });
        tier_reports[tier] = tier_report;

        if (max_entries > 0 && tier_report["entry_count"] > max_entries) {
            violations += ({ ([ "type": "tier_entry_limit", "tier": tier,
                "limit": max_entries, "count": tier_report["entry_count"] ]) });
        }
        if (id != "" && conflict["duplicate_id_is_conflict"] && !undefinedp(seen_ids[id])) {
            violations += ({ ([ "type": "duplicate_id", "id": id,
                "first_tier": seen_ids[id], "tier": tier ]) });
        }
        if (id != "")
            seen_ids[id] = tier;

        if ((entry_budget["score"] || 0) > (tier_report["limit"] || 0)) {
            violations += ({ ([ "type": "entry_budget_exceeded", "tier": tier,
                "id": id, "used": entry_budget["score"], "limit": tier_report["limit"],
                "over": (entry_budget["score"] || 0) - (tier_report["limit"] || 0) ]) });
        }
    }

    foreach (tier, tier_report in tier_reports || ([])) {
        if ((tier_report["max_entry_score"] || 0) > (tier_report["limit"] || 0))
            tier_report["over_budget"] = tier_report["max_entry_score"]
                - tier_report["limit"];
        tier_reports[tier] = tier_report;
    }

    return ([
        "ok": 1,
        "valid": sizeof(violations) == 0,
        "skill_id": stringp(skill_id) ? skill_id : "",
        "entry_count": sizeof(entry_reports),
        "total_score": total_score,
        "tier_reports": tier_reports,
        "entries": entry_reports,
        "violations": violations,
        "budget_unit": budget["budget_unit"] || "effect_score",
        "conflict_scope": conflict["exclusive_group_scope"] || "skill_tier",
    ]);
}
mapping query_skill_breakthrough_budget_report(string skill_id)
{
    return query_skill_breakthrough_entry_set_budget_report(skill_id,
        query_skill_breakthrough_definitions(skill_id));
}
mapping *query_skill_breakthrough_entries(object player, string skill_id)
{
    if (!objectp(player) || !stringp(skill_id) || skill_id == "")
        return ({ });
    return SKILL_BREAKTHROUGH_D->query_selected_entries(player, skill_id);
}
private void add_breakthrough_numeric_values(mapping result, mapping values)
{
    string key;
    mixed value;

    if (!mapp(result) || !mapp(values))
        return;
    foreach (key, value in values) {
        if (stringp(key) && intp(value))
            result[key] = (result[key] || 0) + value;
    }
}
private mapping scale_breakthrough_numeric_values(mapping values, int factor_bp)
{
    mapping result;
    string key;
    int value;

    result = ([ ]);
    if (!mapp(values) || factor_bp <= 0)
        return result;
    foreach (key, value in values) {
        if (stringp(key) && intp(value))
            result[key] = value * factor_bp / BP_BASE;
    }
    return result;
}
private mapping query_breakthrough_passive_effects(mapping entry, string domain)
{
    mapping domains;
    mapping effects;

    if (!mapp(entry) || !stringp(domain) || domain == "")
        return ([]);
    domains = mapp(entry["passive_effects"]) ? entry["passive_effects"] : 0;
    if (mapp(domains) && mapp(domains[domain]))
        return copy(domains[domain]);
    domains = mapp(entry["numeric_effects"]) ? entry["numeric_effects"] : 0;
    if (mapp(domains) && mapp(domains[domain]))
        return copy(domains[domain]);
    if (entry["domain"] == domain && mapp(entry["effects"])) {
        effects = entry["effects"];
        return copy(effects);
    }
    return ([]);
}
private mapping query_breakthrough_action_effects(mapping entry, string domain,
    string action_id)
{
    mapping effects;
    mapping by_action;

    if (!mapp(entry) || !stringp(domain) || domain == "" || action_id == "")
        return ([]);
    by_action = mapp(entry["action_effects"]) ? entry["action_effects"] : 0;
    if (mapp(by_action)) {
        effects = mapp(by_action[action_id]) ? by_action[action_id] : by_action["*"];
        return mapp(effects) ? copy(effects) : ([]);
    }
    if (entry["domain"] == domain && mapp(entry["effects"]))
        return copy(entry["effects"]);
    return ([]);
}
private mapping query_breakthrough_action_modifiers(mapping entry, string domain,
    string action_id)
{
    mapping modifiers;
    mapping by_action;
    mixed value;

    if (!mapp(entry) || !stringp(domain) || domain == "")
        return ([]);
    modifiers = mapp(entry["action_modifiers"]) ? entry["action_modifiers"] : 0;
    if (!mapp(modifiers))
        return ([]);
    if (action_id != "") {
        by_action = mapp(modifiers[action_id]) ? modifiers[action_id] : modifiers["*"];
        if (mapp(by_action))
            return copy(by_action);
    }
    foreach (value in values(modifiers))
        if (mapp(value))
            return ([]);
    return entry["domain"] == domain ? copy(modifiers) : ([]);
}
private void merge_breakthrough_status_modifier(mapping result, string status_id,
    mapping modifier)
{
    mapping current;
    mapping stats;
    string key;
    mixed value;

    if (!mapp(result) || !stringp(status_id) || status_id == "" || !mapp(modifier))
        return;
    current = mapp(result[status_id]) ? result[status_id] : ([]);
    foreach (key, value in modifier) {
        if (key == "stat_effects" && mapp(value)) {
            stats = mapp(current["stat_effects"]) ? current["stat_effects"] : ([]);
            add_breakthrough_numeric_values(stats, value);
            current["stat_effects"] = stats;
        } else if (stringp(key) && intp(value))
            current[key] = (current[key] || 0) + value;
    }
    result[status_id] = current;
}
private void add_breakthrough_status_modifiers(mapping result, mapping entry)
{
    mapping modifiers;
    mapping modifier;
    string status_id;

    modifiers = mapp(entry) && mapp(entry["status_modifiers"])
        ? entry["status_modifiers"] : 0;
    foreach (status_id, modifier in modifiers || ([]))
        merge_breakthrough_status_modifier(result, status_id, modifier);
}
private mapping query_breakthrough_status_requirement_modifiers(mapping entry,
    string action_id)
{
    mapping by_action;
    mapping modifiers;

    if (!mapp(entry) || !stringp(action_id) || action_id == "")
        return ([]);
    by_action = mapp(entry["status_requirement_modifiers"])
        ? entry["status_requirement_modifiers"] : 0;
    if (!mapp(by_action))
        return ([]);
    modifiers = mapp(by_action[action_id]) ? by_action[action_id] : by_action["*"];
    return mapp(modifiers) ? copy(modifiers) : ([]);
}
private void add_breakthrough_status_requirement_modifiers(mapping result,
    mapping entry, string action_id)
{
    mapping current;
    mapping modifiers;
    mapping status_deltas;
    string requirement_key;
    string status_id;
    int delta;

    if (!mapp(result))
        return;
    modifiers = query_breakthrough_status_requirement_modifiers(entry, action_id);
    foreach (requirement_key in ({
        "requires_self_status",
        "requires_target_status",
        "consume_self_status",
        "consume_target_status",
    })) {
        status_deltas = mapp(modifiers[requirement_key])
            ? modifiers[requirement_key] : 0;
        if (!mapp(status_deltas))
            continue;
        current = mapp(result[requirement_key]) ? result[requirement_key] : ([ ]);
        foreach (status_id, delta in status_deltas) {
            if (!stringp(status_id) || status_id == "" || !intp(delta) || delta == 0)
                continue;
            current[status_id] = (current[status_id] || 0) + delta;
        }
        result[requirement_key] = current;
    }
}
private mapping query_breakthrough_action_status_effects(mapping entry,
    string action_id)
{
    mapping by_action;
    mapping effects;

    if (!mapp(entry) || !stringp(action_id) || action_id == "")
        return ([]);
    by_action = mapp(entry["action_status_effects"])
        ? entry["action_status_effects"] : 0;
    if (!mapp(by_action))
        return ([]);
    effects = mapp(by_action[action_id]) ? by_action[action_id] : by_action["*"];
    return mapp(effects) ? copy(effects) : ([]);
}
private void add_breakthrough_action_status_effects(mapping result, mapping entry,
    string action_id)
{
    mapping effects;
    mapping effect;
    mixed raw_effect;
    mixed *owner_effects;
    string owner;

    if (!mapp(result))
        return;
    effects = query_breakthrough_action_status_effects(entry, action_id);
    foreach (owner in ({ "self", "target" })) {
        owner_effects = arrayp(effects[owner]) ? effects[owner] : ({ });
        if (!arrayp(result[owner]))
            result[owner] = ({ });
        foreach (raw_effect in owner_effects) {
            if (!mapp(raw_effect))
                continue;
            effect = copy(raw_effect);
            result[owner] += ({ effect });
        }
    }
}
private mapping apply_breakthrough_action_context(mapping effects, mapping entry,
    string action_id, object player, object target)
{
    mapping context;
    mapping pvp_factors;
    mapping pve_factors;

    if (!mapp(effects))
        return ([]);
    context = mapp(entry) ? copy(entry) : ([]);
    pvp_factors = mapp(entry["action_pvp_factor_bp"])
        ? entry["action_pvp_factor_bp"] : 0;
    pve_factors = mapp(entry["action_pve_factor_bp"])
        ? entry["action_pve_factor_bp"] : 0;
    if (mapp(pvp_factors) && intp(pvp_factors[action_id])
        && pvp_factors[action_id] > 0)
        context["pvp_factor_bp"] = pvp_factors[action_id];
    if (mapp(pve_factors) && intp(pve_factors[action_id])
        && pve_factors[action_id] > 0)
        context["pve_factor_bp"] = pve_factors[action_id];
    return apply_numeric_context_factor(effects, context, player, target);
}
private mapping project_skill_breakthrough_action(mapping *entries, object player,
    string domain, object target, string action_id, string active_slot,
    int include_modifiers, int include_effects)
{
    mapping action_status_effects;
    mapping action_modifiers;
    mapping entry;
    mapping numeric_effects;
    mapping status_modifiers;
    mapping status_requirement_modifiers;
    mapping values;

    action_status_effects = ([ "self": ({ }), "target": ({ }) ]);
    action_modifiers = ([]);
    numeric_effects = ([]);
    status_modifiers = ([]);
    status_requirement_modifiers = ([]);
    foreach (entry in entries || ({ })) {
        if (!mapp(entry))
            continue;

        if (include_modifiers) {
            values = query_breakthrough_action_modifiers(entry, domain, action_id);
            add_breakthrough_numeric_values(action_modifiers, values);
            if (action_id != "") {
                add_breakthrough_status_modifiers(status_modifiers, entry);
                add_breakthrough_status_requirement_modifiers(
                    status_requirement_modifiers, entry, action_id);
            }
        }

        if (!include_effects)
            continue;
        if (action_id != "")
            values = query_breakthrough_action_effects(entry, domain, action_id);
        else
            values = query_breakthrough_passive_effects(entry, domain);
        if (action_id != "")
            add_breakthrough_action_status_effects(action_status_effects, entry,
                action_id);
        if (action_id != "")
            values = apply_breakthrough_action_context(values, entry, action_id,
                player, target);
        else
            values = apply_numeric_context_factor(values, entry, player, target);
        add_breakthrough_numeric_values(numeric_effects, values);
    }

    return ([
        "active_slot": stringp(active_slot) ? active_slot : "",
        "passive_effects": action_id == "" ? copy(numeric_effects) : ([]),
        "action_effects": action_id != "" ? copy(numeric_effects) : ([]),
        "action_modifiers": action_modifiers,
        "status_modifiers": status_modifiers,
        "status_requirement_modifiers": status_requirement_modifiers,
        "action_status_effects": action_status_effects,
        "numeric_effects": numeric_effects,
    ]);
}
varargs mapping query_skill_breakthrough_action_projection(object player,
    string skill_id, string domain, object target, string action_id,
    string active_slot)
{
    mapping *entries;

    if (!objectp(player) || !stringp(skill_id) || skill_id == ""
        || !stringp(domain) || domain == "")
        return ([ "passive_effects": ([]), "action_effects": ([]),
            "action_modifiers": ([]), "status_modifiers": ([]),
            "status_requirement_modifiers": ([]),
            "action_status_effects": ([ "self": ({ }), "target": ({ }) ]),
            "numeric_effects": ([]) ]);

    entries = query_skill_breakthrough_entries(player, skill_id);
    return project_skill_breakthrough_action(entries, player, domain, target,
        stringp(action_id) ? action_id : "", active_slot, 1, 1);
}
int query_next_skill_breakthrough_level(object player, string skill_id)
{
    return SKILL_BREAKTHROUGH_D->query_next_level(player, skill_id);
}
mapping query_skill_breakthrough_state(object player, string skill_id)
{
    mapping breakthrough;

    if (!objectp(player) || !stringp(skill_id) || skill_id == "")
        return ([ "ok": 0, "error": "invalid_arguments" ]);

    breakthrough = SKILL_BREAKTHROUGH_D->query_breakthrough_state(player, skill_id);
    if (!mapp(breakthrough) || !breakthrough["ok"])
        return breakthrough;

    return ([
        "ok": 1,
        "skill_id": skill_id,
        "kind": breakthrough["kind"],
        "level": breakthrough["level"],
        "tier": breakthrough["tier"],
        "unlocked_tier": breakthrough["unlocked_tier"],
        "interval": breakthrough["interval"],
        "max_tier": breakthrough["max_tier"],
        "release_max_tier": breakthrough["release_max_tier"],
        "next_level": breakthrough["next_level"],
        "entries": breakthrough["entries"],
        "selected": breakthrough["selected"],
        "available": breakthrough["available"],
        "pending_count": breakthrough["pending_count"],
    ]);
}
varargs mapping query_skill_breakthrough_numeric_effect(object player, string skill_id, string domain, object target)
{
    mapping *entries;
    mapping effects;
    int passive_weight_bp;

    if (!objectp(player) || !stringp(skill_id) || skill_id == "" || !stringp(domain) || domain == "")
        return ([]);
    if (!skill_breakthrough_numeric_domain_supported(skill_id, domain))
        return ([]);

    entries = query_skill_breakthrough_entries(player, skill_id);
    passive_weight_bp = query_skill_passive_weight_bp(player, skill_id);
    if (passive_weight_bp <= 0)
        return ([]);
    effects = project_skill_breakthrough_action(entries, player, domain, target,
        "", "", 0, 1)["numeric_effects"];
    return scale_breakthrough_numeric_values(effects, passive_weight_bp);
}
varargs mapping query_skill_breakthrough_action_modifiers(object player, string skill_id,
    string domain, object target, string action_id)
{
    mapping *entries;

    if (!objectp(player) || !stringp(skill_id) || skill_id == "" || !stringp(domain) || domain == "")
        return ([]);

    entries = query_skill_breakthrough_entries(player, skill_id);
    return project_skill_breakthrough_action(entries, player, domain, target,
        stringp(action_id) ? action_id : "", "", 1, 0)["action_modifiers"];
}
