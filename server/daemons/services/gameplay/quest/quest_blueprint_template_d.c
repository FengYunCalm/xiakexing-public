/**
 * @file    quest_blueprint_template_d.c
 * @brief   任务蓝图系统级模板真源
 */

#pragma strict_types

#include <game/quest.h>

inherit "/lib/framework/daemon";

mapping build_template_payload_with_overrides(string template_id, string quest_id, string provider_id, mapping overrides);
mapping query_template_schema(string template_id);
mapping validate_template_overrides(string template_id, mapping overrides);
mapping query_composed_template_schema(string *template_ids);
mapping validate_composed_template_overrides(string *template_ids, mapping overrides);
mapping build_composed_template_payload(string *template_ids, string quest_id, string provider_id, mapping overrides);

private mapping clone_payload(mapping payload)
{
    if (!mapp(payload))
        return 0;
    return restore_variable(save_variable(payload));
}

private mixed clone_value(mixed value)
{
    return restore_variable(save_variable(value));
}

private int is_allowed_override_field(string field)
{
    return member_array(field, ({
        "name",
        "description",
        "scope",
        "hidden",
        "chain_id",
        "chain_order",
        "next_quests",
        "region",
        "world_key",
        "rewards",
        "unlock",
        "objectives",
        "steps",
    })) != -1;
}

private int value_matches_schema_type(mixed value, string schema_type)
{
    switch (schema_type) {
        case "string":
            return stringp(value);
        case "int":
            return intp(value);
        case "mapping":
            return mapp(value);
        case "array":
            return arrayp(value);
        case "bool":
            return intp(value) && member_array(value, ({ 0, 1 })) != -1;
    }

    return 0;
}

private string *normalize_template_ids(string *template_ids)
{
    string *normalized;
    string template_id;

    normalized = ({ });
    if (!arrayp(template_ids))
        return ({ });

    foreach (template_id in template_ids) {
        if (!stringp(template_id) || template_id == "")
            continue;
        if (member_array(template_id, normalized) != -1)
            continue;
        normalized += ({ template_id });
    }

    return normalized;
}

private string query_composed_quest_type(string *template_ids)
{
    mapping schema;
    string template_id;
    string quest_type;

    quest_type = QUEST_TYPE_TALK;
    foreach (template_id in template_ids) {
        schema = query_template_schema(template_id);
        if (!mapp(schema))
            continue;

        if (schema["quest_type"] == QUEST_TYPE_EXPLORE)
            return QUEST_TYPE_EXPLORE;
        if (schema["quest_type"] == QUEST_TYPE_DELIVERY)
            quest_type = QUEST_TYPE_DELIVERY;
        else if (schema["quest_type"] == QUEST_TYPE_COLLECT && quest_type != QUEST_TYPE_DELIVERY)
            quest_type = QUEST_TYPE_COLLECT;
    }

    return quest_type;
}

private mixed *namespace_template_steps(mixed *steps, string template_id, int template_index)
{
    mixed *result;
    mapping step;
    string step_id;

    result = ({ });
    if (!arrayp(steps))
        return result;

    foreach (step in steps) {
        if (!mapp(step)) {
            result += ({ step });
            continue;
        }

        step = clone_payload(step);
        step_id = step["id"] || "step";
        step["id"] = sprintf("%02d_%s_%s", template_index + 1, template_id, step_id);
        result += ({ step });
    }

    return result;
}

