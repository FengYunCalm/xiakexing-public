/**
 * @file    quest_validate.c
 * @brief   任务定义校验工具,校验任务结构字段合法性。
 */

#pragma strict_types

#include <game/quest.h>
#include <system/services.h>

#define QUEST_STEP_MACHINE_HELPER "/daemons/services/gameplay/quest/quest_step_machine"

private int is_string_array(mixed value)
{
    mixed item;

    if (!arrayp(value))
        return 0;

    foreach (item in value) {
        if (!stringp(item) || item == "")
            return 0;
    }

    return 1;
}

private int unlock_has_effective_condition(mapping unlock)
{
    mapping rep_map;

    if (!mapp(unlock))
        return 0;
    if (arrayp(unlock["completed_quests"]) && sizeof(unlock["completed_quests"]) > 0)
        return 1;
    if (arrayp(unlock["any_completed_quests"]) && sizeof(unlock["any_completed_quests"]) > 0)
        return 1;
    if (intp(unlock["min_skill_cap"]) && unlock["min_skill_cap"] > 0)
        return 1;
    if (stringp(unlock["sect_id"]) && unlock["sect_id"] != "")
        return 1;
    if (mapp(unlock["player_vars"]) && sizeof(keys(unlock["player_vars"])) > 0)
        return 1;

    rep_map = unlock["reputation"];
    if (mapp(rep_map) && sizeof(keys(rep_map)) > 0)
        return 1;

    return 0;
}

private string find_unresolved_template_placeholder(mixed value)
{
    mixed item;
    string key;
    string found;

    if (stringp(value)) {
        if (strsrch(value, "TODO_") != -1)
            return value;
        if (strsrch(value, "模板生成的系统级草稿，请按真实任务语义补全字段") != -1)
            return "TEMPLATE_DESCRIPTION";
        return "";
    }

    if (arrayp(value)) {
        foreach (item in value) {
            found = find_unresolved_template_placeholder(item);
            if (stringp(found) && found != "")
                return found;
        }
        return "";
    }

    if (mapp(value)) {
        foreach (key in keys(value)) {
            found = find_unresolved_template_placeholder(value[key]);
            if (stringp(found) && found != "")
                return found;
        }
    }

    return "";
}

private void push_issue(mapping report, string quest_id, string level, string message)
{
    string bucket;

    if (!mapp(report) || !stringp(quest_id) || !stringp(level) || !stringp(message))
        return;

    if (!mapp(report["quests"][quest_id]))
        report["quests"][quest_id] = ([ "errors": ({ }), "warnings": ({ }) ]);

    bucket = lower_case(level) == "error" ? "errors" : "warnings";
    report["quests"][quest_id][bucket] += ({ message });
    report[bucket] += ({ sprintf("[%s] %s", quest_id, message) });
}

private void validate_unlock(mapping defs, mapping report, string quest_id, mapping unlock)
{
    string ref_id;
    mapping rep_map;
    string rep_key;

    if (!mapp(unlock)) {
        push_issue(report, quest_id, "error", "hidden 任务必须提供 unlock 条件。") ;
        return;
    }

    if (unlock["completed_quests"] && !is_string_array(unlock["completed_quests"]))
        push_issue(report, quest_id, "error", "unlock.completed_quests 必须是 string 数组。");
    if (arrayp(unlock["completed_quests"]) && sizeof(unlock["completed_quests"]) == 0)
        push_issue(report, quest_id, "error", "unlock.completed_quests 不能为空。");
    if (unlock["any_completed_quests"] && !is_string_array(unlock["any_completed_quests"]))
        push_issue(report, quest_id, "error", "unlock.any_completed_quests 必须是 string 数组。");
    if (arrayp(unlock["any_completed_quests"]) && sizeof(unlock["any_completed_quests"]) == 0)
        push_issue(report, quest_id, "error", "unlock.any_completed_quests 不能为空。");
    if (unlock["min_skill_cap"] && (!intp(unlock["min_skill_cap"]) || unlock["min_skill_cap"] < 1))
        push_issue(report, quest_id, "error", "unlock.min_skill_cap 必须是正整数。");
    if (unlock["sect_id"] && (!stringp(unlock["sect_id"]) || unlock["sect_id"] == ""))
        push_issue(report, quest_id, "error", "unlock.sect_id 必须是非空字符串。");
    if (unlock["player_vars"] && !mapp(unlock["player_vars"]))
        push_issue(report, quest_id, "error", "unlock.player_vars 必须是 mapping。");

    if (is_string_array(unlock["completed_quests"])) {
        foreach (ref_id in unlock["completed_quests"]) {
            if (!mapp(defs[ref_id]))
                push_issue(report, quest_id, "error", "unlock.completed_quests 引用了不存在的任务 " + ref_id + "。");
        }
    }

    if (is_string_array(unlock["any_completed_quests"])) {
        foreach (ref_id in unlock["any_completed_quests"]) {
            if (!mapp(defs[ref_id]))
                push_issue(report, quest_id, "error", "unlock.any_completed_quests 引用了不存在的任务 " + ref_id + "。");
        }
    }

    rep_map = unlock["reputation"];
    if (mapp(rep_map)) {
        foreach (rep_key in keys(rep_map)) {
            if (!intp(rep_map[rep_key]))
                push_issue(report, quest_id, "error", "unlock.reputation 的阈值必须是整数。") ;
        }
    } else if (unlock["reputation"]) {
        push_issue(report, quest_id, "error", "unlock.reputation 必须是 mapping。") ;
    }
}

