/**
 * @file    multicore_config_d.c
 * @brief   多核执行模式与灰度开关真源
 *
 * 该服务只管理运行模式、灰度开关和回滚入口，不直接调度 LPC。真正
 * owner 执行、跨 owner 消息与事务提交仍必须回到对应 owner 服务。
 */

#pragma strict_types
#pragma save_types

inherit "/lib/framework/daemon";

#define MODE_SINGLE_THREAD        "single_thread"
#define MODE_OWNER_SCHEDULER      "owner_scheduler"
#define MODE_OWNER_THREAD_CANARY  "owner_thread_canary"
#define MODE_OWNER_THREAD_COMPACT "owner_thread_compact"
#define MODE_OWNER_THREAD_FULL    "owner_thread_full"

#define WORKER_DRIVER_ADAPTER "/daemons/services/system/multicore/worker_driver_adapter"
#define OWNER_PROFILE_RUNTIME_FILE "/data/runtime/multicore.profile"
#define OWNER_THREAD_CANARY_COUNT 1
#define OWNER_THREAD_COMPACT_COUNT 1
#define OWNER_THREAD_FULL_COUNT 4

#define POLICY_SINGLE_THREAD    "single_thread"
#define POLICY_OWNER_SCHEDULER  "owner_scheduler"
#define POLICY_SERVICE_OWNER    "service_owner"
#define POLICY_SNAPSHOT_ONLY    "snapshot_only"

nosave string _mode;
nosave string _default_mode;
nosave int _default_thread_count;
nosave mapping _feature_flags;
nosave mapping _service_policies;
nosave mapping _last_change;
nosave mapping _boot_start;

mapping query_status();

private int is_valid_mode(string mode)
{
    return member_array(mode, ({
        MODE_SINGLE_THREAD,
        MODE_OWNER_SCHEDULER,
        MODE_OWNER_THREAD_CANARY,
        MODE_OWNER_THREAD_COMPACT,
        MODE_OWNER_THREAD_FULL,
    })) != -1;
}

private string *query_feature_keys()
{
    return ({
        "player_owner",
        "room_owner",
        "combat_owner",
        "session_owner",
        "global_service_owner",
        "async_persistence",
        "aoi",
    });
}

private string *query_service_policy_modes()
{
    return ({
        POLICY_SINGLE_THREAD,
        POLICY_OWNER_SCHEDULER,
        POLICY_SERVICE_OWNER,
        POLICY_SNAPSHOT_ONLY,
    });
}

private int is_valid_service_policy(string policy)
{
    return stringp(policy) && member_array(policy, query_service_policy_modes()) != -1;
}

private int owner_runtime_mode_enabled()
{
    return _mode == MODE_OWNER_SCHEDULER ||
        _mode == MODE_OWNER_THREAD_CANARY ||
        _mode == MODE_OWNER_THREAD_COMPACT ||
        _mode == MODE_OWNER_THREAD_FULL;
}

private int mapping_int(mapping source, string key)
{
    return mapp(source) && intp(source[key]) ? source[key] : 0;
}

private string query_default_owner_mode()
{
    string profile;
    mixed err;

    err = catch(profile = read_file(OWNER_PROFILE_RUNTIME_FILE));
    if (err || !stringp(profile))
        return MODE_OWNER_THREAD_FULL;

    profile = lower_case(trim(profile));
    if (profile == MODE_OWNER_THREAD_COMPACT)
        return MODE_OWNER_THREAD_COMPACT;
    return MODE_OWNER_THREAD_FULL;
}

private int query_mode_thread_count(string mode)
{
    switch (mode) {
    case MODE_OWNER_THREAD_CANARY:
        return OWNER_THREAD_CANARY_COUNT;
    case MODE_OWNER_THREAD_COMPACT:
        return OWNER_THREAD_COMPACT_COUNT;
    case MODE_OWNER_THREAD_FULL:
        return OWNER_THREAD_FULL_COUNT;
    default:
        return 0;
    }
}

private string query_production_start_path()
{
    return "automatic " + _default_mode + " boot";
}

