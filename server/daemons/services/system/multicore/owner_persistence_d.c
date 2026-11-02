/**
 * @file    owner_persistence_d.c
 * @brief   owner snapshot 与持久化 WAL 门面
 *
 * Snapshot 与收据日志使用受限的可序列化记录，核对写入成功后才允许清理待处理记录；
 * 该日志不执行业务副作用重放。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

#define MAX_PENDING_SNAPSHOTS 8192
/* 账本格式标记：非空账本必须能读回它（或读回真实数据），否则按损坏处理。 */
#define LEDGER_FORMAT_MARKER 20260918
#define MAX_TERMINAL_SNAPSHOT_HISTORY 8192

int _next_snapshot_id;
int _next_log_id;
int _snapshots_created_total;
int _snapshots_terminal_total;
int _pending_snapshot_count;
int _terminal_snapshot_evictions;
int _snapshot_capacity_rejections;
mapping _snapshots;
mapping _idempotency_log;
mapping _write_ahead_log;
mapping _snapshot_wal_ids;
mapping _wal_operation_index;
mapping _terminal_snapshot_order;
int _terminal_snapshot_order_head;
int _terminal_snapshot_order_tail;
int *_pending_replay;
/* Handler objects are runtime-only; the WAL stores only stable names and versions. */
nosave mapping _wal_handlers;

private string query_save_file();
private int persist_runtime_state();
private void restore_persistence_state();
private mapping register_wal_handlers();
private void normalize_restored_state();
private mapping build_terminal_snapshot_record(mapping snapshot);
private int retain_terminal_snapshot(int snapshot_id);
private mapping append_wal(string operation, string idempotency_key, int snapshot_id,
mapping payload);
private mapping invoke_wal_handler(mapping entry);
private int query_snapshot_wal_owner(int log_id);
private string canonical_snapshot_value(mixed value);

#include "owner_persistence_publication.c"

private string _recovery_state;
private string _recovery_error;
private int _ledger_format;

void create()
{
    ::create();
    _next_snapshot_id = 1;
    _next_log_id = 1;
    _snapshots_created_total = 0;
    _snapshots_terminal_total = 0;
    _pending_snapshot_count = 0;
    _terminal_snapshot_evictions = 0;
    _snapshot_capacity_rejections = 0;
    _snapshots = ([]);
    _idempotency_log = ([]);
    _write_ahead_log = ([]);
    _snapshot_wal_ids = ([]);
    _wal_operation_index = ([]);
    _terminal_snapshot_order = ([]);
    _terminal_snapshot_order_head = 1;
    _terminal_snapshot_order_tail = 1;
    _pending_replay = ({});
    _wal_handlers = register_wal_handlers();
    restore_persistence_state();
    schedule_publication_retry();
}

private string query_save_file()
{
    return DATA_DIR + "runtime/owner_persistence_d";
}

/* 非空账本至少要能读回标记、任一数据结构或计数器，否则视为损坏。 */
private int ledger_restore_verifiable()
{
    if (_ledger_format == LEDGER_FORMAT_MARKER)
        return 1;
    if (_next_log_id > 1 || _next_snapshot_id > 1)
        return 1;
    return sizeof(_snapshots) + sizeof(_write_ahead_log) + sizeof(_idempotency_log)
        + sizeof(_terminal_snapshot_order) + sizeof(_snapshot_wal_ids) > 0;
}

private int persist_runtime_state()
{
    string runtime_dir;
    mixed err;
    int saved;

    /* 账本损坏时绝不写回：覆盖会让“可人工恢复的损坏文件”变成静默清空。 */
    if (_recovery_state == "corrupt") {
        log_file("owner_persistence", sprintf("%s refuse to persist over corrupt ledger: %s\n",
            ctime(time()), _recovery_error));
        return 0;
    }
    runtime_dir = DATA_DIR + "runtime";
    if (file_size(runtime_dir) == -1 && !mkdir(runtime_dir))
        return 0;
    _ledger_format = LEDGER_FORMAT_MARKER;
    saved = 0;
    err = catch(saved = save_object(query_save_file()));
    return !err && saved;
}

private void restore_persistence_state()
{
    mixed err;
    int restored;

    _recovery_state = "fresh";
    _recovery_error = "";
    if (file_size(query_save_file() + ".o") == -1)
        return;
    err = file_size(query_save_file() + ".o") > 0
        ? catch(restored = restore_object(query_save_file()))
        : "persistence ledger is empty or not a regular file";
    /* 这个驱动对垃圾文件 restore_object 会抛错、返回 1，且一个变量都不恢复；
     * 所以只靠 err/返回值判不出损坏，必须看“账本标记或实际数据是否读回来”。 */
    if (err || !restored || !ledger_restore_verifiable()) {
        /* 损坏的账本不能当“合法空”继续：保留原文件、拒绝覆盖，并暴露状态。 */
        _recovery_state = "corrupt";
        _recovery_error = err ? sprintf("%O", err)
            : !restored ? "restore_object returned failure"
            : "restored ledger contains no recognizable state";
        log_file("owner_persistence", sprintf("%s owner persistence restore failed: %s\n",
            ctime(time()), _recovery_error));
        _next_snapshot_id = 1;
        _next_log_id = 1;
        _snapshots = ([]);
        _idempotency_log = ([]);
        _write_ahead_log = ([]);
        _snapshot_wal_ids = ([]);
        _wal_operation_index = ([]);
        _terminal_snapshot_order = ([]);
        _terminal_snapshot_order_head = 1;
        _terminal_snapshot_order_tail = 1;
        _pending_replay = ({});
    }
    if (!mapp(_snapshots))
        _snapshots = ([]);
    if (!mapp(_idempotency_log))
        _idempotency_log = ([]);
    if (!mapp(_write_ahead_log))
        _write_ahead_log = ([]);
    if (!mapp(_snapshot_wal_ids))
        _snapshot_wal_ids = ([]);
    if (!mapp(_wal_operation_index))
        _wal_operation_index = ([]);
    if (!mapp(_terminal_snapshot_order))
        _terminal_snapshot_order = ([]);
    if (!arrayp(_pending_replay))
        _pending_replay = ({});
    if (_next_snapshot_id <= 0)
        _next_snapshot_id = 1;
    if (_next_log_id <= 0)
        _next_log_id = 1;
    if (_recovery_state != "corrupt")
        _recovery_state = "ready";
    normalize_restored_state();
    if (_recovery_state != "corrupt" && sizeof(_pending_replay) > 0)
        _recovery_state = "pending_recovery";
}

/* 供 health/诊断区分：fresh（还没落盘）/ ready / pending_recovery / corrupt。 */
string query_recovery_state()
{
    return _recovery_state;
}

string query_recovery_error()
{
    return _recovery_error;
}