private mapping build_shared_override_schema()
{
    return ([
        "name": ([ "type": "string", "required": 0, "description": "覆盖任务名。" ]),
        "description": ([ "type": "string", "required": 0, "description": "覆盖任务描述。" ]),
        "scope": ([ "type": "string", "required": 0, "description": "覆盖 scope。" ]),
        "hidden": ([ "type": "bool", "required": 0, "description": "覆盖 hidden 标记。" ]),
        "chain_id": ([ "type": "string", "required": 0, "description": "覆盖任务链 ID。" ]),
        "chain_order": ([ "type": "int", "required": 0, "description": "覆盖任务链顺序。" ]),
        "next_quests": ([ "type": "array", "required": 0, "description": "覆盖 next_quests。" ]),
        "region": ([ "type": "string", "required": 0, "description": "覆盖 region 字段。" ]),
        "world_key": ([ "type": "string", "required": 0, "description": "覆盖 world_key 字段。" ]),
        "rewards": ([ "type": "mapping", "required": 0, "description": "整体覆盖 rewards。" ]),
        "unlock": ([ "type": "mapping", "required": 0, "description": "整体覆盖 unlock。" ]),
        "objectives": ([ "type": "array", "required": 0, "description": "整体覆盖 objectives。" ]),
        "steps": ([ "type": "array", "required": 0, "description": "整体覆盖 steps。" ]),
    ]);
}

private mixed apply_placeholder_replacements(mixed value, mapping replacements)
{
    mixed item;
    mapping output;
    mixed *result;
    string key;

    if (stringp(value) && mapp(replacements) && member_array(value, keys(replacements)) != -1)
        return clone_value(replacements[value]);

    if (arrayp(value)) {
        result = ({ });
        foreach (item in value)
            result += ({ apply_placeholder_replacements(item, replacements) });
        return result;
    }

    if (mapp(value)) {
        output = ([]);
        foreach (key in keys(value))
            output[key] = apply_placeholder_replacements(value[key], replacements);
        return output;
    }

    return value;
}

private void collect_unresolved_placeholders(mixed value, mapping seen)
{
    mixed item;
    string key;

    if (!mapp(seen))
        return;

    if (stringp(value)) {
        if (strsrch(value, "TODO_") == 0)
            seen[value] = 1;
        return;
    }

    if (arrayp(value)) {
        foreach (item in value)
            collect_unresolved_placeholders(item, seen);
        return;
    }

    if (mapp(value)) {
        foreach (key in keys(value))
            collect_unresolved_placeholders(value[key], seen);
    }
}

private string *query_unresolved_placeholders(mapping payload)
{
    mapping seen;

    seen = ([]);
    if (!mapp(payload))
        return ({ });
    collect_unresolved_placeholders(payload, seen);
    return sort_array(keys(seen), 1);
}

