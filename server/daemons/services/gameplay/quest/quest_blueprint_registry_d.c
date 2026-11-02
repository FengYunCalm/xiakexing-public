/**
 * @file    quest_blueprint_registry_d.c
 * @brief   任务蓝图注册表真源
 *
 * @note
 * 模板 schema、artifact schema、compose 规则统一从这里导出。
 * 具体模板载荷构建仍回到 `quest_blueprint_template_d.c`，蓝图 artifact
 * 真格式仍回到 `quest_blueprint_d.c`；registry 负责统一读侧与版本治理。
 */

#pragma strict_types

#include <system/services.h>

inherit "/lib/framework/daemon";

#define QUEST_BLUEPRINT_REGISTRY_VERSION 2
#define QUEST_BLUEPRINT_TEMPLATE_SCHEMA_VERSION 1
#define QUEST_BLUEPRINT_COMPOSE_SCHEMA_VERSION 1

string *query_template_ids();
mapping query_template_schema(string template_id);
mapping query_artifact_schema();
mapping normalize_template_schema(mapping schema);
mapping migrate_template_schema(mapping schema);
mapping normalize_composed_template_schema(mapping schema);
mapping migrate_composed_template_schema(mapping schema);
mapping normalize_artifact(mapping artifact);
mapping migrate_artifact(mapping artifact);

private mapping clone_mapping_value(mapping value)
{
    if (!mapp(value))
        return 0;
    return restore_variable(save_variable(value));
}

private string *normalize_string_array(mixed *values)
{
    string *normalized;
    mixed value;

    normalized = ({ });
    if (!arrayp(values))
        return ({ });

    foreach (value in values) {
        if (!stringp(value) || value == "")
            continue;
        if (member_array(value, normalized) != -1)
            continue;
        normalized += ({ value });
    }

    return normalized;
}

private string *normalize_template_ids(string *template_ids)
{
    return normalize_string_array(template_ids);
}

private mapping query_compose_modes()
{
    mapping modes;
    mapping schema;
    string template_id;

    modes = ([]);
    foreach (template_id in query_template_ids()) {
        schema = query_template_schema(template_id);
        if (!mapp(schema))
            continue;
        modes[template_id] = schema["compose_mode"] || "append_steps";
    }

    return modes;
}

private int query_schema_version(mapping schema)
{
    if (!mapp(schema))
        return 0;
    if (!intp(schema["schema_version"]))
        return 0;
    return schema["schema_version"];
}

private mapping normalize_field_schema(mapping field_schema)
{
    mapping normalized;
    mapping field_spec;
    string key;

    normalized = ([]);
    if (!mapp(field_schema))
        return normalized;
    foreach (key in keys(field_schema)) {
        field_spec = mapp(field_schema[key]) ? field_schema[key] : ([]);
        normalized[key] = ([
            "type": field_spec["type"] || "string",
            "required": field_spec["required"] ? 1 : 0,
            "description": field_spec["description"] || "",
        ]);
    }
    return normalized;
}

private mapping normalize_template_schema_internal(mapping schema)
{
    return ([
        "schema_version": QUEST_BLUEPRINT_TEMPLATE_SCHEMA_VERSION,
        "template_id": schema["template_id"] || "",
        "label": schema["label"] || "",
        "quest_type": schema["quest_type"] || "",
        "compose_mode": schema["compose_mode"] || "append_steps",
        "description": schema["description"] || "",
        "required_placeholders": normalize_string_array(schema["required_placeholders"]),
        "placeholder_schema": normalize_field_schema(schema["placeholder_schema"] || ([])),
        "field_schema": normalize_field_schema(schema["field_schema"] || ([])),
        "example_overrides": mapp(schema["example_overrides"]) ? clone_mapping_value(schema["example_overrides"]) : ([]),
    ]);
}

private mapping normalize_component_schemas(mapping component_schemas)
{
    mapping normalized;
    mapping schema;
    string key;

    normalized = ([]);
    if (!mapp(component_schemas))
        return normalized;

    foreach (key in keys(component_schemas)) {
        schema = mapp(component_schemas[key]) ? component_schemas[key] : 0;
        if (!mapp(schema))
            continue;
        normalized[key] = normalize_template_schema_internal(schema);
    }

    return normalized;
}

