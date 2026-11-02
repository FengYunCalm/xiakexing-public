/**
 * @file    quest_blueprint_d.c
 * @brief   任务蓝图草稿/发布真源



 *
 * @par Description:
 * 管理员通过 `questcreate` 维护的运行时任务蓝图统一收口在这里。
 * 本守护只负责草稿持久化、校验与发布状态，不承担任务运行时状态。
 */

#pragma strict_types
#pragma save_types

#include <system/globals.h>
#include <game/quest.h>

inherit "/lib/framework/daemon";

#define QUEST_VALIDATE_HELPER "/daemons/services/gameplay/quest/quest_validate"
#define QUEST_BLUEPRINT_ARTIFACT_SCHEMA_VERSION 1

mapping _blueprints;
/* L-150：损坏存档进入待恢复状态；保存被拒绝，直到人工确认重建。 */
nosave int _blueprint_restore_failed;
nosave mapping _last_blueprint_save_phase;

private string get_save_file();
private void ensure_runtime_state();
private void restore_blueprints();
private int save_blueprints();
mapping query_blueprint_save_phase();
private int is_safe_quest_id(string quest_id);
private mapping clone_quest_definition(mapping quest_def);
private int can_publish_quest_def(mapping quest_def);
mapping query_blueprint(string quest_id);

mapping query_blueprint_artifact_schema()
{
    return ([
        "artifact_type": "quest_blueprint",
        "schema_version": QUEST_BLUEPRINT_ARTIFACT_SCHEMA_VERSION,
        "required_keys": ({ "artifact_type", "schema_version", "source", "quest" }),
        "source_keys": ({ "quest_id", "status", "created_by", "updated_at", "published_at" }),
        "import_policy": ([
            "force_draft": 1,
            "reset_released": 1,
            "allow_override_quest_id": 1,
        ]),
    ]);
}

mapping query_blueprint_runtime_snapshot()
{
    string *quest_ids;
    string quest_id;
    mapping record;
    int draft_count;
    int published_count;

    ensure_runtime_state();
    quest_ids = sort_array(keys(_blueprints), 1);
    draft_count = 0;
    published_count = 0;

    foreach (quest_id in quest_ids) {
        record = _blueprints[quest_id];
        if (!mapp(record))
            continue;
        if (record["status"] == "published")
            published_count += 1;
        else
            draft_count += 1;
    }

    return ([
        "save_file": get_save_file() + ".o",
        "total_count": sizeof(quest_ids),
        "draft_count": draft_count,
        "published_count": published_count,
        "quest_ids": quest_ids,
    ]);
}

int reload_blueprint_runtime()
{
    restore_blueprints();
    return 1;
}

mapping export_blueprint_artifact(string quest_id)
{
    mapping record;

    ensure_runtime_state();
    record = _blueprints[quest_id];
    if (!is_safe_quest_id(quest_id) || !mapp(record) || !mapp(record["quest"]))
        return 0;

    return ([
        "artifact_type": "quest_blueprint",
        "schema_version": QUEST_BLUEPRINT_ARTIFACT_SCHEMA_VERSION,
        "source": ([
            "quest_id": quest_id,
            "status": record["status"] || "draft",
            "created_by": record["created_by"] || "",
            "updated_at": record["updated_at"] || 0,
            "published_at": record["published_at"] || 0,
        ]),
        "quest": clone_quest_definition(record["quest"]),
    ]);
}

/* L-151：未知任务字段在导入/写入/发布入口即拒绝（运行期会静默丢弃）。 */
private int quest_definition_fields_are_known(mapping quest_def)
{
    string *allowed;
    string field;

    if (!mapp(quest_def))
        return 0;
    allowed = call_other(QUEST_VALIDATE_HELPER, "query_quest_definition_fields");
    if (!arrayp(allowed))
        return 1;
    foreach (field in keys(quest_def)) {
        if (member_array(field, allowed) == -1) {
            log_file("system/quest", sprintf(
                "[%s] blueprint quest field rejected (not whitelisted): %s\n",
                ctime(time()), field));
            return 0;
        }
    }
    return 1;
}

private int blueprint_field_is_valid(string field, mixed value)
{
    string *allowed;
    mapping types;
    string expected;

    allowed = call_other(QUEST_VALIDATE_HELPER, "query_quest_definition_fields");
    types = call_other(QUEST_VALIDATE_HELPER, "query_quest_definition_field_types");
    if (!arrayp(allowed) || member_array(field, allowed) == -1) {
        log_file("system/quest", sprintf(
            "[%s] blueprint field rejected (not whitelisted): %s\n",
            ctime(time()), field));
        return 0;
    }
    expected = mapp(types) ? types[field] : "";
    if (expected == "string" && !stringp(value))
        return 0;
    if (expected == "int" && !intp(value))
        return 0;
    if (expected == "array" && !arrayp(value))
        return 0;
    if (expected == "mapping" && !mapp(value))
        return 0;
    return 1;
}