private void validate_auto_accept_follow_ups(mapping defs, mapping report,
    string quest_id, mapping quest_def)
{
    mixed auto_accept;
    mixed next_quests;
    mapping seen;
    string next_id;

    if (!mapp(quest_def)
        || member_array("auto_accept_next_quests", keys(quest_def)) == -1)
        return;

    auto_accept = quest_def["auto_accept_next_quests"];
    if (!is_string_array(auto_accept) || sizeof(auto_accept) == 0) {
        push_issue(report, quest_id, "error",
            "auto_accept_next_quests 必须是非空 string 数组。");
        return;
    }

    next_quests = quest_def["next_quests"];
    seen = ([]);
    foreach (next_id in auto_accept) {
        if (seen[next_id]) {
            push_issue(report, quest_id, "error",
                "auto_accept_next_quests 含有重复任务 " + next_id + "。");
            continue;
        }
        seen[next_id] = 1;
        if (next_id == quest_id) {
            push_issue(report, quest_id, "error",
                "auto_accept_next_quests 不能指向自身。");
            continue;
        }
        if (!mapp(defs[next_id])) {
            push_issue(report, quest_id, "error",
                "auto_accept_next_quests 引用了不存在的任务 " + next_id + "。");
            continue;
        }
        if (!arrayp(next_quests) || member_array(next_id, next_quests) == -1)
            push_issue(report, quest_id, "error",
                "auto_accept_next_quests 的任务必须同时声明在 next_quests 中："
                + next_id + "。");
    }
}

/* L-142：条件/效果 op 与步骤字段白名单——与 quest_step_machine 的运行时
 * 真源共用（query_step_condition_ops / query_step_effect_ops /
 * query_step_definition_fields），未知即内容错误，发布前台拦下。 */
private void validate_step_op_whitelist(mapping report, string quest_id,
    mapping step)
{
    string *condition_ops;
    string *effect_ops;
    string *allowed_fields;
    mixed entry;
    string op;
    string field;
    string step_id;

    if (!mapp(step))
        return;
    step_id = stringp(step["id"]) ? step["id"] : "step";
    condition_ops = call_other(QUEST_STEP_MACHINE_HELPER,
        "query_step_condition_ops");
    effect_ops = call_other(QUEST_STEP_MACHINE_HELPER, "query_step_effect_ops");
    allowed_fields = call_other(QUEST_STEP_MACHINE_HELPER,
        "query_step_definition_fields");
    if (!arrayp(condition_ops) || !arrayp(effect_ops)
        || !arrayp(allowed_fields))
        return;

    if (arrayp(step["conditions"])) {
        foreach (entry in step["conditions"]) {
            if (!mapp(entry)) {
                push_issue(report, quest_id, "error",
                    step_id + " conditions 存在非 mapping 条目。");
                continue;
            }
            op = stringp(entry["op"]) ? entry["op"] : "";
            if (member_array(op, condition_ops) == -1)
                push_issue(report, quest_id, "error", step_id
                    + " 未知条件 op「" + op + "」，运行时不会生效。");
        }
    }
    if (arrayp(step["effects"])) {
        foreach (entry in step["effects"]) {
            if (!mapp(entry)) {
                push_issue(report, quest_id, "error",
                    step_id + " effects 存在非 mapping 条目。");
                continue;
            }
            op = stringp(entry["op"]) ? entry["op"] : "";
            if (member_array(op, effect_ops) == -1)
                push_issue(report, quest_id, "error", step_id
                    + " 未知效果 op「" + op + "」，运行时不会生效。");
        }
    }
    foreach (field in keys(step)) {
        if (member_array(field, allowed_fields) != -1)
            continue;
        push_issue(report, quest_id, "error", step_id
            + " 未知步骤字段「" + field + "」，运行时不会保留。");
    }
}

