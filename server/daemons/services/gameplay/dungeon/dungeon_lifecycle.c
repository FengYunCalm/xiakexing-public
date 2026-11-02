/**
 * @file    dungeon_lifecycle.c
 * @brief   副本创建、完成、失败、离开和销毁模块。
 */

#pragma strict_types
#pragma save_types

#include <system/constants.h>
#include <system/services.h>
#include <framework/message_facade.h>

#define DUNGEON_STORE query_dungeon_runtime_store()

mapping query_dungeon_runtime_store();
mapping query_dungeon_definition_for_actor(object actor, string dungeon_type);
int next_dungeon_counter();
string ensure_player_id(object player);
object find_online_player(string player_id);
string find_player_dungeon_id_by_player_id(string player_id);
string find_player_dungeon_id(object player);
mapping get_player_dungeon(object player);
mapping build_member_states(string *player_ids);
void sync_dungeon_mode(mapping dungeon);
void remove_player_from_runtime(mapping dungeon, string player_id);
object resolve_exit_room(mapping dungeon);
int move_member_to_exit(mapping dungeon, string player_id, string message);
int clear_player_resume(string player_id);
int save_player_resume(mapping dungeon, string player_id, string template_path);
void track_player_template(object player, mapping dungeon, object room);
int all_members_claimed_treasury(mapping dungeon);
int all_members_wiped(mapping dungeon);
void close_dungeon_run(string dungeon_id, string reason, string message);
int remove_member_from_dungeon(string dungeon_id, string player_id, string reason, int apply_cooldown);
int apply_guild_contribution_reward(object player, mapping def);
void refresh_dungeon_timeout(string dungeon_id);
mapping build_command_result(int success, string message, mapping extra);
int complete_dungeon(string dungeon_id);
varargs int fail_dungeon(string dungeon_id, string reason);
int set_player_cooldown(string player_id, string dungeon_type, int cooldown);
int should_apply_dungeon_cooldown(mapping dungeon);
int query_dungeon_cooldown_seconds(mapping dungeon);
void schedule_failed_dungeon_cleanup(string dungeon_id);
void destroy_dungeon(string dungeon_id);
int cleanup_dungeon_instance_rooms(string dungeon_id);

int create_dungeon(string dungeon_type, object *players, int difficulty)
{
    string dungeon_id;
    object player;
    string player_id;
    string *player_ids;
    mapping def;
    mapping store;
    int duration;
    int max_players;

    if (!stringp(dungeon_type) || dungeon_type == "" || !arrayp(players))
        return 0;

    def = query_dungeon_definition_for_actor(players[0], dungeon_type);
    if (!mapp(def))
        return 0;

    max_players = intp(def["max_players"])
        ? def["max_players"] : MAX_TEAM_SIZE;
    if (sizeof(players) == 0 || sizeof(players) > max_players)
        return 0;

    player_ids = ({ });
    foreach (player in players) {
        player_id = ensure_player_id(player);
        if (!stringp(player_id) || player_id == ""
            || member_array(player_id, player_ids) != -1)
            return 0;
        player_ids += ({ player_id });
    }
    if (!sizeof(player_ids) || sizeof(player_ids) > max_players)
        return 0;

    duration = intp(def["duration"]) && def["duration"] > 0 ? def["duration"] : 600;

    dungeon_id = sprintf("%s_%d_%d", dungeon_type, time(), next_dungeon_counter());
    store = query_dungeon_runtime_store();
    store[dungeon_id] = ([
        "id": dungeon_id,
        "type": dungeon_type,
        "name": def["name"] || dungeon_type,
        "entry_room": def["entry_room"] || "",
        "exit_room": def["exit_room"] || "",
        "players": player_ids,
        "leader": player_ids[0],
        "owner_id": player_ids[0],
        "state_scope": def["state_scope"] || "world",
        "instance_scope": def["instance_scope"] || "shared",
        "instance_id": "dungeon:" + dungeon_id,
        "mode": sizeof(player_ids) > 1 ? "team" : "solo",
        "difficulty": difficulty || 1,
        "cooldown_enabled": DUNGEON_REGISTRY_D->query_dungeon_cooldown_enabled(dungeon_type),
        "cooldown_seconds": DUNGEON_REGISTRY_D->query_dungeon_cooldown_seconds(dungeon_type),
        "state": "active",
        "expire_time": time() + duration,
        "kills": 0,
        "deaths": 0,
        "stage": 1,
        "boss_defeated": 0,
        "member_states": build_member_states(player_ids),
        "member_room_templates": ([]),
        "treasury_claimed": ([]),
    ]);
    return 1;
}