mapping validate_blueprint_artifact(mapping artifact)
{
    mapping policy;
    mapping quest_def;
    mapping source;
    mapping schema;
    string artifact_type;
    string quest_id;

    schema = query_blueprint_artifact_schema();
    if (!mapp(artifact))
        return ([ "ok": 0, "error": "invalid_artifact" ]);

    artifact_type = artifact["artifact_type"] || "";
    if (artifact_type != schema["artifact_type"])
        return ([ "ok": 0, "error": "invalid_artifact_type" ]);
    if (artifact["schema_version"] != schema["schema_version"])
        return ([ "ok": 0, "error": "unsupported_schema_version" ]);

    source = artifact["source"];
    quest_def = artifact["quest"];
    if (!mapp(source) || !mapp(quest_def))
        return ([ "ok": 0, "error": "missing_sections" ]);

    quest_id = source["quest_id"] || quest_def["id"] || "";
    if (!is_safe_quest_id(quest_id))
        return ([ "ok": 0, "error": "invalid_quest_id" ]);
    if (!stringp(quest_def["provider"]) || quest_def["provider"] == "")
        return ([ "ok": 0, "error": "invalid_provider_id" ]);
    if (!stringp(quest_def["type"]) || quest_def["type"] == "")
        return ([ "ok": 0, "error": "invalid_quest_type" ]);
    if (!quest_definition_fields_are_known(quest_def))
        return ([ "ok": 0, "error": "unknown_quest_field" ]);

    policy = schema["import_policy"] || ([]);
    return ([
        "ok": 1,
        "quest_id": quest_id,
        "artifact": restore_variable(save_variable(artifact)),
        "schema": schema,
        "policy": policy,
    ]);
}

mapping import_blueprint_artifact(string actor_id, mapping artifact, string override_quest_id, int overwrite)
{
    mapping previous_record;
    mapping record;
    mapping quest_def;
    mapping validation;
    string quest_id;

    ensure_runtime_state();
    validation = validate_blueprint_artifact(artifact);
    if (!mapp(validation) || !validation["ok"])
        return validation;

    quest_id = stringp(override_quest_id) && override_quest_id != ""
        ? override_quest_id
        : validation["quest_id"];
    if (!is_safe_quest_id(quest_id))
        return ([ "ok": 0, "error": "invalid_override_quest_id" ]);
    if (mapp(_blueprints[quest_id]) && !overwrite)
        return ([ "ok": 0, "error": "quest_id_exists" ]);

    quest_def = clone_quest_definition(validation["artifact"]["quest"]);
    if (!mapp(quest_def))
        return ([ "ok": 0, "error": "invalid_quest_payload" ]);

    quest_def["id"] = quest_id;
    quest_def["released"] = 0;
    record = ([
        "quest": quest_def,
        "status": "draft",
        "created_by": stringp(actor_id) ? actor_id : "",
        "updated_at": time(),
        "published_at": 0,
        "imported_from": validation["artifact"]["source"] || ([]),
    ]);

    previous_record = _blueprints[quest_id];
    _blueprints[quest_id] = record;
    {
        int save_code;

        save_code = save_blueprints();
        if (save_code == 0) {
            /* L-150：落盘失败必须回滚内存，不得报成功。 */
            if (mapp(previous_record))
                _blueprints[quest_id] = previous_record;
            else
                map_delete(_blueprints, quest_id);
            return ([ "ok": 0, "outcome": "not_written",
                "error": "save_failed" ]);
        }
        if (save_code < 0)
            return ([ "ok": 0, "persisted": -1, "outcome": "unknown",
                "error": "blueprint save state is unknown",
                "save_phase": query_blueprint_save_phase() ]);
        if (save_code == 2)
            return ([ "ok": 0, "persisted": 1,
                "outcome": "persisted_receipt_pending",
                "retry_token": _last_blueprint_save_phase["retry_token"] || "",
                "blueprint": query_blueprint(quest_id) ]);
    }
    return ([ "ok": 1, "persisted": 1, "outcome": "committed",
        "blueprint": query_blueprint(quest_id) ]);
}

private string get_save_file()
{
    return DATA_DIR + "runtime/quest_blueprint_d";
}

