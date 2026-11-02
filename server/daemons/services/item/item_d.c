/**
 * @file    item_d.c
 * @brief   物品服务真源



 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

private nosave object _template_service;

mapping query_item_template(string item_id);
object create_item(string item_id, int count);
object create_item_with_affixes(string item_id, int count);
void ensure_item_identity(object item);
string query_binding_owner_id(object owner, string binding_policy);
private void roll_equipment_affixes(object item);

#define EQUIPMENT_AFFIXES_CONFIG  "/config/equipment_affixes"
#define AFFIX_ROLL_MAX_ATTEMPTS   8

private object resolve_item_template_service()
{
    object template_d;

    if (objectp(_template_service))
        return _template_service;

    template_d = find_object("/daemons/services/item/item_template");
    if (!objectp(template_d))
        template_d = load_object("/daemons/services/item/item_template");

    if (objectp(template_d))
        _template_service = template_d;

    return template_d;
}

mapping query_item_owner_route(string item_id, string route_key)
{
    mapping route;
    string key;
    mixed err;

    key = stringp(route_key) && route_key != ""
        ? route_key
        : (stringp(item_id) && item_id != "" ? item_id : "item");
    err = catch(route = OWNER_RUNTIME_D->query_service_owner_route("item", key));
    if (err || !mapp(route) || route["ok"] != 1)
        return ([
            "ok": 0,
            "service": "item",
            "key": key,
            "item_id": stringp(item_id) ? item_id : "",
            "failure_reason": err ? sprintf("owner route query failed: %O", err)
                : (mapp(route) ? route["failure_reason"] : "owner route unavailable"),
            "readiness": mapp(route) ? route["readiness"] : ([ "ok": 0 ]),
            "dependencies": mapp(route) ? route["dependencies"] : ([ ]),
            "direct_cross_owner_write": 0,
        ]);

    route["item_id"] = stringp(item_id) ? item_id : "";
    route["same_owner_direct_only"] = 1;
    route["direct_cross_owner_write"] = 0;
    return route;
}

mapping stamp_item_owner_route(object item, string item_id, string reason)
{
    mapping route;

    if (!objectp(item))
        return ([ "ok": 0, "reason": "item is required" ]);

    route = query_item_owner_route(item_id, file_name(item));
    item->set_temp("owner/item_route", route);
    item->set_temp("owner/item_route_reason", stringp(reason) ? reason : "");
    return route;
}

void bind_item_owner(object item, object owner, string reason)
{
    string binding_policy;
    string owner_id;

    if (!objectp(item) || !objectp(owner))
        return;
    ensure_item_identity(item);
    binding_policy = item->query("acquisition/binding_policy")
        || item->query("binding_policy") || "none";
    item->set("binding_policy", binding_policy);
    if (binding_policy == "none") {
        item->set("owner_id", "");
        return;
    }
    owner_id = query_binding_owner_id(owner, binding_policy);
    if (owner_id == "")
        return;
    item->set("owner_id", owner_id);
    item->set("ownership/provenance", stringp(reason) ? reason : "");
}

string query_binding_owner_id(object owner, string binding_policy)
{
    string owner_id;

    if (!objectp(owner) || binding_policy == "none")
        return "";
    if (binding_policy == "account" && function_exists("query_account_id", owner)) {
        owner_id = owner->query_account_id();
        if (stringp(owner_id) && owner_id != "")
            return owner_id;
    }
    owner_id = get_object_id(owner);
    return stringp(owner_id) ? owner_id : "";
}

void ensure_item_identity(object item)
{
    mixed raw_ids;
    string template_id;

    if (!objectp(item))
        return;
    if (!stringp(item->query("template_id"))
        || item->query("template_id") == "") {
        raw_ids = function_exists("query_id", item)
            ? item->query_id() : get_object_id(item);
        if (stringp(raw_ids))
            template_id = raw_ids;
        else if (arrayp(raw_ids) && sizeof(raw_ids) > 0
            && stringp(raw_ids[0]))
            template_id = raw_ids[0];
        if (stringp(template_id) && template_id != "")
            item->set("template_id", template_id);
    }
    if (!stringp(item->query("instance_id"))
        || item->query("instance_id") == "")
        item->set("instance_id", hash("sha256", sprintf(
            "item:%s:%d:%d:%s", item->query("template_id") || "item",
            time(), random(2147483647), file_name(item))));
    if (!stringp(item->query("owner_id")))
        item->set("owner_id", "");
    if (!stringp(item->query("binding_policy"))
        || item->query("binding_policy") == "")
        item->set("binding_policy",
            item->query("acquisition/binding_policy") || "none");
}

private string resolve_item_base_path(mapping template)
{
    string item_type;

    if (!mapp(template))
        return "/lib/base/item/item";

    item_type = template["type"] || "";
    switch (item_type) {
        case "weapon":
            return "/lib/base/item/weapon";

        case "armor":
            return "/lib/base/item/armor";
    }

    return "/lib/base/item/item";
}

private int normalize_item_quality(mixed raw_quality)
{
    if (intp(raw_quality) && raw_quality > 0)
        return raw_quality;

    if (stringp(raw_quality)) {
        switch (raw_quality) {
            case "uncommon":
                return 2;
            case "common":
            default:
                return 1;
        }
    }

    return 1;
}

/* Only this service knows the retired template and instance marker. */
private int template_is_training_weapon(mapping template)
{
    return mapp(template) && (template["training_weapon"]
        || template["beta0_starter_weapon"]);
}

