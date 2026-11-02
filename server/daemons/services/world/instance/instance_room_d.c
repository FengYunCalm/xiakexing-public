/**
 * @file    instance_room_d.c
 * @brief   副本与私人剧情共用的实例房间克隆和回收真源。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

#define MAX_ACTIVE_INSTANCES 300
#define MAX_ROOMS_PER_INSTANCE 16
#define ROOM_IDLE_SECONDS 120
#define INSTANCE_IDLE_SECONDS 900

nosave mapping _instances;
nosave mapping _health_status;

private int valid_instance_id(string instance_id)
{
    int ch;
    int index;

    if (!stringp(instance_id) || strlen(instance_id) < 1
        || strlen(instance_id) > 160)
        return 0;
    for (index = 0; index < strlen(instance_id); index++) {
        ch = instance_id[index];
        if ((ch >= 'a' && ch <= 'z')
            || (ch >= 'A' && ch <= 'Z')
            || (ch >= '0' && ch <= '9')
            || ch == '.' || ch == '_' || ch == '-' || ch == ':')
            continue;
        return 0;
    }
    return 1;
}

private int path_is_under_world_root(string template_path, string root)
{
    if (!stringp(root) || root == "" || strsrch(template_path, root) != 0)
        return 0;
    if (template_path == root || root[strlen(root) - 1] == '/')
        return 1;
    return strlen(template_path) > strlen(root)
        && template_path[strlen(root)] == '/';
}

private int is_world_template_path(string template_path)
{
    string root;
    string *roots;

    if (!stringp(template_path) || template_path == ""
        || template_path[0] != '/' || strsrch(template_path, "..") != -1
        || strsrch(template_path, "#") != -1)
        return 0;
    roots = CONFIG_D->query_world_roots();
    foreach (root in roots) {
        if (path_is_under_world_root(template_path, root))
            return 1;
    }
    return 0;
}

private int count_active_instances()
{
    mapping state;
    int count;

    foreach (state in values(_instances || ([]))) {
        if (mapp(state))
            count++;
    }
    return count;
}

private mapping ensure_instance_state(string instance_id, mapping context)
{
    mapping state;

    state = _instances[instance_id];
    if (mapp(state)) {
        state["last_active_at"] = time();
        return state;
    }
    if (count_active_instances() >= MAX_ACTIVE_INSTANCES) {
        _health_status["capacity_rejections"]++;
        return 0;
    }
    state = ([
        "instance_id": instance_id,
        "context": mapp(context) ? copy(context) : ([]),
        "rooms": ([]),
        "room_touched_at": ([]),
        "created_at": time(),
        "last_active_at": time(),
    ]);
    _instances[instance_id] = state;
    _health_status["instances_created"]++;
    return state;
}

private int room_has_active_occupant(object room)
{
    object occupant;

    if (!objectp(room))
        return 0;
    foreach (occupant in all_inventory(room)) {
        if (!objectp(occupant))
            continue;
        if (userp(occupant) || interactive(occupant))
            return 1;
        if ((living(occupant)
            || (function_exists("is_npc", occupant)
            && call_other(occupant, "is_npc")))
            && function_exists("is_fighting", occupant)
            && call_other(occupant, "is_fighting"))
            return 1;
    }
    return 0;
}

private int destroy_room_if_idle(object room)
{
    object occupant;

    if (!objectp(room))
        return 1;
    if (room_has_active_occupant(room))
        return 0;
    foreach (occupant in all_inventory(room)) {
        if (objectp(occupant))
            destruct(occupant);
    }
    destruct(room);
    return 1;
}

private int evict_oldest_idle_room(mapping state)
{
    object room;
    string candidate_path;
    string template_path;
    int candidate_touched_at;
    int touched_at;

    if (!mapp(state) || !mapp(state["rooms"])
        || !mapp(state["room_touched_at"]))
        return 0;
    candidate_path = "";
    candidate_touched_at = 0;
    foreach (template_path, room in state["rooms"]) {
        if (!objectp(room)) {
            candidate_path = template_path;
            break;
        }
        if (room_has_active_occupant(room))
            continue;
        touched_at = state["room_touched_at"][template_path] || 0;
        if (candidate_path == "" || touched_at < candidate_touched_at) {
            candidate_path = template_path;
            candidate_touched_at = touched_at;
        }
    }
    if (candidate_path == "")
        return 0;
    room = state["rooms"][candidate_path];
    if (objectp(room) && !destroy_room_if_idle(room))
        return 0;
    map_delete(state["rooms"], candidate_path);
    map_delete(state["room_touched_at"], candidate_path);
    _health_status["rooms_reclaimed"]++;
    _health_status["capacity_room_evictions"]++;
    return 1;
}

object resolve_instance_room(string instance_id, string template_path, mapping context)
{
    mapping state;
    object room;
    mixed err;

    if (!valid_instance_id(instance_id) || !is_world_template_path(template_path))
        return 0;
    state = ensure_instance_state(instance_id, context);
    if (!mapp(state))
        return 0;
    room = state["rooms"][template_path];
    if (objectp(room)) {
        state["room_touched_at"][template_path] = time();
        return room;
    }
    if (sizeof(state["rooms"]) >= MAX_ROOMS_PER_INSTANCE
        && !evict_oldest_idle_room(state)) {
        _health_status["room_capacity_rejections"]++;
        return 0;
    }

    err = catch(room = clone_object(template_path));
    if (err || !objectp(room)) {
        _health_status["clone_failures"]++;
        _health_status["last_error"] = sprintf("instance clone failed: %O", err);
        return 0;
    }
    room->set("instance/runtime", 1);
    room->set("instance/id", instance_id);
    room->set("instance/template_path", template_path);
    room->set("instance/context", copy(state["context"]));
    if (state["context"]["kind"] == "dungeon") {
        room->set("dungeon/runtime", 1);
        room->set("dungeon/id", state["context"]["dungeon_id"] || "");
        room->set("dungeon/type", state["context"]["dungeon_type"] || "");
        room->set("dungeon/owner_id", state["context"]["owner_id"] || "");
        room->set("dungeon/state_scope", state["context"]["state_scope"] || "");
        room->set("dungeon/instance_scope",
            state["context"]["instance_scope"] || "");
        room->set("dungeon/template_path", template_path);
    }
    state["rooms"][template_path] = room;
    state["room_touched_at"][template_path] = time();
    state["last_active_at"] = time();
    _health_status["rooms_created"]++;
    return room;
}

string query_template_room_path(mixed room_or_path)
{
    string template_path;

    if (objectp(room_or_path)) {
        template_path = room_or_path->query("instance/template_path") || "";
        if (template_path == "")
            template_path = base_name(room_or_path);
        return template_path;
    }
    if (!stringp(room_or_path))
        return "";
    if (strsrch(room_or_path, "#") != -1)
        return "";
    return room_or_path;
}

string resolve_instance_exit(string instance_id, string template_path, string direction)
{
    mapping context;
    mapping exits;
    mapping state;
    object room;
    string destination;

    state = _instances[instance_id];
    if (!mapp(state) || !stringp(direction) || direction == "")
        return "";
    room = resolve_instance_room(instance_id, template_path, state["context"]);
    if (!objectp(room))
        return "";
    exits = room->query("exits") || ([]);
    destination = exits[direction];
    if (!stringp(destination) || destination == "")
        return "";
    if (strsrch(destination, "#") != -1)
        return destination;
    context = state["context"];
    room = resolve_instance_room(instance_id, destination, context);
    return objectp(room) ? file_name(room) : destination;
}

int rewrite_instance_exits(string instance_id)
{
    mapping exits;
    mapping rewritten;
    mapping state;
    object room;
    string destination;
    string direction;
    string template_path;

    state = _instances[instance_id];
    if (!mapp(state))
        return 0;
    foreach (template_path, room in state["rooms"]) {
        if (!objectp(room))
            return 0;
        exits = room->query("exits") || ([]);
        rewritten = ([]);
        foreach (direction, destination in exits) {
            if (objectp(state["rooms"][destination]))
                rewritten[direction] = file_name(state["rooms"][destination]);
            else
                rewritten[direction] = destination;
        }
        room->set("exits", rewritten);
    }
    return 1;
}

object query_instance_room(string instance_id, string template_path)
{
    mapping state;

    state = _instances[instance_id];
    if (!mapp(state) || !mapp(state["rooms"]))
        return 0;
    return state["rooms"][template_path];
}

mapping query_instance_context(string instance_id)
{
    mapping state;

    state = _instances[instance_id];
    return mapp(state) ? copy(state["context"] || ([])) : ([]);
}

mapping query_instance_registry_snapshot()
{
    mapping context;
    mapping registry;
    mapping state;
    string instance_id;

    registry = ([]);
    foreach (instance_id, state in _instances || ([])) {
        if (!mapp(state))
            continue;
        context = mapp(state["context"]) ? copy(state["context"]) : ([]);
        registry[instance_id] = ([
            "instance_id": instance_id,
            "kind": context["kind"] || "",
            "owner_id": context["owner_id"] || "",
            "state_scope": context["state_scope"] || "",
            "instance_scope": context["instance_scope"] || "",
            "template_rooms": keys(state["rooms"] || ([])),
            "created_at": state["created_at"] || 0,
            "last_active_at": state["last_active_at"] || 0,
        ]);
    }
    return ([
        "ok": 1,
        "count": sizeof(registry),
        "instances": registry,
    ]);
}

int release_instance(string instance_id)
{
    mapping state;
    object room;

    state = _instances[instance_id];
    if (!mapp(state))
        return 1;
    foreach (room in values(state["rooms"] || ([]))) {
        if (room_has_active_occupant(room)) {
            _health_status["reclaims_deferred"]++;
            return 0;
        }
    }
    foreach (room in values(state["rooms"] || ([])))
        destroy_room_if_idle(room);
    map_delete(_instances, instance_id);
    _health_status["instances_reclaimed"]++;
    return 1;
}

mapping reclaim_idle_instances()
{
    mapping state;
    object room;
    string instance_id;
    string template_path;
    string *instance_ids;
    string *template_paths;
    int reclaimed_instances;
    int reclaimed_rooms;

    instance_ids = keys(copy(_instances || ([])));
    foreach (instance_id in instance_ids) {
        state = _instances[instance_id];
        if (!mapp(state))
            continue;
        template_paths = keys(copy(state["rooms"] || ([])));
        foreach (template_path in template_paths) {
            room = state["rooms"][template_path];
            if (time() - (state["room_touched_at"][template_path] || time())
                    < ROOM_IDLE_SECONDS
                || !destroy_room_if_idle(room))
                continue;
            map_delete(state["rooms"], template_path);
            map_delete(state["room_touched_at"], template_path);
            reclaimed_rooms++;
        }
        if (sizeof(state["rooms"]) == 0
            && time() - (state["last_active_at"] || time()) >= INSTANCE_IDLE_SECONDS
            && release_instance(instance_id))
            reclaimed_instances++;
    }
    _health_status["rooms_reclaimed"] += reclaimed_rooms;
    call_out("reclaim_idle_instances", ROOM_IDLE_SECONDS);
    return ([
        "ok": 1,
        "rooms_reclaimed": reclaimed_rooms,
        "instances_reclaimed": reclaimed_instances,
    ]);
}

mapping query_instance_health_snapshot()
{
    mapping snapshot;
    mapping state;
    int room_count;

    foreach (state in values(_instances || ([]))) {
        if (mapp(state) && mapp(state["rooms"]))
            room_count += sizeof(state["rooms"]);
    }
    snapshot = copy(_health_status || ([]));
    snapshot["status"] = snapshot["last_error"] == "" ? "ok" : "degraded";
    snapshot["active_instances"] = sizeof(_instances || ([]));
    snapshot["active_rooms"] = room_count;
    snapshot["limits"] = ([
        "max_active_instances": MAX_ACTIVE_INSTANCES,
        "max_rooms_per_instance": MAX_ROOMS_PER_INSTANCE,
        "room_idle_seconds": ROOM_IDLE_SECONDS,
        "instance_idle_seconds": INSTANCE_IDLE_SECONDS,
    ]);
    return snapshot;
}

void create()
{
    ::create();
    _instances = ([]);
    _health_status = ([
        "instances_created": 0,
        "rooms_created": 0,
        "instances_reclaimed": 0,
        "rooms_reclaimed": 0,
        "capacity_room_evictions": 0,
        "reclaims_deferred": 0,
        "capacity_rejections": 0,
        "room_capacity_rejections": 0,
        "clone_failures": 0,
        "last_error": "",
    ]);
    call_out("reclaim_idle_instances", ROOM_IDLE_SECONDS);
}
