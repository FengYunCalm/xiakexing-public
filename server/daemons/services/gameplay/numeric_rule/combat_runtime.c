/**
 * @file    numeric_rule/combat_runtime.c
 * @brief   战斗清理/资源/攻击/生存预算
 *
 * @par Description:
 * 含 {query_combat_cleanup_budget_framework, query_combat_cleanup_int, query_combat_cleanup_reason_allowed, query_combat_cleanup_reason_score, query_combat_cleanup_budget_report, query_combat_resource_budget_framework, query_combat_resource_int, query_combat_resource_current, query_combat_resource_max, apply_combat_resource_cost, apply_combat_resource_recover, query_combat_resource_budget_report, apply_combat_resource_budget, query_combat_attack_budget_framework, query_combat_attack_budget_report, query_combat_survival_budget_framework, query_combat_survival_budget_report}。
 *
 * 本模块是 combat_runtime 域纯规则片段，由 `numeric_rule_d.c` 继承；
 * 不持有 daemon 状态（nosave 全部留在 facade），
 * 配置经 facade 的 query_section/query_section_int 读取。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>
#include <game/skill.h>
#include <game_constants.h>
#include "defs.h"

#undef is_player
int query_mapping_int(mapping source, string key, int default_value);
int query_numeric_budget_int(mapping defaults, mapping context, string key, int default_value);
mapping query_section(string section_name);
mapping query_combat_cleanup_budget_framework()
{
    mapping framework;

    framework = query_section("combat_cleanup_budget");
    return mapp(framework) ? copy(framework) : ([]);
}
private int query_combat_cleanup_int(mapping defaults, mapping context,
    string key, int default_value)
{
    if (mapp(context) && !undefinedp(context[key]) && intp(context[key]))
        return context[key];
    if (mapp(defaults) && !undefinedp(defaults[key]) && intp(defaults[key]))
        return defaults[key];
    return default_value;
}
private int query_combat_cleanup_reason_allowed(mapping defaults,
    mapping context, string reason)
{
    if (reason == "invalid_object")
        return query_combat_cleanup_int(defaults, context, "allow_invalid_object_cleanup", 1) ? 1 : 0;
    if (reason == "id_mismatch")
        return query_combat_cleanup_int(defaults, context, "allow_id_mismatch_cleanup", 1) ? 1 : 0;
    if (reason == "no_fight_state")
        return query_combat_cleanup_int(defaults, context, "allow_no_fight_state_cleanup", 1) ? 1 : 0;
    if (reason == "idle_timeout" || reason == "age_timeout")
        return 1;
    return 0;
}
private int query_combat_cleanup_reason_score(mapping weights, string reason)
{
    if (reason == "invalid_object")
        return query_mapping_int(weights, "invalid_object_weight", 500);
    if (reason == "id_mismatch")
        return query_mapping_int(weights, "id_mismatch_weight", 400);
    if (reason == "no_fight_state")
        return query_mapping_int(weights, "no_fight_state_weight", 300);
    return 0;
}
mapping query_combat_cleanup_budget_report(mapping context)
{
    mapping defaults;
    mapping framework;
    mapping weights;
    string reason;
    int active_combats;
    int age_seconds;
    int block_over_budget;
    int cleanup_allowed;
    int idle_seconds;
    int limit;
    int max_age;
    int max_idle;
    int score;
    int timeout_allowed;

    if (!mapp(context))
        context = ([]);

    framework = query_combat_cleanup_budget_framework();
    defaults = mapp(framework["defaults"]) ? framework["defaults"] : ([]);
    weights = mapp(framework["weights"]) ? framework["weights"] : ([]);
    reason = stringp(context["reason"]) && context["reason"] != "" ? context["reason"] : "unknown";
    age_seconds = query_combat_cleanup_int(defaults, context, "age_seconds", 0);
    idle_seconds = query_combat_cleanup_int(defaults, context, "idle_seconds", 0);
    active_combats = query_combat_cleanup_int(defaults, context, "active_combats", 0);
    max_idle = query_combat_cleanup_int(defaults, context, "max_idle_seconds", 0);
    max_age = query_combat_cleanup_int(defaults, context, "max_age_seconds", 0);
    limit = query_combat_cleanup_int(defaults, context, "budget_limit", 1200);
    block_over_budget = query_combat_cleanup_int(defaults, context, "block_over_budget", 0);

    if (age_seconds < 0)
        age_seconds = 0;
    if (idle_seconds < 0)
        idle_seconds = 0;
    if (active_combats < 0)
        active_combats = 0;

    timeout_allowed = (reason == "idle_timeout" && max_idle > 0 && idle_seconds >= max_idle)
        || (reason == "age_timeout" && max_age > 0 && age_seconds >= max_age);
    cleanup_allowed = query_combat_cleanup_reason_allowed(defaults, context, reason) || timeout_allowed;
    score = query_combat_cleanup_reason_score(weights, reason)
        + idle_seconds * query_mapping_int(weights, "idle_second_weight", 2)
        + age_seconds * query_mapping_int(weights, "age_second_weight", 1)
        + active_combats * query_mapping_int(weights, "active_combat_weight", 10);

    return ([
        "ok": 1,
        "valid": limit <= 0 || score <= limit,
        "reason": reason,
        "cleanup_allowed": cleanup_allowed ? 1 : 0,
        "age_seconds": age_seconds,
        "idle_seconds": idle_seconds,
        "active_combats": active_combats,
        "max_idle_seconds": max_idle,
        "max_age_seconds": max_age,
        "score": score,
        "limit": limit,
        "over_budget": limit > 0 && score > limit ? score - limit : 0,
        "budget_unit": defaults["budget_unit"] || "cleanup_risk_score",
        "block_apply": limit > 0 && score > limit && block_over_budget ? 1 : 0,
    ]);
}
mapping query_combat_resource_budget_framework()
{
    mapping framework;

    framework = query_section("combat_resource_budget");
    return mapp(framework) ? copy(framework) : ([]);
}
private int query_combat_resource_int(mapping defaults, mapping context,
    string key, int default_value)
{
    if (mapp(context) && !undefinedp(context[key]) && intp(context[key]))
        return context[key];
    if (mapp(defaults) && !undefinedp(defaults[key]) && intp(defaults[key]))
        return defaults[key];
    return default_value;
}
private int is_player_state_actor(object actor)
{
    return objectp(actor) && function_exists("is_player", actor)
        && actor->is_player();
}

private int commit_combat_resource(object actor, string path, int value,
    string operation)
{
    mapping change;
    mapping response;

    if (!objectp(actor))
        return 0;
    if (!is_player_state_actor(actor))
        return actor->set(path, value);
    change = ([ path: value ]);
    response = PLAYER_STATE_D->commit_state_fields(actor, change,
        PLAYER_STATE_D->build_state_idempotency_key(actor,
            operation || "combat/resource", change));
    return mapp(response) && response["ok"];
}

private int query_combat_resource_current(object actor, string resource)
{
    if (!objectp(actor))
        return 0;
    if (resource == "mp")
        return actor->query("combat/mp") || 0;
    if (resource == "ep")
        return actor->query("combat/ep") || 0;
    return 0;
}
private int query_combat_resource_max(object actor, string resource)
{
    mapping bonuses;
    int base;

    if (!objectp(actor))
        return 0;
    if (resource == "mp") {
        if (function_exists("query_max_mp", actor))
            base = actor->query_max_mp();
        else
            base = actor->query("combat/max_mp") || 0;
    } else if (resource == "ep") {
        if (function_exists("query_max_ep", actor))
            base = actor->query_max_ep();
        else
            base = actor->query("combat/max_ep") || 0;
    } else {
        return 0;
    }
    /* 经脉加成（玩家专属）：mp/hp 类资源上限叠加 */
    if (userp(actor)) {
        bonuses = MERIDIAN_D->query_meridian_bonuses(actor);
        if (resource == "mp" && intp(bonuses["mp"]))
            base += bonuses["mp"];
        if (resource == "ep" && intp(bonuses["max_ep"]))
            base += bonuses["max_ep"];
        if (resource == "hp" && intp(bonuses["hp"]))
            base += bonuses["hp"];
    }
    return base;
}
private void apply_combat_resource_cost(object actor, string resource, int amount)
{
    int current;

    if (!objectp(actor) || amount <= 0)
        return;

    current = query_combat_resource_current(actor, resource);
    if (resource == "mp") {
        if (function_exists("consume_mp", actor) && actor->consume_mp(amount))
            return;
        commit_combat_resource(actor, "combat/mp",
            current > amount ? current - amount : 0, "combat/cost_mp");
    } else if (resource == "ep") {
        if (function_exists("consume_ep", actor) && actor->consume_ep(amount))
            return;
        commit_combat_resource(actor, "combat/ep",
            current > amount ? current - amount : 0, "combat/cost_ep");
    }
}
private void apply_combat_resource_recover(object actor, string resource, int amount)
{
    int current;
    int max_value;
    int next_value;

    if (!objectp(actor) || amount <= 0)
        return;

    if (resource == "mp" && function_exists("heal_mp", actor)) {
        actor->heal_mp(amount);
        return;
    }
    if (resource == "ep" && function_exists("heal_ep", actor)) {
        actor->heal_ep(amount);
        return;
    }

    current = query_combat_resource_current(actor, resource);
    max_value = query_combat_resource_max(actor, resource);
    next_value = current + amount;
    if (max_value > 0 && next_value > max_value)
        next_value = max_value;
    if (resource == "mp")
        commit_combat_resource(actor, "combat/mp", next_value,
            "combat/recover_mp");
    else if (resource == "ep")
        commit_combat_resource(actor, "combat/ep", next_value,
            "combat/recover_ep");
}
mapping query_combat_resource_budget_report(object actor, mapping context)
{
    mapping defaults;
    mapping framework;
    mapping weights;
    string action_type;
    int block_insufficient;
    int block_over_budget;
    int ep_cost;
    int ep_recover;
    int limit;
    int mp_cost;
    int mp_recover;
    int score;

    if (!mapp(context))
        context = ([]);

    framework = query_combat_resource_budget_framework();
    defaults = mapp(framework["defaults"]) ? framework["defaults"] : ([]);
    weights = mapp(framework["weights"]) ? framework["weights"] : ([]);
    action_type = stringp(context["action_type"]) && context["action_type"] != "" ? context["action_type"] : "basic_attack";
    if (action_type == "basic_attack") {
        mp_cost = query_combat_resource_int(defaults, context, "basic_attack_mp_cost", 0);
        ep_cost = query_combat_resource_int(defaults, context, "basic_attack_ep_cost", 0);
    }
    if (action_type == "round_start") {
        mp_recover = query_combat_resource_int(defaults, context, "round_mp_recover", 0);
        ep_recover = query_combat_resource_int(defaults, context, "round_ep_recover", 0);
    }
    if (mp_cost < 0) mp_cost = 0;
    if (ep_cost < 0) ep_cost = 0;
    if (mp_recover < 0) mp_recover = 0;
    if (ep_recover < 0) ep_recover = 0;

    block_insufficient = query_combat_resource_int(defaults, context, "insufficient_resource_blocks_action", 0) ? 1 : 0;
    limit = query_combat_resource_int(defaults, context, "budget_limit", 1000);
    block_over_budget = query_combat_resource_int(defaults, context, "block_over_budget", 0);
    score = mp_cost * query_mapping_int(weights, "mp_cost_weight", 20)
        + ep_cost * query_mapping_int(weights, "ep_cost_weight", 10)
        + mp_recover * query_mapping_int(weights, "mp_recover_weight", 15)
        + ep_recover * query_mapping_int(weights, "ep_recover_weight", 8)
        + (block_insufficient ? query_mapping_int(weights, "block_weight", 200) : 0);

    return ([
        "ok": 1,
        "valid": limit <= 0 || score <= limit,
        "action_type": action_type,
        "mp_cost": mp_cost,
        "ep_cost": ep_cost,
        "mp_recover": mp_recover,
        "ep_recover": ep_recover,
        "mp_current": query_combat_resource_current(actor, "mp"),
        "ep_current": query_combat_resource_current(actor, "ep"),
        "insufficient_resource_blocks_action": block_insufficient,
        "affordable": query_combat_resource_current(actor, "mp") >= mp_cost
            && query_combat_resource_current(actor, "ep") >= ep_cost,
        "score": score,
        "limit": limit,
        "over_budget": limit > 0 && score > limit ? score - limit : 0,
        "budget_unit": defaults["budget_unit"] || "combat_resource_score",
        "block_apply": limit > 0 && score > limit && block_over_budget ? 1 : 0,
    ]);
}
mapping apply_combat_resource_budget(object actor, mapping context)
{
    mapping report;

    report = query_combat_resource_budget_report(actor, context);
    if (!objectp(actor) || !mapp(report))
        return report;

    if (report["block_apply"]
        || (!report["affordable"] && report["insufficient_resource_blocks_action"])) {
        report["action_blocked"] = 1;
        report["applied"] = 0;
        return report;
    }

    apply_combat_resource_cost(actor, "mp", report["mp_cost"] || 0);
    apply_combat_resource_cost(actor, "ep", report["ep_cost"] || 0);
    apply_combat_resource_recover(actor, "mp", report["mp_recover"] || 0);
    apply_combat_resource_recover(actor, "ep", report["ep_recover"] || 0);
    report["action_blocked"] = 0;
    report["applied"] = 1;
    return report;
}
mapping query_combat_attack_budget_framework()
{
    mapping framework;

    framework = query_section("combat_attack_budget");
    return mapp(framework) ? copy(framework) : ([]);
}
mapping query_combat_attack_budget_report(mapping context)
{
    mapping defaults;
    mapping framework;
    mapping weights;
    int block_over_budget;
    int critical_rate;
    int damage;
    int final_damage;
    int hit_rate;
    int limit;
    int parry_reduction_bp;
    int parry_rate;
    int score;

    if (!mapp(context))
        context = ([]);

    framework = query_combat_attack_budget_framework();
    defaults = mapp(framework["defaults"]) ? framework["defaults"] : ([]);
    weights = mapp(framework["weights"]) ? framework["weights"] : ([]);
    hit_rate = query_numeric_budget_int(defaults, context, "hit_rate", 0);
    damage = query_numeric_budget_int(defaults, context, "damage", 0);
    final_damage = query_numeric_budget_int(defaults, context, "final_damage", damage);
    critical_rate = query_numeric_budget_int(defaults, context, "critical_rate", 0);
    parry_rate = query_numeric_budget_int(defaults, context, "parry_rate", 0);
    parry_reduction_bp = query_numeric_budget_int(defaults, context, "parry_reduction_bp", 0);
    limit = query_numeric_budget_int(defaults, context, "budget_limit", 1800);
    block_over_budget = query_numeric_budget_int(defaults, context, "block_over_budget", 0);

    if (hit_rate < 0) hit_rate = 0;
    if (damage < 0) damage = 0;
    if (final_damage < 0) final_damage = 0;
    if (critical_rate < 0) critical_rate = 0;
    if (parry_rate < 0) parry_rate = 0;
    if (parry_reduction_bp < 0) parry_reduction_bp = 0;

    score = hit_rate * query_mapping_int(weights, "hit_rate_weight", 8)
        + critical_rate * query_mapping_int(weights, "critical_rate_weight", 12)
        + damage * query_mapping_int(weights, "damage_weight", 20)
        + final_damage * query_mapping_int(weights, "final_damage_weight", 25)
        + parry_reduction_bp * query_mapping_int(weights, "parry_reduction_weight", 4) / BP_BASE;

    return ([
        "ok": 1,
        "valid": limit <= 0 || score <= limit,
        "hit_rate": hit_rate,
        "damage": damage,
        "final_damage": final_damage,
        "critical_rate": critical_rate,
        "parry_rate": parry_rate,
        "parry_reduction_bp": parry_reduction_bp,
        "max_hit_rate": query_numeric_budget_int(defaults, context, "max_hit_rate", 95),
        "max_critical_rate": query_numeric_budget_int(defaults, context, "max_critical_rate", 50),
        "max_parry_rate": query_numeric_budget_int(defaults, context, "max_parry_rate", 70),
        "max_parry_reduction_bp": query_numeric_budget_int(defaults, context, "max_parry_reduction_bp", 8000),
        "score": score,
        "limit": limit,
        "over_budget": limit > 0 && score > limit ? score - limit : 0,
        "budget_unit": defaults["budget_unit"] || "attack_resolution_score",
        "block_apply": limit > 0 && score > limit && block_over_budget ? 1 : 0,
    ]);
}
mapping query_combat_survival_budget_framework()
{
    mapping framework;

    framework = query_section("combat_survival_budget");
    return mapp(framework) ? copy(framework) : ([]);
}
mapping query_combat_survival_budget_report(object actor, mapping context)
{
    mapping defaults;
    mapping framework;
    mapping weights;
    string event;
    int block_over_budget;
    int command_blocked;
    int duration;
    int exp_loss_bp;
    int kill_mode;
    int limit;
    int money_loss_bp;
    int notify_dungeon;
    int recover_hp_bp;
    int score;

    if (!mapp(context))
        context = ([]);

    framework = query_combat_survival_budget_framework();
    defaults = mapp(framework["defaults"]) ? framework["defaults"] : ([]);
    weights = mapp(framework["weights"]) ? framework["weights"] : ([]);
    event = stringp(context["event"]) && context["event"] != "" ? context["event"] : "player_down";
    kill_mode = query_numeric_budget_int(defaults, context, "kill_mode", 0) ? 1 : 0;
    duration = query_numeric_budget_int(defaults, context, "unconscious_duration_seconds", 20);
    recover_hp_bp = query_numeric_budget_int(defaults, context, "wake_recover_hp_bp", 1000);
    exp_loss_bp = query_numeric_budget_int(defaults, context, "death_exp_loss_bp", 0);
    money_loss_bp = query_numeric_budget_int(defaults, context, "death_money_loss_bp", 0);
    command_blocked = query_numeric_budget_int(defaults, context, "command_blocked_while_down", 1) ? 1 : 0;
    notify_dungeon = query_numeric_budget_int(defaults, context, "notify_dungeon_state", 1) ? 1 : 0;
    limit = query_numeric_budget_int(defaults, context, "budget_limit", 1000);
    block_over_budget = query_numeric_budget_int(defaults, context, "block_over_budget", 0);

    if (duration < 0) duration = 0;
    if (recover_hp_bp < 0) recover_hp_bp = 0;
    if (exp_loss_bp < 0) exp_loss_bp = 0;
    if (money_loss_bp < 0) money_loss_bp = 0;

    score = duration * query_mapping_int(weights, "duration_second_weight", 10)
        + recover_hp_bp * query_mapping_int(weights, "recover_bp_weight", 1) / 100
        + (exp_loss_bp + money_loss_bp) * query_mapping_int(weights, "death_loss_bp_weight", 2) / 100
        + (command_blocked ? query_mapping_int(weights, "command_block_weight", 100) : 0)
        + (kill_mode ? query_mapping_int(weights, "kill_mode_weight", 200) : 0);

    return ([
        "ok": 1,
        "valid": limit <= 0 || score <= limit,
        "event": event,
        "actor_id": objectp(actor) ? get_object_id(actor) : "",
        "kill_mode": kill_mode,
        "unconscious_duration_seconds": duration,
        "wake_recover_hp_bp": recover_hp_bp,
        "death_exp_loss_bp": exp_loss_bp,
        "death_money_loss_bp": money_loss_bp,
        "command_blocked_while_down": command_blocked,
        "notify_dungeon_state": notify_dungeon,
        "score": score,
        "limit": limit,
        "over_budget": limit > 0 && score > limit ? score - limit : 0,
        "budget_unit": defaults["budget_unit"] || "survival_penalty_score",
        "block_apply": limit > 0 && score > limit && block_over_budget ? 1 : 0,
    ]);
}
