/**
 * @file    questcreate.c
 * @brief   任务蓝图创建命令



 *
 * @par Description:
 * 管理员任务蓝图入口。第一版只开放给 wizard/admin，
 * 负责 runtime 草稿创建、校验与发布，不承担公开玩家 UGC 平台职责。
 */

#pragma strict_types

#include <system/ansi.h>
#include <system/services.h>

inherit "/lib/framework/command";
inherit "/lib/std/command_utils";

int help(object me);
private int is_questcreate_operator(object me);
private string resolve_actor_id(object me);
private mapping parse_lpc_payload(string payload);
private mapping parse_scaffold_payload(string rest);
private mapping parse_compose_payload(string rest);
private mapping parse_import_payload(string rest);
private string format_blueprint_summary(mapping record);
private int do_list(object me, string rest);
private int do_templates(object me);
private int do_schema(object me, string template_id);
private int do_artifact_schema(object me);
private int do_registry(object me);
private int do_export(object me, string rest);
private int do_import(object me, string rest);
private int do_scaffold(object me, string rest);
private int do_compose_schema(object me, string rest);
private int do_compose(object me, string rest);
private int do_show(object me, string quest_id);
private int do_init(object me, string rest);
private int do_set(object me, string rest);
private int do_objectives(object me, string rest);
private int do_steps(object me, string rest);
private int do_rewards(object me, string rest);
private int do_unlock(object me, string rest);
private int do_validate(object me, string quest_id);
private int do_publish(object me, string quest_id);
private int do_unpublish(object me, string quest_id);
private int do_delete(object me, string quest_id);
private int dispatch_action(object me, string action, string rest);

private string trim_payload_prefix(string payload)
{
    while (stringp(payload) && payload != ""
        && member_array(payload[0], ({ ' ', '\t', '\r', '\n' })) != -1)
        payload = payload[1..];

    return payload;
}

private int payload_looks_like_json(string payload)
{
    payload = trim_payload_prefix(payload);
    if (!stringp(payload) || payload == "")
        return 0;

    return member_array(payload[0], ({ '{', '[' })) != -1;
}

private int is_questcreate_operator(object me)
{
    return objectp(me)
        && (wizardp(me)
        || (function_exists("is_wizard", me) && me->is_wizard())
        || CONFIG_D->is_admin_player(me));
}

private string resolve_actor_id(object me)
{
    if (!objectp(me))
        return "";
    if (function_exists("query_account_id", me))
        return me->query_account_id() || get_object_id(me);

    return get_object_id(me);
}

private mapping parse_lpc_payload(string payload)
{
    mixed data;
    mixed err;

    payload = trim_payload_prefix(payload);
    if (!stringp(payload) || payload == "")
        return ([ "ok": 0, "error": "empty_payload" ]);

    if (payload_looks_like_json(payload)) {
        err = catch(data = json_decode(payload));
        if (err || (!mapp(data) && !arrayp(data)))
            return ([ "ok": 0, "error": "invalid_payload" ]);
        return ([ "ok": 1, "data": data ]);
    }

    err = catch(data = restore_variable(payload));
    if (err)
        return ([ "ok": 0, "error": "invalid_payload" ]);

    return ([ "ok": 1, "data": data ]);
}

private mapping parse_scaffold_payload(string rest)
{
    mapping parsed;
    mapping payload;
    string quest_id;
    string template_id;
    string provider_id;

    parsed = parse_subcommand_args(rest);
    if (!parsed["valid"] || parsed["rest"] == "")
        return ([ "ok": 0, "error": "usage" ]);
    quest_id = parsed["action"];

    parsed = parse_subcommand_args(parsed["rest"]);
    if (!parsed["valid"] || parsed["rest"] == "")
        return ([ "ok": 0, "error": "usage" ]);
    template_id = parsed["action"];

    parsed = parse_subcommand_args(parsed["rest"]);
    if (!parsed["valid"])
        return ([ "ok": 0, "error": "usage" ]);
    provider_id = parsed["action"];
    payload = ([
        "ok": 1,
        "quest_id": quest_id,
        "template_id": template_id,
        "provider_id": provider_id,
        "overrides": ([]),
    ]);

    if (!stringp(parsed["rest"]) || parsed["rest"] == "")
        return payload;

    parsed = parse_lpc_payload(parsed["rest"]);
    if (!parsed["ok"] || !mapp(parsed["data"]))
        return ([ "ok": 0, "error": "invalid_overrides" ]);

    payload["overrides"] = parsed["data"];
    return payload;
}