private void validate_steps(mapping report, string quest_id, mapping quest_def,
    mapping kill_targets)
{
    mixed *steps;
    mixed step;
    string step_type;
    string target;
    int released;

    released = member_array("released", keys(quest_def)) == -1 || quest_def["released"] != 0;
    steps = quest_def["steps"];
    if (!arrayp(steps) || sizeof(steps) == 0) {
        if (released)
            push_issue(report, quest_id, "warning", "没有 steps，任务会退回 legacy progress 模式。") ;
        return;
    }

    foreach (step in steps) {
        if (!mapp(step)) {
            push_issue(report, quest_id, "error", "steps 中存在非 mapping 条目。") ;
            continue;
        }

        step_type = step["type"];
        if (!stringp(step_type) || step_type == "") {
            push_issue(report, quest_id, "error", "step 缺少 type。") ;
            continue;
        }

        target = step["target"];
        switch (step_type) {
            case QUEST_STEP_KILL:
                if (!stringp(target) || target == "")
                    push_issue(report, quest_id, "error", step["id"] + " 缺少 target。") ;
                /* 击杀事件由战斗结算读取 NPC 的 quest_kill_target 触发；内容层
                 * 没有实体声明该目标时，步骤会静默卡住——内容校验前置拦截。 */
                else if (mapp(kill_targets) && !kill_targets[target])
                    push_issue(report, quest_id, "error", step["id"] + " 的 target「"
                        + target + "」没有任何实体声明 quest_kill_target，击杀不会推进进度。") ;
                break;
            case QUEST_STEP_TALK:
                if (!stringp(target) || target == "")
                    push_issue(report, quest_id, "error", step["id"] + " 缺少 target。") ;
                break;
            case QUEST_STEP_ASK:
                if (!stringp(target) || target == "")
                    push_issue(report, quest_id, "error", step["id"] + " 缺少 target。") ;
                if (!stringp(step["topic"]) || step["topic"] == "")
                    push_issue(report, quest_id, "warning", step["id"] + " 未指定 topic，容易误触发。") ;
                break;
            case QUEST_STEP_GIVE:
                if (!stringp(target) || target == "")
                    push_issue(report, quest_id, "error", step["id"] + " 缺少 target。") ;
                if (!stringp(step["item_id"]) || step["item_id"] == "")
                    push_issue(report, quest_id, "error", step["id"] + " 缺少 item_id。") ;
                else if (!ITEM_D->item_exists(step["item_id"]))
                    push_issue(report, quest_id, "error", step["id"] + " 的 item_id「"
                        + step["item_id"] + "」不存在，交付不会推进。") ;
                break;
            case QUEST_STEP_COLLECT:
                if (!stringp(step["item_id"]) || step["item_id"] == "")
                    push_issue(report, quest_id, "error", step["id"] + " 缺少 item_id。") ;
                else if (!ITEM_D->item_exists(step["item_id"]))
                    push_issue(report, quest_id, "error", step["id"] + " 的 item_id「"
                        + step["item_id"] + "」不存在，收集不会推进。") ;
                break;
            case QUEST_STEP_ARRIVE:
                if (!stringp(step["room"]) || step["room"] == "")
                    push_issue(report, quest_id, "error", step["id"] + " 缺少 room。") ;
                else if (file_size(step["room"] + ".c") <= 0)
                    push_issue(report, quest_id, "warning", step["id"] + " 的 room「"
                        + step["room"] + "」没有对应房间文件，到达可能不会触发。") ;
                break;
            case QUEST_STEP_ESCORT:
                if (!stringp(target) || target == "")
                    push_issue(report, quest_id, "error", step["id"] + " 缺少 target。") ;
                if (!stringp(step["room"]) || step["room"] == "")
                    push_issue(report, quest_id, "error", step["id"] + " 缺少 room。") ;
                else if (file_size(step["room"] + ".c") <= 0)
                    push_issue(report, quest_id, "warning", step["id"] + " 的 room「"
                        + step["room"] + "」没有对应房间文件，护送终点无法到达。") ;
                break;
            case QUEST_STEP_CONSUME:
                if (!stringp(step["item_id"]) || step["item_id"] == "")
                    push_issue(report, quest_id, "error", step["id"] + " 缺少 item_id。") ;
                else if (!ITEM_D->item_exists(step["item_id"]))
                    push_issue(report, quest_id, "error", step["id"] + " 的 item_id「"
                        + step["item_id"] + "」不存在，消耗不会推进。") ;
                if (step["action"] && (!stringp(step["action"]) || step["action"] == ""))
                    push_issue(report, quest_id, "error", step["id"] + " action 非法。") ;
                break;
            case QUEST_STEP_LOOK:
                if ((!stringp(target) || target == "")
                    && (!stringp(step["room"]) || step["room"] == ""))
                    push_issue(report, quest_id, "error", step["id"] + " 缺少 target 或 room。") ;
                else if (stringp(step["room"]) && step["room"] != ""
                    && file_size(step["room"] + ".c") <= 0)
                    push_issue(report, quest_id, "warning", step["id"] + " 的 room「"
                        + step["room"] + "」没有对应房间文件，查看不会触发。") ;
                break;
            case QUEST_STEP_ACTION:
                if (!stringp(step["action"]) || step["action"] == "")
                    push_issue(report, quest_id, "error", step["id"] + " 缺少 action。") ;
                if (step["outcome"] && (!stringp(step["outcome"]) || step["outcome"] == ""))
                    push_issue(report, quest_id, "error", step["id"] + " outcome 非法。") ;
                if (step["service_event"] && !intp(step["service_event"]))
                    push_issue(report, quest_id, "error", step["id"] + " service_event 必须是整数标记。") ;
                break;
            default:
                /* 未知步骤类型是内容错误：运行时不会处理，发布前台必须拦住
                 * （曾只发 warning，拼错类型的步骤会静默卡关）。 */
                push_issue(report, quest_id, "error", "未知步骤类型 " + step_type + "，运行时不会处理。") ;
        }

        if (step["conditions"] && !arrayp(step["conditions"]))
            push_issue(report, quest_id, "error", step["id"] + " conditions 必须是数组。") ;
        if (step["effects"] && !arrayp(step["effects"]))
            push_issue(report, quest_id, "error", step["id"] + " effects 必须是数组。") ;
        /* L-142：未知条件/效果 op 与未知步骤字段在发布时拒绝。 */
        validate_step_op_whitelist(report, quest_id, step);

        if (member_array(target, ({ "guard" })) != -1)
            push_issue(report, quest_id, "warning", step["id"] + " 使用了易歧义目标 id「" + target + "」，建议改成唯一显示名。") ;
    }
}

