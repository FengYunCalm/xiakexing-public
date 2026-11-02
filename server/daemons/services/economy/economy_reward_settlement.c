/**
 * @file    economy_reward_settlement.c
 * @brief   奖励金币倍率结算模块。
 */

#pragma strict_types
#pragma save_types

mapping query_economy_config();
float normalize_multiplier(mixed raw_multiplier, float fallback);

private mapping query_economy_reward_config()
{
    mapping config;

    config = query_economy_config();
    return mapp(config) && mapp(config["rewards"]) ? config["rewards"] : ([]);
}

float query_reward_gold_multiplier(string source)
{
    mapping reward_config;
    float multiplier;
    string source_name;

    reward_config = query_economy_reward_config();
    source_name = stringp(source) ? lower_case(source) : "";
    multiplier = normalize_multiplier(reward_config["gold_multiplier"], 1.0);
    if (source_name == "")
        return multiplier;

    if (strsrch(source_name, "quest") != -1)
        return normalize_multiplier(reward_config["quest_gold_multiplier"], multiplier);
    if (strsrch(source_name, "dungeon") != -1)
        return normalize_multiplier(reward_config["dungeon_gold_multiplier"], multiplier);
    if (strsrch(source_name, "combat") != -1 || strsrch(source_name, "loot") != -1 || strsrch(source_name, "kill") != -1)
        return normalize_multiplier(reward_config["kill_gold_multiplier"], multiplier);

    return multiplier;
}

int query_reward_gold_bonus(string source, int gold)
{
    float multiplier;

    if (!intp(gold) || gold <= 0)
        return 0;

    multiplier = query_reward_gold_multiplier(source);
    return to_int(gold * multiplier) - gold;
}

int apply_reward_gold_multiplier(string source, int gold)
{
    int bonus;

    if (!intp(gold) || gold <= 0)
        return gold;

    bonus = query_reward_gold_bonus(source, gold);
    if (bonus <= 0)
        return gold;

    return gold + bonus;
}