private mapping parse_compose_payload(string rest)
{
    mapping parsed;
    mapping payload;
    string quest_id;
    string provider_id;
    string template_csv;
    string *template_ids;

    parsed = parse_subcommand_args(rest);
    if (!parsed["valid"] || parsed["rest"] == "")
        return ([ "ok": 0, "error": "usage" ]);
    quest_id = parsed["action"];

    parsed = parse_subcommand_args(parsed["rest"]);
    if (!parsed["valid"] || parsed["rest"] == "")
        return ([ "ok": 0, "error": "usage" ]);
    provider_id = parsed["action"];

    parsed = parse_subcommand_args(parsed["rest"]);
    if (!parsed["valid"])
        return ([ "ok": 0, "error": "usage" ]);
    template_csv = parsed["action"];
    template_ids = filter_array(explode(template_csv, ","), (: stringp($1) && $1 != "" :));
    if (!sizeof(template_ids))
        return ([ "ok": 0, "error": "usage" ]);

    payload = ([
        "ok": 1,
        "quest_id": quest_id,
        "provider_id": provider_id,
        "template_ids": template_ids,
        "overrides": ([]),
    ]);

    if (!stringp(parsed["rest"]) || parsed["rest"] == "")
        return payload;

    parsed = parse_lpc_payload(parsed["rest"]);
    if (!parsed["ok"] || !mapp(parsed["data"]))
        return ([ "ok": 0, "error": "invalid_overrides" ]);

    payload["overrides"] = parsed["data"];
    return payload;
}

private mapping parse_import_payload(string rest)
{
    mapping parsed;
    mapping payload;

    parsed = parse_subcommand_args(rest);
    if (!parsed["valid"] || parsed["rest"] == "")
        return ([ "ok": 0, "error": "usage" ]);

    payload = parse_lpc_payload(parsed["rest"]);
    if (!payload["ok"] || !mapp(payload["data"]))
        return ([ "ok": 0, "error": "invalid_artifact" ]);

    return ([
        "ok": 1,
        "override_quest_id": parsed["action"] == "-" ? "" : parsed["action"],
        "artifact": payload["data"],
    ]);
}

private string format_blueprint_summary(mapping record)
{
    mapping quest_def;

    if (!mapp(record) || !mapp(record["quest"]))
        return "【任务创建】未找到任务蓝图。\n";

    quest_def = record["quest"];
    return sprintf(
        "【任务蓝图】\n状态：%s\n任务 ID：%s\nprovider：%s\ntype：%s\nscope：%s\nhidden：%d\nreleased：%d\nname：%s\ndescription：%s\nchain_id：%s\nchain_order：%d\nnext_quests：%s\nobjectives：%s\nsteps：%s\nrewards：%s\nunlock：%s\n",
        record["status"] || "draft",
        quest_def["id"] || "",
        quest_def["provider"] || "",
        quest_def["type"] || "",
        quest_def["scope"] || "",
        quest_def["hidden"] ? 1 : 0,
        quest_def["released"] ? 1 : 0,
        quest_def["name"] || "",
        quest_def["description"] || "",
        save_variable(quest_def["chain_id"] || ""),
        quest_def["chain_order"] || 0,
        save_variable(quest_def["next_quests"] || ({ })),
        save_variable(quest_def["objectives"] || ({ })),
        save_variable(quest_def["steps"] || ({ })),
        save_variable(quest_def["rewards"] || ([])),
        save_variable(quest_def["unlock"] || ([])));
}