private string query_reward_item_id(mixed entry)
{
    if (stringp(entry))
        return entry;
    if (mapp(entry))
        return entry["template_id"] || entry["item_id"] || entry["item"] || entry["id"] || "";
    return "";
}

/* 奖励键 schema：奖励链（reward_apply_d）只消费固定键集合，其余键会被
 * 静默丢弃——内容层写错键名既不报错也不发奖（历史 QUEST_REWARD_SKILL 即此类）。 */
private void validate_reward_keys(mapping report, string quest_id, mapping rewards)
{
    string *known_keys;
    string key;

    if (!mapp(rewards))
        return;
    known_keys = ({ "exp", "potential", "gold", "sect_contribution", "reputation",
        "title", "item", "items", "attachments" });
    foreach (key in keys(rewards)) {
        if (member_array(key, known_keys) != -1)
            continue;
        push_issue(report, quest_id, "error",
            "rewards 含奖励链未消费的键「" + key + "」，会被静默丢弃。") ;
    }
}

private void validate_reward_items(mapping report, string quest_id, mapping rewards)
{
    mixed *attachments;
    mixed entry;
    string item_id;

    if (!mapp(rewards))
        return;

    if (arrayp(rewards["items"]))
        push_issue(report, quest_id, "error", "rewards.items 不会被正式奖励链消费，请改用 attachments。") ;

    item_id = rewards["item"];
    if (stringp(item_id) && item_id != "" && !ITEM_D->item_exists(item_id))
        push_issue(report, quest_id, "error", "rewards.item 引用了不存在的物品模板 " + item_id + "。") ;

    attachments = arrayp(rewards["attachments"]) ? rewards["attachments"] : ({ });
    foreach (entry in attachments) {
        item_id = query_reward_item_id(entry);
        if (!stringp(item_id) || item_id == "")
            push_issue(report, quest_id, "error", "rewards.attachments 存在非法物品条目。") ;
        else if (!ITEM_D->item_exists(item_id))
            push_issue(report, quest_id, "error", "rewards.attachments 引用了不存在的物品模板 " + item_id + "。") ;
    }
}