private mapping register_wal_handlers()
{
    return ([
        "snapshot_captured": ([
            "name": "snapshot", "record_type": "snapshot_persist", "version": 1,
        ]),
        "async_persist": ([
            "name": "snapshot", "record_type": "snapshot_persist", "version": 1,
        ]),
        "world_state": ([
            "name": "world_state", "record_type": "audit_receipt", "version": 1,
        ]),
        "world_event": ([
            "name": "world_event", "record_type": "audit_receipt", "version": 1,
        ]),
        "npc_schedule": ([
            "name": "npc_schedule", "record_type": "audit_receipt", "version": 1,
        ]),
        "player_projection": ([
            "name": "player_projection", "record_type": "audit_receipt", "version": 1,
        ]),
        "owner_output": ([
            "name": "owner_output", "record_type": "audit_receipt", "version": 1,
        ]),
        "owner_commit": ([
            "name": "owner_commit", "record_type": "audit_receipt", "version": 1,
        ]),
    ]);
}

private void normalize_restored_state()
{
    int log_id;
    int snapshot_id;
    int order_id;
    int pending_count;
    int terminal_count;
    int wal_active;
    int *pending;
    int *ordered_terminals;
    mapping entry;
    mapping handler;
    mapping snapshot;
    mapping seen_terminals;
    int *wal_ids;
    string idempotency_key;

    /*
     * Older owner-persistence files could retain the snapshot counter while
     * losing part of the terminal-order index.  Reconcile that metadata on
     * restore instead of treating historical receipts as live failures.
     */
    foreach (log_id in keys(_write_ahead_log)) {
        entry = _write_ahead_log[log_id];
        if (!mapp(entry))
            continue;
        if (entry["state"] == "replayed")
            entry["state"] = "verified";
        else if (entry["state"] == "completed")
            entry["state"] = "settled";
        handler = _wal_handlers[entry["domain"]];
        if (!stringp(entry["record_type"]) && mapp(handler))
            entry["record_type"] = handler["record_type"] || "audit_receipt";
        snapshot_id = query_snapshot_wal_owner(log_id);
        if ((!intp(entry["snapshot_id"]) || entry["snapshot_id"] <= 0)
            && snapshot_id > 0)
            entry["snapshot_id"] = snapshot_id;
        if ((!intp(entry["snapshot_id"]) || entry["snapshot_id"] <= 0)
            && mapp(entry["frozen_payload"])
            && intp(entry["frozen_payload"]["id"]))
            entry["snapshot_id"] = entry["frozen_payload"]["id"];
        _write_ahead_log[log_id] = entry;
    }

    /*
     * A retained receipt may outlive its terminal snapshot after an older
     * history-pruning implementation.  It cannot be replayed without a
     * snapshot, so settle the orphan receipt and remove its replay ownership;
     * never fabricate business state from an audit payload.
     */
    foreach (snapshot_id in keys(_snapshot_wal_ids)) {
        if (mapp(_snapshots[snapshot_id]))
            continue;
        wal_ids = _snapshot_wal_ids[snapshot_id];
        if (arrayp(wal_ids))
            foreach (log_id in wal_ids) {
                entry = _write_ahead_log[log_id];
                if (!mapp(entry))
                    continue;
                entry["state"] = "settled";
                entry["snapshot_id"] = 0;
                entry["updated_at"] = time();
                _write_ahead_log[log_id] = entry;
                if (stringp(entry["operation_id"]))
                    _wal_operation_index[entry["operation_id"]] = log_id;
            }
        map_delete(_snapshot_wal_ids, snapshot_id);
    }
    foreach (log_id in keys(_write_ahead_log)) {
        entry = _write_ahead_log[log_id];
        if (!mapp(entry) || !intp(entry["snapshot_id"])
            || entry["snapshot_id"] <= 0
            || mapp(_snapshots[entry["snapshot_id"]]))
            continue;
        entry["state"] = "settled";
        entry["snapshot_id"] = 0;
        entry["updated_at"] = time();
        _write_ahead_log[log_id] = entry;
        if (stringp(entry["operation_id"]))
            _wal_operation_index[entry["operation_id"]] = log_id;
    }

    pending = ({ });
    foreach (log_id in keys(_write_ahead_log)) {
        entry = _write_ahead_log[log_id];
        if (mapp(entry) && member_array(entry["state"],
            ({ "pending", "retry_pending" })) != -1)
            pending += ({ log_id });
    }
    _pending_replay = pending;

    ordered_terminals = ({ });
    seen_terminals = ([]);
    foreach (order_id in keys(_terminal_snapshot_order)) {
        snapshot_id = _terminal_snapshot_order[order_id];
        snapshot = _snapshots[snapshot_id];
        if (mapp(snapshot) && snapshot["terminal_recorded"]
            && !seen_terminals[snapshot_id]) {
            ordered_terminals += ({ snapshot_id });
            seen_terminals[snapshot_id] = 1;
        }
    }
    foreach (snapshot_id in keys(_snapshots)) {
        snapshot = _snapshots[snapshot_id];
        if (!mapp(snapshot))
            continue;
        if (snapshot["state"] == "replayed")
            snapshot["state"] = "verified";
        snapshot["crash_replayable"] = 0;
        snapshot["business_replayable"] = 0;
        snapshot["durable_receipt"] = 1;
        if (!snapshot["terminal_recorded"]) {
            wal_active = 0;
            wal_ids = _snapshot_wal_ids[snapshot_id];
            if (arrayp(wal_ids))
                foreach (log_id in wal_ids) {
                    entry = _write_ahead_log[log_id];
                    if (mapp(entry) && member_array(entry["state"],
                        ({ "pending", "retry_pending" })) != -1) {
                        wal_active = 1;
                        break;
                    }
                }
            if (!wal_active)
                snapshot = build_terminal_snapshot_record(snapshot);
        }
        _snapshots[snapshot_id] = snapshot;
        if (snapshot["terminal_recorded"]) {
            /* 括号必须显式：不然 else 会绑到内层 if，把已终态快照算成 pending。 */
            if (!seen_terminals[snapshot_id]) {
                ordered_terminals += ({ snapshot_id });
                seen_terminals[snapshot_id] = 1;
            }
        } else
            pending_count++;
    }

    _terminal_snapshot_order = ([]);
    _terminal_snapshot_order_head = 1;
    _terminal_snapshot_order_tail = 1;
    foreach (snapshot_id in ordered_terminals)
        _terminal_snapshot_order[_terminal_snapshot_order_tail++] = snapshot_id;
    terminal_count = sizeof(ordered_terminals);
    if (_snapshots_created_total < terminal_count + pending_count)
        _snapshots_created_total = terminal_count + pending_count;
    _pending_snapshot_count = pending_count;
    _snapshots_terminal_total = _snapshots_created_total - pending_count;
    if (_snapshots_terminal_total < terminal_count)
        _snapshots_terminal_total = terminal_count;
    _terminal_snapshot_evictions = _snapshots_terminal_total - terminal_count;

    foreach (idempotency_key in keys(_idempotency_log)) {
        snapshot_id = _idempotency_log[idempotency_key];
        if (!mapp(_snapshots[snapshot_id]))
            map_delete(_idempotency_log, idempotency_key);
    }
    persist_runtime_state();
}

