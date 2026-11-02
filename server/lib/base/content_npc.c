#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/base/npc";

nosave mapping _ordinary_topics;
nosave mapping _story_topics;

private string *coerce_ids(mixed raw_ids)
{
    string *ids;
    mixed value;

    ids = ({ });
    if (!arrayp(raw_ids))
        return ids;
    foreach (value in raw_ids) {
        if (stringp(value) && value != "" && member_array(value, ids) == -1)
            ids += ({ value });
    }
    return ids;
}

private int set_combat_max_value(mapping combat, string key, int fallback)
{
    mixed value;
    int max_value;

    value = mapp(combat) && !undefinedp(combat[key])
        ? combat[key] : 0;
    max_value = intp(value) && value > 0 ? value : fallback;
    return set("combat/max_" + key, max_value);
}

private int set_combat_current_value(mapping combat, string key, int fallback,
    int preserve_current)
{
    mixed value;
    int max_value;
    int current_value;
    int has_value;

    max_value = query("combat/max_" + key);
    if (!intp(max_value) || max_value <= 0)
        max_value = fallback;
    has_value = mapp(combat) && !undefinedp(combat[key]);
    value = has_value ? combat[key] : 0;
    if (preserve_current) {
        current_value = query("combat/" + key);
        if (!intp(current_value))
            current_value = max_value;
    } else {
        current_value = has_value && intp(value) && value >= 0
            ? value : max_value;
    }
    if (current_value < 0)
        current_value = 0;
    if (current_value > max_value)
        current_value = max_value;
    return set("combat/" + key, current_value);
}

private int clear_content_field(string path)
{
    mixed value;

    value = query(path);
    if ((stringp(value) && value != "") || (intp(value) && value != 0))
        return delete(path);
    return 1;
}

private string *query_shop_goods(string shop_id)
{
    mapping shops;
    mapping shop;
    mixed goods;

    if (!stringp(shop_id) || shop_id == "")
        return ({ });
    shops = CONFIG_D->query_config("content", "shops");
    shop = mapp(shops) ? shops[shop_id] : 0;
    if (!mapp(shop))
        return ({ });
    goods = arrayp(shop["goods"]) ? shop["goods"] : shop["default_items"];
    return coerce_ids(goods);
}

private int ensure_template_inventory(mapping inventory)
{
    object item;
    object *items;
    mixed raw_count;
    string template_id;
    int count;
    int found;

    if (!mapp(inventory))
        inventory = ([ ]);

    /* 只清理由内容模板创建的物品；玩家或运行时给予的物品永远不在
     * 热更清理范围内。这样模板删除后不会把未知旧字段一并销毁。 */
    items = all_inventory(this_object());
    foreach (item in items) {
        template_id = objectp(item) ? item->query("template_id") : "";
        if (objectp(item) && item->query_temp("content_npc_inventory") == 1
            && (!stringp(template_id) || template_id == ""
                || undefinedp(inventory[template_id])))
            destruct(item);
    }

    foreach (template_id, raw_count in inventory) {
        if (!stringp(template_id) || template_id == "")
            continue;
        count = intp(raw_count) && raw_count > 0 ? raw_count : 1;
        found = 0;
        items = all_inventory(this_object());
        foreach (item in items) {
            if (objectp(item) && item->query("template_id") == template_id) {
                found = 1;
                break;
            }
        }
        if (found)
            continue;
        item = ITEM_D->create_item(template_id, count);
        if (!objectp(item))
            return 0;
        item->set_temp("content_npc_inventory", 1);
        if (!item->move(this_object())) {
            destruct(item);
            return 0;
        }
    }
    return 1;
}

