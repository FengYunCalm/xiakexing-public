/**
 * @file    dungeon_runtime_common.c
 * @brief   副本运行时公共 helper、成员账本工具和关闭编排。
 */

#pragma strict_types
#pragma save_types

#include <game_constants.h>
#include <system/services.h>
#include <framework/message_facade.h>

#define DUNGEON_STORE query_dungeon_runtime_store()

mapping query_dungeon_runtime_store();
int next_dungeon_counter();
int complete_dungeon(string dungeon_id);
varargs int fail_dungeon(string dungeon_id, string reason);
int set_player_cooldown(string player_id, string dungeon_type, int cooldown);
int query_dungeon_cooldown_seconds(mapping dungeon);
void schedule_failed_dungeon_cleanup(string dungeon_id);
void destroy_dungeon(string dungeon_id);
int cleanup_dungeon_instance_rooms(string dungeon_id);

string ensure_player_id(object player)
{
    string pid;

    if (!objectp(player))
        return 0;

    pid = get_object_id(player);
    if (!stringp(pid) || pid == "")
        return 0;

    return pid;
}

object find_online_player(string player_id)
{
    if (!stringp(player_id) || player_id == "")
        return 0;

    return "/lib/framework/daemon"->find_player_online(player_id);
}

string find_player_dungeon_id_by_player_id(string player_id)
{
    string dungeon_id;
    mapping dungeon;

    if (!stringp(player_id) || player_id == "")
        return 0;

    foreach (dungeon_id, dungeon in DUNGEON_STORE) {
        if (mapp(dungeon) && member_array(player_id, dungeon["players"] || ({ })) != -1)
            return dungeon_id;
    }

    return 0;
}

string find_player_dungeon_id(object player)
{
    return find_player_dungeon_id_by_player_id(ensure_player_id(player));
}

mapping build_member_states(string *player_ids)
{
    mapping out;
    string player_id;

    out = ([]);
    if (!arrayp(player_ids))
        return out;

    foreach (player_id in player_ids)
        out[player_id] = "active";

    return out;
}

void sync_dungeon_mode(mapping dungeon)
{
    string *players;

    if (!mapp(dungeon))
        return;

    players = dungeon["players"] || ({ });
    if (!arrayp(players))
        players = ({ });

    dungeon["mode"] = sizeof(players) > 1 ? "team" : "solo";
    if (!sizeof(players))
        dungeon["leader"] = "";
    else if (member_array(dungeon["leader"], players) == -1)
        dungeon["leader"] = players[0];
}

private string *filter_player_ids(string *player_ids, string removed_id)
{
    string candidate;
    string *filtered;

    filtered = ({ });
    foreach (candidate in player_ids) {
        if (candidate != removed_id)
            filtered += ({ candidate });
    }
    return filtered;
}

void remove_player_from_runtime(mapping dungeon, string player_id)
{
    string *player_ids;

    if (!mapp(dungeon) || !stringp(player_id) || player_id == "")
        return;

    player_ids = arrayp(dungeon["players"])
        ? dungeon["players"] : ({ });
    dungeon["players"] = filter_player_ids(player_ids, player_id);

    if (mapp(dungeon["member_room_templates"]))
        map_delete(dungeon["member_room_templates"], player_id);

    sync_dungeon_mode(dungeon);
}

object resolve_exit_room(mapping dungeon)
{
    object room;
    string room_path;

    if (!mapp(dungeon))
        return 0;

    room_path = dungeon["exit_room"];
    if (!stringp(room_path) || room_path == "")
        room_path = CONFIG_D->query_start_room();

    return WORLD_TRAVEL_D->resolve_public_room(room_path, ([
        "reason": "dungeon_exit",
    ]));
}

int move_member_to_exit(mapping dungeon, string player_id, string message)
{
    mapping moved;
    object player;
    object exit_room;

    if (!mapp(dungeon) || !stringp(player_id) || player_id == "")
        return 0;

    player = find_online_player(player_id);
    if (!objectp(player))
        return 1;
    exit_room = resolve_exit_room(dungeon);
    moved = objectp(exit_room) ? WORLD_TRAVEL_D->move_actor(player, exit_room, ([
        "reason": "dungeon_exit",
    ])) : 0;
    if (!mapp(moved) || !moved["ok"])
        return 0;
    if (stringp(message) && message != "")
        msg_send(player, message);
    return 1;
}