private mapping freeze_mapping(mapping value)
{
    mapping frozen;
    mixed err;

    if (!mapp(value))
        return ([]);
    err = catch(frozen = restore_variable(save_variable(value)));
    if (err || !mapp(frozen))
        return 0;
    return frozen;
}

private mapping append_wal(string operation, string idempotency_key, int snapshot_id, mapping payload)
{
    int log_id;
    int existing_log_id;
    mapping entry;
    mapping existing;
    mapping frozen;
    mapping handler;

    if (!stringp(operation) || operation == "" || !stringp(idempotency_key)
        || idempotency_key == "")
        return ([ "ok": 0, "state": "failed", "error_code": "WAL_IDENTITY_REQUIRED" ]);
    handler = _wal_handlers[operation];
    if (!mapp(handler))
        return ([ "ok": 0, "state": "failed", "error_code": "WAL_HANDLER_MISSING" ]);
    frozen = freeze_mapping(payload);
    if (!mapp(frozen))
        return ([ "ok": 0, "state": "failed", "error_code": "WAL_PAYLOAD_NOT_SERIALIZABLE" ]);

    /* L-91：同键重入必须比对“操作+落盘参数+归属快照”是否完全一致；
     * 完全一致直接复用既有收据（不重复入账），不一致 fail-closed。 */
    foreach (existing_log_id in keys(_write_ahead_log)) {
        existing = _write_ahead_log[existing_log_id];
        if (!mapp(existing) || existing["operation_id"] != idempotency_key)
            continue;
        if (existing["domain"] != operation
            || canonical_snapshot_value(existing["frozen_payload"])
                != canonical_snapshot_value(frozen)
            || (intp(existing["snapshot_id"]) ? existing["snapshot_id"] : 0)
                != (snapshot_id > 0 ? snapshot_id : 0))
            return ([
                "ok": 0,
                "state": "failed",
                "error_code": "WAL_IDEMPOTENCY_CONFLICT",
                "log_id": existing_log_id,
                "log": copy(existing),
            ]);
        return ([
            "ok": 1,
            "log_id": existing_log_id,
            "log": copy(existing),
            "persisted": 1,
            "idempotent_hit": 1,
        ]);
    }

    log_id = _next_log_id++;
    entry = ([
        "operation_id": idempotency_key,
        "domain": operation,
        "record_type": handler["record_type"] || "audit_receipt",
        "snapshot_id": snapshot_id > 0 ? snapshot_id : 0,
        "frozen_payload": frozen,
        "state": "pending",
        "attempts": 0,
        "created_at": time(),
        "updated_at": time(),
        "handler_version": handler["version"],
    ]);
    _write_ahead_log[log_id] = entry;
    if (snapshot_id > 0) {
        if (!arrayp(_snapshot_wal_ids[snapshot_id]))
            _snapshot_wal_ids[snapshot_id] = ({});
        _snapshot_wal_ids[snapshot_id] += ({ log_id });
    } else
        _wal_operation_index[idempotency_key] = log_id;
    _pending_replay += ({ log_id });
    if (!persist_runtime_state()) {
        entry["state"] = "retry_pending";
        entry["attempts"] = 1;
        entry["updated_at"] = time();
        _write_ahead_log[log_id] = entry;
        return ([
            "ok": 0,
            "state": "retry_pending",
            "error_code": "WAL_PERSIST_FAILED",
            "retry_token": sprintf("wal-retry/%d/%d", log_id, entry["attempts"]),
            "log_id": log_id,
            "log": copy(entry),
        ]);
    }
    _write_ahead_log[log_id] = entry;
    return ([ "ok": 1, "log_id": log_id, "log": copy(entry), "persisted": 1 ]);
}

private int requeue_wal_entry(int log_id)
{
    mapping entry;
    int *pending;

    entry = _write_ahead_log[log_id];
    if (!mapp(entry))
        return 0;
    entry["state"] = "retry_pending";
    entry["attempts"] = intp(entry["attempts"]) ? entry["attempts"] + 1 : 1;
    entry["updated_at"] = time();
    _write_ahead_log[log_id] = entry;
    pending = _pending_replay;
    if (member_array(log_id, pending) == -1)
        _pending_replay += ({ log_id });
    return persist_runtime_state();
}

private int requeue_snapshot_wal(int snapshot_id)
{
    int log_id;
    int *wal_ids;
    mapping pending;

    wal_ids = _snapshot_wal_ids[snapshot_id];
    if (!arrayp(wal_ids))
        return persist_runtime_state();
    pending = ([]);
    foreach (log_id in _pending_replay)
        pending[log_id] = 1;
    foreach (log_id in wal_ids) {
        if (!mapp(_write_ahead_log[log_id]))
            continue;
        _write_ahead_log[log_id]["state"] = "retry_pending";
        _write_ahead_log[log_id]["attempts"] =
            intp(_write_ahead_log[log_id]["attempts"])
                ? _write_ahead_log[log_id]["attempts"] + 1 : 1;
        _write_ahead_log[log_id]["updated_at"] = time();
        if (!pending[log_id]) {
            _pending_replay += ({ log_id });
            pending[log_id] = 1;
        }
    }
    return persist_runtime_state();
}

private int snapshot_wal_verification_complete(int snapshot_id)
{
    int log_id;
    int *wal_ids;

    wal_ids = _snapshot_wal_ids[snapshot_id];
    if (!arrayp(wal_ids) || sizeof(wal_ids) == 0)
        return 0;
    foreach (log_id in wal_ids) {
        if (!mapp(_write_ahead_log[log_id]) ||
            member_array(_write_ahead_log[log_id]["state"],
                ({ "verified", "settled" })) == -1)
            return 0;
    }
    return 1;
}

private int query_snapshot_wal_owner(int log_id)
{
    int snapshot_id;

    foreach (snapshot_id in keys(_snapshot_wal_ids))
        if (arrayp(_snapshot_wal_ids[snapshot_id])
            && member_array(log_id, _snapshot_wal_ids[snapshot_id]) != -1)
            return snapshot_id;
    return 0;
}

private int count_tracked_wal_entries()
{
    int log_id;
    int snapshot_id;
    mapping entry;
    mapping seen;

    seen = ([]);
    foreach (snapshot_id in keys(_snapshot_wal_ids)) {
        if (!mapp(_snapshots[snapshot_id]) || !arrayp(_snapshot_wal_ids[snapshot_id]))
            return -1;
        foreach (log_id in _snapshot_wal_ids[snapshot_id]) {
            entry = _write_ahead_log[log_id];
            if (seen[log_id] || !mapp(entry))
                return -1;
            seen[log_id] = 1;
        }
    }
    foreach (log_id in keys(_write_ahead_log)) {
        entry = _write_ahead_log[log_id];
        if (!mapp(entry) || seen[log_id])
            continue;
        if (!stringp(entry["operation_id"]) ||
            _wal_operation_index[entry["operation_id"]] != log_id)
            return -1;
        seen[log_id] = 1;
    }
    return sizeof(seen);
}

