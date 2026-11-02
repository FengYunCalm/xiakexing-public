/**
 * @file    item.c
 * @brief   基础物品类



 *
 * @par Description:
 * 物品基类，供武器、防具、容器和消耗品继承。
 */

#pragma strict_types

#include <system/services.h>

inherit "/lib/base/object";

nosave string _item_type;
nosave int _value;
nosave int _durability;
nosave int _max_durability;
nosave int _no_drop;
nosave int _no_sell;
nosave int _no_give;
nosave int _item_weight;

#undef short
#undef max
#undef min

#define ITEM_TYPE_MISC "misc"

void set_item_type(string type);
void set_value(int value);
void set_weight(int weight);
void set_damage(int value);
void set_skill_type(string type);
void set_max_durability(int value);
void set_durability(int value);
void set_armor_type(string type);
void set_defense(int value);

int apply_content_entity(mapping entity)
{
    mapping template;
    mapping effects;
    mapping requirements;
    string template_id;
    string present_id;
    string item_type;
    string *ids;
    mixed value;
    int count;

    if (!mapp(entity))
        return 0;
    template_id = stringp(entity["template_id"]) ? entity["template_id"] : "";
    template = ITEM_D->query_item_template(template_id);
    if (!mapp(template))
        return 0;

    present_id = stringp(entity["present_id"]) ? entity["present_id"] : "";
    ids = ({ });
    if (present_id != "")
        ids += ({ present_id });
    if (template_id != "")
        ids += ({ template_id });
    if (stringp(template["name"]) && template["name"] != "")
        ids += ({ template["name"] });
    if (arrayp(template["aliases"])) {
        foreach (value in template["aliases"])
            if (stringp(value) && value != "" && member_array(value, ids) == -1)
                ids += ({ value });
    }

    set_name(template["name"] || template_id);
    set("id", ids);
    set("template_id", template_id);
    item_type = stringp(template["type"]) && template["type"] != ""
        ? template["type"] : ITEM_TYPE_MISC;
    set_item_type(item_type);
    set("description", template["description"] || "没有什么特别的。");
    set("long", template["description"] || "没有什么特别的。");
    set_value(intp(template["value"]) ? template["value"] : 0);
    set_weight(intp(template["weight"]) ? template["weight"] : 1);
    set("quality", template["quality"] || 1);
    effects = mapp(template["effects"]) ? copy(template["effects"]) : ([ ]);
    requirements = mapp(template["requirements"])
        ? copy(template["requirements"]) : ([ ]);
    set("effects", effects);
    set("requirements", requirements);
    set("base_unit", template["base_unit"] || "个");
    set("no_sell", template["no_sell"] ? 1 : 0);
    set("no_give", template["no_give"] ? 1 : 0);
    set("no_drop", template["no_drop"] ? 1 : 0);
    set("no_get", template["no_get"] ? 1 : 0);
    if (entity["no_sell"])
        set("no_sell", 1);
    if (entity["no_give"])
        set("no_give", 1);
    if (entity["no_drop"])
        set("no_drop", 1);
    if (entity["no_get"])
        set("no_get", 1);
    set("consume_action", template["consume_action"] || "");
    set("stackable", template["stackable"] ? 1 : 0);
    set("max_stack", template["max_stack"] || 1);
    set("binding_policy", template["binding_policy"] || "character");
    set("acquisition/binding_policy", template["binding_policy"] || "character");
    set("usage_modes", template["usage_modes"] || ({ }));
    count = intp(entity["amount"]) && entity["amount"] > 0
        ? entity["amount"]
        : (intp(entity["count"]) && entity["count"] > 0 ? entity["count"] : 1);
    set("amount", count);
    set("count", count);

    if (item_type == "weapon") {
        if (intp(effects["damage"]) && function_exists("set_damage", this_object()))
            set_damage(effects["damage"]);
        if (stringp(effects["skill"]) && function_exists("set_skill_type", this_object()))
            set_skill_type(effects["skill"]);
        if (function_exists("set_max_durability", this_object()))
            set_max_durability(template["max_durability"] || 100);
        if (function_exists("set_durability", this_object()))
            set_durability(template["durability"] || template["max_durability"] || 100);
    } else if (item_type == "armor") {
        if (stringp(template["armor_type"]) && function_exists("set_armor_type", this_object()))
            set_armor_type(template["armor_type"]);
        if (intp(effects["armor"]) && function_exists("set_defense", this_object()))
            set_defense(effects["armor"]);
        if (function_exists("set_max_durability", this_object()))
            set_max_durability(template["max_durability"] || 100);
        if (function_exists("set_durability", this_object()))
            set_durability(template["durability"] || template["max_durability"] || 100);
    }
    ITEM_D->ensure_item_identity(this_object());
    return 1;
}