private void ensure_runtime_state()
{
    if (!mapp(_blueprints))
        _blueprints = ([]);
}

private void restore_blueprints()
{
    string file;

    file = get_save_file();
    if (file_size(file + ".o") <= 0) {
        _blueprints = ([]);
        _blueprint_restore_failed = 0;
        return;
    }

    if (catch(restore_object(file))) {
        /* L-150：损坏存档不得静默清空，也不得被空集覆盖；冻结保存等待
         * 人工确认重建（与任务账本 L-140 同一失败分态原则）。 */
        log_file("system/quest", sprintf(
            "[%s] restore quest blueprints failed; blueprint ledger frozen, save refused: %s\n",
            ctime(time()), file));
        ensure_runtime_state();
        _blueprint_restore_failed = 1;
        return;
    }

    ensure_runtime_state();
    _blueprint_restore_failed = 0;
}

private int persist_blueprints()
{
    mapping detail;
    string file;
    string runtime_dir;

    /* L-150：待恢复状态下拒绝保存，避免空蓝图集覆盖唯一存档。 */
    if (_blueprint_restore_failed) {
        _last_blueprint_save_phase = ([ "outcome": "not_written",
            "persisted": 0, "save_result": 0,
            "error": "blueprint restore is blocked" ]);
        return 0;
    }
    ensure_runtime_state();
    file = get_save_file();
    runtime_dir = DATA_DIR + "runtime";
    if (file_size(runtime_dir) == -1)
        mkdir(runtime_dir);

    detail = owner_save_object_phases(file,
        "owner/global/service/quest/blueprint", "quest_blueprint_state", ([
        "blueprint_count": sizeof(_blueprints),
        "state_digest": hash("sha256", save_variable(_blueprints)),
    ]), "quest_blueprint/save/" + hash("sha256", save_variable(_blueprints)));
    _last_blueprint_save_phase = mapp(detail) ? detail
        : ([ "outcome": "unknown", "persisted": -1,
            "save_result": 0, "error": "owner save result unavailable" ]);
    if (!mapp(detail))
        return -1;
    if (detail["persisted"] != 1)
        return detail["outcome"] == "not_written" ? 0 : -1;
    return detail["ok"] == 1 ? 1 : 2;
}

private int save_blueprints()
{
    return persist_blueprints();
}

mapping query_blueprint_save_phase()
{
    return mapp(_last_blueprint_save_phase) ? copy(_last_blueprint_save_phase)
        : ([ "outcome": "not_attempted", "persisted": 0,
            "save_result": 0 ]);
}

private int is_safe_quest_id(string quest_id)
{
    if (!stringp(quest_id) || quest_id == "")
        return 0;
    if (strlen(quest_id) > 64)
        return 0;

    return regexp(quest_id, "^[a-zA-Z0-9_]+$");
}

private mapping clone_quest_definition(mapping quest_def)
{
    if (!mapp(quest_def))
        return 0;

    return restore_variable(save_variable(quest_def));
}

private int can_publish_quest_def(mapping quest_def)
{
    if (!mapp(quest_def))
        return 0;
    if (!stringp(quest_def["provider"]) || quest_def["provider"] == "")
        return 0;
    if (!stringp(quest_def["type"]) || quest_def["type"] == "")
        return 0;
    if (!stringp(quest_def["name"]) || quest_def["name"] == "")
        return 0;
    if (!stringp(quest_def["description"]) || quest_def["description"] == "")
        return 0;
    if (!arrayp(quest_def["steps"]) || sizeof(quest_def["steps"]) == 0)
        return 0;

    return 1;
}

mapping query_blueprint(string quest_id)
{
    ensure_runtime_state();
    if (!is_safe_quest_id(quest_id) || !mapp(_blueprints[quest_id]))
        return 0;

    return restore_variable(save_variable(_blueprints[quest_id]));
}

string *query_blueprint_ids(string status)
{
    string *quest_ids;
    string quest_id;
    mapping record;
    string *result;

    ensure_runtime_state();
    quest_ids = sort_array(keys(_blueprints), 1);
    if (!stringp(status) || status == "" || status == "all")
        return quest_ids;

    result = ({ });
    foreach (quest_id in quest_ids) {
        record = _blueprints[quest_id];
        if (!mapp(record))
            continue;
        if (record["status"] == status)
            result += ({ quest_id });
    }
    return result;
}

