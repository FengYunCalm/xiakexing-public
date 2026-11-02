/**
 * @file    world_projection_d.c
 * @brief   玩家敏感世界投影配置与 NPC 视图 owner。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

/* Player-specific actor/hide projection state has one owner: this daemon. */

/* 实体索引 key 的世界键前缀（单属主；调整时全站同步，防 key 分叉）。 */
#define WORLD_KEY_PUBLIC "public"

nosave mapping _projection_actors;
nosave mapping _projection_hide_rules;
nosave mapping _projection_status;
private mapping query_projection_config()
{
    mapping config;

    config = CONFIG_D->query_full_config("world_projection");
    return mapp(config) ? config : ([]);
}

private string *coerce_string_array(mixed value)
{
    string *result;
    mixed entry;

    result = ({ });
    if (!arrayp(value))
        return result;
    foreach (entry in value) {
        if (stringp(entry) && entry != "")
            result += ({ entry });
    }
    return result;
}

private int validate_projection_actor(string actor_id, mapping actor_def)
{
    string actor_type;

    if (!stringp(actor_id) || actor_id == "" || !mapp(actor_def))
        return 0;
    if (!stringp(actor_def["name"]) || actor_def["name"] == "")
        return 0;
    if (!stringp(actor_def["target_present_id"]) || actor_def["target_present_id"] == "")
        return 0;
    if (sizeof(coerce_string_array(actor_def["ids"])) == 0)
        return 0;

    actor_type = actor_def["type"] || "fixed_room_actor";
    switch (actor_type) {
        case "tracked_room_actor":
            return stringp(actor_def["tracked_room_flag"]) && actor_def["tracked_room_flag"] != "";

        case "fixed_room_actor":
            return stringp(actor_def["room"]) && actor_def["room"] != "";
    }
    return 0;
}

private int validate_projection_hide_rule(string rule_id, mapping rule)
{
    if (!stringp(rule_id) || rule_id == "" || !mapp(rule))
        return 0;
    if (!stringp(rule["room"]) || rule["room"] == "")
        return 0;
    if (!stringp(rule["hide_flag"]) || rule["hide_flag"] == "")
        return 0;
    if (stringp(rule["match_present_id"]) && rule["match_present_id"] != "")
        return 1;
    return sizeof(coerce_string_array(rule["match_names"])) > 0;
}

private void mark_projection_reload_failure(string error_message)
{
    if (!mapp(_projection_status))
        _projection_status = ([]);
    _projection_status["config_name"] = "world_projection";
    _projection_status["last_attempt_at"] = time();
    _projection_status["last_reload_ok"] = 0;
    _projection_status["last_error"] = stringp(error_message) ? error_message : "unknown";
}

private void mark_projection_reload_success()
{
    int version;

    if (!mapp(_projection_status))
        _projection_status = ([]);
    version = intp(_projection_status["config_version"]) ? _projection_status["config_version"] : 0;
    _projection_status["config_name"] = "world_projection";
    _projection_status["config_version"] = version + 1;
    _projection_status["last_attempt_at"] = time();
    _projection_status["last_reload_at"] = time();
    _projection_status["last_reload_ok"] = 1;
    _projection_status["last_error"] = "";
}

