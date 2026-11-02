/**
 * @file    login_projection_d.c
 * @brief   连接期投影身份与登录位置恢复真源。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>
#include <system/globals.h>
#include <system/xk_protocol.h>

inherit "/lib/framework/daemon";

nosave mapping _health_status;
nosave mapping _projection_by_actor;

private mapping failure(string code, string message)
{
    return ([
        "ok": 0,
        "stage": "failed",
        "source": "",
        "requested_path": "",
        "template_room": "",
        "runtime_room": "",
        "final_room": "",
        "projection_reason": code || "projection_failed",
        "persisted": 0,
        "error_code": code || "projection_failed",
        "error": message || "projection operation failed",
    ]);
}

private mapping projection_result(string stage, mapping location, string reason,
    int persisted)
{
    mapping result;

    result = ([
        "ok": 1,
        "stage": stage,
        "source": mapp(location) ? (location["source"] || "") : "",
        "requested_path": mapp(location) ? (location["requested_path"] || "") : "",
        "template_room": mapp(location) ? (location["template_room"] || "") : "",
        "runtime_room": mapp(location) ? (location["runtime_room"] || "") : "",
        "final_room": mapp(location) ? (location["final_room"] || location["runtime_room"] ||
        "") : "",
        "projection_reason": mapp(location) && location["projection_reason"]
            ? location["projection_reason"] : (reason || "login_projection"),
        "persisted": persisted ? 1 : 0,
        "location": location,
    ]);
    return result;
}

private string query_actor_key(object actor)
{
    if (!objectp(actor))
        return "";
    return sprintf("%O", actor);
}

private string query_actor_id(object actor)
{
    return objectp(actor) ? get_object_id(actor) : "";
}

private string new_projection_id(object actor, int revision)
{
    return hash("sha256", sprintf("xkx-login-projection:%s:%d:%d:%d",
        query_actor_id(actor), revision, time(), random(2147483647)));
}

private mapping ensure_projection_identity(object actor)
{
    string actor_key;
    mapping identity;
    int revision;

    if (!objectp(actor))
        return ([ "projection_id": "", "projection_revision": 0,
            "projection_generation": 0 ]);
    actor_key = query_actor_key(actor);
    identity = _projection_by_actor[actor_key];
    if (!mapp(identity)) {
        revision = 0;
        identity = ([
            "projection_id": new_projection_id(actor, revision),
            "projection_revision": revision,
            "projection_generation": CONTENT_REGISTRY_D->query_content_generation(),
            "reason": "login_projection_initialized",
            "changed_at": time(),
        ]);
        _projection_by_actor[actor_key] = identity;
    }
    return identity;
}

mapping query_projection_identity(object actor)
{
    mapping identity;

    identity = copy(ensure_projection_identity(actor));
    if (mapp(identity))
        identity["projection_generation"] = CONTENT_REGISTRY_D->query_content_generation();
    return identity;
}

mapping advance_projection(object actor, string reason)
{
    mapping identity;
    string actor_key;
    int revision;

    if (!objectp(actor))
        return ([ "projection_id": "", "projection_revision": 0,
            "projection_generation": 0 ]);
    actor_key = query_actor_key(actor);
    identity = ensure_projection_identity(actor);
    revision = (identity["projection_revision"] || 0) + 1;
    if (revision <= 0)
        revision = 1;
    identity = ([
        "projection_id": new_projection_id(actor, revision),
        "projection_revision": revision,
        "projection_generation": CONTENT_REGISTRY_D->query_content_generation(),
        "reason": stringp(reason) ? reason : "",
        "changed_at": time(),
    ]);
    _projection_by_actor[actor_key] = identity;
    CLIENT_SYNC_D->discard_non_chat_projection(actor, identity);
    return copy(identity);
}

int projection_identity_matches(object actor, mapping identity)
{
    mapping current;

    if (!objectp(actor) || !mapp(identity))
        return 0;
    current = query_projection_identity(actor);
    return current["projection_id"] == identity["projection_id"]
        && current["projection_revision"] == identity["projection_revision"]
        && current["projection_generation"] == identity["projection_generation"];
}

mapping query_projection_context(object actor)
{
    mapping projection;

    projection = query_projection_identity(actor);
    return ([
        "projection_id": projection["projection_id"],
        "projection_revision": projection["projection_revision"],
        "projection_generation": projection["projection_generation"],
        "reason": projection["reason"] || "",
        "protocol_version": XK_PROTOCOL_VERSION_STRING,
    ]);
}


private mapping restore_login_projection_core(object actor)
{
    mapping restored;

    if (!objectp(actor))
        return failure("invalid_actor", "login projection restore requires an actor");
    actor->set_temp("login/bootstrap_room_sync", 1);
    restored = WORLD_TRAVEL_D->restore_login_location(actor);
    if (!mapp(restored) || !restored["ok"])
        return restored;
    advance_projection(actor, "login_projection_restore");
    CLIENT_SYNC_D->request_full_player_snapshot(actor);
    return ([ "ok": 1, "location": restored ]);
}

private string query_login_start_room()
{
    string room_path;

    room_path = CONFIG_D->query_start_room();
    return stringp(room_path) ? room_path : "";
}

private void refresh_login_room(object room)
{
    mixed err;

    if (!objectp(room))
        return;
    if (function_exists("refresh_room_entities", room)) {
        err = catch(call_other(room, "refresh_room_entities"));
        if (err)
            log_file("system/session", sprintf("[%s] refresh_login_room failed for %s: %O\n",
                ctime(time()), file_name(room), err));
        return;
    }
    if (function_exists("ensure_room_entities", room)) {
        err = catch(call_other(room, "ensure_room_entities"));
        if (err)
            log_file("system/session", sprintf("[%s] ensure_room_entities failed for %s: %O\n",
                ctime(time()), file_name(room), err));
    }
}

private object resolve_start_room_object(object actor)
{
    string start_room_path;
    object start_room;

    start_room_path = query_login_start_room();
    if (start_room_path == "") {
        log_file("system/session", sprintf("[%s] resolve_start_room_object: no configured start room\n",
            ctime(time())));
        return 0;
    }
    start_room = WORLD_TRAVEL_D->resolve_public_room(start_room_path, ([
        "actor": actor,
        "reason": "login_start_room",
    ]));
    if (!objectp(start_room)) {
        log_file("system/session", sprintf("[%s] resolve_start_room_object: failed to resolve %s\n",
            ctime(time()), start_room_path));
        return 0;
    }
    refresh_login_room(start_room);
    return start_room;
}

/* 紧急兜底放置的 move 域事务提交：真源（WORLD_TRAVEL_D）签发 token 持有
 * 对象句柄，payload 只携带 token；apply 结果经 coordinator 的
 * transaction.apply_result 透传给调用方。 */
