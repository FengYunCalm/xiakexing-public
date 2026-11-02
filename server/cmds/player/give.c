/**
 * @file    give.c
 * @brief   给予命令 - 将物品给予其他角色



 *
 * @par Description:
 * give 的公共入口只保留参数解析与特殊目标 hook 调度；标准物品转移真链统一回到 `ITEM_INVENTORY_D`。
 */

#pragma strict_types

#include <system/services.h>
#include <framework/message_facade.h>

inherit "/lib/std/command_utils";
inherit "/lib/framework/network";

int help(object me);
int main(object me, string arg);

private object find_give_target(object me, string target_name)
{
    object env;
    object target;

    env = environment(me);
    if (!objectp(env))
        return 0;

    target = find_visible_player(me, target_name);
    if (objectp(target) && target != me)
        return target;

    target = find_living_in_room(env, target_name, me);
    if (objectp(target))
        return target;

    target = QUEST_ACTOR_D->resolve_actor_target(me, env, target_name);
    if (objectp(target) && environment(target) == env)
        return target;

    return 0;
}

private int dispatch_target_give_hook(object me, string arg)
{
    object target;
    mixed hook_result;
    mixed err;

    target = find_give_target(me, arg);
    if (!objectp(target) || !function_exists("do_give", target))
        return 0;

    err = catch(hook_result = target->do_give(arg));
    return !err && hook_result;
}

private int give_currency_amount(object me, object target, int amount,
    mapping response_context)
{
    string target_name;

    if (!objectp(me) || !objectp(target) || amount <= 0)
        return 0;

    target_name = get_object_display_name(target, "对方");
    if (function_exists("is_npc", target) && call_other(target, "is_npc")) {
        if (!function_exists("can_accept_give_money", target)
            || !(target->can_accept_give_money(me, amount))) {
            if (!function_exists("reject_give_money", target)
                || !(target->reject_give_money(me, amount)))
                msg_send(me, target_name + "摆了摆手：这些钱你自己留着吧。\n", response_context);
            return 1;
        }
    }
    if (!MONEY_D->transfer_money(me, target, amount)) {
        msg_send(me, "你身上没有足够的" + MONEY_D->format_money(amount) + "。\n", response_context);
        return 1;
    }

    msg_send(me, "你把" + MONEY_D->format_money(amount) + "给了" + target_name + "。\n", response_context);
    msg_send(target, get_object_display_name(me, "有人") + "给了你" + MONEY_D->format_money(amount) + "。\n");
    if (objectp(environment(me)))
        msg_to_room(environment(me), get_object_display_name(me, "有人") + "给了" + target_name + "一些钱。\n", ({ me, target }));
    ITEM_FEEDBACK_D->sync_transfer(me, target, response_context);
    return 1;
}

private mapping parse_counted_item(string item_text)
{
    int count;
    string item_name;

    if (sscanf(item_text || "", "%d %s", count, item_name) == 2 && count > 0 && item_name != "")
        return ([ "count": count, "item": item_name ]);

    return ([ "count": 1, "item": item_text || "" ]);
}

int main(object me, string arg)
{
    object target;
    mapping parsed;
    mapping counted_item;
    mapping response_context;

    if (!objectp(me))
        return 0;

    response_context = CLIENT_SYNC_D->capture_command_response_context(me);
    parsed = parse_give_args(arg);
    if (!parsed["valid"]) {
        if (dispatch_target_give_hook(me, arg))
            return 1;
        msg_send(me, parsed["error"]);
        return 1;
    }

    target = find_give_target(me, parsed["target"]);
    if (!objectp(target)) {
        msg_send(me, "这里没有「" + parsed["target"] + "」。\n");
        return 1;
    }

    if (MONEY_D->is_currency_reference(parsed["item"]) && function_exists("do_give", target)) {
        mixed hook_result;
        mixed err;

        err = catch(hook_result = target->do_give(arg));
        if (!err && hook_result)
            return 1;
    }

    if (MONEY_D->parse_currency_amount(parsed["item"]) > 0)
        return give_currency_amount(me, target,
            MONEY_D->parse_currency_amount(parsed["item"]), response_context);

    counted_item = parse_counted_item(parsed["item"]);
    return call_other(ITEM_INVENTORY_D, "give_item", me, target,
        counted_item["item"], counted_item["count"], response_context);
}

int help(object me)
{
    msg_send(me, @HELP
命令：give（给予）

将物品给予其他角色。

用法：
  give <物品名> to <角色名>
  give <物品名> <角色名>
HELP
    );
    return 1;
}