void reload_projection_registry()
{
    mapping config;
    mapping configured_actors;
    mapping configured_hide_rules;
    mapping validated_actors;
    mapping validated_hide_rules;
    mapping actor_def;
    mapping hide_rule;
    string actor_id;
    string rule_id;
    int invalid_actor_count;
    int invalid_hide_rule_count;

    config = query_projection_config();
    configured_actors = mapp(config["actors"]) ? config["actors"] : ([ ]);
    configured_hide_rules = mapp(config["hide_rules"]) ? config["hide_rules"] : ([ ]);
    validated_actors = ([ ]);
    validated_hide_rules = ([ ]);

    foreach (actor_id, actor_def in configured_actors) {
        if (!validate_projection_actor(actor_id, actor_def)) {
            invalid_actor_count++;
            continue;
        }
        validated_actors[actor_id] = copy(actor_def);
        validated_actors[actor_id]["id"] = actor_id;
        validated_actors[actor_id]["ids"] = coerce_string_array(actor_def["ids"]);
    }
    foreach (rule_id, hide_rule in configured_hide_rules) {
        if (!validate_projection_hide_rule(rule_id, hide_rule)) {
            invalid_hide_rule_count++;
            continue;
        }
        validated_hide_rules[rule_id] = copy(hide_rule);
        validated_hide_rules[rule_id]["id"] = rule_id;
        validated_hide_rules[rule_id]["match_names"] =
        coerce_string_array(hide_rule["match_names"]);
    }

    if (invalid_actor_count > 0 || invalid_hide_rule_count > 0) {
        mark_projection_reload_failure(sprintf(
            "invalid world projection definitions: actors=%d hide_rules=%d",
            invalid_actor_count, invalid_hide_rule_count));
        return;
    }
    _projection_actors = validated_actors;
    _projection_hide_rules = validated_hide_rules;
    mark_projection_reload_success();
}

void create()
{
    ::create();
    _projection_actors = ([ ]);
    _projection_hide_rules = ([ ]);
    _projection_status = ([
        "config_name": "world_projection",
        "config_version": 0,
        "last_attempt_at": 0,
        "last_reload_at": 0,
        "last_reload_ok": 0,
        "last_error": "",
    ]);
    reload_projection_registry();
    CONFIG_D->compat_register_config_callback("world_projection", "on_config_reloaded", this_object());
}

void on_config_reloaded(string key, string config_name)
{
    if (key == "world_projection" || config_name == "world_projection")
        reload_projection_registry();
}

void on_config_reload_rollback(string key, string config_name)
{
    if (key == "world_projection" || config_name == "world_projection")
        reload_projection_registry();
}
private string query_projection_scope_key(object player, object room)
{
    if (!objectp(player) || !objectp(room))
        return "";
    return WORLD_CONTEXT_D->query_player_personal_scope_key(player, room);
}

private string query_projection_actor_present_id(string actor_id, mapping actor_def)
{
    string *ids;

    if (mapp(actor_def) && stringp(actor_def["target_present_id"])
        && actor_def["target_present_id"] != "")
        return actor_def["target_present_id"];
    ids = mapp(actor_def) ? coerce_string_array(actor_def["ids"]) : ({ });
    if (sizeof(ids) > 0)
        return ids[0];
    return stringp(actor_id) ? actor_id : "";
}

private mapping build_projection_actor_payload(string actor_id, mapping actor_def)
{
    string name;
    string summary;
    string description;

    if (!stringp(actor_id) || actor_id == "" || !mapp(actor_def))
        return 0;
    name = actor_def["name"] || "";
    if (!stringp(name) || name == "")
        return 0;
    summary = actor_def["summary"] || "";
    description = actor_def["description"] || "";
    if (!stringp(summary) || summary == "")
        summary = stringp(description) && description != "" ? description : name + "正在此处等候。";
    if (!stringp(description) || description == "")
        description = summary;

    return ([
        "id": query_projection_actor_present_id(actor_id, actor_def),
        "projection_id": actor_id,
        "name": name,
        "ids": coerce_string_array(actor_def["ids"]),
        "type": "npc",
        "type_label": actor_def["type_label"] || "人物",
        "summary": summary,
        "description": description,
    ]);
}

private mapping attach_projection_actor_actions(object player, object room,
    mapping actor_def, mapping payload)
{
    mapping live_payload;
    object target;
    string target_id;

    if (!objectp(player) || !objectp(room) || !mapp(actor_def) || !mapp(payload))
        return payload;
    target_id = actor_def["target_present_id"] || "";
    if (!stringp(target_id) || target_id == "")
        return payload;
    target = present(target_id, room);
    if (!objectp(target))
        return payload;
    live_payload = CLIENT_PROJECTION_D->build_room_npc_payload(target, player);
    if (mapp(live_payload) && arrayp(live_payload["action_groups"]))
        payload["action_groups"] = live_payload["action_groups"];
    if (mapp(live_payload) && live_payload["quest_available"])
        payload["quest_available"] = 1;
    if (mapp(live_payload) && arrayp(live_payload["available_quest_ids"]))
        payload["available_quest_ids"] = live_payload["available_quest_ids"];
    return payload;
}