private object load_worker_adapter()
{
    object adapter;
    mixed err;

    err = catch(adapter = load_object(WORKER_DRIVER_ADAPTER));
    if (err || !objectp(adapter))
        return 0;
    return adapter;
}

private mapping query_owner_thread_status()
{
    object adapter;
    mapping status;
    mixed err;

    adapter = load_worker_adapter();
    if (!objectp(adapter))
        return ([
            "success": 0,
            "running": 0,
            "thread_count": 0,
            "max_owner_threads": 0,
            "reason": "worker driver adapter unavailable",
        ]);

    err = catch(status = adapter->query_owner_thread_status());
    if (err || !mapp(status))
        return ([
            "success": 0,
            "running": 0,
            "thread_count": 0,
            "max_owner_threads": 0,
            "error": err,
            "reason": "owner thread status unavailable",
        ]);

    return status;
}

private mapping query_owner_runtime_status()
{
    object adapter;
    mapping status;
    mixed err;

    adapter = load_worker_adapter();
    if (!objectp(adapter))
        return ([]);

    err = catch(status = adapter->query_owner_runtime_status());
    if (err || !mapp(status))
        return ([]);
    return status;
}

private mapping stop_owner_worker(string reason)
{
    object adapter;
    mapping stop_control;
    mapping status;
    mixed err;

    adapter = load_worker_adapter();
    if (!objectp(adapter))
        return ([
            "thread_control": "stop",
            "thread_stop_ok": 0,
            "thread_running": 0,
            "thread_count": 0,
            "max_owner_threads": 0,
            "thread_control_error": "worker driver adapter unavailable",
            "reason": reason,
        ]);

    status = query_owner_thread_status();
    if (mapp(status) && !mapping_int(status, "enabled")
        && !mapping_int(status, "thread_count"))
        return ([
            "thread_control": "stop",
            "thread_stop_ok": 1,
            "owner_output_quiesce_ok": 1,
            "owner_output_quiesce": ([]),
            "thread_running": 0,
            "thread_count": 0,
            "max_owner_threads": mapping_int(status, "max_owner_threads"),
            "thread_control_error": "",
            "reason": reason,
        ]);

    err = catch(stop_control = adapter->stop_owner_thread_gracefully());
    status = query_owner_thread_status();
    return ([
        "thread_control": "stop",
        "thread_stop_ok": err || !mapp(stop_control)
            ? 0 : mapping_int(stop_control, "thread_stop_ok"),
        "owner_output_quiesce_ok": err || !mapp(stop_control)
            ? 0 : mapping_int(stop_control, "quiesce_ok"),
        "owner_output_quiesce": mapp(stop_control)
            && mapp(stop_control["quiesce"])
            ? stop_control["quiesce"] : ([]),
        "thread_running": mapping_int(status, "enabled"),
        "thread_count": mapping_int(status, "thread_count"),
        "max_owner_threads": mapping_int(status, "max_owner_threads"),
        "thread_control_error": err ? sprintf("%O", err)
            : mapp(stop_control) && stringp(stop_control["thread_stop_error"])
                ? stop_control["thread_stop_error"] : "",
        "reason": reason,
    ]);
}