private mapping commit_emergency_start_move(object actor, object start_room)
{
    mapping created;
    mapping payload;
    mapping prepared;
    mapping committed;
    mapping applied;
    string move_token;
    mixed err;
    int txn_id;

    move_token = WORLD_TRAVEL_D->register_move_proposal(actor, start_room, ([
        "reason": "login_emergency_fallback",
    ]));
    if (!stringp(move_token) || move_token == "")
        return ([ "ok": 0, "error_code": "move_proposal_register_failed" ]);
    payload = ([
        "schema_version": 1,
        "move": ([ "move_token": move_token ]),
    ]);
    err = catch(created = OWNER_COMMIT_D->create_domain_transaction("move",
        sprintf("world/login_emergency/%s/%d", file_name(actor), time()),
        ({ "owner/world/move" }), payload));
    if (err)
        return 0;
    if (!mapp(created) || created["ok"] != 1)
        return 0;
    txn_id = mapp(created["transaction"]) ? created["transaction"]["id"] : 0;
    err = catch(prepared = OWNER_COMMIT_D->prepare_transaction(txn_id));
    if (err || !mapp(prepared) || prepared["ok"] != 1)
        return 0;
    err = catch(committed = OWNER_COMMIT_D->commit_transaction(txn_id));
    if (err || !mapp(committed) || committed["ok"] != 1)
        return 0;
    if (!mapp(committed["transaction"])
        || !mapp(applied = committed["transaction"]["apply_result"])
        || !mapp(applied["move_result"]))
        return 0;
    return applied["move_result"];
}

