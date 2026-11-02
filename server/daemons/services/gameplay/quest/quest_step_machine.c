/**
 * @file    quest_step_machine.c
 * @brief   任务步进状态机,归一化目标计数与步进判定。
 */

#pragma strict_types

#include <game/quest.h>

#define QUEST_TRANSITION_HELPER "/daemons/services/gameplay/quest/quest_transition_d"

/* L-142：步骤运行态字段白名单——build_step_state 与发布校验
 * （quest_validate.validate_steps）共用同一真源；新增字段必须两处同步。
 * 定义侧额外允许 "prompt"（desc 的 fallback，见 resolve_step_description）。 */
string *query_step_state_fields()
{
    return ({
        "id", "type", "desc", "action_label", "target", "room", "item_id",
        "action", "outcome", "topic", "command", "service_event",
        "conditions", "effects", "count", "current", "completed",
    });
}

string *query_step_definition_fields()
{
    return query_step_state_fields() + ({ "prompt" });
}

/* L-142：条件/效果 op 白名单（与 evaluate_condition / apply_effect 的
 * switch 分支一一对应）；发布校验拒绝未知 op，运行时未知 op fail-closed。 */
string *query_step_condition_ops()
{
    return ({ "var_eq", "var_gte", "player_var_eq", "player_var_gte" });
}

string *query_step_effect_ops()
{
    return ({ "set_var", "inc_var", "set_player_var", "inc_player_var" });
}

private int normalize_target_count(mixed value)
{
    if (intp(value) && value > 0)
        return value;
    return 1;
}

private string normalize_step_id(mapping step_def, int index)
{
    string step_id;

    if (!mapp(step_def))
        return sprintf("step_%d", index);

    step_id = step_def["id"];
    if (!stringp(step_id) || step_id == "")
        step_id = sprintf("step_%d", index);
    return step_id;
}

private string resolve_step_description(mapping step_def, string fallback)
{
    string description;

    if (!mapp(step_def))
        return fallback;

    description = step_def["desc"];
    if (!stringp(description) || description == "")
        description = step_def["prompt"];
    return stringp(description) && description != "" ? description : fallback;
}

private mapping build_step_state(mapping step_def, mapping old_state, int legacy_progress, int index)
{
    mapping step_state;
    int target_count;
    int current_count;

    if (!mapp(step_def))
        return 0;

    target_count = normalize_target_count(step_def["count"]);
    current_count = mapp(old_state) && intp(old_state["current"]) ? old_state["current"] : 0;
    if (!mapp(old_state) && legacy_progress > 0 && index == 0)
        current_count = legacy_progress;
    if (current_count > target_count)
        current_count = target_count;

    step_state = ([
        "id": normalize_step_id(step_def, index),
        "type": step_def["type"] || "",
        "desc": resolve_step_description(step_def, "未命名步骤"),
        "action_label": step_def["action_label"] || "",
        "target": step_def["target"] || "",
        "room": step_def["room"] || "",
        "item_id": step_def["item_id"] || "",
        "action": step_def["action"] || "",
        "outcome": step_def["outcome"] || "",
        "topic": step_def["topic"] || "",
        "command": step_def["command"] || "",
        "service_event": step_def["service_event"] ? 1 : 0,
        "conditions": arrayp(step_def["conditions"]) ? copy(step_def["conditions"]) : ({ }),
        "effects": arrayp(step_def["effects"]) ? copy(step_def["effects"]) : ({ }),
        "count": target_count,
        "current": current_count,
        "completed": current_count >= target_count
    ]);

    return step_state;
}

private int find_first_incomplete_step(mixed *steps)
{
    int index;

    if (!arrayp(steps) || sizeof(steps) == 0)
        return -1;

    for (index = 0; index < sizeof(steps); index++) {
        if (!mapp(steps[index]))
            continue;
        if (!(steps[index]["completed"] || 0))
            return index;
    }

    return sizeof(steps);
}

private int payload_matches_target(mixed raw, string expected)
{
    string *items;

    if (!stringp(expected) || expected == "")
        return 0;

    if (stringp(raw))
        return raw == expected;

    if (!arrayp(raw))
        return 0;

    items = filter_array(raw, (: stringp($1) && $1 != "" :));
    return member_array(expected, items) != -1;
}

