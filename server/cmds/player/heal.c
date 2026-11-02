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
    object pharmacist;
    mapping result;
    mapping response_context;

    if (!objectp(me))
        return 0;
    response_context = CLIENT_SYNC_D->capture_command_response_context(me);
    pharmacist = find_pharmacist(me);
    if (!objectp(pharmacist)) {
        msg_send(me, "这里只能找大夫疗伤。\n", response_context);
        return 1;
    }

    result = MEDICAL_D->heal_player(me, pharmacist, response_context);
    MEDICAL_D->sync_heal_result(me, result, response_context);
    msg_send(me, mapp(result) ? (result["message"] || "疗伤失败。\n") : "疗伤失败。\n",
        response_context);
    return 1;
}

int help(object me)
{
    msg_send(me, "用法：heal\n");
    return 1;
}
