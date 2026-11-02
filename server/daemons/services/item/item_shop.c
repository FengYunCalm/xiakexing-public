/**
 * @file    item_shop.c
 * @brief   商店交易服务（真源）



 */

#pragma strict_types
#pragma save_types

#include <system/ansi.h>
#include <system/daemon.h>
#include <framework/message_facade.h>
#include <system/xk_protocol.h>

inherit "/lib/framework/daemon";

#define ITEM_INVENTORY "/daemons/services/item/item_inventory"

private string resolve_goods_id(mapping goods, string item_name);
private string resolve_shop_id(object vendor);
private mapping resolve_shop_config(string shop_id);
private string get_vendor_display_name(object vendor);
private string get_item_display_name(object item);
private object resolve_vendor_for_player(object player);
object find_shop_npc(object player);
int get_sell_price(string shop_id, object item);

private int query_stack_count(object item)
{
    int amount;

    if (!objectp(item))
        return 0;

    /* L-123：非堆叠物品只按 1 件计价。旧实现无条件读 amount/count，
     * 一个带脏字段的非堆叠对象能卖出多份价钱而只交出一件。 */
    if (!item->query("stackable"))
        return 1;
    amount = item->query("amount");
    if (!intp(amount) || amount <= 0)
        amount = item->query("count");
    if (!intp(amount) || amount <= 0)
        amount = 1;
    return amount;
}

private int vendor_accepts_item(object vendor, object item)
{
    mapping goods;
    string template_id;

    if (!objectp(vendor) || !objectp(item))
        return 0;

    goods = vendor->query_all_goods();
    if (!mapp(goods) || sizeof(goods) == 0)
        return 1;

    template_id = item->query("template_id");
    if (!stringp(template_id) || template_id == "")
        return 0;

    return !undefinedp(goods[template_id]);
}

private mapping resolve_goods_match(mapping goods, string item_name)
{
    string goods_id;
    mapping item_data;
    string *matches;

    if (!mapp(goods) || !stringp(item_name) || item_name == "")
        return ([ "id": "", "ambiguous": 0 ]);

    if (mapp(goods[item_name]))
        return ([ "id": item_name, "ambiguous": 0 ]);

    matches = ({ });

    foreach (goods_id, item_data in goods) {
        if (!mapp(item_data))
            continue;
        if (item_data["name"] == item_name)
            return ([ "id": goods_id, "ambiguous": 0 ]);
        if (strsrch(item_data["name"] || "", item_name) != -1)
            matches += ({ goods_id });
    }

    if (sizeof(matches) == 1)
        return ([ "id": matches[0], "ambiguous": 0 ]);

    if (sizeof(matches) > 1)
        return ([ "id": "", "ambiguous": 1 ]);

    return ([ "id": "", "ambiguous": 0 ]);
}

private string resolve_shop_id(object vendor)
{
    string shop_id;

    if (!objectp(vendor))
        return "";

    shop_id = vendor->query("shop_id");
    return stringp(shop_id) ? shop_id : "";
}

private mapping resolve_shop_config(string shop_id)
{
    if (!stringp(shop_id) || shop_id == "")
        return 0;

    return ITEM_D->query_shop(shop_id);
}

private string get_vendor_display_name(object vendor)
{
    if (!objectp(vendor))
        return "商人";

    return get_object_name(vendor) || "商人";
}

private string get_item_display_name(object item)
{
    string name;

    if (!objectp(item))
        return "物品";

    name = get_object_name(item);
    if (!stringp(name) || name == "")
        name = call_other("/lib/std/utils/util_player", "get_object_short_name", item, "");
    if (!stringp(name) || name == "")
        name = "物品";

    return name;
}

private object resolve_vendor_for_player(object player)
{
    if (!objectp(player))
        return 0;

    return find_shop_npc(player);
}

object find_shop_npc(object player)
{
    object env;
    object vendor;

    if (!objectp(player))
        return 0;
    env = environment(player);
    if (!objectp(env))
        return 0;

    /*
     * 商人发现链的目标不是“尽量找到一个对象”，而是“只找到真正商人”：
     * 1. 优先兼容显式 id（vendor / test vendor）
     * 2. 再回退到统一商人契约：npc_type=vendor 或非空 vendor_goods
     * 3. 不再通过 `do_sell()` 之类旧空壳函数猜测商人身份
     */
    vendor = present("vendor", env);
    if (!objectp(vendor))
        vendor = present("test vendor", env);
    if (!objectp(vendor)) {
        foreach (object ob in all_inventory(env)) {
            if (ob->query("npc_type") == "vendor" ||
                (arrayp(ob->query("vendor_goods")) && sizeof(ob->query("vendor_goods")) > 0)) {
                vendor = ob;
                break;
            }
        }
    }
    return vendor;
}

