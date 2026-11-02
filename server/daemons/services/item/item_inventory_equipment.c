/**
 * @file    item_inventory_equipment.c
 * @brief   装备、卸下和装备需求校验模块。
 */

#pragma strict_types
#pragma save_types

#include <system/ansi.h>
#include <system/services.h>
#include <framework/message_facade.h>

string query_item_display_name(object item);
string resolve_item_command_target(object item);
object find_item_by_name(object me, string item_name);
int is_equipped_item(object item);
void send_xk_quick_cmds(object me, string commands, int append_inventory,
    mapping response_context);

object find_equipped_item_by_name(object me, string item_name)
{
    object *inv;
    object item;
    mixed item_ids;
    string display_name;

    if (!objectp(me) || !stringp(item_name) || item_name == "")
        return 0;

    inv = all_inventory(me);
    foreach (item in inv) {
        if (!is_equipped_item(item))
            continue;

        display_name = query_item_display_name(item);
        item_ids = function_exists("query_id", item) ? item->query_id() : 0;
        if (display_name == item_name
            || (stringp(item_ids) && item_ids == item_name)
            || (arrayp(item_ids) && member_array(item_name, item_ids) != -1))
            return item;
    }

    return 0;
}

string query_item_type_safe(object item)
{
    if (!objectp(item))
        return "";
    if (function_exists("query_item_type", item))
        return item->query_item_type();
    return item->query("item_type") || "";
}

string resolve_armor_slot(object item)
{
    string equip_slot;

    if (!objectp(item))
        return "";

    equip_slot = function_exists("query_armor_type", item)
        ? item->query_armor_type()
        : item->query("armor_type");

    return stringp(equip_slot) ? equip_slot : "";
}

mapping validate_equip_requirements(object me, object item)
{
    mapping gate_state;
    mapping requirements;
    int required_skill_cap;

    if (!objectp(me) || !objectp(item))
        return ([ "ok": 0, "message": "无法装备这件物品。\n" ]);

    if (!ITEM_D->can_actor_use_item(me, item))
        return ([ "ok": 0, "message": "这件物品当前模式不可使用。\n" ]);

    requirements = item->query("requirements");
    if (!mapp(requirements))
        requirements = ([]);

    required_skill_cap = requirements["skill_cap"] || 0;
    gate_state = NUMERIC_RULE_D->query_skill_cap_gate_state(me, required_skill_cap);
    if (required_skill_cap > 0 && !gate_state["satisfied"]) {
        return ([
            "ok": 0,
            "message": sprintf("%s需要技能上限达到 %d 才能装备。\n",
                query_item_display_name(item), gate_state["required_skill_cap"] || 0),
        ]);
    }

    return ([ "ok": 1 ]);
}

int perform_item_unequip(object item)
{
    switch (query_item_type_safe(item)) {
        case "weapon":
            if (function_exists("unequip", item))
                return item->unequip();
            break;

        case "armor":
            if (function_exists("remove_armor", item))
                return item->remove_armor();
            if (function_exists("remove", item))
                return item->remove();
            break;
    }

    if (function_exists("unequip", item))
        return item->unequip();
    if (function_exists("remove_armor", item))
        return item->remove_armor();
    return 0;
}

string build_reequip_command(object item)
{
    return "equip " + resolve_item_command_target(item);
}

private mapping room_equipment_message_options(object room)
{
    return ([
        "channel": "main",
        "intent": "append",
        "priority": "low",
        "reliability": "important",
        "display_mode": "instant",
        "payload": ([ "kind": "room_equipment" ]),
    ]);
}