private int pending_replay_is_valid()
{
    int log_id;
    mapping entry;
    mapping seen;

    seen = ([]);
    foreach (log_id in _pending_replay) {
        entry = _write_ahead_log[log_id];
        if (seen[log_id] || !mapp(entry) ||
            member_array(entry["state"], ({ "pending", "retry_pending" })) == -1)
            return 0;
        seen[log_id] = 1;
    }
    return 1;
}

private int settle_snapshot_wal(int snapshot_id, int keep_for_replay)
{
    int log_id;
    int remaining_count;
    int *remaining;
    int *wal_ids;
    mapping settled;

    wal_ids = _snapshot_wal_ids[snapshot_id];
    if (!arrayp(wal_ids) || sizeof(wal_ids) == 0)
        return 1;
    if (keep_for_replay) {
        requeue_snapshot_wal(snapshot_id);
        return 0;
    }

    settled = ([]);
    foreach (log_id in wal_ids) {
        settled[log_id] = 1;
        if (mapp(_write_ahead_log[log_id])) {
            _write_ahead_log[log_id]["state"] = "settled";
            _write_ahead_log[log_id]["updated_at"] = time();
        }
    }
    remaining = allocate(sizeof(_pending_replay));
    foreach (log_id in _pending_replay) {
        if (!settled[log_id])
            remaining[remaining_count++] = log_id;
    }
    _pending_replay = remaining_count > 0 ? remaining[0..remaining_count - 1] : ({});
    return persist_runtime_state();
}

private void purge_snapshot_wal(int snapshot_id)
{
    int log_id;
    int *wal_ids;

    wal_ids = _snapshot_wal_ids[snapshot_id];
    if (!arrayp(wal_ids))
        return;
    foreach (log_id in wal_ids)
        map_delete(_write_ahead_log, log_id);
    map_delete(_snapshot_wal_ids, snapshot_id);
    persist_runtime_state();
}

private mapping build_terminal_snapshot_record(mapping snapshot)
{
    return ([
        "id": snapshot["id"],
        "object_id": snapshot["object_id"],
        "owner_id": snapshot["owner_id"],
        "owner_epoch": snapshot["owner_epoch"],
        "snapshot_type": snapshot["snapshot_type"],
        "identity_digest": snapshot["identity_digest"],
        "idempotency_key": snapshot["idempotency_key"],
        "state": snapshot["state"],
        "reason": stringp(snapshot["reason"]) ? snapshot["reason"] : "",
        "captured_at": snapshot["captured_at"],
        "updated_at": snapshot["updated_at"],
        "terminal_recorded": 1,
        "payload_released": 1,
        "async_persistence": 1,
        "durable_receipt": 1,
        "business_replayable": 0,
        "crash_replayable": 0,
    ]);
}

private void prune_terminal_snapshot_history()
{
    int snapshot_id;
    mapping snapshot;
    string idempotency_key;

    while (_terminal_snapshot_order_tail - _terminal_snapshot_order_head >
        MAX_TERMINAL_SNAPSHOT_HISTORY) {
        snapshot_id = _terminal_snapshot_order[_terminal_snapshot_order_head];
        map_delete(_terminal_snapshot_order, _terminal_snapshot_order_head);
        _terminal_snapshot_order_head++;
        snapshot = _snapshots[snapshot_id];
        if (!mapp(snapshot))
            continue;
        idempotency_key = snapshot["idempotency_key"];
        if (stringp(idempotency_key) && intp(_idempotency_log[idempotency_key])
            && _idempotency_log[idempotency_key] == snapshot_id)
            map_delete(_idempotency_log, idempotency_key);
        purge_snapshot_wal(snapshot_id);
        map_delete(_snapshots, snapshot_id);
        _terminal_snapshot_evictions++;
    }
    persist_runtime_state();
}

private int retain_terminal_snapshot(int snapshot_id)
{
    mapping previous;
    int previous_pending;

    if (!mapp(_snapshots[snapshot_id]))
        return 0;
    if (_snapshots[snapshot_id]["terminal_recorded"])
        return 1;
    previous = _snapshots[snapshot_id];
    previous_pending = _pending_snapshot_count;
    _snapshots[snapshot_id] = build_terminal_snapshot_record(previous);
    _terminal_snapshot_order[_terminal_snapshot_order_tail++] = snapshot_id;
    _snapshots_terminal_total++;
    if (_pending_snapshot_count > 0)
        _pending_snapshot_count--;
    if (!persist_runtime_state()) {
        _snapshots[snapshot_id] = previous;
        map_delete(_terminal_snapshot_order, --_terminal_snapshot_order_tail);
        _snapshots_terminal_total--;
        _pending_snapshot_count = previous_pending;
        return 0;
    }
    prune_terminal_snapshot_history();
    return 1;
}

private mapping sanitize_owner_snapshot(object target, string owner_id, mapping payload)
{
    mapping metadata;
    mapping frozen_payload;

    frozen_payload = freeze_mapping(payload);
    if (!mapp(frozen_payload))
        return 0;
    metadata = objectp(target) ? OWNER_RUNTIME_D->query_owner_metadata(target) : ([ "ok": 0 ]);
    return ([
        "object_id": objectp(target) ? file_name(target) : "",
        "owner_id": stringp(owner_id) && owner_id != "" ? owner_id : (metadata["owner_id"] || ""),
        "owner_epoch": metadata["owner_epoch"] || 0,
        "payload": frozen_payload,
        "captured_at": time(),
    ]);
}

/* Canonicalize the frozen business identity.  save_variable() is not a safe
 * equality oracle for mappings because construction order is not business
 * content; terminal snapshots retain only this digest after payload pruning. */
private string canonical_snapshot_value(mixed value)
{
    mixed key;
    mixed item;
    string *parts;

    if (mapp(value)) {
        parts = ({});
        foreach (key in sort_array(keys(value), 1))
            parts += ({ canonical_snapshot_value(key) + "="
                + canonical_snapshot_value(value[key]) });
        return "M{" + implode(parts, ";") + "}";
    }
    if (arrayp(value)) {
        parts = ({});
        foreach (item in value)
            parts += ({ canonical_snapshot_value(item) });
        return "A[" + implode(parts, ";") + "]";
    }
    if (stringp(value))
        return sprintf("S%d:%s", strlen(value), value);
    if (intp(value))
        return sprintf("I:%d", value);
    if (floatp(value))
        return "F:" + sprintf("%O", value);
    if (objectp(value))
        return "O:" + file_name(value);
    return "X:" + sprintf("%O", value);
}

private string snapshot_identity_digest(mapping snapshot)
{
    return hash("sha256", canonical_snapshot_value(([
        "object_id": snapshot["object_id"],
        "owner_id": snapshot["owner_id"],
        "owner_epoch": snapshot["owner_epoch"],
        "snapshot_type": snapshot["snapshot_type"],
        "payload": snapshot["payload"],
    ])));
}