varargs void open_shop(object player, object vendor, mapping response_context)
{
    if (!objectp(player) || !objectp(vendor))
        return;
    emit_xk_shop_snapshot(player, vendor, response_context);
}

varargs int process_buy(object player, object vendor, string item_name,
    mapping response_context)
{
    mapping goods;
    mapping goods_match;
    mapping result;
    mapping price_breakdown;
    string goods_id;
    string shop_id;
    int price;
    string display_name;

    if (!objectp(player) || !objectp(vendor) || !stringp(item_name) || item_name == "")
        return 0;

    /*
     * 购买主链刻意收口在这里：
     * - 玩家命令只负责定位商人与参数
     * - NPC 不再直接承担交易真逻辑
     * - 这里统一处理：商品解析、扣钱、实例化、协议回包、背包/状态刷新
     */
    goods = vendor->query_all_goods();
    if (!mapp(goods) || sizeof(goods) == 0)
        return 0;

    goods_match = resolve_goods_match(goods, item_name);
    if (mapp(goods_match) && goods_match["ambiguous"]) {
        msg_error(player, HIY + "这个名称匹配多个商品，请输入更精确的物品名。" NOR + "\n",
            "buy", "ambiguous_goods", item_name, 0);
        return 1;
    }

    goods_id = mapp(goods_match) ? goods_match["id"] : "";
    if (!stringp(goods_id) || goods_id == "") {
        msg_error(player, HIY + get_vendor_display_name(vendor) + "没有这个商品。" NOR + "\n",
            "buy", "goods_not_found", item_name, 0);
        return 1;
    }

    shop_id = resolve_shop_id(vendor);
    price_breakdown = ECONOMY_SETTLEMENT_D->query_shop_buy_price_breakdown(
        shop_id, player, vendor, goods_id, ITEM_D->get_item_value(goods_id));
    price = mapp(price_breakdown) ? price_breakdown["final_price"] : 0;
    display_name = goods[goods_id]["name"] || goods_id;
    if (price <= 0) {
        msg_error(player, HIY + get_vendor_display_name(vendor) + "暂时不卖这个商品。" NOR + "\n",
            "buy", "goods_unavailable", item_name, 0);
        return 1;
    }

    result = ECONOMY_SETTLEMENT_D->settle_shop_buy(player, goods_id, display_name, price);
    if (mapp(result))
        result["price_breakdown"] = price_breakdown;
    ECONOMY_FEEDBACK_D->emit_shop_settlement_result(player, result,
        response_context);

    return 1;
}

varargs int process_sell(object player, object vendor, object item,
    mapping response_context)
{
    mapping result;
    string template_id;
    string display_name;
    string shop_id;
    int price;

    if (!objectp(player) || !objectp(vendor) || !objectp(item))
        return 0;

    /*
     * 出售链和购买链保持对称：
     * - 玩家命令只做参数与目标定位
     * - 商店服务统一判断是否可卖、计算价格、加钱、销毁物品、回包
     * 这样 NPC 侧就不会再因为空壳 `do_buy()` 造成静默失败。
    */
    if (item->query("no_sell")) {
        msg_error(player, HIR + get_item_display_name(item) + "无法出售。" NOR + "\n",
            "sell", "no_sell", get_item_display_name(item), 0);
        return 0;
    }

    if (!vendor_accepts_item(vendor, item)) {
        msg_error(player, HIY + get_vendor_display_name(vendor) + "不收这个东西。" NOR + "\n",
            "sell", "vendor_reject", get_item_display_name(item), 0);
        return 0;
    }

    template_id = item->query("template_id");
    display_name = get_item_display_name(item);
    shop_id = resolve_shop_id(vendor);

    if (!stringp(template_id) || template_id == "") {
        price = ECONOMY_SETTLEMENT_D->query_shop_sell_price(shop_id, item,
            item->query("value") || 0) * query_stack_count(item);
    } else {
        price = get_sell_price(shop_id, item) * query_stack_count(item);
    }

    if (price <= 0) {
        msg_error(player, HIY + get_vendor_display_name(vendor) + "不收这个东西。" NOR + "\n",
            "sell", "price_unavailable", get_item_display_name(item), 0);
        return 0;
    }

    result = ECONOMY_SETTLEMENT_D->settle_shop_sell(player, item, display_name, price);
    ECONOMY_FEEDBACK_D->emit_shop_settlement_result(player, result,
        response_context);

    return 1;
}

