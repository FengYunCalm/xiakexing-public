/**
 * @file    owner_route_d.c
 * @brief   owner 路由与 actor 快照真源由 owner_route_d 收敛而来）
 *
 * 该服务只固化 owner 路由与安全边界，不并发执行 LPC，也不持有
 * player/room object 引用。真正的移动、attach、save 和同步提交仍由当前主线程 owner 完成。
 * 多 scope catalog / session route / cross-scope operation / player transfer /
 * driver experiment 原型面已删除，owner metadata 不再宣称可切换多 scope。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

#define OWNER_SCOPE_GLOBAL_LOGIN      "global/login"
#define OWNER_SCOPE_DUNGEON_COMBAT    "dungeon/combat_instance"

nosave string _last_error_message;

private int reject_route(string message)
{
    _last_error_message = message;
    return 0;
}

private int has_prefix(string text, string prefix)
{
    return stringp(text) && stringp(prefix) && prefix != "" && strsrch(text, prefix) == 0;
}

private string normalize_object_path(string path)
{
    if (!stringp(path) || path == "")
        return "";
    return path[0..0] == "/" ? path : "/" + path;
}

private string query_actor_player_id(object player)
{
    string player_id;

    if (!objectp(player))
        return "unknown";
    player_id = get_object_id(player);
    return stringp(player_id) && player_id != ""
        ? player_id : file_name(player);
}

private string query_actor_player_name(object player)
{
    string player_name;

    if (!objectp(player))
        return "";
    player_name = get_object_name(player);
    return stringp(player_name) && player_name != ""
        ? player_name : query_actor_player_id(player);
}

void create()
{
    ::create();
    _last_error_message = "";
}

int health_check()
{
    return 1;
}

string query_owner_scope_for_room(string room_path)
{
    mapping region_context;

    room_path = normalize_object_path(room_path);
    if (!stringp(room_path) || room_path == "")
        return OWNER_SCOPE_GLOBAL_LOGIN;
    if (has_prefix(room_path, "/world/dungeon/")
        || has_prefix(room_path, "/world/instances/"))
        return OWNER_SCOPE_DUNGEON_COMBAT;
    if (has_prefix(room_path, "/world/")) {
        region_context = REGION_D->resolve_region_context(room_path);
        return mapp(region_context) && region_context["ok"]
            && stringp(region_context["scope_key"])
            ? region_context["scope_key"] : "";
    }
    return OWNER_SCOPE_GLOBAL_LOGIN;
}

mapping query_owner_route_for_object(object target)
{
    object env;
    string path;
    string normalized_path;
    string env_path;
    string owner_kind;
    string owner_id;
    string owner_scope_id;
    string player_id;

    if (!objectp(target))
        return ([ "ok": 0, "reason": "object is required" ]);

    path = file_name(target);
    normalized_path = normalize_object_path(path);
    env = environment(target);
    env_path = objectp(env) ? normalize_object_path(file_name(env)) : "";

    if (function_exists("is_player", target) && call_other(target, "is_player")) {
        player_id = query_actor_player_id(target);
        owner_kind = "player";
        owner_id = "owner/player/" + player_id;
        owner_scope_id = query_owner_scope_for_room(env_path);
    } else if (has_prefix(normalized_path, "/world/")) {
        owner_kind = "room";
        owner_id = "owner/room/" + path;
        owner_scope_id = query_owner_scope_for_room(normalized_path);
    } else if (has_prefix(normalized_path, "/daemons/services/")) {
        owner_kind = "global_service";
        owner_id = "owner/global/service/" + path;
        owner_scope_id = OWNER_SCOPE_GLOBAL_LOGIN;
    } else if (objectp(env)) {
        owner_kind = "contained_object";
        owner_id = "owner/container/" + file_name(env);
        owner_scope_id = query_owner_scope_for_room(env_path);
    } else {
        owner_kind = "global_object";
        owner_id = "owner/global/object/" + path;
        owner_scope_id = OWNER_SCOPE_GLOBAL_LOGIN;
    }

    if (has_prefix(normalized_path, "/world/") && owner_scope_id == "")
        return ([
            "ok": 0,
            "error_code": "REGION_UNRESOLVED",
            "object": path,
            "owner_kind": owner_kind,
            "owner_id": owner_id,
            "owner_scope_id": "",
            "environment": env_path,
            "direct_cross_owner_write": 0,
        ]);

    return ([
        "ok": 1,
        "object": path,
        "owner_kind": owner_kind,
        "owner_id": owner_id,
        "owner_scope_id": owner_scope_id,
        "environment": env_path,
        "single_writer": 1,
        "message_only_cross_owner": 1,
        "direct_cross_owner_write": 0,
    ]);
}

mapping build_cross_owner_message_spec(string source_owner, string target_owner, string message_type, mapping payload)
{
    if (!stringp(source_owner) || source_owner == "" || !stringp(target_owner) || target_owner == "")
        return ([ "allowed": 0, "reason": "source_owner and target_owner are required" ]);
    if (!stringp(message_type) || message_type == "")
        return ([ "allowed": 0, "reason": "message_type is required" ]);

    return ([
        "type": "owner/cross_owner_message",
        "allowed": 1,
        "source_owner": source_owner,
        "target_owner": target_owner,
        "message_type": message_type,
        "payload": mapp(payload) ? copy(payload) : ([]),
        "commit_owner": target_owner,
        "requires_owner_mailbox": 1,
        "message_only_cross_owner": 1,
        "direct_cross_owner_write": 0,
    ]);
}

mapping build_player_actor_snapshot(object player)
{
    object room;
    string room_path;
    string owner_scope_id;
    string player_id;

    if (!objectp(player))
        return ([ "ok": 0, "reason": "player object is required" ]);

    room = environment(player);
    room_path = objectp(room) ? file_name(room) : "";
    owner_scope_id = query_owner_scope_for_room(room_path);
    player_id = query_actor_player_id(player);
    if (has_prefix(normalize_object_path(room_path), "/world/")
        && owner_scope_id == "")
        return ([
            "ok": 0,
            "error_code": "REGION_UNRESOLVED",
            "player_id": player_id,
        ]);

    return ([
        "ok": 1,
        "type": "player_actor_snapshot",
        "player_id": player_id,
        "owner_key": "player/" + player_id,
        "name": query_actor_player_name(player),
        "room": room_path,
        "owner_scope_id": owner_scope_id,
        "combat": ([
            "hp": player->query("combat/hp") || 0,
            "max_hp": player->query("combat/max_hp") || 0,
            "mp": player->query("combat/mp") || 0,
            "max_mp": player->query("combat/max_mp") || 0,
            "ep": player->query("combat/ep") || 0,
            "max_ep": player->query("combat/max_ep") || 0,
        ]),
        "route": ([
            "owner_scope_id": owner_scope_id,
            "message_only_between_owner_scopes": 1,
            "single_write_owner": 1,
        ]),
    ]);
}

/*
 * actor_score consumes only these frozen combat facts.  Keep routing and room
 * identity out of this task input: they neither affect its result nor belong
 * in its async input hash.
 */
