/**
 * @file    alchemy.c
 * @brief   玩家炼药命令
 *
 * @note
 * 当前只开放药师场景下的固定药方制作；命令层只做参数解析、场景发现
 * 与结果回包，具体材料/扣费/产物真链统一回到 `ALCHEMY_D`。
 */

#pragma strict_types

#include <system/services.h>

inherit "/lib/framework/command";
inherit "/lib/std/command_utils";

int main(object me, string arg);
int help(object me);

private object find_pharmacist(object me)
{
    object env;
    object target;

    env = environment(me);
    if (!objectp(env))
        return 0;

    target = find_living_in_room_by_contract(env, "healer", "healer", me);
    if (objectp(target))
        return target;
    target = find_living_in_room(env, "大夫", me);
    if (objectp(target))
        return target;
    return find_living_in_room(env, "pharmacist", me);
}

int main(object me, string arg)
{
    mapping parsed;
    mapping result;
    object pharmacist;
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
            pharmacist = find_pharmacist(me);
            if (!objectp(pharmacist)) {
                msg_send(me, "这里只有孙大夫才能替你配药。\n");
                return 1;
            }
            result = ALCHEMY_D->craft(me, pharmacist, parsed["rest"]);
            ITEM_FEEDBACK_D->sync_forge_result(me, result, response_context);
            msg_send(me, result["message"] || "配药失败。\n");
            return 1;
        default:
            return help(me);
    }
}

int help(object me)
{
    msg_send(me, "用法：alchemy craft <recipe_id>\n");
    return 1;
}