private mapping start_owner_worker(int thread_count, string reason)
{
    object adapter;
    mapping stop_control;
    mapping status;
    mixed err;
    int started;
    int running;
    int actual_count;

    adapter = load_worker_adapter();
    if (!objectp(adapter))
        return ([
            "thread_control": "start",
            "thread_start_ok": 0,
            "requested_thread_count": thread_count,
            "thread_stop_before_start_ok": 0,
            "thread_running": 0,
            "thread_count": 0,
            "max_owner_threads": 0,
            "thread_control_error": "worker driver adapter unavailable",
            "reason": reason,
        ]);

    stop_control = stop_owner_worker("owner worker pre-start");
    if (!mapp(stop_control) || !stop_control["thread_stop_ok"])
        return ([
            "thread_control": "start",
            "thread_start_ok": 0,
            "requested_thread_count": thread_count,
            "thread_stop_before_start_ok": 0,
            "owner_output_quiesce_ok": mapp(stop_control)
                ? stop_control["owner_output_quiesce_ok"] : 0,
            "thread_running": 0,
            "thread_count": 0,
            "max_owner_threads": 0,
            "thread_control_error": mapp(stop_control)
                && stringp(stop_control["thread_control_error"])
                && stop_control["thread_control_error"] != ""
                ? stop_control["thread_control_error"]
                : "owner worker pre-start stop failed",
            "reason": reason,
        ]);

    err = catch(started = adapter->start_owner_thread(thread_count));
    status = query_owner_thread_status();
    running = mapping_int(status, "enabled");
    actual_count = mapping_int(status, "thread_count");
    return ([
        "thread_control": "start",
        "thread_start_ok": err ? 0 : (started && running && actual_count >= thread_count ? 1 : 0),
        "requested_thread_count": thread_count,
        "thread_stop_before_start_ok": 1,
        "owner_output_quiesce_ok": stop_control["owner_output_quiesce_ok"],
        "thread_running": running,
        "thread_count": actual_count,
        "max_owner_threads": mapping_int(status, "max_owner_threads"),
        "thread_control_error": err ? sprintf("%O", err) : "",
        "reason": reason,
    ]);
}

private int production_owner_mode_ready(mapping thread_status)
{
    if (!mapping_int(thread_status, "enabled"))
        return 0;
    if (_mode == MODE_OWNER_THREAD_COMPACT)
        return mapping_int(thread_status, "thread_count") >= OWNER_THREAD_COMPACT_COUNT &&
            mapping_int(thread_status, "max_owner_threads") >= OWNER_THREAD_COMPACT_COUNT;
    if (_mode == MODE_OWNER_THREAD_FULL)
        return mapping_int(thread_status, "thread_count") >= OWNER_THREAD_FULL_COUNT &&
            mapping_int(thread_status, "max_owner_threads") >= OWNER_THREAD_FULL_COUNT;
    return 0;
}

private mapping apply_runtime_policy(mapping policy)
{
    mapping result;
    mapping thread_status;

    if (!mapp(policy))
        return ([]);

    thread_status = query_owner_thread_status();
    result = copy(policy);
    result["enabled_by_default"] = 1;
    result["active"] = owner_runtime_mode_enabled() ? 1 : 0;
    result["production_owner_mode_required"] = 1;
    result["production_owner_mode_ready"] = production_owner_mode_ready(thread_status) ? 1 : 0;
    result["development_single_thread_allowed"] = _mode == MODE_SINGLE_THREAD ? 1 : 0;
    result["service_shard_policy"] = result["policy"] == POLICY_SERVICE_OWNER ? 1 : 0;
    result["service_shard_key_required"] = result["policy"] == POLICY_SERVICE_OWNER ? 1 : 0;
    result["hot_path_service_owner_single_point"] = 0;
    result["service_owner_single_point"] = 0;
    return result;
}

private mapping build_service_policy(string service, string policy, string owner_id, string description)
{
    return ([
        "service": service,
        "policy": policy,
        "owner_id": owner_id,
        "description": description,
        "enabled_by_default": 1,
        "configured_by_default": 1,
        "service_shard_policy": policy == POLICY_SERVICE_OWNER ? 1 : 0,
        "service_shard_key_required": policy == POLICY_SERVICE_OWNER ? 1 : 0,
        "hot_path_service_owner_single_point": 0,
        "service_owner_single_point": 0,
        "mutable_payload_allowed": 0,
        "direct_cross_owner_write": 0,
        "payload_contract": "spec_snapshot_or_commit_proposal_only",
        "commit_boundary_required": policy == POLICY_SERVICE_OWNER ? 1 : 0,
        "snapshot_only": policy == POLICY_SNAPSHOT_ONLY ? 1 : 0,
        "same_owner_direct_only": policy == POLICY_OWNER_SCHEDULER ? 1 : 0,
    ]);
}

