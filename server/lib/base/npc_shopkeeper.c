/**
 * @file    npc_shopkeeper.c
 * @brief   通用商店伙计 NPC base（北京/扬州店伙共享）。
 *
 * 点击 → 弹窗（谈买卖）→ 货架列表（list）→ 点商品买/卖，全点击交互。
 * 商品由 npcs.c 模板的 vendor_goods 提供（物品库 ID 列表），
 * 价格走 ECONOMY_SETTLEMENT_D，商店收购/出售率在 shops.c 配置。
 *
 * 原 beijing/npc_beijing_shopkeeper.c 与 yangzhou/npc_yz_shopkeeper.c
 * 内容逐字节相同，收敛为本 base；两区域文件改为薄壳继承，避免同一规则两份手写。
 */

#pragma strict_types

#include <system/services.h>

inherit "/lib/base/npc";

private string *coerce_ids(mixed value)
{
    string *result;
    mixed entry;

    result = ({ });
    if (!arrayp(value))
        return result;
    foreach (entry in value) {
        if (stringp(entry) && entry != "")
            result += ({ entry });
    }
    return result;
}


void create()
{
    ::create();
    set_name("店伙计");
    set("id", ({ "shopkeeper", "伙计", "店伙计" }));
    set("npc_type", "vendor");
    set("role", "vendor");
    set("long", "一个手脚麻利的店伙计，见你进来便抬了抬眼。\n");
    set("functions", ({ "talk", "ask", "list", "buy", "sell" }));
    set("no_fight", 1);
    set("combat/max_hp", 100);
    set("combat/hp", 100);
    set("combat/max_mp", 80);
    set("combat/mp", 80);
    set("combat/max_ep", 50);
    set("combat/ep", 50);
}

int apply_content_entity(mapping entity)
{
    mapping shop;
    mapping shops;
    mapping template;
    mixed goods;
    string present_id;
    string shop_id;
    string template_id;
    string *ids;
    mixed value;
    int ok;

    if (!mapp(entity))
        return 0;
    shop_id = stringp(entity["shop_id"]) ? entity["shop_id"] : "";
    template_id = stringp(entity["template_id"]) ? entity["template_id"] : "";
    present_id = stringp(entity["present_id"]) ? entity["present_id"]
        : (stringp(entity["id"]) ? entity["id"] : "");
    template = CONTENT_REGISTRY_D->query_npc_template(template_id);
    if (!mapp(template))
        return 0;
    /* shop_id 优先取 spawn 配置，空则回退模板 shop_id（模板是权威）。
     * 此前只读 entity，spawn 配置未带 shop_id 的商贩会拿不到商品（购买静默失效）。 */
    if (shop_id == "")
        shop_id = stringp(template["shop_id"]) ? template["shop_id"] : "";
    shops = CONFIG_D->query_config("content", "shops");
    shop = mapp(shops) ? shops[shop_id] : 0;
    ids = ({ present_id });
    if (arrayp(template["ids"]))
        ids += coerce_ids(template["ids"]);
    ids = coerce_ids(ids);
    ok = set("id", ids);
    set_name(stringp(template["name"]) && template["name"] != ""
        ? template["name"] : "店伙计");
    ok = set("long", stringp(template["long"]) && template["long"] != ""
        ? template["long"] : "一个手脚麻利的店伙计，见你进来便抬了抬眼。\n") && ok;
    ok = set("npc_type", stringp(template["npc_type"])
        && template["npc_type"] != "" ? template["npc_type"] : "vendor") && ok;
    ok = set("role", stringp(template["role"]) && template["role"] != ""
        ? template["role"] : "vendor") && ok;
    ok = set("functions", arrayp(template["functions"])
        ? template["functions"] : ({ "talk", "ask", "list", "buy", "sell" })) && ok;

    if (shop_id != "")
        ok = set("shop_id", shop_id) && ok;
    else if (query("shop_id"))
        ok = delete("shop_id") && ok;

    /* Content reloads must replace the runtime inventory, including an empty
     * list. Never leave goods from the previous generation on a live NPC. */
    goods = ({ });
    if (mapp(shop)) {
        if (arrayp(shop["goods"]))
            goods = shop["goods"];
        else if (arrayp(shop["default_items"]))
            goods = shop["default_items"];
    }
    goods = coerce_ids(goods);
    ok = set("vendor_goods", goods) && ok;
    value = entity["loot_table_id"];
    if (stringp(value) && value != "")
        ok = set("loot_table_id", value) && ok;
    else if (query("loot_table_id"))
        ok = delete("loot_table_id") && ok;
    value = entity["quest_kill_target"];
    if (stringp(value) && value != "")
        ok = set("quest_kill_target", value) && ok;
    else if (query("quest_kill_target"))
        ok = delete("quest_kill_target") && ok;
    return ok ? 1 : 0;
}

/* 打烊：商店配置 open_hours（{开门时辰, 关门时辰}）时按时辰判定 */
int is_shop_closed()
{
    mapping shop;
    string shop_id;
    int *hours;

    shop_id = query("shop_id") || "";
    if (shop_id == "")
        return 0;
    shop = ITEM_D->query_shop(shop_id);
    if (!mapp(shop))
        return 0;
    hours = shop["open_hours"];
    if (!arrayp(hours) || sizeof(hours) != 2)
        return 0;
    return !TIME_D->is_shop_open(([ "open_hours": hours ]));
}

int do_talk(string arg)
{
    string text;
    string *clues;
    mapping goods;
    mapping *groups;

    goods = query_all_goods();
    if (sizeof(keys(goods)) > 0) {
        text = query_name() + "招呼你：看看有什么合用的？\n";
    } else {
        text = query_name() + "陪了个笑脸：货还没上齐，您改天再来。\n";
    }

    groups = ({ });
    /* 当铺脏货支线：偷听过米铺闲话后，朝奉话里有了分寸 */
    clues = this_player()->query("listen/clues");
    if (arrayp(clues) && member_array("pawn_dirty_goods", clues) != -1) {
        text += query_name() + "压低了声音：听说你耳朵挺灵。铺子里的买卖，讲究个来路清白。有些东西，柜台上不便细说——你要是手头有「来路不明」的货，夜里来，我按老规矩收。\n";
    }

    return present_interaction(this_player(), query_name(), text,
        query_primary_id() + "_shop", ([
            "source": "shopkeeper",
            "dialog_bucket": "shop",
            "action_groups": groups,
        ]));
}