private int do_list(object me, string rest)
{
    string status;
    string *quest_ids;
    string output;

    status = stringp(rest) && rest != "" ? rest : "all";
    if (member_array(status, ({ "draft", "published", "all" })) == -1) {
        msg_send(me, "用法：questcreate list [draft|published|all]\n");
        return 1;
    }

    quest_ids = QUEST_BLUEPRINT_D->query_blueprint_ids(status);
    output = sprintf("【任务创建】%s 蓝图列表\n", status);
    if (!arrayp(quest_ids) || !sizeof(quest_ids))
        output += "- 无\n";
    else
        output += implode(quest_ids, "\n") + "\n";
    msg_send(me, output);
    return 1;
}

private int do_templates(object me)
{
    string *template_ids;
    mapping spec;
    string template_id;
    string output;

    template_ids = QUEST_BLUEPRINT_REGISTRY_D->query_template_ids();
    output = "【任务模板】\n";
    foreach (template_id in template_ids) {
        spec = QUEST_BLUEPRINT_REGISTRY_D->query_template_schema(template_id);
        output += sprintf("- %s: %s（type=%s placeholders=%d fields=%d）",
            template_id,
            spec["description"] || "",
            spec["quest_type"] || "",
            sizeof(spec["required_placeholders"] || ({ })),
            sizeof(keys(spec["field_schema"] || ([]))));
        output += "\n";
    }
    msg_send(me, output);
    return 1;
}

private int do_schema(object me, string template_id)
{
    mapping schema;

    schema = QUEST_BLUEPRINT_REGISTRY_D->query_template_schema(template_id);
    if (!mapp(schema)) {
        msg_send(me, "【任务模板】未找到该模板。\n");
        return 1;
    }

    msg_send(me, sprintf("【任务模板 Schema】\nid=%s\nlabel=%s\ntype=%s\ndescription=%s\nrequired_placeholders=%s\nplaceholder_schema=%s\nfield_schema=%s\nexample_overrides=%s\n",
        schema["template_id"] || "",
        schema["label"] || "",
        schema["quest_type"] || "",
        schema["description"] || "",
        save_variable(schema["required_placeholders"] || ({ })),
        save_variable(schema["placeholder_schema"] || ([])),
        save_variable(schema["field_schema"] || ([])),
        save_variable(schema["example_overrides"] || ([]))));
    return 1;
}

private int do_artifact_schema(object me)
{
    msg_send(me, sprintf("【蓝图 Artifact Schema】\n%s\n",
        save_variable(QUEST_BLUEPRINT_REGISTRY_D->query_artifact_schema())));
    return 1;
}

private int do_registry(object me)
{
    msg_send(me, sprintf("【蓝图 Registry】\n%s\n",
        save_variable(QUEST_BLUEPRINT_REGISTRY_D->query_registry_snapshot())));
    return 1;
}

private int do_export(object me, string rest)
{
    mapping artifact;
    string format;
    string quest_id;

    if (!stringp(rest) || rest == "") {
        msg_send(me, "用法：questcreate export <任务ID> [json|lpc]\n");
        return 1;
    }

    if (sscanf(rest, "%s %s", quest_id, format) != 2) {
        quest_id = rest;
        format = "json";
    }

    artifact = QUEST_BLUEPRINT_D->export_blueprint_artifact(quest_id);
    if (!mapp(artifact)) {
        msg_send(me, "【任务创建】导出蓝图失败。\n");
        return 1;
    }

    if (format == "lpc")
        msg_send(me, sprintf("【蓝图导出 LPC】\n%s\n", save_variable(artifact)));
    else
        msg_send(me, sprintf("【蓝图导出 JSON】\n%s\n", json_encode(artifact)));
    return 1;
}

