/**
 * @file    dungeon_d.c
 * @brief   副本运行时服务真源门面
 *
 * @note
 * 对外保持 DUNGEON_D 单一入口；内部按公共运行 helper、查询/冷却、生命周期、
 * 宝库领取和成员/恢复拆分。副本内容模板、实例房间和战斗脚本仍不在这里定义。
 */

#pragma strict_types
#pragma save_types

#include <game_constants.h>

inherit "/lib/framework/daemon";
inherit "/daemons/services/gameplay/dungeon/dungeon_runtime_common";
inherit "/daemons/services/gameplay/dungeon/dungeon_runtime_queries";
inherit "/daemons/services/gameplay/dungeon/dungeon_lifecycle";
inherit "/daemons/services/gameplay/dungeon/dungeon_treasury";
inherit "/daemons/services/gameplay/dungeon/dungeon_membership";

nosave mapping _dungeons;
nosave int _dungeon_counter;

mapping query_dungeon_runtime_store()
{
    if (!mapp(_dungeons))
        _dungeons = ([]);
    return _dungeons;
}

int next_dungeon_counter()
{
    _dungeon_counter += 1;
    return _dungeon_counter;
}

int complete_dungeon(string dungeon_id);
varargs int fail_dungeon(string dungeon_id, string reason);
int abort_dungeon_entry(string dungeon_id, object *entered_players);
int mark_boss_defeated(object player, string dungeon_type);
int refresh_player_dungeon_state(object player);
int force_player_dungeon_timeout(object player);
int retry_pending_leave(object player);
int notify_player_down(object player, object killer);
int notify_player_recovered(object player);
mapping handle_player_disconnect(object player);
int resume_player(object player);
int invalidate_player_runtime_membership(object player, string reason);
void track_player_room(object player, object room);
int on_team_membership_changed(mapping event);
int set_player_cooldown(string player_id, string dungeon_type, int cooldown);
void destroy_dungeon(string dungeon_id);
int cleanup_dungeon_instance_rooms(string dungeon_id);

void create()
{
    ::create();
    if (!mapp(_dungeons))
        _dungeons = ([]);
}
