/**
 * @file    item_inventory.c
 * @brief   玩家物品生命周期真链门面
 *
 * @par Description:
 * 对外保持 ITEM_INVENTORY_D 单一入口；内部按公共 helper、堆叠入包、展示/拾取/丢弃、
 * 给予/转移/附件、装备和消耗拆分。
 */

#pragma strict_types
#pragma save_types

inherit "/lib/framework/daemon";
inherit "/daemons/services/item/item_inventory_common";
inherit "/daemons/services/item/item_inventory_stack";
inherit "/daemons/services/item/item_inventory_view";
inherit "/daemons/services/item/item_inventory_transfer";
inherit "/daemons/services/item/item_inventory_equipment";
inherit "/daemons/services/item/item_inventory_consume";

/**
 * 丢弃尚未进入任何容器的临时物品；创建型命令不得直接拥有对象销毁权。
 */
void discard_unplaced_item(object item)
{
    if (objectp(item) && !environment(item))
        destruct(item);
}