private int do_import(object me, string rest)
{
    mapping migrated;
    mapping parsed;
    mapping result;

    parsed = parse_import_payload(rest);
    if (!parsed["ok"]) {
        msg_send(me, "用法：questcreate import <override_quest_id|-> <LPC/JSON artifact>\n");
        return 1;
    }

    migrated = QUEST_BLUEPRINT_REGISTRY_D->migrate_artifact(parsed["artifact"]);
    if (!mapp(migrated) || !migrated["ok"]) {
        msg_send(me, sprintf("【任务创建】artifact 迁移失败：%s\n",
            mapp(migrated) ? (migrated["error"] || "unknown_error") : "unknown_error"));
        return 1;
    }

    result = QUEST_BLUEPRINT_D->import_blueprint_artifact(resolve_actor_id(me), migrated["artifact"], parsed["override_quest_id"], 0);
    if (!mapp(result) || !result["ok"]) {
        msg_send(me, sprintf("【任务创建】导入蓝图失败：%s\n", mapp(result) ? (result["error"] || "unknown_error") : "unknown_error"));
        return 1;
    }

    msg_send(me, "【任务创建】蓝图已导入为草稿。\n");
    msg_send(me, format_blueprint_summary(result["blueprint"]));
    return 1;
}

private int do_scaffold(object me, string rest)
{
    mapping parsed;
    mapping validation;
    mapping payload;
    mapping result;
    string actor_id;
    string quest_id;
    string template_id;
    string provider_id;

    parsed = parse_scaffold_payload(rest);
    if (!parsed["ok"]) {
        if (parsed["error"] == "invalid_overrides")
            msg_send(me, "用法：questcreate scaffold <任务ID> <template_id> <provider_id> [LPC/JSON mapping]\n");
        else
            msg_send(me, "用法：questcreate scaffold <任务ID> <template_id> <provider_id> [LPC/JSON mapping]\n");
        return 1;
    }

    quest_id = parsed["quest_id"];
    template_id = parsed["template_id"];
    provider_id = parsed["provider_id"];
    validation = QUEST_BLUEPRINT_REGISTRY_D->validate_template_overrides(template_id, parsed["overrides"]);
    if (!mapp(validation) || !validation["ok"]) {
        msg_send(me, sprintf("【任务创建】模板参数非法：%s\n",
            mapp(validation) ? implode(validation["errors"] || ({ "unknown_error" }), ", ") : "unknown_error"));
        return 1;
    }

    payload = QUEST_BLUEPRINT_REGISTRY_D->build_template_payload_with_overrides(
        template_id, quest_id, provider_id, parsed["overrides"]);
    if (!mapp(payload)) {
        msg_send(me, "【任务创建】模板不存在，或模板参数非法。\n");
        return 1;
    }

    actor_id = resolve_actor_id(me);
    result = QUEST_BLUEPRINT_D->create_blueprint(actor_id, quest_id, provider_id, payload["quest_type"]);
    if (!mapp(result) || !result["ok"]) {
        msg_send(me, "【任务创建】初始化模板蓝图失败。\n");
        return 1;
    }

    QUEST_BLUEPRINT_D->set_blueprint_field(quest_id, "name", payload["name"]);
    QUEST_BLUEPRINT_D->set_blueprint_field(quest_id, "description", payload["description"]);
    QUEST_BLUEPRINT_D->set_blueprint_field(quest_id, "scope", payload["scope"]);
    if (member_array("hidden", keys(payload)) != -1)
        QUEST_BLUEPRINT_D->set_blueprint_field(quest_id, "hidden", payload["hidden"]);
    if (stringp(payload["chain_id"]))
        QUEST_BLUEPRINT_D->set_blueprint_field(quest_id, "chain_id", payload["chain_id"]);
    if (intp(payload["chain_order"]))
        QUEST_BLUEPRINT_D->set_blueprint_field(quest_id, "chain_order", payload["chain_order"]);
    if (arrayp(payload["next_quests"]))
        QUEST_BLUEPRINT_D->set_blueprint_field(quest_id, "next_quests", payload["next_quests"]);
    if (stringp(payload["region"]) && payload["region"] != "")
        QUEST_BLUEPRINT_D->set_blueprint_field(quest_id, "region", payload["region"]);
    if (stringp(payload["world_key"]) && payload["world_key"] != "")
        QUEST_BLUEPRINT_D->set_blueprint_field(quest_id, "world_key", payload["world_key"]);
    QUEST_BLUEPRINT_D->replace_blueprint_objectives(quest_id, payload["objectives"]);
    QUEST_BLUEPRINT_D->replace_blueprint_steps(quest_id, payload["steps"]);
    QUEST_BLUEPRINT_D->set_blueprint_rewards(quest_id, payload["rewards"]);
    QUEST_BLUEPRINT_D->set_blueprint_unlock(quest_id, payload["unlock"]);
    msg_send(me, sprintf("【任务创建】已按模板 %s 初始化蓝图。\n", template_id));
    if (arrayp(payload["required_placeholders"]) && sizeof(payload["required_placeholders"]))
        msg_send(me, sprintf("【任务创建】发布前必须替换这些占位：%s\n", implode(payload["required_placeholders"], ", ")));
    msg_send(me, format_blueprint_summary(QUEST_BLUEPRINT_D->query_blueprint(quest_id)));
    return 1;
}

