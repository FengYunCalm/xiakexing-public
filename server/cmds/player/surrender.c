#pragma strict_types

#include <system/services.h>
#include <framework/message_facade.h>

inherit "/lib/framework/command";
inherit "/lib/std/command_utils";

/**
 * @file    surrender.c
 * @brief   自首命令：在府衙向衙门投案，进入六扇门大牢（支线「老贼的托付」入口）。
 *
 * 大牢解密逃出链路已有（jail props + escape），本命令补齐主动进入入口：
 * 玩家在广陵县署（yamen）区域可自首入牢，牢里找老贼接支线。
 */

int help(object me);

int main(object me, string arg)
{
    object env;
    mapping moved;
    string env_path;

    if (!objectp(me))
        return 0;

    env = environment(me);
    if (!objectp(env))
        return 0;
    env_path = base_name(env);

    if (member_array(env_path, ({
        "/world/junzhou/rooms/junzhou_yamen",
        "/world/huaian/rooms/baohuai_huaian_yamen_front",
    })) == -1) {
        msg_send(me, "你不在衙门，投案得先走到县署再说。\n");
        return 1;
    }
    if (me->query("combat/hp") <= 0) {
        msg_send(me, "你现在站都站不稳，先去歇口气。\n");
        return 1;
    }

    msg_send(me, "你走到值房前，拍了下案板：「我，投案。」\n"
        "一名差役抬头看了你一眼，拿铁链比了比，又放下了：「自己走进去吧，省得我费事。」\n");
    moved = WORLD_TRAVEL_D->move_actor(me, "/world/yangzhou/rooms/jail__cell", ([
        "reason": "surrender",
    ]));
    if (!mapp(moved) || moved["ok"] != 1) {
        msg_send(me, "衙门的押送路线上出了岔子，你暂时无法投案。\n");
        return 1;
    }
    return 1;
}

int help(object me)
{
    msg_send(me, "命令：surrender（自首）\n"
        "在府衙（广陵县署）向衙门投案，进入大牢。牢里有话可听，有路可逃。\n");
    return 1;
}