int abort_dungeon_entry(string dungeon_id, object *entered_players)
{
    mapping dungeon;
    object player;
    string player_id;
    string *player_ids;
    int all_moved;

    dungeon = DUNGEON_STORE[dungeon_id];
    if (!mapp(dungeon))
        return 0;

    all_moved = 1;
    foreach (player in entered_players || ({ })) {
        player_id = ensure_player_id(player);
        if (!stringp(player_id) || player_id == "")
            continue;
        if (!move_member_to_exit(dungeon, player_id,
            "副本进入失败，你已返回入口。\n"))
            all_moved = 0;
    }
    if (!all_moved) {
        fail_dungeon(dungeon_id, "entry_rollback_failed");
        return 0;
    }

    player_ids = (dungeon["players"] || ({ })) + ({ });
    foreach (player_id in player_ids)
        clear_player_resume(player_id);
    destroy_dungeon(dungeon_id);
    return 1;
}

int retry_pending_leave(object player)
{
    mapping dungeon;
    mapping pending;
    mapping moved;
    object origin_room;
    string dungeon_id;
    string player_id;
    string origin_room_path;

    if (!objectp(player))
        return 0;
    player_id = ensure_player_id(player);
    dungeon_id = find_player_dungeon_id_by_player_id(player_id);
    if (!stringp(dungeon_id) || dungeon_id == "")
        return 1;
    dungeon = DUNGEON_STORE[dungeon_id];
    pending = mapp(dungeon) && mapp(dungeon["leave_pending"])
        ? dungeon["leave_pending"][player_id] : 0;
    if (!mapp(pending))
        return 1;

    origin_room = pending["origin_room_object"];
    origin_room_path = pending["origin_room_path"] || "";
    if (!objectp(origin_room) && origin_room_path != "")
        origin_room = DUNGEON_INSTANCE_D->query_instance_room(dungeon_id,
            origin_room_path);
    moved = objectp(origin_room)
        ? WORLD_TRAVEL_D->move_actor(player, origin_room, ([
            "reason": "dungeon_leave_retry",
        ]))
        : 0;
    if (!mapp(moved) || !moved["ok"]) {
        pending["attempts"] = (pending["attempts"] || 0) + 1;
        pending["last_error"] = "rollback_move_failed";
        pending["updated_at"] = time();
        return 0;
    }

    map_delete(dungeon["leave_pending"], player_id);
    return 1;
}

int leave_dungeon(object player)
{
    mapping dungeon;
    mapping moved;
    mapping rollback;
    object origin_room;
    string player_id;
    string dungeon_id;
    string exit_room_path;
    string origin_room_path;

    if (!objectp(player))
        return 0;

    player_id = ensure_player_id(player);
    dungeon_id = find_player_dungeon_id_by_player_id(player_id);
    if (!stringp(dungeon_id) || dungeon_id == "")
        return 0;
    dungeon = DUNGEON_STORE[dungeon_id];
    if (!mapp(dungeon))
        return 0;

    origin_room = environment(player);
    origin_room_path = objectp(origin_room) ? base_name(origin_room) : "";
    exit_room_path = dungeon["exit_room"];
    if (!stringp(exit_room_path) || exit_room_path == "")
        exit_room_path = CONFIG_D->query_start_room();
    moved = WORLD_TRAVEL_D->move_actor(player, exit_room_path, ([
        "reason": "dungeon_leave",
    ]));
    if (!mapp(moved) || !moved["ok"])
        return 0;

    if (remove_member_from_dungeon(dungeon_id, player_id, "left", 1))
        return 1;

    rollback = objectp(origin_room)
        ? WORLD_TRAVEL_D->move_actor(player, origin_room, ([
            "reason": "dungeon_leave_rollback",
        ]))
        : 0;
    if (mapp(rollback) && rollback["ok"])
        return 0;

    if (!mapp(dungeon["leave_pending"]))
        dungeon["leave_pending"] = ([]);
    dungeon["leave_pending"][player_id] = ([
        "player_id": player_id,
        "dungeon_id": dungeon_id,
        "origin_room_path": origin_room_path,
        "origin_room_object": origin_room,
        "exit_room_path": exit_room_path,
        "attempts": 1,
        "state": "position_restore_pending",
        "created_at": time(),
        "updated_at": time(),
    ]);
    return 0;
}