private mapping build_template_catalog()
{
    mapping field_schema;

    field_schema = build_shared_override_schema();
    return ([
        "talk_basic": ([
            "label": "基础对话",
            "quest_type": QUEST_TYPE_TALK,
            "description": "最小对话任务脚手架，适合 provider 对话引导。",
            "required_placeholders": ({ }),
            "placeholder_schema": ([]),
            "field_schema": field_schema,
            "example_overrides": ([ "name": "对话脚手架", "description": "最小对话模板示例" ]),
        ]),
        "ask_basic": ([
            "label": "基础问答",
            "quest_type": QUEST_TYPE_TALK,
            "description": "最小 ask/topic 任务脚手架，适合菜单式问答链。",
            "required_placeholders": ({ "TODO_TOPIC" }),
            "placeholder_schema": ([
                "TODO_TOPIC": ([ "type": "string", "required": 1, "description": "ask 步骤的 topic。" ]),
            ]),
            "field_schema": field_schema,
            "example_overrides": ([ "TODO_TOPIC": "帮助" ]),
        ]),
        "arrive_basic": ([
            "label": "基础到达",
            "quest_type": QUEST_TYPE_EXPLORE,
            "description": "最小 arrive 任务脚手架，适合路径或巡逻验证。",
            "required_placeholders": ({ }),
            "placeholder_schema": ([]),
            "field_schema": field_schema,
            "example_overrides": ([ "description": "最小到达模板示例" ]),
        ]),
        "give_basic": ([
            "label": "基础交付",
            "quest_type": QUEST_TYPE_DELIVERY,
            "description": "最小 give 任务脚手架，适合道具交付链。",
            "required_placeholders": ({ "TODO_ITEM_ID" }),
            "placeholder_schema": ([
                "TODO_ITEM_ID": ([ "type": "string", "required": 1, "description": "交付的物品模板 ID。" ]),
            ]),
            "field_schema": field_schema,
            "example_overrides": ([ "TODO_ITEM_ID": "" ]),
        ]),
        "consume_basic": ([
            "label": "基础消耗",
            "quest_type": QUEST_TYPE_COLLECT,
            "description": "最小 consume 任务脚手架，适合道具使用/消耗链。",
            "required_placeholders": ({ "TODO_ITEM_ID" }),
            "placeholder_schema": ([
                "TODO_ITEM_ID": ([ "type": "string", "required": 1, "description": "消耗的物品模板 ID。" ]),
            ]),
            "field_schema": field_schema,
            "example_overrides": ([ "TODO_ITEM_ID": "" ]),
        ]),
        "ask_arrive_report": ([
            "label": "问路到达回报",
            "quest_type": QUEST_TYPE_EXPLORE,
            "description": "多步骤脚手架：询问 -> 到达 -> 回报，适合运行时路径验证。",
            "required_placeholders": ({ "TODO_TOPIC", "TODO_ROOM" }),
            "placeholder_schema": ([
                "TODO_TOPIC": ([ "type": "string", "required": 1, "description": "ask 步骤的 topic。" ]),
                "TODO_ROOM": ([ "type": "string", "required": 1, "description": "arrive 步骤的目标房间路径。" ]),
            ]),
            "field_schema": field_schema,
            "example_overrides": ([ "TODO_TOPIC": "帮助", "TODO_ROOM": "TODO_ROOM_PATH" ]),
        ]),
        "give_feedback_loop": ([
            "label": "交付反馈回路",
            "quest_type": QUEST_TYPE_DELIVERY,
            "description": "多步骤脚手架：对话 -> 交付 -> 回报，适合交付型系统链。",
            "required_placeholders": ({ "TODO_ITEM_ID" }),
            "placeholder_schema": ([
                "TODO_ITEM_ID": ([ "type": "string", "required": 1, "description": "交付物品模板 ID。" ]),
            ]),
            "field_schema": field_schema,
            "example_overrides": ([ "TODO_ITEM_ID": "" ]),
        ]),
        "consume_feedback_loop": ([
            "label": "消耗反馈回路",
            "quest_type": QUEST_TYPE_COLLECT,
            "description": "多步骤脚手架：对话 -> 消耗 -> 回报，适合使用/消耗型系统链。",
            "required_placeholders": ({ "TODO_ITEM_ID" }),
            "placeholder_schema": ([
                "TODO_ITEM_ID": ([ "type": "string", "required": 1, "description": "消耗物品模板 ID。" ]),
            ]),
            "field_schema": field_schema,
            "example_overrides": ([ "TODO_ITEM_ID": "" ]),
        ]),
        "hidden_followup_chain": ([
            "label": "隐藏后续链节点",
            "quest_type": QUEST_TYPE_TALK,
            "description": "隐藏任务链后继脚手架，适合需要 unlock/chain 元数据的系统任务。",
            "required_placeholders": ({ "TODO_CHAIN_ID", "TODO_PREV_QUEST_ID" }),
            "compose_mode": "single_only",
            "placeholder_schema": ([
                "TODO_CHAIN_ID": ([ "type": "string", "required": 1, "description": "任务链 ID。" ]),
                "TODO_PREV_QUEST_ID": ([ "type": "string", "required": 1, "description": "前置任务 ID。" ]),
            ]),
            "field_schema": field_schema,
            "example_overrides": ([ "TODO_CHAIN_ID": "newbie_chain", "TODO_PREV_QUEST_ID": "quest_intro_01" ]),
        ]),
    ]);
}

string *query_template_ids()
{
    return sort_array(keys(build_template_catalog()), 1);
}

mapping query_template_spec(string template_id)
{
    mapping catalog;

    catalog = build_template_catalog();
    if (!stringp(template_id) || !mapp(catalog[template_id]))
        return 0;
    return clone_payload(catalog[template_id]);
}