mapping build_player_actor_score_snapshot_from_combat(object player, mapping combat)
{
    string player_id;

    if (!objectp(player) || !mapp(combat))
        return ([ "ok": 0, "reason": "player object is required" ]);

    player_id = query_actor_player_id(player);
    return ([
        "ok": 1,
        "type": "player_actor_score_snapshot",
        "player_id": player_id,
        "owner_key": "player/" + player_id,
        "combat": ([
            "hp": combat["hp"] || 0,
            "max_hp": combat["max_hp"] || 0,
            "mp": combat["mp"] || 0,
            "max_mp": combat["max_mp"] || 0,
            "ep": combat["ep"] || 0,
            "max_ep": combat["max_ep"] || 0,
        ]),
    ]);
}

mapping build_player_actor_score_snapshot(object player)
{
    mapping combat;

    if (!objectp(player))
        return ([ "ok": 0, "reason": "player object is required" ]);
    combat = ([
        "hp": player->query("combat/hp") || 0,
        "max_hp": function_exists("query_max_hp", player)
            ? player->query_max_hp() : (player->query("combat/max_hp") || 0),
        "mp": player->query("combat/mp") || 0,
        "max_mp": function_exists("query_max_mp", player)
            ? player->query_max_mp() : (player->query("combat/max_mp") || 0),
        "ep": player->query("combat/ep") || 0,
        "max_ep": function_exists("query_max_ep", player)
            ? player->query_max_ep() : (player->query("combat/max_ep") || 0),
    ]);
    return build_player_actor_score_snapshot_from_combat(player, combat);
}

mapping query_owner_migration_policy()
{
    return ([
        "status": "formal_gameplay_owner_policy",
        "phases": ({ "readonly", "player", "room", "combat", "global_service" }),
        "readonly": ([
            "execution_mode": "snapshot_async_only",
            "direct_object_access": 0,
            "direct_cross_owner_write": 0,
        ]),
        "player": ([
            "execution_mode": "owner_scheduler_serial",
            "owner_kind": "player",
            "requires_session_fifo": 1,
            "requires_commit_boundary": 1,
            "direct_cross_owner_write": 0,
        ]),
        "room": ([
            "execution_mode": "owner_scheduler_serial",
            "owner_kind": "room",
            "requires_message_bus": 1,
            "requires_commit_boundary": 1,
            "direct_cross_owner_write": 0,
        ]),
        "combat": ([
            "execution_mode": "message_plus_transaction_boundary",
            "owner_kind": "combat",
            "requires_message_bus": 1,
            "requires_commit_boundary": 1,
            "direct_cross_owner_write": 0,
        ]),
        "global_service": ([
            "execution_mode": "single_writer_service_owner",
            "owner_kind": "global_service",
            "requires_message_bus": 1,
            "requires_idempotency": 1,
            "direct_cross_owner_write": 0,
        ]),
        "forbidden_direct_parallel_lpc": ({
            "move_object",
            "destruct_object",
            "clone_object",
            "load_object",
            "save_object",
            "economy_settlement",
            "mail_attachment_claim",
        }),
        "fallback": "owner_scheduler_or_single_thread",
    ]);
}

mapping query_owner_migration_phase(string phase)
{
    mapping policy;

    if (!stringp(phase) || phase == "")
        return ([ "ok": 0, "reason": "phase is required" ]);
    policy = query_owner_migration_policy();
    if (!mapp(policy[phase]))
        return ([ "ok": 0, "reason": "unknown owner migration phase" ]);

    return ([
        "ok": 1,
        "phase": phase,
        "policy": copy(policy[phase]),
        "fallback": policy["fallback"],
    ]);
}