int complete_dungeon(string dungeon_id)
{
    mapping dungeon;

    dungeon = DUNGEON_STORE[dungeon_id];
    if (!mapp(dungeon) || dungeon["state"] != "active")
        return 0;

    dungeon["state"] = "completed";
    dungeon["complete_time"] = time();
    return 1;
}

varargs int fail_dungeon(string dungeon_id, string reason)
{
    mapping dungeon;
    mapping cooldown_pending;
    int cooldown_result;
    int cooldown_seconds;
    string player_id;

    dungeon = DUNGEON_STORE[dungeon_id];
    if (!mapp(dungeon) || dungeon["state"] != "active")
        return 0;

    dungeon["state"] = "failed";
    dungeon["fail_reason"] = stringp(reason) && reason != "" ? reason : "failed";
    dungeon["fail_time"] = time();
    if (should_apply_dungeon_cooldown(dungeon)) {
        cooldown_seconds = query_dungeon_cooldown_seconds(dungeon);
        cooldown_pending = dungeon["cooldown_pending"];
        if (!mapp(cooldown_pending))
            cooldown_pending = dungeon["cooldown_pending"] = ([]);
        foreach (player_id in dungeon["players"] || ({ })) {
            cooldown_result = set_player_cooldown(player_id, dungeon["type"],
                cooldown_seconds);
            if (cooldown_result != 1)
                cooldown_pending[player_id] = ([
                    "player_id": player_id,
                    "dungeon_type": dungeon["type"],
                    "cooldown": cooldown_seconds,
                    "expires_at": time() + cooldown_seconds,
                    "save_result": cooldown_result,
                    "updated_at": time(),
                ]);
        }
    }
    schedule_failed_dungeon_cleanup(dungeon_id);

    return 1;
}

int mark_boss_defeated(object player, string dungeon_type)
{
    string player_id;
    string dungeon_id;
    mapping dungeon;

    if (!objectp(player) || !stringp(dungeon_type) || dungeon_type == "")
        return 0;

    player_id = ensure_player_id(player);
    if (!stringp(player_id) || player_id == "")
        return 0;

    foreach (dungeon_id, dungeon in DUNGEON_STORE) {
        if (!mapp(dungeon) || dungeon["state"] != "active")
            continue;
        if (dungeon["type"] != dungeon_type)
            continue;
        if (member_array(player_id, dungeon["players"] || ({ })) == -1)
            continue;

        dungeon["boss_defeated"] = 1;
        dungeon["kills"] = (dungeon["kills"] || 0) + 1;
        if ((dungeon["stage"] || 0) < 2)
            dungeon["stage"] = 2;
        return 1;
    }

    return 0;
}


void destroy_dungeon(string dungeon_id)
{
    mapping store;

    if (!stringp(dungeon_id) || dungeon_id == "")
        return;

    store = query_dungeon_runtime_store();
    cleanup_dungeon_instance_rooms(dungeon_id);
    map_delete(store, dungeon_id);
}

int cleanup_dungeon_instance_rooms(string dungeon_id)
{
    int timer_handle;

    if (!stringp(dungeon_id) || dungeon_id == "")
        return 1;

    if (DUNGEON_INSTANCE_D->destroy_instance_rooms(dungeon_id))
        return 1;
    timer_handle = TIMER_D->call_later("cleanup_dungeon_instance_rooms", 1,
        this_object(), dungeon_id);
    if (!intp(timer_handle) || timer_handle <= 0)
        timer_handle = call_out("cleanup_dungeon_instance_rooms", 1, dungeon_id);
    if (!intp(timer_handle) || timer_handle <= 0)
        log_file("system/dungeon", sprintf("cleanup timer unavailable for %s\n", dungeon_id));
    return 0;
}
