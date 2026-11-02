#pragma strict_types

#include <system/services.h>
#include <framework/message_facade.h>

inherit "/lib/framework/command";

#define STEAL_FAIL_STREAK_PATH "steal/fail_streak"
#define STEAL_FAIL_THRESHOLD 3

int help(object me);

private int query_pickpocket_level(object me)
{
    if (!objectp(me))
        return 0;
    return LIFE_SKILL_D->get_skill_level(me, "pickpocket");
}

private int query_steal_success_rate(object me, object target)
{
    int level;
    int rate;
    int dexterity;
    int stealth;

    level = query_pickpocket_level(me);
    dexterity = intp(me->query("attributes/dex"))
        ? me->query("attributes/dex")
        : (intp(me->query("dex")) ? me->query("dex") : 20);
    stealth = intp(me->query("stealth_bonus"))
        ? me->query("stealth_bonus") : 0;

    rate = 30 + level / 10 + dexterity * 2 / 10 + stealth / 100;
    if (objectp(target)) {
        if (stringp(target->query("shop_id")) && target->query("shop_id") != "")
            rate -= 10;
        if (target->query("role") == "guard"
            || target->query("npc_type") == "guard")
            rate -= 15;
    }
    if (rate > 85)
        rate = 85;
    if (rate < 5)
        rate = 5;
    return rate;
}

private mapping query_target_steal_drops(object target)
{
    mapping drops;
    mapping result;
    mixed item_id;
    mixed raw_chance;

    if (!objectp(target))
        return ([ ]);
    drops = target->query("steal_drops");
    if (!mapp(drops))
        return ([ ]);
    result = ([ ]);
    foreach (item_id, raw_chance in drops)
        if (stringp(item_id) && item_id != ""
            && intp(raw_chance) && raw_chance > 0)
            result[item_id] = raw_chance;
    return result;
}

private void send_steal_result(object me, string message, string kind,
    string target_name, mapping response_context)
{
    msg_event(me, message, ([
        "channel": "main",
        "intent": "append",
        "priority": "normal",
        "reliability": "important",
        "display_mode": "paced",
        "response_context": response_context,
        "payload": ([
            "kind": kind,
            "ok": 1,
            "target": target_name,
        ]),
    ]));
}

int main(object me, string arg)
{
    object target;
    object env;
    object item;
    mapping drops;
    string *drop_ids;
    string item_id;
    string target_name;
    mixed raw_chance;
    int total;
    int roll;
    int accumulated;
    int fail_streak;
    int rate;
    int moral;
    mapping response_context;

    if (!objectp(me))
        return 0;
    response_context = CLIENT_SYNC_D->capture_command_response_context(me);
    if (!stringp(arg) || arg == "")
        return help(me);

    env = environment(me);
    if (!objectp(env)) {
        msg_send(me, "这里没有可下手的目标。\n");
        return 1;
    }
    target = call_other("/lib/std/utils/util_player",
        "find_living_in_room", env, arg, me);
    if (!objectp(target)) {
        msg_send(me, "这里没有这个目标。\n");
        return 1;
    }
    if (target == me) {
        msg_send(me, "你总不能偷自己。\n");
        return 1;
    }
    if (!userp(target) && !target->query("stealable")) {
        msg_send(me, "这人身上没什么值得下手的。\n");
        return 1;
    }

    target_name = target->query("name") || target->query_name() || arg;
    rate = query_steal_success_rate(me, target);
    roll = random(100) + 1;
    if (roll <= rate) {
        drops = query_target_steal_drops(target);
        drop_ids = keys(drops);
        item_id = "";
        total = 0;
        foreach (item_id, raw_chance in drops)
            if (intp(raw_chance) && raw_chance > 0)
                total += raw_chance;
        if (total > 0) {
            roll = random(total);
            accumulated = 0;
            foreach (item_id in drop_ids) {
                accumulated += drops[item_id];
                if (roll < accumulated)
                    break;
            }
        } else {
            item_id = "";
        }

        me->delete_temp(STEAL_FAIL_STREAK_PATH);
        moral = intp(me->query("moral")) ? me->query("moral") : 0;
        me->set("moral", moral - 1);
        if (item_id != "") {
            item = ITEM_D->create_item(item_id, 1);
            if (objectp(item) && item->move(me)) {
                if (function_exists("schedule_save", me))
                    me->schedule_save();
                send_steal_result(me,
                    sprintf("你指尖一探，%s的东西到了你怀里——%s到手。\n",
                        target_name, item->query("name") || item_id),
                    "steal_success", target_name, response_context);
                return 1;
            }
            if (objectp(item))
                ITEM_INVENTORY_D->discard_unplaced_item(item);
        }
        if (!MONEY_D->add_money(me, 10 + random(15))) {
            send_steal_result(me,
                sprintf("你从%s身上摸到了铜钱，但钱袋暂时无法落账。\n", target_name),
                "steal_partial", target_name, response_context);
            return 1;
        }
        if (function_exists("schedule_save", me))
            me->schedule_save();
        send_steal_result(me,
            sprintf("你指尖一探，%s的腰间空了——摸到一把铜钱。\n", target_name),
            "steal_success", target_name, response_context);
        return 1;
    }

    fail_streak = intp(me->query_temp(STEAL_FAIL_STREAK_PATH))
        ? me->query_temp(STEAL_FAIL_STREAK_PATH) : 0;
    fail_streak++;
    me->set_temp(STEAL_FAIL_STREAK_PATH, fail_streak);
    if (fail_streak >= STEAL_FAIL_THRESHOLD) {
        me->delete_temp(STEAL_FAIL_STREAK_PATH);
        msg_send(me, sprintf("%s察觉到了你的动作，按住衣襟退开了几步。\n",
            target_name));
        return 1;
    }

    msg_send(me, sprintf("%s察觉到了什么，看了你一眼。\n", target_name));
    return 1;
}

int help(object me)
{
    msg_send(me, "用法：steal <目标>\n"
        "说明：妙手空空的功夫，成功率看本事；连续失手会让目标提高警惕。\n");
    return 1;
}
