/**
 * @file    buy.c
 * @brief   购买命令 - 公共 buy 动词入口



 *
 * @par Description:
 * buy 的公共动词入口，负责商贩发现、参数转发、结果反馈与商店协议入口。
 * 商品读侧仍归 `SHOP_D` 持有，最终成交价按当前 `economy` 配置结算。
 */

#pragma strict_types

#include <system/ansi.h>
#include <game/command.h>
#include <system/daemon.h>
#include <framework/message_facade.h>

inherit "/lib/framework/network";
inherit "/lib/std/command_utils";

int help(object me);
int main(object me, string arg);

int open_shop_interface(object me, object vendor, mapping response_context);
int process_shop_purchase(object me, object vendor, string arg,
    mapping response_context);


/**
 * @brief 打开商店界面
 * @param me 玩家对象
 * @param vendor 商贩对象
 * @return 1成功
 */
int open_shop_interface(object me, object vendor, mapping response_context)
{
    object shop_d;

    /* SHOP_D 继续负责商品读侧；成交价改由 economy settlement 真链决定。 */
    shop_d = load_shop_service();
    if (objectp(shop_d)) {
        shop_d->open_shop(me, vendor, response_context);
    } else {
        emit_xk_shop_snapshot(me, vendor, response_context);
        msg_send(me, HIY "你打开了「" + get_object_display_name(vendor, "商人") + "」的商店。" NOR + "\n",
            response_context);
    }
    return 1;
}

/**
 * @brief 处理商店守护进程购买
 * @param me 玩家对象
 * @param vendor 商贩对象
 * @param arg 物品名称
 * @return 购买结果
 */
int process_shop_purchase(object me, object vendor, string arg,
    mapping response_context)
{
    object shop_d;

    /* 商品读侧继续由 SHOP_D 承担，最终成交价由 economy settlement 真链决定。 */
    shop_d = load_shop_service();
    if (!objectp(shop_d)) {
        msg_error(me, HIR "商店服务暂时不可用，请稍后再试。" NOR + "\n",
            "buy", "shop_service_unavailable", arg, 0, response_context);
        return 1;
    }

    return shop_d->process_buy(me, vendor, arg, response_context);
}

/**
 * @brief 购买命令主函数
 * @param me 玩家对象
 * @param arg 命令参数
 * @return 购买结果
 */
int main(object me, string arg)
{
    object vendor;
    mapping response_context;

    if (!me) return 0;

    response_context = CLIENT_SYNC_D->capture_command_response_context(me);

    vendor = find_vendor_in_room(me);
    if (!vendor) {
        msg_error(me, "这里没有商贩。\n", "buy", "vendor_not_found", stringp(arg) ? arg : "", 0,
            response_context);
        return 1;
    }

    /* 打烊检查：商贩配置 open_hours 时，非营业时段拒绝交易 */
    if (function_exists("is_shop_closed", vendor) && vendor->is_shop_closed()) {
        msg_send(me, "铺子已经打烊了，伙计隔着门板喊：明儿赶早吧。\n",
            response_context);
        return 1;
    }

    if (!arg || arg == "")
    {
        return open_shop_interface(me, vendor, response_context);
    }

    return process_shop_purchase(me, vendor, arg, response_context);
}

int help(object me)
{
    msg_send(me, @HELP
命令：buy（购买）

从商贩处购买物品。

用法：
  buy           - 打开商店界面（发送XK SHOP协议）
  buy <物品名>  - 购买指定物品（发送XK TRDE协议）
  list          - 查看商品列表

 示例：
  buy           - 打开商店
  buy 馒头      - 购买馒头
  buy 木剑      - 购买木剑

XK协议：
  - SHOP: 商店信息
  - TRDE: 交易结果

 注意：
  - 购买需要足够的铜钱
  - 部分商品有购买限制
  - 商品名有歧义时需要输入更精确的全名
  - 最终成交价按当前 economy 配置结算

相关命令：
  sell     - 出售物品
  list     - 查看商品列表

HELP
    );
    return 1;
}
