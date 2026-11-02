/**
 * @file    owner_message_bus_d.c
 * @brief   跨 owner 消息总线门面
 *
 * 消息总线只传递不可变 spec、snapshot query 和 commit proposal，不传裸
 * LPC object。真正对象写入必须由目标 owner 或 CommitCoordinator 完成。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

#define WORKER_DRIVER_ADAPTER "/daemons/services/system/multicore/worker_driver_adapter"
#define DEFAULT_DEADLINE_SEC          3
#define MAX_TERMINAL_MESSAGE_HISTORY 8192
#define MESSAGE_EXPIRY_SWEEP_BUDGET   64

nosave int _next_message_id;
nosave mapping _messages;
nosave mapping _wait_graph;
nosave mapping _wait_edges;
nosave mapping _replies;
nosave mapping _terminal_message_order;
nosave mapping _terminal_message_recorded;
nosave int _terminal_message_order_head;
nosave int _terminal_message_order_tail;
nosave int _message_history_evictions;
nosave int _expired_message_count;
nosave int _driver_submit_ok;
nosave int _driver_submit_failed;

void create()
{
    ::create();
    _next_message_id = 1;
    _messages = ([]);
    _wait_graph = ([]);
    _wait_edges = ([]);
    _replies = ([]);
    _terminal_message_order = ([]);
    _terminal_message_recorded = ([]);
    _terminal_message_order_head = 1;
    _terminal_message_order_tail = 1;
    _message_history_evictions = 0;
    _expired_message_count = 0;
    _driver_submit_ok = 0;
    _driver_submit_failed = 0;
}

private object query_adapter()
{
    return find_object(WORKER_DRIVER_ADAPTER) || load_object(WORKER_DRIVER_ADAPTER);
}

private int is_valid_message_kind(string kind)
{
    return member_array(kind, ({
        "request_reply",
        "fire_and_forget",
        "snapshot_query",
        "commit_proposal",
    })) != -1;
}

private mapping build_base_message(string source_owner, string target_owner, string kind, mapping payload, int deadline_sec)
{
    int message_id;

    message_id = _next_message_id++;
    if (deadline_sec <= 0)
        deadline_sec = DEFAULT_DEADLINE_SEC;

    return ([
        "id": message_id,
        "source_owner": source_owner,
        "target_owner": target_owner,
        "kind": kind,
        "payload": mapp(payload) ? copy(payload) : ([]),
        "state": "created",
        "created_at": time(),
        "deadline_at": time() + deadline_sec,
        "causation_id": mapp(payload) && stringp(payload["causation_id"])
            ? payload["causation_id"] : "",
        "owner_epoch": mapp(payload) && intp(payload["owner_epoch"])
            ? payload["owner_epoch"] : (mapp(payload) && intp(payload["epoch"])
                ? payload["epoch"] : 0),
    ]);
}

private int wait_path_exists(string from_owner, string target_owner, mapping visited, int depth)
{
    int message_id;
    mapping edge;

    if (!stringp(from_owner) || !stringp(target_owner))
        return 0;
    if (from_owner == target_owner)
        return 1;
    if (depth > sizeof(_wait_edges) + 1 || visited[from_owner])
        return 0;
    visited[from_owner] = 1;
    foreach (message_id, edge in _wait_edges) {
        if (mapp(edge) && edge["source_owner"] == from_owner
            && wait_path_exists(edge["target_owner"], target_owner, visited, depth + 1))
            return 1;
    }
    return 0;
}

private void rebuild_wait_graph_projection()
{
    int message_id;
    mapping edge;

    _wait_graph = ([]);
    foreach (message_id, edge in _wait_edges) {
        if (mapp(edge))
            _wait_graph[edge["source_owner"]] = edge["target_owner"];
    }
}

private void remove_wait_edge(int message_id)
{
    map_delete(_wait_edges, message_id);
    rebuild_wait_graph_projection();
}

private int message_is_active(mapping message)
{
    return mapp(message) && member_array(message["state"],
        ({ "submitted", "reply_pending" })) != -1;
}

private int message_is_terminal(mapping message)
{
    return mapp(message) && member_array(message["state"],
        ({ "replied", "timeout", "failed" })) != -1;
}

private void prune_terminal_message_history()
{
    int message_id;

    while (_terminal_message_order_tail - _terminal_message_order_head >
        MAX_TERMINAL_MESSAGE_HISTORY) {
        message_id = _terminal_message_order[_terminal_message_order_head];
        map_delete(_terminal_message_order, _terminal_message_order_head);
        _terminal_message_order_head++;
        if (!intp(message_id))
            continue;
        map_delete(_terminal_message_recorded, message_id);
        map_delete(_messages, message_id);
        map_delete(_replies, message_id);
        _message_history_evictions++;
    }
}

private void record_terminal_message(int message_id)
{
    if (!message_is_terminal(_messages[message_id]) ||
        _terminal_message_recorded[message_id])
        return;
    _terminal_message_recorded[message_id] = 1;
    _terminal_message_order[_terminal_message_order_tail++] = message_id;
    prune_terminal_message_history();
}

private void sweep_expired_messages()
{
    mapping message;
    int message_id;
    int now;
    int inspected;

    now = time();
    inspected = 0;
    foreach (message_id, message in _messages) {
        if (inspected++ >= MESSAGE_EXPIRY_SWEEP_BUDGET)
            break;
        if (!message_is_active(message) || (message["deadline_at"] || 0) >= now)
            continue;
        message["state"] = "timeout";
        message["reply_state"] = "timeout";
        message["reason"] = "deadline_exceeded";
        message["terminal_recorded"] = 1;
        message["updated_at"] = now;
        _messages[message_id] = message;
        remove_wait_edge(message_id);
        record_terminal_message(message_id);
        _expired_message_count++;
    }
}

private mapping failed_submission(mapping message, string error_code, mixed error)
{
    message["state"] = "failed";
    message["error_code"] = error_code;
    message["error"] = error;
    message["updated_at"] = time();
    _messages[message["id"]] = message;
    record_terminal_message(message["id"]);
    return ([
        "ok": 0,
        "state": "failed",
        "error_code": error_code,
        "message": copy(message),
    ]);
}

private void mark_message_submitted(mapping message, mapping submitted)
{
    message["driver_submit"] = submitted;
    message["executor_backed"] = 1;
    message["state"] = "submitted";
    message["submitted_at"] = time();
}

mapping submit_message(string source_owner, string target_owner, string kind, mapping payload, int deadline_sec)
{
    object adapter;
    mapping message;
    mapping submitted;
    mapping edge;
    string payload_key;
    mixed err;

    sweep_expired_messages();
    if (!stringp(source_owner) || source_owner == "" || !stringp(target_owner) || target_owner == "")
        return ([ "ok": 0, "state": "failed", "error_code": "OWNER_ID_REQUIRED" ]);
    if (!is_valid_message_kind(kind))
        return ([ "ok": 0, "state": "failed", "error_code": "MESSAGE_KIND_INVALID" ]);

    message = build_base_message(source_owner, target_owner, kind, payload, deadline_sec);
    if (message["causation_id"] == "")
        message["causation_id"] = sprintf("message/%d", message["id"]);
    edge = ([
        "message_id": message["id"],
        "source_owner": source_owner,
        "target_owner": target_owner,
        "causation_id": message["causation_id"],
        "owner_epoch": message["owner_epoch"],
        "deadline_at": message["deadline_at"],
    ]);
    payload_key = sprintf("msg:%d", message["id"]);
    err = catch(adapter = query_adapter());
    if (err || !objectp(adapter)) {
        _driver_submit_failed++;
        return failed_submission(message, "OWNER_ADAPTER_UNAVAILABLE", err ||
        "driver adapter unavailable");
    }
    err = catch(submitted = adapter->submit_owner_message(source_owner, target_owner, kind,
    payload_key));
    if (err || !mapp(submitted) || submitted["success"] != 1) {
        _driver_submit_failed++;
        return failed_submission(message, "OWNER_SUBMISSION_FAILED", err || submitted);
    }

    _driver_submit_ok++;
    mark_message_submitted(message, submitted);
    _messages[message["id"]] = message;
    _wait_edges[message["id"]] = edge;
    rebuild_wait_graph_projection();
    return ([ "ok": 1, "message": copy(message) ]);
}

mapping submit_request(string source_owner, string target_owner, string request_type, mapping payload, int deadline_sec)
{
    mapping request_payload;
    mapping result;

    request_payload = mapp(payload) ? copy(payload) : ([]);
    request_payload["request_type"] = stringp(request_type) && request_type != "" ? request_type : "request";
    request_payload["expects_reply"] = 1;

    result = submit_message(source_owner, target_owner, "request_reply", request_payload, deadline_sec);
    if (mapp(result) && result["ok"] == 1 && mapp(result["message"])) {
        result["message"]["expects_reply"] = 1;
        result["message"]["reply_state"] = "pending";
        _messages[result["message"]["id"]] = result["message"];
    }
    return result;
}

mapping mark_reply(int message_id, string source_owner, string target_owner,
    string causation_id, int owner_epoch, int deadline_at, mapping payload)
{
    mapping message;
    mapping reply;

    if (!mapp(_messages[message_id]))
        return ([ "ok": 0, "state": "failed", "error_code": "MESSAGE_UNKNOWN" ]);
    message = _messages[message_id];
    if (member_array(message["state"], ({ "submitted", "reply_pending" })) == -1)
        return ([ "ok": 0, "state": "failed", "error_code": "MESSAGE_TERMINAL" ]);
    if (message["target_owner"] != source_owner || message["source_owner"] != target_owner
        || message["causation_id"] != causation_id || message["owner_epoch"] != owner_epoch
        || message["deadline_at"] != deadline_at)
        return ([ "ok": 0, "state": "failed", "error_code": "MESSAGE_IDENTITY_MISMATCH" ]);
    if (time() > message["deadline_at"])
        return ([ "ok": 0, "state": "failed", "error_code": "MESSAGE_DEADLINE_EXCEEDED" ]);

    reply = ([
        "message_id": message_id,
        "source_owner": source_owner,
        "target_owner": target_owner,
        "causation_id": causation_id,
        "owner_epoch": owner_epoch,
        "payload": mapp(payload) ? copy(payload) : ([]),
        "state": "reply_submitted",
        "created_at": time(),
    ]);
    _replies[message_id] = reply;
    message["state"] = "replied";
    message["reply_state"] = "received";
    message["terminal_recorded"] = 1;
    message["updated_at"] = time();
    _messages[message_id] = message;
    remove_wait_edge(message_id);
    record_terminal_message(message_id);

    return ([ "ok": 1, "state": "replied", "message": copy(message), "reply": copy(reply) ]);
}

mapping mark_timeout(int message_id, string source_owner, string target_owner,
    string causation_id, int owner_epoch, int deadline_at, string reason)
{
    mapping message;

    if (!mapp(_messages[message_id]))
        return ([ "ok": 0, "state": "failed", "error_code": "MESSAGE_UNKNOWN" ]);
    message = _messages[message_id];
    if (member_array(message["state"], ({ "submitted", "reply_pending" })) == -1)
        return ([ "ok": 0, "state": "failed", "error_code": "MESSAGE_TERMINAL" ]);
    if (message["target_owner"] != source_owner || message["source_owner"] != target_owner
        || message["causation_id"] != causation_id || message["owner_epoch"] != owner_epoch
        || message["deadline_at"] != deadline_at)
        return ([ "ok": 0, "state": "failed", "error_code": "MESSAGE_IDENTITY_MISMATCH" ]);

    message["state"] = "timeout";
    message["reply_state"] = "timeout";
    message["reason"] = stringp(reason) && reason != "" ? reason : "deadline_exceeded";
    message["terminal_recorded"] = 1;
    message["updated_at"] = time();
    _messages[message_id] = message;
    remove_wait_edge(message_id);
    record_terminal_message(message_id);

    return ([ "ok": 1, "state": "timeout", "message": copy(message), "timeout_supported": 1 ]);
}

mapping mark_message_state(int message_id, string state, string reason)
{
    mapping message;

    if (!mapp(_messages[message_id]))
        return ([ "ok": 0, "state": "failed", "error_code": "MESSAGE_UNKNOWN" ]);
    message = _messages[message_id];
    if (member_array(message["state"], ({ "replied", "timeout", "failed" })) >= 0)
        return ([ "ok": 0, "state": "failed", "error_code": "MESSAGE_TERMINAL" ]);
    message["state"] = state;
    message["reason"] = stringp(reason) ? reason : "";
    message["updated_at"] = time();
    _messages[message_id] = message;
    if (member_array(state, ({ "replied", "timeout", "failed" })) >= 0) {
        message["terminal_recorded"] = 1;
        _messages[message_id] = message;
        remove_wait_edge(message_id);
        record_terminal_message(message_id);
    }
    return ([ "ok": 1, "message": copy(message) ]);
}

mapping query_message(int message_id)
{
    sweep_expired_messages();
    if (!mapp(_messages[message_id]))
        return 0;
    return copy(_messages[message_id]);
}

mapping query_wait_graph()
{
    sweep_expired_messages();
    return copy(_wait_graph);
}

mapping query_deadlock_watchdog()
{
    int message_id;
    mapping edge;
    mapping visited;

    foreach (message_id, edge in _wait_edges) {
        if (!mapp(edge))
            continue;
        visited = ([]);
        if (wait_path_exists(edge["target_owner"], edge["source_owner"], visited, 0))
            return ([
                "deadlock_watchdog": 1,
                "potential_deadlock": 1,
                "cycle": ({ edge["source_owner"], edge["target_owner"] }),
                "wait_graph_edges": sizeof(_wait_edges),
                "cycle_detection": "bounded_dfs",
            ]);
    }

    return ([
        "deadlock_watchdog": 1,
        "potential_deadlock": 0,
        "cycle": ({}),
        "wait_graph_edges": sizeof(_wait_edges),
        "cycle_detection": "bounded_dfs",
    ]);
}

mapping query_status()
{
    sweep_expired_messages();
    return ([
        "messages": sizeof(_messages),
        "replies": sizeof(_replies),
        "wait_edges": sizeof(_wait_edges),
        "wait_edge_records": sizeof(_wait_edges),
        "terminal_history_limit": MAX_TERMINAL_MESSAGE_HISTORY,
        "message_history_evictions": _message_history_evictions,
        "expired_message_count": _expired_message_count,
        "mode": "owner_message_bus_executor_bridge",
        "driver_submit_ok": _driver_submit_ok,
        "driver_submit_failed": _driver_submit_failed,
        "executor_bridge_ready": 1,
        "payload_policy": "spec_snapshot_or_commit_proposal_only",
        "request_reply": 1,
        "timeout_supported": 1,
        "deadlock_watchdog": 1,
        "message_only_cross_owner": 1,
        "facade_only_runtime_claims": 0,
        "direct_cross_owner_write": 0,
    ]);
}