private int step_matches_event(mapping step_state, string event_name, mapping payload)
{
    string step_type;
    string topic;
    string action;
    string outcome;

    if (!mapp(step_state) || !stringp(event_name) || !mapp(payload))
        return 0;

    step_type = step_state["type"];
    switch (step_type) {
        case QUEST_STEP_KILL:
            return stringp(payload["target"]) && payload["target"] == step_state["target"];

        case QUEST_STEP_TALK:
            return event_name == "talk"
                && payload_matches_target(payload["target_ids"], step_state["target"]);

        case QUEST_STEP_ASK:
            topic = step_state["topic"];
            if (event_name != "ask")
                return 0;
            if (!payload_matches_target(payload["target_ids"], step_state["target"]))
                return 0;
            if (stringp(topic) && topic != "")
                return stringp(payload["topic"]) && payload["topic"] == topic;
            return 1;

        case QUEST_STEP_ARRIVE:
            return event_name == "arrive"
                && stringp(payload["room"])
                && payload["room"] == step_state["room"];

        case QUEST_STEP_ESCORT:
            if (event_name != "escort")
                return 0;
            if (stringp(step_state["target"]) && step_state["target"] != ""
                && !payload_matches_target(payload["target_ids"], step_state["target"]))
                return 0;
            if (stringp(step_state["room"]) && step_state["room"] != ""
                && (!stringp(payload["room"]) || payload["room"] != step_state["room"]))
                return 0;
            return (stringp(step_state["target"]) && step_state["target"] != "")
                || (stringp(step_state["room"]) && step_state["room"] != "");

        case QUEST_STEP_GIVE:
            return event_name == "give"
                && payload_matches_target(payload["target_ids"], step_state["target"])
                && payload_matches_target(payload["item_ids"], step_state["item_id"]);

        case QUEST_STEP_COLLECT:
            return event_name == "collect"
                && payload_matches_target(payload["item_ids"], step_state["item_id"]);

        case QUEST_STEP_CONSUME:
            action = step_state["action"];
            if (event_name != "consume")
                return 0;
            if (!payload_matches_target(payload["item_ids"], step_state["item_id"]))
                return 0;
            if (stringp(action) && action != "")
                return stringp(payload["action"]) && payload["action"] == action;
            return 1;

        case QUEST_STEP_LOOK:
            if (event_name != "look")
                return 0;
            if (payload_matches_target(payload["target_ids"], step_state["target"]))
                return 1;
            return stringp(step_state["room"]) && step_state["room"] != ""
                && stringp(payload["room"])
                && payload["room"] == step_state["room"];

        case QUEST_STEP_ACTION:
            action = step_state["action"];
            outcome = step_state["outcome"];
            if (event_name != "action")
                return 0;
            if (stringp(action) && action != ""
                && (!stringp(payload["action"]) || payload["action"] != action))
                return 0;
            if (stringp(step_state["target"]) && step_state["target"] != ""
                && !payload_matches_target(payload["target_ids"], step_state["target"]))
                return 0;
            if (stringp(step_state["room"]) && step_state["room"] != ""
                && (!stringp(payload["room"]) || payload["room"] != step_state["room"]))
                return 0;
            if (stringp(outcome) && outcome != ""
                && (!stringp(payload["outcome"]) || payload["outcome"] != outcome))
                return 0;
            return stringp(action) && action != "";

        default:
            if (event_name == "progress")
                return 1;
    }

    return 0;
}

private void sync_legacy_progress(mapping quest_info)
{
    int index;
    mapping step_state;

    if (!mapp(quest_info) || !arrayp(quest_info["steps"]))
        return;

    index = quest_info["current_step"];
    if (intp(index) && index >= 0 && index < sizeof(quest_info["steps"])) {
        step_state = quest_info["steps"][index];
        if (mapp(step_state)) {
            quest_info["progress"] = step_state["current"] || 0;
            return;
        }
    }

    if (sizeof(quest_info["steps"]) > 0 && mapp(quest_info["steps"][<1]))
        quest_info["progress"] = quest_info["steps"][<1]["count"] || 0;
    else
        quest_info["progress"] = 0;
}