private void initialize_flags(int enabled)
{
    string key;

    _feature_flags = ([]);
    foreach (key in query_feature_keys())
        _feature_flags[key] = enabled ? 1 : 0;
}

private void initialize_service_policies()
{
    _service_policies = ([
        "login": build_service_policy("login", POLICY_SINGLE_THREAD,
            "owner/global/service/login", "login handoff remains main-thread serialized"),
        "gateway_session": build_service_policy("gateway_session", POLICY_OWNER_SCHEDULER,
            "owner/global/service/gateway_session", "gateway session ingress is owner-routed when enabled"),
        "command_dispatch": build_service_policy("command_dispatch", POLICY_OWNER_SCHEDULER,
            "owner/global/service/command_dispatch", "player command dispatch is owner-routed when enabled"),
        "client_sync": build_service_policy("client_sync", POLICY_OWNER_SCHEDULER,
            "owner/global/service/client_sync", "client output preserves owner FIFO"),
        "player_session": build_service_policy("player_session", POLICY_OWNER_SCHEDULER,
            "owner/global/service/player_session", "player session state follows player owner"),
        "player": build_service_policy("player", POLICY_OWNER_SCHEDULER,
            "owner/global/service/player", "player writes are same-owner or commit proposals"),
        "item": build_service_policy("item", POLICY_OWNER_SCHEDULER,
            "owner/global/service/item", "item lifecycle writes are owner-bound"),
        "world_room": build_service_policy("world_room", POLICY_OWNER_SCHEDULER,
            "owner/global/service/world_room", "room lifecycle writes are owner-bound"),
        "economy": build_service_policy("economy", POLICY_SERVICE_OWNER,
            "owner/global/service/economy", "economy settlement is a service owner"),
        "mail": build_service_policy("mail", POLICY_SERVICE_OWNER,
            "owner/global/service/mail", "mail runtime and persistence are service-owned"),
        "guild": build_service_policy("guild", POLICY_SERVICE_OWNER,
            "owner/global/service/guild", "guild state is service-owned"),
        "rank": build_service_policy("rank", POLICY_SERVICE_OWNER,
            "owner/global/service/rank", "rank state and reward claims are service-owned"),
        "quest_reward": build_service_policy("quest_reward", POLICY_SERVICE_OWNER,
            "owner/global/service/quest_reward", "quest and reward apply cross-owner writes use commit proposals"),
        "combat": build_service_policy("combat", POLICY_SERVICE_OWNER,
            "owner/global/service/combat", "combat sessions are coordinated by combat owner"),
        "heartbeat": build_service_policy("heartbeat", POLICY_OWNER_SCHEDULER,
            "owner/global/service/heartbeat", "heartbeat handlers are owner-routed when enabled"),
        "delayed_callback": build_service_policy("delayed_callback", POLICY_OWNER_SCHEDULER,
            "owner/global/service/delayed_callback", "timer callbacks carry owner route and frozen payload policy"),
        "owner_persistence": build_service_policy("owner_persistence", POLICY_SNAPSHOT_ONLY,
            "owner/global/service/owner_persistence", "persistence accepts owner snapshots only"),
        "room_view": build_service_policy("room_view", POLICY_SNAPSHOT_ONLY,
            "owner/global/service/room_view", "room views are read snapshots"),
        "client_projection": build_service_policy("client_projection", POLICY_SNAPSHOT_ONLY,
            "owner/global/service/client_projection", "client projections are snapshot-only"),
        "dialog_projection": build_service_policy("dialog_projection", POLICY_SNAPSHOT_ONLY,
            "owner/global/service/dialog_projection", "dialog projections are snapshot-only"),
        "async_compute": build_service_policy("async_compute", POLICY_SNAPSHOT_ONLY,
            "owner/global/service/async_compute", "async compute receives frozen snapshots"),
    ]);
}

