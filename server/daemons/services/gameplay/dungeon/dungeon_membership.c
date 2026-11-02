/**
 * @file    dungeon_membership.c
 * @brief   副本成员状态、掉线恢复和队伍变更模块。
 */

#pragma strict_types
#pragma save_types

#include <game_constants.h>
#include <system/services.h>
#include <framework/message_facade.h>

#define DUNGEON_STORE query_dungeon_runtime_store()

mapping query_dungeon_runtime_store();
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
int leave_dungeon(object player);
int retry_pending_leave(object player);
int apply_guild_contribution_reward(object player, mapping def);
void refresh_dungeon_timeout(string dungeon_id);
mapping build_command_result(int success, string message, mapping extra);
int complete_dungeon(string dungeon_id);
varargs int fail_dungeon(string dungeon_id, string reason);
int set_player_cooldown(string player_id, string dungeon_type, int cooldown);
void destroy_dungeon(string dungeon_id);
int cleanup_dungeon_instance_rooms(string dungeon_id);

int invalidate_player_runtime_membership(object player, string reason)
{
    mapping dungeon;
    object room;
    string dungeon_id;
    string player_id;

    if (!objectp(player))
        return 0;
    player_id = ensure_player_id(player);
    dungeon_id = find_player_dungeon_id_by_player_id(player_id);
    if (!stringp(dungeon_id) || dungeon_id == "") {
        clear_player_resume(player_id);
        return 0;
    }
    dungeon = DUNGEON_STORE[dungeon_id];
    room = environment(player);
    if (mapp(dungeon) && objectp(room)
        && room->query("dungeon/id") == dungeon_id
        && !move_member_to_exit(dungeon, player_id,
            "副本状态已失效，你已返回入口。\n"))
        return 0;
    return remove_member_from_dungeon(dungeon_id, player_id,
        stringp(reason) && reason != "" ? reason : "runtime_invalidated", 0);
}

void track_player_room(object player, object room)
{
    string dungeon_id;
    mapping dungeon;

    if (!objectp(player) || !objectp(room))
        return;

    dungeon_id = room->query("dungeon/id");
    if (!stringp(dungeon_id) || dungeon_id == "")
        return;

    dungeon = DUNGEON_STORE[dungeon_id];
    if (!mapp(dungeon) || member_array(ensure_player_id(player), dungeon["players"] || ({ })) == -1)
        return;

    track_player_template(player, dungeon, room);
}

int notify_player_down(object player, object killer)
{
    string player_id;
    string dungeon_id;
    mapping dungeon;

    if (!objectp(player))
        return 0;

    player_id = ensure_player_id(player);
    dungeon_id = find_player_dungeon_id_by_player_id(player_id);
    if (!stringp(dungeon_id) || dungeon_id == "")
        return 0;

    dungeon = DUNGEON_STORE[dungeon_id];
    if (!mapp(dungeon) || dungeon["state"] != "active")
        return 0;

    if (!mapp(dungeon["member_states"]))
        dungeon["member_states"] = build_member_states(dungeon["players"] || ({ }));

    dungeon["member_states"][player_id] = "dead";
    dungeon["deaths"] = (dungeon["deaths"] || 0) + 1;
    track_player_template(player, dungeon, environment(player));

    if (all_members_wiped(dungeon))
        close_dungeon_run(dungeon_id, "wipe", "副本团灭，所有人都被送出了实例。\n");

    return 1;
}

int notify_player_recovered(object player)
{
    string player_id;
    string dungeon_id;
    mapping dungeon;

    if (!objectp(player))
        return 0;

    player_id = ensure_player_id(player);
    dungeon_id = find_player_dungeon_id_by_player_id(player_id);
    if (!stringp(dungeon_id) || dungeon_id == "")
        return 0;

    dungeon = DUNGEON_STORE[dungeon_id];
    if (!mapp(dungeon) || dungeon["state"] != "active")
        return 0;

    if (!mapp(dungeon["member_states"]))
        dungeon["member_states"] = build_member_states(dungeon["players"] || ({ }));

    dungeon["member_states"][player_id] = "active";
    track_player_template(player, dungeon, environment(player));
    return 1;
}

