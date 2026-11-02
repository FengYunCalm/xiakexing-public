/**
 * @file    combat_rewards.c
 * @brief   战斗奖励子服务



 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

void combat_msg(object who, string msg);

private int query_outcome_enabled(mapping budget, string key, int default_value)
{
    if (!mapp(budget) || undefinedp(budget[key]) || !intp(budget[key]))
        return default_value;
    return budget[key] ? 1 : 0;
}

private object query_reward_owner(object killer)
{
    if (!objectp(killer) || !userp(killer))
        return 0;
    return killer;
}

void grant_npc_defeat_rewards(object npc, object killer)
{
    mapping outcome_budget;
    string loot_table_id;
    mapping result;
    mapping rewards;
    string reward_title;
    string target_name;
    object reward_owner;

    reward_owner = query_reward_owner(killer);
    if (!objectp(npc) || !objectp(reward_owner))
        return;

    loot_table_id = npc->query("loot_table_id");
    if (!stringp(loot_table_id) || loot_table_id == "")
        return;

    rewards = COMBAT_LOOT_REGISTRY_D->query_loot_table(loot_table_id);
    if (!mapp(rewards))
        return;

    outcome_budget = NUMERIC_RULE_D->query_combat_outcome_budget_report(([
        "result": "npc_defeat",
        "kill_mode": 1,
        "npc_defeat": 1,
        "has_loot": 1,
    ]));
    if (!query_outcome_enabled(outcome_budget, "reward_on_npc_defeat", 1))
        return;

    target_name = npc->query_name() || npc->query("name") || "对手";
    reward_title = rewards["title"];
    if (stringp(reward_title) && reward_title != "")
        combat_msg(reward_owner, reward_title + "\n");

    combat_msg(reward_owner, "你击败了" + target_name + "！\n");

    result = COMBAT_LOOT_D->settle_npc_defeat_loot_v2(npc, reward_owner, rewards, ([
        "drop_on_full": 1,
        "source": "combat_npc_defeat",
        "target_key": loot_table_id,
        "target_name": target_name,
    ]));
    if (!mapp(result) || !result["ok"]) {
        /* L-133：奖励应用失败不得静默吞掉；给玩家诚实反馈，不报“已发放”。 */
        combat_msg(reward_owner, "战利品结算失败，本次奖励未发放，请稍后再试。\n");
        return;
    }
}

void handle_npc_defeat_effects(object npc, object killer)
{
    mapping outcome_budget;
    object room;
    string death_broadcast;
    string dungeon_boss_type;
    string loot_table_id;
    string quest_kill_target;
    object reward_owner;

    reward_owner = query_reward_owner(killer);
    if (!objectp(npc) || !objectp(reward_owner))
        return;
    if (!function_exists("is_npc", npc) || !call_other(npc, "is_npc"))
        return;

    death_broadcast = npc->query("death_broadcast");
    loot_table_id = npc->query("loot_table_id");
    dungeon_boss_type = npc->query("dungeon_boss_type");
    quest_kill_target = npc->query("quest_kill_target");
    outcome_budget = NUMERIC_RULE_D->query_combat_outcome_budget_report(([
        "result": "npc_defeat",
        "kill_mode": 1,
        "npc_defeat": 1,
        "has_loot": stringp(loot_table_id) && loot_table_id != "" ? 1 : 0,
        "has_dungeon_progress": stringp(dungeon_boss_type) && dungeon_boss_type != "" ? 1 : 0,
        "has_quest_progress": stringp(quest_kill_target) && quest_kill_target != "" ? 1 : 0,
    ]));
    room = environment(npc);
    if (stringp(death_broadcast) && death_broadcast != "" && objectp(room))
        msg_to_room(room, death_broadcast);

    if (query_outcome_enabled(outcome_budget, "reward_on_npc_defeat", 1))
        grant_npc_defeat_rewards(npc, reward_owner);

    if (query_outcome_enabled(outcome_budget, "dungeon_boss_progress_on_npc_defeat", 1)
        && stringp(dungeon_boss_type) && dungeon_boss_type != "")
        DUNGEON_D->mark_boss_defeated(reward_owner, dungeon_boss_type);

    if (query_outcome_enabled(outcome_budget, "quest_progress_on_npc_defeat", 1)
        && stringp(quest_kill_target) && quest_kill_target != "")
        QUEST_D->record_kill_progress(reward_owner, quest_kill_target, 1);

    if (objectp(room))
        CLIENT_SYNC_D->request_status_room_snapshot(reward_owner, room, 0);
}