string query_item_type();
string query_name();
string short();

private string query_consume_action()
{
    string action;

    action = query("consume_action");
    return stringp(action) && action != "" ? action : "eat";
}

private mapping room_consumable_message_options(object room)
{
    return ([
        "channel": "main",
        "intent": "append",
        "priority": "low",
        "reliability": "important",
        "display_mode": "instant",
        "payload": ([ "kind": "room_consumable" ]),
    ]);
}

private int query_stack_count()
{
    return query("amount") || query("count") || 1;
}

private void consume_one_unit()
{
    int amount;

    if (!query("stackable")) {
        destruct(this_object());
        return;
    }

    amount = query_stack_count() - 1;
    if (amount <= 0) {
        destruct(this_object());
        return;
    }

    set("amount", amount);
    set("count", amount);
}

private varargs string apply_consumable_effects(object me, int consume_now)
{
    mapping effects;
    int hp_restore;
    int mp_restore;
    string action_text;
    string item_name;

    if (!objectp(me) || query_item_type() != "consumable")
        return 0;

    effects = query("effects");
    if (!mapp(effects))
        return 0;

    hp_restore = intp(effects["hp"]) ? effects["hp"] : 0;
    mp_restore = intp(effects["mp"]) ? effects["mp"] : 0;
    if (hp_restore <= 0 && mp_restore <= 0)
        return 0;

    /* 恢复写入失败时不能继续发“已服下”的成功回执：物品会照扣、状态却没变。
     * receive_healing 现在返回真实提交结果，这里必须先确认再继续。 */
    if (hp_restore > 0 && !me->receive_healing("hp", hp_restore))
        return 0;
    if (mp_restore > 0 && !me->receive_healing("mp", mp_restore))
        return 0;

    item_name = query_name();
    if (!stringp(item_name) || item_name == "")
        item_name = short();

    action_text = query_consume_action() == "drink" ? "喝下了" : "吃下了";
    if (consume_now)
        consume_one_unit();
    return "你" + action_text + item_name + "。";
}

void set_item_type(string type)
{
    _item_type = type;
    set("item_type", type);
}

string query_item_type()
{
    return _item_type || query("item_type") || ITEM_TYPE_MISC;
}

void set_value(int value)
{
    _value = value;
    set("value", value);
}

int query_value()
{
    return _value || query("value") || 0;
}

int query_amount()
{
    return query_stack_count();
}

void set_weight(int weight)
{
    _item_weight = weight;
    set("weight", weight);
}

int query_weight()
{
    return _item_weight || query("weight") || 0;
}

void set_max_durability(int value)
{
    _max_durability = value;
    set("max_durability", value);
    if (!intp(_durability) || _durability > value) {
        _durability = value;
        set("durability", _durability);
    }
}

int query_max_durability()
{
    int stored_value;

    stored_value = query("max_durability");
    if (intp(stored_value) && stored_value > 0)
        return stored_value;

    if (intp(_max_durability) && _max_durability > 0)
        return _max_durability;

    return 100;
}

void set_durability(int value)
{
    _durability = value;
    set("durability", value);
}

int query_durability()
{
    int stored_value;

    stored_value = query("durability");
    if (intp(stored_value))
        return stored_value;

    if (intp(_durability) && _durability > 0)
        return _durability;

    return query_max_durability();
}

void consume_durability(int amount)
{
    int current;

    if (amount <= 0)
        return;

    current = query_durability() - amount;
    if (current < 0)
        current = 0;
    set_durability(current);
}