mapping create_blueprint(string actor_id, string quest_id, string provider_id, string quest_type)
{
    mapping record;

    ensure_runtime_state();
    if (!is_safe_quest_id(quest_id))
        return ([ "ok": 0, "error": "invalid_quest_id" ]);
    if (!stringp(provider_id) || provider_id == "")
        return ([ "ok": 0, "error": "invalid_provider_id" ]);
    if (!stringp(quest_type) || quest_type == "")
        return ([ "ok": 0, "error": "invalid_quest_type" ]);
    if (mapp(_blueprints[quest_id]))
        return ([ "ok": 0, "error": "quest_id_exists" ]);

    record = ([
        "quest": ([
            "id": quest_id,
            "name": quest_id,
            "provider": provider_id,
            "type": quest_type,
            "scope": QUEST_SCOPE_PERSONAL,
            "hidden": 0,
            "released": 0,
            "description": "",
            "objectives": ({ }),
            "steps": ({ }),
            "rewards": ([]),
            "unlock": ([]),
        ]),
        "status": "draft",
        "created_by": stringp(actor_id) ? actor_id : "",
        "updated_at": time(),
        "published_at": 0,
    ]);
    _blueprints[quest_id] = record;
    {
        int save_code;

        save_code = save_blueprints();
        if (save_code == 0) {
            /* L-150：落盘失败必须回滚内存，不得报成功。 */
            map_delete(_blueprints, quest_id);
            return ([ "ok": 0, "outcome": "not_written",
                "error": "save_failed" ]);
        }
        if (save_code < 0)
            return ([ "ok": 0, "persisted": -1, "outcome": "unknown",
                "error": "blueprint save state is unknown",
                "save_phase": query_blueprint_save_phase() ]);
        if (save_code == 2)
            return ([ "ok": 0, "persisted": 1,
                "outcome": "persisted_receipt_pending",
                "retry_token": _last_blueprint_save_phase["retry_token"] || "",
                "blueprint": query_blueprint(quest_id) ]);
    }
    return ([ "ok": 1, "persisted": 1, "outcome": "committed",
        "blueprint": query_blueprint(quest_id) ]);
}

int set_blueprint_field(string quest_id, string field, mixed value)
{
    mapping record;
    mixed previous_value;
    int previous_updated_at;
    int save_code;

    ensure_runtime_state();
    if (!is_safe_quest_id(quest_id) || !stringp(field) || field == "")
        return 0;

    record = _blueprints[quest_id];
    if (!mapp(record) || !mapp(record["quest"]))
        return 0;
    /* L-151：字段名与类型白名单；未知字段写入不得进入发布。 */
    if (!blueprint_field_is_valid(field, value))
        return 0;

    previous_value = record["quest"][field];
    previous_updated_at = record["updated_at"];
    record["quest"][field] = value;
    record["updated_at"] = time();
    save_code = save_blueprints();
    if (save_code == 0) {
        /* L-150：落盘失败恢复字段原值，不得报成功。 */
        if (undefinedp(previous_value))
            map_delete(record["quest"], field);
        else
            record["quest"][field] = previous_value;
        record["updated_at"] = previous_updated_at;
        return 0;
    }
    /* The int façade is boolean: a terminal receipt is not a successful
     * command result. Keep the candidate in memory for owner retry. */
    return save_code == 1 ? 1 : 0;
}

int replace_blueprint_objectives(string quest_id, mixed *objectives)
{
    if (!arrayp(objectives))
        return 0;

    return set_blueprint_field(quest_id, "objectives", restore_variable(save_variable(objectives)));
}

int replace_blueprint_steps(string quest_id, mixed *steps)
{
    if (!arrayp(steps))
        return 0;

    return set_blueprint_field(quest_id, "steps", restore_variable(save_variable(steps)));
}

int set_blueprint_rewards(string quest_id, mapping rewards)
{
    if (!mapp(rewards))
        return 0;

    return set_blueprint_field(quest_id, "rewards", clone_quest_definition(rewards));
}

int set_blueprint_unlock(string quest_id, mapping unlock)
{
    if (!mapp(unlock))
        return 0;

    return set_blueprint_field(quest_id, "unlock", clone_quest_definition(unlock));
}