int clear_player_resume(string player_id)
{
    if (!stringp(player_id) || player_id == "")
        return 0;

    return DUNGEON_PLAYER_D->clear_resume_context(player_id);
}

int save_player_resume(mapping dungeon, string player_id, string template_path)
{
    if (!mapp(dungeon) || !stringp(player_id) || player_id == "")
        return 0;

    return DUNGEON_PLAYER_D->set_resume_context(player_id, ([
        "dungeon_id": dungeon["id"] || "",
        "template_path": stringp(template_path) ? template_path : "",
        "timestamp": time(),
    ]));
}

int should_apply_dungeon_cooldown(mapping dungeon)
{
    if (!mapp(dungeon))
        return 0;
    if (!undefinedp(dungeon["cooldown_enabled"])
        && intp(dungeon["cooldown_enabled"]))
        return dungeon["cooldown_enabled"] ? 1 : 0;
    return DUNGEON_REGISTRY_D->query_dungeon_cooldown_enabled(dungeon["type"]);
}

int query_dungeon_cooldown_seconds(mapping dungeon)
{
    if (!mapp(dungeon))
        return 0;
    if (!undefinedp(dungeon["cooldown_seconds"])
        && intp(dungeon["cooldown_seconds"])
        && dungeon["cooldown_seconds"] >= 0)
        return dungeon["cooldown_seconds"];
    return DUNGEON_REGISTRY_D->query_dungeon_cooldown_seconds(dungeon["type"]);
}

void track_player_template(object player, mapping dungeon, object room)
{
    string player_id;
    string template_path;

    if (!objectp(player) || !mapp(dungeon) || !objectp(room))
        return;

    player_id = ensure_player_id(player);
    if (!stringp(player_id) || player_id == "")
        return;

    template_path = room->query("dungeon/template_path");
    if (!stringp(template_path) || template_path == "")
        return;
    if (!mapp(dungeon["member_room_templates"]))
        dungeon["member_room_templates"] = ([]);

    dungeon["member_room_templates"][player_id] = template_path;
}

int all_members_claimed_treasury(mapping dungeon)
{
    mapping claimed;
    string player_id;

    if (!mapp(dungeon) || !arrayp(dungeon["players"]) || !sizeof(dungeon["players"]))
        return 0;

    claimed = dungeon["treasury_claimed"];
    if (!mapp(claimed))
        claimed = ([]);

    foreach (player_id in dungeon["players"]) {
        if (!claimed[player_id])
            return 0;
    }

    return 1;
}

int all_members_wiped(mapping dungeon)
{
    mapping member_states;
    string player_id;
    string state;
    int seen_effective_member;

    if (!mapp(dungeon))
        return 0;

    member_states = dungeon["member_states"];
    if (!mapp(member_states))
        member_states = ([]);

    seen_effective_member = 0;
    foreach (player_id in dungeon["players"] || ({ })) {
        state = member_states[player_id];
        if (state == "offline")
            continue;
        seen_effective_member = 1;
        if (state != "dead")
            return 0;
    }

    return seen_effective_member;
}

private int schedule_dungeon_callback(string callback, int delay, string dungeon_id)
{
    int handle;

    handle = TIMER_D->call_later(callback, delay, this_object(), dungeon_id);
    if (intp(handle) && handle > 0)
        return handle;

    /* owner runtime 未就绪时使用本 daemon 自有 call_out，保持回调仍在本 owner 内。 */
    return call_out(callback, delay, dungeon_id);
}