mapping restore_login_projection(object actor)
{
    mapping restored;
    object start_room;
    mixed err;

    if (!MIGRATION_D->is_identity_migration_ready())
        return failure("identity_migration_not_ready",
            "identity migration has not completed; login projection is blocked");
    if (!objectp(actor))
        return failure("invalid_actor", "login projection restore requires an actor");
    err = catch(restored = restore_login_projection_core(actor));
    if (!err && mapp(restored) && restored["ok"])
        return projection_result("projection_restore", restored["location"],
            "login_projection_restore", 1);
    if (!err && mapp(restored) && restored["fatal"])
        return restored;

    err = catch(restored = WORLD_TRAVEL_D->restore_login_location(actor));
    if (!err && mapp(restored) && restored["ok"]) {
        advance_projection(actor, "login_location_fallback");
        return projection_result("location_fallback", restored,
            "login_location_fallback", 1);
    }

    start_room = resolve_start_room_object(actor);
    if (!objectp(start_room))
        return failure("start_room_unavailable",
            "configured start room is unavailable");
    /* 紧急兜底放置走 move 域 owner 事务：actor 未进 users()（连接尚未
     * 交接），payload 以真源签发的 move_token 携带对象句柄；apply 失败
     * 由协调器统一回滚后上报。 */
    err = catch(restored = commit_emergency_start_move(actor, start_room));
    if (err || !mapp(restored) || !restored["ok"])
        return failure("emergency_fallback_failed",
            "emergency start room placement failed");
    restored["source"] = "emergency_start_room";
    restored["requested_path"] = query_login_start_room();
    restored["projection_reason"] = "login_emergency_fallback";
    restored["final_room"] = restored["runtime_room"] || "";
    advance_projection(actor, "login_emergency_fallback");
    return projection_result("emergency_fallback", restored,
        "login_emergency_fallback", 1);
}

mapping query_health_snapshot()
{
    mapping snapshot;
    mapping migration_status;

    snapshot = copy(_health_status || ([]));
    snapshot["active_projections"] = sizeof(keys(_projection_by_actor || ([])));
    snapshot["status"] = snapshot["last_error"] == "" ? "ok" : "degraded";
    if (!MIGRATION_D->is_identity_migration_ready()) {
        migration_status = MIGRATION_D->query_identity_migration_status();
        snapshot["status"] = "degraded";
        snapshot["last_error"] = migration_status["last_error"]
            || "identity migration is not ready";
    }
    return snapshot;
}

/* 终止生命周期在 destruct 前释放对象键；重试重复调用保持幂等。 */
mapping handle_player_logout(object actor)
{
    string actor_key;

    if (!objectp(actor))
        return ([ "ok": 0, "error": "player unavailable" ]);
    actor_key = query_actor_key(actor);
    if (actor_key != "")
        map_delete(_projection_by_actor, actor_key);
    return ([ "ok": 1, "state": "projection_released" ]);
}

void create()
{
    ::create();
    _health_status = ([
        "last_error": "",
    ]);
    _projection_by_actor = ([]);
}

#define XK_PROTOCOL_CORE "/kernel/framework/xk_protocol_core"

varargs int xk_send_projection_state(object actor, mapping payload,
    mapping response_context)
{
    if (!objectp(actor))
        return 0;
    return call_other(XK_PROTOCOL_CORE, "xk_send", actor, XK_REAL,
        mapp(payload) ? payload : ([]), response_context);
}
