/**
 * @file    item_inventory_common.c
 * @brief   物品生命周期公共查询、展示和目标解析 helper。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

int item_inventory_supports_xk_protocol(object target)
{
    mixed version;

    if (!objectp(target) || !interactive(target))
        return 0;
    if (target->query_temp("client/plain_text_only"))
        return 0;

    version = target->query_temp("xk_protocol_version");
    return stringp(version) && version != "";
}

string query_item_display_name(object item)
{
    return call_other("/lib/std/utils/util_item", "get_item_display_name", item, "物品");
}

string query_actor_display_name(object actor, string fallback)
{
    return call_other("/lib/std/utils/util_player", "get_object_display_name", actor, fallback);
}

string resolve_item_command_target(object item)
{
    return call_other("/lib/std/utils/util_item", "get_item_command_target", item);
}

object resolve_item_from_collection(object *items, string item_name)
{
    return call_other("/lib/std/utils/util_item", "find_item_in_collection",
        items, item_name);
}

object find_item_by_name(object me, string item_name)
{
    object item;

    if (!objectp(me) || !stringp(item_name) || item_name == "")
        return 0;

    item = present(item_name, me);
    if (objectp(item))
        return item;

    return resolve_item_from_collection(all_inventory(me), item_name);
}

object find_accessible_item(object me, string item_name)
{
    object item;
    object env;

    if (!objectp(me) || !stringp(item_name) || item_name == "")
        return 0;

    item = find_item_by_name(me, item_name);
    if (objectp(item))
        return item;

    env = environment(me);
    if (!objectp(env))
        return 0;

    item = present(item_name, env);
    if (objectp(item))
        return item;

    return resolve_item_from_collection(all_inventory(env), item_name);
}

object find_give_target(object me, string target_name)
{
    object env;
    object target;

    if (!objectp(me) || !stringp(target_name) || target_name == "")
        return 0;

    env = environment(me);
    if (!objectp(env))
        return 0;

    target = call_other("/lib/std/utils/util_player", "find_visible_player", me, target_name);
    if (objectp(target) && target != me)
        return target;

    target = call_other("/lib/std/utils/util_player", "find_living_in_room", env, target_name, me);
    if (objectp(target))
        return target;

    target = QUEST_ACTOR_D->resolve_actor_target(me, env, target_name);
    if (objectp(target) && environment(target) == env)
        return target;

    return 0;
}

int is_equipped_item(object item)
{
    return objectp(item)
        && function_exists("query_equipped", item)
        && item->query_equipped();
}

int can_receive_target(object target)
{
    if (!objectp(target))
        return 0;
    if (function_exists("is_character", target) && target->is_character())
        return 1;
    return userp(target);
}