void schedule_failed_dungeon_cleanup(string dungeon_id)
{
    mapping dungeon;
    mapping pending;
    int attempts;
    int delay;
    int handle;

    dungeon = query_dungeon_runtime_store()[dungeon_id];
    if (!mapp(dungeon) || dungeon["state"] != "failed")
        return;

    pending = dungeon["cleanup_pending"];
    if (!mapp(pending)) {
        pending = ([
            "attempts": 0,
            "last_error": "",
            "moved": ([]),
            "updated_at": time(),
        ]);
        dungeon["cleanup_pending"] = pending;
    }
    if (pending["scheduled"])
        return;

    attempts = intp(pending["attempts"]) ? pending["attempts"] : 0;
    delay = attempts >= 5 ? 5 : 1;
    handle = schedule_dungeon_callback("retry_failed_dungeon_cleanup", delay,
        dungeon_id);
    if (!intp(handle) || handle <= 0) {
        pending["scheduled"] = 0;
        pending["last_error"] = "cleanup_timer_unavailable";
        pending["updated_at"] = time();
        return;
    }
    pending["scheduled"] = 1;
    pending["next_attempt_at"] = time() + delay;
    pending["updated_at"] = time();
}

private int retry_dungeon_cooldowns(mapping dungeon)
{
    mapping pending;
    mapping entry;
    string player_id;
    int save_result;
    int complete;

    if (!mapp(dungeon))
        return 1;
    pending = dungeon["cooldown_pending"];
    if (!mapp(pending) || sizeof(pending) == 0)
        return 1;

    complete = 1;
    foreach (player_id, entry in pending) {
        if (!mapp(entry) || !intp(entry["expires_at"])
            || !stringp(entry["dungeon_type"])
            || entry["dungeon_type"] == "") {
            complete = 0;
            continue;
        }
        save_result = DUNGEON_PLAYER_D->set_player_cooldown_until(
            player_id, entry["dungeon_type"], entry["expires_at"]);
        entry["save_result"] = save_result;
        entry["updated_at"] = time();
        if (save_result == 1)
            map_delete(pending, player_id);
        else
            complete = 0;
    }
    return complete;
}

void retry_failed_dungeon_cleanup(string dungeon_id)
{
    mapping dungeon;
    mapping pending;
    mapping store;
    string player_id;
    string *player_ids;
    int all_moved;
    int cleanup_handle;

    store = query_dungeon_runtime_store();
    dungeon = store[dungeon_id];
    if (!mapp(dungeon) || dungeon["state"] != "failed")
        return;

    pending = dungeon["cleanup_pending"];
    if (!mapp(pending))
        pending = dungeon["cleanup_pending"] = ([ "attempts": 0 ]);
    if (!mapp(pending["moved"]))
        pending["moved"] = ([]);
    pending["scheduled"] = 0;
    player_ids = (dungeon["players"] || ({ })) + ({ });
    all_moved = 1;
    foreach (player_id in player_ids) {
        if (pending["moved"][player_id])
            continue;
        if (move_member_to_exit(dungeon, player_id,
            "副本已失败，你已返回入口。\n")) {
            pending["moved"][player_id] = 1;
            clear_player_resume(player_id);
        } else
            all_moved = 0;
    }
    if (all_moved && !retry_dungeon_cooldowns(dungeon)) {
        pending["last_error"] = "cooldown_save_pending";
        pending["updated_at"] = time();
        schedule_failed_dungeon_cleanup(dungeon_id);
        return;
    }
    if (all_moved) {
        cleanup_handle = schedule_dungeon_callback(
            "cleanup_dungeon_instance_rooms", 1, dungeon_id);
        if (!intp(cleanup_handle) || cleanup_handle <= 0) {
            pending["last_error"] = "room_cleanup_timer_unavailable";
            pending["updated_at"] = time();
            return;
        }
        map_delete(store, dungeon_id);
        return;
    }

    pending["attempts"] = (pending["attempts"] || 0) + 1;
    pending["last_error"] = "member_exit_move_failed";
    pending["updated_at"] = time();
    schedule_failed_dungeon_cleanup(dungeon_id);
}