private mapping ensure_quest_vars(mapping quest_info)
{
    if (!mapp(quest_info["vars"]))
        quest_info["vars"] = ([]);
    return quest_info["vars"];
}

private mapping ensure_player_vars(mapping player_data)
{
    if (!mapp(player_data["player_vars"]))
        player_data["player_vars"] = ([]);
    return player_data["player_vars"];
}

private int evaluate_condition(mapping condition, mapping quest_info, mapping player_data)
{
    mapping vars;
    mapping player_vars;
    string op;
    string key;
    mixed value;

    if (!mapp(condition))
        return 1;

    vars = ensure_quest_vars(quest_info);
    player_vars = ensure_player_vars(player_data);
    op = condition["op"];
    key = condition["key"];
    value = condition["value"];

    switch (op) {
        case "var_eq":
            return vars[key] == value;
        case "var_gte":
            return intp(vars[key]) && intp(value) && vars[key] >= value;
        case "player_var_eq":
            return player_vars[key] == value;
        case "player_var_gte":
            return intp(player_vars[key]) && intp(value) && player_vars[key] >= value;
        default:
            /* L-142：未知条件 op fail-closed（发布校验已拒绝未知 op；
             * 运行时兜底不再默认放行）。 */
            debug_message(sprintf(
                "[quest_step_machine] unknown condition op=%O\n", op));
            return 0;
    }
}

private int conditions_pass(mapping step_state, mapping quest_info, mapping player_data)
{
    mixed condition;
    mixed *conditions;

    conditions = step_state["conditions"];
    if (!arrayp(conditions) || sizeof(conditions) == 0)
        return 1;

    foreach (condition in conditions) {
        if (!evaluate_condition(condition, quest_info, player_data))
            return 0;
    }

    return 1;
}

/* L-142：效果应用返回是否全部识别并应用；未知 op 不得静默忽略。 */
private int apply_effect(mapping effect, mapping quest_info, mapping player_data)
{
    mapping vars;
    mapping player_vars;
    string op;
    string key;
    mixed value;

    if (!mapp(effect))
        return 0;

    vars = ensure_quest_vars(quest_info);
    player_vars = ensure_player_vars(player_data);
    op = effect["op"];
    key = effect["key"];
    value = effect["value"];

    switch (op) {
        case "set_var":
            vars[key] = value;
            return 1;
        case "inc_var":
            if (!intp(vars[key]))
                vars[key] = 0;
            vars[key] += intp(value) ? value : 1;
            return 1;
        case "set_player_var":
            player_vars[key] = value;
            return 1;
        case "inc_player_var":
            if (!intp(player_vars[key]))
                player_vars[key] = 0;
            player_vars[key] += intp(value) ? value : 1;
            return 1;
    }
    debug_message(sprintf("[quest_step_machine] unknown effect op=%O key=%O\n",
        op, key));
    return 0;
}

private int apply_step_effects(mapping step_state, mapping quest_info, mapping player_data)
{
    mixed effect;
    mixed *effects;
    int applied;

    effects = step_state["effects"];
    if (!arrayp(effects) || sizeof(effects) == 0)
        return 1;

    applied = 1;
    foreach (effect in effects) {
        if (!apply_effect(effect, quest_info, player_data))
            applied = 0;
    }
    return applied;
}

mapping build_initial_quest_runtime(mapping quest_def)
{
    mapping quest_info;
    mixed *step_defs;
    mixed *steps;
    int index;

    quest_info = ([
        "status": QUEST_STATUS_ACCEPTED,
        "accepted_at": time(),
        "progress": 0,
        "current_step": 0,
        "runtime_version": QUEST_RUNTIME_VERSION,
        "steps": ({ }),
        "vars": ([])
    ]);

    step_defs = mapp(quest_def) && arrayp(quest_def["steps"]) ? quest_def["steps"] : ({ });
    steps = ({ });
    for (index = 0; index < sizeof(step_defs); index++)
        steps += ({ build_step_state(step_defs[index], 0, 0, index) });

    if (sizeof(steps) > 0) {
        quest_info["steps"] = steps;
        quest_info["current_step"] = find_first_incomplete_step(steps);
        sync_legacy_progress(quest_info);
    }

    return quest_info;
}

