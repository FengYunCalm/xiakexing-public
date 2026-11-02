/**
 * @file    gather_runtime_d.c
 * @brief   生活技能采集运行时真源



 *
 * @note
 * 本文件只负责采集运行时结算，不持有世界采集点配置，也不提供玩家公共命令入口。
 * 第一批只接 synthetic gather target 契约，用于管理员验收与后续公共动词接线前的最小真链验证。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

private mapping create_result_state(string skill_id)
{
    return ([
        "ok": 0,
        "error": "",
        "skill_id": stringp(skill_id) ? skill_id : "",
        "target_id": "",
        "target_name": "",
        "exp_awarded": 0,
        "level_before": 0,
        "level_after": 0,
        "exp_before": 0,
        "exp_after": 0,
        "levelled_up": 0,
        "operation_id": "",
        "applied": 0,
        "action_budget": ([ ]),
    ]);
}

private int query_item_count(object item)
{
    int count;

    if (!objectp(item))
        return 0;

    count = item->query("count") || item->query("amount") || 1;
    if (count <= 0)
        count = 1;

    return count;
}

private mapping collect_inventory_reward_progress(mapping applied)
{
    mapping progress;
    object item;
    string template_id;

    progress = ([]);
    if (!mapp(applied) || !arrayp(applied["inventory_items"]))
        return progress;

    foreach (item in applied["inventory_items"]) {
        if (!objectp(item))
            continue;

        template_id = item->query("template_id");
        if (!stringp(template_id) || template_id == "")
            continue;

        progress[template_id] = (progress[template_id] || 0) + query_item_count(item);
    }

    return progress;
}

private string build_gather_operation_id(object player, string target_id,
    mapping budget, mapping options)
{
    string player_id;
    int attempt_at;

    if (mapp(options) && stringp(options["operation_id"])
        && options["operation_id"] != "")
        return options["operation_id"];
    player_id = PLAYER_STATE_D->query_player_id(player);
    if (!stringp(player_id) || player_id == "")
        player_id = base_name(player);
    attempt_at = mapp(budget) && intp(budget["now"])
        ? budget["now"] : time();
    return sprintf("gather:%s:%s:%d", player_id, target_id, attempt_at);
}

private void record_gather_collect_progress(object player, mapping applied)
{
    mapping progress;
    string item_id;
    int count;

    if (!objectp(player) || !mapp(applied))
        return;

    progress = collect_inventory_reward_progress(applied);
    foreach (item_id, count in progress) {
        if (stringp(item_id) && item_id != "" && intp(count) && count > 0) {
            QUEST_D->record_item_acquired(player, item_id, count);
        }
    }
}

private mapping normalize_synthetic_target(string skill_id, mixed raw_target)
{
    mapping target;
    string reward_item;
    int reward_count;
    int exp_reward;
    int required_level;

    if (!stringp(skill_id) || skill_id == "" || !mapp(raw_target))
        return 0;
    if (raw_target["contract"] != "synthetic_gather_target")
        return 0;
    if (!stringp(raw_target["skill_id"]) || raw_target["skill_id"] != skill_id)
        return 0;

    reward_item = raw_target["reward_item"];
    if (!stringp(reward_item) || reward_item == "")
        reward_item = "";
    if (!ITEM_D->item_exists(reward_item))
        return 0;

    reward_count = raw_target["reward_count"];
    if (!intp(reward_count) || reward_count <= 0)
        reward_count = 1;

    exp_reward = raw_target["exp_reward"];
    if (!intp(exp_reward) || exp_reward <= 0)
        exp_reward = 1;

    required_level = raw_target["required_level"];
    if (!intp(required_level) || required_level < 0)
        required_level = 0;

    target = ([
        "contract": "synthetic_gather_target",
        "target_id": stringp(raw_target["target_id"]) && raw_target["target_id"] != ""
            ? raw_target["target_id"]
            : ("synthetic_" + skill_id + "_target"),
        "target_name": stringp(raw_target["target_name"]) && raw_target["target_name"] != ""
            ? raw_target["target_name"]
            : "合成采集样本",
        "skill_id": skill_id,
        "reward_item": reward_item,
        "reward_count": reward_count,
        "exp_reward": exp_reward,
        "required_level": required_level,
    ]);

    return target;
}

mapping build_synthetic_gather_target(string skill_id, mapping overrides)
{
    mapping target;

    target = ([
        "contract": "synthetic_gather_target",
        "target_id": "synthetic_" + skill_id + "_target",
        "target_name": "合成草药样本",
        "skill_id": skill_id,
        "reward_item": "",
        "reward_count": 1,
        "exp_reward": 3,
        "required_level": 0,
    ]);

    if (mapp(overrides))
        target += copy(overrides);

    target["contract"] = "synthetic_gather_target";
    target["skill_id"] = skill_id;
    if (!stringp(target["reward_item"]) || target["reward_item"] == "")
        target["reward_item"] = "";

    return target;
}

mapping perform_gather(object player, string skill_id, mixed raw_target, mapping options)
{
    mapping result;
    mapping skill_def;
    mapping target;
    mapping budget;
    mapping budget_commit;
    mapping applied;
    mapping skill_result;
    string operation_id;
    int drop_on_full;

    result = create_result_state(skill_id);
    if (!objectp(player)) {
        result["error"] = "invalid_player";
        return result;
    }
    if (!stringp(skill_id) || skill_id == "") {
        result["error"] = "invalid_skill_id";
        return result;
    }

    skill_def = LIFE_SKILL_D->query_skill_definition(skill_id);
    if (!mapp(skill_def) || skill_def["type"] != "gathering") {
        result["error"] = "invalid_gather_skill";
        return result;
    }

    target = normalize_synthetic_target(skill_id, raw_target);
    if (!mapp(target)) {
        result["error"] = "invalid_target";
        return result;
    }

    result["target_id"] = target["target_id"];
    result["target_name"] = target["target_name"];
    result["level_before"] = LIFE_SKILL_D->get_skill_level(player, skill_id);
    result["exp_before"] = player->query("life_skills_exp/" + skill_id) || 0;
    if (result["level_before"] < target["required_level"]) {
        result["error"] = "insufficient_skill_level";
        return result;
    }

    budget = NUMERIC_RULE_D->query_action_budget_state(player,
        "life_skill_gather", skill_id, ([ "target_key": target["target_id"] ]));
    result["action_budget"] = budget;
    if (!mapp(budget) || !budget["ok"]) {
        result["error"] = mapp(budget) ? (budget["error"] || "action_budget_failed") : "action_budget_failed";
        return result;
    }
    operation_id = build_gather_operation_id(player, target["target_id"],
        budget, options);
    result["operation_id"] = operation_id;
    if (mapp(options) && options["dry_run"]) {
        result["ok"] = 1;
        return result;
    }

    budget_commit = NUMERIC_RULE_D->commit_action_budget_event(player, budget);
    if (!mapp(budget_commit) || !budget_commit["ok"]) {
        result["error"] = "action_budget_commit_failed";
        return result;
    }

    drop_on_full = mapp(options) && options["drop_on_full"] ? 1 : 0;
    applied = REWARD_APPLY_D->apply_reward_package(player, ([
        "item": target["reward_item"],
        "item_count": target["reward_count"],
    ]), ([
        "drop_on_full": drop_on_full,
        "source": "life_skill_gather:" + target["target_id"],
        "target_key": operation_id,
        "idempotent": 1,
        "persist": 1,
    ]));
    if (!mapp(applied) || !applied["ok"]) {
        result["error"] = mapp(applied) && stringp(applied["error"])
            ? applied["error"] : "reward_apply_failed";
        return result;
    }

    record_gather_collect_progress(player, applied);

    skill_result = LIFE_SKILL_D->apply_skill_experience_operation(player,
        skill_id, target["exp_reward"], operation_id);
    if (!mapp(skill_result) || !skill_result["ok"]) {
        result["error"] = "skill_persist_failed";
        result["pending_recovery"] = 1;
        result["applied"] = applied;
        return result;
    }

    result["ok"] = 1;
    result["exp_awarded"] = target["exp_reward"];
    result["applied"] = applied;
    result["level_after"] = skill_result["level"];
    result["exp_after"] = skill_result["exp"];
    result["levelled_up"] = skill_result["levelled_up"] ? 1 : 0;
    return result;
}
