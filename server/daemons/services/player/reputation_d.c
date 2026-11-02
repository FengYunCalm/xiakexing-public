/**
 * @file    reputation_d.c
 * @brief   声望守护进程,fame/credit/alignment 声望状态真源。
 */

#pragma strict_types
#pragma save_types

#include <system/globals.h>
#include <system/services.h>
#include <system/event_names.h>

inherit "/lib/framework/daemon";

#define REPUTATION_KEY_FAME      "fame"
#define REPUTATION_KEY_CREDIT    "credit"
#define REPUTATION_KEY_ALIGNMENT "alignment"
#define REPUTATION_KEY_NPC_FAVOR "npc_favor"
#define NPC_FAVOR_MAX 100
#define NPC_FAVOR_ORDINARY_MAX 10

private mapping query_reputation_config();
private int query_config_int(mapping config, string section_name, string key, int default_value);

private int normalize_alignment_value(int value)
{
    mapping config;
    int min_value;
    int max_value;

    config = query_reputation_config();
    min_value = query_config_int(config, "reputation", "min_alignment", -1000);
    max_value = query_config_int(config, "reputation", "max_alignment", 1000);
    if (value < min_value)
        return min_value;
    if (value > max_value)
        return max_value;
    return value;
}

private mapping query_reputation_config()
{
    mixed config;

    config = CONFIG_D->query_full_config("reputation");
    return mapp(config) ? config : ([]);
}

private int query_config_int(mapping config, string section_name, string key, int default_value)
{
    mapping section;
    mixed value;
    int parsed;

    if (!mapp(config))
        return default_value;

    section = config[section_name];
    if (!mapp(section))
        return default_value;

    value = section[key];
    if (intp(value))
        return value;
    if (stringp(value) && sscanf(value, "%d", parsed) == 1)
        return parsed;
    return default_value;
}

private void emit_reputation_change(object player, string scope, int delta)
{
    EVENT_D->emit(EVENT_PLAYER_REPUTATION_CHANGED, ([
        "player_id": objectp(player) ? PLAYER_STATE_D->query_player_id(player) : "",
        "scope": scope,
        "delta": delta,
        "current": objectp(player) ? (player->query("reputation/" + scope) || 0) : 0,
        "timestamp": time()
    ]));
}

int query_reputation_scope(object player, string scope)
{
    if (!objectp(player) || !stringp(scope) || scope == "")
        return 0;
    return player->query("reputation/" + scope) || 0;
}

int set_reputation_scope(object player, string scope, int value)
{
    mapping change;
    mapping response;
    string path;

    if (!objectp(player) || !stringp(scope) || scope == "")
        return 0;

    if (scope == REPUTATION_KEY_ALIGNMENT)
        value = normalize_alignment_value(value);
    path = "reputation/" + scope;
    change = ([ path: value ]);
    response = PLAYER_STATE_D->commit_state_fields(player, change,
        PLAYER_STATE_D->build_state_idempotency_key(player,
            "reputation/set", change));
    if (!mapp(response) || !response["ok"])
        return 0;
    emit_reputation_change(player, scope, 0);
    return 1;
}

int add_reputation_scope(object player, string scope, int delta)
{
    mapping change;
    mapping response;
    string path;
    int current;
    int next_value;

    if (!objectp(player) || !stringp(scope) || scope == "" || delta == 0)
        return 0;

    path = "reputation/" + scope;
    current = player->query(path) || 0;
    next_value = current + delta;
    if (scope == REPUTATION_KEY_ALIGNMENT)
        next_value = normalize_alignment_value(next_value);
    change = ([ path: next_value ]);
    response = PLAYER_STATE_D->commit_state_fields(player, change,
        PLAYER_STATE_D->build_state_idempotency_key(player,
            "reputation/add", change));
    if (!mapp(response) || !response["ok"])
        return 0;
    emit_reputation_change(player, scope, next_value - current);
    return 1;
}

int query_fame(object player)
{
    return query_reputation_scope(player, REPUTATION_KEY_FAME);
}

int add_fame(object player, int delta)
{
    return add_reputation_scope(player, REPUTATION_KEY_FAME, delta);
}

int query_credit(object player)
{
    return query_reputation_scope(player, REPUTATION_KEY_CREDIT);
}

int add_credit(object player, int delta)
{
    return add_reputation_scope(player, REPUTATION_KEY_CREDIT, delta);
}

int query_alignment(object player)
{
    return query_reputation_scope(player, REPUTATION_KEY_ALIGNMENT);
}

int add_alignment(object player, int delta)
{
    int current;
    int next_value;

    if (!objectp(player) || delta == 0)
        return 0;

    current = query_alignment(player);
    next_value = normalize_alignment_value(current + delta);

    {
        mapping change;
        mapping response;

        change = ([ "reputation/" + REPUTATION_KEY_ALIGNMENT: next_value ]);
        response = PLAYER_STATE_D->commit_state_fields(player, change,
            PLAYER_STATE_D->build_state_idempotency_key(player,
                "reputation/add_alignment", change));
        if (!mapp(response) || !response["ok"])
            return 0;
    }
    emit_reputation_change(player, REPUTATION_KEY_ALIGNMENT,
        next_value - current);
    return 1;
}