int is_training_weapon(object item)
{
    if (!objectp(item))
        return 0;
    return item->query("training_weapon")
        || item->query("beta0_starter_weapon");
}

private void apply_template_to_item(object item, string item_id, mapping template, int count)
{
    mapping effects;
    mapping requirements;
    mixed aliases;
    string *ids;
    string item_type;

    if (!objectp(item) || !stringp(item_id) || !mapp(template))
        return;

    item_type = template["type"] || "material";
    effects = mapp(template["effects"]) ? copy(template["effects"]) : ([]);
    requirements = mapp(template["requirements"]) ? copy(template["requirements"]) : ([]);
    aliases = arrayp(template["aliases"]) ? template["aliases"] : ({ });
    ids = ({ item_id });
    if (stringp(template["name"]) && template["name"] != "")
        ids += ({ template["name"] });
    foreach (string alias in aliases) {
        if (stringp(alias) && alias != "" && member_array(alias, ids) == -1)
            ids += ({ alias });
    }

    item->set_name(template["name"] || item_id);
    item->set("id", ids);
    item->set("template_id", item_id);
    item->set("instance_id", hash("sha256", sprintf(
        "item:%s:%d:%d:%s", item_id, time(), random(2147483647),
        file_name(item))));

    if (function_exists("set_item_type", item))
        item->set_item_type(item_type);
    else
        item->set("item_type", item_type);

    item->set("description", template["description"] || "没有什么特别的。");
    item->set("long", template["description"] || "没有什么特别的。");

    if (function_exists("set_value", item))
        item->set_value(template["value"] || 0);
    else
        item->set("value", template["value"] || 0);

    if (function_exists("set_weight", item))
        item->set_weight(template["weight"] || 1);
    else
        item->set("weight", template["weight"] || 1);

    item->set("quality", normalize_item_quality(template["quality"]));
    item->set("effects", effects);
    item->set("requirements", requirements);
    item->set("base_unit", template["base_unit"] || "个");
    item->set("no_sell", template["no_sell"] ? 1 : 0);
    item->set("no_give", template["no_give"] ? 1 : 0);
    item->set("no_drop", template["no_drop"] ? 1 : 0);
    item->set("no_get", template["no_get"] ? 1 : 0);
    item->set("unique_per_player", template["unique_per_player"] ? 1 : 0);
    item->set("acquisition/provenance", template["acquisition/provenance"] || ([]));
    item->set("provenance", template["acquisition/provenance"] || ([]));
    item->set("acquisition/binding_policy", template["binding_policy"] || "character");
    item->set("binding_policy", template["binding_policy"] || "character");
    item->set("owner_id", "");
    item->set("acquisition/usage_modes", template["usage_modes"] || ({ }));
    item->set("training_weapon", template_is_training_weapon(template) ? 1 : 0);
    item->set("required_skill_cap", requirements["skill_cap"] || 0);
    item->set("consume_action", template["consume_action"] || "");
    if (stringp(template["manual_id"]) && template["manual_id"] != "")
        item->set("manual_id", template["manual_id"]);
    item->set("stackable", template["stackable"] ? 1 : 0);
    item->set("max_stack", template["max_stack"] || 1);
    /* L-123：数量只属于可堆叠物品。非堆叠模板即使被调用方传入 count，
     * 也不得留下 amount/count，否则售价/展示会把它当成多件。 */
    if (template["stackable"]) {
        item->set("amount", count);
        item->set("count", count);
    } else {
        item->set("amount", 0);
        item->set("count", 0);
    }

    switch (item_type) {
        case "weapon":
            if (function_exists("set_max_durability", item))
                item->set_max_durability(template["max_durability"] || 100);
            if (function_exists("set_durability", item))
                item->set_durability(template["durability"] || (template["max_durability"] || 100));
            if (intp(effects["damage"]) && function_exists("set_damage", item))
                item->set_damage(effects["damage"]);
            if (stringp(effects["skill"]) && function_exists("set_skill_type", item))
                item->set_skill_type(effects["skill"]);
            break;

        case "armor":
            if (function_exists("set_max_durability", item))
                item->set_max_durability(template["max_durability"] || 100);
            if (function_exists("set_durability", item))
                item->set_durability(template["durability"] || (template["max_durability"] || 100));
            if (stringp(template["armor_type"]) && function_exists("set_armor_type", item))
                item->set_armor_type(template["armor_type"]);
            if (intp(effects["armor"]) && function_exists("set_defense", item))
                item->set_defense(effects["armor"]);
            break;
    }
}