void normalize_quest_runtime(mapping quest_definitions, mapping player_data)
{
    string bucket;
    string quest_id;
    mapping quest_info;
    mapping quest_def;
    mixed *step_defs;
    mixed *existing_steps;
    mixed *steps;
    int legacy_progress;
    int index;
    int first_incomplete;

    if (!mapp(quest_definitions) || !mapp(player_data))
        return;

    foreach (bucket in ({ "active", "completed", "abandoned" })) {
        if (!mapp(player_data[bucket]))
            continue;

        foreach (quest_id, quest_info in player_data[bucket]) {
            if (!mapp(quest_info))
                continue;

            quest_def = quest_definitions[quest_id];
            if (!mapp(quest_def))
                continue;

            step_defs = arrayp(quest_def["steps"]) ? quest_def["steps"] : ({ });
            if (sizeof(step_defs) == 0) {
                quest_info["runtime_version"] = QUEST_RUNTIME_VERSION;
                continue;
            }

            existing_steps = arrayp(quest_info["steps"]) ? quest_info["steps"] : ({ });
            legacy_progress = intp(quest_info["progress"]) ? quest_info["progress"] : 0;
            steps = ({ });

            for (index = 0; index < sizeof(step_defs); index++) {
                mapping old_state;

                old_state = index < sizeof(existing_steps) && mapp(existing_steps[index])
                    ? existing_steps[index]
                    : 0;
                steps += ({ build_step_state(step_defs[index], old_state, legacy_progress, index) });
            }

            quest_info["steps"] = steps;
            first_incomplete = find_first_incomplete_step(steps);
            quest_info["current_step"] = first_incomplete;
            quest_info["runtime_version"] = QUEST_RUNTIME_VERSION;
            if (!mapp(quest_info["vars"]))
                quest_info["vars"] = ([]);

            if (first_incomplete >= sizeof(steps)
                && quest_info["status"] == QUEST_STATUS_ACCEPTED)
                quest_info["status"] = QUEST_STATUS_COMPLETED;

            sync_legacy_progress(quest_info);
        }
    }
}

mixed *build_objective_view(mapping quest_def, mapping quest_info)
{
    mixed *step_defs;
    mixed *steps;
    mixed *result;
    int index;

    step_defs = mapp(quest_def) && arrayp(quest_def["steps"]) ? quest_def["steps"] : ({ });
    if (sizeof(step_defs) == 0)
        return 0;

    steps = mapp(quest_info) && arrayp(quest_info["steps"]) ? quest_info["steps"] : ({ });
    result = ({ });
    for (index = 0; index < sizeof(step_defs); index++) {
        mapping step_state;
        mapping step_def;
        int target_count;
        int current_count;

        step_def = step_defs[index];
        step_state = index < sizeof(steps) && mapp(steps[index]) ? steps[index] : 0;
        target_count = normalize_target_count(step_def["count"]);
        current_count = mapp(step_state) ? (step_state["current"] || 0) : 0;
        result += ({ ([
            "desc": resolve_step_description(step_def, "未命名目标"),
            "current": current_count,
            "target": target_count,
            "completed": mapp(step_state) ? (step_state["completed"] || 0) : 0
        ]) });
    }

    return result;
}

int can_turn_in_runtime(mapping quest_definitions, mapping player_data, string quest_id)
{
    mapping quest_def;
    mapping quest_info;
    mixed *steps;

    if (!mapp(quest_definitions) || !mapp(player_data) || !stringp(quest_id) || quest_id == "")
        return 0;

    quest_def = quest_definitions[quest_id];
    if (!mapp(quest_def))
        return 0;

    quest_info = player_data["active"][quest_id];
    if (!mapp(quest_info))
        quest_info = player_data["completed"][quest_id];
    if (!mapp(quest_info))
        return 0;

    steps = quest_info["steps"];
    if (!arrayp(steps) || sizeof(steps) == 0)
        return 0;

    return quest_info["status"] == QUEST_STATUS_COMPLETED;
}

