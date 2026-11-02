/**
 * @file    combat_targeting.c
 * @brief   战斗入口校验子服务



 */

#pragma strict_types

#include <system/ansi.h>
#include <system/services.h>

inherit "/lib/std/utils/util_player";

object prepare_attack(object combat_service, object attacker, string target_name)
{
    object env;
    object target;
    mapping pvp_budget;

    if (!objectp(attacker) || !stringp(target_name) || target_name == "")
        return 0;

    env = environment(attacker);
    if (!objectp(env)) {
        combat_service->combat_msg(attacker, "你好像不在任何地方。\n");
        return 0;
    }

    if (combat_service->is_in_combat(attacker) || attacker->is_fighting()) {
        combat_service->combat_msg(attacker, HIR "你已经在战斗中了！" NOR "\n");
        return 0;
    }

    target = find_living_in_room(env, target_name, attacker);
    if (!objectp(target)) {
        combat_service->combat_msg(attacker, "这里没有「" + target_name + "」。\n");
        return 0;
    }

    if (!function_exists("is_character", target) || !target->is_character()) {
        combat_service->combat_msg(attacker, "那不是生物。\n");
        return 0;
    }

    if (target->query_temp("is_dying") || ((target->query("combat/hp") || 0) <= 0)) {
        combat_service->combat_msg(attacker, target->query_name() + "已经倒下了。\n");
        return 0;
    }

    if (target == attacker) {
        combat_service->combat_msg(attacker, "攻击自己？\n");
        return 0;
    }

    pvp_budget = NUMERIC_RULE_D->query_combat_pvp_budget_report(attacker, target, ([
        "kill_mode": 1,
        "safe_room": 0,
    ]));
    if (mapp(pvp_budget) && pvp_budget["block_attack"]) {
        combat_service->combat_msg(attacker, HIR "当前 PVP 风险预算不允许发起攻击。" NOR "\n");
        return 0;
    }

    if (combat_service->is_in_combat(target) || target->is_fighting()) {
        combat_service->combat_msg(attacker, target->query_name() + "正在战斗中！\n");
        return 0;
    }

    return target;
}

object prepare_fight(object combat_service, object attacker, string target_name)
{
    object env;
    object target;

    if (!objectp(attacker) || !stringp(target_name) || target_name == "")
        return 0;

    env = environment(attacker);
    if (!objectp(env)) {
        combat_service->combat_msg(attacker, "你现在无法切磋。\n");
        return 0;
    }

    if (env->query("no_fight")) {
        combat_service->combat_msg(attacker, HIR "这里禁止切磋。" NOR "\n");
        return 0;
    }

    target = find_living_in_room(env, target_name, attacker);
    if (!objectp(target)) {
        combat_service->combat_msg(attacker, "这里没有「" + target_name + "」。\n");
        return 0;
    }

    if (!function_exists("is_character", target) || !target->is_character()) {
        combat_service->combat_msg(attacker, "这个目标无法切磋。\n");
        return 0;
    }

    if (target->query_temp("is_dying") || ((target->query("combat/hp") || 0) <= 0)) {
        combat_service->combat_msg(attacker, target->query_name() + "已经倒下了。\n");
        return 0;
    }

    if (target == attacker) {
        combat_service->combat_msg(attacker, "你不能和自己切磋。\n");
        return 0;
    }

    if (combat_service->is_in_combat(attacker) || attacker->is_fighting()) {
        combat_service->combat_msg(attacker, "你正在战斗中！\n");
        return 0;
    }

    if (combat_service->is_in_combat(target) || target->is_fighting()) {
        combat_service->combat_msg(attacker, target->query_name() + "正在战斗中！\n");
        return 0;
    }

    return target;
}
