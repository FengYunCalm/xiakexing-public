/**
 * @file    read_summary_d.c
 * @brief   高频读侧摘要异步计算服务
 *
 * 本服务收口 quest / skills / map 等命令的只读快照摘要。worker 只计算
 * digest；命令仍从各业务真源读取展示内容，不把业务状态写入 worker。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

#define SUMMARY_REPEAT 16
#define LOADTEST_CONFIG_CACHE_TTL 2
#define SUMMARY_CACHE_TTL 30
#define SUMMARY_CACHE_LIMIT 512
#define SUMMARY_PRUNE_BUDGET 16

nosave mapping _last_summaries;
nosave mapping _summary_cached_at;
nosave mapping _loadtest_config_cache;
nosave int _loadtest_config_cached_at;

void create()
{
    ::create();
    _last_summaries = ([]);
    _summary_cached_at = ([]);
    _loadtest_config_cache = 0;
    _loadtest_config_cached_at = 0;
}

private string query_player_key(object player)
{
    string player_id;

    if (!objectp(player))
        return "player/unknown";
    if (function_exists("query_account_id", player)) {
        player_id = player->query_account_id() || "";
        if (player_id != "")
            return "player/" + player_id;
    }
    player_id = get_object_id(player);
    if (!stringp(player_id) || player_id == "")
        player_id = geteuid(player) || file_name(player);
    return "player/" + (stringp(player_id) && player_id != "" ? player_id : "unknown");
}

private string query_player_account_key(object player)
{
    mixed account;
    string account_id;

    if (!objectp(player))
        return "";
    if (function_exists("query_account_id", player)) {
        account_id = player->query_account_id() || "";
        if (account_id != "")
            return account_id;
    }
    account = player->query("account");
    if (stringp(account) && account != "")
        return account;
    return get_object_id(player);
}

private int has_prefix(string value, string prefix)
{
    if (!stringp(value) || !stringp(prefix) || prefix == "")
        return 0;
    if (strlen(value) < strlen(prefix))
        return 0;
    return value[0..strlen(prefix) - 1] == prefix;
}

private mapping query_cached_loadtest_config()
{
    mapping cfg;
    mixed err;
    int now;

    now = time();
    if (mapp(_loadtest_config_cache)
        && now - _loadtest_config_cached_at <= LOADTEST_CONFIG_CACHE_TTL)
        return copy(_loadtest_config_cache);

    err = catch(cfg = CONFIG_D->query_loadtest_config());
    if (err || !mapp(cfg)) {
        _loadtest_config_cache = 0;
        _loadtest_config_cached_at = now;
        return 0;
    }

    _loadtest_config_cache = copy(cfg);
    _loadtest_config_cached_at = now;
    return copy(_loadtest_config_cache);
}

private int is_loadtest_player(object player)
{
    mapping cfg;
    mixed prefixes;
    string account_id;
    string prefix;

    if (!objectp(player))
        return 0;
    cfg = query_cached_loadtest_config();
    if (!mapp(cfg) || !cfg["enabled"])
        return 0;

    account_id = query_player_account_key(player);
    if (account_id == "")
        return 0;

    prefixes = cfg["account_prefixes"];
    if (!arrayp(prefixes))
        return 0;
    foreach (prefix in prefixes) {
        if (has_prefix(account_id, prefix))
            return 1;
    }
    return 0;
}

private int should_allow_loadtest_readonly_worker(object player)
{
    mapping cfg;

    if (!is_loadtest_player(player))
        return 0;
    cfg = query_cached_loadtest_config();
    return mapp(cfg) && cfg["allow_readonly_worker"] ? 1 : 0;
}

private void remove_summary_cache_entry(string key)
{
    if (!stringp(key) || key == "")
        return;
    map_delete(_last_summaries, key);
    map_delete(_summary_cached_at, key);
}

private void prune_summary_cache(int now)
{
    int cached_at;
    string key;
    int inspected;

    foreach (key in keys(_last_summaries || ([]))) {
        if (inspected++ >= SUMMARY_PRUNE_BUDGET)
            break;
        cached_at = _summary_cached_at[key];
        if (cached_at <= 0 || now - cached_at > SUMMARY_CACHE_TTL)
            remove_summary_cache_entry(key);
    }
}

private void cache_summary(string key, mapping summary, int now)
{
    string candidate;
    string oldest;
    int candidate_at;
    int oldest_at;

    oldest = "";
    prune_summary_cache(now);
    if (!mapp(_last_summaries[key])
        && sizeof(keys(_last_summaries || ([]))) >= SUMMARY_CACHE_LIMIT) {
        oldest_at = now + 1;
        foreach (candidate in keys(_last_summaries || ([]))) {
            candidate_at = _summary_cached_at[candidate];
            if (candidate_at < oldest_at) {
                oldest_at = candidate_at;
                oldest = candidate;
            }
        }
        if (oldest != "")
            remove_summary_cache_entry(oldest);
    }
    _last_summaries[key] = copy(summary);
    _summary_cached_at[key] = now;
}

private mapping build_degraded_summary(string key, mapping snapshot, string state)
{
    mapping summary;

    if (mapp(_last_summaries[key])) {
        summary = copy(_last_summaries[key]);
        summary["async_state"] = state;
        summary["loadtest_degraded"] = 1;
        return summary;
    }

    return ([
        "ok": 0,
        "source": "read_summary_d",
        "async": 1,
        "async_key": key,
        "async_state": state,
        "pending_task_id": 0,
        "snapshot": copy(snapshot),
        "digest": ([]),
        "loadtest_degraded": 1,
    ]);
}

private mapping query_digest(string key, mapping snapshot, object player)
{
    mapping response;
    mapping digest;
    mapping summary;
    int cached_at;
    int now;

    now = time();
    prune_summary_cache(now);
    cached_at = _summary_cached_at[key];
    if (mapp(_last_summaries[key])
        && (cached_at <= 0 || now - cached_at > SUMMARY_CACHE_TTL))
        remove_summary_cache_entry(key);

    if (is_loadtest_player(player) && !should_allow_loadtest_readonly_worker(player))
        return build_degraded_summary(key, snapshot, "loadtest_skipped");

    response = ASYNC_COMPUTE_D->query_or_submit(key, "snapshot_digest", snapshot, ([
        "owner_key": key,
        "repeat": SUMMARY_REPEAT,
        "timeout_ms": 3000,
        "ttl_ms": 30000,
    ]));
    digest = mapp(response) ? response["result_spec"] : 0;
    summary = ([
        "ok": mapp(response) && response["success"] ? 1 : 0,
        "source": "read_summary_d",
        "async": 1,
        "async_key": key,
        "async_state": mapp(response) ? (response["async_state"] || response["status"] || "pending") : "pending",
        "pending_task_id": mapp(response) ? (response["pending_task_id"] || response["task_id"] || 0) : 0,
        "snapshot": copy(snapshot),
        "digest": mapp(digest) ? copy(digest) : ([]),
    ]);
    if (summary["ok"])
        cache_summary(key, summary, now);
    else if (mapp(_last_summaries[key]))
        summary = copy(_last_summaries[key]) + ([ "async_state": summary["async_state"] ]);
    return summary;
}

mapping query_named_summary(object player, string category, mapping snapshot)
{
    string key;

    if (!stringp(category) || category == "")
        category = "generic";
    if (!mapp(snapshot))
        snapshot = ([]);
    snapshot = copy(snapshot);
    if (!stringp(snapshot["type"]) || snapshot["type"] == "")
        snapshot["type"] = "named_summary";
    snapshot["category"] = category;
    snapshot["generated_at"] = time();
    key = query_player_key(player) + "/" + category;
    return query_digest(key, snapshot, player);
}

mapping query_quest_summary(object player, string status)
{
    mapping quests;
    mapping snapshot;
    string key;

    if (!stringp(status) || status == "")
        status = "active";
    quests = QUEST_D->query_player_quests_by_status(player, status);
    snapshot = ([
        "type": "quest_summary",
        "status": status,
        "count": mapp(quests) ? sizeof(keys(quests)) : 0,
        "quest_ids": mapp(quests) ? sort_array(keys(quests), 1) : ({ }),
        "generated_at": time(),
    ]);
    key = query_player_key(player) + "/quest/" + status;
    return query_digest(key, snapshot, player);
}

mapping query_skill_summary(object player)
{
    mapping skills;
    mapping snapshot;
    string skill_id;
    int total_level;

    skills = SKILL_D->query_player_skills(player);
    foreach (skill_id in keys(skills || ([]))) {
        if (mapp(skills[skill_id]) && intp(skills[skill_id]["level"]))
            total_level += skills[skill_id]["level"];
    }
    snapshot = ([
        "type": "skill_summary",
        "skill_count": mapp(skills) ? sizeof(keys(skills)) : 0,
        "total_level": total_level,
        "skill_ids": mapp(skills) ? sort_array(keys(skills), 1) : ({ }),
        "generated_at": time(),
    ]);
    return query_digest(query_player_key(player) + "/skills", snapshot, player);
}

mapping query_map_summary(object player)
{
    object env;
    mapping exits;
    mapping snapshot;
    string room_path;

    if (!objectp(player))
        return (["ok":0, "error":"player object is required"]);
    env = environment(player);
    if (!objectp(env))
        return (["ok":0, "error":"player environment is required"]);
    exits = env->query("exits");
    room_path = file_name(env);
    snapshot = ([
        "type": "map_summary",
        "room": room_path || "",
        "title": env->query("short") || "",
        "zone": env->query("zone") || "",
        "exit_count": mapp(exits) ? sizeof(keys(exits)) : 0,
        "exit_dirs": mapp(exits) ? sort_array(keys(exits), 1) : ({ }),
        "generated_at": time(),
    ]);
    return query_digest("room/" + (room_path || "unknown") + "/map", snapshot, player);
}

mapping query_room_view_summary(object player, object env, mapping room_view)
{
    mapping exits;
    mapping snapshot;
    string room_path;

    if (!objectp(player))
        return (["ok":0, "error":"player object is required"]);
    if (!objectp(env))
        env = environment(player);
    if (!objectp(env))
        return (["ok":0, "error":"room object is required"]);
    if (!mapp(room_view))
        room_view = ROOM_VIEW_D->query_room_view(player, env);
    exits = mapp(room_view) ? room_view["exits"] : env->query("exits");
    room_path = file_name(env);
    snapshot = ([
        "type": "room_view_summary",
        "room": room_path || "",
        "title": mapp(room_view) ? (room_view["title"] || "") : (env->query("short") || ""),
        "npc_count": mapp(room_view) && arrayp(room_view["npcs"]) ? sizeof(room_view["npcs"]) : 0,
        "item_count": mapp(room_view) && arrayp(room_view["items"]) ? sizeof(room_view["items"]) : 0,
        "exit_count": mapp(exits) ? sizeof(keys(exits)) : 0,
        "exit_dirs": mapp(exits) ? sort_array(keys(exits), 1) : ({ }),
        "generated_at": time(),
    ]);
    return query_digest(query_player_key(player) + "/look/" + (room_path || "unknown"), snapshot, player);
}

mapping query_inventory_summary(object player)
{
    mapping snapshot;
    object item;
    object *items;
    string *templates;
    int equipped;

    if (!objectp(player))
        return (["ok":0, "error":"player object is required"]);
    items = all_inventory(player);
    templates = ({ });
    foreach (item in items) {
        if (!objectp(item))
            continue;
        if (function_exists("query_equipped", item) && item->query_equipped())
            equipped++;
        if (stringp(item->query("template_id")) && item->query("template_id") != "")
            templates += ({ item->query("template_id") });
    }
    snapshot = ([
        "type": "inventory_summary",
        "item_count": sizeof(items),
        "equipped_count": equipped,
        "template_ids": sort_array(templates, 1),
        "generated_at": time(),
    ]);
    return query_digest(query_player_key(player) + "/inventory", snapshot, player);
}

mapping query_vitals_summary(object player)
{
    mapping snapshot;

    if (!objectp(player))
        return (["ok":0, "error":"player object is required"]);
    snapshot = ([
        "type": "vitals_summary",
        "hp": player->query("combat/hp") || 0,
        "max_hp": player->query("combat/max_hp") || 0,
        "mp": player->query("combat/mp") || 0,
        "max_mp": player->query("combat/max_mp") || 0,
        "ep": player->query("combat/ep") || 0,
        "max_ep": player->query("combat/max_ep") || 0,
        "generated_at": time(),
    ]);
    return query_digest(query_player_key(player) + "/vitals", snapshot, player);
}

mapping query_diag_summary(object player)
{
    mapping combat;
    mapping stats;
    mapping snapshot;
    object env;
    string combat_id;

    if (!objectp(player))
        return (["ok":0, "error":"player object is required"]);
    env = environment(player);
    combat_id = COMBAT_D->query_combat_id(player);
    combat = stringp(combat_id) ? COMBAT_D->query_combat(combat_id) : 0;
    stats = COMBAT_D->query_combat_stats();
    snapshot = ([
        "type": "diag_summary",
        "room": objectp(env) ? file_name(env) : "",
        "combat_id": combat_id || "",
        "in_combat": COMBAT_D->is_in_combat(player) ? 1 : 0,
        "round": mapp(combat) ? (combat["round"] || 0) : 0,
        "active_combats": mapp(stats) ? (stats["active_combats"] || 0) : 0,
        "total_combats": mapp(stats) ? (stats["total_combats"] || 0) : 0,
        "generated_at": time(),
    ]);
    return query_digest(query_player_key(player) + "/diag", snapshot, player);
}

mapping handle_player_logout(object player)
{
    string prefix;
    string key;
    int removed;

    if (!objectp(player))
        return ([ "ok": 0, "error": "player unavailable" ]);
    prefix = query_player_key(player) + "/";
    foreach (key in keys(_last_summaries || ([]))) {
        if (strsrch(key, prefix) == 0) {
            remove_summary_cache_entry(key);
            removed++;
        }
    }
    return ([ "ok": 1, "state": "summary_cache_released", "removed": removed ]);
}

mapping query_read_summary_status()
{
    prune_summary_cache(time());
    return ([
        "cached": sizeof(keys(_last_summaries || ([]))),
        "cache_limit": SUMMARY_CACHE_LIMIT,
        "cache_ttl_seconds": SUMMARY_CACHE_TTL,
        "async_compute": ASYNC_COMPUTE_D->query_async_compute_status(),
    ]);
}