int record_event_progress(mapping quest_definitions, mapping player_data, string event_name, mapping payload)
{
    string quest_id;
    mapping quest_def;
    mapping quest_info;
    mapping step_state;
    mixed *steps;
    int step_index;
    int target_count;
    int increment;
    int updated;
    int next_index;

    if (!mapp(quest_definitions) || !mapp(player_data) || !mapp(player_data["active"]))
        return 0;

    foreach (quest_id, quest_info in player_data["active"]) {
        if (!mapp(quest_info) || quest_info["status"] != QUEST_STATUS_ACCEPTED)
            continue;

        if (event_name == "progress"
            && stringp(payload["quest_id"]) && payload["quest_id"] != quest_id)
            continue;

        quest_def = quest_definitions[quest_id];
        if (!mapp(quest_def))
            continue;

        steps = quest_info["steps"];
        if (!arrayp(steps) || sizeof(steps) == 0)
            continue;

        step_index = quest_info["current_step"];
        if (!intp(step_index) || step_index < 0 || step_index >= sizeof(steps))
            step_index = find_first_incomplete_step(steps);
        if (step_index < 0 || step_index >= sizeof(steps))
            continue;

        step_state = steps[step_index];
        if (!mapp(step_state) || !step_matches_event(step_state, event_name, payload))
            continue;
        if (!conditions_pass(step_state, quest_info, player_data))
            continue;

        target_count = normalize_target_count(step_state["count"]);
        increment = intp(payload["count"]) && payload["count"] > 0 ? payload["count"] : 1;
        if (step_state["type"] != QUEST_STEP_KILL
            && step_state["type"] != QUEST_STEP_GIVE
            && step_state["type"] != QUEST_STEP_COLLECT)
            increment = target_count;

        step_state["current"] = (step_state["current"] || 0) + increment;
        if (step_state["current"] > target_count)
            step_state["current"] = target_count;
        if (step_state["current"] >= target_count)
            step_state["completed"] = 1;
        if (step_state["completed"]
            && !apply_step_effects(step_state, quest_info, player_data)) {
            /* L-142：未知效果 op 不得静默忽略；该步退回未完成等待内容修复
             * （发布校验已拒绝未知 op，运行时不在此处静默吞掉）。 */
            step_state["completed"] = 0;
        }

        next_index = find_first_incomplete_step(steps);
        quest_info["current_step"] = next_index;
        if (next_index >= sizeof(steps))
            call_other(QUEST_TRANSITION_HELPER, "set_completed_if_ready", player_data, quest_id, 1);
        sync_legacy_progress(quest_info);
        updated = 1;
    }

    return updated;
}

string *query_matching_active_quests(mapping quest_definitions, mapping player_data,
    string event_name, mapping payload)
{
    string quest_id;
    mapping quest_def;
    mapping quest_info;
    mapping step_state;
    mixed *steps;
    string *matches;
    int step_index;

    if (!mapp(quest_definitions) || !mapp(player_data) || !mapp(player_data["active"]) || !mapp(payload))
        return ({ });

    matches = ({ });
    foreach (quest_id, quest_info in player_data["active"]) {
        if (!mapp(quest_info) || quest_info["status"] != QUEST_STATUS_ACCEPTED)
            continue;

        quest_def = quest_definitions[quest_id];
        if (!mapp(quest_def))
            continue;

        steps = quest_info["steps"];
        if (!arrayp(steps) || sizeof(steps) == 0)
            continue;

        step_index = quest_info["current_step"];
        if (!intp(step_index) || step_index < 0 || step_index >= sizeof(steps))
            step_index = find_first_incomplete_step(steps);
        if (step_index < 0 || step_index >= sizeof(steps))
            continue;

        step_state = steps[step_index];
        if (!mapp(step_state) || !step_matches_event(step_state, event_name, payload))
            continue;
        if (!conditions_pass(step_state, quest_info, player_data))
            continue;

        matches += ({ quest_id });
    }

    return matches;
}