private void validate_escort_definition(mapping report, string quest_id, mapping quest_def)
{
    mapping escort;
    string target;
    string destination;

    if (quest_def["type"] != QUEST_TYPE_ESCORT)
        return;

    escort = quest_def["escort"];
    target = mapp(escort) ? escort["target"] : quest_def["escort_target"];
    destination = mapp(escort) ? escort["destination"] : quest_def["escort_destination"];

    if (!stringp(target) || target == "")
        push_issue(report, quest_id, "error", "escort 任务缺少 escort.target。") ;
    if (!stringp(destination) || destination == "")
        push_issue(report, quest_id, "error", "escort 任务缺少 escort.destination。") ;
}

private void validate_template_placeholders(mapping report, string quest_id, mapping quest_def)
{
    string placeholder;

    placeholder = find_unresolved_template_placeholder(quest_def);
    if (stringp(placeholder) && placeholder != "")
        push_issue(report, quest_id, "error", "仍包含未替换模板占位「" + placeholder + "」。") ;
}

/* L-151：任务定义顶层字段真源——蓝图 set_field/发布与 artifact 导入共用；
 * 未知字段写入会在运行期静默失效，必须在入口拒绝。 */
string *query_quest_definition_fields()
{
    return ({
        "id", "name", "provider", "type", "scope", "description", "hidden",
        "released", "steps", "objectives", "rewards", "unlock", "region",
        "world_key", "chain_id", "chain_order", "next_quests",
        "auto_accept_next_quests", "escort", "escort_target",
        "escort_destination", "puzzle", "state_scope", "instance_scope",
    });
}

mapping query_quest_definition_field_types()
{
    return ([
        "id": "string", "name": "string", "provider": "string",
        "type": "string", "scope": "string", "description": "string",
        "region": "string", "world_key": "string", "chain_id": "string",
        "state_scope": "string", "instance_scope": "string",
        "escort_target": "string", "escort_destination": "string",
        "hidden": "int", "released": "int", "chain_order": "int",
        "steps": "array", "objectives": "array", "next_quests": "array",
        "auto_accept_next_quests": "array",
        "rewards": "mapping", "unlock": "mapping", "escort": "mapping",
        "puzzle": "mapping",
    ]);
}