mapping query_template_schema(string template_id)
{
    mapping spec;

    spec = query_template_spec(template_id);
    if (!mapp(spec))
        return 0;
    return ([
        "template_id": template_id,
        "label": spec["label"] || "",
        "quest_type": spec["quest_type"] || "",
        "compose_mode": spec["compose_mode"] || "append_steps",
        "description": spec["description"] || "",
        "required_placeholders": spec["required_placeholders"] || ({ }),
        "placeholder_schema": clone_payload(spec["placeholder_schema"] || ([])),
        "field_schema": clone_payload(spec["field_schema"] || ([])),
        "example_overrides": clone_payload(spec["example_overrides"] || ([])),
    ]);
}

mapping validate_template_overrides(string template_id, mapping overrides)
{
    mapping schema;
    mapping placeholder_schema;
    mapping field_schema;
    mapping field_spec;
    string *missing_required;
    string key;
    string schema_type;
    mixed value;

    schema = query_template_schema(template_id);
    if (!mapp(schema))
        return ([ "ok": 0, "errors": ({ "template_not_found" }), "warnings": ({ }), "missing_required": ({ }) ]);
    if (!mapp(overrides))
        overrides = ([]);

    placeholder_schema = schema["placeholder_schema"] || ([]);
    field_schema = schema["field_schema"] || ([]);
    missing_required = ({ });
    foreach (key in keys(placeholder_schema)) {
        field_spec = placeholder_schema[key];
        if (field_spec["required"] && member_array(key, keys(overrides)) == -1)
            missing_required += ({ key });
    }

    foreach (key in keys(overrides)) {
        value = overrides[key];
        if (mapp(placeholder_schema[key]))
            field_spec = placeholder_schema[key];
        else if (mapp(field_schema[key]))
            field_spec = field_schema[key];
        else
            return ([ "ok": 0, "errors": ({ "unknown_override:" + key }), "warnings": ({ }), "missing_required": missing_required ]);

        schema_type = field_spec["type"] || "";
        if (!value_matches_schema_type(value, schema_type))
            return ([ "ok": 0, "errors": ({ "invalid_type:" + key + ":" + schema_type }), "warnings": ({ }), "missing_required": missing_required ]);
    }

    return ([
        "ok": 1,
        "errors": ({ }),
        "warnings": ({ }),
        "missing_required": sort_array(missing_required, 1),
        "schema": schema,
    ]);
}

mapping query_composed_template_schema(string *template_ids)
{
    mapping component_schemas;
    mapping shared_schema;
    mapping example_overrides;
    mapping schema;
    string *normalized_ids;
    string template_id;
    mixed *required_entries;

    normalized_ids = normalize_template_ids(template_ids);
    if (!sizeof(normalized_ids))
        return 0;

    component_schemas = ([]);
    required_entries = ({ });
    foreach (template_id in normalized_ids) {
        schema = query_template_schema(template_id);
        if (!mapp(schema))
            return 0;
        component_schemas[template_id] = schema;
        if (schema["compose_mode"] != "append_steps")
            return 0;
        if (sizeof(schema["required_placeholders"] || ({ })))
            required_entries += ({ ([ "template_id": template_id, "placeholders": schema["required_placeholders"] ]) });
    }

    shared_schema = build_shared_override_schema();
    example_overrides = ([ "templates": ([ ]) ]);
    foreach (template_id in normalized_ids)
        example_overrides["templates"][template_id] = component_schemas[template_id]["example_overrides"] || ([]);

    return ([
        "template_ids": normalized_ids,
        "quest_type": query_composed_quest_type(normalized_ids),
        "component_schemas": component_schemas,
        "shared_field_schema": shared_schema,
        "required_placeholders": required_entries,
        "example_overrides": example_overrides,
    ]);
}

