/**
 * @file    dungeon_room.c
 * @brief   副本房间进入/销毁与玩家副本状态查询（dungeon 活跃链）。



 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

mapping get_dungeon(string dungeon_id)
{
    return DUNGEON_D->get_dungeon(dungeon_id);
}

mapping get_player_dungeon(object player)
{
    return DUNGEON_D->get_player_dungeon(player);
}

int enter_dungeon(object player, string dungeon_id)
{
    mapping dungeon;
    mapping moved;
    string player_id;
    object room;

    if (!objectp(player) || !stringp(dungeon_id))
        return 0;

    player_id = get_object_id(player);
    dungeon = DUNGEON_D->get_dungeon(dungeon_id);
    if (!stringp(player_id) || !mapp(dungeon))
        return 0;
    if (member_array(player_id, dungeon["players"] || ({ })) == -1)
        return 0;

    room = DUNGEON_INSTANCE_D->query_instance_entry_room(dungeon_id, dungeon);
    if (!objectp(room))
        return 0;

    moved = WORLD_TRAVEL_D->move_actor(player, room, ([
        "reason": "dungeon_enter",
    ]));
    return mapp(moved) && moved["ok"] ? 1 : 0;
}

void destroy_dungeon(string dungeon_id)
{
    DUNGEON_INSTANCE_D->destroy_instance_rooms(dungeon_id);
    DUNGEON_D->destroy_dungeon(dungeon_id);
}