private mapping normalize_composed_template_schema_internal(mapping schema)
{
    return ([
        "schema_version": QUEST_BLUEPRINT_COMPOSE_SCHEMA_VERSION,
        "template_ids": normalize_template_ids(schema["template_ids"]),
        "quest_type": schema["quest_type"] || "",
        "required_placeholders": normalize_string_array(schema["required_placeholders"]),
        "shared_field_schema": normalize_field_schema(schema["shared_field_schema"] || ([])),
        "component_schemas": normalize_component_schemas(schema["component_schemas"] || ([])),
        "example_overrides": mapp(schema["example_overrides"]) ? clone_mapping_value(schema["example_overrides"]) : ([]),
    ]);
}

private mapping normalize_artifact_internal(mapping artifact)
{
    mapping normalized;
    mapping quest_def;
    mapping source;
    string quest_id;

    normalized = restore_variable(save_variable(artifact));
    if (!mapp(normalized))
        return 0;

    source = mapp(normalized["source"]) ? normalized["source"] : ([]);
    quest_def = mapp(normalized["quest"]) ? normalized["quest"] : ([]);
    quest_id = source["quest_id"] || quest_def["id"] || "";

    normalized["artifact_type"] = normalized["artifact_type"] || "quest_blueprint";
    normalized["schema_version"] = QUEST_BLUEPRINT_D->query_blueprint_artifact_schema()["schema_version"] || 1;
    normalized["source"] = ([
        "quest_id": quest_id,
        "status": source["status"] || "draft",
        "created_by": source["created_by"] || "",
        "updated_at": source["updated_at"] || 0,
        "published_at": source["published_at"] || 0,
    ]);
    quest_def["id"] = quest_id;
    normalized["quest"] = quest_def;
    return normalized;
}

private mapping build_migration_step(int from_version, int to_version, string step, string description)
{
    return ([
        "from_version": from_version,
        "to_version": to_version,
        "step": step,
        "description": description,
    ]);
}

private mixed *query_template_migration_steps()
{
    return ({
        build_migration_step(0,
            QUEST_BLUEPRINT_TEMPLATE_SCHEMA_VERSION,
            "template_v0_to_v1",
            "补齐 schema_version、compose_mode、placeholder_schema、field_schema 与 example_overrides。"),
    });
}

private mixed *query_composed_template_migration_steps()
{
    return ({
        build_migration_step(0,
            QUEST_BLUEPRINT_COMPOSE_SCHEMA_VERSION,
            "compose_v0_to_v1",
            "补齐 schema_version、template_ids、shared_field_schema、component_schemas 与 example_overrides。"),
    });
}

private mixed *query_artifact_migration_steps()
{
    return ({
        build_migration_step(0,
            query_artifact_schema()["schema_version"] || 1,
            "artifact_v0_to_v1",
            "补齐 artifact_type、schema_version、source 元数据并对齐 quest.id/source.quest_id。"),
    });
}

private mapping build_migration_table()
{
    return ([
        "template": query_template_migration_steps(),
        "compose": query_composed_template_migration_steps(),
        "artifact": query_artifact_migration_steps(),
    ]);
}

private mapping migrate_template_schema_v0_to_v1(mapping schema)
{
    mapping migrated;

    migrated = clone_mapping_value(schema);
    if (!mapp(migrated))
        return 0;

    if (!arrayp(migrated["required_placeholders"]))
        migrated["required_placeholders"] = ({ });
    if (!stringp(migrated["compose_mode"]) || migrated["compose_mode"] == "")
        migrated["compose_mode"] = "append_steps";
    if (!mapp(migrated["placeholder_schema"]))
        migrated["placeholder_schema"] = ([]);
    if (!mapp(migrated["field_schema"]))
        migrated["field_schema"] = ([]);
    if (!mapp(migrated["example_overrides"]))
        migrated["example_overrides"] = ([]);

    migrated["schema_version"] = QUEST_BLUEPRINT_TEMPLATE_SCHEMA_VERSION;
    return migrated;
}

private mapping migrate_composed_template_schema_v0_to_v1(mapping schema)
{
    mapping migrated;

    migrated = clone_mapping_value(schema);
    if (!mapp(migrated))
        return 0;

    if (!arrayp(migrated["template_ids"]))
        migrated["template_ids"] = ({ });
    if (!arrayp(migrated["required_placeholders"]))
        migrated["required_placeholders"] = ({ });
    if (!mapp(migrated["shared_field_schema"]))
        migrated["shared_field_schema"] = ([]);
    if (!mapp(migrated["component_schemas"]))
        migrated["component_schemas"] = ([]);
    if (!mapp(migrated["example_overrides"]))
        migrated["example_overrides"] = ([]);

    migrated["schema_version"] = QUEST_BLUEPRINT_COMPOSE_SCHEMA_VERSION;
    return migrated;
}