int register_item_template(mapping template)
{
    return ITEM_D->register_item_template(template);
}

mapping query_item_template(string item_id)
{
    return ITEM_D->query_item_template(item_id);
}

int item_exists(string item_id)
{
    return ITEM_D->item_exists(item_id);
}

int get_item_value(string item_id)
{
    return ITEM_D->get_item_value(item_id);
}

string get_item_name(string item_id)
{
    return ITEM_D->get_item_name(item_id);
}

object create_item(string item_id, int count)
{
    return ITEM_D->create_item(item_id, count);
}

object clone_item(object item)
{
    return call_other(ITEM_INVENTORY, "clone_item", item);
}

int can_stack(object item1, object item2)
{
    return call_other(ITEM_INVENTORY, "can_stack", item1, item2);
}

int merge_items(object dest, object src)
{
    return call_other(ITEM_INVENTORY, "merge_items", dest, src);
}

int get_sell_price(string shop_id, object item)
{
    int base_price;
    string template_id;

    if (!objectp(item))
        return 0;

    template_id = item->query("template_id");
    base_price = stringp(template_id) && template_id != ""
        ? ITEM_D->get_item_value(template_id)
        : (item->query("value") || 0);

    return ECONOMY_SETTLEMENT_D->query_shop_sell_price(shop_id, item, base_price);
}

void setup_default_shops()
{
    ITEM_D->setup_default_shops();
}


/* ===== 领域协议发送器（自 kernel/simul_efun_trade_xk.c 迁入，逐字保留） ===== */

#define XK_PROTOCOL_CORE "/kernel/framework/xk_protocol_core"

private mapping build_shop_item_payload(string item_id, mapping item_data)
{
    return CLIENT_PROJECTION_D->build_shop_item_payload(item_id, item_data);
}

private string *resolve_shop_item_order(object vendor, mapping goods)
{
    mixed goods_order;
    mixed entry;
    string *ordered_ids;

    if (!objectp(vendor) || !mapp(goods))
        return ({ });

    ordered_ids = ({ });
    goods_order = vendor->query("vendor_goods");
    if (arrayp(goods_order)) {
        foreach (entry in goods_order) {
            if (!stringp(entry) || entry == "" || undefinedp(goods[entry]))
                continue;
            if (member_array(entry, ordered_ids) != -1)
                continue;
            ordered_ids += ({ entry });
        }
    }

    foreach (entry in keys(goods)) {
        if (!stringp(entry) || entry == "")
            continue;
        if (member_array(entry, ordered_ids) != -1)
            continue;
        ordered_ids += ({ entry });
    }

    return ordered_ids;
}

varargs void xk_emit_shop_snapshot(object me, object vendor,
    mapping response_context)
{
    mapping goods;
    mixed npc_ids;
    string *ordered_ids;
    mixed *items_payload;
    string shop_name;
    string npc_id;
    int index;

    if (!me || !interactive(me) || !objectp(vendor))
        return;
    goods = vendor->query_all_goods();
    if (!mapp(goods)) {
        call_other(XK_PROTOCOL_CORE, "xk_send", me, XK_SHOP,
            ([ "shop_name": "", "npc_id": "", "items": ({ }) ]),
            response_context);
        return;
    }

    shop_name = vendor->query_name() || vendor->query("name") || "";
    npc_ids = vendor->query_id();
    if (arrayp(npc_ids) && sizeof(npc_ids) > 0)
        npc_id = npc_ids[0];
    else if (stringp(npc_ids))
        npc_id = npc_ids;
    else
        npc_id = "vendor";
    items_payload = ({ });
    ordered_ids = resolve_shop_item_order(vendor, goods);
    for (index = 0; index < sizeof(ordered_ids); index++) {
        mapping item_data;
        mapping item_payload;
        string item_id;

        item_data = goods[ordered_ids[index]];
        if (!mapp(item_data))
            continue;

        item_id = ordered_ids[index];
        item_payload = build_shop_item_payload(item_id, item_data);
        if (mapp(item_payload))
            items_payload += ({ item_payload });
    }

    call_other(XK_PROTOCOL_CORE, "xk_send", me, XK_SHOP, ([
        "shop_name": shop_name,
        "npc_id": npc_id,
        "items": items_payload,
    ]), response_context);
}
