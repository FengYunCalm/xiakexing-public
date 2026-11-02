/**
 * @file    get.c
 * @brief   获取命令 - 从环境中拾取物品



 *
 * @par Description:
 * 公共拾取入口，命令层只保留参数提示与调度；玩家物品生命周期真链统一回到 `ITEM_INVENTORY_D`。
 */

#pragma strict_types

#include <system/services.h>
#include <framework/message_facade.h>

inherit "/lib/framework/command";

int help(object me);
int main(object me, string arg);

int main(object me, string arg)
{
    mapping response_context;

    if (!objectp(me))
        return 0;
    if (!stringp(arg) || arg == "") {
        msg_send(me, "捡什么？\n");
        return 1;
    }

    response_context = CLIENT_SYNC_D->capture_command_response_context(me);
    return call_other(ITEM_INVENTORY_D, "pickup_item", me, arg,
        response_context);
}

int help(object me)
{
    msg_send(me, "命令：get <物品>\n");
    return 1;
}