private int snapshot_identity_matches(mapping existing, mapping candidate)
{
    if (!mapp(existing) || !mapp(candidate)
        || !stringp(existing["identity_digest"])
        || !stringp(candidate["identity_digest"]))
        return 0;
    return existing["identity_digest"] == candidate["identity_digest"];
}

mapping create_snapshot(object target, string owner_id, string snapshot_type, mapping payload, string idempotency_key)
{
    int snapshot_id;
    mapping snapshot;
    mapping existing;
    mapping appended;
    mapping receipt_candidate;
    mapping publication;

    if (!objectp(target))
        return ([ "ok": 0, "reason": "target is required" ]);
    if (!stringp(snapshot_type) || snapshot_type == "")
        return ([ "ok": 0, "reason": "snapshot_type is required" ]);
    if (!stringp(idempotency_key) || idempotency_key == "")
        return ([ "ok": 0, "reason": "idempotency_key is required" ]);
    snapshot = sanitize_owner_snapshot(target, owner_id, payload);
    if (!mapp(snapshot))
        return ([ "ok": 0, "state": "failed", "error_code": "SNAPSHOT_NOT_SERIALIZABLE" ]);
    snapshot["snapshot_type"] = snapshot_type;
    snapshot["identity_digest"] = snapshot_identity_digest(snapshot);
    if (intp(_idempotency_log[idempotency_key])
        && mapp(existing = _snapshots[_idempotency_log[idempotency_key]])) {
        if (!snapshot_identity_matches(existing, snapshot)) {
            /* A new runtime epoch may acknowledge old published bytes, but
             * never gains permission to write an old, uncommitted candidate. */
            receipt_candidate = copy(snapshot);
            receipt_candidate["owner_epoch"] = existing["owner_epoch"];
            receipt_candidate["identity_digest"] = snapshot_identity_digest(receipt_candidate);
            publication = query_snapshot_publication(existing["id"]);
            if (publication["outcome"] == "not_written"
                    && snapshot["owner_epoch"] != existing["owner_epoch"])
                return ([ "ok": 0, "state": "rejected",
                    "error_code": "SNAPSHOT_IDEMPOTENCY_CONFLICT" ]);
            if (!snapshot_identity_matches(existing, receipt_candidate))
                return ([ "ok": 0, "state": "rejected",
                    "error_code": "SNAPSHOT_IDEMPOTENCY_CONFLICT" ]);
        }
        if (existing["state"] == "retry_pending") {
            /* 内存中的失败捕获不是持久接纳；同 ID 重试必须先补齐账本。 */
            existing["state"] = "captured";
            if (!persist_runtime_state()) {
                existing["state"] = "retry_pending";
                return ([ "ok": 0, "state": "retry_pending",
                    "error_code": "WAL_PERSIST_FAILED",
                    "retry_token": existing["retry_token"] || "",
                    "snapshot": copy(existing) ]);
            }
        }
        return ([ "ok": 1, "duplicate": 1, "snapshot": copy(existing) ]);
    }
    if (_pending_snapshot_count >= MAX_PENDING_SNAPSHOTS) {
        _snapshot_capacity_rejections++;
        return ([ "ok": 0, "reason": "pending snapshot capacity reached" ]);
    }

    snapshot_id = _next_snapshot_id++;
    snapshot["id"] = snapshot_id;
    snapshot["idempotency_key"] = idempotency_key;
    snapshot["state"] = "captured";
    snapshot["async_persistence"] = 1;
    snapshot["durable_receipt"] = 1;
    snapshot["business_replayable"] = 0;
    snapshot["crash_replayable"] = 0;
    _snapshots[snapshot_id] = snapshot;
    _idempotency_log[idempotency_key] = snapshot_id;
    _snapshots_created_total++;
    _pending_snapshot_count++;
    appended = append_wal("snapshot_captured", idempotency_key, snapshot_id, snapshot);
    if (!mapp(appended) || appended["ok"] != 1) {
        snapshot["state"] = "retry_pending";
        snapshot["retry_token"] = mapp(appended) ? appended["retry_token"] : "";
        _snapshots[snapshot_id] = snapshot;
        return ([
            "ok": 0,
            "state": "retry_pending",
            "error_code": mapp(appended) ? appended["error_code"] : "WAL_APPEND_FAILED",
            "retry_token": snapshot["retry_token"],
            "snapshot": copy(snapshot),
        ]);
    }
    return ([ "ok": 1, "snapshot": copy(snapshot), "log": appended["log"] ]);
}

mapping enqueue_async_persist(int snapshot_id, string idempotency_key)
{
    mapping snapshot;
    mapping entry;
    mapping appended;

    if (!mapp(_snapshots[snapshot_id]))
        return ([ "ok": 0, "reason": "unknown snapshot" ]);
    if (!stringp(idempotency_key) || idempotency_key == "")
        return ([ "ok": 0, "reason": "idempotency_key is required" ]);

    snapshot = _snapshots[snapshot_id];
    if (snapshot["terminal_recorded"])
        return ([ "ok": 0, "reason": "snapshot is already terminal" ]);
    if (snapshot["state"] == "queued") {
        if (snapshot["queued_idempotency_key"] == idempotency_key &&
            intp(snapshot["queued_log_id"]) && mapp(_write_ahead_log[snapshot["queued_log_id"]]))
            return ([ "ok": 1, "duplicate": 1,
                      "log": copy(_write_ahead_log[snapshot["queued_log_id"]]),
                      "snapshot": copy(snapshot) ]);
        return ([ "ok": 0, "reason": "snapshot persistence is already queued" ]);
    }
    if (snapshot["state"] == "save_failed" && intp(snapshot["queued_log_id"])) {
        if (snapshot["queued_idempotency_key"] != idempotency_key)
            return ([ "ok": 0, "reason": "snapshot retry idempotency key mismatch" ]);
        requeue_snapshot_wal(snapshot_id);
        snapshot["state"] = "queued";
        snapshot["queued_at"] = time();
        _snapshots[snapshot_id] = snapshot;
        return ([ "ok": 1, "duplicate": 1, "retry": 1,
                  "log": copy(_write_ahead_log[snapshot["queued_log_id"]]),
                  "snapshot": copy(snapshot) ]);
    }
    if (member_array(snapshot["state"], ({ "captured", "save_failed" })) == -1)
        return ([ "ok": 0, "reason": "snapshot state does not allow persistence enqueue" ]);
    appended = append_wal("async_persist", idempotency_key, snapshot_id, snapshot);
    if (!mapp(appended) || appended["ok"] != 1)
        return appended;
    entry = appended["log"];
    snapshot["state"] = "queued";
    snapshot["queued_log_id"] = appended["log_id"];
    snapshot["queued_idempotency_key"] = idempotency_key;
    snapshot["queued_at"] = time();
    _snapshots[snapshot_id] = snapshot;
    if (!persist_runtime_state()) {
        snapshot["state"] = "save_failed";
        snapshot["reason"] = "queued snapshot state persistence failed";
        _snapshots[snapshot_id] = snapshot;
        requeue_snapshot_wal(snapshot_id);
        return ([ "ok": 0, "state": "retry_pending",
                  "error_code": "SNAPSHOT_QUEUE_PERSIST_FAILED",
                  "retry_token": sprintf("snapshot-retry/%d", snapshot_id),
                  "snapshot": copy(snapshot) ]);
    }
    return ([ "ok": 1, "duplicate": 0, "log": copy(entry), "snapshot": copy(snapshot) ]);
}

