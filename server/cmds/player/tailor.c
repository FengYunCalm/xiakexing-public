/**
 * @file    tailor.c
 * @brief   玩家裁衣命令
 */

#pragma strict_types

#include <system/services.h>

inherit "/lib/framework/command";
inherit "/lib/std/command_utils";

int main(object me, string arg);
int help(object me);

private object find_tailor(object me)
{
    object env;
    object target;

    env = environment(me);
    if (!objectp(env))
        return 0;

    target = find_living_in_room_by_contract(env, "", "tailor", me);
    if (objectp(target))
        return target;
    target = find_living_in_room(env, "裁缝", me);
    if (objectp(target))
        return target;
    target = find_living_in_room(env, "苏裁缝", me);
    if (objectp(target))
        return target;
    return find_living_in_room(env, "tailor", me);
}

int main(object me, string arg)
{
    mapping parsed;
    mapping result;
    object tailor;
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
            tailor = find_tailor(me);
            if (!objectp(tailor)) {
                msg_send(me, "这里只有苏裁缝才能替你裁衣。\n");
                return 1;
            }
            result = TAILOR_D->craft(me, tailor, parsed["rest"]);
            ITEM_FEEDBACK_D->sync_forge_result(me, result, response_context);
            msg_send(me, result["message"] || "裁衣失败。\n");
            return 1;
        default:
            return help(me);
    }
}

int help(object me)
{
    msg_send(me, "用法：tailor craft <recipe_id>\n");
    return 1;
}