private mapping migrate_artifact_v0_to_v1(mapping artifact)
{
    mapping migrated;

    migrated = normalize_artifact_internal(artifact);
    if (!mapp(migrated))
        return 0;

    migrated["schema_version"] = query_artifact_schema()["schema_version"] || 1;
    return migrated;
}

mapping query_registry_snapshot()
{
    mapping artifact_schema;
    mapping migration_table;
    string *template_ids;

    template_ids = query_template_ids();
    artifact_schema = query_artifact_schema();
    migration_table = build_migration_table();
    return ([
        "registry_version": QUEST_BLUEPRINT_REGISTRY_VERSION,
        "template_schema_version": QUEST_BLUEPRINT_TEMPLATE_SCHEMA_VERSION,
        "compose_schema_version": QUEST_BLUEPRINT_COMPOSE_SCHEMA_VERSION,
        "artifact_schema_version": artifact_schema["schema_version"] || 0,
        "template_count": sizeof(template_ids),
        "template_ids": template_ids,
        "compose_modes": query_compose_modes(),
        "artifact_type": artifact_schema["artifact_type"] || "",
        "migration_policy": ([
            "template": "explicit_step_chain",
            "compose": "explicit_step_chain",
            "artifact": "explicit_step_chain_before_import",
        ]),
        "migration_table": migration_table,
    ]);
}

string *query_template_ids()
{
    return QUEST_BLUEPRINT_TEMPLATE_D->query_template_ids();
}

mapping query_template_schema(string template_id)
{
    mapping schema;

    schema = QUEST_BLUEPRINT_TEMPLATE_D->query_template_schema(template_id);
    if (!mapp(schema))
        return 0;
    return normalize_template_schema_internal(schema);
}

mapping normalize_template_schema(mapping schema)
{
    if (!mapp(schema))
        return 0;
    return normalize_template_schema_internal(schema);
}

mapping migrate_template_schema(mapping schema)
{
    int current_version;
    int from_version;
    mapping normalized;
    mixed *steps;

    if (!mapp(schema))
        return ([ "ok": 0, "error": "invalid_schema" ]);

    from_version = query_schema_version(schema);
    current_version = from_version;
    normalized = clone_mapping_value(schema);
    steps = ({ });

    if (current_version > QUEST_BLUEPRINT_TEMPLATE_SCHEMA_VERSION) {
        return ([
            "ok": 0,
            "error": "unsupported_schema_version",
            "from_version": current_version,
            "to_version": QUEST_BLUEPRINT_TEMPLATE_SCHEMA_VERSION,
        ]);
    }

    while (current_version < QUEST_BLUEPRINT_TEMPLATE_SCHEMA_VERSION) {
        switch (current_version) {
            case 0:
                normalized = migrate_template_schema_v0_to_v1(normalized);
                steps += ({ build_migration_step(0,
                    QUEST_BLUEPRINT_TEMPLATE_SCHEMA_VERSION,
                    "template_v0_to_v1",
                    "补齐 schema_version、compose_mode、placeholder_schema、field_schema 与 example_overrides。") });
                current_version = QUEST_BLUEPRINT_TEMPLATE_SCHEMA_VERSION;
                break;
            default:
                return ([
                    "ok": 0,
                    "error": "missing_migration_step",
                    "from_version": current_version,
                    "to_version": QUEST_BLUEPRINT_TEMPLATE_SCHEMA_VERSION,
                    "steps": steps,
                ]);
        }

        if (!mapp(normalized))
            return ([ "ok": 0, "error": "migration_failed", "from_version": from_version ]);
    }

    normalized = normalize_template_schema_internal(normalized);
    return ([
        "ok": 1,
        "from_version": from_version,
        "to_version": QUEST_BLUEPRINT_TEMPLATE_SCHEMA_VERSION,
        "migrated": sizeof(steps) ? 1 : 0,
        "steps": steps,
        "schema": normalized,
    ]);
}

mapping validate_template_overrides(string template_id, mapping overrides)
{
    return QUEST_BLUEPRINT_TEMPLATE_D->validate_template_overrides(template_id, overrides);
}

mapping build_template_payload_with_overrides(string template_id, string quest_id, string provider_id, mapping overrides)
{
    return QUEST_BLUEPRINT_TEMPLATE_D->build_template_payload_with_overrides(template_id, quest_id, provider_id, overrides);
}

mapping query_composed_template_schema(string *template_ids)
{
    mapping schema;

    schema = QUEST_BLUEPRINT_TEMPLATE_D->query_composed_template_schema(normalize_template_ids(template_ids));
    if (!mapp(schema))
        return 0;
    return normalize_composed_template_schema_internal(schema);
}