void close_dungeon_run(string dungeon_id, string reason, string message)
{
    mapping dungeon;
    mapping store;
    mapping pending;
    string player_id;
    string *player_ids;
    int all_moved;
    int cleanup_handle;

    store = query_dungeon_runtime_store();
    dungeon = store[dungeon_id];
    if (!mapp(dungeon))
        return;

    fail_dungeon(dungeon_id, reason);
    dungeon = store[dungeon_id];
    if (!mapp(dungeon))
        return;

    pending = dungeon["cleanup_pending"];
    if (!mapp(pending))
        pending = dungeon["cleanup_pending"] = ([ "moved": ([]) ]);
    if (!mapp(pending["moved"]))
        pending["moved"] = ([]);
    player_ids = (dungeon["players"] || ({ })) + ({ });
    all_moved = 1;
    foreach (player_id in player_ids) {
        if (pending["moved"][player_id])
            continue;
        if (move_member_to_exit(dungeon, player_id, message)) {
            pending["moved"][player_id] = 1;
            clear_player_resume(player_id);
        } else
            all_moved = 0;
    }
    if (!all_moved) {
        pending["attempts"] = (pending["attempts"] || 0) + 1;
        pending["last_error"] = "member_exit_move_failed";
        pending["updated_at"] = time();
        schedule_failed_dungeon_cleanup(dungeon_id);
        return;
    }
    if (!retry_dungeon_cooldowns(dungeon)) {
        pending["last_error"] = "cooldown_save_pending";
        pending["updated_at"] = time();
        schedule_failed_dungeon_cleanup(dungeon_id);
        return;
    }

    cleanup_handle = schedule_dungeon_callback(
        "cleanup_dungeon_instance_rooms", 1, dungeon_id);
    if (!intp(cleanup_handle) || cleanup_handle <= 0) {
        pending["last_error"] = "room_cleanup_timer_unavailable";
        pending["updated_at"] = time();
        return;
    }
    map_delete(store, dungeon_id);
}

int remove_member_from_dungeon(string dungeon_id, string player_id, string reason, int apply_cooldown)
{
    mapping dungeon;
    mapping cooldown_pending;
    int cooldown_result;
    int cooldown_seconds;

    dungeon = DUNGEON_STORE[dungeon_id];
    if (!mapp(dungeon) || member_array(player_id, dungeon["players"] || ({ })) == -1)
        return 0;

    if (!mapp(dungeon["member_states"]))
        dungeon["member_states"] = ([]);
    dungeon["member_states"][player_id] = reason;
    if (apply_cooldown && should_apply_dungeon_cooldown(dungeon)) {
        cooldown_seconds = query_dungeon_cooldown_seconds(dungeon);
        cooldown_result = set_player_cooldown(player_id, dungeon["type"],
            cooldown_seconds);
        if (cooldown_result != 1) {
            cooldown_pending = dungeon["cooldown_pending"];
            if (!mapp(cooldown_pending))
                cooldown_pending = dungeon["cooldown_pending"] = ([]);
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

    clear_player_resume(player_id);
    remove_player_from_runtime(dungeon, player_id);

    if (!sizeof(dungeon["players"] || ({ }))) {
        if (mapp(dungeon["cooldown_pending"])
            && sizeof(dungeon["cooldown_pending"])) {
            dungeon["state"] = "failed";
            dungeon["fail_reason"] = "cooldown_save_pending";
            schedule_failed_dungeon_cleanup(dungeon_id);
        } else
            destroy_dungeon(dungeon_id);
    }
    return 1;
}

int apply_guild_contribution_reward(object player, mapping def)
{
    int guild_contribution_reward;

    if (!objectp(player) || !mapp(def))
        return 0;

    guild_contribution_reward = def["guild_contribution_reward"];
    if (!intp(guild_contribution_reward) || guild_contribution_reward <= 0)
        return 0;

    if (!stringp(GUILD_D->query_player_guild(player)) || GUILD_D->query_player_guild(player) == "")
        return 0;

    if (!GUILD_D->add_contribution(player, guild_contribution_reward, "副本通关"))
        return 0;

    return guild_contribution_reward;
}

void refresh_dungeon_timeout(string dungeon_id)
{
    mapping dungeon;
    int expire_time;

    dungeon = DUNGEON_STORE[dungeon_id];
    if (!mapp(dungeon) || dungeon["state"] != "active")
        return;

    expire_time = dungeon["expire_time"];
    if (!intp(expire_time) || expire_time <= 0 || expire_time > time())
        return;

    fail_dungeon(dungeon_id, "timeout");
}

mapping build_command_result(int success, string message, mapping extra)
{
    mapping result;

    result = ([
        "success": success ? 1 : 0,
        "message": stringp(message) ? message : "",
    ]);
    if (mapp(extra))
        result += extra;
    return result;
}
