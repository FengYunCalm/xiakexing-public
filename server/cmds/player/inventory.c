/**
 * @file    inventory.c
 * @brief   物品栏命令



 *
 * @par Description:
 * inventory 入口只保留提示与分发；展示与协议同步统一回到 `ITEM_INVENTORY_D`。
 */

#pragma strict_types

#include <system/services.h>

inherit "/lib/framework/network";

int help(object me);
int main(object me, string arg);

int main(object me, string arg)
{
    mapping response_context;

    if (!objectp(me))
        return 0;
    response_context = CLIENT_SYNC_D->capture_command_response_context(me);
    return call_other(ITEM_INVENTORY_D, "show_inventory", me,
        response_context);
}

int help(object me)
{
    msg_send(me, @HELP
命令：inventory（物品栏）

查看你身上的所有物品。

用法：
  inventory
  i
HELP
    );
    return 1;
}
