#pragma strict_types

#include <system/services.h>
#include <framework/message_facade.h>

inherit "/lib/framework/command";
inherit "/lib/std/command_utils";

int help(object me);

/*
 * 大牢解密交互（按钮 cmd：jail interact <kind>，玩家不手输）。
 * 解密对象是牢房里的物件（npc_yz_jail_prop），点击弹窗 → 按钮触发。
 */
int main(object me, string arg)
{
    object prop;
    object env;
    string kind;
    string rest;

    if (!objectp(me))
        return 0;
    if (!stringp(arg) || arg == "")
        return help(me);

    env = environment(me);
    if (!objectp(env))
        return 0;

    if (sscanf(arg, "interact %s", rest) == 1) {
        object *candidates;
        int i;

        kind = trim(rest);
        candidates = all_inventory(env);
        for (i = 0; i < sizeof(candidates); i++) {
            if (!objectp(candidates[i]))
                continue;
            if (candidates[i]->query("jail_prop_kind") != kind)
                continue;
            if (function_exists("do_jail", candidates[i]))
                return candidates[i]->do_jail(kind);
        }
        msg_send(me, "这里没有可交互的东西。\n");
        return 1;
    }

    return help(me);
}

int help(object me)
{
    msg_send(me, "用法：jail interact <graffiti|brick|lock>\n"
        "说明：大牢解密交互，通过牢房物件的弹窗按钮触发。\n");
    return 1;
}
