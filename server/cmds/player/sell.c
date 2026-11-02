/**
 * @file    sell.c
 * @brief   出售命令 - 公共 sell 动词入口



 *
 * @par Description:
 * sell 的公共动词入口，负责目标商贩查找、物品解析、结果反馈与商店协议更新。
 * 出售判定与最终成交价按当前 `economy` 配置结算。
 */

#pragma strict_types

#include <system/ansi.h>
#include <game/command.h>
#include <framework/message_facade.h>

inherit "/lib/framework/network";
inherit "/lib/std/command_utils";

int help(object me);
int main(object me, string arg);

object find_vendor_in_room(object me);
private object find_item_by_name(object me, string item_name);
private int sell_single_item(object me, object vendor, string item_name,
    mapping response_context);
private int sell_all_items(object me, object vendor, mapping response_context);
private void send_sell_updates(object me);

private object find_item_by_name(object me, string item_name)
{
    if (!objectp(me) || !stringp(item_name) || item_name == "")
        return 0;

    return find_item_in_collection(all_inventory(me), item_name);
}


/**
 * @brief 出售单个物品
 * @param me 玩家对象
 * @param vendor 商贩对象
 * @param item_name 物品名称
 * @return 1=成功, 0=失败
 */
private int sell_single_item(object me, object vendor, string item_name,
    mapping response_context)
{
    object item;
    object shop_d;

    item = find_item_by_name(me, item_name);

    if (!item)
    {
        msg_error(me, "你身上没有「" + item_name + "」。\n", "sell", "item_not_found", item_name, 0,
            response_context);
        return 1;
    }

    if (function_exists("query_equipped", item) && item->query_equipped())
    {
        ECONOMY_FEEDBACK_D->emit_trade_failure_result(
            me,
            "sell",
            item,
            HIR + get_object_display_name(item, "该物品") + "已装备，请先卸下。" NOR + "\n",
            response_context
        );
        return 1;
    }

    /* 估价、是否可卖和结算都以 SHOP_D / ECONOMY_SETTLEMENT_D 为准。 */
    shop_d = load_shop_service();
    if (!objectp(shop_d)) {
        msg_error(me, HIR "商店服务暂时不可用，请稍后再试。" NOR + "\n",
            "sell", "shop_service_unavailable", item_name, 0, response_context);
        return 1;
    }

    if (!shop_d->process_sell(me, vendor, item, response_context))
    {
        return 1;
    }

    send_xk_quick_cmds(me,
        "操作|继续出售:sell|查看商品:list|查看物品:i",
        1, response_context);

    return 1;
}

/**
 * @brief 出售所有可卖物品
 * @param me 玩家对象
 * @param vendor 商贩对象
 * @return 出售数量
 */
private int sell_all_items(object me, object vendor, mapping response_context)
{
    object *items = all_inventory(me);
    object shop_d;
    int count = 0;
    int i;
    int item_count = sizeof(items);

    shop_d = load_shop_service();
    if (!objectp(shop_d)) {
        msg_error(me, HIR "商店服务暂时不可用，请稍后再试。" NOR + "\n",
            "sell", "shop_service_unavailable", "all", 0, response_context);
        return 0;
    }

    for (i = 0; i < item_count; i++)
    {
        if (function_exists("query_equipped", items[i]) && items[i]->query_equipped())
            continue;

        /* 批量出售沿用同一真源，避免 all 分支和单件分支漂移。 */
        if (shop_d->process_sell(me, vendor, items[i], response_context))
            count++;
    }

    return count;
}

/**
 * @brief 出售命令主函数
 * @param me 玩家对象
 * @param arg 命令参数
 * @return 1=成功, 0=失败
 */
int main(object me, string arg)
{
    object vendor;
    int result;
    mapping response_context;

    if (!me)
        return 0;

    response_context = CLIENT_SYNC_D->capture_command_response_context(me);

    if (!arg || arg == "")
    {
        msg_error(me, "卖什么？\n", "sell", "missing_item", "", 0, response_context);
        return 1;
    }

    vendor = find_vendor_in_room(me);
    if (!vendor)
    {
        msg_error(me, "这里没有商贩。\n", "sell", "vendor_not_found", arg, 0, response_context);
        return 1;
    }

    if (arg == "all")
    {
        result = sell_all_items(me, vendor, response_context);

        if (result <= 0)
        {
            msg_error(me, "没有可出售的物品。\n", "sell", "no_sellable_item", "all", 0);
            return 1;
        }

        msg_system(me, ECONOMY_FEEDBACK_D->format_bulk_sell_notice(result), "bulk_sell",
            response_context);
        return 1;
    }

    return sell_single_item(me, vendor, arg, response_context);
}

int help(object me)
{
    msg_send(me, @HELP
命令：sell（出售）

向商贩出售物品换取银两。

用法：
  sell <物品名>  - 出售指定物品
  sell all       - 出售所有可卖物品

示例：
  sell 剑        - 出售剑
  sell all       - 出售所有可卖物品

 注意：
  - 已装备的物品需要先卸下
  - 绑定物品无法出售
  - 最终成交价按当前 economy 配置结算

相关命令：
  buy     - 购买物品
  list    - 查看商品列表

HELP
    );
    return 1;
}