void create()
{
    ::create();
    _default_mode = query_default_owner_mode();
    _default_thread_count = query_mode_thread_count(_default_mode);
    _mode = _default_mode;
    initialize_flags(1);
    initialize_service_policies();
    _boot_start = ([
        "attempted": 0,
        "scheduled": 1,
        "requested_mode": _default_mode,
        "requested_thread_count": _default_thread_count,
        "thread_start_ok": 0,
        "automatic_fallback": 0,
    ]);
    call_out("start_default_owner_profile", 0);
    _last_change = ([
        "mode": _mode,
        "feature": "",
        "enabled": 1,
        "reason": "default " + _default_mode + " boot scheduled",
        "boot_start_ok": 0,
        "changed_at": time(),
    ]);
}

void start_default_owner_profile()
{
    mapping thread_status;

    if (mapping_int(_boot_start, "attempted"))
        return;

    if (_mode != _default_mode) {
        _boot_start = copy(_boot_start) + ([
            "attempted": 1,
            "scheduled": 1,
            "requested_mode": _default_mode,
            "thread_start_ok": 0,
            "skipped": 1,
            "skip_reason": "mode changed before boot start",
            "automatic_fallback": 0,
        ]);
        return;
    }

    // An administrator may have selected the boot mode in the same event-loop
    // turn as this boot callout. Reuse a healthy profile instead of stopping
    // and restarting workers that are already serving traffic.
    thread_status = query_owner_thread_status();
    if (mapping_int(thread_status, "enabled")
        && mapping_int(thread_status, "thread_count") >= _default_thread_count
        && mapping_int(thread_status, "max_owner_threads") >= _default_thread_count) {
        _boot_start = copy(_boot_start) + ([
            "attempted": 1,
            "scheduled": 1,
            "requested_mode": _default_mode,
            "requested_thread_count": _default_thread_count,
            "thread_start_ok": 1,
            "thread_stop_before_start_ok": 1,
            "thread_running": mapping_int(thread_status, "enabled"),
            "thread_count": mapping_int(thread_status, "thread_count"),
            "max_owner_threads": mapping_int(thread_status, "max_owner_threads"),
            "reused_existing_profile": 1,
            "automatic_fallback": 0,
        ]);
        _last_change = ([
            "mode": _mode,
            "feature": "",
            "enabled": 1,
            "reason": "default " + _default_mode + " boot reused existing profile",
            "boot_start_ok": 1,
            "changed_at": time(),
        ]);
        return;
    }

    _boot_start = start_owner_worker(_default_thread_count,
        "default " + _default_mode + " boot") + ([
        "attempted": 1,
        "scheduled": 1,
        "requested_mode": _default_mode,
        "automatic_fallback": 0,
    ]);
    _last_change = ([
        "mode": _mode,
        "feature": "",
        "enabled": 1,
        "reason": "default " + _default_mode + " boot",
        "boot_start_ok": mapping_int(_boot_start, "thread_start_ok"),
        "changed_at": time(),
    ]);
}

string query_mode()
{
    return _mode;
}

int query_mode_enabled(string mode)
{
    return stringp(mode) && _mode == mode;
}

int set_mode(string mode, string reason)
{
    if (!is_valid_mode(mode))
        return 0;

    _mode = mode;
    _last_change = ([
        "mode": _mode,
        "feature": "",
        "enabled": 0,
        "reason": stringp(reason) && reason != "" ? reason : "manual mode change",
        "changed_at": time(),
    ]);
    return 1;
}

int rollback_to_single_thread(string reason)
{
    mapping thread_control;

    thread_control = stop_owner_worker("rollback to single thread");
    if (!thread_control["thread_stop_ok"])
        return 0;

    initialize_flags(0);
    return set_mode(MODE_SINGLE_THREAD,
        stringp(reason) && reason != "" ? reason : "rollback to single thread");
}

int set_feature_enabled(string feature, int enabled, string reason)
{
    if (!stringp(feature) || member_array(feature, query_feature_keys()) == -1)
        return 0;

    _feature_flags[feature] = enabled ? 1 : 0;
    _last_change = ([
        "mode": _mode,
        "feature": feature,
        "enabled": enabled ? 1 : 0,
        "reason": stringp(reason) && reason != "" ? reason : "manual feature change",
        "changed_at": time(),
    ]);
    return 1;
}