mapping query_item_templates()
{
    object template_d;
    mapping templates;
    string *template_ids;
    string item_id;

    template_d = resolve_item_template_service();
    if (!objectp(template_d))
        return ([]);

    template_d->load_item_templates();
    template_ids = template_d->query_all_template_ids();
    templates = ([]);
    foreach (item_id in template_ids)
        templates[item_id] = template_d->query_item_template(item_id);

    return templates;
}

mapping query_shop_inventories()
{
    object template_d;
    mapping shops;
    string *shop_ids;
    string shop_id;

    template_d = resolve_item_template_service();
    if (!objectp(template_d))
        return ([]);

    template_d->setup_default_shops();
    shop_ids = template_d->query_all_shop_ids();
    shops = ([]);
    foreach (shop_id in shop_ids)
        shops[shop_id] = template_d->query_shop_config(shop_id);

    return shops;
}

void create()
{
    ::create();
    resolve_item_template_service();
}

int register_item_template(mapping template)
{
    object template_d;

    template_d = resolve_item_template_service();
    if (!objectp(template_d))
        return 0;

    return template_d->register_item_template(template);
}

mapping query_item_template(string item_id)
{
    object template_d;

    template_d = resolve_item_template_service();
    if (!objectp(template_d))
        return 0;

    template_d->load_item_templates();
    return template_d->query_item_template(item_id);
}

int item_exists(string item_id)
{
    object template_d;

    template_d = resolve_item_template_service();
    if (!objectp(template_d))
        return 0;

    template_d->load_item_templates();
    return template_d->template_exists(item_id);
}

int can_actor_use_item(object actor, object item)
{
    mapping definition;
    string item_id;

    if (!objectp(actor) || !objectp(item))
        return 0;
    item_id = item->query("template_id") || "";
    if (item_id == "")
        return 1;
    definition = query_item_template(item_id);
    if (!mapp(definition))
        return 0;
    return CONTENT_MODE_POLICY_D->is_content_usable(actor, "item", definition);
}

int get_item_value(string item_id)
{
    mapping template;

    template = query_item_template(item_id);
    return mapp(template) ? (template["value"] || 0) : 0;
}

string get_item_name(string item_id)
{
    mapping template;

    template = query_item_template(item_id);
    return mapp(template) ? (template["name"] || "未知物品") : "未知物品";
}

object create_item(string item_id, int count)
{
    mapping template;
    object item;
    string item_path;

    if (!stringp(item_id) || count <= 0)
        return 0;

    template = query_item_template(item_id);
    if (!mapp(template))
        return 0;

    item_path = resolve_item_base_path(template);
    item = new(item_path);
    if (!objectp(item))
        return 0;

    apply_template_to_item(item, item_id, template, count);
    stamp_item_owner_route(item, item_id, "create_item");
    return item;
}

object create_item_with_affixes(string item_id, int count)
{
    object item;

    item = create_item(item_id, count);
    if (objectp(item))
        roll_equipment_affixes(item);
    return item;
}

/* 坏增产出时按装备预算 roll 词条（仅 weapon/armor 且品质高于 COMMON）。
 * 词条写物品 affixes 字段，战斗侧由 combat_math 聚合；description 追加效果行。 */