varargs int equip_item(object me, string item_name, mapping response_context)
{
    object item;
    object old_equip;
    mapping validation;
    string item_type;
    string equip_slot;

    response_context = mapp(response_context) ? copy(response_context) : 0;
    if (!objectp(me) || !stringp(item_name) || item_name == "")
        return 0;

    item = find_item_by_name(me, item_name);
    if (!objectp(item)) {
        msg_send(me, "你身上没有「" + item_name + "」。\n", response_context);
        return 1;
    }

    validation = validate_equip_requirements(me, item);
    if (!mapp(validation) || !validation["ok"]) {
        msg_send(me, mapp(validation) ? (validation["message"] || "装备失败。\n") : "装备失败。\n", response_context);
        return 1;
    }

    item_type = query_item_type_safe(item);
    switch (item_type) {
        case "weapon":
            old_equip = me->query_temp("weapon");
            if (objectp(old_equip) && old_equip != item) {
                msg_send(me, HIR + "你先要卸下当前装备的" + query_item_display_name(old_equip) + "。" NOR + "\n", response_context);
                return 1;
            }
            if (!function_exists("wield", item)) {
                msg_send(me, HIY + query_item_display_name(item) + "无法装备。" NOR + "\n", response_context);
                return 1;
            }
            if (!item->wield()) {
                msg_send(me, HIY + query_item_display_name(item) + "装备失败，可能不满足装备条件。" NOR + "\n", response_context);
                return 1;
            }
            if (ITEM_D->is_training_weapon(item))
                QUEST_D->record_action_progress(me,
                    "training_weapon_equipped", item, "success");
            msg_to_room_event(environment(me),
                me->query_name() + "装备了" + query_item_display_name(item) + "。\n",
                ({ me }), room_equipment_message_options(environment(me)));
            ITEM_FEEDBACK_D->sync_equipment_change(me, response_context);
            send_xk_quick_cmds(me,
                "操作|卸下装备:unequip " + resolve_item_command_target(item)
                + "|查看物品:i|查看属性:score",
                1, response_context);
            return 1;

        case "armor":
            equip_slot = resolve_armor_slot(item);
            if (equip_slot == "") {
                msg_send(me, HIY + query_item_display_name(item) + "缺少有效装备部位。" NOR + "\n", response_context);
                return 1;
            }
            old_equip = stringp(equip_slot) && equip_slot != ""
                ? me->query_temp("armor/" + equip_slot)
                : 0;
            if (objectp(old_equip) && old_equip != item) {
                msg_send(me, HIR + "你先要脱下当前穿着的" + query_item_display_name(old_equip) + "。" NOR + "\n", response_context);
                return 1;
            }
            if (!function_exists("wear", item)) {
                msg_send(me, HIY + query_item_display_name(item) + "无法穿着。" NOR + "\n", response_context);
                return 1;
            }
            if (!item->wear()) {
                msg_send(me, HIY + query_item_display_name(item) + "穿着失败，可能不满足装备条件。" NOR + "\n", response_context);
                return 1;
            }
            msg_to_room_event(environment(me),
                me->query_name() + "穿上了" + query_item_display_name(item) + "。\n",
                ({ me }), room_equipment_message_options(environment(me)));
            ITEM_FEEDBACK_D->sync_equipment_change(me, response_context);
            send_xk_quick_cmds(me,
                "操作|脱下装备:unequip " + resolve_item_command_target(item)
                + "|查看物品:i|查看属性:score",
                1, response_context);
            return 1;
    }

    msg_send(me, HIY + query_item_display_name(item) + "不是可装备的物品。" NOR + "\n", response_context);
    return 1;
}

varargs int unequip_item(object me, string item_name, mapping response_context)
{
    object item;

    response_context = mapp(response_context) ? copy(response_context) : 0;
    if (!objectp(me) || !stringp(item_name) || item_name == "")
        return 0;

    item = find_equipped_item_by_name(me, item_name);
    if (!objectp(item)) {
        msg_send(me, "你没有装备「" + item_name + "」。\n", response_context);
        return 1;
    }
    if (function_exists("is_fighting", me) && me->is_fighting()) {
        msg_send(me, HIR "战斗中无法更换装备！" NOR + "\n", response_context);
        return 1;
    }
    if (!perform_item_unequip(item)) {
        msg_send(me, HIY + query_item_display_name(item) + "似乎被诅咒了，无法卸下。" NOR + "\n", response_context);
        return 1;
    }

    msg_to_room_event(environment(me),
        me->query_name() + "卸下了" + query_item_display_name(item) + "。\n",
        ({ me }), room_equipment_message_options(environment(me)));
    ITEM_FEEDBACK_D->sync_equipment_change(me, response_context);
    send_xk_quick_cmds(me,
        "操作|重新装备:" + build_reequip_command(item) + "|查看物品:i|查看属性:score",
        1, response_context);
    return 1;
}