void create()
{
    ::create();
    _ordinary_topics = ([ ]);
    _story_topics = ([ ]);
    set_name("测试人");
    set("id", ({ "test_npc", "测试人" }));
    set("npc_type", "ordinary");
    set("role", "test");
    set("long", "一个用于验证交互链路的 NPC。\n");
    set("functions", ({ "talk", "ask" }));
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
    mapping template;
    mapping combat;
    string template_id;
    string present_id;
    string entity_id;
    string *ids;
    string *functions;
    mixed value;
    int preserve_current;
    int ok;

    if (!mapp(entity))
        return 0;
    template_id = stringp(entity["template_id"]) ? entity["template_id"] : "";
    template = CONTENT_REGISTRY_D->query_npc_template(template_id);
    if (!mapp(template))
        return 0;

    present_id = stringp(entity["present_id"]) ? entity["present_id"] : "";
    entity_id = stringp(entity["id"]) ? entity["id"] : present_id;
    if (entity_id == "")
        entity_id = template_id;
    preserve_current = query_temp("world_lifecycle/content_entity_id") == entity_id;

    ids = ({ });
    if (present_id != "")
        ids += ({ present_id });
    ids += coerce_ids(template["ids"]);
    if (stringp(template["name"]) && template["name"] != "")
        ids += ({ template["name"] });
    ok = set("id", coerce_ids(ids));

    set_name(stringp(template["name"]) && template["name"] != ""
        ? template["name"] : "测试人");
    ok = set("long", stringp(template["long"]) && template["long"] != ""
        ? template["long"] : "一个用于验证交互链路的 NPC。\n") && ok;
    ok = set("npc_type", stringp(template["npc_type"])
        && template["npc_type"] != "" ? template["npc_type"] : "ordinary") && ok;
    ok = set("role", stringp(template["role"]) && template["role"] != ""
        ? template["role"] : "test") && ok;
    functions = arrayp(template["functions"]) ? template["functions"]
        : ({ "talk", "ask" });
    ok = set("functions", functions) && ok;
    ok = set("provider_id", stringp(template["provider_id"])
        ? template["provider_id"] : "") && ok;
    ok = set("no_fight", template["can_fight"] ? 0 : 1) && ok;
    if (template["no_attack"])
        ok = set("no_attack", 1) && ok;
    else
        ok = clear_content_field("no_attack") && ok;
    if (template["training_dummy"])
        ok = set("training_dummy", 1) && ok;
    else
        ok = clear_content_field("training_dummy") && ok;
    if (template["trade_proxy"]) {
        ok = set("trade_proxy", 1) && ok;
        if (present_id != "")
            ok = set("trade_proxy_id", present_id) && ok;
    } else {
        ok = clear_content_field("trade_proxy") && ok;
        ok = clear_content_field("trade_proxy_id") && ok;
    }
    if (!undefinedp(template["combat_exp"])
        && intp(template["combat_exp"]) && template["combat_exp"] >= 0)
        ok = set("combat_exp", template["combat_exp"]) && ok;
    else
        ok = clear_content_field("combat_exp") && ok;
    if (template["stealable"])
        ok = set("stealable", 1) && ok;
    else
        ok = clear_content_field("stealable") && ok;
    if (mapp(template["steal_drops"]))
        ok = set("steal_drops", copy(template["steal_drops"])) && ok;
    else
        ok = clear_content_field("steal_drops") && ok;
    ok = ensure_template_inventory(
        mapp(template["inventory_items"]) ? template["inventory_items"] : ([ ]))
        && ok;

    combat = mapp(template["combat"]) ? template["combat"] : ([ ]);
    ok = set_combat_max_value(combat, "hp", 100) && ok;
    ok = set_combat_current_value(combat, "hp", 100, preserve_current) && ok;
    ok = set_combat_max_value(combat, "mp", 80) && ok;
    ok = set_combat_current_value(combat, "mp", 80, preserve_current) && ok;
    ok = set_combat_max_value(combat, "ep", 50) && ok;
    ok = set_combat_current_value(combat, "ep", 50, preserve_current) && ok;

    _ordinary_topics = mapp(template["ordinary_topics"])
        ? copy(template["ordinary_topics"]) : ([ ]);
    _story_topics = mapp(template["story_topics"])
        ? copy(template["story_topics"]) : ([ ]);

    if (template["healer"])
        ok = set("healer", 1) && ok;
    else
        ok = clear_content_field("healer") && ok;
    if (template["banker"])
        ok = set("banker", 1) && ok;
    else
        ok = clear_content_field("banker") && ok;
    if (stringp(template["shop_id"]) && template["shop_id"] != "")
        ok = set("shop_id", template["shop_id"]) && ok;
    else
        ok = clear_content_field("shop_id") && ok;
    ok = set("vendor_goods", query_shop_goods(query("shop_id"))) && ok;

    value = entity["loot_table_id"];
    if (stringp(value) && value != "")
        ok = set("loot_table_id", value) && ok;
    else
        ok = clear_content_field("loot_table_id") && ok;
    /* 任务击杀目标：与 loot_table_id 同为实体级透传。死亡结算读取该
     * 字段推进 kill 步骤，缺声明时击杀事件不会进入任务系统。 */
    value = entity["quest_kill_target"];
    if (stringp(value) && value != "")
        ok = set("quest_kill_target", value) && ok;
    else
        ok = clear_content_field("quest_kill_target") && ok;
    return ok ? 1 : 0;
}

int apply_content_ask_topics(mapping ordinary_topics, mapping story_topics)
{
    _ordinary_topics = mapp(ordinary_topics) ? copy(ordinary_topics) : ([ ]);
    _story_topics = mapp(story_topics) ? copy(story_topics) : ([ ]);
    return 1;
}

mixed *query_client_ask_topics(object player)
{
    mixed *topics;
    string topic;

    topics = ({ });
    foreach (topic in keys(_ordinary_topics))
        topics += ({ ([ "topic": topic, "label": topic, "kind": "ordinary" ]) });
    foreach (topic in keys(_story_topics))
        topics += ({ ([ "topic": topic, "label": topic, "kind": "story" ]) });
    return topics;
}

int do_talk(string arg)
{
    string *topics;
    string topic;
    string text;

    topics = keys(_ordinary_topics);
    if (!sizeof(topics))
        topics = keys(_story_topics);
    if (!sizeof(topics))
        return present_interaction(this_player(), query_name(),
            query_name() + "看了看你，没有接话。\n",
            query_primary_id() + "_talk", ([ "dialog_bucket": "talk" ]));
    topic = topics[random(sizeof(topics))];
    text = mapp(_ordinary_topics) && stringp(_ordinary_topics[topic])
        ? _ordinary_topics[topic] : _story_topics[topic];
    return present_interaction(this_player(), query_name(), text + "\n",
        query_primary_id() + "_talk", ([ "dialog_bucket": "talk" ]));
}

int do_ask(string topic)
{
    string text;

    text = "";
    if (stringp(topic) && topic != "") {
        if (mapp(_ordinary_topics) && stringp(_ordinary_topics[topic]))
            text = _ordinary_topics[topic];
        else if (mapp(_story_topics) && stringp(_story_topics[topic]))
            text = _story_topics[topic];
    }
    if (text == "")
        text = query_name() + "说：这个我不知道。";
    return present_interaction(this_player(), query_name(), text + "\n",
        query_primary_id() + "_ask", ([ "dialog_bucket": "talk" ]));
}
