/**
 * @file    util_status.c
 * @brief   状态检查工具 - Status Check Utilities



 *
 * @par Description:
 * 提供玩家状态检查功能，包括战斗状态、安全区检查、装备检查等。
 */

#pragma strict_types
#pragma save_types

#include <system/globals.h>
#include <system/ansi.h>
#include <system/constants.h>
#include <game_constants.h>

/**
 * @brief 检查玩家是否在战斗中
 * @param player 玩家对象
 * @return 1=在战斗中, 0=不在
 */
int is_fighting(object player)
{
    if (!player)
        return 0;

    if (player->is_fighting()) {
        notify_fail(HIR "你正在战斗中！" NOR "\n");
        return 1;
    }

    return 0;
}

/**
 * @brief 检查玩家是否处于安全区
 * @param player 玩家对象
 * @return 1=安全区, 0=非安全区
 */
int is_safe_zone(object player)
{
    object env;
    mixed no_fight;

    if (!player)
        return 0;

    env = environment(player);
    if (!env)
        return 0;

    no_fight = env->query("no_fight");
    if (no_fight) {
        notify_fail(HIR "这里禁止战斗。" NOR "\n");
        return 1;
    }

    return 0;
}

/**
 * @brief 检查物品是否可装备
 * @param item 物品对象
 * @return 1=可装备, 0=不可
 */
int is_equippable(object item)
{
    string item_type;

    if (!item)
        return 0;

    item_type = item->query("item_type");

    if (item_type != "weapon" && item_type != "armor") {
        notify_fail(sprintf("%s不是可装备的物品。\n",
            item->query_name()));
        return 0;
    }

    return 1;
}

int ensure_can_move(object player)
{
    if (!objectp(player))
        return 0;

    if (player->is_busy()) {
        notify_fail("你正忙着呢。\n");
        return 0;
    }

    if (player->is_fighting()) {
        notify_fail("你正在战斗中，无法离开。\n");
        return 0;
    }

    return 1;
}

int ensure_can_start_combat(object me, object target)
{
    if (!objectp(me) || !objectp(target))
        return 0;

    if (me == target) {
        notify_fail("你不能和自己切磋。\n");
        return 0;
    }

    if (!target->is_character()) {
        notify_fail("这个目标无法战斗。\n");
        return 0;
    }

    if (me->is_fighting()) {
        notify_fail("你正在战斗中！\n");
        return 0;
    }

    if (target->is_fighting()) {
        notify_fail(target->query_name() + "正在战斗中！\n");
        return 0;
    }

    return 1;
}

private int query_status_resource_max(object who, string resource)
{
    if (!objectp(who) || !stringp(resource) || resource == "")
        return 0;

    switch (resource) {
        case "hp":
            if (function_exists("query_max_hp", who))
                return who->query_max_hp();
            break;
        case "mp":
            if (function_exists("query_max_mp", who))
                return who->query_max_mp();
            break;
        case "ep":
            if (function_exists("query_max_ep", who))
                return who->query_max_ep();
            break;
    }

    return who->query("combat/max_" + resource) || 0;
}

mapping query_combat_resource_snapshot(object who)
{
    mapping maxima;
    int hp;
    int max_hp;
    int mp;
    int max_mp;
    int ep;
    int max_ep;

    if (!objectp(who))
        return ([
            "hp": 0,
            "max_hp": 100,
            "mp": 0,
            "max_mp": 100,
            "ep": 0,
            "max_ep": 100,
        ]);

    maxima = function_exists("query_vital_maxima_snapshot", who)
        ? who->query_vital_maxima_snapshot() : ([]);
    hp = who->query("combat/hp") || 0;
    max_hp = mapp(maxima) && intp(maxima["hp"]) && maxima["hp"] > 0
        ? maxima["hp"] : query_status_resource_max(who, "hp") || 100;
    mp = who->query("combat/mp") || 0;
    max_mp = mapp(maxima) && intp(maxima["mp"]) && maxima["mp"] > 0
        ? maxima["mp"] : query_status_resource_max(who, "mp") || 100;
    ep = who->query("combat/ep") || 0;
    max_ep = mapp(maxima) && intp(maxima["ep"]) && maxima["ep"] > 0
        ? maxima["ep"] : query_status_resource_max(who, "ep") || 100;

    return ([
        "hp": hp,
        "max_hp": max_hp,
        "mp": mp,
        "max_mp": max_mp,
        "ep": ep,
        "max_ep": max_ep,
    ]);
}
