/**
 * @file    equip.c
 * @brief   装备管理命令



 *
 * @par Description:
 * 装备入口只保留提示与分发；装备运行时规则统一回到 `ITEM_INVENTORY_D`。
 */

#pragma strict_types

#include <system/services.h>
#include <framework/message_facade.h>

inherit "/lib/framework/network";

int help(object me);
int main(object me, string arg);

int main(object me, string arg)
{
    int result;
    mapping response_context;

    if (!objectp(me))
        return 0;
    if (!stringp(arg) || arg == "") {
        msg_send(me, "装备什么？\n");
        return 1;
    }

    response_context = CLIENT_SYNC_D->capture_command_response_context(me);
    result = call_other(ITEM_INVENTORY_D, "equip_item", me, arg,
        response_context);
    return result;
}

int help(object me)
{
    msg_send(me, @HELP
命令：equip（装备）

装备武器或防具。

用法：
  equip <物品名>
HELP
    );
    return 1;
}
