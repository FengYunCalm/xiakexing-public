#pragma strict_types

#include <system/ansi.h>
#include <system/services.h>

inherit "/lib/framework/command";
inherit "/lib/std/command_utils";

int main(object me, string arg);
int help(object me);

private object find_blacksmith(object me)
{
    object env;
    object target;

    env = environment(me);
    if (!objectp(env))
        return 0;

    target = find_living_in_room_by_contract(env, "", "blacksmith", me);
    if (objectp(target))
        return target;
    target = find_living_in_room(env, "铁匠", me);
    if (objectp(target))
        return target;
    return find_living_in_room(env, "blacksmith", me);
}

int main(object me, string arg)
{
    object item;
    object smith;
    mapping result;
    mapping response_context;

    if (!objectp(me))
        return 0;
    if (!stringp(arg) || arg == "")
        return help(me);

    smith = find_blacksmith(me);
    if (!objectp(smith)) {
        msg_send(me, "这里只能找铁匠修理装备。\n");
        return 1;
    }

    item = find_item_in_player(me, arg);
    if (!objectp(item)) {
        msg_send(me, "你身上没有这件物品。\n");
        return 1;
    }
    if (!function_exists("query_durability", item) || !function_exists("query_max_durability", item) || !function_exists("set_durability", item)) {
        msg_send(me, "这件物品无法修理。\n");
        return 1;
    }

    response_context = CLIENT_SYNC_D->capture_command_response_context(me);
    result = ITEM_REPAIR_D->repair_item(me, smith, item);
    ITEM_FEEDBACK_D->sync_repair_result(me, item, result, response_context);
    msg_send(me, mapp(result) ? (result["message"] || "修理失败。\n") : "修理失败。\n");
    return 1;
}

int help(object me)
{
    msg_send(me, "用法：repair <物品名>\n");
    return 1;
}
