/**
 * @file    content_mode_policy_d.c
 * @brief   内容可见性、使用策略和内容链接真源。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

#define LINK_TYPES ({ "scene", "rumor", "skill", "item" })
#define STATE_SCOPES ({ "none", "world", "character", "mmo" })
#define INSTANCE_SCOPES ({ "shared", "character" })
#define BINDING_POLICIES ({ "none", "character", "account" })
/* action 投影剥离的冗余 policy 字段（客户端 DialogActionPacket 不消费）。
 * 若 project_content 未来新增注入字段，须同步到此清单，否则字段会悄悄回传使包复膨胀。 */
#define ACTION_POLICY_STRIP_KEYS ({ \
    "state_scope", "instance_scope", \
    "acquisition_modes", "usage_modes", "binding_policy", "requirements", \
    "cross_world_links" })

nosave mapping _cross_world_links;
nosave mapping _health_status;

mapping project_content(object actor, string content_type, mapping definition);

private mixed deep_copy(mixed value)
{
    return restore_variable(save_variable(value));
}

private mapping failure(string code, string message)
{
    return ([
        "ok": 0,
        "error_code": code || "content_policy_failed",
        "error": message || "content mode policy is invalid",
    ]);
}

private string *normalize_modes(mixed raw_modes)
{
    string *normalized;
    string mode;

    normalized = ({ });
    if (!arrayp(raw_modes))
        return normalized;
    foreach (mode in raw_modes) {
        if (member_array(mode, normalized) != -1)
            continue;
        normalized += ({ mode });
    }
    return normalized;
}

private string *normalize_entitlements(mixed raw_entitlements)
{
    string *normalized;
    string entitlement_id;

    normalized = ({ });
    if (!arrayp(raw_entitlements))
        return normalized;
    foreach (entitlement_id in raw_entitlements) {
        if (!stringp(entitlement_id) || entitlement_id == ""
            || member_array(entitlement_id, normalized) != -1)
            continue;
        normalized += ({ entitlement_id });
    }
    return normalized;
}

private string query_default_state_scope(string content_type)
{
    if (strsrch(content_type, "quest") == 0)
        return "character";
    if (content_type == "key_item")
        return "character";
    if (member_array(content_type, ({
        "escort", "trade_route", "fishing", "pvp", "mmo_activity",
    })) != -1)
        return "mmo";
    if (content_type == "skill" || content_type == "item")
        return "character";
    if (content_type == "dungeon")
        return "character";
    return "world";
}

private string query_default_instance_scope(string content_type)
{
    return "shared";
}

private mapping normalize_requirements(mapping definition)
{
    mapping requirements;

    requirements = mapp(definition["requirements"])
        ? deep_copy(definition["requirements"]) : ([]);
    requirements["entitlements"] = normalize_entitlements(
        requirements["entitlements"]);
    return requirements;
}

private mapping normalize_policy(string content_type, mapping definition)
{
    mapping policy;
    string binding_policy;
    string state_scope;

    state_scope = definition["state_scope"] ||
        query_default_state_scope(content_type);
    binding_policy = definition["binding_policy"] ||
        ((content_type == "item" || content_type == "skill")
            ? "character" : "none");
    policy = ([
        "content_type": content_type,
        "state_scope": state_scope,
        "instance_scope": definition["instance_scope"] ||
            query_default_instance_scope(content_type),
        "acquisition_modes": normalize_modes(definition["acquisition_modes"]),
        "usage_modes": normalize_modes(definition["usage_modes"]),
        "binding_policy": binding_policy,
        "requirements": normalize_requirements(definition),
        "cross_world_links": mapp(definition["cross_world_links"])
            ? deep_copy(definition["cross_world_links"]) : ([]),
    ]);
    return policy;
}

private mapping validate_normalized_policy(mapping policy)
{
    mapping links;
    string link_type;
    mixed link_target;

    if (!mapp(policy) || !stringp(policy["content_type"])
        || policy["content_type"] == "")
        return failure("invalid_content_type", "content type is required");
    if (member_array(policy["state_scope"], STATE_SCOPES) == -1)
        return failure("invalid_state_scope", "content state scope is invalid");
    if (member_array(policy["instance_scope"], INSTANCE_SCOPES) == -1)
        return failure("invalid_instance_scope", "content instance scope is invalid");
    if (member_array(policy["binding_policy"], BINDING_POLICIES) == -1)
        return failure("invalid_binding_policy", "content binding policy is invalid");

    links = policy["cross_world_links"];
    foreach (link_type, link_target in links) {
        if (member_array(link_type, LINK_TYPES) == -1
            || (!stringp(link_target) && !mapp(link_target)))
            return failure("invalid_cross_world_link",
                "cross world link type or target is invalid");
    }
    return ([ "ok": 1, "policy": policy ]);
}

private void index_cross_world_links(mapping definition, mapping policy)
{
    mapping link_record;
    string content_id;
    string link_type;
    mixed link_target;

    content_id = definition["id"] || definition["content_id"] || "";
    if (content_id == "")
        return;
    foreach (link_type, link_target in policy["cross_world_links"]) {
        link_record = mapp(link_target)
            ? deep_copy(link_target) : ([ "target_id": link_target ]);
        link_record["link_type"] = link_type;
        link_record["content_id"] = content_id;
        _cross_world_links[link_type][content_id] = link_record;
        _health_status["links_indexed"]++;
    }
}

mapping validate_content_policy(string content_type, mapping definition)
{
    mapping policy;

    if (!stringp(content_type) || content_type == "" || !mapp(definition))
        return failure("invalid_definition", "content type and definition are required");
    policy = normalize_policy(content_type, definition);
    return validate_normalized_policy(policy);
}