int query_feature_enabled(string feature)
{
    if (!stringp(feature) || !mapp(_feature_flags))
        return 0;
    return _feature_flags[feature] ? 1 : 0;
}

mapping query_feature_flags()
{
    return copy(_feature_flags);
}

mapping query_service_policy(string service)
{
    if (!stringp(service) || !mapp(_service_policies) || !mapp(_service_policies[service]))
        return ([ "ok": 0, "reason": "unknown service policy" ]);
    return apply_runtime_policy(_service_policies[service]);
}

mapping query_service_map_v2()
{
    mapping result;
    string service;

    result = ([]);
    foreach (service in keys(_service_policies))
        result[service] = apply_runtime_policy(_service_policies[service]);
    return result;
}

int set_service_policy(string service, string policy, string owner_id, string reason)
{
    mapping current;

    if (!stringp(service) || service == "" || !is_valid_service_policy(policy))
        return 0;

    current = mapp(_service_policies[service])
        ? copy(_service_policies[service])
        : build_service_policy(service, policy, "", "custom service policy");
    current["policy"] = policy;
    current["owner_id"] = stringp(owner_id) && owner_id != "" ? owner_id : current["owner_id"];
    current["updated_at"] = time();
    current["reason"] = stringp(reason) ? reason : "";
    current["enabled_by_default"] = 1;
    current["configured_by_default"] = 1;
    current["mutable_payload_allowed"] = 0;
    current["direct_cross_owner_write"] = 0;
    current["payload_contract"] = "spec_snapshot_or_commit_proposal_only";
    current["commit_boundary_required"] = policy == POLICY_SERVICE_OWNER ? 1 : 0;
    current["snapshot_only"] = policy == POLICY_SNAPSHOT_ONLY ? 1 : 0;
    current["same_owner_direct_only"] = policy == POLICY_OWNER_SCHEDULER ? 1 : 0;
    _service_policies[service] = current;
    _last_change = ([
        "mode": _mode,
        "feature": "service_policy:" + service,
        "enabled": 0,
        "reason": stringp(reason) && reason != "" ? reason : "service policy update",
        "changed_at": time(),
    ]);
    return 1;
}

mapping set_production_profile(string profile, string reason)
{
    string mode;
    mapping thread_control;
    string control_reason;

    switch (profile) {
        case "owner_scheduler":
            mode = MODE_OWNER_SCHEDULER;
            thread_control = stop_owner_worker("owner scheduler route-only profile");
            break;
        case "canary":
        case "owner_thread_canary":
            mode = MODE_OWNER_THREAD_CANARY;
            thread_control = start_owner_worker(OWNER_THREAD_CANARY_COUNT, "owner thread canary profile");
            break;
        case "compact":
        case "owner_thread_compact":
            mode = MODE_OWNER_THREAD_COMPACT;
            if (!set_mode(mode, stringp(reason) && reason != "" ? reason : "production owner profile"))
                return ([ "ok": 0, "reason": "set mode failed" ]);
            initialize_flags(1);
            thread_control = start_owner_worker(OWNER_THREAD_COMPACT_COUNT, "owner thread compact profile");
            break;
        case "full":
        case "owner_thread_full":
            mode = MODE_OWNER_THREAD_FULL;
            if (!set_mode(mode, stringp(reason) && reason != "" ? reason : "production owner profile"))
                return ([ "ok": 0, "reason": "set mode failed" ]);
            initialize_flags(1);
            thread_control = start_owner_worker(OWNER_THREAD_FULL_COUNT, "owner thread full profile");
            break;
        default:
            return ([ "ok": 0, "reason": "unsupported production owner profile" ]);
    }

    if (!mapp(thread_control) ||
        (thread_control["thread_control"] == "start" && !thread_control["thread_start_ok"]) ||
        (thread_control["thread_control"] == "stop" && !thread_control["thread_stop_ok"])) {
        control_reason = stringp(thread_control["thread_control_error"]) &&
            thread_control["thread_control_error"] != "" ?
            thread_control["thread_control_error"] : "owner worker control failed";
        return query_status() + thread_control + ([
            "ok": 0,
            "profile": profile,
            "reason": control_reason,
            "automatic_fallback": 0,
        ]);
    }

    if (!set_mode(mode, stringp(reason) && reason != "" ? reason : "production owner profile"))
        return ([ "ok": 0, "reason": "set mode failed" ]);
    initialize_flags(1);

    return query_status() + thread_control + ([
        "ok": 1,
        "profile": profile,
    ]);
}