private void roll_equipment_affixes(object item)
{
    mapping affix_pool;
    mapping budget;
    mixed *candidates;
    mixed *rolled;
    string *used_ids;
    string *used_groups;
    string item_type;
    string affix_id;
    string desc;
    int affix_limit;
    int max_affixes;
    int remaining;
    int cost;
    int i;

    if (!objectp(item))
        return;
    item_type = item->query("item_type") || "";
    if (item_type != "weapon" && item_type != "armor")
        return;
    if ((item->query("quality") || 1) <= 1)
        return;
    if (arrayp(item->query("affixes")) && sizeof(item->query("affixes")) > 0)
        return;

    budget = NUMERIC_RULE_D->query_equipment_template_budget(
        item->query("template_id") || "", 0);
    if (!mapp(budget) || !budget["ok"])
        return;
    affix_limit = intp(budget["affix_budget_limit"]) ? budget["affix_budget_limit"] : 0;
    max_affixes = intp(budget["max_affixes"]) && budget["max_affixes"] > 0
        ? budget["max_affixes"] : 4;
    if (affix_limit <= 0 || max_affixes <= 0)
        return;

    affix_pool = call_other(EQUIPMENT_AFFIXES_CONFIG, "query_equipment_affixes");
    if (!mapp(affix_pool))
        return;

    candidates = ({ });
    foreach (affix_id, mapping affix in affix_pool) {
        if (!mapp(affix) || !stringp(affix["stat"]))
            continue;
        if (arrayp(affix["tags"]) && member_array(item_type, affix["tags"]) == -1)
            continue;
        candidates += ({ affix });
    }
    if (sizeof(candidates) == 0)
        return;

    rolled = ({ });
    used_ids = ({ });
    used_groups = ({ });
    remaining = affix_limit;
    for (i = 0; i < AFFIX_ROLL_MAX_ATTEMPTS && sizeof(rolled) < max_affixes; i++) {
        mapping pick;
        string pick_id;
        string group;

        pick = candidates[random(sizeof(candidates))];
        pick_id = pick["id"] || "";
        cost = intp(pick["cost"]) ? pick["cost"] : 0;
        if (cost <= 0 || cost > remaining)
            continue;
        if (member_array(pick_id, used_ids) != -1)
            continue;
        group = stringp(pick["exclusive_group"]) && pick["exclusive_group"] != ""
            ? pick["exclusive_group"] : "";
        if (group != "" && member_array(group, used_groups) != -1)
            continue;

        used_ids += ({ pick_id });
        if (group != "")
            used_groups += ({ group });
        rolled += ({ pick });
        remaining -= cost;
    }

    if (sizeof(rolled) == 0)
        return;

    item->set("affixes", rolled);
    desc = item->query("description") || item->query("long") || "";
    foreach (mapping affix in rolled) {
        desc += "\n◆ " + (stringp(affix["description"]) ? affix["description"] : "");
    }
    item->set("description", desc);
    item->set("long", desc);
}

int register_shop(string shop_id, mapping shop_data)
{
    object template_d;

    if (!stringp(shop_id) || !mapp(shop_data))
        return 0;

    template_d = resolve_item_template_service();
    if (!objectp(template_d))
        return 0;

    return template_d->register_shop_config(shop_id, shop_data);
}

mapping query_shop(string shop_id)
{
    object template_d;
    mapping shop;
    int generation;

    generation = CONTENT_REGISTRY_D->query_content_generation();
    template_d = resolve_item_template_service();
    if (!objectp(template_d))
        return ([
            "ok": 0,
            "shop_id": stringp(shop_id) ? shop_id : "",
            "goods": ({ }),
            "generation": generation,
            "source": "content",
        ]);

    template_d->setup_default_shops();
    shop = template_d->query_shop_config(shop_id);
    if (!mapp(shop))
        return ([
            "ok": 0,
            "shop_id": stringp(shop_id) ? shop_id : "",
            "goods": ({ }),
            "generation": generation,
            "source": "content",
        ]);

    shop = copy(shop);
    shop["ok"] = 1;
    shop["shop_id"] = stringp(shop_id) ? shop_id : shop["shop_id"];
    shop["goods"] = arrayp(shop["goods"])
        ? copy(shop["goods"])
        : (arrayp(shop["default_items"])
            ? copy(shop["default_items"]) : ({ }));
    shop["generation"] = generation;
    shop["source"] = "content";
    return shop;
}

void setup_default_shops()
{
    object template_d;

    template_d = resolve_item_template_service();
    if (!objectp(template_d))
        return;

    template_d->setup_default_shops();
}
