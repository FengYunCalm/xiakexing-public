#pragma strict_types

#include <system/services.h>

inherit "/lib/framework/command";
inherit "/lib/std/command_utils";

int main(object me, string arg);
int help(object me);

private object find_blacksmith(object me)
{
    object env;
    object smith;

    env = environment(me);
    if (!objectp(env))
        return 0;

    smith = find_living_in_room_by_contract(env, "", "blacksmith", me);
    if (objectp(smith))
        return smith;
    smith = find_living_in_room(env, "铁匠", me);
    if (objectp(smith))
        return smith;
    return find_living_in_room(env, "blacksmith", me);
}

int main(object me, string arg)
{
    mapping parsed;
    mapping result;
    object item;
    object smith;
    mapping response_context;

    if (!objectp(me))
        return 0;
    if (!stringp(arg) || arg == "")
        return help(me);

    response_context = CLIENT_SYNC_D->capture_command_response_context(me);
    parsed = parse_subcommand_args(arg);
    if (!parsed["valid"])
        return help(me);

    switch (parsed["action"]) {
        case "craft":
            if (!stringp(parsed["rest"]) || parsed["rest"] == "")
                return help(me);
            smith = find_blacksmith(me);
            if (!objectp(smith)) {
                msg_send(me, "这里只有铁匠才能替你打造东西。\n");
                return 1;
            }
            result = FORGE_D->craft(me, smith, parsed["rest"]);
            ITEM_FEEDBACK_D->sync_forge_result(me, result, response_context);
            msg_send(me, result["message"] || "打造失败。\n");
            return 1;
        case "enhance":
            if (!stringp(parsed["rest"]) || parsed["rest"] == "")
                return help(me);
            smith = find_blacksmith(me);
            if (!objectp(smith)) {
                msg_send(me, "这里只有铁匠才能替你强化装备。\n");
                return 1;
            }
            item = find_item_in_player(me, parsed["rest"]);
            if (!objectp(item)) {
                msg_send(me, "你身上没有这件物品。\n");
                return 1;
            }
            result = FORGE_D->enhance(me, smith, item);
            ITEM_FEEDBACK_D->sync_forge_result(me, result, response_context);
            msg_send(me, result["message"] || "强化失败。\n");
            return 1;
        default:
            return help(me);
    }
}

int help(object me)
{
    msg_send(me, "用法：forge craft <recipe_id> | forge enhance <物品名>\n");
    return 1;
}
