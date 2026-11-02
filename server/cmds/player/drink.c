/**
 * @file    drink.c
 * @brief   饮用命令 - 使用可饮用物品



 *
 * @par Description:
 * drink 的公共入口只保留提示与分发；consume 真链统一回到 `ITEM_INVENTORY_D`。
 */

#pragma strict_types

#include <system/services.h>
#include <framework/message_facade.h>

inherit "/lib/framework/network";

int help(object me);
int main(object me, string arg);

int main(object me, string arg)
{
    mapping response_context;

    if (!objectp(me))
        return 0;
    if (!stringp(arg) || arg == "") {
        msg_send(me, "你要喝什么？\n");
        return 1;
    }

    response_context = CLIENT_SYNC_D->capture_command_response_context(me);
    return call_other(ITEM_INVENTORY_D, "consume_item", me, "drink", arg,
        response_context);
}

int help(object me)
{
    msg_send(me, "命令：drink <物品>\n");
    return 1;
}
