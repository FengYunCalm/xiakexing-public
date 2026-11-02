#pragma strict_types

#include <system/services.h>
#include <framework/message_facade.h>

inherit "/lib/framework/command";
inherit "/lib/std/command_utils";

int help(object me);

/* 住宿：客店歇宿一晚（收费 20 文），恢复气血/内力/精力 */
#define REST_COST 20

int main(object me, string arg)
{
    int balance;
    int max_hp;
    int max_mp;
    int max_ep;
    mapping money_result;

    if (!objectp(me))
        return 0;
    if (!present("baochang_inn", environment(me))
        && !present("inn_keeper", environment(me))) {
        msg_send(me, "这里不是客店，没处歇宿。\n");
        return 1;
    }

    balance = MONEY_D->query_balance(me);
    if (balance < REST_COST) {
        msg_send(me, "歇宿一晚要 20 文钱，你身上的钱不够。\n");
        return 1;
    }

    money_result = ECONOMY_SETTLEMENT_D->settle_money_operation(me,
        "rest", "inn", "deduct", REST_COST,
        PLAYER_STATE_D->build_state_idempotency_key(me, "rest/inn", ([
            "cost": REST_COST,
        ])));
    if (!mapp(money_result) || money_result["ok"] != 1) {
        msg_send(me, "歇宿扣款失败，请稍后再试。\n");
        return 1;
    }
    max_hp = NUMERIC_RULE_D->query_combat_resource_max(me, "hp");
    max_mp = NUMERIC_RULE_D->query_combat_resource_max(me, "mp");
    max_ep = NUMERIC_RULE_D->query_combat_resource_max(me, "ep");
    me->set("combat/hp", max_hp);
    me->set("combat/mp", max_mp);
    me->set("combat/ep", max_ep);
    /* 顺带解除战败虚弱 */
    me->delete("combat/weakened");
    me->schedule_save();
    msg_send(me, "你付了 20 文钱，在小客房睡了一觉。\n"
        "醒来精神饱满，气血、内力、精力都已恢复。\n");
    return 1;
}

int help(object me)
{
    msg_send(me, @HELP
命令：rest（歇宿）

在客店歇宿一晚（20 文），恢复气血/内力/精力并解除虚弱。

用法：
  rest
HELP
    );
    return 1;
}