private mapping invoke_wal_handler(mapping entry)
{
    mapping handler;
    mapping snapshot;
    object service;
    string service_path;
    string record_type;
    int snapshot_id;
    mixed err;

    if (!mapp(entry) || !stringp(entry["domain"]))
        return ([ "ok": 0, "error_code": "WAL_ENTRY_INVALID" ]);
    handler = _wal_handlers[entry["domain"]];
    if (!mapp(handler) || entry["handler_version"] != handler["version"])
        return ([ "ok": 0, "error_code": "WAL_HANDLER_VERSION_MISMATCH" ]);
    record_type = entry["record_type"] || handler["record_type"] || "";
    if (record_type == "snapshot_persist") {
        snapshot_id = intp(entry["snapshot_id"]) ? entry["snapshot_id"] : 0;
        snapshot = _snapshots[snapshot_id];
        if (snapshot_id <= 0 || !mapp(snapshot))
            return ([ "ok": 0, "error_code": "WAL_SNAPSHOT_MISSING" ]);
        return ([
            "ok": 1,
            "handler": "snapshot",
            "record_type": "snapshot_persist",
            "verification": "snapshot_present",
            "business_applied": 0,
            "persisted": 1,
        ]);
    }
    if (record_type != "audit_receipt")
        return ([ "ok": 0, "error_code": "WAL_RECORD_TYPE_UNKNOWN" ]);

    switch (handler["name"]) {
        case "world_state":
            service_path = WORLD_STATE_D;
            break;
        case "world_event":
            service_path = WORLD_EVENT_D;
            break;
        case "npc_schedule":
            service_path = NPC_SCHEDULE_D;
            break;
        case "player_projection":
            service_path = CLIENT_PROJECTION_D;
            break;
        case "owner_output":
            service_path = OWNER_SESSION_FIFO_D;
            break;
        case "owner_commit":
            service_path = OWNER_COMMIT_D;
            break;
        default:
            return ([ "ok": 0, "error_code": "WAL_HANDLER_UNKNOWN" ]);
    }
    err = catch(service = load_object(service_path));
    if (err || !objectp(service))
        return ([
            "ok": 0,
            "error_code": "WAL_HANDLER_UNAVAILABLE",
            "handler": handler["name"],
            "record_type": "audit_receipt",
            "error": err || "service object unavailable",
        ]);
    return ([
        "ok": 1,
        "handler": handler["name"],
        "handler_version": handler["version"],
        "record_type": "audit_receipt",
        "verification": "service_present",
        "business_applied": 0,
        "persisted": 1,
        "service_path": service_path,
    ]);
}

mapping persist_wal_state(string operation_id, string domain, mapping frozen_payload)
{
    int log_id;
    mapping existing;
    mapping appended;
    mapping frozen;

    if (!stringp(operation_id) || operation_id == "" || !stringp(domain) || domain == "")
        return ([ "ok": 0, "state": "failed", "error_code": "WAL_IDENTITY_REQUIRED" ]);
    if (!mapp(_wal_handlers[domain]))
        return ([ "ok": 0, "state": "failed", "error_code": "WAL_HANDLER_MISSING" ]);
    frozen = freeze_mapping(frozen_payload);
    if (!mapp(frozen))
        return ([ "ok": 0, "state": "failed", "error_code": "WAL_PAYLOAD_NOT_SERIALIZABLE" ]);
    log_id = _wal_operation_index[operation_id];
    if (intp(log_id) && mapp(_write_ahead_log[log_id])) {
        existing = _write_ahead_log[log_id];
        if (existing["domain"] != domain
            || canonical_snapshot_value(existing["frozen_payload"])
                != canonical_snapshot_value(frozen))
            return ([ "ok": 0, "state": "rejected", "error_code": "WAL_IDEMPOTENCY_CONFLICT" ]);
        if (existing["state"] == "retry_pending") {
            if (!requeue_wal_entry(log_id))
                return ([ "ok": 0, "state": "retry_pending",
                          "error_code": "WAL_PERSIST_FAILED",
                          "retry_token": sprintf("wal-retry/%d/%d", log_id,
                              intp(existing["attempts"]) ? existing["attempts"] : 0),
                          "operation_id": operation_id,
                          "log": copy(existing) ]);
            existing = _write_ahead_log[log_id];
        }
        return ([
            "ok": 1,
            "duplicate": 1,
            "state": existing["state"],
            "operation_id": operation_id,
            "persisted": 1,
            "log": copy(existing),
        ]);
    }
    appended = append_wal(domain, operation_id, 0, frozen);
    if (!mapp(appended) || appended["ok"] != 1)
        return appended + ([ "operation_id": operation_id ]);
    return ([
        "ok": 1,
        "duplicate": 0,
        "state": "pending",
        "operation_id": operation_id,
        "persisted": 1,
        "log": appended["log"],
    ]);
}