mapping compile_content_policy(string content_type, mapping definition)
{
    mapping validated;

    validated = validate_content_policy(content_type, definition);
    if (!validated["ok"]) {
        _health_status["compile_failures"]++;
        _health_status["last_error"] = validated["error"] ||
            "content policy compilation failed";
        return validated;
    }
    index_cross_world_links(definition, validated["policy"]);
    _health_status["policies_compiled"]++;
    validated["policy"]["ok"] = 1;
    return validated["policy"];
}

private int policy_allows_actor(object actor, mapping policy)
{
    string entitlement_id;

    if (!objectp(actor) || !mapp(policy))
        return 0;
    foreach (entitlement_id in policy["requirements"]["entitlements"]) {
        if (!ENTITLEMENT_D->has_entitlement(actor, entitlement_id))
            return 0;
    }
    return 1;
}

int is_content_visible(object actor, string content_type, mapping definition)
{
    mapping policy;

    if (!objectp(actor))
        return 0;
    policy = compile_content_policy(content_type, definition);
    if (!mapp(policy) || !policy["ok"])
        return 0;
    return policy_allows_actor(actor, policy);
}

int is_content_acquirable(object actor, string content_type, mapping definition)
{
    mapping policy;

    if (!objectp(actor))
        return 0;
    policy = compile_content_policy(content_type, definition);
    if (!mapp(policy) || !policy["ok"])
        return 0;
    return policy_allows_actor(actor, policy);
}

int is_content_usable(object actor, string content_type, mapping definition)
{
    mapping policy;

    if (!objectp(actor))
        return 0;
    policy = compile_content_policy(content_type, definition);
    if (!mapp(policy) || !policy["ok"])
        return 0;
    return policy_allows_actor(actor, policy);
}

mapping project_content(object actor, string content_type, mapping definition)
{
    mapping policy;
    mapping projected;

    policy = compile_content_policy(content_type, definition);
    if (!mapp(policy) || !policy["ok"])
        return policy;
    if (!policy_allows_actor(actor, policy)) {
        _health_status["projections_hidden"]++;
        return ([
            "ok": 1,
            "visible": 0,
            "content_type": content_type,
            "content_id": definition["id"] || definition["content_id"] || "",
        ]);
    }
    projected = deep_copy(definition);
    projected["state_scope"] = policy["state_scope"];
    projected["instance_scope"] = policy["instance_scope"];
    projected["acquisition_modes"] = copy(policy["acquisition_modes"]);
    projected["usage_modes"] = copy(policy["usage_modes"]);
    projected["binding_policy"] = policy["binding_policy"];
    projected["requirements"] = deep_copy(policy["requirements"]);
    projected["cross_world_links"] = deep_copy(policy["cross_world_links"]);
    projected["ok"] = 1;
    projected["visible"] = 1;
    _health_status["projections_visible"]++;
    return projected;
}

private string query_action_content_type(mapping action)
{
    string content_type;
    string kind;

    content_type = action["content_type"] || "";
    if (content_type != "")
        return content_type;
    kind = action["kind"] || "";
    if (action["surface"] == "quest" || action["target_group"] == "quest"
        || strsrch(kind, "quest") == 0)
        return "quest_action";
    return kind != "" ? kind : "action";
}

mapping *project_actions(object actor, mapping *actions)
{
    mapping action;
    mapping definition;
    mapping projected;
    mapping *result;
    string content_type;

    result = ({ });
    if (!arrayp(actions))
        return result;
    foreach (action in actions) {
        if (!mapp(action))
            continue;
        content_type = query_action_content_type(action);
        definition = copy(action);
        projected = project_content(actor, content_type, definition);
        if (mapp(projected) && projected["visible"]) {
            /* 精简房间包：仅 action 投影剥离客户端不消费的 policy 字段。
             * 客户端 DialogActionPacket 只消费 id/surface/role/label/cmd/type/
             * kind/target_group。不能动 project_content 全局——room/npc/item 投影保留
             * 这些字段供 room/inventory 包消费（ProjectionPackets.kt/WorldPackets.kt）。 */
            foreach (string key in ACTION_POLICY_STRIP_KEYS)
                map_delete(projected, key);
            result += ({ projected });
        }
    }
    return result;
}

mapping query_cross_world_link(string link_type, string content_id)
{
    mapping record;

    if (member_array(link_type, LINK_TYPES) == -1
        || !stringp(content_id) || content_id == "")
        return ([]);
    record = _cross_world_links[link_type][content_id];
    return mapp(record) ? deep_copy(record) : ([]);
}

/* 允许的 scope 值查询（内容校验方消费，避免复制字面量产生双真源）。 */
string *query_allowed_state_scopes()
{
    return STATE_SCOPES;
}

string *query_allowed_instance_scopes()
{
    return INSTANCE_SCOPES;
}

mapping query_health_snapshot()
{
    mapping snapshot;
    string link_type;
    int link_count;

    foreach (link_type in LINK_TYPES)
        link_count += sizeof(_cross_world_links[link_type] || ([]));
    snapshot = copy(_health_status || ([]));
    snapshot["status"] = snapshot["last_error"] == "" ? "ok" : "degraded";
    snapshot["link_types"] = LINK_TYPES;
    snapshot["cross_world_link_count"] = link_count;
    return snapshot;
}

void create()
{
    string link_type;

    ::create();
    _cross_world_links = ([]);
    foreach (link_type in LINK_TYPES)
        _cross_world_links[link_type] = ([]);
    _health_status = ([
        "policies_compiled": 0,
        "compile_failures": 0,
        "projections_visible": 0,
        "projections_hidden": 0,
        "links_indexed": 0,
        "last_error": "",
    ]);
}
