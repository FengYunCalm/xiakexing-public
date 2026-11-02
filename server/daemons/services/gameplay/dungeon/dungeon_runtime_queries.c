/**
 * @file    dungeon_runtime_queries.c
 * @brief   副本定义、查询和冷却门面模块。
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
int should_apply_dungeon_cooldown(mapping dungeon);
void destroy_dungeon(string dungeon_id);
int cleanup_dungeon_instance_rooms(string dungeon_id);

mapping get_dungeon(string dungeon_id)
{
    if (!stringp(dungeon_id) || !mapp(DUNGEON_STORE[dungeon_id]))
        return 0;

    return copy(DUNGEON_STORE[dungeon_id]);
}

mapping query_dungeon_definition(string dungeon_type)
{
    return DUNGEON_REGISTRY_D->query_dungeon_definition(dungeon_type);
}

mapping query_dungeon_definition_for_actor(object actor, string dungeon_type)
{
    mapping definition;
    mapping projected;

    definition = query_dungeon_definition(dungeon_type);
    if (!mapp(definition))
        return 0;
    projected = CONTENT_MODE_POLICY_D->project_content(actor, "dungeon", definition);
    return mapp(projected) && projected["ok"] && projected["visible"]
        ? projected : 0;
}

string *query_all_dungeon_types()
{
    return DUNGEON_REGISTRY_D->query_all_dungeon_types();
}

mapping get_player_dungeon(object player)
{
    string dungeon_id;
    mapping dungeon;

    dungeon_id = find_player_dungeon_id(player);
    if (!stringp(dungeon_id) || dungeon_id == "")
        return 0;

    refresh_dungeon_timeout(dungeon_id);
    dungeon = DUNGEON_STORE[dungeon_id];
    return mapp(dungeon) ? copy(dungeon) : 0;
}

int refresh_player_dungeon_state(object player)
{
    string dungeon_id;

    dungeon_id = find_player_dungeon_id(player);
    if (!stringp(dungeon_id) || dungeon_id == "")
        return 0;

    refresh_dungeon_timeout(dungeon_id);
    return 1;
}

int force_player_dungeon_timeout(object player)
{
    string dungeon_id;
    mapping store;

    dungeon_id = find_player_dungeon_id(player);
    if (!stringp(dungeon_id) || dungeon_id == "")
        return 0;

    store = query_dungeon_runtime_store();
    if (!mapp(store[dungeon_id]))
        return 0;

    store[dungeon_id]["expire_time"] = time() - 1;
    return 1;
}

int query_cooldown(object player, string dungeon_type)
{
    mapping definition;
    string player_id;

    if (!objectp(player) || !stringp(dungeon_type) || dungeon_type == "")
        return 0;

    player_id = ensure_player_id(player);
    if (!stringp(player_id) || player_id == "")
        return 0;

    definition = query_dungeon_definition_for_actor(player, dungeon_type);
    if (mapp(definition) && !should_apply_dungeon_cooldown(definition))
        return 0;

    return DUNGEON_PLAYER_D->query_player_cooldown(player_id, dungeon_type);
}

int set_player_cooldown(string player_id, string dungeon_type, int cooldown)
{
    if (!stringp(player_id) || player_id == ""
        || !stringp(dungeon_type) || dungeon_type == "")
        return 0;

    return DUNGEON_PLAYER_D->set_player_cooldown(player_id,
        dungeon_type, cooldown);
}