private int projection_actor_matches_room(object room, mapping actor_def,
    string tracked_room)
{
    string room_path;

    if (!objectp(room) || !mapp(actor_def))
        return 0;
    room_path = base_name(room);
    switch (actor_def["type"] || "fixed_room_actor") {
        case "tracked_room_actor":
            return stringp(tracked_room) && tracked_room != "" && tracked_room == room_path;
        case "fixed_room_actor":
            return stringp(actor_def["room"]) && actor_def["room"] == room_path;
    }
    return 0;
}

private mapping query_projection_payload_for_def(object player, object room,
    string actor_id, mapping actor_def, string projection_scope_key)
{
    string tracked_room;
    string visible_flag;

    if (!objectp(player) || !objectp(room) || !mapp(actor_def))
        return 0;
    visible_flag = actor_def["visible_flag"] || "";
    if (visible_flag != "") {
        if (!stringp(projection_scope_key) || projection_scope_key == "")
            return 0;
        if (!WORLD_STATE_D->query_scope_flag(projection_scope_key, visible_flag))
            return 0;
    }
    tracked_room = "";
    if (stringp(actor_def["tracked_room_flag"]) && actor_def["tracked_room_flag"] != ""
        && stringp(projection_scope_key) && projection_scope_key != "")
        tracked_room = WORLD_STATE_D->query_scope_flag(projection_scope_key,
        actor_def["tracked_room_flag"]) || "";
    if (!projection_actor_matches_room(room, actor_def, tracked_room))
        return 0;
    return attach_projection_actor_actions(player, room, actor_def,
        build_projection_actor_payload(actor_id, actor_def));
}

private varargs mapping *query_projection_payloads(object player, object room,
    string projection_scope_key)
{
    mapping *payload_buffer;
    mapping *payloads;
    mapping actor_def;
    mapping payload;
    string actor_id;
    int payload_count;

    if (!stringp(projection_scope_key) || projection_scope_key == "")
        projection_scope_key = query_projection_scope_key(player, room);
    payload_buffer = allocate(sizeof(_projection_actors || ([ ])));
    foreach (actor_id, actor_def in _projection_actors || ([ ])) {
        payload = query_projection_payload_for_def(player, room, actor_id, actor_def,
            projection_scope_key);
        if (mapp(payload))
            payload_buffer[payload_count++] = payload;
    }
    payloads = payload_count > 0
        ? payload_buffer[0..payload_count - 1]
        : ({ });
    return payloads;
}

private int hide_rule_matches_npc(object npc, object room, mapping rule)
{
    string name;
    string room_path;

    if (!objectp(npc) || !objectp(room) || !mapp(rule))
        return 0;
    room_path = base_name(room);
    if (!stringp(rule["room"]) || rule["room"] != room_path)
        return 0;
    if (stringp(rule["match_present_id"]) && rule["match_present_id"] != ""
        && present(rule["match_present_id"], room) == npc)
        return 1;
    name = npc->query_name() || npc->query("name") || "";
    return member_array(name, rule["match_names"] || ({ })) != -1;
}

private string *query_payload_ids(mapping payload)
{
    mixed ids;

    ids = payload["ids"];
    if (arrayp(ids))
        return ids;
    return ({ payload["id"] });
}

