/**
 * @file    player_actor_analysis_d.c
 * @brief   玩家读侧多核分析服务
 *
 * 本服务只把主线程采集的玩家快照交给 driver worker 做纯计算，返回
 * result spec；不直接修改玩家、背包、经济或战斗状态。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";
inherit "/lib/std/utils/util_player";

#define ROSTER_DIGEST_OWNER "online/roster"
#define ACTOR_SCORE_SOURCE "driver_worker_async_actor_score"

void create()
{
    ::create();
}

private mapping build_online_roster_entry(object player)
{
    mapping snapshot;

    snapshot = query_player_public_snapshot(player);
    return ([
        "name": stringp(snapshot["name"]) && snapshot["name"] != "" ? snapshot["name"] : "无名",
        "id": stringp(snapshot["id"]) && snapshot["id"] != "" ? snapshot["id"] : "未知",
        "progression_rank": intp(snapshot["progression_rank"]) ? snapshot["progression_rank"] : 1,
        "skill_cap": intp(snapshot["skill_cap"]) ? snapshot["skill_cap"] : 0,
        "exp": intp(snapshot["exp"]) ? snapshot["exp"] : 0,
        "idle": intp(snapshot["idle"]) ? snapshot["idle"] : 0,
        "last_on": intp(snapshot["last_on"]) ? snapshot["last_on"] : time(),
        "is_wizard": wizardp(player) ? 1 : 0,
        "role": wizardp(player) ? "wizard" : "player",
    ]);
}

private string query_actor_score_key(mapping snapshot)
{
    if (mapp(snapshot) && stringp(snapshot["owner_key"]) && snapshot["owner_key"] != "")
        return snapshot["owner_key"];
    if (mapp(snapshot) && stringp(snapshot["player_id"]) && snapshot["player_id"] != "")
        return "player/" + snapshot["player_id"];
    return "player/unknown";
}

private string query_actor_score_key_for_player(object player)
{
    string player_id;

    if (!objectp(player))
        return "player/unknown";
    player_id = PLAYER_STATE_D->query_player_id(player);
    if (stringp(player_id) && player_id != "")
        return "player/" + player_id;
    return "player/" + file_name(player);
}

private mapping decorate_actor_score(mapping result, mapping snapshot,
    mapping response)
{
    string key;
    string async_state;

    key = query_actor_score_key(snapshot);
    async_state = mapp(response) && stringp(response["async_state"])
        && response["async_state"] != ""
            ? response["async_state"] : "ready";
    result = mapp(result) ? copy(result) : ([]);
    result["ok"] = 1;
    result["source"] = ACTOR_SCORE_SOURCE;
    result["async"] = 1;
    result["async_state"] = async_state;
    result["async_input_hash"] = mapp(response)
        && intp(response["async_input_hash"])
            ? response["async_input_hash"] : 0;
    result["completed_at"] = mapp(response) && intp(response["completed_at"])
        ? response["completed_at"] : 0;
    result["player_id"] = snapshot["player_id"] || "";
    result["owner_scope_id"] = snapshot["owner_scope_id"] || "";
    result["commit_boundary"] = "read_only_projection";
    return result;
}

private mapping query_player_actor_analysis_from_snapshot(mapping snapshot)
{
    mapping response;
    mapping result;
    string key;

    if (!mapp(snapshot) || !snapshot["ok"]) {
        return ([
            "ok": 0,
            "source": ACTOR_SCORE_SOURCE,
            "async": 1,
            "async_state": "failed",
            "error": snapshot["reason"] || "invalid actor snapshot",
        ]);
    }

    key = query_actor_score_key(snapshot);
    response = ASYNC_COMPUTE_D->query_or_submit(key, "actor_score", snapshot, ([
        "owner_key": key,
        "timeout_ms": 3000,
        "ttl_ms": 30000,
    ]));
    result = mapp(response) ? response["result_spec"] : 0;
    if (mapp(result))
        return decorate_actor_score(result, snapshot, response);
    return ([
        "ok": 0,
        "source": ACTOR_SCORE_SOURCE,
        "async": 1,
        "async_state": mapp(response) ? (response["async_state"] || response["status"] || "pending") : "failed",
        "pending_task_id": mapp(response) ? (response["pending_task_id"] || response["task_id"] || 0) : 0,
        "async_input_hash": mapp(response) && intp(response["async_input_hash"])
            ? response["async_input_hash"] : 0,
        "completed_at": mapp(response) && intp(response["completed_at"])
            ? response["completed_at"] : 0,
        "player_id": snapshot["player_id"] || "",
        "owner_scope_id": snapshot["owner_scope_id"] || "",
        "error": mapp(response) ? (response["error"] || "") : "async compute failed",
    ]);
}

mapping query_player_actor_analysis_from_combat(object player, mapping combat)
{
    mapping snapshot;

    if (!objectp(player) || !mapp(combat))
        return ([ "ok": 0, "error": "player and combat facts are required" ]);
    snapshot = OWNER_ROUTE_D->build_player_actor_score_snapshot_from_combat(player, combat);
    return query_player_actor_analysis_from_snapshot(snapshot);
}

mapping query_player_actor_analysis(object player)
{
    mapping snapshot;

    if (!objectp(player))
        return ([ "ok": 0, "error": "player object is required" ]);
    snapshot = OWNER_ROUTE_D->build_player_actor_score_snapshot(player);
    return query_player_actor_analysis_from_snapshot(snapshot);
}

/*
 * Current-hit freshness must observe the actor worker terminal state without
 * rebuilding its live snapshot or submitting another task.  The full snapshot
 * remains the miss/refresh path in query_player_actor_analysis().
 */