mapping validate_composed_template_overrides(string *template_ids, mapping overrides)
{
    mapping component_schemas;
    mapping component_overrides;
    mapping field_schema;
    mapping field_spec;
    mapping result;
    mapping schema;
    mixed value;
    string key;
    string template_id;

    schema = query_composed_template_schema(template_ids);
    if (!mapp(schema))
        return ([ "ok": 0, "errors": ({ "invalid_template_ids" }), "warnings": ({ }), "component_results": ([]), "schema": 0 ]);

    if (!mapp(overrides))
        overrides = ([]);
    component_overrides = mapp(overrides["templates"]) ? overrides["templates"] : ([]);
    field_schema = schema["shared_field_schema"] || ([]);
    component_schemas = schema["component_schemas"] || ([]);

    foreach (key in keys(overrides)) {
        if (key == "templates")
            continue;
        if (!mapp(field_schema[key]))
            return ([ "ok": 0, "errors": ({ "unknown_shared_override:" + key }), "warnings": ({ }), "component_results": ([]), "schema": schema ]);
        field_spec = field_schema[key];
        value = overrides[key];
        if (!value_matches_schema_type(value, field_spec["type"] || ""))
            return ([ "ok": 0, "errors": ({ "invalid_shared_type:" + key + ":" + (field_spec["type"] || "") }), "warnings": ({ }), "component_results": ([]), "schema": schema ]);
    }

    result = ([]);
    foreach (template_id in schema["template_ids"]) {
        result[template_id] = validate_template_overrides(template_id, component_overrides[template_id]);
        if (!mapp(result[template_id]) || !result[template_id]["ok"])
            return ([ "ok": 0, "errors": ({ "invalid_component_override:" + template_id }), "warnings": ({ }), "component_results": result, "schema": schema ]);
    }

    return ([
        "ok": 1,
        "errors": ({ }),
        "warnings": ({ }),
        "component_results": result,
        "schema": schema,
    ]);
}

mapping build_template_payload(string template_id, string quest_id, string provider_id)
{
    return build_template_payload_with_overrides(template_id, quest_id, provider_id, 0);
}

