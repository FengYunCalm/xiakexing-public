/**
 * @file    numeric_rule/combat_pvp.c
 * @brief   PVP 预算与伤害保护
 *
 * @par Description:
 * 含 {query_combat_pvp_budget_framework, query_actor_is_player_object, query_combat_pvp_budget_report, query_combat_pvp_damage_protection_framework, query_combat_pvp_damage_protection_profile, query_combat_pvp_damage_store, query_combat_pvp_window_total, apply_combat_pvp_overflow_protection, apply_combat_pvp_damage_protection, prune_combat_pvp_damage_entries}。
 *
 * 本模块是 combat_pvp 域纯规则片段，由 `numeric_rule_d.c` 继承；
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
string query_numeric_context(object actor, object target);
mapping query_section(string section_name);
mapping apply_combat_pvp_damage_protection(object attacker, object target, int raw_damage, mapping context);
mapping query_combat_pvp_damage_protection_framework();
mapping query_combat_pvp_budget_framework()
{
    mapping framework;

    framework = query_section("combat_pvp_budget");
    return mapp(framework) ? copy(framework) : ([]);
}
private int query_actor_is_player_object(object actor)
{
    if (!objectp(actor))
        return 0;
    if (function_exists("is_player", actor) && call_other(actor, "is_player"))
        return 1;
    return interactive(actor) ? 1 : 0;
}
mapping query_combat_pvp_budget_report(object attacker, object target, mapping context)
{
    mapping defaults;
    mapping framework;
    mapping weights;
    int allow_player_attack;
    int allow_safe_room_attack;
    int block_over_budget;
    int kill_mode;
    int limit;
    int safe_room;
    int score;
    int target_is_player;
    int target_weaker;

    if (!mapp(context))
        context = ([]);

    framework = query_combat_pvp_budget_framework();
    defaults = mapp(framework["defaults"]) ? framework["defaults"] : ([]);
    weights = mapp(framework["weights"]) ? framework["weights"] : ([]);
    target_is_player = query_numeric_budget_int(defaults, context, "target_is_player",
        query_actor_is_player_object(target)) ? 1 : 0;
    kill_mode = query_numeric_budget_int(defaults, context, "kill_mode", 1) ? 1 : 0;
    safe_room = query_numeric_budget_int(defaults, context, "safe_room", 0) ? 1 : 0;
    target_weaker = query_numeric_budget_int(defaults, context, "target_weaker", 0) ? 1 : 0;
    allow_player_attack = query_numeric_budget_int(defaults, context, "allow_player_attack", 1) ? 1 : 0;
    allow_safe_room_attack = query_numeric_budget_int(defaults, context, "allow_safe_room_attack", 0) ? 1 : 0;
    limit = query_numeric_budget_int(defaults, context, "pk_risk_limit", 1200);
    block_over_budget = query_numeric_budget_int(defaults, context, "block_over_budget", 0);

    score = (target_is_player ? query_mapping_int(weights, "player_target_weight", 500) : 0)
        + (kill_mode ? query_mapping_int(weights, "kill_mode_weight", 400) : 0)
        + (safe_room ? query_mapping_int(weights, "safe_room_weight", 800) : 0)
        + (target_weaker ? query_mapping_int(weights, "weaker_target_weight", 300) : 0);

    return ([
        "ok": 1,
        "valid": limit <= 0 || score <= limit,
        "attacker_id": objectp(attacker) ? get_object_id(attacker) : "",
        "target_id": objectp(target) ? get_object_id(target) : "",
        "target_is_player": target_is_player,
        "kill_mode": kill_mode,
        "safe_room": safe_room,
        "target_weaker": target_weaker,
        "allow_player_attack": allow_player_attack,
        "allow_safe_room_attack": allow_safe_room_attack,
        "block_attack": (target_is_player && !allow_player_attack)
            || (safe_room && !allow_safe_room_attack)
            || (limit > 0 && score > limit && block_over_budget) ? 1 : 0,
        "score": score,
        "limit": limit,
        "over_budget": limit > 0 && score > limit ? score - limit : 0,
        "budget_unit": defaults["budget_unit"] || "pvp_risk_score",
    ]);
}
mapping query_combat_pvp_damage_protection_framework()
{
    mapping framework;

    framework = query_section("combat_pvp_damage_protection");
    return mapp(framework) ? copy(framework) : ([]);
}
private mapping query_combat_pvp_damage_protection_profile(string damage_source)
{
    mapping defaults;
    mapping framework;
    mapping profile;
    mapping profiles;
    mapping result;
    mixed value;
    string key;

    framework = query_combat_pvp_damage_protection_framework();
    defaults = mapp(framework["defaults"]) ? copy(framework["defaults"]) : ([]);
    profiles = mapp(framework["source_profiles"]) ? framework["source_profiles"] : ([]);
    if (!stringp(damage_source) || damage_source == "")
        damage_source = "basic_attack";

    profile = profiles[damage_source];
    result = defaults;
    foreach (key, value in profile || ([ ])) {
        if (stringp(key))
            result[key] = value;
    }
    return result;
}
private mapping query_combat_pvp_damage_store(object target)
{
    mapping store;

    if (!objectp(target))
        return ([]);
    store = target->query_temp("combat/pvp_damage_window");
    return mapp(store) ? store : ([]);
}
private mixed *prune_combat_pvp_damage_entries(mixed *entries, int now, int window_seconds)
{
    mixed *result;
    mixed entry;
    int at;

    result = ({ });
    foreach (entry in entries || ({ })) {
        if (!mapp(entry))
            continue;
        at = intp(entry["at"]) ? entry["at"] : 0;
        if (at <= 0)
            continue;
        if (window_seconds > 0 && now - at > window_seconds)
            continue;
        result += ({ entry });
    }
    return result;
}
private int query_combat_pvp_window_total(mixed *entries)
{
    mapping entry;
    int total;

    total = 0;
    foreach (entry in entries || ({ })) {
        if (!mapp(entry))
            continue;
        if (intp(entry["window_damage"]) && entry["window_damage"] > 0)
            total += entry["window_damage"];
    }
    return total;
}
private int apply_combat_pvp_overflow_protection(int value, int soft_limit,
    int overflow_reduce_bp)
{
    int keep_bp;

    if (value <= 0)
        return 0;
    if (soft_limit <= 0 || value <= soft_limit)
        return value;
    if (overflow_reduce_bp <= 0)
        return value;
    if (overflow_reduce_bp >= BP_BASE)
        return soft_limit;

    keep_bp = BP_BASE - overflow_reduce_bp;
    return soft_limit + (value - soft_limit) * keep_bp / BP_BASE;
}
mapping apply_combat_pvp_damage_protection(object attacker, object target,
    int raw_damage, mapping context)
{
    mapping profile;
    mapping result;
    mapping state;
    mapping store;
    mixed *entries;
    string damage_source;
    int applied_damage;
    int enabled;
    int incoming_window_damage;
    int max_hp;
    int now;
    int post_single_hit_damage;
    int prevented_damage;
    int prior_window_damage;
    int single_hit_overflow_reduce_bp;
    int single_hit_soft_damage;
    int window_overflow_reduce_bp;
    int window_raw_total_damage;
    int window_soft_damage;
    int window_damage;
    int window_seconds;
    int window_total_damage;
    int window_weight_bp;
    string damage_context;

    if (!mapp(context))
        context = ([]);
    if (raw_damage < 0)
        raw_damage = 0;

    damage_source = stringp(context["damage_source"]) && context["damage_source"] != ""
        ? context["damage_source"]
        : "basic_attack";
    profile = query_combat_pvp_damage_protection_profile(damage_source);
    enabled = query_mapping_int(profile, "enabled", 1) ? 1 : 0;
    result = ([
        "ok": 1,
        "enabled": enabled,
        "damage_source": damage_source,
        "raw_damage": raw_damage,
        "applied_damage": raw_damage,
        "prevented_damage": 0,
        "protection_triggered": 0,
        "protection_reason": "",
        "single_hit_soft_damage": 0,
        "single_hit_overflow_reduce_bp": 0,
        "window_soft_damage": 0,
        "window_overflow_reduce_bp": 0,
        "prior_window_damage": 0,
        "window_raw_total_damage": 0,
        "window_total_damage": 0,
        "window_remaining_damage": 0,
        "window_contribution_damage": 0,
        "window_seconds": 0,
    ]);

    damage_context = stringp(context["damage_context"]) ? context["damage_context"] : "";
    if (damage_context == "" && context["source_is_player"] && objectp(target) && userp(target))
        damage_context = "pvp";
    if (damage_context == "")
        damage_context = query_numeric_context(attacker, target);

    if (!enabled || raw_damage <= 0 || damage_context != "pvp")
        return result;

    max_hp = objectp(target) ? (target->query("combat/max_hp") || 0) : 0;
    if (max_hp <= 0)
        return result;

    single_hit_soft_damage = max_hp * query_mapping_int(profile, "single_hit_soft_bp", 4000) / BP_BASE;
    window_soft_damage = max_hp * query_mapping_int(profile, "window_soft_bp", 8000) / BP_BASE;
    single_hit_overflow_reduce_bp = query_mapping_int(profile,
        "single_hit_overflow_reduce_bp", 8000);
    window_overflow_reduce_bp = query_mapping_int(profile,
        "window_overflow_reduce_bp", 9000);
    window_seconds = query_mapping_int(profile, "window_seconds", 3);
    window_weight_bp = query_mapping_int(profile, "window_weight_bp", BP_BASE);
    if (single_hit_soft_damage < 1)
        single_hit_soft_damage = 1;
    if (window_soft_damage < 1)
        window_soft_damage = 1;
    if (single_hit_overflow_reduce_bp < 0)
        single_hit_overflow_reduce_bp = 0;
    if (single_hit_overflow_reduce_bp > BP_BASE)
        single_hit_overflow_reduce_bp = BP_BASE;
    if (window_overflow_reduce_bp < 0)
        window_overflow_reduce_bp = 0;
    if (window_overflow_reduce_bp > BP_BASE)
        window_overflow_reduce_bp = BP_BASE;
    if (window_weight_bp < 1)
        window_weight_bp = BP_BASE;

    now = time();
    store = query_combat_pvp_damage_store(target);
    state = mapp(store["target"]) ? store["target"] : ([ ]);
    entries = prune_combat_pvp_damage_entries(state["entries"], now, window_seconds);
    prior_window_damage = query_combat_pvp_window_total(entries);
    if (prior_window_damage < 0)
        prior_window_damage = 0;

    post_single_hit_damage = apply_combat_pvp_overflow_protection(raw_damage,
        single_hit_soft_damage, single_hit_overflow_reduce_bp);
    incoming_window_damage = post_single_hit_damage * window_weight_bp / BP_BASE;
    if (post_single_hit_damage > 0 && incoming_window_damage < 1)
        incoming_window_damage = 1;

    window_raw_total_damage = prior_window_damage + incoming_window_damage;
    window_total_damage = apply_combat_pvp_overflow_protection(window_raw_total_damage,
        window_soft_damage, window_overflow_reduce_bp);
    window_damage = window_total_damage - prior_window_damage;
    if (window_damage < 0)
        window_damage = 0;

    applied_damage = window_damage * BP_BASE / window_weight_bp;
    if (window_damage > 0 && applied_damage < 1)
        applied_damage = 1;
    if (applied_damage > post_single_hit_damage)
        applied_damage = post_single_hit_damage;
    if (applied_damage < 0)
        applied_damage = 0;

    prevented_damage = raw_damage - applied_damage;
    if (prevented_damage < 0)
        prevented_damage = 0;

    if (window_damage > 0) {
        entries += ({ ([
            "at": now,
            "applied_damage": applied_damage,
            "window_damage": window_damage,
            "source": damage_source,
        ]) });
        store["target"] = ([ "entries": entries ]);
        target->set_temp("combat/pvp_damage_window", store);
    } else if (sizeof(entries) > 0) {
        store["target"] = ([ "entries": entries ]);
        target->set_temp("combat/pvp_damage_window", store);
    } else if (mapp(store["target"])) {
        map_delete(store, "target");
        target->set_temp("combat/pvp_damage_window", store);
    }

    result["applied_damage"] = applied_damage;
    result["prevented_damage"] = prevented_damage;
    result["protection_triggered"] = prevented_damage > 0 ? 1 : 0;
    result["single_hit_soft_damage"] = single_hit_soft_damage;
    result["single_hit_overflow_reduce_bp"] = single_hit_overflow_reduce_bp;
    result["window_soft_damage"] = window_soft_damage;
    result["window_overflow_reduce_bp"] = window_overflow_reduce_bp;
    result["prior_window_damage"] = prior_window_damage;
    result["window_raw_total_damage"] = window_raw_total_damage;
    result["window_total_damage"] = window_total_damage;
    result["window_remaining_damage"] = window_soft_damage - window_total_damage;
    if (result["window_remaining_damage"] < 0)
        result["window_remaining_damage"] = 0;
    result["window_contribution_damage"] = window_damage;
    result["window_seconds"] = window_seconds;

    if (raw_damage > post_single_hit_damage && post_single_hit_damage > applied_damage) {
        result["protection_reason"] = "single_hit_and_window_decay";
    } else if (raw_damage > post_single_hit_damage) {
        result["protection_reason"] = "single_hit_decay";
    } else if (post_single_hit_damage > applied_damage) {
        result["protection_reason"] = "window_decay";
    }

    return result;
}