mapping validate_blueprint(string quest_id, mapping builtin_defs)
{
    mapping record;
    mapping defs;
    mapping report;

    ensure_runtime_state();
    record = _blueprints[quest_id];
    if (!mapp(record) || !mapp(record["quest"]))
        return ([ "ok": 0, "errors": ({ "missing_blueprint" }), "warnings": ({ }), "quests": ([]) ]);
    /* L-151：未知字段（含历史数据/直接映射修改）不得通过发布。 */
    if (!quest_definition_fields_are_known(record["quest"]))
        return ([ "ok": 0,
            "errors": ({ "[" + quest_id + "] 未知任务字段，发布被拒绝。" }),
            "warnings": ({ }), "quests": ([]) ]);

    defs = mapp(builtin_defs) ? restore_variable(save_variable(builtin_defs)) : ([]);
    if (mapp(defs[quest_id]) && record["status"] != "published") {
        return ([
            "ok": 0,
            "errors": ({ "[" + quest_id + "] quest_id 与现有任务冲突。" }),
            "warnings": ({ }),
            "quests": ([ quest_id: ([ "errors": ({ "quest_id 与现有任务冲突。" }), "warnings": ({ }) ]) ]),
        ]);
    }

    map_delete(defs, quest_id);
    defs[quest_id] = clone_quest_definition(record["quest"]);
    report = call_other(QUEST_VALIDATE_HELPER, "validate_all_quest_definitions", defs);
    return mapp(report) ? report : ([ "ok": 0, "errors": ({ "validation_failed" }), "warnings": ({ }), "quests": ([]) ]);
}

int publish_blueprint(string quest_id, mapping builtin_defs)
{
    mapping record;
    mapping report;
    mapping quest_def;
    mixed previous_released;
    string previous_status;
    int previous_published_at;
    int previous_updated_at;
    int save_code;

    ensure_runtime_state();
    record = _blueprints[quest_id];
    if (!mapp(record) || !mapp(record["quest"]))
        return 0;
    if (mapp(builtin_defs[quest_id]) && record["status"] != "published")
        return 0;

    quest_def = record["quest"];
    if (!can_publish_quest_def(quest_def))
        return 0;

    report = validate_blueprint(quest_id, builtin_defs);
    if (!mapp(report) || sizeof(report["errors"] || ({ })) > 0)
        return 0;

    previous_released = quest_def["released"];
    previous_status = record["status"];
    previous_updated_at = record["updated_at"];
    previous_published_at = record["published_at"];
    quest_def["released"] = 1;
    record["status"] = "published";
    record["updated_at"] = time();
    record["published_at"] = time();
    save_code = save_blueprints();
    if (save_code == 0) {
        /* L-150：落盘失败恢复发布前状态，不得报成功。 */
        quest_def["released"] = previous_released;
        record["status"] = previous_status;
        record["updated_at"] = previous_updated_at;
        record["published_at"] = previous_published_at;
        return 0;
    }
    /* persisted_receipt_pending/unknown keep the candidate; neither is a
     * successful publish result. */
    return save_code == 1 ? 1 : 0;
}

int unpublish_blueprint(string quest_id)
{
    mapping record;
    mapping quest_def;
    mixed previous_released;
    string previous_status;
    int previous_published_at;
    int previous_updated_at;
    int save_code;

    ensure_runtime_state();
    record = _blueprints[quest_id];
    if (!mapp(record) || !mapp(record["quest"]))
        return 0;

    quest_def = record["quest"];
    previous_released = quest_def["released"];
    previous_status = record["status"];
    previous_updated_at = record["updated_at"];
    previous_published_at = record["published_at"];
    quest_def["released"] = 0;
    record["status"] = "draft";
    record["updated_at"] = time();
    record["published_at"] = 0;
    save_code = save_blueprints();
    if (save_code == 0) {
        /* L-150：落盘失败恢复下架前状态，不得报成功。 */
        quest_def["released"] = previous_released;
        record["status"] = previous_status;
        record["updated_at"] = previous_updated_at;
        record["published_at"] = previous_published_at;
        return 0;
    }
    return save_code == 1 ? 1 : 0;
}

int delete_blueprint(string quest_id)
{
    mapping previous_record;
    int save_code;

    ensure_runtime_state();
    if (!is_safe_quest_id(quest_id) || !mapp(_blueprints[quest_id]))
        return 0;

    previous_record = _blueprints[quest_id];
    map_delete(_blueprints, quest_id);
    save_code = save_blueprints();
    if (save_code == 0) {
        /* L-150：落盘失败恢复被删蓝图，不得报成功。 */
        _blueprints[quest_id] = previous_record;
        return 0;
    }
    /* Keep a pending/unknown deletion candidate for owner recovery, but do
     * not report it as a completed command. */
    return save_code == 1 ? 1 : 0;
}

mapping query_published_definitions()
{
    string quest_id;
    mapping record;
    mapping result;

    ensure_runtime_state();
    result = ([]);
    foreach (quest_id, record in _blueprints) {
        if (!mapp(record) || record["status"] != "published" || !mapp(record["quest"]))
            continue;
        result[quest_id] = clone_quest_definition(record["quest"]);
    }
    return result;
}

void create()
{
    ::create();
    restore_blueprints();
}