mapping build_template_payload_with_overrides(string template_id, string quest_id, string provider_id, mapping overrides)
{
    mapping spec;
    mapping payload;
    mapping replacements;
    string key;

    spec = query_template_spec(template_id);
    if (!mapp(spec) || !stringp(quest_id) || quest_id == "" || !stringp(provider_id) || provider_id == "")
        return 0;

    payload = ([
        "name": sprintf("%s_%s", template_id, quest_id),
        "description": sprintf("由 %s 模板生成的系统级草稿，请按真实任务语义补全字段。", template_id),
        "scope": QUEST_SCOPE_PERSONAL,
        "objectives": ({ }),
        "steps": ({ }),
        "rewards": ([ "exp": 1 ]),
        "unlock": ([ ]),
        "quest_type": spec["quest_type"],
        "template_id": template_id,
        "required_placeholders": spec["required_placeholders"] || ({ }),
    ]);

    switch (template_id) {
        case "talk_basic":
            payload["objectives"] = ({ ([ "desc": "与发布者完成一次对话", "target": provider_id, "count": 1 ]) });
            payload["steps"] = ({ ([ "id": "talk_provider", "type": QUEST_STEP_TALK, "desc": "与发布者对话", "target": provider_id, "count": 1, "conditions": ({ }), "effects": ({ }) ]) });
            break;
        case "ask_basic":
            payload["objectives"] = ({ ([ "desc": "向发布者询问指定主题", "target": provider_id, "count": 1 ]) });
            payload["steps"] = ({ ([ "id": "ask_provider_topic", "type": QUEST_STEP_ASK, "desc": "向发布者询问主题", "target": provider_id, "topic": "TODO_TOPIC", "count": 1, "conditions": ({ }), "effects": ({ }) ]) });
            break;
        case "arrive_basic":
            payload["objectives"] = ({ ([ "desc": "抵达指定房间", "room": "TODO_ROOM_PATH", "count": 1 ]) });
            payload["steps"] = ({ ([ "id": "arrive_target_room", "type": QUEST_STEP_ARRIVE, "desc": "抵达目标房间", "room": "TODO_ROOM_PATH", "count": 1, "conditions": ({ }), "effects": ({ }) ]) });
            break;
        case "give_basic":
            payload["objectives"] = ({ ([ "desc": "向发布者交付指定物品", "target": provider_id, "item_id": "TODO_ITEM_ID", "count": 1 ]) });
            payload["steps"] = ({ ([ "id": "give_target_item", "type": QUEST_STEP_GIVE, "desc": "向发布者交付物品", "target": provider_id, "item_id": "TODO_ITEM_ID", "count": 1, "conditions": ({ }), "effects": ({ }) ]) });
            break;
        case "consume_basic":
            payload["objectives"] = ({ ([ "desc": "消耗指定物品一次", "item_id": "TODO_ITEM_ID", "count": 1 ]) });
            payload["steps"] = ({ ([ "id": "consume_target_item", "type": QUEST_STEP_CONSUME, "desc": "消耗指定物品", "item_id": "TODO_ITEM_ID", "count": 1, "conditions": ({ }), "effects": ({ }) ]) });
            break;
        case "ask_arrive_report":
            payload["objectives"] = ({
                ([ "desc": "向发布者询问路径主题", "target": provider_id, "count": 1 ]),
                ([ "desc": "抵达指定房间", "room": "TODO_ROOM", "count": 1 ]),
                ([ "desc": "回到发布者处回报", "target": provider_id, "count": 1 ]),
            });
            payload["steps"] = ({
                ([ "id": "ask_provider_topic", "type": QUEST_STEP_ASK, "desc": "向发布者询问主题", "target": provider_id, "topic": "TODO_TOPIC", "count": 1, "conditions": ({ }), "effects": ({ }) ]),
                ([ "id": "arrive_target_room", "type": QUEST_STEP_ARRIVE, "desc": "抵达目标房间", "room": "TODO_ROOM", "count": 1, "conditions": ({ }), "effects": ({ }) ]),
                ([ "id": "report_to_provider", "type": QUEST_STEP_TALK, "desc": "向发布者回报", "target": provider_id, "count": 1, "conditions": ({ }), "effects": ({ }) ]),
            });
            break;
        case "give_feedback_loop":
            payload["objectives"] = ({
                ([ "desc": "先和发布者确认交付要求", "target": provider_id, "count": 1 ]),
                ([ "desc": "交付指定物品", "target": provider_id, "item_id": "TODO_ITEM_ID", "count": 1 ]),
                ([ "desc": "交付后再次确认", "target": provider_id, "count": 1 ]),
            });
            payload["steps"] = ({
                ([ "id": "brief_provider", "type": QUEST_STEP_TALK, "desc": "与发布者确认交付要求", "target": provider_id, "count": 1, "conditions": ({ }), "effects": ({ }) ]),
                ([ "id": "give_target_item", "type": QUEST_STEP_GIVE, "desc": "向发布者交付物品", "target": provider_id, "item_id": "TODO_ITEM_ID", "count": 1, "conditions": ({ }), "effects": ({ }) ]),
                ([ "id": "confirm_delivery", "type": QUEST_STEP_TALK, "desc": "向发布者确认交付完成", "target": provider_id, "count": 1, "conditions": ({ }), "effects": ({ }) ]),
            });
            break;
        case "consume_feedback_loop":
            payload["objectives"] = ({
                ([ "desc": "先和发布者确认使用要求", "target": provider_id, "count": 1 ]),
                ([ "desc": "消耗指定物品", "item_id": "TODO_ITEM_ID", "count": 1 ]),
                ([ "desc": "回到发布者处确认结果", "target": provider_id, "count": 1 ]),
            });
            payload["steps"] = ({
                ([ "id": "brief_provider", "type": QUEST_STEP_TALK, "desc": "与发布者确认使用要求", "target": provider_id, "count": 1, "conditions": ({ }), "effects": ({ }) ]),
                ([ "id": "consume_target_item", "type": QUEST_STEP_CONSUME, "desc": "消耗指定物品", "item_id": "TODO_ITEM_ID", "count": 1, "conditions": ({ }), "effects": ({ }) ]),
                ([ "id": "confirm_consume", "type": QUEST_STEP_TALK, "desc": "向发布者确认消耗完成", "target": provider_id, "count": 1, "conditions": ({ }), "effects": ({ }) ]),
            });
            break;
        case "hidden_followup_chain":
            payload["name"] = sprintf("hidden_followup_%s", quest_id);
            payload["hidden"] = 1;
            payload["chain_id"] = "TODO_CHAIN_ID";
            payload["chain_order"] = 2;
            payload["next_quests"] = ({ });
            payload["unlock"] = ([ "completed_quests": ({ "TODO_PREV_QUEST_ID" }) ]);
            payload["objectives"] = ({ ([ "desc": "向发布者确认后续阶段", "target": provider_id, "count": 1 ]) });
            payload["steps"] = ({ ([ "id": "followup_talk", "type": QUEST_STEP_TALK, "desc": "与发布者确认后续阶段", "target": provider_id, "count": 1, "conditions": ({ }), "effects": ({ }) ]) });
            break;
        default:
            return 0;
    }

    if (mapp(overrides)) {
        replacements = ([]);
        foreach (key in keys(overrides)) {
            if (strsrch(key, "TODO_") == 0)
                replacements[key] = overrides[key];
            else if (is_allowed_override_field(key))
                payload[key] = clone_value(overrides[key]);
        }

        payload = apply_placeholder_replacements(payload, replacements);
    }

    payload["required_placeholders"] = query_unresolved_placeholders(payload);
    return clone_payload(payload);
}

