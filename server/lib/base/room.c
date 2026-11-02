/**
 * @file    room.c
 * @brief   房间基类



 *
 * @par Description:
 * 房间基类，提供出口、内容容器与房间生命周期基础能力。
 */

#pragma strict_types

#include <system/globals.h>
#include <system/ansi.h>
#include <system/services.h>
#include <framework/dbase.h>

inherit "/lib/base/object";
inherit "/lib/base/room_flags";
inherit "/lib/base/room_exits";
inherit "/lib/base/room_identity";
inherit "/lib/base/room_lifecycle";
inherit "/lib/base/room_items";
inherit "/lib/base/room_presence";
inherit "/lib/base/room_directions";

mapping query_exits();
string get_exit_direction_name(string dir);

int ensure_room_entities()
{
    int ensured;
    mixed checked_before;

    /* “未发布结果”的判据是 lifecycle 完全没跑（时间戳既不前进也不等于
     * 当前秒）。lifecycle 在 T 秒完成而本调用在 T+1 秒读返回值的跨秒
     * 场景是正常完成（房间可能本来就没有实体），不得误报。 */
    checked_before = query_temp("world_lifecycle/entities_checked_at");
    ensured = WORLD_ENTITY_LIFECYCLE_D->ensure_room_entities(this_object());
    if (!ensured
        && query_temp("world_lifecycle/entities_checked_at") == checked_before
        && query_temp("world_lifecycle/entities_checked_at") != time()) {
        log_file("system/error", sprintf("[%s] room entity lifecycle did not publish a result for %s\n",
            ctime(time()), base_name(this_object())));
    }
    return ensured;
}

int ensure_room_entities_with_timing(mapping timing_ns)
{
    int ensured;
    mixed checked_before;

    checked_before = query_temp("world_lifecycle/entities_checked_at");
    ensured = WORLD_ENTITY_LIFECYCLE_D->ensure_room_entities_with_timing(this_object(), timing_ns);
    if (!ensured
        && query_temp("world_lifecycle/entities_checked_at") == checked_before
        && query_temp("world_lifecycle/entities_checked_at") != time()) {
        log_file("system/error", sprintf("[%s] timed room entity lifecycle did not publish a result for %s\n",
            ctime(time()), base_name(this_object())));
    }
    return ensured;
}

/**
 * @brief 获取房间内所有对象
 * @return 对象数组
 */
object *query_inventory()
{
    return all_inventory(this_object());
}

string query_long()
{
    return "";
}
