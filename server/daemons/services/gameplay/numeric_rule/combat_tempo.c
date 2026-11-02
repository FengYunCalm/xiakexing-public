/**
 * @file    numeric_rule/combat_tempo.c
 * @brief   战斗节奏/逃跑/胜负/再会战预算
 *
 * @par Description:
 * 含 {query_combat_formula, query_combat_tempo_budget_framework, query_combat_tempo_profile, query_combat_tempo_int, query_combat_round_interval_seconds, query_combat_max_rounds, query_combat_tempo_budget_report, query_combat_flee_budget_framework, query_combat_flee_int, query_combat_flee_failure_busy_seconds, query_combat_flee_budget_report, query_combat_outcome_budget_framework, query_combat_outcome_int, query_combat_outcome_budget_report, query_combat_spar_recovery_policy, query_combat_reengage_budget_framework, query_combat_reengage_int, query_combat_reengage_result_cooldown, query_combat_reengage_result_weight, query_combat_reengage_budget_report, query_combat_reengage_state, mark_combat_reengage_cooldown}。
 *
 * 本模块是 combat_tempo 域纯规则片段，由 `numeric_rule_d.c` 继承；
 * 不持有 daemon 状态（nosave 全部留在 facade），
 * 配置经 facade 的 query_section/query_section_int 读取。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>
#include <game/skill.h>
#include <game_constants.h>
#include "defs.h"
int clamp_outcome_bp(int value);
int query_mapping_int(mapping source, string key, int default_value);
mapping query_section(string section_name);
mapping query_combat_formula(string formula_name)
{
    mapping formulas;
    mapping formula;

    formulas = query_section("combat_formula");
    formula = mapp(formulas) && stringp(formula_name) ? formulas[formula_name] : 0;
    return mapp(formula) ? copy(formula) : ([]);
}
mapping query_combat_tempo_budget_framework()
{
    mapping framework;

    framework = query_section("combat_tempo_budget");
    return mapp(framework) ? copy(framework) : ([]);
}
mapping query_combat_tempo_profile(string profile_id)
{
    mapping defaults;
    mapping framework;
    mapping profile;
    mapping profiles;
    mapping result;

    framework = query_combat_tempo_budget_framework();
    defaults = mapp(framework["defaults"]) ? framework["defaults"] : ([]);
    profiles = mapp(framework["profiles"]) ? framework["profiles"] : ([]);
    if (!stringp(profile_id) || profile_id == "")
        profile_id = "default";
    profile = profiles[profile_id];
    if (!mapp(profile))
        return ([]);

    result = copy(defaults);
    result += profile;
    result["profile"] = profile_id;
    return result;
}
private int query_combat_tempo_int(mapping profile, mapping overrides,
    string key, int default_value)
{
    if (mapp(overrides) && !undefinedp(overrides[key]) && intp(overrides[key]))
        return overrides[key];
    if (mapp(profile) && !undefinedp(profile[key]) && intp(profile[key]))
        return profile[key];
    return default_value;
}
int query_combat_round_interval_seconds()
{
    mapping profile;

    profile = query_combat_tempo_profile("default");
    return query_combat_tempo_int(profile, ([]), "round_interval_seconds", 1);
}
int query_combat_max_rounds()
{
    mapping profile;

    profile = query_combat_tempo_profile("default");
    return query_combat_tempo_int(profile, ([]), "max_rounds", 100);
}
mapping query_combat_tempo_budget_report(string profile_id, mapping overrides)
{
    mapping framework;
    mapping profile;
    mapping weights;
    int action_slots;
    int attacker_slots;
    int block_over_budget;
    int defender_slots;
    int divisor;
    int duration_seconds;
    int interval;
    int limit;
    int max_rounds;
    int npc_skill_slots;
    int score;
    int status_slots;

    if (!stringp(profile_id) || profile_id == "")
        profile_id = "default";
    profile = query_combat_tempo_profile(profile_id);
    if (!mapp(profile) || sizeof(keys(profile)) == 0)
        return ([ "ok": 0, "error": "unknown_profile", "profile": profile_id ]);

    framework = query_combat_tempo_budget_framework();
    weights = mapp(framework["weights"]) ? framework["weights"] : ([]);
    interval = query_combat_tempo_int(profile, overrides, "round_interval_seconds", 1);
    max_rounds = query_combat_tempo_int(profile, overrides, "max_rounds", 100);
    attacker_slots = query_combat_tempo_int(profile, overrides, "attacker_action_slots", 1);
    defender_slots = query_combat_tempo_int(profile, overrides, "defender_action_slots", 1);
    status_slots = query_combat_tempo_int(profile, overrides, "status_tick_slots", 2);
    npc_skill_slots = query_combat_tempo_int(profile, overrides, "npc_skill_check_slots", 2);
    limit = query_combat_tempo_int(profile, overrides, "budget_limit", 1500);
    block_over_budget = query_combat_tempo_int(profile, overrides, "block_over_budget", 0);

    if (interval < 1)
        interval = 1;
    if (max_rounds < 1)
        max_rounds = 1;
    if (attacker_slots < 0)
        attacker_slots = 0;
    if (defender_slots < 0)
        defender_slots = 0;
    if (status_slots < 0)
        status_slots = 0;
    if (npc_skill_slots < 0)
        npc_skill_slots = 0;

    duration_seconds = interval * max_rounds;
    action_slots = attacker_slots + defender_slots;
    divisor = query_mapping_int(weights, "duration_score_divisor", 10);
    if (divisor < 1)
        divisor = 1;
    score = max_rounds * query_mapping_int(weights, "round_weight", 5)
        + max_rounds * action_slots * query_mapping_int(weights, "action_slot_weight", 2)
        + max_rounds * status_slots * query_mapping_int(weights, "status_tick_weight", 1)
        + max_rounds * npc_skill_slots * query_mapping_int(weights, "npc_skill_check_weight", 1)
        + duration_seconds * query_mapping_int(weights, "duration_second_weight", 1) / divisor;

    return ([
        "ok": 1,
        "valid": limit <= 0 || score <= limit,
        "profile": profile_id,
        "round_interval_seconds": interval,
        "max_rounds": max_rounds,
        "estimated_max_seconds": duration_seconds,
        "attacker_action_slots": attacker_slots,
        "defender_action_slots": defender_slots,
        "status_tick_slots": status_slots,
        "npc_skill_check_slots": npc_skill_slots,
        "score": score,
        "limit": limit,
        "over_budget": limit > 0 && score > limit ? score - limit : 0,
        "budget_unit": profile["budget_unit"] || "tempo_score",
        "block_apply": limit > 0 && score > limit && block_over_budget ? 1 : 0,
    ]);
}
mapping query_combat_flee_budget_framework()
{
    mapping framework;

    framework = query_section("combat_flee_budget");
    return mapp(framework) ? copy(framework) : ([]);
}
private int query_combat_flee_int(mapping defaults, mapping overrides,
    string key, int default_value)
{
    if (mapp(overrides) && !undefinedp(overrides[key]) && intp(overrides[key]))
        return overrides[key];
    if (mapp(defaults) && !undefinedp(defaults[key]) && intp(defaults[key]))
        return defaults[key];
    return default_value;
}
int query_combat_flee_failure_busy_seconds()
{
    mapping defaults;
    mapping framework;

    framework = query_combat_flee_budget_framework();
    defaults = mapp(framework["defaults"]) ? framework["defaults"] : ([]);
    return query_combat_flee_int(defaults, ([]), "failure_busy_seconds", 2);
}
mapping query_combat_flee_budget_report(object who, mapping context)
{
    mapping defaults;
    mapping framework;
    mapping weights;
    mixed *enemies;
    int base_chance;
    int block_over_budget;
    int busy;
    int busy_penalty;
    int chance_bp;
    int dexterity;
    int dexterity_factor;
    int dexterity_difference;
    int enemy_dexterity;
    int directed;
    int directed_success;
    int divisor;
    int enemy_count;
    int enemy_penalty;
    int failure_busy;
    int limit;
    int max_chance;
    int min_chance;
    int score;
    int threshold;

    if (!mapp(context))
        context = ([]);

    framework = query_combat_flee_budget_framework();
    defaults = mapp(framework["defaults"]) ? framework["defaults"] : ([]);
    weights = mapp(framework["weights"]) ? framework["weights"] : ([]);
    dexterity = !undefinedp(context["dexterity"]) && intp(context["dexterity"])
        ? context["dexterity"]
        : (objectp(who) ? (who->query("attributes/dexterity") || 0) : 0);
    busy = !undefinedp(context["is_busy"]) && intp(context["is_busy"])
        ? (context["is_busy"] ? 1 : 0)
        : (objectp(who) && who->is_busy() ? 1 : 0);
    if (!undefinedp(context["enemy_count"]) && intp(context["enemy_count"]))
        enemy_count = context["enemy_count"];
    else if (objectp(who) && function_exists("query_enemies", who)) {
        enemies = who->query_enemies();
        enemy_count = arrayp(enemies) ? sizeof(enemies) : 0;
    }
    directed = !undefinedp(context["directed"]) && context["directed"] ? 1 : 0;

    base_chance = query_combat_flee_int(defaults, context, "base_chance_bp", 5000);
    enemy_dexterity = !undefinedp(context["enemy_dexterity"])
        && intp(context["enemy_dexterity"])
        && context["enemy_dexterity"] > 0
        ? context["enemy_dexterity"] : dexterity;
    dexterity_factor = query_combat_flee_int(defaults, context, "dexterity_chance_bp_per_point", 100);
    busy_penalty = query_combat_flee_int(defaults, context, "busy_penalty_bp", 2000);
    enemy_penalty = query_combat_flee_int(defaults, context, "enemy_count_penalty_bp", 1000);
    threshold = query_combat_flee_int(defaults, context, "enemy_count_penalty_threshold", 1);
    min_chance = query_combat_flee_int(defaults, context, "min_chance_bp", 1000);
    max_chance = query_combat_flee_int(defaults, context, "max_chance_bp", 9000);
    failure_busy = query_combat_flee_int(defaults, context, "failure_busy_seconds", 2);
    limit = query_combat_flee_int(defaults, context, "budget_limit", 2000);
    block_over_budget = query_combat_flee_int(defaults, context, "block_over_budget", 0);

    dexterity_difference = dexterity - enemy_dexterity;
    chance_bp = base_chance + dexterity_difference * dexterity_factor;
    if (busy)
        chance_bp -= busy_penalty;
    /* 以一名敌人为基准，每增加一名敌人扣一个 enemy_penalty；最终
     * chance_bp 仍由 min_chance_bp 保证不低于 10%。 */
    if (enemy_count > threshold)
        chance_bp -= (enemy_count - threshold) * enemy_penalty;
    if (chance_bp < min_chance)
        chance_bp = min_chance;
    if (chance_bp > max_chance)
        chance_bp = max_chance;
    if (failure_busy < 0)
        failure_busy = 0;

    divisor = query_mapping_int(weights, "chance_risk_divisor", 10);
    if (divisor < 1)
        divisor = 1;
    score = (BP_BASE - chance_bp) / divisor
        + failure_busy * query_mapping_int(weights, "failure_busy_weight", 200)
        + (enemy_count > threshold ? (enemy_count - threshold) * query_mapping_int(weights, "enemy_pressure_weight", 100) : 0)
        + (!directed ? query_mapping_int(weights, "undirected_weight", 100) : 0);
    /* S20/L-138：定向仅指定出口，成功仍由 chance_bp 决定。保留字段供
     * 客户端兼容读取，但永远不报告必成。 */
    directed_success = 0;

    return ([
        "ok": 1,
        "valid": limit <= 0 || score <= limit,
        "chance_bp": chance_bp,
        "dexterity": dexterity,
        "enemy_dexterity": enemy_dexterity,
        "dexterity_difference": dexterity_difference,
        "is_busy": busy,
        "enemy_count": enemy_count,
        "directed": directed,
        "directed_success": directed_success ? 1 : 0,
        "failure_busy_seconds": failure_busy,
        "score": score,
        "limit": limit,
        "over_budget": limit > 0 && score > limit ? score - limit : 0,
        "budget_unit": defaults["budget_unit"] || "flee_risk_score",
        "block_apply": limit > 0 && score > limit && block_over_budget ? 1 : 0,
    ]);
}
mapping query_combat_outcome_budget_framework()
{
    mapping framework;

    framework = query_section("combat_outcome_budget");
    return mapp(framework) ? copy(framework) : ([]);
}
private int query_combat_outcome_int(mapping defaults, mapping context,
    string key, int default_value)
{
    if (mapp(context) && !undefinedp(context[key]) && intp(context[key]))
        return context[key];
    if (mapp(defaults) && !undefinedp(defaults[key]) && intp(defaults[key]))
        return defaults[key];
    return default_value;
}
mapping query_combat_outcome_budget_report(mapping context)
{
    mapping defaults;
    mapping framework;
    mapping weights;
    string result;
    int block_over_budget;
    int clear_dying;
    int dungeon_progress;
    int has_dungeon_progress;
    int has_loot;
    int has_quest_progress;
    int kill_mode;
    int limit;
    int npc_defeat;
    int quest_progress;
    int recover_hp_bp;
    int recover_mp_bp;
    int reward_on_defeat;
    int round_count;
    int score;
    int send_end_packet;
    int spar_recovery;

    if (!mapp(context))
        context = ([]);

    framework = query_combat_outcome_budget_framework();
    defaults = mapp(framework["defaults"]) ? framework["defaults"] : ([]);
    weights = mapp(framework["weights"]) ? framework["weights"] : ([]);
    result = stringp(context["result"]) && context["result"] != "" ? context["result"] : "draw";
    kill_mode = !undefinedp(context["kill_mode"]) && context["kill_mode"] ? 1 : 0;
    spar_recovery = !undefinedp(context["spar_recovery"])
        ? (context["spar_recovery"] ? 1 : 0)
        : (!kill_mode ? 1 : 0);
    npc_defeat = !undefinedp(context["npc_defeat"]) && context["npc_defeat"] ? 1 : 0;
    has_loot = !undefinedp(context["has_loot"]) && context["has_loot"] ? 1 : 0;
    has_quest_progress = !undefinedp(context["has_quest_progress"]) && context["has_quest_progress"] ? 1 : 0;
    has_dungeon_progress = !undefinedp(context["has_dungeon_progress"]) && context["has_dungeon_progress"] ? 1 : 0;
    round_count = query_combat_outcome_int(defaults, context, "round_count", 0);
    if (round_count < 0)
        round_count = 0;

    recover_hp_bp = clamp_outcome_bp(query_combat_outcome_int(defaults, context, "spar_recover_hp_bp", BP_BASE));
    recover_mp_bp = clamp_outcome_bp(query_combat_outcome_int(defaults, context, "spar_recover_mp_bp", BP_BASE));
    clear_dying = query_combat_outcome_int(defaults, context, "spar_clear_dying", 1) ? 1 : 0;
    send_end_packet = query_combat_outcome_int(defaults, context, "send_end_packet", 1) ? 1 : 0;
    reward_on_defeat = query_combat_outcome_int(defaults, context, "reward_on_npc_defeat", 1) ? 1 : 0;
    quest_progress = query_combat_outcome_int(defaults, context, "quest_progress_on_npc_defeat", 1) ? 1 : 0;
    dungeon_progress = query_combat_outcome_int(defaults, context, "dungeon_boss_progress_on_npc_defeat", 1) ? 1 : 0;
    limit = query_combat_outcome_int(defaults, context, "budget_limit", 1800);
    block_over_budget = query_combat_outcome_int(defaults, context, "block_over_budget", 0);

    score = (result == "win" || result == "lose" || result == "npc_defeat"
            ? query_mapping_int(weights, "decisive_result_weight", 300)
            : query_mapping_int(weights, "draw_result_weight", 100))
        + (kill_mode ? query_mapping_int(weights, "kill_mode_weight", 500) : 0)
        + (spar_recovery ? query_mapping_int(weights, "spar_recovery_weight", 300) : 0)
        + (npc_defeat && has_loot && reward_on_defeat ? query_mapping_int(weights, "npc_defeat_reward_weight", 400) : 0)
        + (npc_defeat && has_quest_progress && quest_progress ? query_mapping_int(weights, "quest_progress_weight", 200) : 0)
        + (npc_defeat && has_dungeon_progress && dungeon_progress ? query_mapping_int(weights, "dungeon_progress_weight", 200) : 0)
        + (send_end_packet ? query_mapping_int(weights, "end_packet_weight", 50) : 0)
        + round_count * query_mapping_int(weights, "round_weight", 5);

    return ([
        "ok": 1,
        "valid": limit <= 0 || score <= limit,
        "result": result,
        "kill_mode": kill_mode,
        "round_count": round_count,
        "spar_recovery": spar_recovery,
        "spar_recover_hp_bp": recover_hp_bp,
        "spar_recover_mp_bp": recover_mp_bp,
        "spar_clear_dying": clear_dying,
        "send_end_packet": send_end_packet,
        "npc_defeat": npc_defeat,
        "reward_on_npc_defeat": reward_on_defeat,
        "quest_progress_on_npc_defeat": quest_progress,
        "dungeon_boss_progress_on_npc_defeat": dungeon_progress,
        "score": score,
        "limit": limit,
        "over_budget": limit > 0 && score > limit ? score - limit : 0,
        "budget_unit": defaults["budget_unit"] || "outcome_settlement_score",
        "block_apply": limit > 0 && score > limit && block_over_budget ? 1 : 0,
    ]);
}
mapping query_combat_spar_recovery_policy()
{
    return query_combat_outcome_budget_report(([
        "result": "spar_recovery",
        "kill_mode": 0,
        "spar_recovery": 1,
    ]));
}
mapping query_combat_reengage_budget_framework()
{
    mapping framework;

    framework = query_section("combat_reengage_budget");
    return mapp(framework) ? copy(framework) : ([]);
}
private int query_combat_reengage_int(mapping defaults, mapping context,
    string key, int default_value)
{
    if (mapp(context) && !undefinedp(context[key]) && intp(context[key]))
        return context[key];
    if (mapp(defaults) && !undefinedp(defaults[key]) && intp(defaults[key]))
        return defaults[key];
    return default_value;
}
private int query_combat_reengage_result_cooldown(mapping defaults,
    mapping context, string result)
{
    /* S20/L-137：再战无冷却。保留旧配置读取面用于兼容诊断，但不允许
     * context 或旧配置重新引入阻断。 */
    return 0;
}
private int query_combat_reengage_result_weight(mapping weights, string result)
{
    if (result == "flee")
        return query_mapping_int(weights, "flee_weight", 200);
    if (result == "win")
        return query_mapping_int(weights, "victory_weight", 150);
    if (result == "lose" || result == "defeat")
        return query_mapping_int(weights, "defeat_weight", 300);
    if (result == "draw")
        return query_mapping_int(weights, "draw_weight", 100);
    return 0;
}
mapping query_combat_reengage_budget_report(mapping context)
{
    mapping defaults;
    mapping framework;
    mapping weights;
    string result;
    int block_during_cooldown;
    int block_over_budget;
    int cooldown;
    int kill_mode;
    int limit;
    int score;

    if (!mapp(context))
        context = ([]);

    framework = query_combat_reengage_budget_framework();
    defaults = mapp(framework["defaults"]) ? framework["defaults"] : ([]);
    weights = mapp(framework["weights"]) ? framework["weights"] : ([]);
    result = stringp(context["result"]) && context["result"] != "" ? context["result"] : "start";
    kill_mode = !undefinedp(context["kill_mode"]) && context["kill_mode"] ? 1 : 0;
    cooldown = query_combat_reengage_result_cooldown(defaults, context, result);
    if (kill_mode)
        cooldown += query_combat_reengage_int(defaults, context, "kill_mode_extra_cooldown_seconds", 0);
    if (cooldown < 0)
        cooldown = 0;

    block_during_cooldown = 0;
    limit = query_combat_reengage_int(defaults, context, "budget_limit", 1000);
    block_over_budget = query_combat_reengage_int(defaults, context, "block_over_budget", 0);
    score = cooldown * query_mapping_int(weights, "cooldown_second_weight", 20)
        + query_combat_reengage_result_weight(weights, result)
        + (kill_mode ? query_mapping_int(weights, "kill_mode_weight", 200) : 0)
        + (block_during_cooldown ? query_mapping_int(weights, "block_weight", 100) : 0);

    return ([
        "ok": 1,
        "valid": limit <= 0 || score <= limit,
        "result": result,
        "kill_mode": kill_mode,
        "cooldown_seconds": cooldown,
        "block_reengage_during_cooldown": block_during_cooldown,
        "score": score,
        "limit": limit,
        "over_budget": limit > 0 && score > limit ? score - limit : 0,
        "budget_unit": defaults["budget_unit"] || "reengage_control_score",
        "block_apply": limit > 0 && score > limit && block_over_budget ? 1 : 0,
    ]);
}
mapping query_combat_reengage_state(object who, mapping context)
{
    mapping report;
    int cooldown_until;
    int now;
    int remaining;

    report = query_combat_reengage_budget_report(context);
    now = time();
    cooldown_until = objectp(who) ? (who->query_temp("combat/reengage_cooldown_until") || 0) : 0;
    remaining = cooldown_until > now ? cooldown_until - now : 0;
    if (objectp(who) && cooldown_until > 0 && remaining <= 0)
        who->delete_temp("combat/reengage_cooldown_until");

    report["cooldown_until"] = cooldown_until;
    report["remaining_seconds"] = remaining;
    report["allowed"] = (remaining <= 0 || !report["block_reengage_during_cooldown"]) ? 1 : 0;
    return report;
}
int mark_combat_reengage_cooldown(object who, mapping context)
{
    mapping report;
    int cooldown;

    if (!objectp(who))
        return 0;

    report = query_combat_reengage_budget_report(context);
    cooldown = mapp(report) ? (report["cooldown_seconds"] || 0) : 0;
    if (cooldown <= 0)
        return 0;

    /* 防止历史配置在运行期重新启用冷却；当前产品规则永远返回 0。 */
    return 0;
}
