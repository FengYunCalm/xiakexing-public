/**
 * @file    eat.c
 * @brief   使用物品命令 - 食用食物和使用消耗品



 *
 * @par Description:
 * eat 的公共入口只保留提示与分发；consume 真链统一回到 `ITEM_INVENTORY_D`。
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
        msg_send(me, "你想吃什么？\n");
        return 1;
    }

    response_context = CLIENT_SYNC_D->capture_command_response_context(me);
    return call_other(ITEM_INVENTORY_D, "consume_item", me, "eat", arg,
        response_context);
}

int help(object me)
{
    msg_send(me, @HELP
命令：eat（使用）

食用食物或使用消耗品。

用法：
  eat <物品名>     - 食用/使用指定物品
HELP
    );
    return 1;
}