mapping replay_pending_logs(int limit)
{
    int verified;
    int failed;
    int log_id;
    int snapshot_id;
    mapping entry;
    mapping handler_result;
    mapping verified_snapshots;
    mapping snapshot;
    int *pending;
    int *remaining;
    int remaining_count;

    verified = 0;
    failed = 0;
    pending = _pending_replay;
    _pending_replay = ({ });
    remaining = allocate(sizeof(pending));
    verified_snapshots = ([]);
    foreach (log_id in pending) {
        entry = _write_ahead_log[log_id];
        if (!mapp(entry))
            continue;
        if (limit > 0 && verified >= limit) {
            remaining[remaining_count++] = log_id;
            continue;
        }
        handler_result = invoke_wal_handler(entry);
        if (!mapp(handler_result) || handler_result["ok"] != 1) {
            log_file("system/owner_persistence", sprintf(
                "[%s] receipt verification failed: log_id=%d operation=%s domain=%s result=%O\n",
                ctime(time())[4..19], log_id, entry["operation_id"] || "",
                entry["domain"] || "", handler_result));
            entry["state"] = "retry_pending";
            entry["attempts"] = intp(entry["attempts"]) ? entry["attempts"] + 1 : 1;
            entry["updated_at"] = time();
            _write_ahead_log[log_id] = entry;
            remaining[remaining_count++] = log_id;
            failed++;
            continue;
        }
        entry["state"] = "verified";
        entry["updated_at"] = time();
        _write_ahead_log[log_id] = entry;
        _pending_replay = remaining_count > 0 ? remaining[0..remaining_count - 1] : ({ });
        if (!persist_runtime_state()) {
            log_file("system/owner_persistence", sprintf(
                "[%s] receipt verification persistence failed: log_id=%d operation=%s domain=%s\n",
                ctime(time())[4..19], log_id, entry["operation_id"] || "",
                entry["domain"] || ""));
            entry["state"] = "retry_pending";
            entry["attempts"] = intp(entry["attempts"]) ? entry["attempts"] + 1 : 1;
            entry["updated_at"] = time();
            _write_ahead_log[log_id] = entry;
            remaining[remaining_count++] = log_id;
            failed++;
            continue;
        }
        snapshot_id = query_snapshot_wal_owner(log_id);
        if (snapshot_id > 0)
            verified_snapshots[snapshot_id] = 1;
        verified++;
    }
    _pending_replay = remaining_count > 0 ? remaining[0..remaining_count - 1] : ({ });
    foreach (snapshot_id in keys(verified_snapshots)) {
        snapshot = _snapshots[snapshot_id];
        if (!mapp(snapshot) || snapshot["terminal_recorded"])
            continue;
        if (snapshot_wal_verification_complete(snapshot_id))
            snapshot["state"] = "verified";
        else
            snapshot["state"] = "verification_pending";
        snapshot["updated_at"] = time();
        _snapshots[snapshot_id] = snapshot;
        if (mapp(snapshot["publication"]))
            continue;
        if (snapshot_wal_verification_complete(snapshot_id)) {
            if (settle_snapshot_wal(snapshot_id, 0))
                retain_terminal_snapshot(snapshot_id);
            else {
                snapshot["state"] = "retry_pending";
                snapshot["reason"] = "terminal WAL persistence failed";
                _snapshots[snapshot_id] = snapshot;
                requeue_snapshot_wal(snapshot_id);
                failed++;
            }
        }
    }
    persist_runtime_state();
    return ([
        "ok": failed == 0,
        "verified": verified,
        "failed": failed,
        "remaining": sizeof(_pending_replay),
        "receipt_verification": 1,
        "business_crash_replay": 0,
        "crash_replay": 0,
        "idempotent": 1,
        "replaces_save_object": 0,
    ]);
}

mapping mark_snapshot_state(int snapshot_id, string state, string reason)
{
    mapping snapshot;

    if (!mapp(_snapshots[snapshot_id]))
        return ([ "ok": 0, "reason": "unknown snapshot" ]);
    if (member_array(state, ({ "saved", "save_failed", "verified" })) == -1)
        return ([ "ok": 0, "reason": "invalid snapshot state" ]);
    snapshot = _snapshots[snapshot_id];
    if (snapshot["terminal_recorded"]) {
        if (snapshot["state"] == state)
            return ([ "ok": 1, "duplicate": 1, "snapshot": copy(snapshot) ]);
        return ([ "ok": 0, "reason": "snapshot is already terminal" ]);
    }
    if (snapshot["state"] == state && state == "save_failed") {
        if (!requeue_snapshot_wal(snapshot_id))
            return ([ "ok": 0, "state": "retry_pending",
                "error_code": "WAL_PERSIST_FAILED",
                "retry_token": sprintf("snapshot-retry/%d", snapshot_id),
                "snapshot": copy(snapshot) ]);
        return ([ "ok": 1, "duplicate": 1, "snapshot": copy(snapshot) ]);
    }
    snapshot["state"] = state;
    snapshot["reason"] = stringp(reason) ? reason : "";
    snapshot["updated_at"] = time();
    _snapshots[snapshot_id] = snapshot;
    if (state == "saved" || state == "verified") {
        if (!settle_snapshot_wal(snapshot_id, 0)) {
            snapshot["state"] = "save_failed";
            snapshot["reason"] = "terminal WAL persistence failed";
            _snapshots[snapshot_id] = snapshot;
            requeue_snapshot_wal(snapshot_id);
            return ([ "ok": 0, "state": "retry_pending",
                      "error_code": "WAL_PERSIST_FAILED",
                      "retry_token": sprintf("snapshot-retry/%d", snapshot_id),
                      "snapshot": copy(snapshot) ]);
        }
        if (!retain_terminal_snapshot(snapshot_id))
            return ([ "ok": 0, "state": "retry_pending",
                "error_code": "SNAPSHOT_TERMINAL_PERSIST_FAILED",
                "retry_token": sprintf("snapshot-retry/%d", snapshot_id),
                "snapshot": copy(_snapshots[snapshot_id]) ]);
        snapshot = _snapshots[snapshot_id];
    } else if (state == "save_failed") {
        settle_snapshot_wal(snapshot_id, 1);
    }
    return ([ "ok": 1, "snapshot": copy(snapshot) ]);
}

/* A publication proved not_written is no longer a recoverable business
 * candidate. Retire only the verified receipt so a later publication on the
 * same file cannot be blocked by an obsolete snapshot. */
mapping retire_unwritten_snapshot(int snapshot_id, string reason)
{
    mapping evidence;
    mapping result;

    if (!intp(snapshot_id) || snapshot_id <= 0)
        return ([ "ok": 0, "outcome": "unknown", "error_code": "SNAPSHOT_ID_INVALID" ]);
    evidence = query_snapshot_publication(snapshot_id);
    if (!mapp(evidence) || evidence["outcome"] != "not_written")
        return ([ "ok": 0,
            "outcome": mapp(evidence) ? evidence["outcome"] : "unknown",
            "error_code": "NOT_WRITTEN_EVIDENCE_REQUIRED",
            "snapshot_id": snapshot_id ]);
    result = mark_snapshot_state(snapshot_id, "save_failed",
        stringp(reason) && reason != "" ? reason
        : "publication evidence proves candidate was not written");
    if (!mapp(result) || result["ok"] != 1)
        return ([ "ok": 0, "outcome": "persisted_receipt_pending",
            "error_code": "SNAPSHOT_FAILURE_RECEIPT_PENDING",
            "snapshot_id": snapshot_id,
            "detail": result ]);
    if (!retain_terminal_snapshot(snapshot_id))
        return ([ "ok": 0, "outcome": "persisted_receipt_pending",
            "error_code": "SNAPSHOT_RETIRE_PENDING",
            "snapshot_id": snapshot_id ]);
    return ([ "ok": 1, "outcome": "not_written", "persisted": 0,
        "snapshot_id": snapshot_id ]);
}

mapping query_snapshot(int snapshot_id)
{
    if (!mapp(_snapshots[snapshot_id]))
        return 0;
    return copy(_snapshots[snapshot_id]);
}

