/**
 * @file    world_observer_d.c
 * @brief   玩家世界投影刷新与观察者视图边界。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

nosave int _projection_player_refresh_count;

int refresh_player_projection_state(object player, object room)
{
    if (!objectp(player) || !objectp(room))
        return 0;

    ROOM_VIEW_D->invalidate_room_view_content(room);
    WORLD_ENTITY_LIFECYCLE_D->ensure_room_entities(room);
    _projection_player_refresh_count++;
    return 1;
}

int sync_player_room_transition(object player, object old_room, object new_room)
{
    return refresh_player_projection_state(player, new_room);
}

int query_player_refresh_count()
{
    return _projection_player_refresh_count;
}

mapping query_health_snapshot()
{
    return ([
        "player_refresh_count": _projection_player_refresh_count,
    ]);
}

void create()
{
    ::create();
    _projection_player_refresh_count = 0;
}
