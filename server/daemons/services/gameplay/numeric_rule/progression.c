/**
 * @file    numeric_rule/progression.c
 * @brief   经验上限与成长档位
 *
 * @par Description:
 * 含 {query_player_exp_skill_cap, query_skill_cap_gate_config, query_required_skill_cap_for_progression_rank, query_required_exp_for_progression_rank, player_meets_skill_cap_gate, query_progression_rank_by_exp, query_player_progression_rank, query_skill_cap_gate_state}。
 *
 * 本模块是 progression 域纯规则片段，由 `numeric_rule_d.c` 继承；
 * 不持有 daemon 状态（nosave 全部留在 facade），
 * 配置经 facade 的 query_section/query_section_int 读取。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>
#include <game/skill.h>
#include <game_constants.h>
#include "defs.h"
int query_exp_required_for_skill_cap(int wanted_skill_cap);
int query_max_skill_level();
mapping query_player_progression_snapshot(object player);
mapping query_progression_snapshot_by_exp(int combat_exp);
mapping query_section(string section_name);
int query_player_exp_skill_cap(object player);
int query_player_exp_skill_cap(object player)
{
    mapping snapshot;

    if (!objectp(player))
        return 0;

    snapshot = query_player_progression_snapshot(player);
    return mapp(snapshot) ? snapshot["skill_cap"] : 0;
}
 mapping query_skill_cap_gate_config()
{
    mapping config;

    config = query_section("skill_cap_gate");
    return mapp(config) ? config : ([]);
}
int query_required_skill_cap_for_progression_rank(int progression_rank)
{
    mapping config;
    int base_cap;
    int cap;
    int max_level;
    int step;

    if (progression_rank <= 1)
        return 0;

    config = query_skill_cap_gate_config();
    base_cap = intp(config["base_skill_cap"]) ? config["base_skill_cap"] : 100;
    step = intp(config["skill_cap_per_progression_rank"]) ? config["skill_cap_per_progression_rank"] : 10;
    max_level = query_max_skill_level();
    if (base_cap < 0)
        base_cap = 0;
    if (step <= 0)
        step = 10;

    cap = base_cap + (progression_rank - 1) * step;
    if (cap > max_level)
        cap = max_level;
    return cap;
}
int query_required_exp_for_progression_rank(int progression_rank)
{
    int required_cap;

    required_cap = query_required_skill_cap_for_progression_rank(progression_rank);
    if (required_cap <= 0)
        return 0;
    return query_exp_required_for_skill_cap(required_cap);
}
int player_meets_skill_cap_gate(object player, int required_skill_cap)
{
    int required_exp;

    if (required_skill_cap <= 0)
        return 1;
    if (!objectp(player))
        return 0;

    required_exp = query_exp_required_for_skill_cap(required_skill_cap);
    return (player->query("combat_exp") || 0) >= required_exp;
}
int query_progression_rank_by_exp(int combat_exp)
{
    mapping snapshot;

    snapshot = query_progression_snapshot_by_exp(combat_exp);
    return mapp(snapshot) ? snapshot["progression_rank"] : 1;
}
int query_player_progression_rank(object player)
{
    mapping snapshot;

    if (!objectp(player))
        return 1;
    snapshot = query_player_progression_snapshot(player);
    return mapp(snapshot) ? snapshot["progression_rank"] : 1;
}
mapping query_skill_cap_gate_state(object player, int required_skill_cap)
{
    int combat_exp;
    int required_exp;

    combat_exp = objectp(player) ? (player->query("combat_exp") || 0) : 0;
    if (required_skill_cap < 0)
        required_skill_cap = 0;
    required_exp = required_skill_cap > 0 ? query_exp_required_for_skill_cap(required_skill_cap) : 0;
    return ([
        "required_skill_cap": required_skill_cap,
        "required_combat_exp": required_exp,
        "current_skill_cap": objectp(player)
            ? query_player_progression_snapshot(player)["skill_cap"] : 0,
        "current_combat_exp": combat_exp,
        "satisfied": required_skill_cap <= 0 || combat_exp >= required_exp,
    ]);
}