mapping query_pending_snapshot(string owner_id, string snapshot_type,
    string content_digest)
{
    int snapshot_id;
    mapping snapshot;
    mapping payload;

    if (!stringp(owner_id) || owner_id == "" || !stringp(snapshot_type)
        || snapshot_type == "" || !stringp(content_digest)
        || content_digest == "")
        return 0;
    foreach (snapshot_id in keys(_snapshots)) {
        snapshot = _snapshots[snapshot_id];
        if (!mapp(snapshot) || snapshot["terminal_recorded"]
            || snapshot["owner_id"] != owner_id
            || snapshot["snapshot_type"] != snapshot_type)
            continue;
        payload = snapshot["payload"];
        if (mapp(payload) && payload["content_digest"] == content_digest)
            return copy(snapshot);
    }
    return 0;
}

/* Lookup does not infer publication from target existence or WAL verification. */
mapping query_pending_publication(string owner_id, string snapshot_type, string file)
{
    mapping snapshot;
    mapping found;
    int snapshot_id;

    if (_recovery_state == "corrupt")
        return ([ "ok": 0, "error_code": "PERSISTENCE_LEDGER_CORRUPT" ]);
    foreach (snapshot_id, snapshot in _snapshots) {
        if (!mapp(snapshot) || snapshot["terminal_recorded"]
            || snapshot["owner_id"] != owner_id || snapshot["snapshot_type"] != snapshot_type)
            continue;
        if (!mapp(snapshot["publication"]))
            return ([ "ok": 0, "error_code": "PUBLICATION_EVIDENCE_MISSING" ]);
        if (snapshot["publication"]["file"] != file)
            continue;
        if (mapp(found))
            return ([ "ok": 0, "error_code": "MULTIPLE_PENDING_PUBLICATIONS" ]);
        found = snapshot;
    }
    return ([ "ok": 1, "snapshot": mapp(found) ? copy(found) : 0 ]);
}

mapping query_status()
{
    int retained_terminal_snapshots;
    int snapshot_conservation_ok;
    int wal_entries_tracked;
    int wal_tracking_conservation_ok;
    int pending_wal_entries;
    int failed_wal_entries;
    int verified_wal_entries;
    int settled_wal_entries;
    int actual_pending_snapshots;
    int actual_terminal_snapshots;
    int log_id;
    int snapshot_id;
    mapping entry;
    mapping snapshot;

    actual_pending_snapshots = 0;
    actual_terminal_snapshots = 0;
    foreach (snapshot_id in keys(_snapshots)) {
        snapshot = _snapshots[snapshot_id];
        if (!mapp(snapshot))
            continue;
        if (snapshot["terminal_recorded"])
            actual_terminal_snapshots++;
        else
            actual_pending_snapshots++;
    }
    if (_snapshots_created_total < actual_pending_snapshots + actual_terminal_snapshots)
        _snapshots_created_total = actual_pending_snapshots + actual_terminal_snapshots;
    if (_pending_snapshot_count != actual_pending_snapshots
        || _snapshots_terminal_total < actual_terminal_snapshots) {
        _pending_snapshot_count = actual_pending_snapshots;
        _snapshots_terminal_total = _snapshots_created_total - actual_pending_snapshots;
        if (_snapshots_terminal_total < actual_terminal_snapshots)
            _snapshots_terminal_total = actual_terminal_snapshots;
        _terminal_snapshot_evictions = _snapshots_terminal_total -
            (_terminal_snapshot_order_tail - _terminal_snapshot_order_head);
        persist_runtime_state();
    }
    retained_terminal_snapshots =
        _terminal_snapshot_order_tail - _terminal_snapshot_order_head;
    snapshot_conservation_ok =
        _snapshots_created_total == _snapshots_terminal_total + _pending_snapshot_count &&
        _snapshots_terminal_total == _terminal_snapshot_evictions + retained_terminal_snapshots &&
        sizeof(_snapshots) == _pending_snapshot_count + retained_terminal_snapshots;
    wal_entries_tracked = count_tracked_wal_entries();
    wal_tracking_conservation_ok = wal_entries_tracked >= 0 &&
        wal_entries_tracked == sizeof(_write_ahead_log) && pending_replay_is_valid();
    foreach (log_id in keys(_write_ahead_log)) {
        entry = _write_ahead_log[log_id];
        if (!mapp(entry))
            continue;
        if (entry["state"] == "pending")
            pending_wal_entries++;
        else if (member_array(entry["state"], ({ "retry_pending", "failed" })) != -1)
            failed_wal_entries++;
        else if (entry["state"] == "verified")
            verified_wal_entries++;
        else if (entry["state"] == "settled")
            settled_wal_entries++;
    }
    return ([
        "snapshots": sizeof(_snapshots),
        "idempotency_keys": sizeof(_idempotency_log),
        "wal_entries": sizeof(_write_ahead_log),
        "pending_replay": sizeof(_pending_replay),
        "snapshots_created_total": _snapshots_created_total,
        "snapshots_terminal_total": _snapshots_terminal_total,
        "pending_snapshots": _pending_snapshot_count,
        "terminal_snapshots_retained": retained_terminal_snapshots,
        "terminal_snapshot_evictions": _terminal_snapshot_evictions,
        "snapshot_capacity_rejections": _snapshot_capacity_rejections,
        "snapshot_conservation_ok": snapshot_conservation_ok,
        "wal_entries_tracked": wal_entries_tracked,
        "wal_tracking_conservation_ok": wal_tracking_conservation_ok,
        "max_pending_snapshots": MAX_PENDING_SNAPSHOTS,
        "max_terminal_snapshot_history": MAX_TERMINAL_SNAPSHOT_HISTORY,
        "pending_wal_entries": pending_wal_entries,
        "failed_wal_entries": failed_wal_entries,
        "verified_wal_entries": verified_wal_entries,
        "settled_wal_entries": settled_wal_entries,
        "mode": "owner_persistence_receipt_snapshot",
        "async_persistence": 1,
        "idempotent_log": 1,
        "durable_receipt_journal": 1,
        "receipt_verification": 1,
        "business_crash_replay": 0,
        "crash_replay": 0,
        "snapshot_only": 1,
        "snapshot_persist_queue": 1,
        "domain_wal": 0,
        "durable_wal": 1,
        "wal_handlers": sizeof(_wal_handlers),
        "wal_operation_index": sizeof(_wal_operation_index),
        "wal_retry_supported": 1,
        "business_snapshot_required": 1,
        "owner_save_object_boundary": 1,
        "save_object_hot_path_replaced": 0,
        "direct_business_save_object_hot_path": 0,
        "facade_only_runtime_claims": 0,
        "replaces_save_object": 0,
    ]);
}

mapping health_check()
{
    mapping status;

    status = query_status();
    status["ok"] = status["snapshot_conservation_ok"] == 1 &&
        status["wal_tracking_conservation_ok"] == 1 &&
        status["failed_wal_entries"] == 0;
    if (!status["ok"]) {
        if (status["failed_wal_entries"] > 0)
            status["error_code"] = "persistence_retry_pending";
        else if (status["snapshot_conservation_ok"] != 1)
            status["error_code"] = "persistence_snapshot_conservation_failed";
        else
            status["error_code"] = "persistence_wal_conservation_failed";
    }
    return status;
}
