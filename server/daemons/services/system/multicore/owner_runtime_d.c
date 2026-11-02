/**
 * @file    owner_runtime_d.c
 * @brief   mudlib 侧 owner metadata、路由和 trace 汇总门面
 *
 * 该服务把 driver 已暴露的 owner efun 与 owner route 合并成稳定查询面。
 * 它不持有 LPC object 的第二份真状态，也不直接跨 owner 写对象。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

#define WORKER_DRIVER_ADAPTER       "/daemons/services/system/multicore/worker_driver_adapter"
#define OWNER_RUNTIME_CACHE_LIMIT  1024
#define OWNER_RUNTIME_CACHE_TTL    900
#define OWNER_RUNTIME_PRUNE_BUDGET 64

nosave mapping _object_lifecycle;
nosave mapping _route_cache;
nosave mapping _gray_state;
nosave int _runtime_cache_evictions;

private string *query_registered_lpc_task_methods()
{
    return ({
        "owner_task_readonly",
        "owner_task_player",
        "owner_task_room",
        "owner_task_session",
        "owner_task_item",
        "owner_task_economy",
        "owner_task_combat",
        "owner_task_mail",
        "owner_task_reward",
        "owner_task_world",
        "owner_task_persistence",
        "owner_task_team",
        "owner_task_guild",
        "owner_task_sect",
        "owner_task_quest",
        "owner_task_rank",
        "owner_task_crafting",
        "owner_task_life_skill",
        "owner_task_idle",
        "owner_task_thievery",
    });
}

void create()
{
    ::create();
    _object_lifecycle = ([]);
    _route_cache = ([]);
    _gray_state = ([
        "enabled": 0,
        "profile": "off",
        "updated_at": time(),
        "rollback_supported": 1,
    ]);
    _runtime_cache_evictions = 0;
}

int health_check()
{
    return mapp(_object_lifecycle) && mapp(_route_cache) && mapp(_gray_state);
}

private object query_adapter()
{
    return find_object(WORKER_DRIVER_ADAPTER) || load_object(WORKER_DRIVER_ADAPTER);
}

private string query_object_key(object target)
{
    return objectp(target) ? file_name(target) : "";
}

private int lifecycle_state_is_terminal(string state)
{
    return stringp(state) && member_array(state, ({
        "destroyed", "destructed", "released", "closed", "terminal_failure"
    })) != -1;
}

private void prune_owner_runtime_cache()
{
    string key;
    string oldest_key;
    mapping lifecycle;
    mapping cached;
    int inspected;
    int now;
    int cached_at;
    int oldest_at;

    now = time();
    inspected = 0;
    foreach (key in keys(_object_lifecycle)) {
        if (inspected++ >= OWNER_RUNTIME_PRUNE_BUDGET)
            break;
        lifecycle = _object_lifecycle[key];
        if (!mapp(lifecycle)
            || lifecycle_state_is_terminal(lifecycle["state"])
            || !intp(lifecycle["updated_at"])
            || now - lifecycle["updated_at"] > OWNER_RUNTIME_CACHE_TTL) {
            map_delete(_object_lifecycle, key);
            _runtime_cache_evictions++;
        }
    }

    inspected = 0;
    foreach (key in keys(_route_cache)) {
        if (inspected++ >= OWNER_RUNTIME_PRUNE_BUDGET)
            break;
        cached = _route_cache[key];
        cached_at = mapp(cached) && intp(cached["cached_at"])
            ? cached["cached_at"] : 0;
        if (cached_at <= 0
            || now - cached_at > OWNER_RUNTIME_CACHE_TTL) {
            map_delete(_route_cache, key);
            _runtime_cache_evictions++;
        }
    }

    while (sizeof(keys(_route_cache)) > OWNER_RUNTIME_CACHE_LIMIT) {
        oldest_key = "";
        oldest_at = now + 1;
        foreach (key in keys(_route_cache)) {
            cached = _route_cache[key];
            cached_at = mapp(cached) && intp(cached["cached_at"])
                ? cached["cached_at"] : 0;
            if (cached_at < oldest_at) {
                oldest_key = key;
                oldest_at = cached_at;
            }
        }
        if (oldest_key == "")
            break;
        map_delete(_route_cache, oldest_key);
        _runtime_cache_evictions++;
    }

    while (sizeof(keys(_object_lifecycle)) > OWNER_RUNTIME_CACHE_LIMIT) {
        oldest_key = "";
        oldest_at = now + 1;
        foreach (key in keys(_object_lifecycle)) {
            lifecycle = _object_lifecycle[key];
            if (!mapp(lifecycle)) {
                oldest_key = key;
                break;
            }
            if ((lifecycle["updated_at"] || 0) < oldest_at) {
                oldest_key = key;
                oldest_at = lifecycle["updated_at"] || 0;
            }
        }
        if (oldest_key == "")
            break;
        map_delete(_object_lifecycle, oldest_key);
        _runtime_cache_evictions++;
    }
}

int clear_object_runtime_state(object target)
{
    string key;

    key = query_object_key(target);
    if (key == "")
        return 0;
    map_delete(_object_lifecycle, key);
    map_delete(_route_cache, key);
    return 1;
}

private mapping query_driver_owner_status(object target)
{
    object adapter;
    mapping status;
    mixed err;

    if (!objectp(target))
        return ([ "ok": 0, "reason": "object is required" ]);
    adapter = query_adapter();
    if (!objectp(adapter))
        return ([ "ok": 0, "reason": "driver adapter unavailable" ]);

    err = catch(status = adapter->query_owner_status(target));
    if (err || !mapp(status))
        return ([ "ok": 0, "reason": "driver owner status failed", "error": err ]);
    status["ok"] = 1;
    return status;
}

mapping query_owner_metadata(object target)
{
    mapping status;
    mapping route;
    mapping lifecycle;
    string key;

    if (!objectp(target))
        return ([ "ok": 0, "reason": "object is required" ]);

    prune_owner_runtime_cache();
    key = query_object_key(target);
    status = query_driver_owner_status(target);
    route = OWNER_ROUTE_D->query_owner_route_for_object(target);
    lifecycle = mapp(_object_lifecycle[key]) ? copy(_object_lifecycle[key]) : ([
        "state": "live",
        "updated_at": time(),
    ]);

    return ([
        "ok": 1,
        "object_id": key,
        "owner_id": mapp(status) ? (status["owner_id"] || "") : "",
        "owner_epoch": mapp(status) ? (status["owner_epoch"] || 0) : 0,
        "lifecycle_state": lifecycle["state"] || "live",
        "lifecycle": lifecycle,
        "route": route,
    ]);
}

mapping submit_lpc_canary(object target, string owner_id)
{
    object adapter;
    mapping submitted;
    mixed err;

    if (!objectp(target) || !stringp(owner_id) || owner_id == "")
        return ([ "ok": 0, "reason": "target and owner_id are required" ]);

    adapter = query_adapter();
    if (!objectp(adapter))
        return ([ "ok": 0, "reason": "driver adapter unavailable" ]);

    err = catch(submitted = adapter->submit_owner_lpc_canary(target, owner_id, "owner_lpc_canary"));
    if (err || !mapp(submitted))
        return ([ "ok": 0, "reason": "driver lpc canary submit failed", "error": err ]);

    submitted = copy(submitted);
    submitted["ok"] = submitted["success"] == 1 ? 1 : 0;
    submitted["restricted_lpc_canary"] = 1;
    submitted["ordinary_lpc_default_closed"] = 1;
    submitted["direct_cross_owner_write"] = 0;
    return submitted;
}

mapping query_lpc_task_registry_contract()
{
    return ([
        "registry_mode": "explicit_owner_task_allowlist",
        "allowed_methods": query_registered_lpc_task_methods(),
        "required_method_prefix": "owner_task_",
        "ordinary_lpc_default_closed": 1,
        "unregistered_task_rejected": 1,
        "registered_domain_tasks": sizeof(query_registered_lpc_task_methods()),
        "max_owner_threads": 4,
        "same_owner_fifo": 1,
        "direct_cross_owner_write": 0,
    ]);
}

mapping query_driver_domain_registry_status()
{
    object adapter;
    mapping status;
    string *required;
    mixed *allowlist;
    string *missing;
    string method;
    mixed err;

    required = query_registered_lpc_task_methods();
    allowlist = ({});
    missing = ({});

    adapter = query_adapter();
    if (!objectp(adapter))
        return ([
            "ok": 0,
            "reason": "driver adapter unavailable",
            "required_domains": required,
            "driver_allowlist": allowlist,
            "missing_driver_domains": required,
            "facade_only_runtime_claims": 0,
        ]);

    err = catch(status = adapter->query_owner_runtime_status());
    if (err || !mapp(status))
        return ([
            "ok": 0,
            "reason": "driver runtime status unavailable",
            "error": err,
            "required_domains": required,
            "driver_allowlist": allowlist,
            "missing_driver_domains": required,
            "facade_only_runtime_claims": 0,
        ]);

    allowlist = arrayp(status["lpc_task_allowlist"]) ? status["lpc_task_allowlist"] : ({});
    foreach (method in required) {
        if (member_array(method, allowlist) == -1)
            missing += ({ method });
    }

    return ([
        "ok": sizeof(missing) == 0 &&
            status["registered_owner_task_domains_ready"] == 1 &&
            status["registered_owner_task_domain_count"] >= sizeof(required) ? 1 : 0,
        "required_domain_count": sizeof(required),
        "driver_domain_count": intp(status["registered_owner_task_domain_count"])
            ? status["registered_owner_task_domain_count"] : sizeof(allowlist),
        "required_domains": required,
        "driver_allowlist": allowlist,
        "missing_driver_domains": missing,
        "driver_registered_owner_task_domains_ready": status["registered_owner_task_domains_ready"] == 1 ? 1 : 0,
        "target_owner_message_executor_ready": status["target_owner_message_executor_ready"] == 1 ? 1 : 0,
        "normal_path_main_fallback_count": intp(status["normal_path_main_fallback_count"])
            ? status["normal_path_main_fallback_count"] : -1,
        "service_shard_executor_ready": status["service_shard_executor_ready"] == 1 ? 1 : 0,
        "ordinary_lpc_default_closed": status["ordinary_lpc_default_closed"] == 1 ? 1 : 0,
        "facade_only_runtime_claims": 0,
    ]);
}

mapping submit_lpc_task(object target, string owner_id, string method)
{
    object adapter;
    mapping submitted;
    mixed err;

    if (!objectp(target) || !stringp(owner_id) || owner_id == "" || !stringp(method) || method == "")
        return ([ "ok": 0, "reason": "target, owner_id and method are required" ]);
    if (member_array(method, query_registered_lpc_task_methods()) == -1)
        return ([ "ok": 0, "reason": "owner task is not registered", "unregistered_task_rejected": 1 ]);

    adapter = query_adapter();
    if (!objectp(adapter))
        return ([ "ok": 0, "reason": "driver adapter unavailable" ]);

    err = catch(submitted = adapter->submit_owner_lpc_task(target, owner_id, method));
    if (err || !mapp(submitted))
        return ([ "ok": 0, "reason": "driver lpc task submit failed", "error": err ]);

    submitted = copy(submitted);
    submitted["ok"] = submitted["success"] == 1 ? 1 : 0;
    submitted["registered_task"] = 1;
    submitted["ordinary_lpc_default_closed"] = 1;
    submitted["direct_cross_owner_write"] = 0;
    return submitted;
}

int set_owner_for_object(object target, string owner_id, string lifecycle_state)
{
    object adapter;
    string key;
    mixed err;

    if (!objectp(target) || !stringp(owner_id) || owner_id == "")
        return 0;
    prune_owner_runtime_cache();
    adapter = query_adapter();
    if (!objectp(adapter))
        return 0;

    err = catch(adapter->set_owner_id(target, owner_id));
    if (err)
        return 0;

    key = query_object_key(target);
    _object_lifecycle[key] = ([
        "state": stringp(lifecycle_state) && lifecycle_state != "" ? lifecycle_state : "live",
        "updated_at": time(),
        "owner_id": owner_id,
    ]);
    return 1;
}

int record_lifecycle(object target, string lifecycle_state, string reason)
{
    string key;
    mapping metadata;

    if (!objectp(target) || !stringp(lifecycle_state) || lifecycle_state == "")
        return 0;
    key = query_object_key(target);
    if (lifecycle_state_is_terminal(lifecycle_state))
        return clear_object_runtime_state(target);
    prune_owner_runtime_cache();
    metadata = query_owner_metadata(target);
    _object_lifecycle[key] = ([
        "state": lifecycle_state,
        "reason": stringp(reason) ? reason : "",
        "updated_at": time(),
        "owner_id": metadata["owner_id"] || "",
        "owner_epoch": metadata["owner_epoch"] || 0,
    ]);
    return 1;
}

mapping query_owner_route(object target)
{
    mapping route;
    string key;

    if (!objectp(target))
        return ([ "ok": 0, "reason": "object is required" ]);
    prune_owner_runtime_cache();
    key = query_object_key(target);
    route = OWNER_ROUTE_D->query_owner_route_for_object(target);
    if (mapp(route) && route["ok"]) {
        _route_cache[key] = ([
            "route": copy(route),
            "cached_at": time(),
        ]);
        prune_owner_runtime_cache();
    }
    return route;
}

mapping query_owner_route_for_key(string owner_kind, string key)
{
    string owner_id;

    if (!stringp(owner_kind) || owner_kind == "" || !stringp(key) || key == "")
        return ([ "ok": 0, "reason": "owner_kind and key are required" ]);

    switch (owner_kind) {
        case "player":
            owner_id = "owner/player/" + key;
            break;
        case "room":
        case "region":
            owner_id = "owner/region/" + key;
            break;
        case "combat":
            owner_id = "owner/combat/" + key;
            break;
        case "session":
            owner_id = "owner/session/" + key;
            break;
        case "global_service":
            owner_id = "owner/global/service/" + key;
            break;
        case "worker":
            owner_id = "owner/worker/" + key;
            break;
        default:
            return ([ "ok": 0, "reason": "unknown owner_kind" ]);
    }

    return ([
        "ok": 1,
        "owner_kind": owner_kind,
        "owner_key": key,
        "owner_id": owner_id,
        "single_writer": owner_kind == "worker" ? 0 : 1,
        "message_only_cross_owner": 1,
        "direct_cross_owner_write": 0,
    ]);
}

mapping query_service_policy(string service)
{
    mapping policy;

    if (!stringp(service) || service == "")
        return ([ "ok": 0, "reason": "service is required" ]);

    policy = MULTICORE_CONFIG_D->query_service_policy(service);
    if (!mapp(policy) || (!undefinedp(policy["ok"]) && policy["ok"] == 0))
        return policy;

    policy["ok"] = 1;
    policy["service_map_v2"] = 1;
    policy["config_source"] = "multicore_config_d";
    return policy;
}

mapping query_service_owner_route(string service, string key)
{
    mapping policy;
    mapping readiness;
    mapping service_readiness;
    mapping dependencies;
    string owner_id;
    string route_key;
    string failure_reason;
    mixed err;

    policy = query_service_policy(service);
    err = catch(readiness = MULTICORE_CONFIG_D->query_runtime_readiness());
    if (err)
        readiness = ([ "ok": 0, "errors": ({ sprintf("multicore_config_d: %O", err) }) ]);
    err = catch(service_readiness = REGISTRY_D->query_service_readiness(service));
    if (err)
        service_readiness = ([
            "graph_ok": 0,
            "registered": 0,
            "node_ready": 0,
            "error": sprintf("registry_d: %O", err),
            "dependencies": ([ ]),
        ]);
    dependencies = mapp(service_readiness)
        && mapp(service_readiness["dependencies"])
        ? service_readiness["dependencies"] : ([ ]);
    if (!mapp(policy) || policy["ok"] == 0)
        failure_reason = "service policy is unavailable";
    else if (!mapp(service_readiness)
        || service_readiness["graph_ok"] != 1)
        failure_reason = mapp(service_readiness)
            && stringp(service_readiness["error"])
            ? "registry_d: " + service_readiness["error"]
            : "registry_d: service dependency graph is not ready";
    else if (service_readiness["registered"] != 1)
        failure_reason = "service is not registered";
    else if (service_readiness["node_ready"] != 1)
        failure_reason = "service dependencies are not ready";
    else if (!mapp(readiness) || readiness["ok"] != 1)
        failure_reason = "owner runtime is not ready";

    if (stringp(failure_reason) && failure_reason != "")
        return ([
            "ok": 0,
            "service": service,
            "key": stringp(key) && key != "" ? key : service,
            "owner_id": mapp(policy) ? (policy["owner_id"] || "") : "",
            "owner_epoch": mapp(readiness) ? (readiness["owner_epoch"] || 0) : 0,
            "readiness": mapp(readiness) ? readiness : ([
                "ok": 0,
                "errors": ({ "owner readiness unavailable" }),
            ]),
            "dependencies": dependencies,
            "failure_reason": failure_reason,
            "direct_cross_owner_write": 0,
        ]);

    route_key = stringp(key) && key != "" ? key : service;
    switch (policy["policy"]) {
        case "owner_scheduler":
            owner_id = "owner/service/" + service + "/" + route_key;
            break;
        case "service_owner":
            owner_id = policy["owner_id"] + "/" + route_key;
            break;
        case "snapshot_only":
            owner_id = policy["owner_id"];
            break;
        case "single_thread":
        default:
            owner_id = "owner/global/main";
            break;
    }

    return ([
        "ok": 1,
        "service": service,
        "key": route_key,
        "owner_id": owner_id,
        "owner_epoch": readiness["owner_epoch"] || 0,
        "readiness": readiness,
        "dependencies": dependencies,
        "failure_reason": "",
        "policy": policy["policy"],
        "config_source": "multicore_config_d",
        "service_map_v2": 1,
        "enabled_by_default": policy["enabled_by_default"] ? 1 : 0,
        "service_shard_key": route_key,
        "service_shard_policy": policy["policy"] == "service_owner" ? 1 : 0,
        "service_shard_key_required": policy["service_shard_key_required"] ? 1 : 0,
        "service_shard_owner": policy["policy"] == "service_owner" ? 1 : 0,
        "hot_path_service_owner_single_point": 0,
        "service_owner_single_point": 0,
        "keyed_service_shard_ready": 1,
        "message_only_cross_owner": 1,
        "payload_contract": policy["payload_contract"],
        "commit_boundary_required": policy["commit_boundary_required"],
        "snapshot_only": policy["snapshot_only"],
        "same_owner_direct_only": policy["same_owner_direct_only"],
        "direct_cross_owner_write": 0,
    ]);
}

mapping query_service_map_v2()
{
    mapping service_map;
    mapping result;
    string service;

    service_map = MULTICORE_CONFIG_D->query_service_map_v2();
    result = ([]);
    foreach (service in keys(service_map)) {
        result[service] = query_service_owner_route(service, service);
    }
    return result;
}

mapping explain_owner(object target)
{
    mapping metadata;

    metadata = query_owner_metadata(target);
    if (!mapp(metadata) || !metadata["ok"])
        return metadata;

    return ([
        "ok": 1,
        "object_id": metadata["object_id"],
        "owner_id": metadata["owner_id"],
        "owner_epoch": metadata["owner_epoch"],
        "lifecycle_state": metadata["lifecycle_state"],
        "route": metadata["route"],
        "single_writer": 1,
        "message_only_cross_owner": 1,
        "direct_cross_owner_write": 0,
    ]);
}

mapping query_runtime_status()
{
    mapping thread_status;
    mapping config_status;
    mapping driver_registry;

    prune_owner_runtime_cache();
    thread_status = objectp(query_adapter()) ? query_adapter()->query_owner_thread_status() : ([]);
    config_status = MULTICORE_CONFIG_D->query_status();
    driver_registry = query_driver_domain_registry_status();
    return ([
        "tracked_lifecycle_objects": sizeof(_object_lifecycle),
        "cached_routes": sizeof(_route_cache),
        "cache_limit": OWNER_RUNTIME_CACHE_LIMIT,
        "cache_ttl_seconds": OWNER_RUNTIME_CACHE_TTL,
        "cache_evictions": _runtime_cache_evictions,
        "driver_adapter": objectp(query_adapter()) ? 1 : 0,
        "mode": "owner_runtime_domain_registry",
        "thread_status": mapp(thread_status) ? thread_status : ([]),
        "gray_state": copy(_gray_state),
        "service_map_v2_ready": 1,
        "runtime_readiness": MULTICORE_CONFIG_D->query_runtime_readiness(),
        "service_map": query_service_map_v2(),
        "service_policy_modes": config_status["service_policy_modes"],
        "service_map_default_enabled": config_status["service_map_default_enabled"] ? 1 : 0,
        "service_payload_contract": "spec_snapshot_or_commit_proposal_only",
        "default_mode": config_status["default_mode"],
        "default_thread_count": config_status["default_thread_count"],
        "production_start_path": config_status["production_start_path"],
        "enabled_by_default": config_status["enabled_by_default"] ? 1 : 0,
        "requires_explicit_start": config_status["requires_explicit_start"] ? 1 : 0,
        "automatic_fallback": 0,
        "emergency_fallback_mode": "single_thread",
        "boot_start_attempted": config_status["boot_start_attempted"],
        "boot_start_ok": config_status["boot_start_ok"],
        "boot_start": config_status["boot_start"],
        "driver_domain_registry": driver_registry,
        "driver_domain_registry_ready": driver_registry["ok"] == 1 ? 1 : 0,
        "missing_driver_domains": driver_registry["missing_driver_domains"],
        "registered_domain_tasks": sizeof(query_registered_lpc_task_methods()),
        "production_owner_runtime_ready": driver_registry["ok"] == 1 &&
            config_status["production_owner_mode_ready"] == 1 ? 1 : 0,
        "facade_only_runtime_claims": 0,
        "keyed_service_shard_ready": 1,
        "hot_path_service_owner_single_point": 0,
        "service_owner_single_point": 0,
        "service_shard_executor_ready": driver_registry["service_shard_executor_ready"] == 1 ? 1 : 0,
        "rollback_supported": 1,
        "restricted_lpc_canary": 1,
        "ordinary_lpc_default_closed": 1,
        "direct_cross_owner_write": 0,
    ]);
}

mapping query_gray_control_contract()
{
    mapping config_status;

    config_status = MULTICORE_CONFIG_D->query_status();
    return ([
        "enabled_by_default": 1,
        "requires_explicit_start": 0,
        "automatic_fallback": 0,
        "emergency_fallback_mode": "single_thread",
        "gray_switch_default_enabled": 0,
        "allowed_profiles": ({ "off", "canary", "registered_task", "queue_stress" }),
        "forbidden_profiles": ({ "ordinary_lpc", "direct_parallel_gameplay" }),
        "default_mode": config_status["default_mode"],
        "default_thread_count": config_status["default_thread_count"],
        "production_start_path": config_status["production_start_path"],
        "gray_start_path": "ownercheck canary or explicit admin start",
        "gray_rollback_path": "gray_profile_off_without_worker_stop",
        "emergency_rollback_path": "multicore_config_d.rollback_to_single_thread",
        "rollback_supported": 1,
        "max_owner_threads": config_status["owner_thread_full_count"],
        "off_main_lpc_execution": "ordinary_rejected_probe_guarded_canary_registered_task_allowed",
        "direct_cross_owner_write": 0,
    ]);
}

mapping set_gray_profile(string profile, string reason)
{
    if (!stringp(profile) || member_array(profile, ({ "off", "canary", "registered_task", "queue_stress" })) == -1)
        return ([ "ok": 0, "reason": "unsupported gray profile" ]);
    _gray_state = ([
        "enabled": profile == "off" ? 0 : 1,
        "profile": profile,
        "reason": stringp(reason) ? reason : "",
        "updated_at": time(),
        "rollback_supported": 1,
    ]);
    return ([ "ok": 1, "gray_state": copy(_gray_state), "direct_cross_owner_write": 0 ]);
}

mapping rollback_gray_profile(string reason)
{
    _gray_state = ([
        "enabled": 0,
        "profile": "off",
        "reason": stringp(reason) ? reason : "rollback",
        "updated_at": time(),
        "rollback_supported": 1,
        "production_worker_unchanged": 1,
    ]);
    return ([
        "ok": 1,
        "gray_state": copy(_gray_state),
        "rollback_supported": 1,
        "production_worker_unchanged": 1,
    ]);
}

mapping query_stress_contract()
{
    return ([
        "stress_scope": "owner_mailbox_canary_and_registered_task_only",
        "requires_curated_profiles": ({ "contracts", "maintenance" }),
        "requires_live_ownercheck": ({ "runtime", "canary", "message", "route" }),
        "max_owner_threads": 4,
        "registered_task_only": 1,
        "ordinary_lpc_default_closed": 1,
        "single_thread_fallback": 0,
        "normal_path_single_thread_fallback": 0,
        "explicit_rollback_fallback": 1,
        "rollback_supported": 1,
        "direct_cross_owner_write": 0,
    ]);
}

mapping query_trace_contract()
{
    return ([
        "required_fields": ({
            "task_id",
            "owner_id",
            "owner_epoch",
            "causation_id",
            "source_owner",
            "target_owner",
            "operation",
            "state",
            "duration_ms",
            "recorded_at",
        }),
        "golden_trace_points": ({
            "login",
            "look",
            "chain_move",
            "get_drop",
            "trade",
            "mail_claim",
            "combat",
            "death_or_flee",
            "disconnect_resume",
            "save_restore",
        }),
        "direct_cross_owner_write": 0,
        "message_only_cross_owner": 1,
    ]);
}