private int do_compose_schema(object me, string rest)
{
    mapping parsed;
    mapping schema;
    string *template_ids;

    parsed = parse_subcommand_args(rest);
    if (!parsed["valid"]) {
        msg_send(me, "用法：questcreate compose-schema <template_id[,template_id...]>\n");
        return 1;
    }

    template_ids = filter_array(explode(parsed["action"], ","), (: stringp($1) && $1 != "" :));
    schema = QUEST_BLUEPRINT_REGISTRY_D->query_composed_template_schema(template_ids);
    if (!mapp(schema)) {
        msg_send(me, "【任务模板】无法生成组合 schema。\n");
        return 1;
    }

    msg_send(me, sprintf("【任务组合 Schema】\ntemplate_ids=%s\nquest_type=%s\nrequired_placeholders=%s\nshared_field_schema=%s\ncomponent_schemas=%s\nexample_overrides=%s\n",
        save_variable(schema["template_ids"] || ({ })),
        schema["quest_type"] || "",
        save_variable(schema["required_placeholders"] || ({ })),
        save_variable(schema["shared_field_schema"] || ([])),
        save_variable(schema["component_schemas"] || ([])),
        save_variable(schema["example_overrides"] || ([]))));
    return 1;
}

private int do_compose(object me, string rest)
{
    mapping parsed;
    mapping validation;
    mapping payload;
    mapping result;
    string actor_id;
    string quest_id;
    string provider_id;
    string *template_ids;

    parsed = parse_compose_payload(rest);
    if (!parsed["ok"]) {
        msg_send(me, "用法：questcreate compose <任务ID> <provider_id> <template_id[,template_id...]> [LPC/JSON mapping]\n");
        return 1;
    }

    quest_id = parsed["quest_id"];
    provider_id = parsed["provider_id"];
    template_ids = parsed["template_ids"];
    validation = QUEST_BLUEPRINT_REGISTRY_D->validate_composed_template_overrides(template_ids, parsed["overrides"]);
    if (!mapp(validation) || !validation["ok"]) {
        msg_send(me, sprintf("【任务创建】组合模板参数非法：%s\n",
            mapp(validation) ? implode(validation["errors"] || ({ "unknown_error" }), ", ") : "unknown_error"));
        return 1;
    }

    payload = QUEST_BLUEPRINT_REGISTRY_D->build_composed_template_payload(template_ids, quest_id, provider_id, parsed["overrides"]);
    if (!mapp(payload)) {
        msg_send(me, "【任务创建】组合模板构建失败。\n");
        return 1;
    }

    actor_id = resolve_actor_id(me);
    result = QUEST_BLUEPRINT_D->create_blueprint(actor_id, quest_id, provider_id, payload["quest_type"]);
    if (!mapp(result) || !result["ok"]) {
        msg_send(me, "【任务创建】初始化组合蓝图失败。\n");
        return 1;
    }

    QUEST_BLUEPRINT_D->set_blueprint_field(quest_id, "name", payload["name"]);
    QUEST_BLUEPRINT_D->set_blueprint_field(quest_id, "description", payload["description"]);
    QUEST_BLUEPRINT_D->set_blueprint_field(quest_id, "scope", payload["scope"]);
    if (member_array("hidden", keys(payload)) != -1)
        QUEST_BLUEPRINT_D->set_blueprint_field(quest_id, "hidden", payload["hidden"]);
    if (stringp(payload["chain_id"]))
        QUEST_BLUEPRINT_D->set_blueprint_field(quest_id, "chain_id", payload["chain_id"]);
    if (intp(payload["chain_order"]))
        QUEST_BLUEPRINT_D->set_blueprint_field(quest_id, "chain_order", payload["chain_order"]);
    if (arrayp(payload["next_quests"]))
        QUEST_BLUEPRINT_D->set_blueprint_field(quest_id, "next_quests", payload["next_quests"]);
    if (stringp(payload["region"]) && payload["region"] != "")
        QUEST_BLUEPRINT_D->set_blueprint_field(quest_id, "region", payload["region"]);
    if (stringp(payload["world_key"]) && payload["world_key"] != "")
        QUEST_BLUEPRINT_D->set_blueprint_field(quest_id, "world_key", payload["world_key"]);
    QUEST_BLUEPRINT_D->replace_blueprint_objectives(quest_id, payload["objectives"]);
    QUEST_BLUEPRINT_D->replace_blueprint_steps(quest_id, payload["steps"]);
    QUEST_BLUEPRINT_D->set_blueprint_rewards(quest_id, payload["rewards"]);
    QUEST_BLUEPRINT_D->set_blueprint_unlock(quest_id, payload["unlock"]);
    msg_send(me, sprintf("【任务创建】已按模板组合 %s 初始化蓝图。\n", implode(template_ids, ",")));
    if (arrayp(payload["required_placeholders"]) && sizeof(payload["required_placeholders"]))
        msg_send(me, sprintf("【任务创建】发布前必须替换这些占位：%s\n", implode(payload["required_placeholders"], ", ")));
    msg_send(me, format_blueprint_summary(QUEST_BLUEPRINT_D->query_blueprint(quest_id)));
    return 1;
}