mapping query_player_actor_analysis_token(object player)
{
    mapping response;
    string key;
    string state;

    key = query_actor_score_key_for_player(player);
    response = ASYNC_COMPUTE_D->poll_async_task_token(key);
    state = mapp(response) && stringp(response["async_state"])
        && response["async_state"] != ""
            ? response["async_state"]
            : mapp(response) && stringp(response["status"])
                && response["status"] != "" ? response["status"] : "empty";
    return ([
        "async_input_hash": mapp(response) && intp(response["async_input_hash"])
            ? response["async_input_hash"] : 0,
        "async_state": state,
        "completed_at": mapp(response) && intp(response["completed_at"])
            ? response["completed_at"] : 0,
    ]);
}

private mapping decorate_roster_digest(mapping result, string state)
{
    result = mapp(result) ? copy(result) : ([]);
    result["ok"] = 1;
    result["state"] = state;
    result["source"] = "driver_worker_async_snapshot_digest";
    result["async"] = 1;
    return result;
}

private mapping build_online_player_roster(object *players, int digest_repeat,
    string digest_owner)
{
    object player;
    mapping *entries;
    mapping entry;
    mapping digest;
    mapping digest_response;
    mapping snapshot;
    int wizard_count;
    int player_count;

    if (!arrayp(players))
        players = ({ });
    if (!stringp(digest_owner) || digest_owner == "")
        digest_owner = ROSTER_DIGEST_OWNER;
    entries = ({ });
    foreach (player in players) {
        if (!objectp(player))
            continue;
        entry = build_online_roster_entry(player);
        entries += ({ entry });
        if (entry["is_wizard"])
            wizard_count++;
        else
            player_count++;
    }

    entries = sort_array(entries,
        (: ($2["is_wizard"] - $1["is_wizard"])
            || strcmp(($1["id"] || ""), ($2["id"] || "")) :));
    snapshot = ([
        "type": "online_player_roster",
        "entries": entries,
        "total": sizeof(entries),
        "wizard_count": wizard_count,
        "player_count": player_count,
    ]);
    digest_response = ASYNC_COMPUTE_D->query_or_submit(digest_owner,
        "snapshot_digest", snapshot, ([
        "owner_key": digest_owner,
        "repeat": digest_repeat,
        "timeout_ms": 3000,
        "ttl_ms": 30000,
    ]));
    digest = mapp(digest_response) && mapp(digest_response["result_spec"])
        ? decorate_roster_digest(digest_response["result_spec"], digest_response["async_state"] || "ready")
        : ([
            "ok": 0,
            "state": mapp(digest_response) ? (digest_response["async_state"] || digest_response["status"] || "pending") : "failed",
            "source": "driver_worker_async_snapshot_digest",
            "async": 1,
            "pending_task_id": mapp(digest_response) ? (digest_response["pending_task_id"] || digest_response["task_id"] || 0) : 0,
            "error": mapp(digest_response) ? (digest_response["error"] || "") : "async compute failed",
        ]);

    return ([
        "ok": 1,
        "type": "online_player_roster",
        "total": sizeof(entries),
        "wizard_count": wizard_count,
        "player_count": player_count,
        "entries": entries,
        "digest": digest,
        "source": "driver_worker_snapshot_digest",
        "commit_boundary": "read_only_projection",
    ]);
}

mapping query_online_player_roster(int require_environment, int digest_repeat)
{
    return build_online_player_roster(query_online_players(require_environment),
        digest_repeat, ROSTER_DIGEST_OWNER);
}

mapping query_online_player_roster_for(object observer, int require_environment,
    int digest_repeat)
{
    string observer_id;

    observer_id = objectp(observer) ? get_object_display_id(observer, "") : "";
    return build_online_player_roster(
        query_online_players(require_environment),
        digest_repeat,
        observer_id == "" ? ROSTER_DIGEST_OWNER
            : ROSTER_DIGEST_OWNER + "/" + observer_id);
}
