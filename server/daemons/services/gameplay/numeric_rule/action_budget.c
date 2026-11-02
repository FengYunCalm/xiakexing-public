/**
 * @file    numeric_rule/action_budget.c
 * @brief   动作预算系统
 *
 * @par Description:
 * 含 {query_action_budget_type_config, normalize_action_budget_resource, query_action_budget_runtime_cooldowns, normalize_action_budget_resource_costs, merge_action_budget_resource_costs, merge_action_budget_resource_costs_from_base, query_action_budget_int, query_action_budget_state_key, query_action_budget_store, query_action_budget_runtime_is_idle, query_action_budget_record, query_action_budget_resource, spend_action_budget_resource, scale_action_budget_costs, recover_action_budget_fatigue, build_action_budget_state, query_action_budget_state_from_spec, query_action_budget_state, commit_action_budget_event, clear_action_budget_state}。
 *
 * 本模块是 action_budget 域纯规则片段，由 `numeric_rule_d.c` 继承；
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
mapping query_action_budget_framework();
mapping query_action_budget_spec(string action_type, string action_id, mapping overrides);
mapping query_action_budget_type_config(string action_type)
{
    mapping framework;
    mapping type_config;
    mapping types;

    if (!stringp(action_type) || action_type == "")
        return ([]);

    framework = query_action_budget_framework();
    types = mapp(framework["types"]) ? framework["types"] : ([]);
    type_config = types[action_type];
    return mapp(type_config) ? copy(type_config) : ([]);
}
private string normalize_action_budget_resource(string resource)
{
    if (!stringp(resource) || resource == "")
        return "";
    switch (resource) {
    case "qi":
        return "hp";
    case "jingli":
        return "ep";
    default:
        return resource;
    }
}
private mapping normalize_action_budget_resource_costs(mapping costs)
{
    mapping normalized;
    string normalized_resource;
    string resource;
    int amount;

    normalized = ([]);
    foreach (resource, amount in costs || ([])) {
        if (!stringp(resource) || !intp(amount) || amount <= 0)
            continue;
        normalized_resource = normalize_action_budget_resource(resource);
        if (normalized_resource == "")
            continue;
        normalized[normalized_resource] = (normalized[normalized_resource] || 0) + amount;
    }
    return normalized;
}
 mapping merge_action_budget_resource_costs(mapping defaults, mapping type_config, mapping overrides)
{
    mapping merged;
    string resource;
    int amount;

    merged = ([]);
    foreach (resource, amount in normalize_action_budget_resource_costs(defaults["resource_costs"]))
        merged[resource] = (merged[resource] || 0) + amount;
    foreach (resource, amount in normalize_action_budget_resource_costs(type_config["resource_costs"]))
        merged[resource] = (merged[resource] || 0) + amount;
    foreach (resource, amount in normalize_action_budget_resource_costs(overrides["resource_costs"]))
        merged[resource] = (merged[resource] || 0) + amount;
    return merged;
}
 mapping merge_action_budget_resource_costs_from_base(mapping base,
    mapping overrides)
{
    mapping merged;
    string resource;
    int amount;

    merged = mapp(base) ? copy(base) : ([]);
    foreach (resource, amount in normalize_action_budget_resource_costs(
        mapp(overrides) ? overrides["resource_costs"] : 0))
        merged[resource] = (merged[resource] || 0) + amount;
    return merged;
}
 int query_action_budget_int(mapping type_config, mapping defaults, mapping overrides,
    string key, int default_value)
{
    if (mapp(overrides) && !undefinedp(overrides[key]) && intp(overrides[key]))
        return overrides[key];
    if (mapp(type_config) && !undefinedp(type_config[key]) && intp(type_config[key]))
        return type_config[key];
    if (mapp(defaults) && !undefinedp(defaults[key]) && intp(defaults[key]))
        return defaults[key];
    return default_value;
}
private string query_action_budget_state_key(string action_type, string action_id)
{
    if (!stringp(action_type) || action_type == "")
        action_type = "unknown";
    if (!stringp(action_id) || action_id == "")
        action_id = "*";
    return action_type + "|" + action_id;
}
private mapping query_action_budget_store(object player)
{
    mapping store;

    if (!objectp(player))
        return ([]);
    store = player->query_temp("numeric/action_budget");
    return mapp(store) ? store : ([]);
}
int query_action_budget_runtime_is_idle(object player)
{
    mapping store;

    if (!objectp(player))
        return 0;
    store = player->query_temp("numeric/action_budget");
    return !mapp(store) || sizeof(store) == 0;
}
mapping query_action_budget_record(object player, string action_type, string action_id)
{
    mapping record;
    mapping store;

    if (!objectp(player))
        return ([]);

    store = query_action_budget_store(player);
    record = store[query_action_budget_state_key(action_type, action_id)];
    return mapp(record) ? copy(record) : ([]);
}
private int query_action_budget_resource(object player, string resource)
{
    resource = normalize_action_budget_resource(resource);
    switch (resource) {
    case "hp":
        return function_exists("query_hp", player) ? player->query_hp() : (player->query("combat/hp") || 0);
    case "mp":
        return function_exists("query_mp", player) ? player->query_mp() : (player->query("combat/mp") || 0);
    case "ep":
        return function_exists("query_ep", player) ? player->query_ep() : (player->query("combat/ep") || 0);
    case "neili":
        return player->query("neili") || 0;
    default:
        return 0;
    }
}
private void spend_action_budget_resource(object player, string resource, int amount)
{
    resource = normalize_action_budget_resource(resource);
    switch (resource) {
    case "hp":
        if (function_exists("set_hp", player))
            player->set_hp(query_action_budget_resource(player, resource) - amount);
        else
            player->set("combat/hp", query_action_budget_resource(player, resource) - amount);
        return;
    case "mp":
        if (function_exists("consume_mp", player))
            player->consume_mp(amount);
        else
            player->set("combat/mp", query_action_budget_resource(player, resource) - amount);
        return;
    case "ep":
        if (function_exists("consume_ep", player))
            player->consume_ep(amount);
        else
            player->set("combat/ep", query_action_budget_resource(player, resource) - amount);
        return;
    case "neili":
        player->set("neili", query_action_budget_resource(player, resource) - amount);
        return;
    }
}
private int action_budget_player_target(object player)
{
    return objectp(player) && function_exists("is_player", player)
        && player->is_player();
}

private int commit_action_budget_resources(object player, mapping costs)
{
    mapping changes;
    mapping response;
    string resource;
    string path;
    int amount;
    int current;

    if (!objectp(player) || !mapp(costs))
        return 0;
    if (!action_budget_player_target(player)) {
        foreach (resource, amount in costs || ([]))
            spend_action_budget_resource(player, resource, amount);
        return 1;
    }

    changes = ([]);
    foreach (resource, amount in costs || ([])) {
        resource = normalize_action_budget_resource(resource);
        current = query_action_budget_resource(player, resource);
        switch (resource) {
        case "hp":
            path = "combat/hp";
            break;
        case "mp":
            path = "combat/mp";
            break;
        case "ep":
            path = "combat/ep";
            break;
        case "neili":
            path = "neili";
            break;
        default:
            continue;
        }
        changes[path] = current - amount;
    }
    if (sizeof(changes) == 0)
        return 1;
    response = PLAYER_STATE_D->commit_state_fields(player, changes,
        PLAYER_STATE_D->build_state_idempotency_key(player,
            "numeric/action_budget_resources", changes));
    if (!mapp(response) || !response["ok"])
        return 0;
    if (function_exists("schedule_owner_hp_read_model_refresh", player))
        player->schedule_owner_hp_read_model_refresh();
    if (function_exists("invalidate_xk_score_projection", player))
        player->invalidate_xk_score_projection();
    return 1;
}

private mapping scale_action_budget_costs(mapping costs, int factor_bp)
{
    mapping scaled;
    string resource;
    int amount;
    int value;

    scaled = ([]);
    if (factor_bp < BP_BASE)
        factor_bp = BP_BASE;
    foreach (resource, amount in costs || ([])) {
        value = amount * factor_bp / BP_BASE;
        if (amount > 0 && value < 1)
            value = 1;
        if (value > 0)
            scaled[resource] = value;
    }
    return scaled;
}
private int recover_action_budget_fatigue(mapping record, mapping spec, int now)
{
    int elapsed;
    int fatigue;
    int last_at;
    int recovery;
    int window;

    if (!mapp(record))
        return 0;
    fatigue = record["fatigue_bp"] || 0;
    last_at = record["last_at"] || 0;
    if (fatigue <= 0 || last_at <= 0)
        return 0;
    elapsed = now - last_at;
    if (elapsed <= 0)
        return fatigue;
    window = spec["fatigue_window_seconds"] || 300;
    if (window > 0 && elapsed > window)
        return 0;
    recovery = elapsed * (spec["fatigue_recovery_bp_per_second"] || 0);
    fatigue -= recovery;
    return fatigue > 0 ? fatigue : 0;
}
private mapping build_action_budget_state(object player, mapping spec)
{
    mapping costs;
    mapping record;
    mapping result;
    string resource;
    int amount;
    int cooldown_remaining;
    int fatigue_bp;
    int fatigue_factor_bp;
    int interval_remaining;
    int now;

    if (!objectp(player))
        return ([ "ok": 0, "error": "invalid_player" ]);
    if (!mapp(spec) || !spec["ok"])
        return spec;

    now = time();
    record = query_action_budget_record(player,
        spec["action_type"], spec["action_id"]);
    fatigue_bp = recover_action_budget_fatigue(record, spec, now);
    fatigue_factor_bp = BP_BASE + fatigue_bp;
    costs = scale_action_budget_costs(spec["resource_costs"], fatigue_factor_bp);
    cooldown_remaining = (record["cooldown_until"] || 0) - now;
    if (cooldown_remaining < 0)
        cooldown_remaining = 0;
    interval_remaining = (record["last_at"] || 0) + (spec["min_interval_seconds"] || 0) - now;
    if (interval_remaining < 0)
        interval_remaining = 0;

    result = spec + ([
        "ok": 1,
        "now": now,
        "fatigue_bp": fatigue_bp,
        "fatigue_factor_bp": fatigue_factor_bp,
        "resource_costs": costs,
        "base_resource_costs": spec["resource_costs"],
        "cooldown_remaining": cooldown_remaining,
        "interval_remaining": interval_remaining,
    ]);
    if (!spec["enabled"])
        return result;
    if (cooldown_remaining > 0) {
        result["ok"] = 0;
        result["error"] = "action_cooldown";
        return result;
    }
    if (interval_remaining > 0) {
        result["ok"] = 0;
        result["error"] = "action_interval";
        return result;
    }

    foreach (resource, amount in costs || ([])) {
        if ((resource == "hp" && query_action_budget_resource(player, resource) <= amount)
            || query_action_budget_resource(player, resource) < amount) {
            result["ok"] = 0;
            result["error"] = "insufficient_resource";
            result["resource"] = resource;
            result["required"] = amount;
            result["current"] = query_action_budget_resource(player, resource);
            return result;
        }
    }
    return result;
}
mapping query_action_budget_state_from_spec(object player, mapping spec)
{
    if (!mapp(spec) || !spec["ok"]
        || !stringp(spec["action_type"]) || spec["action_type"] == ""
        || !stringp(spec["action_id"]) || spec["action_id"] == "")
        return ([ "ok": 0, "error": "invalid_action_budget_spec" ]);
    return build_action_budget_state(player, spec);
}
mapping query_action_budget_state(object player, string action_type, string action_id, mapping overrides)
{
    mapping spec;

    spec = query_action_budget_spec(action_type, action_id, overrides || ([]));
    return build_action_budget_state(player, spec);
}
mapping commit_action_budget_event(object player, mapping budget)
{
    mapping costs;
    mapping store;
    string resource;
    string state_key;
    int amount;
    int fatigue_bp;
    int max_fatigue;
    int now;

    if (!objectp(player) || !mapp(budget) || !budget["ok"])
        return ([ "ok": 0, "error": "invalid_budget" ]);

    now = budget["now"] || time();
    costs = budget["resource_costs"];
    if (!commit_action_budget_resources(player, costs))
        return ([ "ok": 0, "error": "resource_commit_failed" ]);

    fatigue_bp = (budget["fatigue_bp"] || 0) + (budget["fatigue_per_use_bp"] || 0);
    max_fatigue = budget["max_fatigue_bp"] || 0;
    if (max_fatigue > 0 && fatigue_bp > max_fatigue)
        fatigue_bp = max_fatigue;

    store = query_action_budget_store(player);
    state_key = query_action_budget_state_key(budget["action_type"], budget["action_id"]);
    store[state_key] = ([
        "last_at": now,
        "cooldown_until": now + (budget["cooldown"] || 0),
        "cooldown": budget["cooldown"] || 0,
        "min_interval_seconds": budget["min_interval_seconds"] || 0,
        "fatigue_bp": fatigue_bp,
        "fatigue_factor_bp": BP_BASE + fatigue_bp,
        "resource_costs": costs,
    ]);
    if (stringp(budget["skill_id"]) && budget["skill_id"] != "")
        store[state_key]["skill_id"] = budget["skill_id"];
    if (stringp(budget["action_name"]) && budget["action_name"] != "")
        store[state_key]["action_name"] = budget["action_name"];
    player->set_temp("numeric/action_budget", store);
    return ([ "ok": 1, "record": copy(store[state_key]) ]);
}
mixed *query_action_budget_runtime_cooldowns(object player)
{
    mapping record;
    mapping spec;
    mapping store;
    mixed *cooldowns;
    string action_id;
    string action_type;
    string key;
    string *parts;
    int cooldown_remaining;
    int interval_total;
    int interval_remaining;
    int now;
    int remaining;
    int total;

    if (!objectp(player))
        return ({ });

    now = time();
    cooldowns = ({ });
    store = query_action_budget_store(player);
    foreach (key, record in store || ([ ])) {
        if (!stringp(key) || !mapp(record))
            continue;
        parts = explode(key, "|");
        action_type = sizeof(parts) > 0 ? parts[0] : "";
        action_id = sizeof(parts) > 1 ? parts[1] : "";
        if (action_type == "" || action_id == "")
            continue;

        spec = query_action_budget_spec(action_type, action_id, ([]));
        cooldown_remaining = (record["cooldown_until"] || 0) - now;
        interval_total = record["min_interval_seconds"] ||
            (mapp(spec) ? (spec["min_interval_seconds"] || 0) : 0);
        interval_remaining = (record["last_at"] || 0) + interval_total - now;
        if (cooldown_remaining < 0)
            cooldown_remaining = 0;
        if (interval_remaining < 0)
            interval_remaining = 0;
        remaining = cooldown_remaining > interval_remaining ? cooldown_remaining : interval_remaining;
        if (remaining <= 0)
            continue;

        total = record["cooldown"] || (mapp(spec) ? (spec["cooldown"] || 0) : 0);
        if (interval_total > total)
            total = interval_total;
        if (total < remaining)
            total = remaining;
        cooldowns += ({ ([
            "action_type": action_type,
            "action_id": action_id == "*" ? "" : action_id,
            "skill_id": stringp(record["skill_id"]) ? record["skill_id"] : "",
            "action_name": stringp(record["action_name"]) ? record["action_name"] : "",
            "remaining_seconds": remaining,
            "total_seconds": total,
            "cooldown_remaining": cooldown_remaining,
            "interval_remaining": interval_remaining,
        ]) });
    }
    return cooldowns;
}
int clear_action_budget_state(object player, string action_type, string action_id)
{
    mapping store;
    string state_key;

    if (!objectp(player))
        return 0;

    store = query_action_budget_store(player);
    state_key = query_action_budget_state_key(action_type, action_id);
    if (mapp(store))
        map_delete(store, state_key);
    player->set_temp("numeric/action_budget", store);
    return 1;
}