private int do_show(object me, string quest_id)
{
    msg_send(me, format_blueprint_summary(QUEST_BLUEPRINT_D->query_blueprint(quest_id)));
    return 1;
}

private int do_init(object me, string rest)
{
    mapping parsed;
    mapping result;

    parsed = parse_command_args(rest, "%s %s %s");
    if (!parsed["valid"]) {
        msg_send(me, "用法：questcreate init <任务ID> <provider_id> <type>\n");
        return 1;
    }

    result = QUEST_BLUEPRINT_D->create_blueprint(resolve_actor_id(me),
        parsed["args"][0], parsed["args"][1], parsed["args"][2]);
    if (!mapp(result) || !result["ok"]) {
        msg_send(me, "【任务创建】初始化蓝图失败。\n");
        return 1;
    }

    msg_send(me, "【任务创建】已创建任务蓝图。\n");
    return 1;
}

private int do_set(object me, string rest)
{
    mapping parsed;
    string quest_id;
    string field;
    string value;
    mixed coerced;

    parsed = parse_subcommand_args(rest);
    if (!parsed["valid"] || parsed["rest"] == "") {
        msg_send(me, "用法：questcreate set <任务ID> <field> <value>\n");
        return 1;
    }
    quest_id = parsed["action"];
    parsed = parse_subcommand_args(parsed["rest"]);
    if (!parsed["valid"] || parsed["rest"] == "") {
        msg_send(me, "用法：questcreate set <任务ID> <field> <value>\n");
        return 1;
    }

    field = parsed["action"];
    value = parsed["rest"];
    if (member_array(field, ({ "name", "provider", "type", "scope", "hidden", "description", "chain_id", "chain_order", "region", "world_key" })) == -1) {
        msg_send(me, "【任务创建】该字段不允许通过 set 修改。\n");
        return 1;
    }
    if (member_array(field, ({ "hidden", "chain_order" })) != -1)
        sscanf(value, "%d", coerced);
    else
        coerced = value;

    if (!QUEST_BLUEPRINT_D->set_blueprint_field(quest_id, field, coerced)) {
        msg_send(me, "【任务创建】更新字段失败。\n");
        return 1;
    }
    msg_send(me, "【任务创建】字段已更新。\n");
    return 1;
}

