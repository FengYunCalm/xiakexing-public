/**
 * @file    async_compute_d.c
 * @brief   统一多核异步计算服务
 *
 * 业务服务只提交 JSON-safe 快照和读取 result spec；真实写入必须回到
 * proposal/业务主线程提交链，不能在 worker 内直接修改 LPC 对象。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

#define DEFAULT_TIMEOUT_MS      3000
#define DEFAULT_TTL_MS          30000
#define ASYNC_CACHE_LIMIT       1024
#define ASYNC_CACHE_TTL         300
#define ASYNC_PRUNE_BUDGET      64

nosave mapping _task_ids;
nosave mapping _task_meta;
nosave mapping _task_states;
nosave mapping _task_errors;
nosave mapping _task_input_hashes;
nosave mapping _last_results;
nosave mapping _last_result_input_hashes;
nosave mapping _task_terminal_at;
nosave int _async_cache_evictions;
nosave int _submitted_count;
nosave int _completed_count;
nosave int _failed_count;

void create()
{
    ::create();
    _task_ids = ([]);
    _task_meta = ([]);
    _task_states = ([]);
    _task_errors = ([]);
    _task_input_hashes = ([]);
    _last_results = ([]);
    _last_result_input_hashes = ([]);
    _task_terminal_at = ([]);
    _async_cache_evictions = 0;
    _submitted_count = 0;
    _completed_count = 0;
    _failed_count = 0;
}

private void clear_task_key(string key)
{
    map_delete(_task_ids, key);
    map_delete(_task_meta, key);
    map_delete(_task_states, key);
    map_delete(_task_errors, key);
    map_delete(_task_input_hashes, key);
    map_delete(_last_results, key);
    map_delete(_last_result_input_hashes, key);
    map_delete(_task_terminal_at, key);
}

private int task_is_pending(string key)
{
    return (_task_ids[key] || 0) > 0;
}

private string validate_snapshot_contract(mapping snapshot)
{
    if (!mapp(snapshot))
        return "worker snapshot must be a mapping";
    if (WORKER_BUS_D->validate_worker_snapshot(snapshot))
        return "";
    return WORKER_BUS_D->query_last_error();
}

private int query_task_terminal_at(string key)
{
    if (intp(_task_terminal_at[key]) && _task_terminal_at[key] > 0)
        return _task_terminal_at[key];
    if (mapp(_task_meta[key]))
        return _task_meta[key]["completed_at"] || 0;
    return 0;
}

private void prune_async_task_cache()
{
    string key;
    string oldest_key;
    int now;
    int inspected;
    int terminal_at;
    int oldest_at;

    now = time();
    inspected = 0;
    foreach (key in keys(_task_states)) {
        if (inspected++ >= ASYNC_PRUNE_BUDGET)
            break;
        if (task_is_pending(key))
            continue;
        terminal_at = query_task_terminal_at(key);
        if (terminal_at > 0 && now - terminal_at > ASYNC_CACHE_TTL) {
            clear_task_key(key);
            _async_cache_evictions++;
        }
    }

    while (sizeof(keys(_task_states)) > ASYNC_CACHE_LIMIT) {
        oldest_key = "";
        oldest_at = now + 1;
        foreach (key in keys(_task_states)) {
            if (task_is_pending(key))
                continue;
            terminal_at = query_task_terminal_at(key);
            if (terminal_at <= 0)
                terminal_at = now;
            if (terminal_at < oldest_at) {
                oldest_key = key;
                oldest_at = terminal_at;
            }
        }
        if (oldest_key == "")
            break;
        clear_task_key(oldest_key);
        _async_cache_evictions++;
    }
}

private mixed sanitize_snapshot_value(mixed value)
{
    mixed key;
    mixed item;
    mixed *items;
    mapping cleaned;

    if (undefinedp(value) || objectp(value))
        return "";
    if (stringp(value) || intp(value) || floatp(value))
        return value;
    if (arrayp(value)) {
        items = ({ });
        foreach (item in value)
            items += ({ sanitize_snapshot_value(item) });
        return items;
    }
    if (mapp(value)) {
        cleaned = ([]);
        foreach (key, item in value) {
            if (stringp(key) && key != "")
                cleaned[key] = sanitize_snapshot_value(item);
        }
        return cleaned;
    }
    return "";
}

private int fold_input_hash(int hash, int value)
{
    value = value % 2147483647;
    if (value < 0)
        value = -value;
    hash = (hash * 33 + value + 1) % 2147483647;
    if (hash < 0)
        hash = -hash;
    return hash;
}

private int hash_string_value(int hash, string value)
{
    int i;
    int size;

    if (!stringp(value))
        return fold_input_hash(hash, 0);
    size = strlen(value);
    hash = fold_input_hash(hash, size);
    for (i = 0; i < size; i++)
        hash = fold_input_hash(hash, value[i]);
    return hash;
}

private int hash_snapshot_value(int hash, mixed value, int depth)
{
    mixed item;
    string key;
    string *keys_sorted;

    if (depth > 8)
        return fold_input_hash(hash, depth);
    if (undefinedp(value) || objectp(value))
        return fold_input_hash(hash, 0);
    if (intp(value))
        return fold_input_hash(fold_input_hash(hash, 1), value);
    if (floatp(value))
        return hash_string_value(fold_input_hash(hash, 2), sprintf("%O", value));
    if (stringp(value))
        return hash_string_value(fold_input_hash(hash, 3), value);
    if (arrayp(value)) {
        hash = fold_input_hash(hash, sizeof(value));
        foreach (item in value)
            hash = hash_snapshot_value(hash, item, depth + 1);
        return hash;
    }
    if (mapp(value)) {
        keys_sorted = sort_array(keys(value), 1);
        foreach (key in keys_sorted) {
            if (key == "generated_at")
                continue;
            hash = hash_string_value(fold_input_hash(hash, 4), key);
            hash = hash_snapshot_value(hash, value[key], depth + 1);
        }
        return hash;
    }
    return fold_input_hash(hash, 0);
}

private int query_safe_snapshot_input_hash(mapping safe_snapshot)
{
    int hash;

    hash = hash_snapshot_value(5381, mapp(safe_snapshot) ? safe_snapshot : ([]), 0);
    return hash > 0 ? hash : 1;
}

private int query_snapshot_input_hash(mapping snapshot)
{
    mixed safe_value;

    safe_value = sanitize_snapshot_value(mapp(snapshot) ? snapshot : ([]));
    return query_safe_snapshot_input_hash(mapp(safe_value) ? safe_value : ([]));
}

private mapping normalize_options(mapping options)
{
    mapping normalized;

    normalized = mapp(options) ? copy(options) : ([]);
    if (!intp(normalized["timeout_ms"]) || normalized["timeout_ms"] <= 0)
        normalized["timeout_ms"] = DEFAULT_TIMEOUT_MS;
    if (!intp(normalized["ttl_ms"]) || normalized["ttl_ms"] < 0)
        normalized["ttl_ms"] = DEFAULT_TTL_MS;
    return normalized;
}

private mapping decorate_response(string key, mapping response, string state)
{
    mapping result;
    int input_hash;

    response = mapp(response) ? copy(response) : ([]);
    result = mapp(response["result_spec"]) ? copy(response["result_spec"]) : ([]);
    input_hash = (state == "ready" || state == "ready_cached" || state == "failed_cached")
        ? (_last_result_input_hashes[key] || 0)
        : (_task_input_hashes[key] || 0);
    response["ok"] = response["success"] ? 1 : 0;
    response["async"] = 1;
    response["async_key"] = key;
    response["async_state"] = state;
    response["async_input_hash"] = input_hash;
    response["pending_task_id"] = _task_ids[key] || 0;
    response["submitted_at"] = mapp(_task_meta[key]) ? (_task_meta[key]["submitted_at"] || 0) : 0;
    response["completed_at"] = mapp(_task_meta[key]) ? (_task_meta[key]["completed_at"] || 0) : 0;
    if (sizeof(result) > 0) {
        result["async_key"] = key;
        result["async_state"] = state;
        result["async_input_hash"] = input_hash;
        result["pending_task_id"] = response["pending_task_id"];
        result["submitted_at"] = response["submitted_at"];
        result["completed_at"] = response["completed_at"];
        result["source"] = "async_compute_d";
        response["result_spec"] = result;
    }
    return response;
}

mapping submit_async_task(string key, string task, mapping snapshot, mapping options)
{
    mapping response;
    mapping safe_snapshot;
    mapping normalized;
    mixed safe_value;
    int input_hash;
    string snapshot_error;

    snapshot_error = validate_snapshot_contract(snapshot);
    if (snapshot_error != "")
        return (["success":0, "error":snapshot_error,
            "async":1, "async_state":"rejected"]);

    if (!stringp(key) || key == "")
        return (["success":0, "error":"async key is required"]);
    if (!stringp(task) || task == "")
        return (["success":0, "error":"async task is required"]);
    prune_async_task_cache();
    if ((_task_ids[key] || 0) > 0)
        return (["success":1, "status":"pending", "task_id":_task_ids[key]]);

    safe_value = sanitize_snapshot_value(mapp(snapshot) ? snapshot : ([]));
    safe_snapshot = mapp(safe_value) ? safe_value : ([]);
    input_hash = query_safe_snapshot_input_hash(safe_snapshot);
    normalized = normalize_options(options);
    response = WORKER_BUS_D->submit_driver_worker_task(task, safe_snapshot, normalized);
    if (mapp(response) && response["success"] && intp(response["task_id"])) {
        _task_ids[key] = response["task_id"];
        _task_input_hashes[key] = input_hash;
        _task_states[key] = mapp(_last_results[key]) ? "pending_cached" : "pending";
        _task_errors[key] = "";
        _task_meta[key] = ([
            "task": task,
            "owner_key": normalized["owner_key"] || "global",
            "input_hash": input_hash,
            "submitted_at": time(),
            "completed_at": 0,
        ]);
        map_delete(_task_terminal_at, key);
        _submitted_count++;
    } else {
        _task_errors[key] = mapp(response) ? (response["error"] || "async submit failed") : "async submit failed";
        _task_states[key] = mapp(_last_results[key]) ? "failed_cached" : "failed";
        _task_terminal_at[key] = time();
        _failed_count++;
    }
    return decorate_response(key, response, _task_states[key]);
}

mapping poll_async_task(string key)
{
    int task_id;
    int input_hash;
    mapping response;

    if (!stringp(key) || key == "")
        return (["success":0, "error":"async key is required"]);
    prune_async_task_cache();
    task_id = _task_ids[key] || 0;
    if (task_id <= 0) {
        if (mapp(_last_results[key]))
            return decorate_response(key, _last_results[key], _task_states[key] == "failed_cached" ? "failed_cached" : "ready_cached");
        return (["success":0, "status":"empty", "async":1, "async_key":key]);
    }

    response = WORKER_BUS_D->poll_driver_worker_task(task_id);
    if (mapp(response) && response["status"] == "pending") {
        _task_states[key] = mapp(_last_results[key]) ? "pending_cached" : "pending";
        return decorate_response(key, response, _task_states[key]);
    }
    if (mapp(response) && response["success"] && mapp(response["result_spec"])) {
        input_hash = _task_input_hashes[key] || 0;
        _task_ids[key] = 0;
        _task_states[key] = "ready";
        _task_errors[key] = "";
        if (mapp(_task_meta[key]))
            _task_meta[key]["completed_at"] = time();
        _task_terminal_at[key] = time();
        _last_result_input_hashes[key] = input_hash;
        _last_results[key] = decorate_response(key, response, "ready");
        _completed_count++;
        return _last_results[key];
    }

    _task_ids[key] = 0;
    _task_errors[key] = mapp(response) ? (response["error"] || "async worker failed") : "async worker failed";
    _task_states[key] = mapp(_last_results[key]) ? "failed_cached" : "failed";
    _task_terminal_at[key] = time();
    _failed_count++;
    if (mapp(_last_results[key]))
        return decorate_response(key, _last_results[key], _task_states[key]);
    return decorate_response(key, response, _task_states[key]);
}

/* Token readers still advance an active task, but cached reads avoid cloning
 * the full result spec when only lifecycle metadata is required. */
