/**
 * @file    drop.c
 * @brief   丢弃命令 - 将物品丢到地上



 *
 * @par Description:
 * 公共丢弃入口，命令层只保留参数提示与调度；玩家物品生命周期真链统一回到 `ITEM_INVENTORY_D`。
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
        msg_send(me, "丢什么？\n");
        return 1;
    }

    response_context = CLIENT_SYNC_D->capture_command_response_context(me);
    return call_other(ITEM_INVENTORY_D, "drop_item", me, arg,
        response_context);
}

int help(object me)
{
    msg_send(me, @HELP
命令：drop（丢弃）

将身上的物品丢弃到地上。

用法：
  drop <物品名>  - 丢弃指定物品
  drop all       - 丢弃身上所有物品
HELP
    );
    return 1;
}