mapping validate_all_quest_definitions(mapping defs)
{
    mapping report;
    string quest_id;
    mapping quest_def;
    string scope;
    string next_id;
    mapping kill_targets;

    report = ([
        "ok": 1,
        "errors": ({ }),
        "warnings": ({ }),
        "quests": ([])
    ]);

    if (!mapp(defs)) {
        push_issue(report, "<system>", "error", "quest definitions 不是 mapping。") ;
        report["ok"] = 0;
        return report;
    }

    /* 击杀目标承载索引一次取齐：kill 步骤 target 必须能被内容实体声明的
     * quest_kill_target 命中，否则击杀事件不会推进进度。 */
    kill_targets = CONTENT_REGISTRY_D->query_quest_kill_targets();

    foreach (quest_id, quest_def in defs) {
        if (!mapp(quest_def)) {
            push_issue(report, quest_id, "error", "任务定义不是 mapping。") ;
            continue;
        }

        if (!stringp(quest_def["id"]) || quest_def["id"] == "")
            push_issue(report, quest_id, "error", "缺少 id。") ;
        else if (quest_def["id"] != quest_id)
            push_issue(report, quest_id, "warning", "mapping key 与 quest_def.id 不一致。") ;

        if (!stringp(quest_def["name"]) || quest_def["name"] == "")
            push_issue(report, quest_id, "error", "缺少 name。") ;
        if (!stringp(quest_def["provider"]) || quest_def["provider"] == "")
            push_issue(report, quest_id, "error", "缺少 provider。") ;
        if (!stringp(quest_def["type"]) || quest_def["type"] == "")
            push_issue(report, quest_id, "error", "缺少 type。") ;
        if (!stringp(quest_def["description"]) || quest_def["description"] == "")
            push_issue(report, quest_id, "warning", "缺少 description。") ;
        if (!mapp(quest_def["rewards"]))
            push_issue(report, quest_id, "warning", "缺少 rewards。") ;
        else {
            validate_reward_items(report, quest_id, quest_def["rewards"]);
            validate_reward_keys(report, quest_id, quest_def["rewards"]);
        }

        scope = quest_def["scope"];
        if (stringp(scope) && member_array(scope, ({ QUEST_SCOPE_PERSONAL, QUEST_SCOPE_REGION, QUEST_SCOPE_WORLD })) == -1)
            push_issue(report, quest_id, "error", "scope 非法。") ;
        if (scope == QUEST_SCOPE_REGION && (!stringp(quest_def["region"]) || quest_def["region"] == ""))
            push_issue(report, quest_id, "warning", "region scope 建议补 region 字段。") ;
        if (scope == QUEST_SCOPE_WORLD && (!stringp(quest_def["world_key"]) || quest_def["world_key"] == ""))
            push_issue(report, quest_id, "warning", "world scope 建议补 world_key 字段。") ;

        if (quest_def["hidden"] && !mapp(quest_def["unlock"]))
            push_issue(report, quest_id, "error", "hidden 任务缺少 unlock 条件。") ;
        else if (quest_def["hidden"] && !unlock_has_effective_condition(quest_def["unlock"]))
            push_issue(report, quest_id, "error", "hidden 任务的 unlock 不能为空规则壳。");
        if (mapp(quest_def["unlock"]))
            validate_unlock(defs, report, quest_id, quest_def["unlock"]);

        if (stringp(quest_def["chain_id"]) && quest_def["chain_id"] != "") {
            if (!intp(quest_def["chain_order"]) || quest_def["chain_order"] < 1)
                push_issue(report, quest_id, "warning", "chain_id 已设置，但 chain_order 缺失或非法。") ;
        }
        if (is_string_array(quest_def["next_quests"])) {
            foreach (next_id in quest_def["next_quests"]) {
                if (!mapp(defs[next_id]))
                    push_issue(report, quest_id, "warning", "next_quests 引用了不存在的任务 " + next_id + "。") ;
            }
        } else if (quest_def["next_quests"]) {
            push_issue(report, quest_id, "warning", "next_quests 必须是 string 数组。") ;
        }
        validate_auto_accept_follow_ups(defs, report, quest_id, quest_def);

        if (quest_def["type"] == QUEST_TYPE_PUZZLE && !mapp(quest_def["puzzle"]))
            push_issue(report, quest_id, "warning", "解密任务缺少 puzzle 元数据。") ;
        validate_escort_definition(report, quest_id, quest_def);
        if (mapp(quest_def["puzzle"])) {
            if (!stringp(quest_def["puzzle"]["kind"]) || quest_def["puzzle"]["kind"] == "")
                push_issue(report, quest_id, "warning", "puzzle.kind 缺失。") ;
            if (!stringp(quest_def["puzzle"]["prompt"]) && !stringp(quest_def["puzzle"]["hint"]))
                push_issue(report, quest_id, "warning", "puzzle 至少应提供 prompt 或 hint。") ;
        }

        validate_template_placeholders(report, quest_id, quest_def);
        validate_steps(report, quest_id, quest_def, kill_targets);
    }

    report["ok"] = sizeof(report["errors"]) == 0;
    return report;
}