private int do_objectives(object me, string rest)
{
    mapping parsed;
    mapping payload;

    parsed = parse_subcommand_args(rest);
    if (!parsed["valid"] || parsed["rest"] == "") {
        msg_send(me, "用法：questcreate objectives <任务ID> <LPC数组>\n");
        return 1;
    }
    payload = parse_lpc_payload(parsed["rest"]);
    if (!payload["ok"] || !arrayp(payload["data"]) || !QUEST_BLUEPRINT_D->replace_blueprint_objectives(parsed["action"], payload["data"])) {
        msg_send(me, "【任务创建】objectives 载荷非法。\n");
        return 1;
    }
    msg_send(me, "【任务创建】objectives 已更新。\n");
    return 1;
}

private int do_steps(object me, string rest)
{
    mapping parsed;
    mapping payload;

    parsed = parse_subcommand_args(rest);
    if (!parsed["valid"] || parsed["rest"] == "") {
        msg_send(me, "用法：questcreate steps <任务ID> <LPC数组>\n");
        return 1;
    }
    payload = parse_lpc_payload(parsed["rest"]);
    if (!payload["ok"] || !arrayp(payload["data"]) || !QUEST_BLUEPRINT_D->replace_blueprint_steps(parsed["action"], payload["data"])) {
        msg_send(me, "【任务创建】steps 载荷非法。\n");
        return 1;
    }
    msg_send(me, "【任务创建】steps 已更新。\n");
    return 1;
}

private int do_rewards(object me, string rest)
{
    mapping parsed;
    mapping payload;

    parsed = parse_subcommand_args(rest);
    if (!parsed["valid"] || parsed["rest"] == "") {
        msg_send(me, "用法：questcreate rewards <任务ID> <LPC mapping>\n");
        return 1;
    }
    payload = parse_lpc_payload(parsed["rest"]);
    if (!payload["ok"] || !mapp(payload["data"]) || !QUEST_BLUEPRINT_D->set_blueprint_rewards(parsed["action"], payload["data"])) {
        msg_send(me, "【任务创建】rewards 载荷非法。\n");
        return 1;
    }
    msg_send(me, "【任务创建】rewards 已更新。\n");
    return 1;
}

private int do_unlock(object me, string rest)
{
    mapping parsed;
    mapping payload;

    parsed = parse_subcommand_args(rest);
    if (!parsed["valid"] || parsed["rest"] == "") {
        msg_send(me, "用法：questcreate unlock <任务ID> <LPC mapping>\n");
        return 1;
    }
    payload = parse_lpc_payload(parsed["rest"]);
    if (!payload["ok"] || !mapp(payload["data"]) || !QUEST_BLUEPRINT_D->set_blueprint_unlock(parsed["action"], payload["data"])) {
        msg_send(me, "【任务创建】unlock 载荷非法。\n");
        return 1;
    }
    msg_send(me, "【任务创建】unlock 已更新。\n");
    return 1;
}

private int do_validate(object me, string quest_id)
{
    mapping report;

    report = QUEST_BLUEPRINT_D->validate_blueprint(quest_id, QUEST_D->get_all_quests_info());
    msg_send(me, call_other("/daemons/services/gameplay/quest/quest_validate", "render_validation_report", report));
    return 1;
}