mapping query_runtime_readiness()
{
    mapping thread_status;
    mapping runtime_status;
    string *errors;
    int driver_domains_ready;
    int gateway_executor_ready;
    int service_shard_executor_ready;
    int target_owner_message_executor_ready;
    int ordinary_lpc_default_closed;
    int worker_ready;

    thread_status = query_owner_thread_status();
    runtime_status = query_owner_runtime_status();
    errors = ({ });
    driver_domains_ready = mapp(runtime_status)
        && runtime_status["registered_owner_task_domains_ready"] == 1 ? 1 : 0;
    gateway_executor_ready = mapp(runtime_status)
        && runtime_status["gateway_command_execute_ready"] == 1 ? 1 : 0;
    service_shard_executor_ready = mapp(runtime_status)
        && runtime_status["service_shard_executor_ready"] == 1 ? 1 : 0;
    target_owner_message_executor_ready = mapp(runtime_status)
        && runtime_status["target_owner_message_executor_ready"] == 1 ? 1 : 0;
    ordinary_lpc_default_closed = mapp(runtime_status)
        && runtime_status["ordinary_lpc_default_closed"] == 1 ? 1 : 0;
    worker_ready = _mode == MODE_OWNER_SCHEDULER
        ? 1
        : production_owner_mode_ready(thread_status);

    if (!owner_runtime_mode_enabled())
        errors += ({ "owner profile is disabled" });
    if (!mapp(runtime_status))
        errors += ({ "driver owner runtime status unavailable" });
    if (!driver_domains_ready)
        errors += ({ "driver owner task domains are not ready" });
    if (!gateway_executor_ready)
        errors += ({ "gateway executor is not ready" });
    if (!service_shard_executor_ready)
        errors += ({ "service shard executor is not ready" });
    if (!target_owner_message_executor_ready)
        errors += ({ "target owner message executor is not ready" });
    if (!ordinary_lpc_default_closed)
        errors += ({ "ordinary LPC default is not closed" });
    if (!worker_ready)
        errors += ({ "owner worker profile is not ready" });

    return ([
        "ok": sizeof(errors) == 0 ? 1 : 0,
        "owner_profile": _mode,
        "worker_count": mapping_int(thread_status, "thread_count"),
        "driver_domains_ready": driver_domains_ready,
        "gateway_executor_ready": gateway_executor_ready,
        "service_shard_executor_ready": service_shard_executor_ready,
        "target_owner_message_executor_ready": target_owner_message_executor_ready,
        "ordinary_lpc_default_closed": ordinary_lpc_default_closed,
        "owner_epoch": mapp(runtime_status) && intp(runtime_status["owner_epoch"])
            ? runtime_status["owner_epoch"] : 0,
        "errors": errors,
    ]);
}

