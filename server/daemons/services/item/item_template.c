/**
 * @file    item_template.c
 * @brief   物品模板管理
 *
 * @par Description:
 * 管理物品模板的注册、查询与区域商店配置。
 */

#pragma strict_types
#pragma save_types

#include <system/globals.h>
#include <system/constants.h>
#include <system/services.h>

#ifndef ITEM_QUALITY_COMMON
#define ITEM_QUALITY_COMMON 1
#endif

#ifndef ITEM_QUALITY_UNCOMMON
#define ITEM_QUALITY_UNCOMMON 2
#endif

#ifndef ITEM_QUALITY_RARE
#define ITEM_QUALITY_RARE 3
#endif

#ifndef ITEM_QUALITY_EPIC
#define ITEM_QUALITY_EPIC 4
#endif

void register_item_template(mapping template);
int register_shop_config(string shop_id, mapping shop_data);
string *query_all_shop_ids();
private void ensure_content_callback();

private nosave mapping _templates = ([]);
private nosave mapping _shop_inventories = ([]);
private nosave int _loaded_generation = 0;
private nosave int _callback_registered = 0;

private void add_item_template(mapping target, mapping template)
{
    string id;

    if (!mapp(target) || !mapp(template))
        return;
    id = template["id"];
    if (!stringp(id) || id == "")
        return;
    target[id] = copy(template);
}

private void add_shop_config(mapping target, string shop_id, mapping shop_data)
{
    if (!mapp(target) || !stringp(shop_id) || shop_id == "" || !mapp(shop_data))
        return;
    target[shop_id] = copy(shop_data);
}

private void load_currency_templates(mapping target)
{
    add_item_template(target, ([
        "id": "item_coin_copper",
        "name": "铜钱",
        "aliases": ({ "copper", "coin", "铜板" }),
        "type": "currency",
        "quality": ITEM_QUALITY_COMMON,
        "description": "一枚通行市井的铜钱。",
        "weight": 0,
        "stackable": 1,
        "max_stack": 999999,
        "base_unit": "文",
        "value": 1,
        "no_sell": 1,
        "effects": ([]),
        "requirements": ([])
    ]));

    add_item_template(target, ([
        "id": "item_coin_silver",
        "name": "银两",
        "aliases": ({ "silver", "银子" }),
        "type": "currency",
        "quality": ITEM_QUALITY_COMMON,
        "description": "一两成色普通的银子，可折一百文铜钱。",
        "weight": 0,
        "stackable": 1,
        "max_stack": 999999,
        "base_unit": "两",
        "value": 100,
        "no_sell": 1,
        "effects": ([]),
        "requirements": ([])
    ]));

    add_item_template(target, ([
        "id": "item_coin_gold",
        "name": "黄金",
        "aliases": ({ "gold", "金子" }),
        "type": "currency",
        "quality": ITEM_QUALITY_COMMON,
        "description": "一两沉甸甸的黄金，可折一万文铜钱。",
        "weight": 0,
        "stackable": 1,
        "max_stack": 999999,
        "base_unit": "两",
        "value": 10000,
        "no_sell": 1,
        "effects": ([]),
        "requirements": ([])
    ]));
}

private void load_content_item_templates(mapping target)
{
    mapping templates;
    mapping template;
    string template_id;

    templates = CONFIG_D->query_config("content", "item_templates");
    if (!mapp(templates))
        return;

    foreach (template_id, template in templates) {
        if (!stringp(template_id) || template_id == "" || !mapp(template))
            continue;
        template = copy(template);
        if (!stringp(template["id"]) || template["id"] == "")
            template["id"] = template_id;
        add_item_template(target, template);
    }
}

private void load_content_shop_configs(mapping target)
{
    mapping shops;
    mapping shop;
    string shop_id;

    shops = CONFIG_D->query_config("content", "shops");
    if (!mapp(shops))
        return;

    foreach (shop_id, shop in shops) {
        if (!stringp(shop_id) || shop_id == "" || !mapp(shop))
            continue;
        shop = copy(shop);
        if (!stringp(shop["id"]) || shop["id"] == "")
            shop["id"] = shop_id;
        add_shop_config(target, shop_id, shop);
    }
}

private mapping build_item_template_snapshot()
{
    mapping templates;

    templates = ([]);
    load_currency_templates(templates);
    load_content_item_templates(templates);
    return templates;
}

private mapping build_shop_snapshot()
{
    mapping shops;

    shops = ([]);
    load_content_shop_configs(shops);
    return shops;
}

/**
 * @brief Content reload callback. The next read rebuilds both registries from
 *       the new content generation, so deleted ids cannot survive in nosave state.
 */
void on_config_reloaded(string key, string config_name)
{
    if (key != "content" && config_name != "content")
        return;

    _loaded_generation = 0;
    _templates = ([]);
    _shop_inventories = ([]);
}

void on_config_reload_rollback(string key, string config_name)
{
    if (key != "content" && config_name != "content")
        return;

    _loaded_generation = 0;
    _templates = ([]);
    _shop_inventories = ([]);
}

/**
 * @brief 按 content generation 原子重建物品与商店快照。
 * @note 货币模板是经济服务真源，重建时始终保留；区域内容则完全来自
 *       当前 CONFIG_D 快照，不能通过增量写入保留已删除的 key。
 */
void load_item_templates()
{
    mapping templates;
    mapping shops;
    int content_generation;

    ensure_content_callback();
    content_generation = CONTENT_REGISTRY_D->query_content_generation();
    if (content_generation > 0 && _loaded_generation == content_generation)
        return;

    templates = build_item_template_snapshot();
    shops = build_shop_snapshot();
    _templates = templates;
    _shop_inventories = shops;
    _loaded_generation = content_generation;
}

private void ensure_content_callback()
{
    if (_callback_registered)
        return;
    CONFIG_D->compat_register_config_callback("content", "on_config_reloaded", this_object());
    _callback_registered = 1;
}

/**
 * @brief 加载并注册额外的运行时模板。
 * @param template 模板数据
 */
void register_item_template(mapping template)
{
    string id;

    if (!mapp(template))
        return;

    id = template["id"];
    if (!stringp(id) || id == "")
        return;

    _templates[id] = copy(template);
}

mapping query_item_template(string id)
{
    if (!stringp(id))
        return 0;

    load_item_templates();
    return copy(_templates[id]);
}

string *query_all_template_ids()
{
    load_item_templates();
    return keys(_templates);
}

int template_exists(string id)
{
    load_item_templates();
    return stringp(id) && mapp(_templates[id]);
}

/**
 * @brief 区域包商店配置的公开加载入口。
 * @note 不再注入硬编码默认商店，货币之外的内容均从当前快照读取。
 */
void setup_default_shops()
{
    load_item_templates();
}

int register_shop_config(string shop_id, mapping shop_data)
{
    if (!stringp(shop_id) || shop_id == "" || !mapp(shop_data))
        return 0;

    _shop_inventories[shop_id] = copy(shop_data);
    return 1;
}

mapping query_shop_config(string shop_id)
{
    if (!stringp(shop_id))
        return 0;

    load_item_templates();
    return copy(_shop_inventories[shop_id]);
}

string *query_all_shop_ids()
{
    load_item_templates();
    return keys(_shop_inventories);
}