mapping query_reputation_snapshot(object player)
{
    mapping raw;

    if (!objectp(player))
        return ([]);
    raw = player->query("reputation");
    return mapp(raw) ? restore_variable(save_variable(raw)) : ([]);
}

private string query_npc_identity(object npc)
{
    string npc_id;

    if (!objectp(npc))
        return "";
    if (function_exists("query_primary_id", npc))
        npc_id = npc->query_primary_id();
    else if (function_exists("query_quest_provider_id", npc))
        npc_id = npc->query_quest_provider_id();
    else
        npc_id = "";
    return stringp(npc_id) ? npc_id : "";
}

private mapping query_npc_favor_store(object player)
{
    mixed raw;

    if (!objectp(player))
        return ([]);
    raw = player->query("reputation/" + REPUTATION_KEY_NPC_FAVOR);
    return mapp(raw) ? restore_variable(save_variable(raw)) : ([]);
}

mapping query_npc_favor_snapshot(object player, string npc_id)
{
    mapping store;
    mapping entry;
    mixed legacy;

    if (!objectp(player) || !stringp(npc_id) || npc_id == "")
        return ([ "npc_id": npc_id || "", "total": 0, "ordinary": 0 ]);
    store = query_npc_favor_store(player);
    entry = store[npc_id];
    if (mapp(entry))
        return ([
            "npc_id": npc_id,
            "total": entry["total"] || 0,
            "ordinary": entry["ordinary"] || 0,
            "gift": entry["gift"] || 0,
            "updated_at": entry["updated_at"] || 0,
        ]);
    legacy = store[npc_id];
    return ([
        "npc_id": npc_id,
        "total": intp(legacy) ? legacy : 0,
        "ordinary": 0,
        "gift": 0,
    ]);
}

int query_npc_favor(object player, object npc)
{
    string npc_id;
    mapping snapshot;

    npc_id = query_npc_identity(npc);
    snapshot = query_npc_favor_snapshot(player, npc_id);
    return snapshot["total"] || 0;
}

mapping apply_npc_favor_delta(object player, string npc_id, int delta,
    int ordinary_delta, string operation_id)
{
    mapping change;
    mapping entry;
    mapping response;
    mapping snapshot;
    mapping store;
    int current_total;
    int current_ordinary;
    int applied_ordinary;
    int applied_total;

    if (!objectp(player) || !stringp(npc_id) || npc_id == "" || delta < 0)
        return ([ "ok": 0, "error": "invalid_arguments" ]);
    snapshot = query_npc_favor_snapshot(player, npc_id);
    current_total = snapshot["total"] || 0;
    current_ordinary = snapshot["ordinary"] || 0;
    applied_ordinary = ordinary_delta > 0
        ? min(ordinary_delta, NPC_FAVOR_ORDINARY_MAX - current_ordinary) : 0;
    applied_total = min(delta, NPC_FAVOR_MAX - current_total);
    if (applied_total < 0)
        applied_total = 0;
    if (applied_ordinary > applied_total)
        applied_ordinary = applied_total;
    if (applied_total == 0)
        return ([ "ok": 1, "npc_id": npc_id, "applied": 0,
            "total": current_total, "ordinary": current_ordinary ]);

    store = query_npc_favor_store(player);
    entry = ([
        "total": current_total + applied_total,
        "ordinary": current_ordinary + applied_ordinary,
        "gift": snapshot["gift"] || 0,
        "updated_at": time(),
    ]);
    store[npc_id] = entry;
    change = ([ "reputation/" + REPUTATION_KEY_NPC_FAVOR: store ]);
    response = PLAYER_STATE_D->commit_state_fields(player, change,
        PLAYER_STATE_D->build_state_idempotency_key(player,
            stringp(operation_id) && operation_id != ""
                ? operation_id : "reputation/npc_favor", change));
    if (!mapp(response) || !response["ok"])
        return ([ "ok": 0, "error": "favor_persist_failed", "npc_id": npc_id ]);
    return ([
        "ok": 1,
        "npc_id": npc_id,
        "applied": applied_total,
        "ordinary_applied": applied_ordinary,
        "total": entry["total"],
        "ordinary": entry["ordinary"],
    ]);
}

mapping apply_npc_favor_give(object player, object npc, object item, int count)
{
    string npc_id;
    int ordinary_delta;

    npc_id = query_npc_identity(npc);
    if (!objectp(item) || item->query("npc_gift") || item->query("favor_gift")
        || item->query("gift_type"))
        return ([ "ok": 1, "skipped": 1, "reason": "special_gift_design_pending" ]);
    ordinary_delta = count > 0 ? count : 1;
    return apply_npc_favor_delta(player, npc_id, ordinary_delta, ordinary_delta,
        "reputation/npc_favor_give");
}