mapping handle_player_disconnect(object player)
{
    string player_id;
    string dungeon_id;
    mapping dungeon;
    string template_path;
    object room;
    int resume_save_result;

    if (!objectp(player))
        return ([ "ok": 0, "error": "player unavailable" ]);

    player_id = ensure_player_id(player);
    dungeon_id = find_player_dungeon_id_by_player_id(player_id);
    if (!stringp(dungeon_id) || dungeon_id == "")
        return ([ "ok": 1, "state": "no_dungeon" ]);

    dungeon = DUNGEON_STORE[dungeon_id];
    if (!mapp(dungeon) || dungeon["state"] != "active")
        return ([ "ok": 1, "state": "dungeon_inactive" ]);

    if (!mapp(dungeon["member_states"]))
        dungeon["member_states"] = build_member_states(dungeon["players"] || ({ }));

    room = environment(player);
    template_path = objectp(room) ? room->query("dungeon/template_path") : 0;
    if ((!stringp(template_path) || template_path == "") && mapp(dungeon["member_room_templates"]))
        template_path = dungeon["member_room_templates"][player_id];

    dungeon["member_states"][player_id] = player->query_temp("is_dying") ? "dead" : "offline";
    if (stringp(template_path) && template_path != "")
        resume_save_result = save_player_resume(dungeon, player_id,
            template_path);
    else
        resume_save_result = DUNGEON_PLAYER_D->set_resume_context(player_id, ([
            "dungeon_id": dungeon["id"] || "",
            "template_path": "",
            "timestamp": time(),
        ]));
    if (resume_save_result != 1)
        return ([ "ok": 0, "state": "dungeon_disconnect_save_pending",
            "dungeon_id": dungeon_id,
            "pending_recovery": 1,
            "save_phase": DUNGEON_PLAYER_D->query_dungeon_save_phase() ]);

    return ([ "ok": 1, "state": "dungeon_disconnected",
        "dungeon_id": dungeon_id ]);
}

int resume_player(object player)
{
    string player_id;
    string dungeon_id;
    mapping dungeon;
    mapping context;
    mapping moved;
    object room;
    string template_path;

    if (!objectp(player))
        return 0;

    player_id = ensure_player_id(player);
    dungeon_id = find_player_dungeon_id_by_player_id(player_id);
    dungeon = stringp(dungeon_id) ? DUNGEON_STORE[dungeon_id] : 0;
    if (mapp(dungeon) && mapp(dungeon["leave_pending"])
        && mapp(dungeon["leave_pending"][player_id]))
        return retry_pending_leave(player) && leave_dungeon(player);

    context = DUNGEON_PLAYER_D->query_resume_context(player_id);
    dungeon_id = mapp(context) ? context["dungeon_id"] : 0;
    if (!stringp(dungeon_id) || dungeon_id == "")
        dungeon_id = find_player_dungeon_id_by_player_id(player_id);
    if (!stringp(dungeon_id) || dungeon_id == "")
        return 0;

    refresh_dungeon_timeout(dungeon_id);
    dungeon = DUNGEON_STORE[dungeon_id];
    if (!mapp(dungeon) || dungeon["state"] != "active") {
        clear_player_resume(player_id);
        return 0;
    }
    if (dungeon["instance_scope"] == "character"
        && dungeon["owner_id"] != player_id) {
        invalidate_player_runtime_membership(player, "world_scope_mismatch");
        return 0;
    }

    template_path = mapp(context) ? context["template_path"] : 0;
    if ((!stringp(template_path) || template_path == "") && mapp(dungeon["member_room_templates"]))
        template_path = dungeon["member_room_templates"][player_id];

    room = DUNGEON_INSTANCE_D->query_instance_room(dungeon_id, template_path);
    if (!objectp(room))
        room = DUNGEON_INSTANCE_D->query_instance_entry_room(dungeon_id, dungeon);
    moved = objectp(room) ? WORLD_TRAVEL_D->move_actor(player, room, ([
        "reason": "dungeon_resume",
    ])) : 0;
    if (!mapp(moved) || !moved["ok"]) {
        invalidate_player_runtime_membership(player, "resume_move_failed");
        return 0;
    }

    if (!mapp(dungeon["member_states"]))
        dungeon["member_states"] = build_member_states(dungeon["players"] || ({ }));

    dungeon["member_states"][player_id] = player->query_temp("is_dying") ? "dead" : "active";
    track_player_template(player, dungeon, room);
    return clear_player_resume(player_id) == 1 ? 1 : 0;
}

int on_team_membership_changed(mapping event)
{
    string event_type;
    string dungeon_id;
    string *affected_ids;
    string *before_ids;
    string player_id;

    if (!mapp(event))
        return 0;

    event_type = event["event"];
    affected_ids = event["affected_ids"] || ({ });
    before_ids = event["member_ids_before"] || ({ });

    foreach (player_id in affected_ids + before_ids + ({ event["actor_id"] })) {
        dungeon_id = find_player_dungeon_id_by_player_id(player_id);
        if (stringp(dungeon_id) && dungeon_id != "")
            break;
    }

    if (!stringp(dungeon_id) || dungeon_id == "")
        return 0;

    switch (event_type) {
        case "dismiss":
            close_dungeon_run(dungeon_id, "team_invalid", "队伍已解散，本次副本被迫终止。\n");
            return 1;
        case "leave":
            foreach (player_id in affected_ids) {
                if (move_member_to_exit(DUNGEON_STORE[dungeon_id], player_id,
                    "你离开了队伍，本次副本资格已失效。\n"))
                    remove_member_from_dungeon(dungeon_id, player_id, "left", 1);
            }
            return 1;
        case "kick":
            foreach (player_id in affected_ids) {
                if (move_member_to_exit(DUNGEON_STORE[dungeon_id], player_id,
                    "你被移出了队伍，本次副本资格已失效。\n"))
                    remove_member_from_dungeon(dungeon_id, player_id, "kicked", 1);
            }
            return 1;
    }

    return 0;
}