mapping poll_async_task_token(string key)
{
    mapping response;
    string state;
    int task_id;

    if (!stringp(key) || key == "")
        return (["success":0, "status":"empty", "async_state":"empty"]);
    task_id = _task_ids[key] || 0;
    if (task_id > 0) {
        response = poll_async_task(key);
        state = mapp(response) && stringp(response["async_state"])
            && response["async_state"] != ""
                ? response["async_state"]
                : mapp(response) && stringp(response["status"])
                    && response["status"] != ""
                        ? response["status"] : "empty";
        return ([
            "success": mapp(response) && response["success"] ? 1 : 0,
            "status": state,
            "async_state": state,
            "async_input_hash": mapp(response)
                && intp(response["async_input_hash"])
                    ? response["async_input_hash"] : 0,
            "completed_at": mapp(response) && intp(response["completed_at"])
                ? response["completed_at"] : 0,
        ]);
    }
    if (mapp(_last_results[key])) {
        state = _task_states[key] == "failed_cached"
            ? "failed_cached" : "ready_cached";
        return ([
            "success": 1,
            "status": state,
            "async_state": state,
            "async_input_hash": _last_result_input_hashes[key] || 0,
            "completed_at": mapp(_task_meta[key])
                && intp(_task_meta[key]["completed_at"])
                    ? _task_meta[key]["completed_at"] : 0,
        ]);
    }
    return ([
        "success": 0,
        "status": "empty",
        "async_state": "empty",
        "async_input_hash": 0,
        "completed_at": 0,
    ]);
}