mapping build_composed_template_payload(string *template_ids, string quest_id, string provider_id, mapping overrides)
{
    mapping component_overrides;
    mapping component_payload;
    mapping payload;
    mapping validation;
    mixed *objectives;
    mixed *steps;
    string *normalized_ids;
    string key;
    string template_id;
    int index;

    normalized_ids = normalize_template_ids(template_ids);
    validation = validate_composed_template_overrides(normalized_ids, overrides);
    if (!mapp(validation) || !validation["ok"] || !mapp(validation["schema"]))
        return 0;

    if (!mapp(overrides))
        overrides = ([]);
    component_overrides = mapp(overrides["templates"]) ? overrides["templates"] : ([]);
    objectives = ({ });
    steps = ({ });
    payload = ([
        "name": sprintf("compose_%s", quest_id),
        "description": sprintf("由模板 %s 组合生成的系统级草稿，请按真实任务语义补全字段。", implode(normalized_ids, ",")),
        "scope": QUEST_SCOPE_PERSONAL,
        "objectives": ({ }),
        "steps": ({ }),
        "rewards": ([ "exp": 1 ]),
        "unlock": ([ ]),
        "quest_type": validation["schema"]["quest_type"] || QUEST_TYPE_TALK,
        "template_id": "compose",
        "template_ids": normalized_ids,
        "required_placeholders": ({ }),
    ]);

    for (index = 0; index < sizeof(normalized_ids); index++) {
        template_id = normalized_ids[index];
        component_payload = build_template_payload_with_overrides(template_id, quest_id, provider_id, component_overrides[template_id]);
        if (!mapp(component_payload))
            return 0;
        objectives += component_payload["objectives"] || ({ });
        steps += namespace_template_steps(component_payload["steps"] || ({ }), template_id, index);
    }

    payload["objectives"] = objectives;
    payload["steps"] = steps;
    foreach (key in keys(overrides)) {
        if (key == "templates")
            continue;
        if (is_allowed_override_field(key))
            payload[key] = clone_value(overrides[key]);
    }

    payload["required_placeholders"] = query_unresolved_placeholders(payload);
    return clone_payload(payload);
}