mapping query_projection_registry_snapshot()
{
    return ([
        "config_name": _projection_status["config_name"] || "world_projection",
        "config_version": intp(_projection_status["config_version"])
            ? _projection_status["config_version"] : 0,
        "last_attempt_at": intp(_projection_status["last_attempt_at"])
            ? _projection_status["last_attempt_at"] : 0,
        "last_reload_at": intp(_projection_status["last_reload_at"])
            ? _projection_status["last_reload_at"] : 0,
        "last_reload_ok": _projection_status["last_reload_ok"] ? 1 : 0,
        "last_error": stringp(_projection_status["last_error"])
            ? _projection_status["last_error"] : "",
        "actor_count": sizeof(_projection_actors || ([ ])),
        "hide_rule_count": sizeof(_projection_hide_rules || ([ ])),
        "player_refresh_count": WORLD_OBSERVER_D->query_player_refresh_count(),
        "content_entity_prune_count": WORLD_ENTITY_LIFECYCLE_D->query_content_entity_prune_count(),
        "actor_ids": sort_array(keys(_projection_actors || ([ ])), 1),
        "hide_rule_ids": sort_array(keys(_projection_hide_rules || ([ ])), 1),
        "actors": copy(_projection_actors || ([ ])),
        "hide_rules": copy(_projection_hide_rules || ([ ])),
    ]);
}

mapping query_player_projection_snapshot(object player, object room)
{
    string scope_key;

    if (!objectp(player) || !objectp(room))
        return ([ ]);
    scope_key = query_projection_scope_key(player, room);
    return ([
        "player_id": PLAYER_STATE_D->query_player_id(player),
        "room": base_name(room),
        "scope_key": scope_key || "",
        "actors": query_projection_payloads(player, room, scope_key),
        "state": stringp(scope_key) && scope_key != ""
            ? WORLD_STATE_D->query_scope_state(scope_key)
            : ([ ]),
    ]);
}

mapping *query_room_actor_payloads(object player, object room)
{
    return query_projection_payloads(player, room);
}

private int should_hide_shared_npc_in_scope(object npc, object room,
    string projection_scope_key)
{
    mapping rule;
    string rule_id;

    if (!objectp(npc) || !objectp(room))
        return 0;
    if (!stringp(projection_scope_key) || projection_scope_key == "")
        return 0;
    foreach (rule_id, rule in _projection_hide_rules || ([ ])) {
        if (!WORLD_STATE_D->query_scope_flag(projection_scope_key, rule["hide_flag"]))
            continue;
        if (hide_rule_matches_npc(npc, room, rule))
            return 1;
    }
    return 0;
}

int should_hide_shared_npc(object player, object npc, object room)
{
    if (!objectp(player) || !objectp(npc) || !objectp(room))
        return 0;
    return should_hide_shared_npc_in_scope(npc, room,
        query_projection_scope_key(player, room));
}

mixed *query_room_npc_view(object player, object room, object *shared_npcs)
{
    mapping payload;
    mapping *payloads;
    object *visible_npcs;
    string projection_scope_key;

    visible_npcs = filter(shared_npcs || ({ }), (: objectp($1) :));
    if (sizeof(_projection_hide_rules || ([ ])) == 0
        && sizeof(_projection_actors || ([ ])) == 0)
        return visible_npcs;

    projection_scope_key = query_projection_scope_key(player, room);
    if (sizeof(_projection_hide_rules || ([ ])) > 0) {
        visible_npcs = filter(visible_npcs,
            (: !should_hide_shared_npc_in_scope($1, $(room),
                $(projection_scope_key)) :));
    }
    if (sizeof(_projection_actors || ([ ])) == 0)
        return visible_npcs;

    payloads = query_projection_payloads(player, room, projection_scope_key);
    if (!arrayp(payloads) || sizeof(payloads) == 0)
        return visible_npcs;

    foreach (payload in payloads) {
        if (!mapp(payload))
            return visible_npcs;
    }

    if (sizeof(visible_npcs) == 0)
        return payloads;
    return visible_npcs + payloads;
}

object resolve_actor_target(object player, object room, string arg)
{
    mapping actor_def;
    mapping payload;
    string *ids;
    string actor_id;

    if (!objectp(player) || !objectp(room) || !stringp(arg) || arg == "")
        return 0;
    foreach (payload in query_projection_payloads(player, room)) {
        ids = query_payload_ids(payload);
        if (member_array(arg, ids) == -1)
            continue;
        actor_id = payload["projection_id"] || payload["id"] || "";
        actor_def = _projection_actors[actor_id];
        if (!mapp(actor_def) || !stringp(actor_def["target_present_id"]))
            return 0;
        return present(actor_def["target_present_id"], room);
    }
    return 0;
}
