/**
 * @file    fishing_runtime_d.c
 * @brief   生活技能钓鱼运行时真源。
 *
 * @note
 * 只负责钓鱼结算：契约归一化、渔获权重 roll、奖励发放、成长与采集进度上报。
 * 不持有钓点配置（在区域包 resource_nodes 切片），也不提供公共命令入口（fish 命令层只做解析）。
 * 经验/等级与防刷 budget 复用 life_skill 体系（budget key 与 gather 同级：life_skill_gather）。
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
        "catch_item": "",
        "catch_count": 0,
        "exp_awarded": 0,
        "level_before": 0,
        "level_after": 0,
        "exp_before": 0,
        "exp_after": 0,
        "levelled_up": 0,
        "operation_id": "",
        "applied": 0,
    ]);
}

private mapping normalize_fishing_contract(string skill_id, mixed raw_target)
{
    mapping contract;
    string reward_item;
    int reward_count;
    int exp_reward;
    int required_level;

    if (!stringp(skill_id) || skill_id == "" || !mapp(raw_target))
        return 0;
    if (raw_target["contract"] != "fishing_contract")
        return 0;
    if (!stringp(raw_target["skill_id"]) || raw_target["skill_id"] != skill_id)
        return 0;
    if (!stringp(raw_target["target_id"]) || raw_target["target_id"] == "")
        return 0;
    if (!mapp(raw_target["catch_table"]))
        return 0;

    reward_count = intp(raw_target["reward_count"]) && raw_target["reward_count"] > 0
        ? raw_target["reward_count"] : 1;
    exp_reward = intp(raw_target["exp_reward"]) && raw_target["exp_reward"] >= 0
        ? raw_target["exp_reward"] : 5;
    required_level = intp(raw_target["required_level"]) && raw_target["required_level"] >= 0
        ? raw_target["required_level"] : 0;
    reward_item = stringp(raw_target["reward_item"]) ? raw_target["reward_item"] : "";

    return ([
        "contract": "fishing_contract",
        "skill_id": skill_id,
        "target_id": raw_target["target_id"],
        "target_name": stringp(raw_target["target_name"]) && raw_target["target_name"] != ""
            ? raw_target["target_name"] : "钓点",
        "catch_table": copy(raw_target["catch_table"]),
        "fallback_item": reward_item,
        "reward_count": reward_count,
        "exp_reward": exp_reward,
        "required_level": required_level,
    ]);
}

/* 渔获权重 roll：catch_table 按权重取 key，权重非正或表空则回退 fallback_item */
private string roll_catch_item(mapping catch_table, string fallback_item)
{
    string item_id;
    int total;
    int roll;
    int acc;

    if (!mapp(catch_table) || sizeof(catch_table) == 0)
        return stringp(fallback_item) && fallback_item != "" ? fallback_item : "";

    total = 0;
    foreach (item_id, mixed weight in catch_table) {
        if (intp(weight) && weight > 0)
            total += weight;
    }
    if (total <= 0)
        return stringp(fallback_item) && fallback_item != "" ? fallback_item : "";

    roll = random(total) + 1;
    acc = 0;
    foreach (item_id, mixed weight in catch_table) {
        if (!intp(weight) || weight <= 0)
            continue;
        acc += weight;
        if (roll <= acc)
            return item_id;
    }
    return stringp(fallback_item) && fallback_item != "" ? fallback_item : "";
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

/* 与 gather_runtime 平行的采集奖励处理。 */
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

private string build_fishing_operation_id(object player, string target_id,
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

private void record_fishing_collect_progress(object player, mapping applied)
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

mapping perform_fishing(object player, string skill_id, mixed raw_target, mapping options)
{
    mapping result;
    mapping skill_def;
    mapping contract;
    mapping budget;
    mapping budget_commit;
    mapping applied;
    mapping skill_result;
    string catch_item;
    string operation_id;
    int catch_count;

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
    if (!mapp(skill_def) || skill_def["activity"] != "fish") {
        result["error"] = "invalid_fishing_skill";
        return result;
    }

    contract = normalize_fishing_contract(skill_id, raw_target);
    if (!mapp(contract)) {
        result["error"] = "invalid_fishing_contract";
        return result;
    }

    result["target_id"] = contract["target_id"];
    result["target_name"] = contract["target_name"];
    result["level_before"] = LIFE_SKILL_D->get_skill_level(player, skill_id);
    result["exp_before"] = player->query("life_skills_exp/" + skill_id) || 0;
    if (result["level_before"] < contract["required_level"]) {
        result["error"] = "insufficient_skill_level";
        return result;
    }

    budget = NUMERIC_RULE_D->query_action_budget_state(player,
        "life_skill_gather", skill_id, ([ "target_key": contract["target_id"] ]));
    if (!mapp(budget) || !budget["ok"]) {
        result["error"] = mapp(budget)
            ? (budget["error"] || "action_budget_failed") : "action_budget_failed";
        return result;
    }
    catch_item = roll_catch_item(contract["catch_table"], contract["fallback_item"]);
    if (catch_item == "") {
        result["error"] = "no_catch_item";
        return result;
    }
    catch_count = contract["reward_count"];
    operation_id = build_fishing_operation_id(player, contract["target_id"],
        budget, options);
    result["operation_id"] = operation_id;

    if (mapp(options) && options["dry_run"]) {
        result["ok"] = 1;
        result["catch_item"] = catch_item;
        result["catch_count"] = catch_count;
        return result;
    }

    budget_commit = NUMERIC_RULE_D->commit_action_budget_event(player, budget);
    if (!mapp(budget_commit) || !budget_commit["ok"]) {
        result["error"] = "action_budget_commit_failed";
        return result;
    }

    applied = REWARD_APPLY_D->apply_reward_package(player, ([
        "item": catch_item,
        "item_count": catch_count,
    ]), ([
        "drop_on_full": mapp(options) && options["drop_on_full"] ? 1 : 0,
        "source": "life_skill_fish:" + contract["target_id"],
        "target_key": operation_id,
        "idempotent": 1,
        "persist": 1,
    ]));
    if (!mapp(applied) || !applied["ok"]) {
        result["error"] = mapp(applied) && stringp(applied["error"])
            ? applied["error"] : "reward_apply_failed";
        return result;
    }

    record_fishing_collect_progress(player, applied);

    skill_result = LIFE_SKILL_D->apply_skill_experience_operation(player,
        skill_id, contract["exp_reward"], operation_id);
    if (!mapp(skill_result) || !skill_result["ok"]) {
        result["error"] = "skill_persist_failed";
        result["pending_recovery"] = 1;
        result["applied"] = applied;
        return result;
    }

    result["ok"] = 1;
    result["catch_item"] = catch_item;
    result["catch_count"] = catch_count;
    result["exp_awarded"] = contract["exp_reward"];
    result["applied"] = applied;
    result["level_after"] = skill_result["level"];
    result["exp_after"] = skill_result["exp"];
    result["levelled_up"] = skill_result["levelled_up"] ? 1 : 0;
    return result;
}