private int player_satisfies_unlock(object player, mapping data, mapping defs, mapping unlock)
{
    string quest_id;
    string current_sect_id;
    mapping rep_map;
    string rep_key;

    if (!mapp(unlock))
        return 1;

    if (intp(unlock["min_skill_cap"]) && objectp(player)
        && !NUMERIC_RULE_D->player_meets_skill_cap_gate(player, unlock["min_skill_cap"]))
        return 0;
    if (stringp(unlock["sect_id"]) && unlock["sect_id"] != "") {
        current_sect_id = SECT_D->query_player_sect_id(player);
        if (unlock["sect_id"] == "*") {
            if (current_sect_id == "")
                return 0;
        } else if (current_sect_id != unlock["sect_id"]) {
            return 0;
        }
    }
    if (mapp(unlock["player_vars"])) {
        mapping player_vars;
        string key;

        player_vars = mapp(data["player_vars"]) ? data["player_vars"] : ([]);
        foreach (key in keys(unlock["player_vars"])) {
            if (player_vars[key] != unlock["player_vars"][key])
                return 0;
        }
    }

    if (is_string_array(unlock["completed_quests"])) {
        foreach (quest_id in unlock["completed_quests"]) {
            if (!call_other("/daemons/services/gameplay/quest/quest_query", "is_quest_completed", data, quest_id))
                return 0;
        }
    }

    if (is_string_array(unlock["any_completed_quests"]) && sizeof(unlock["any_completed_quests"]) > 0) {
        int any_ok;

        foreach (quest_id in unlock["any_completed_quests"]) {
            if (call_other("/daemons/services/gameplay/quest/quest_query", "is_quest_completed", data, quest_id)) {
                any_ok = 1;
                break;
            }
        }
        if (!any_ok)
            return 0;
    }

    rep_map = unlock["reputation"];
    if (mapp(rep_map) && objectp(player)) {
        foreach (rep_key in keys(rep_map)) {
            if (REPUTATION_D->query_reputation_scope(player, rep_key) < rep_map[rep_key])
                return 0;
        }
    }

    return 1;
}

int is_quest_visible_for_player(mapping defs, mapping data, object player, string quest_id)
{
    mapping quest_def;
    mapping quest_info;
    int released;

    quest_def = defs[quest_id];
    if (!mapp(quest_def))
        return 0;

    quest_info = call_other("/daemons/services/gameplay/quest/quest_query",
        "query_player_quest_record", data, quest_id);
    if (mapp(quest_info))
        return 1;

    if (member_array("released", keys(quest_def)) == -1)
        released = 1;
    else
        released = quest_def["released"] != 0;
    if (!released)
        return 0;

    return player_satisfies_unlock(player, data, defs, quest_def["unlock"]);
}

int can_accept_quest(mapping defs, mapping data, object player, string quest_id)
{
    mapping quest_info;
    mapping quest_def;
    mapping daily_log;
    int status;

    quest_def = mapp(defs) ? defs[quest_id] : 0;
    /* 日常任务：同一天已完成后不可再接（按游戏日） */
    if (mapp(quest_def) && quest_def["daily"]) {
        daily_log = mapp(data) ? data["daily_log"] : 0;
        if (mapp(daily_log) && intp(daily_log[quest_id])
            && daily_log[quest_id] == TIME_D->query_day())
            return 0;
    }

    quest_info = call_other("/daemons/services/gameplay/quest/quest_query",
        "query_player_quest_record", data, quest_id);
    if (mapp(quest_info)) {
        status = quest_info["status"] || QUEST_STATUS_AVAILABLE;
        return status == QUEST_STATUS_FAILED;
    }

    return is_quest_visible_for_player(defs, data, player, quest_id);
}

string render_validation_report(mapping report)
{
    string text;
    string quest_id;

    if (!mapp(report))
        return "【任务定义校验】无结果。\n";

    text = sprintf("【任务定义校验】errors=%d warnings=%d\n",
        sizeof(report["errors"] || ({ })), sizeof(report["warnings"] || ({ })));
    foreach (quest_id in sort_array(keys(report["quests"] || ([])), 1)) {
        mapping item;
        string *errors;
        string *warnings;
        string msg;

        item = report["quests"][quest_id];
        errors = item["errors"] || ({ });
        warnings = item["warnings"] || ({ });
        if (!sizeof(errors) && !sizeof(warnings))
            continue;

        text += sprintf("- %s\n", quest_id);
        foreach (msg in errors)
            text += sprintf("  [ERROR] %s\n", msg);
        foreach (msg in warnings)
            text += sprintf("  [WARN ] %s\n", msg);
    }

    if (sizeof(report["errors"] || ({ })) == 0 && sizeof(report["warnings"] || ({ })) == 0)
        text += "未发现问题。\n";
    return text;
}
