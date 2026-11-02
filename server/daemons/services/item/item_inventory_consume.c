/**
 * @file    item_inventory_consume.c
 * @brief   食用、饮用和消耗品使用模块。
 */

#pragma strict_types
#pragma save_types

#include <system/ansi.h>
#include <system/services.h>
#include <framework/message_facade.h>

string query_item_display_name(object item);
object find_accessible_item(object me, string item_name);
void send_xk_quick_cmds(object me, string commands, int append_inventory,
    mapping response_context);

private int did_consume_item(object item, string verb, int before_amount,
    int before_remaining)
{
    int remaining;

    if (!objectp(item))
        return 1;
    if (call_other(ITEM_INVENTORY_D, "query_item_stack_amount", item) < before_amount)
        return 1;
    remaining = verb == "drink"
        ? item->query("liquid/remaining") : item->query("food_remaining");
    return remaining < before_remaining;
}

varargs int consume_item(object me, string verb, string arg,
    mapping response_context)
{
    object item;
    object source_env;
    mixed item_ids;
    mixed raw_item_ids;
    string template_id;
    string hook_name;
    string item_type;
    string display_name;
    string result_msg;
    int before_amount;
    int before_remaining;
    int handled;

    response_context = mapp(response_context) ? copy(response_context) : 0;
    if (!objectp(me) || !stringp(arg) || arg == "")
        return 0;
    if (verb != "eat" && verb != "drink")
        return 0;

    item = find_accessible_item(me, arg);
    if (!objectp(item)) {
        msg_send(me, "这里没有「" + arg + "」。\n", response_context);
        return 1;
    }

    source_env = environment(item);
    if (!ITEM_D->can_actor_use_item(me, item)) {
        msg_send(me, "这件物品当前模式不可使用。\n", response_context);
        return 1;
    }
    item_ids = ({ });
    template_id = item->query("template_id");
    if (stringp(template_id) && template_id != "")
        item_ids += ({ template_id });
    raw_item_ids = function_exists("query_id", item)
        ? item->query_id() : get_object_id(item);
    if (stringp(raw_item_ids) && raw_item_ids != "")
        item_ids += ({ raw_item_ids });
    else if (arrayp(raw_item_ids))
        item_ids += filter_array(raw_item_ids,
            (: stringp($1) && $1 != "" :));
    before_amount = call_other(ITEM_INVENTORY_D, "query_item_stack_amount", item);
    before_remaining = verb == "drink"
        ? item->query("liquid/remaining")
        : item->query("food_remaining");

    hook_name = verb == "drink" ? "do_drink" : "do_eat";
    if (function_exists(hook_name, item)) {
        handled = call_other(item, hook_name, arg);
        if (handled) {
            if (did_consume_item(item, verb, before_amount, before_remaining))
                QUEST_D->record_consume_progress(me, item_ids, verb);
            ITEM_FEEDBACK_D->sync_consumable_use(me, source_env,
                response_context);
        }
        return handled;
    }

    if (verb == "drink") {
        msg_send(me, "这个东西不能喝。\n", response_context);
        return 1;
    }

    item_type = item->query("item_type");
    display_name = query_item_display_name(item);
    if (item_type != "consumable") {
        msg_send(me, HIR + display_name + "不能食用或使用。" NOR + "\n", response_context);
        return 1;
    }
    if (!function_exists("consume", item)) {
        msg_send(me, HIY + display_name + "无法使用。" NOR + "\n", response_context);
        return 1;
    }

    result_msg = item->consume(me);
    if (!stringp(result_msg) || result_msg == "") {
        msg_send(me, HIY + display_name + "使用失败。" NOR + "\n", response_context);
        return 1;
    }

    msg_send(me, HIG + result_msg + NOR + "\n", response_context);
    msg_to_room(environment(me), me->query_name() + "使用了" + display_name + "。\n", ({ me }));
    if (did_consume_item(item, verb, before_amount, before_remaining))
        QUEST_D->record_consume_progress(me, item_ids, "eat");
    ITEM_FEEDBACK_D->sync_consumable_use(me, source_env, response_context);
    send_xk_quick_cmds(me, "操作|查看状态:score|查看物品:i|查看技能:skills", 1,
        response_context);
    return 1;
}
