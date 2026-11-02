/**
 * @file    dungeon_treasury.c
 * @brief   副本宝库领取和奖励结算模块。
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
int apply_guild_contribution_reward(object player, mapping def);
void refresh_dungeon_timeout(string dungeon_id);
mapping build_command_result(int success, string message, mapping extra);
int complete_dungeon(string dungeon_id);
varargs int fail_dungeon(string dungeon_id, string reason);
int set_player_cooldown(string player_id, string dungeon_type, int cooldown);
void destroy_dungeon(string dungeon_id);
int cleanup_dungeon_instance_rooms(string dungeon_id);

varargs mapping command_claim_treasury(object player,
    mapping response_context)
{
    mapping dungeon;
    mapping store;
    mapping def;
    mapping claimed;
    mapping applied;
    mapping marker;
    mapping player_claims;
    mapping settlement;
    int guild_contribution_reward;
    string player_id;
    string room_path;
    string settlement_key;

    if (!objectp(player))
        return build_command_result(0, "无效角色。\n", 0);

    dungeon = get_player_dungeon(player);
    if (!mapp(dungeon))
        return build_command_result(0, "当前不在任何副本中。\n", 0);

    def = DUNGEON_REGISTRY_D->query_dungeon_definition(dungeon["type"]);
    room_path = base_name(environment(player));
    if (!mapp(def) || !stringp(def["treasury_room"]) || def["treasury_room"] != room_path)
        return build_command_result(0, "这里只能在藏宝库领取战利品。\n", 0);

    player_id = ensure_player_id(player);
    claimed = dungeon["treasury_claimed"];
    if (!mapp(claimed))
        claimed = ([]);
    if (claimed[player_id])
        return build_command_result(0, "当前副本宝库奖励已经领取过了。\n", 0);
    if (dungeon["state"] == "failed")
        return build_command_result(0, "当前副本已失败，无法领取宝库奖励。\n", ([ "state": dungeon["state"] ]));
    if (dungeon["state"] == "completed")
        return build_command_result(0, "当前副本结算已经结束。\n", ([ "state": dungeon["state"] ]));
    if (dungeon["state"] != "active")
        return build_command_result(0, "当前副本不处于可领取状态。\n", ([ "state": dungeon["state"] ]));
    if (!dungeon["boss_defeated"])
        return build_command_result(0, "必须先击败副本首领，才能领取宝库奖励。\n", 0);

    settlement_key = "dungeon:" + hash("sha256",
        "treasury:" + dungeon["id"] + ":" + player_id);
    settlement = REWARD_SETTLEMENT_D->settle_reward(player, settlement_key,
        ([ "attachments": copy(def["treasury_rewards"] || ({ })) ]), ([
            "source_mode": "dungeon",
            "source_content_id": dungeon["id"],
            "resolution_token": player_id,
        ]));
    if (!mapp(settlement) || !settlement["ok"])
        return build_command_result(0, "宝库奖励领取失败。\n", ([
            "settlement": settlement,
        ]));

    player_claims = player->query("dungeon/treasury_claims");
    player_claims = mapp(player_claims) ? copy(player_claims) : ([]);
    player_claims[dungeon["id"]] = time();
    marker = REWARD_SETTLEMENT_D->commit_claim_marker(player,
        settlement_key, "dungeon/treasury_claims", player_claims);
    if (!mapp(marker) || !marker["ok"])
        return build_command_result(0, "宝库奖励已结算，领取标记待恢复。\n", ([
            "settlement": settlement,
        ]));

    applied = settlement["applied"] || ([]);
    guild_contribution_reward = apply_guild_contribution_reward(player, def);

    store = query_dungeon_runtime_store();
    dungeon = store[dungeon["id"]];
    if (!mapp(dungeon))
        return build_command_result(0, "当前副本状态已失效。\n", 0);
    if (!mapp(dungeon["treasury_claimed"]))
        dungeon["treasury_claimed"] = ([]);

    dungeon["treasury_claimed"][player_id] = time();
    if (all_members_claimed_treasury(dungeon))
        complete_dungeon(dungeon["id"]);
    REWARD_FEEDBACK_D->sync_reward_client_state(player, applied,
        response_context);
    return build_command_result(1, REWARD_FEEDBACK_D->format_reward_summary(([ "attachments": def["treasury_rewards"] ])), ([
        "applied": applied,
        "dungeon_id": dungeon["id"],
        "guild_contribution": guild_contribution_reward,
    ]));
}
