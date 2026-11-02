/**
 * @file    unequip.c
 * @brief   卸下装备命令



 *
 * @par Description:
 * 卸下入口只保留提示与分发；卸下运行时规则统一回到 `ITEM_INVENTORY_D`。
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
        msg_send(me, "卸下什么？\n");
        return 1;
    }

    response_context = CLIENT_SYNC_D->capture_command_response_context(me);
    return call_other(ITEM_INVENTORY_D, "unequip_item", me, arg,
        response_context);
}

int help(object me)
{
    msg_send(me, @HELP
命令：unequip（卸下装备）

卸下已装备的武器或防具。

用法：
  unequip <物品名>
HELP
    );
    return 1;
}
