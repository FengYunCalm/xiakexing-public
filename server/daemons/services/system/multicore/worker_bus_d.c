/**
 * @file    worker_bus_d.c
 * @brief   外围 worker 任务门面服务
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

#define DRIVER_WORKER_ADAPTER "/daemons/services/system/multicore/worker_driver_adapter"

nosave string _last_error_message;

private string validate_snapshot_value(mixed value, int depth)
{
    mixed key;
    mixed item;
    string err;

    if (depth > 8)
        return "worker snapshot nesting is too deep";
    if (objectp(value))
        return "worker snapshot must not contain object references";
    if (undefinedp(value))
        return "worker snapshot must not contain undefined values";
    if (stringp(value) || intp(value) || floatp(value))
        return "";

    if (arrayp(value)) {
        foreach (item in value) {
            err = validate_snapshot_value(item, depth + 1);
            if (err != "")
                return err;
        }
        return "";
    }

    if (mapp(value)) {
        foreach (key, item in value) {
            if (!stringp(key))
                return "worker snapshot mapping keys must be strings";
            err = validate_snapshot_value(item, depth + 1);
            if (err != "")
                return err;
        }
        return "";
    }

    return "worker snapshot contains unsupported value type";
}

private int reject_task(string message)
{
    _last_error_message = message;
    return 0;
}

private int record_invalid_result(string message)
{
    _last_error_message = message;
    return 0;
}

void create()
{
    ::create();
    _last_error_message = "";
}

int health_check()
{
    return 1;
}

string query_last_error()
{
    return _last_error_message || "";
}

int validate_worker_snapshot(mapping snapshot)
{
    if (!mapp(snapshot))
        return reject_task("worker snapshot must be a mapping");

    _last_error_message = validate_snapshot_value(snapshot, 0);
    if (_last_error_message != "")
        return reject_task(_last_error_message);

    return 1;
}

int validate_worker_options(mapping options)
{
    if (!mapp(options))
        return 1;

    _last_error_message = validate_snapshot_value(options, 0);
    if (_last_error_message != "")
        return reject_task(_last_error_message);

    return 1;
}

int validate_worker_result_spec(mapping response)
{
    mapping result_spec;

    if (!mapp(response))
        return record_invalid_result("worker response must be a mapping");
    if (!intp(response["success"]))
        return record_invalid_result("worker response must include success flag");
    if (!response["success"]) {
        _last_error_message = "";
        return 1;
    }

    result_spec = response["result_spec"];
    if (!mapp(result_spec))
        return record_invalid_result("worker success response must include result_spec mapping");
    if (!stringp(result_spec["type"]) || result_spec["type"] == "")
        return record_invalid_result("worker result_spec type is required");

    _last_error_message = validate_snapshot_value(result_spec, 0);
    if (_last_error_message != "")
        return record_invalid_result(_last_error_message);

    _last_error_message = "";
    return 1;
}

private mapping normalize_driver_result_spec(string task, mapping response)
{
    mapping result_spec;

    if (task != "combat_damage" || !mapp(response) || !response["success"])
        return response;
    result_spec = response["result_spec"];
    if (!mapp(result_spec) || !intp(result_spec["damage"]) || !intp(result_spec["input_hash"]))
        return response;
    return ([
        "success": 1,
        "result_spec": ([
            "type": "combat_damage",
            "damage": result_spec["damage"],
            "input_hash": result_spec["input_hash"],
            "snapshot_hash": intp(result_spec["snapshot_hash"]) ? result_spec["snapshot_hash"] : 0,
        ]),
    ]);
}

private object query_driver_worker_adapter()
{
    object adapter;

    adapter = find_object(DRIVER_WORKER_ADAPTER);
    if (objectp(adapter))
        return adapter;
    if (catch(adapter = load_object(DRIVER_WORKER_ADAPTER))) {
        _last_error_message = "driver worker efun is unavailable";
        return 0;
    }
    return adapter;
}

mapping run_driver_worker_task(string task, mapping snapshot, mapping options)
{
    object adapter;
    mapping response;

    if (!stringp(task) || task == "") {
        reject_task("worker task name is required");
        return (["success":0, "error":_last_error_message]);
    }
    if (!validate_worker_snapshot(snapshot))
        return (["success":0, "error":_last_error_message]);
    if (!validate_worker_options(options))
        return (["success":0, "error":_last_error_message]);

    adapter = query_driver_worker_adapter();
    if (!objectp(adapter))
        return (["success":0, "error":_last_error_message]);

    response = adapter->run_task(task, snapshot, mapp(options) ? options : ([]));
    response = normalize_driver_result_spec(task, response);
    if (!validate_worker_result_spec(response))
        return (["success":0, "error":sprintf("%s: response=%O", _last_error_message, response)]);

    _last_error_message = response["success"] ? "" : (response["error"] || "worker task failed");
    return response;
}

mapping submit_driver_worker_task(string task, mapping snapshot, mapping options)
{
    object adapter;
    mapping response;

    if (!stringp(task) || task == "") {
        reject_task("worker task name is required");
        return (["success":0, "error":_last_error_message]);
    }
    if (!validate_worker_snapshot(snapshot))
        return (["success":0, "error":_last_error_message]);
    if (!validate_worker_options(options))
        return (["success":0, "error":_last_error_message]);

    adapter = query_driver_worker_adapter();
    if (!objectp(adapter))
        return (["success":0, "error":_last_error_message]);

    response = adapter->submit_task(task, snapshot, mapp(options) ? options : ([]));
    if (!mapp(response) || !response["success"] || !intp(response["task_id"])) {
        record_invalid_result("driver worker submit response is invalid");
        return (["success":0, "error":_last_error_message]);
    }

    _last_error_message = "";
    return response;
}

mapping submit_driver_worker_batch(mixed *tasks)
{
    object adapter;
    mixed item;
    mapping item_map;
    mapping response;

    if (!arrayp(tasks) || sizeof(tasks) <= 0) {
        reject_task("worker batch tasks must be a non-empty array");
        return (["success":0, "error":_last_error_message]);
    }

    foreach (item in tasks) {
        if (!mapp(item)) {
            reject_task("worker batch item must be a mapping");
            return (["success":0, "error":_last_error_message]);
        }
        item_map = item;
        if (!stringp(item_map["task"]) || item_map["task"] == "") {
            reject_task("worker batch item task is required");
            return (["success":0, "error":_last_error_message]);
        }
        if (!validate_worker_snapshot(item_map["snapshot"]))
            return (["success":0, "error":_last_error_message]);
        if (!validate_worker_options(item_map["options"]))
            return (["success":0, "error":_last_error_message]);
    }

    adapter = query_driver_worker_adapter();
    if (!objectp(adapter))
        return (["success":0, "error":_last_error_message]);

    response = adapter->submit_batch(tasks);
    if (!mapp(response) || !arrayp(response["results"])) {
        record_invalid_result("driver worker batch submit response is invalid");
        return (["success":0, "error":_last_error_message]);
    }

    _last_error_message = response["success"] ? "" : "driver worker batch submit rejected some tasks";
    return response;
}

mapping poll_driver_worker_task(int task_id)
{
    object adapter;
    mapping response;

    if (task_id <= 0) {
        reject_task("worker task_id must be positive");
        return (["success":0, "error":_last_error_message]);
    }

    adapter = query_driver_worker_adapter();
    if (!objectp(adapter))
        return (["success":0, "error":_last_error_message]);

    response = adapter->poll_task(task_id);
    if (!validate_worker_result_spec(response))
        return (["success":0, "error":_last_error_message]);

    _last_error_message = response["success"] ? "" : (response["error"] || "worker task is not ready");
    return response;
}

mapping poll_driver_worker_batch(int *task_ids)
{
    object adapter;
    mixed item;
    mapping response;

    if (!arrayp(task_ids) || sizeof(task_ids) <= 0) {
        reject_task("worker poll batch task_ids must be a non-empty array");
        return (["success":0, "error":_last_error_message]);
    }
    foreach (item in task_ids) {
        if (!intp(item) || item <= 0) {
            reject_task("worker poll batch task_id must be positive");
            return (["success":0, "error":_last_error_message]);
        }
    }

    adapter = query_driver_worker_adapter();
    if (!objectp(adapter))
        return (["success":0, "error":_last_error_message]);

    response = adapter->poll_batch(task_ids);
    if (!mapp(response) || !arrayp(response["results"])) {
        record_invalid_result("driver worker batch poll response is invalid");
        return (["success":0, "error":_last_error_message]);
    }

    _last_error_message = response["success"] ? "" : "driver worker batch poll has failed results";
    return response;
}

mapping query_driver_worker_status()
{
    object adapter;
    mapping response;

    adapter = query_driver_worker_adapter();
    if (!objectp(adapter))
        return (["success":0, "error":_last_error_message]);

    response = adapter->query_worker_status();
    if (!mapp(response))
        return (["success":0, "error":"driver worker status response is invalid"]);
    return response;
}