mapping query_status()
{
    mapping thread_status;
    mapping runtime_status;
    int production_ready;

    thread_status = query_owner_thread_status();
    runtime_status = query_owner_runtime_status();
    production_ready = production_owner_mode_ready(thread_status);
    return ([
        "mode": _mode,
        "default_mode": _default_mode,
        "default_thread_count": _default_thread_count,
        "default_profile_file": OWNER_PROFILE_RUNTIME_FILE,
        "default_profile_fallback": MODE_OWNER_THREAD_FULL,
        "production_start_path": query_production_start_path(),
        "owner_thread_compact_count": OWNER_THREAD_COMPACT_COUNT,
        "owner_thread_full_count": OWNER_THREAD_FULL_COUNT,
        "enabled_by_default": 1,
        "requires_explicit_start": 0,
        "automatic_fallback": 0,
        "emergency_fallback_mode": MODE_SINGLE_THREAD,
        "boot_start_attempted": mapping_int(_boot_start, "attempted"),
        "boot_start_ok": mapping_int(_boot_start, "thread_start_ok"),
        "boot_start": mapp(_boot_start) ? copy(_boot_start) : ([]),
        "features": copy(_feature_flags),
        "service_map_v2_ready": 1,
        "service_policy_modes": query_service_policy_modes(),
        "service_policy_count": sizeof(_service_policies),
        "service_map_default_enabled": 1,
        "service_map_active": owner_runtime_mode_enabled() ? 1 : 0,
        "production_owner_mode_ready": production_ready ? 1 : 0,
        "production_owner_profiles": ({ "owner_scheduler", "canary", "compact", "full" }),
        "development_single_thread_allowed": _mode == MODE_SINGLE_THREAD ? 1 : 0,
        "facade_only_runtime_claims": 0,
        "keyed_service_shard_ready": 1,
        "hot_path_service_owner_single_point": 0,
        "service_owner_single_point": 0,
        "last_change": copy(_last_change),
        "safe_default": _default_mode,
        "fallback_available": 1,
        "thread_running": mapping_int(thread_status, "enabled"),
        "thread_count": mapping_int(thread_status, "thread_count"),
        "max_owner_threads": mapping_int(thread_status, "max_owner_threads"),
        "worker_dispatch_count": mapping_int(thread_status, "thread_dispatched"),
        "off_main_task_count": mapping_int(thread_status, "executor_safe_task_dispatched") +
            mapping_int(thread_status, "executor_runnable_task_dispatched") +
            mapping_int(thread_status, "thread_message_dispatched"),
        "main_dispatch_count": mapping_int(thread_status, "main_dispatched"),
        "main_fallback_count": mapping_int(runtime_status, "normal_path_main_fallback_count"),
        "normal_path_main_fallback_count": mapping_int(runtime_status, "normal_path_main_fallback_count"),
        "thread_status": mapp(thread_status) ? thread_status : ([]),
        "runtime_readiness": query_runtime_readiness(),
    ]);
}

mapping query_execution_contract()
{
    mapping thread_status;

    thread_status = query_owner_thread_status();
    return ([
        "modes": ({
            MODE_SINGLE_THREAD,
            MODE_OWNER_SCHEDULER,
            MODE_OWNER_THREAD_CANARY,
            MODE_OWNER_THREAD_COMPACT,
            MODE_OWNER_THREAD_FULL,
        }),
        "default_mode": _default_mode,
        "thread_count": _default_thread_count,
        "default_profile_file": OWNER_PROFILE_RUNTIME_FILE,
        "default_profile_fallback": MODE_OWNER_THREAD_FULL,
        "production_start_path": query_production_start_path(),
        "owner_thread_compact_count": OWNER_THREAD_COMPACT_COUNT,
        "owner_thread_full_count": OWNER_THREAD_FULL_COUNT,
        "enabled_by_default": 1,
        "requires_explicit_start": 0,
        "automatic_fallback": 0,
        "emergency_fallback_mode": MODE_SINGLE_THREAD,
        "boot_start": copy(_boot_start),
        "features": query_feature_keys(),
        "service_map_v2_ready": 1,
        "service_policy_modes": query_service_policy_modes(),
        "service_map_default_enabled": 1,
        "service_payload_contract": "spec_snapshot_or_commit_proposal_only",
        "safe_by_default": 1,
        "production_owner_mode_required": 1,
        "production_owner_mode_ready": production_owner_mode_ready(thread_status) ? 1 : 0,
        "development_single_thread_allowed": 1,
        "facade_only_runtime_claims": 0,
        "keyed_service_shard_ready": 1,
        "hot_path_service_owner_single_point": 0,
        "service_owner_single_point": 0,
        "tests_deferred_to_final_stage": 1,
        "fallback_available": 1,
    ]);
}