mapping query_or_submit(string key, string task, mapping snapshot, mapping options)
{
    mapping response;
    mapping submitted;
    int cached_hash;
    int input_hash;
    int task_hash;
    string snapshot_error;

    snapshot_error = validate_snapshot_contract(snapshot);
    if (snapshot_error != "")
        return (["success":0, "error":snapshot_error,
            "async":1, "async_state":"rejected"]);

    prune_async_task_cache();
    input_hash = query_snapshot_input_hash(snapshot);
    response = poll_async_task(key);
    task_hash = _task_input_hashes[key] || 0;
    cached_hash = _last_result_input_hashes[key] || 0;
    if (mapp(response) && (response["async_state"] == "pending" || response["async_state"] == "pending_cached")) {
        if (task_hash == input_hash)
            return response;
        _task_ids[key] = 0;
        _task_states[key] = "stale";
        _task_errors[key] = "async task input hash is stale";
        _task_terminal_at[key] = time();
        if (cached_hash == input_hash && mapp(_last_results[key]))
            return decorate_response(key, _last_results[key], "ready_cached");
    }
    if (mapp(response) && response["success"] && mapp(response["result_spec"])
        && intp(response["async_input_hash"]) && response["async_input_hash"] == input_hash)
        return response;
    submitted = submit_async_task(key, task, snapshot, options);
    if (!mapp(submitted) || !submitted["success"]) {
        if (cached_hash == input_hash && mapp(_last_results[key]))
            return decorate_response(key, _last_results[key], "ready_cached");
        return submitted;
    }
    return poll_async_task(key);
}