private int do_publish(object me, string quest_id)
{
    if (!QUEST_BLUEPRINT_D->publish_blueprint(quest_id, QUEST_D->get_all_quests_info())) {
        msg_send(me, "【任务创建】发布失败。请先通过校验并确保任务 ID 不冲突。\n");
        return 1;
    }

    QUEST_D->reload_runtime_blueprints();
    msg_send(me, "【任务创建】任务蓝图已发布。\n");
    return 1;
}

private int do_unpublish(object me, string quest_id)
{
    if (!QUEST_BLUEPRINT_D->unpublish_blueprint(quest_id)) {
        msg_send(me, "【任务创建】下架失败。\n");
        return 1;
    }

    QUEST_D->reload_runtime_blueprints();
    msg_send(me, "【任务创建】任务蓝图已下架。\n");
    return 1;
}

private int do_delete(object me, string quest_id)
{
    if (!QUEST_BLUEPRINT_D->delete_blueprint(quest_id)) {
        msg_send(me, "【任务创建】删除失败。\n");
        return 1;
    }

    QUEST_D->reload_runtime_blueprints();
    msg_send(me, "【任务创建】任务蓝图已删除。\n");
    return 1;
}

private int dispatch_action(object me, string action, string rest)
{
    switch (action) {
        case "list":
            return do_list(me, rest);
        case "templates":
            return do_templates(me);
        case "schema":
            return do_schema(me, rest);
        case "artifact-schema":
            return do_artifact_schema(me);
        case "registry":
            return do_registry(me);
        case "export":
            return do_export(me, rest);
        case "import":
            return do_import(me, rest);
        case "scaffold":
            return do_scaffold(me, rest);
        case "compose-schema":
            return do_compose_schema(me, rest);
        case "compose":
            return do_compose(me, rest);
        case "show":
            return do_show(me, rest);
        case "init":
            return do_init(me, rest);
        case "set":
            return do_set(me, rest);
        case "objectives":
            return do_objectives(me, rest);
        case "steps":
            return do_steps(me, rest);
        case "rewards":
            return do_rewards(me, rest);
        case "unlock":
            return do_unlock(me, rest);
        case "validate":
            return do_validate(me, rest);
        case "publish":
            return do_publish(me, rest);
        case "unpublish":
            return do_unpublish(me, rest);
        case "delete":
            return do_delete(me, rest);
    }

    return help(me);
}

int main(object me, string arg)
{
    mapping parsed;

    if (!objectp(me))
        return 0;

    if (!is_questcreate_operator(me)) {
        msg_send(me, HIY "【任务创建】UGC任务创建框架已预留，当前版本暂未开放。" NOR "\n");
        return 1;
    }

    if (!stringp(arg) || arg == "")
        return help(me);

    parsed = parse_subcommand_args(arg);
    if (!parsed["valid"])
        return help(me);
    return dispatch_action(me, parsed["action"], parsed["rest"]);
}

int help(object me)
{
    msg_send(me, @HELP
命令：questcreate

questcreate list [draft|published|all]
questcreate templates
questcreate schema <template_id>
questcreate artifact-schema
questcreate registry
questcreate export <任务ID> [json|lpc]
questcreate import <override_quest_id|-> <LPC/JSON artifact>
questcreate scaffold <任务ID> <template_id> <provider_id> [LPC/JSON mapping]
questcreate compose-schema <template_id[,template_id...]>
questcreate compose <任务ID> <provider_id> <template_id[,template_id...]> [LPC/JSON mapping]
questcreate show <任务ID>
questcreate init <任务ID> <provider_id> <type>
questcreate set <任务ID> <field> <value>
questcreate objectives <任务ID> <LPC数组>
questcreate steps <任务ID> <LPC数组>
questcreate rewards <任务ID> <LPC mapping>
questcreate unlock <任务ID> <LPC mapping>
questcreate validate <任务ID>
questcreate publish <任务ID>
questcreate unpublish <任务ID>
questcreate delete <任务ID>
HELP
    );
    return 1;
}