int id(string arg)
{
    mixed ids;

    if (!stringp(arg) || arg == "")
        return 0;

    ids = function_exists("query_id", this_object())
        ? query_id() : get_object_id(this_object());
    if (stringp(ids))
        return ids == arg;
    if (arrayp(ids))
        return member_array(arg, ids) != -1;

    return 0;
}

string short()
{
    return query("short") || query("name") || "一件物品";
}

string query_item_info()
{
    return query("long") || short();
}

void init()
{
}

string consume(object me)
{
    return apply_consumable_effects(me, 1);
}

int perform_food_consumption(object me, string arg, string empty_message,
    string self_message, string room_message, string remaining_long_fmt,
    string finish_message)
{
    int food;
    int supply;

    if (!id(arg))
        return 0;

    food = query("food_remaining");
    supply = query("food_supply");
    if (food <= 0) {
        msg_send(me, empty_message + "\n");
        return 1;
    }

    msg_send(me, self_message + "\n");
    if (objectp(environment(me)) && stringp(room_message) && room_message != "")
        msg_to_room(environment(me),
            (me->query_name() || me->query("name") || "有人") + room_message + "\n",
            ({ me }));

    me->receive_healing("hp", supply);
    food--;
    set("food_remaining", food);

    if (food <= 0) {
        if (stringp(finish_message) && finish_message != "")
            msg_send(me, finish_message + "\n");
        destruct(this_object());
        return 1;
    }

    if (stringp(remaining_long_fmt) && remaining_long_fmt != "")
        set("long", sprintf(remaining_long_fmt, food));
    return 1;
}

int do_eat(string arg)
{
    object me;
    object room;
    string item_name;
    string actor_name;
    string result;

    if (!id(arg) || query_item_type() != "consumable")
        return 0;
    if (query_consume_action() == "drink")
        return 0;

    me = this_player();
    item_name = query_name();
    if (!stringp(item_name) || item_name == "")
        item_name = short();
    actor_name = me->query_name() || me->query("name") || "有人";
    room = environment(me);
    result = apply_consumable_effects(me, 0);
    if (!stringp(result) || result == "")
        return 0;

    call_other("/kernel/simul_efun", "msg_send", me, result + "\n");
    if (objectp(room))
        call_other("/kernel/simul_efun", "msg_to_room_event", room,
            actor_name + "吃下了" + item_name + "。\n", ({ me }),
            room_consumable_message_options(room));
    consume_one_unit();
    return 1;
}

int do_drink(string arg)
{
    object me;
    object room;
    string item_name;
    string actor_name;
    string result;

    if (!id(arg) || query_item_type() != "consumable")
        return 0;
    if (query_consume_action() != "drink")
        return 0;

    me = this_player();
    item_name = query_name();
    if (!stringp(item_name) || item_name == "")
        item_name = short();
    actor_name = me->query_name() || me->query("name") || "有人";
    room = environment(me);
    result = apply_consumable_effects(me, 0);
    if (!stringp(result) || result == "")
        return 0;

    call_other("/kernel/simul_efun", "msg_send", me, result + "\n");
    if (objectp(room))
        call_other("/kernel/simul_efun", "msg_to_room_event", room,
            actor_name + "喝下了" + item_name + "。\n", ({ me }),
            room_consumable_message_options(room));
    consume_one_unit();
    return 1;
}

int perform_drink_consumption(object me, string arg, string empty_message,
    string self_message, string room_message, string empty_long,
    string remaining_long_fmt)
{
    int liquid;
    int supply;

    if (!id(arg))
        return 0;

    liquid = query("liquid/remaining");
    supply = query("liquid/supply");
    if (liquid <= 0) {
        msg_send(me, empty_message + "\n");
        return 1;
    }

    msg_send(me, self_message + "\n");
    if (objectp(environment(me)) && stringp(room_message) && room_message != "")
        msg_to_room(environment(me),
            (me->query_name() || me->query("name") || "有人") + room_message + "\n",
            ({ me }));

    me->receive_healing("mp", supply);
    liquid--;
    set("liquid/remaining", liquid);

    if (liquid <= 0) {
        if (stringp(empty_long) && empty_long != "")
            set("long", empty_long);
        return 1;
    }

    if (stringp(remaining_long_fmt) && remaining_long_fmt != "")
        set("long", sprintf(remaining_long_fmt, liquid));
    return 1;
}
