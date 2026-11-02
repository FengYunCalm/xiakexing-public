/**
 * @file    list.c
 * @brief   查看商店商品列表命令



 */

#pragma strict_types

#include <system/ansi.h>
#include <framework/message_facade.h>

inherit "/lib/framework/command";
inherit "/lib/std/command_utils";

object find_vendor_in_room(object me);

private int open_vendor_shop(object me, object vendor)
{
    object shop_d;

    if (!objectp(me) || !objectp(vendor))
        return 0;

    shop_d = load_shop_service();
    if (!objectp(shop_d)) {
        msg_send(me, HIR "商店服务暂时不可用，请稍后再试。" NOR + "\n");
        return 1;
    }

    shop_d->open_shop(me, vendor);
    return 1;
}

int main(object me, string arg)
{
    object vendor;
    if (!objectp(me))
        return 0;

    vendor = find_vendor_in_room(me);
    if (!objectp(vendor)) {
        msg_send(me, "这里没有可查看的商品列表。\n");
        return 1;
    }

    if (!is_xk_client(me))
        msg_send(me, HIY "你查看了「" + get_object_display_name(vendor, "商人") + "」的货单。" NOR "\n");
    if (open_vendor_shop(me, vendor))
        return 1;

    msg_send(me, "对方没有可查看的商品列表。\n");
    return 1;
}

int help(object me)
{
    msg_send(me, @HELP
命令：list（商品列表）

查看当前场景商人的商品列表。

用法：
  list            查看商人货单

说明：
  - 必须与商人处于同一房间
  - 货单内容以商店真源为准

相关命令：
  buy     - 购买物品
  sell    - 出售物品

HELP
    );
    return 1;
}