mapping normalize_composed_template_schema(mapping schema)
{
    if (!mapp(schema))
        return 0;
    return normalize_composed_template_schema_internal(schema);
}

mapping migrate_composed_template_schema(mapping schema)
{
    int current_version;
    int from_version;
    mapping normalized;
    mixed *steps;

    if (!mapp(schema))
        return ([ "ok": 0, "error": "invalid_schema" ]);

    from_version = query_schema_version(schema);
    current_version = from_version;
    normalized = clone_mapping_value(schema);
    steps = ({ });

    if (current_version > QUEST_BLUEPRINT_COMPOSE_SCHEMA_VERSION) {
        return ([
            "ok": 0,
            "error": "unsupported_schema_version",
            "from_version": current_version,
            "to_version": QUEST_BLUEPRINT_COMPOSE_SCHEMA_VERSION,
        ]);
    }

    while (current_version < QUEST_BLUEPRINT_COMPOSE_SCHEMA_VERSION) {
        switch (current_version) {
            case 0:
                normalized = migrate_composed_template_schema_v0_to_v1(normalized);
                steps += ({ build_migration_step(0,
                    QUEST_BLUEPRINT_COMPOSE_SCHEMA_VERSION,
                    "compose_v0_to_v1",
                    "补齐 schema_version、template_ids、shared_field_schema、component_schemas 与 example_overrides。") });
                current_version = QUEST_BLUEPRINT_COMPOSE_SCHEMA_VERSION;
                break;
            default:
                return ([
                    "ok": 0,
                    "error": "missing_migration_step",
                    "from_version": current_version,
                    "to_version": QUEST_BLUEPRINT_COMPOSE_SCHEMA_VERSION,
                    "steps": steps,
                ]);
        }

        if (!mapp(normalized))
            return ([ "ok": 0, "error": "migration_failed", "from_version": from_version ]);
    }

    normalized = normalize_composed_template_schema_internal(normalized);
    return ([
        "ok": 1,
        "from_version": from_version,
        "to_version": QUEST_BLUEPRINT_COMPOSE_SCHEMA_VERSION,
        "migrated": sizeof(steps) ? 1 : 0,
        "steps": steps,
        "schema": normalized,
    ]);
}

mapping validate_composed_template_overrides(string *template_ids, mapping overrides)
{
    return QUEST_BLUEPRINT_TEMPLATE_D->validate_composed_template_overrides(normalize_template_ids(template_ids), overrides);
}

mapping build_composed_template_payload(string *template_ids, string quest_id, string provider_id, mapping overrides)
{
    return QUEST_BLUEPRINT_TEMPLATE_D->build_composed_template_payload(normalize_template_ids(template_ids), quest_id, provider_id, overrides);
}

mapping query_artifact_schema()
{
    return QUEST_BLUEPRINT_D->query_blueprint_artifact_schema();
}

mapping normalize_artifact(mapping artifact)
{
    if (!mapp(artifact))
        return 0;
    return normalize_artifact_internal(artifact);
}

mapping migrate_artifact(mapping artifact)
{
    int current_version;
    int from_version;
    mapping normalized;
    mixed *steps;
    int target_version;

    if (!mapp(artifact))
        return ([ "ok": 0, "error": "invalid_artifact" ]);

    from_version = query_schema_version(artifact);
    current_version = from_version;
    normalized = clone_mapping_value(artifact);
    steps = ({ });
    target_version = query_artifact_schema()["schema_version"] || 1;

    if (current_version > target_version) {
        return ([
            "ok": 0,
            "error": "unsupported_schema_version",
            "from_version": current_version,
            "to_version": target_version,
        ]);
    }

    while (current_version < target_version) {
        switch (current_version) {
            case 0:
                normalized = migrate_artifact_v0_to_v1(normalized);
                steps += ({ build_migration_step(0,
                    target_version,
                    "artifact_v0_to_v1",
                    "补齐 artifact_type、schema_version、source 元数据并对齐 quest.id/source.quest_id。") });
                current_version = target_version;
                break;
            default:
                return ([
                    "ok": 0,
                    "error": "missing_migration_step",
                    "from_version": current_version,
                    "to_version": target_version,
                    "steps": steps,
                ]);
        }
    }

    normalized = normalize_artifact_internal(normalized);
    if (!mapp(normalized))
        return ([ "ok": 0, "error": "invalid_artifact" ]);
    return ([
        "ok": 1,
        "from_version": from_version,
        "to_version": target_version,
        "migrated": sizeof(steps) ? 1 : 0,
        "steps": steps,
        "artifact": normalized,
    ]);
}