mapping query_last_result(string key)
{
    prune_async_task_cache();
    if (!stringp(key) || key == "")
        return (["success":0, "error":"async key is required"]);
    if (mapp(_last_results[key]))
        return decorate_response(key, _last_results[key], _task_states[key] == "failed_cached" ? "failed_cached" : "ready_cached");
    return (["success":0, "status":"empty", "async":1, "async_key":key]);
}

mapping submit_async_batch(mapping *items)
{
    mapping item;
    mapping response;
    mapping *tasks;
    mixed safe_value;

    prune_async_task_cache();
    if (!arrayp(items) || sizeof(items) <= 0)
        return (["success":0, "error":"async batch items are required"]);
    tasks = ({ });
    foreach (item in items) {
        if (!mapp(item) || !stringp(item["key"]) || item["key"] == "")
            continue;
        safe_value = sanitize_snapshot_value(item["snapshot"] || ([]));
        tasks += ({ ([
            "task": item["task"],
            "snapshot": mapp(safe_value) ? safe_value : ([]),
            "options": normalize_options(item["options"]),
        ]) });
    }
    response = WORKER_BUS_D->submit_driver_worker_batch(tasks);
    if (mapp(response) && arrayp(response["results"]))
        _submitted_count += response["accepted"] || 0;
    return response;
}

mapping query_async_compute_status()
{
    int active_count;
    string key;

    prune_async_task_cache();
    active_count = 0;
    foreach (key in keys(_task_states)) {
        if (task_is_pending(key))
            active_count++;
    }
    return ([
        "submitted": _submitted_count,
        "completed": _completed_count,
        "failed": _failed_count,
        "tracked": sizeof(keys(_task_states)),
        "active": active_count,
        "cached": sizeof(keys(_last_results)),
        "cache_limit": ASYNC_CACHE_LIMIT,
        "cache_ttl_seconds": ASYNC_CACHE_TTL,
        "cache_evictions": _async_cache_evictions,
        "driver": WORKER_BUS_D->query_driver_worker_status(),
    ]);
}
