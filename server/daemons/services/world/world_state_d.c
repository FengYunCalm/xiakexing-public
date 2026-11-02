#pragma strict_types
#pragma save_types

#include <system/globals.h>
#include <system/services.h>
#include <system/event_names.h>

inherit "/lib/framework/daemon";

#define PERSIST_RETRY_SECONDS 5
#define PERSIST_RETRY_LIMIT 3
/* L-168：每个 scope 保留的幂等收据上限（仅回收已终结且未被 pending 引用的记录）。 */
#define MAX_SCOPE_RECEIPTS 128

mapping _state_buckets;
/* 世界状态代次只在候选持久化成功后对外发布。 */
int _generation;
nosave int _persisted_generation;
nosave int _persist_pending;
nosave int _pending_generation;
nosave string _pending_operation_id;
nosave string _last_persist_result;
nosave string _last_persist_error;
nosave mapping _last_world_save_phase;
nosave mapping _pending_state_buckets;
nosave mapping _pending_change;
nosave mapping _synthetic_fail_scopes;
nosave mapping _pending_scope_entries;
nosave mapping _scope_generations;
nosave mapping _persisted_scope_generations;
nosave mapping _legacy_scope_keys;
nosave int _pending_sequence;
nosave int _persist_retry_count;
nosave int _persist_retry_handle;
nosave int _operation_seq;
nosave int _legacy_snapshot_valid;

int set_scope_counter_monotonic(string scope_key, string counter_key, int value);
int set_scope_flag_with_operation(string scope_key, string flag_key, mixed value,
    string operation_id);
int set_scope_flags_with_operation(string scope_key, mapping flags,
    string operation_id);
int add_scope_counter_with_operation(string scope_key, string counter_key,
    int delta, string operation_id);
int save_world_state();
private void retry_pending_persist();
private mapping new_bucket(string scope_key);
private mapping candidate_bucket(mapping state, string scope_key);
private mixed deep_copy_value(mixed value);
private string next_operation_id(string prefix);
private mapping catch_owner_transaction(mixed txn_id)
{
    mixed err;
    mapping record;

    err = catch(record = OWNER_COMMIT_D->query_transaction(txn_id));
    return err ? 0 : (mapp(record) ? record : 0);
}

private void trim_scope_receipts(mapping bucket);
private mapping catch_owner_transaction(mixed txn_id);
private int pending_candidate_may_publish(mapping entry);
private void schedule_persist_retry();
private int persist_current(string operation_id, mapping change);
private string query_scope_save_file(string scope_key);
private string *query_persist_scope_keys(mapping change);
private int persist_scope(string scope_key, string operation_id);
int synthetic_fail_next_scope_persist(string scope_key);
private int persist_candidate(mapping candidate, int generation,
    string operation_id, mapping change, mixed txn_id);
private int publish_candidate(mapping candidate, int generation,
    string operation_id, mapping change, mixed txn_id);
private string pending_scope_key(mapping change);
private void refresh_pending_status();
private void retain_pending_scope(mapping candidate, int generation,
    string operation_id, mapping change, mixed txn_id);
private mapping build_pending_candidate(mapping entry);
private void stamp_candidate_scope_generation(mapping candidate,
    mapping change, int generation);

private mixed deep_copy_value(mixed value)
{
    mixed copied;
    mixed err;

    err = catch(copied = restore_variable(save_variable(value)));
    return err ? 0 : copied;
}

private string query_save_file()
{
    return DATA_DIR + "runtime/world_state_d";
}

/* L-168：scope 收据必须有界。只回收**已终结**（不属于任何 pending 候选）
 * 的最旧记录；pending 引用的 operation_id 永不回收，避免失去幂等判定依据。 */

private int pending_candidate_may_publish(mapping entry)
{
    mapping txn;
    mixed txn_id;
    string state;

    if (!mapp(entry))
        return 0;
    txn_id = entry["txn_id"];
    /* 0/empty means this candidate was not created by an owner
     * transaction (for example, a direct persistence retry). */
    if ((intp(txn_id) && txn_id <= 0)
        || (stringp(txn_id) && txn_id == "")
        || (!intp(txn_id) && !stringp(txn_id)))
        return 1;
    txn = catch_owner_transaction(txn_id);
    if (!mapp(txn))
        /* 事务终态记录已被淘汰：无法证明允许提交 → 不发布。 */
        return 0;
    state = stringp(txn["state"]) ? txn["state"] : "";
    return state == "committed" || state == "prepared" || state == "applied"
        || state == "";
}

private void trim_scope_receipts(mapping bucket)
{
    mapping receipts;
    mapping pins;
    string *ids;
    string *trim_ids;
    string id;
    int n;
    int index;

    if (!mapp(bucket) || !mapp(bucket["operation_receipts"]))
        return;
    receipts = bucket["operation_receipts"];
    n = sizeof(receipts);
    if (n <= MAX_SCOPE_RECEIPTS)
        return;

    pins = ([ ]);
    if (mapp(_pending_scope_entries)) {
        foreach (id in keys(_pending_scope_entries)) {
            if (mapp(_pending_scope_entries[id])
                && stringp(_pending_scope_entries[id]["operation_id"]))
                pins[_pending_scope_entries[id]["operation_id"]] = 1;
        }
    }
    ids = sort_array(keys(receipts), "sort_receipts_by_time", this_object(), receipts);
    trim_ids = ({ });
    foreach (id in ids) {
        if (pins[id])
            continue;
        trim_ids += ({ id });
    }
    index = 0;
    while (n > MAX_SCOPE_RECEIPTS && index < sizeof(trim_ids)) {
        map_delete(receipts, trim_ids[index]);
        index++;
        n--;
    }
}

int sort_receipts_by_time(string a, string b, mapping receipts)
{
    int ta;
    int tb;

    ta = mapp(receipts[a]) && intp(receipts[a]["committed_at"])
        ? receipts[a]["committed_at"] : 0;
    tb = mapp(receipts[b]) && intp(receipts[b]["committed_at"])
        ? receipts[b]["committed_at"] : 0;
    return ta < tb ? -1 : (ta > tb ? 1 : 0);
}

private mapping new_bucket(string scope_key)
{
    return ([
        "scope_key": scope_key,
        "version": 1,
        "generation": 0,
        "flags": ([ ]),
        "counters": ([ ]),
        /* Durable receipts make a replay of the same scope operation a
         * duplicate instead of a second counter increment. */
        "operation_receipts": ([ ]),
        "updated_at": time(),
    ]);
}

private mapping candidate_bucket(mapping state, string scope_key)
{
    mapping bucket;

    if (!mapp(state) || !stringp(scope_key) || scope_key == "")
        return 0;
    if (!mapp(state[scope_key]))
        state[scope_key] = new_bucket(scope_key);
    bucket = state[scope_key];
    if (!mapp(bucket["flags"]))
        bucket["flags"] = ([ ]);
    if (!mapp(bucket["counters"]))
        bucket["counters"] = ([ ]);
    if (!mapp(bucket["operation_receipts"]))
        bucket["operation_receipts"] = ([ ]);
    return bucket;
}

private mapping query_existing_bucket(string scope_key)
{
    if (!stringp(scope_key) || scope_key == "" || !mapp(_state_buckets)
        || !mapp(_state_buckets[scope_key]))
        return 0;
    return _state_buckets[scope_key];
}

private string pending_scope_key(mapping change)
{
    if (mapp(change) && change["kind"] == "snapshot")
        return "__snapshot__";
    if (mapp(change) && stringp(change["scope_key"])
        && change["scope_key"] != "")
        return change["scope_key"];
    return "__world_state__";
}

private void refresh_pending_status()
{
    string key;
    mapping entry;
    mapping first;

    _persist_pending = mapp(_pending_scope_entries)
        && sizeof(_pending_scope_entries) > 0;
    if (!_persist_pending) {
        _pending_generation = 0;
        _pending_operation_id = "";
        _pending_state_buckets = 0;
        _pending_change = 0;
        _persist_retry_count = 0;
        return;
    }
    foreach (key, entry in _pending_scope_entries) {
        if (!mapp(entry))
            continue;
        if (!mapp(first) || entry["sequence"] < first["sequence"]) {
            first = entry;
        }
    }
    if (!mapp(first)) {
        _pending_scope_entries = ([]);
        refresh_pending_status();
        return;
    }
    _pending_generation = intp(first["generation"])
        ? first["generation"] : 0;
    _pending_operation_id = stringp(first["operation_id"])
        ? first["operation_id"] : "";
    _pending_change = mapp(first["change"]) ? copy(first["change"]) : 0;
    _persist_retry_count = intp(first["retry_count"])
        ? first["retry_count"] : 0;
    _pending_state_buckets = mapp(first["candidate"])
        ? copy(first["candidate"]) : 0;
}

private void retain_pending_scope(mapping candidate, int generation,
    string operation_id, mapping change, mixed txn_id)
{
    mapping entry;
    mapping buckets;
    string key;
    string scope_key;

    if (!mapp(_pending_scope_entries))
        _pending_scope_entries = ([]);
    key = pending_scope_key(change);
    entry = ([
        "sequence": ++_pending_sequence,
        "generation": generation,
        "operation_id": operation_id,
        "change": mapp(change) ? copy(change) : ([ ]),
        "retry_count": 0,
        /* L-09：记录发起该候选的 owner 事务身份，重试发布前用它做终态围栏。 */
        "txn_id": txn_id,
    ]);
    if (mapp(change) && change["kind"] == "snapshot") {
        entry["mode"] = "snapshot";
        entry["candidate"] = deep_copy_value(candidate);
    } else if (mapp(change) && arrayp(change["scope_keys"])) {
        entry["mode"] = "scopes";
        entry["scope_keys"] = change["scope_keys"] + ({});
        entry["removed_scopes"] = change["kind"] == "purge"
            ? change["scope_keys"] + ({}) : ({ });
        buckets = ([]);
        foreach (scope_key in change["scope_keys"]) {
            if (mapp(candidate[scope_key]))
                buckets[scope_key] = deep_copy_value(candidate[scope_key]);
        }
        entry["buckets"] = buckets;
    } else {
        entry["mode"] = "scope";
        scope_key = mapp(change) ? change["scope_key"] : "";
        entry["scope_key"] = stringp(scope_key) ? scope_key : "";
        entry["bucket"] = stringp(scope_key) && mapp(candidate[scope_key])
            ? deep_copy_value(candidate[scope_key]) : 0;
    }
    _pending_scope_entries[key] = entry;
    refresh_pending_status();
}

private mapping build_pending_candidate(mapping entry)
{
    mapping candidate;
    mapping buckets;
    mapping bucket;
    string scope_key;

    if (!mapp(entry))
        return 0;
    if (entry["mode"] == "snapshot")
        return deep_copy_value(_state_buckets || ([ ]));
    candidate = deep_copy_value(_state_buckets || ([ ]));
    if (!mapp(candidate))
        return 0;
    if (entry["mode"] == "scopes") {
        foreach (scope_key in entry["removed_scopes"] || ({ }))
            map_delete(candidate, scope_key);
        buckets = entry["buckets"];
        if (mapp(buckets)) {
            foreach (scope_key, bucket in buckets)
                candidate[scope_key] = deep_copy_value(bucket);
        }
        return candidate;
    }
    scope_key = entry["scope_key"];
    if (!stringp(scope_key) || scope_key == "")
        return candidate;
    if (mapp(entry["bucket"]))
        candidate[scope_key] = deep_copy_value(entry["bucket"]);
    else
        map_delete(candidate, scope_key);
    return candidate;
}

private void stamp_candidate_scope_generation(mapping candidate,
    mapping change, int generation)
{
    string scope_key;

    if (!mapp(candidate) || !mapp(change) || change["kind"] == "snapshot")
        return;
    if (arrayp(change["scope_keys"])) {
        foreach (scope_key in change["scope_keys"])
            if (mapp(candidate[scope_key]))
                candidate[scope_key]["generation"] = generation;
        return;
    }
    scope_key = change["scope_key"];
    if (stringp(scope_key) && mapp(candidate[scope_key]))
        candidate[scope_key]["generation"] = generation;
}

private string next_operation_id(string prefix)
{
    _operation_seq++;
    return sprintf("world-state/%s/%d/%d", prefix, _generation,
        _operation_seq);
}

private string query_scope_save_file(string scope_key)
{
    return DATA_DIR + "runtime/world_state_scope_"
        + hash("sha256", scope_key);
}

private string *query_persist_scope_keys(mapping change)
{
    string *scope_keys;
    string scope_key;

    scope_keys = ({ });
    if (!mapp(change) || change["kind"] == "snapshot") {
        foreach (scope_key in keys(_state_buckets || ([ ])))
            scope_keys += ({ scope_key });
        foreach (scope_key in keys(_legacy_scope_keys || ([ ])))
            if (member_array(scope_key, scope_keys) == -1)
                scope_keys += ({ scope_key });
        return scope_keys;
    }
    if (arrayp(change["scope_keys"])) {
        foreach (scope_key in change["scope_keys"])
            if (stringp(scope_key) && scope_key != ""
                && member_array(scope_key, scope_keys) == -1)
                scope_keys += ({ scope_key });
        return scope_keys;
    }
    scope_key = change["scope_key"];
    if (stringp(scope_key) && scope_key != "")
        scope_keys += ({ scope_key });
    return scope_keys;
}

private int persist_scope(string scope_key, string operation_id)
{
    mapping previous_state;
    mapping scope_state;
    mapping payload;
    string file;
    string scope_operation;
    mixed err;
    mapping detail;

    if (!stringp(scope_key) || scope_key == "") {
        _last_world_save_phase = ([
            "outcome": "not_written", "persisted": 0,
            "save_result": 0, "error": "scope key is invalid",
        ]);
        return 0;
    }
    scope_state = mapp(_state_buckets[scope_key])
        ? deep_copy_value(_state_buckets[scope_key]) : ([
            "scope_key": scope_key,
            "version": 1,
            "generation": _generation,
            "deleted": 1,
        ]);
    if (!mapp(scope_state)) {
        _last_world_save_phase = ([
            "outcome": "not_written", "persisted": 0,
            "save_result": 0, "error": "scope snapshot is not serializable",
        ]);
        _last_persist_result = "not_written";
        _last_persist_error = "scope snapshot is not serializable";
        return 0;
    }
    previous_state = _state_buckets;
    _state_buckets = ([ scope_key: scope_state ]);
    file = query_scope_save_file(scope_key);
    scope_operation = sprintf("%s/%s", operation_id,
        hash("sha256", scope_key));
    payload = ([
        "scope_key": scope_key,
        "scope_generation": intp(scope_state["generation"])
            ? scope_state["generation"] : _generation,
        "deleted": scope_state["deleted"] ? 1 : 0,
        "operation_id": operation_id,
    ]);
    if (mapp(_synthetic_fail_scopes)
        && _synthetic_fail_scopes[scope_key]) {
        map_delete(_synthetic_fail_scopes, scope_key);
        _state_buckets = previous_state;
        _last_world_save_phase = ([
            "outcome": "not_written", "persisted": 0,
            "save_result": 0,
            "error": "synthetic world-state persistence failure",
        ]);
        _last_persist_result = "not_written";
        _last_persist_error = "synthetic world-state persistence failure";
        return 0;
    }
    err = catch(detail = owner_save_object_phases(file,
        "owner/world/scope/" + scope_key, "world_state_scope", payload,
        "world_state/scope/" + scope_operation));
    _state_buckets = previous_state;
    _last_world_save_phase = err || !mapp(detail)
        ? ([ "outcome": "unknown", "persisted": -1, "save_result": 0,
            "error": err ? sprintf("%O", err) : "world state save unavailable" ])
        : copy(detail);
    if (!mapp(detail) || detail["persisted"] != 1) {
        _last_persist_result = _last_world_save_phase["outcome"];
        _last_persist_error = _last_world_save_phase["error"] || "";
        return 0;
    }
    _last_persist_result = detail["outcome"] || "unknown";
    _last_persist_error = detail["error"] || "";
    return detail["ok"] == 1 ? 1 : 2;
}

void restore_world_state()
{
    string file;
    string runtime_dir;
    string *entries;
    string entry_name;
    string suffix;
    string scope_key;
    mapping legacy_buckets;
    mapping merged_buckets;
    mapping shard_buckets;
    mapping bucket;
    mapping existing_bucket;
    mixed err;
    int legacy_generation;
    int shard_generation;
    int bucket_generation;
    int existing_generation;
    int max_generation;
    int operation_seq;

    file = query_save_file();
    runtime_dir = DATA_DIR + "runtime";
    _pending_state_buckets = 0;
    _pending_change = 0;
    _pending_scope_entries = ([ ]);
    _scope_generations = ([ ]);
    _persisted_scope_generations = ([ ]);
    _legacy_scope_keys = ([ ]);
    _legacy_snapshot_valid = 0;
    _pending_sequence = 0;
    _persist_pending = 0;
    _pending_generation = 0;
    _pending_operation_id = "";
    _persist_retry_count = 0;
    _persist_retry_handle = 0;
    _last_persist_result = "not_attempted";
    _last_persist_error = "";
    _synthetic_fail_scopes = ([ ]);
    _state_buckets = ([ ]);
    _generation = 0;
    operation_seq = 0;
    legacy_buckets = ([ ]);

    /* 兼容旧版单文件快照；只要它存在，第一次新的成功保存会把
     * 所有旧 scope 连同删除 tombstone 一并迁移到分片文件。 */
    if (file_size(file + ".o") > 0) {
        err = catch(restore_object(file));
        if (err || !mapp(_state_buckets)) {
            _state_buckets = ([ ]);
            _generation = 0;
            _last_persist_result = "restore_failed";
            _last_persist_error = err ? sprintf("%O", err)
                : "legacy world state snapshot is invalid";
        } else {
            legacy_buckets = deep_copy_value(_state_buckets);
            if (!mapp(legacy_buckets))
                legacy_buckets = ([ ]);
            _legacy_snapshot_valid = 1;
            foreach (scope_key in keys(legacy_buckets))
                _legacy_scope_keys[scope_key] = 1;
            legacy_generation = intp(_generation) && _generation >= 0
                ? _generation : 0;
            operation_seq = intp(_operation_seq) && _operation_seq >= 0
                ? _operation_seq : 0;
        }
    }

    merged_buckets = legacy_buckets;
    max_generation = legacy_generation;
    entries = get_dir(runtime_dir + "/");
    if (arrayp(entries)) {
        foreach (entry_name in entries) {
            if (!stringp(entry_name)
                || sscanf(entry_name, "world_state_scope_%s.o", suffix) != 1
                || file_size(runtime_dir + "/" + entry_name) <= 0)
                continue;
            _state_buckets = ([ ]);
            err = catch(restore_object(runtime_dir + "/world_state_scope_" + suffix));
            if (err || !mapp(_state_buckets))
                continue;
            shard_buckets = _state_buckets;
            shard_generation = intp(_generation) && _generation >= 0
                ? _generation : 0;
            if (shard_generation > max_generation)
                max_generation = shard_generation;
            foreach (scope_key, bucket in shard_buckets) {
                if (!stringp(scope_key) || !mapp(bucket))
                    continue;
                bucket_generation = intp(bucket["generation"])
                    && bucket["generation"] >= 0
                        ? bucket["generation"] : shard_generation;
                existing_bucket = merged_buckets[scope_key];
                existing_generation = mapp(existing_bucket)
                    && intp(existing_bucket["generation"])
                    && existing_bucket["generation"] >= 0
                        ? existing_bucket["generation"] : legacy_generation;
                if (bucket_generation < existing_generation)
                    continue;
                if (bucket["deleted"]) {
                    map_delete(merged_buckets, scope_key);
                    continue;
                }
                merged_buckets[scope_key] = deep_copy_value(bucket);
            }
        }
    }

    _state_buckets = mapp(merged_buckets) ? merged_buckets : ([ ]);
    _generation = max_generation;
    _operation_seq = operation_seq > _operation_seq
        ? operation_seq : _operation_seq;
    if (!intp(_generation) || _generation < 0)
        _generation = 0;
    foreach (scope_key, bucket in _state_buckets) {
        if (!mapp(bucket))
            continue;
        if (!intp(bucket["generation"]) || bucket["generation"] < 0)
            bucket["generation"] = _generation;
        _scope_generations[scope_key] = bucket["generation"];
        _persisted_scope_generations[scope_key] = bucket["generation"];
    }
    _persisted_generation = _generation;
    refresh_pending_status();
}

/* Reload one scope shard for synthetic restart probes without resetting the
 * live daemon's unrelated scopes or pending recovery state. */
int restore_scope_from_disk(string scope_key)
{
    mapping previous_state;
    mapping candidate;
    mapping shard_state;
    string file;
    mixed err;
    int previous_generation;
    int previous_operation_seq;
    int shard_generation;

    if (!stringp(scope_key) || scope_key == "")
        return 0;
    file = query_scope_save_file(scope_key) + ".o";
    if (file_size(file) <= 0)
        return 0;
    previous_state = _state_buckets;
    previous_generation = _generation;
    previous_operation_seq = _operation_seq;
    _state_buckets = ([ ]);
    _generation = 0;
    _operation_seq = 0;
    err = catch(restore_object(file));
    shard_state = !err && mapp(_state_buckets)
        && mapp(_state_buckets[scope_key])
            ? deep_copy_value(_state_buckets[scope_key]) : 0;
    _state_buckets = previous_state;
    _generation = previous_generation;
    _operation_seq = previous_operation_seq;
    if (err || !mapp(shard_state))
        return 0;

    candidate = deep_copy_value(previous_state || ([ ]));
    if (!mapp(candidate))
        candidate = ([ ]);
    if (shard_state["deleted"])
        map_delete(candidate, scope_key);
    else
        candidate[scope_key] = shard_state;
    _state_buckets = candidate;
    if (shard_state["deleted"]) {
        map_delete(_scope_generations, scope_key);
        map_delete(_persisted_scope_generations, scope_key);
    } else {
        shard_generation = intp(shard_state["generation"])
            && shard_state["generation"] >= 0
                ? shard_state["generation"] : previous_generation;
        _scope_generations[scope_key] = shard_generation;
        _persisted_scope_generations[scope_key] = shard_generation;
        if (shard_generation > _generation)
            _generation = shard_generation;
    }
    return 1;
}

private void schedule_persist_retry()
{
    mapping entry;
    int retryable;

    if (_persist_retry_handle || !_persist_pending)
        return;
    foreach (entry in values(_pending_scope_entries || ([ ]))) {
        if (mapp(entry) && (!intp(entry["retry_count"])
            || entry["retry_count"] < PERSIST_RETRY_LIMIT)) {
            retryable = 1;
            break;
        }
    }
    if (!retryable)
        return;
    _persist_retry_handle = call_out("retry_pending_persist", PERSIST_RETRY_SECONDS);
}

private int persist_current(string operation_id, mapping change)
{
    string file;
    string runtime_dir;
    string *scope_keys;
    string scope_key;
    mixed err;
    int saved;
    int scope_result;
    int pending_phase;
    int migrating_legacy;

    if (!mapp(_state_buckets))
        _state_buckets = ([ ]);
    runtime_dir = DATA_DIR + "runtime";
    if (file_size(runtime_dir) == -1 && !mkdir(runtime_dir)) {
        _last_persist_result = "failed";
        _last_persist_error = "runtime directory creation failed";
        return 0;
    }

    /* A legacy global file is read-only compatibility input.  Migrate the
     * complete candidate before removing it, including tombstones for scopes
     * that disappeared since the legacy snapshot. */
    file = query_save_file();
    migrating_legacy = file_size(file + ".o") > 0 && _legacy_snapshot_valid;
    scope_keys = query_persist_scope_keys(
        migrating_legacy ? ([ "kind": "snapshot" ]) : change);
    if (migrating_legacy)
        foreach (scope_key in keys(_legacy_scope_keys || ([ ])))
            if (member_array(scope_key, scope_keys) == -1)
                scope_keys += ({ scope_key });

    pending_phase = 0;
    foreach (scope_key in scope_keys) {
        scope_result = persist_scope(scope_key, operation_id);
        if (scope_result == 1)
            continue;
        if (scope_result == 2) {
            pending_phase = 1;
            continue;
        }
        if (pending_phase) {
            _last_persist_result = "unknown";
            _last_persist_error = "world state has mixed persistence phases";
        } else {
            _last_persist_result = mapp(_last_world_save_phase)
                ? _last_world_save_phase["outcome"] : "unknown";
            _last_persist_error = mapp(_last_world_save_phase)
                ? (_last_world_save_phase["error"] || "")
                : "world state scope persistence failed";
        }
        return 0;
    }

    /* A scope with a durable file but an unfinished receipt remains part of
     * the same candidate. Do not migrate/delete the legacy aggregate or
     * report the whole world candidate as committed yet. */
    if (pending_phase) {
        _last_persist_result = "persisted_receipt_pending";
        _last_persist_error = "world state scope receipt pending";
        return 2;
    }

    if (migrating_legacy) {
        err = catch(saved = rm(file + ".o"));
        if (err || !saved) {
            _last_persist_result = "failed";
            _last_persist_error = err ? sprintf("%O", err)
                : "legacy world state snapshot cleanup failed";
            return 0;
        }
        _legacy_scope_keys = ([ ]);
    }

    _persisted_generation = _generation;
    _last_persist_result = "success";
    _last_persist_error = "";
    return 1;
}

private void mark_persisted_change(mapping candidate, mapping change,
    int generation)
{
    mapping bucket;
    string scope_key;

    if (!mapp(_scope_generations))
        _scope_generations = ([ ]);
    if (!mapp(_persisted_scope_generations))
        _persisted_scope_generations = ([ ]);
    if (!mapp(change) || change["kind"] == "snapshot") {
        foreach (scope_key, bucket in candidate || ([])) {
            if (mapp(bucket)) {
                _scope_generations[scope_key] =
                    intp(bucket["generation"])
                        ? bucket["generation"] : generation;
                _persisted_scope_generations[scope_key] =
                    _scope_generations[scope_key];
            }
        }
        return;
    }
    if (arrayp(change["scope_keys"])) {
        foreach (scope_key in change["scope_keys"]) {
            if (mapp(candidate[scope_key])) {
                _scope_generations[scope_key] = generation;
                _persisted_scope_generations[scope_key] = generation;
            } else {
                map_delete(_scope_generations, scope_key);
                map_delete(_persisted_scope_generations, scope_key);
            }
        }
        return;
    }
    scope_key = change["scope_key"];
    if (stringp(scope_key) && scope_key != "") {
        _scope_generations[scope_key] = generation;
        _persisted_scope_generations[scope_key] = generation;
    }
}

private int persist_candidate(mapping candidate, int generation,
    string operation_id, mapping change, mixed txn_id)
{
    mapping previous_state;
    int previous_generation;
    int saved;
    string key;

    if (!mapp(candidate) || generation <= _generation)
        return 0;
    key = pending_scope_key(change);
    previous_state = _state_buckets;
    previous_generation = _generation;
    _state_buckets = candidate;
    _generation = generation;
    saved = persist_current(operation_id, change);
    if (saved == 1) {
        mark_persisted_change(candidate, change, generation);
        if (mapp(_pending_scope_entries))
            map_delete(_pending_scope_entries, key);
        refresh_pending_status();
        return 1;
    }

    /* A candidate whose file was written or whose publication is unknown must
     * remain the live recovery candidate. Only an explicit not_written phase
     * restores the previous read model. */
    if (stringp(_last_persist_result)
        && _last_persist_result != "not_written") {
        retain_pending_scope(candidate, generation, operation_id, change, txn_id);
        schedule_persist_retry();
        return 0;
    }
    _state_buckets = previous_state;
    _generation = previous_generation;
    retain_pending_scope(candidate, generation, operation_id, change, txn_id);
    schedule_persist_retry();
    return 0;
}

private int publish_candidate(mapping candidate, int generation,
    string operation_id, mapping change, mixed txn_id)
{
    mapping event_payload;

    stamp_candidate_scope_generation(candidate, change, generation);
    if (!persist_candidate(candidate, generation, operation_id, change, txn_id))
        return 0;
    if (mapp(change)) {
        event_payload = deep_copy_value(change);
        if (mapp(event_payload)) {
            event_payload["generation"] = generation;
            event_payload["persisted"] = 1;
            event_payload["operation_id"] = operation_id;
            event_payload["timestamp"] = time();
            EVENT_D->emit(EVENT_WORLD_STATE_CHANGED, event_payload);
        }
    }
    return 1;
}

private void retry_pending_persist()
{
    mapping previous_state;
    mapping candidate;
    mapping change;
    mapping entry;
    mapping selected;
    string key;
    string selected_key;
    string operation_id;
    int previous_generation;
    int generation;
    int saved;
    int retry_number;

    _persist_retry_handle = 0;
    if (!mapp(_pending_scope_entries) || sizeof(_pending_scope_entries) == 0)
        return;
    foreach (key, entry in _pending_scope_entries) {
        if (!mapp(entry) || (intp(entry["retry_count"])
            && entry["retry_count"] >= PERSIST_RETRY_LIMIT))
            continue;
        if (!mapp(selected) || entry["sequence"] < selected["sequence"]) {
            selected = entry;
            selected_key = key;
        }
    }
    if (!mapp(selected) || selected_key == "") {
        refresh_pending_status();
        return;
    }
    /* L-09：候选属于某个 owner 事务时，发布前必须确认该事务仍允许提交。
     * 事务已被拒绝/回滚，或终态记录已被淘汰无法证明 → 放弃发布并把候选标为
     * 终态 rejected_after_transaction（保留可审计记录），绝不稍后生效。 */
    if (!pending_candidate_may_publish(selected)) {
        selected["state"] = "rejected_after_transaction";
        selected["rejected_at"] = time();
        selected["retry_count"] = PERSIST_RETRY_LIMIT;
        _pending_scope_entries[selected_key] = selected;
        refresh_pending_status();
        if (sizeof(_pending_scope_entries) > 0)
            schedule_persist_retry();
        return;
    }

    candidate = build_pending_candidate(selected);
    if (!mapp(candidate)) {
        selected["retry_count"] = selected["retry_count"] + 1;
        _pending_scope_entries[selected_key] = selected;
        refresh_pending_status();
        schedule_persist_retry();
        return;
    }
    change = mapp(selected["change"]) ? copy(selected["change"]) : 0;
    operation_id = selected["operation_id"];
    generation = _generation + 1;
    stamp_candidate_scope_generation(candidate, change, generation);
    previous_state = _state_buckets;
    previous_generation = _generation;
    _state_buckets = candidate;
    _generation = generation;
    retry_number = (intp(selected["retry_count"]) ? selected["retry_count"] : 0) + 1;
    selected["retry_count"] = retry_number;
    saved = persist_current(operation_id, change);
    if (saved == 1) {
        mark_persisted_change(candidate, change, generation);
        map_delete(_pending_scope_entries, selected_key);
        refresh_pending_status();
        if (mapp(change)) {
            change = deep_copy_value(change);
            if (!mapp(change)) {
                /* 事件载荷复制失败不能吞掉重试调度。 */
                if (sizeof(_pending_scope_entries) > 0)
                    schedule_persist_retry();
                return;
            }
            change["generation"] = generation;
            change["persisted"] = 1;
            change["recovered"] = 1;
            change["operation_id"] = operation_id;
            change["timestamp"] = time();
            EVENT_D->emit(EVENT_WORLD_STATE_CHANGED, change);
        }
        /* L-08：成功一项后必须继续调度剩余 pending，否则其它 scope 的候选
         * 只会在下一次无关触发时才重试（旧实现直接 return）。 */
        if (sizeof(_pending_scope_entries) > 0)
            schedule_persist_retry();
        return;
    }
    if (stringp(_last_persist_result)
        && _last_persist_result != "not_written") {
        /* Keep the candidate visible and retryable after a durable or unknown
         * publication. It must not be reverted as if no file was touched. */
        _pending_scope_entries[selected_key] = selected;
        refresh_pending_status();
        schedule_persist_retry();
        return;
    }
    _state_buckets = previous_state;
    _generation = previous_generation;
    _pending_scope_entries[selected_key] = selected;
    refresh_pending_status();
    schedule_persist_retry();
}

int save_world_state()
{
    string operation_id;
    mapping candidate;
    mapping change;
    int saved;

    if (_persist_pending) {
        retry_pending_persist();
        return !_persist_pending;
    }
    operation_id = next_operation_id("snapshot");
    change = ([
        "scope_key": "world/global",
        "kind": "snapshot",
    ]);
    saved = persist_current(operation_id, change);
    if (saved == 1)
        mark_persisted_change(_state_buckets, change, _generation);
    else {
        candidate = deep_copy_value(_state_buckets);
        retain_pending_scope(mapp(candidate) ? candidate : ([ ]),
            _generation, operation_id, change, 0);
        schedule_persist_retry();
    }
    return saved;
}

private int is_player_scope_key(string scope_key, string player_id)
{
    string *parts;

    if (!stringp(scope_key) || !stringp(player_id))
        return 0;
    parts = explode(scope_key, ":");
    if (sizeof(parts) == 3 && parts[0] == "player" && parts[1] == player_id)
        return 1;
    return sizeof(parts) == 6 && parts[0] == "instance"
        && parts[2] == "player" && parts[3] == player_id
        && parts[4] == "region";
}

int purge_player_scopes(string player_id)
{
    mapping candidate;
    string scope_key;
    string *scope_keys;
    int removed;
    int all_ok;

    if (!stringp(player_id) || player_id == "" || strsrch(player_id, ":") != -1)
        return 0;
    scope_keys = keys(copy(_state_buckets || ([ ])));
    all_ok = 1;
    foreach (scope_key in scope_keys) {
        if (!is_player_scope_key(scope_key, player_id))
            continue;
        candidate = deep_copy_value(_state_buckets || ([ ]));
        if (!mapp(candidate)) {
            all_ok = 0;
            continue;
        }
        map_delete(candidate, scope_key);
        if (!publish_candidate(candidate, _generation + 1,
            next_operation_id("purge"), ([
                "scope_key": scope_key,
                "scope_keys": ({ scope_key }),
                "kind": "purge",
                "removed": 1,
            ]), 0)) {
            all_ok = 0;
            continue;
        }
        removed++;
    }
    return removed > 0 && all_ok;
}

/* Synthetic/instance cleanup uses the same scope tombstone path as player
 * cleanup. It is intentionally narrow: only an exact stable scope key is
 * accepted, so callers cannot purge a prefix or another scope by accident. */
int synthetic_fail_next_scope_persist(string scope_key)
{
    if (!stringp(scope_key)
        || strsrch(scope_key, "shared:synthetic_") != 0)
        return 0;
    if (!mapp(_synthetic_fail_scopes))
        _synthetic_fail_scopes = ([ ]);
    _synthetic_fail_scopes[scope_key] = 1;
    return 1;
}

int clear_synthetic_scope(string scope_key)
{
    string file;
    string key;
    mapping entry;
    mixed err;

    if (!stringp(scope_key) || strsrch(scope_key, "shared:synthetic_") != 0)
        return 0;
    if (mapp(_state_buckets) && mapp(_state_buckets[scope_key]))
        return 0;
    foreach (key, entry in _pending_scope_entries || ([ ])) {
        if (!mapp(entry))
            continue;
        if ((entry["mode"] == "scope" && entry["scope_key"] == scope_key)
            || (member_array(scope_key, entry["scope_keys"] || ({ })) != -1))
            map_delete(_pending_scope_entries, key);
    }
    refresh_pending_status();
    if (!_persist_pending && _persist_retry_handle) {
        remove_call_out(_persist_retry_handle);
        _persist_retry_handle = 0;
    }
    map_delete(_synthetic_fail_scopes, scope_key);
    map_delete(_scope_generations, scope_key);
    map_delete(_persisted_scope_generations, scope_key);
    map_delete(_legacy_scope_keys, scope_key);
    file = query_scope_save_file(scope_key) + ".o";
    err = file_size(file) > 0 ? catch(rm(file)) : 0;
    return !err;
}

int purge_scope(string scope_key)
{
    mapping candidate;

    if (!stringp(scope_key) || scope_key == "")
        return 0;
    candidate = deep_copy_value(_state_buckets || ([ ]));
    if (!mapp(candidate))
        return 0;
    if (undefinedp(candidate[scope_key]))
        return 1;
    map_delete(candidate, scope_key);
    return publish_candidate(candidate, _generation + 1,
        next_operation_id("purge_scope"), ([
            "scope_key": scope_key,
            "scope_keys": ({ scope_key }),
            "kind": "purge",
            "removed": 1,
        ]), 0);
}

void create()
{
    ::create();
    restore_world_state();
}

mapping query_scope_state(string scope_key)
{
    mapping bucket;

    if (!stringp(scope_key) || scope_key == "")
        return ([ "present": 0 ]);
    bucket = query_existing_bucket(scope_key);
    if (!mapp(bucket))
        return ([ "present": 0, "scope_key": scope_key ]);
    bucket = deep_copy_value(bucket);
    bucket["present"] = 1;
    bucket["scope_generation"] = intp(_scope_generations[scope_key])
        ? _scope_generations[scope_key] : 0;
    bucket["persisted_scope_generation"] =
        intp(_persisted_scope_generations[scope_key])
            ? _persisted_scope_generations[scope_key] : 0;
    return bucket;
}

int query_scope_generation(string scope_key)
{
    return stringp(scope_key) && intp(_scope_generations[scope_key])
        ? _scope_generations[scope_key] : 0;
}

mapping query_scope_persistence_status(string scope_key)
{
    mapping entry;
    string key;
    int pending;

    if (!stringp(scope_key) || scope_key == "")
        return ([ "ok": 0, "scope_key": scope_key || "" ]);
    foreach (key, entry in _pending_scope_entries || ([ ])) {
        if (!mapp(entry))
            continue;
        if ((entry["mode"] == "scope" && entry["scope_key"] == scope_key)
            || (entry["mode"] == "scopes"
                && member_array(scope_key, entry["scope_keys"] || ({ })) != -1)
            || entry["mode"] == "snapshot") {
            pending = 1;
            break;
        }
    }
    return ([
        "ok": 1,
        "scope_key": scope_key,
        "generation": query_scope_generation(scope_key),
        "persisted_generation": intp(_persisted_scope_generations[scope_key])
            ? _persisted_scope_generations[scope_key] : 0,
        "pending": pending,
        "state": pending ? "pending"
            : (query_scope_generation(scope_key) > 0 ? "persisted" : "absent"),
    ]);
}

mapping query_all_scope_states()
{
    return mapp(_state_buckets) ? deep_copy_value(_state_buckets) : ([ ]);
}

/* Atomic world-scope mutation. Flags and counter deltas are prepared against
 * one candidate and published once. The operation receipt is part of that
 * same candidate, so retrying a committed operation is durable and idempotent. */
varargs mapping apply_scope_mutation(string scope_key, mapping flags,
    mapping counter_deltas, string operation_id, mixed txn_id)
{
    mapping current_bucket;
    mapping candidate;
    mapping bucket;
    mapping receipt;
    mapping before_bucket;
    mapping after_bucket;
    mapping change;
    string fingerprint;
    string key;
    int current;
    int value;

    if (!stringp(scope_key) || scope_key == ""
        || (!mapp(flags) && !mapp(counter_deltas))
        || !stringp(operation_id) || operation_id == "")
        return ([ "ok": 0, "state": "invalid_args" ]);
    if (mapp(flags) && sizeof(flags) == 0
        && mapp(counter_deltas) && sizeof(counter_deltas) == 0)
        return ([ "ok": 0, "state": "invalid_args" ]);

    current_bucket = query_existing_bucket(scope_key);
    fingerprint = hash("sha256", sprintf("%O", ([
        "flags": mapp(flags) ? flags : ([ ]),
        "counter_deltas": mapp(counter_deltas) ? counter_deltas : ([ ]),
    ])));
    receipt = mapp(current_bucket) && mapp(current_bucket["operation_receipts"])
        ? current_bucket["operation_receipts"][operation_id] : 0;
    if (mapp(receipt)) {
        if (receipt["fingerprint"] != fingerprint)
            return ([ "ok": 0, "state": "param_mismatch",
                "operation_id": operation_id,
                "existing_fingerprint": receipt["fingerprint"],
                "incoming_fingerprint": fingerprint,
                "current_generation": query_scope_generation(scope_key) ]);
        return ([ "ok": 1, "duplicate": 1,
            "generation": query_scope_generation(scope_key),
            "operation_id": operation_id,
            "before_bucket": deep_copy_value(current_bucket),
            "after_bucket": deep_copy_value(current_bucket) ]);
    }

    before_bucket = mapp(current_bucket)
        ? deep_copy_value(current_bucket) : 0;
    candidate = deep_copy_value(_state_buckets || ([ ]));
    bucket = candidate_bucket(candidate, scope_key);
    if (!mapp(candidate) || !mapp(bucket))
        return ([ "ok": 0, "state": "candidate_failed" ]);
    if (mapp(flags)) {
        foreach (key in keys(flags)) {
            if (!stringp(key) || key == "")
                return ([ "ok": 0, "state": "invalid_flag_key" ]);
            bucket["flags"][key] = deep_copy_value(flags[key]);
        }
    }
    if (mapp(counter_deltas)) {
        foreach (key in keys(counter_deltas)) {
            if (!stringp(key) || key == "" || !intp(counter_deltas[key]))
                return ([ "ok": 0, "state": "invalid_counter_delta" ]);
            current = intp(bucket["counters"][key])
                ? bucket["counters"][key] : 0;
            value = current + counter_deltas[key];
            bucket["counters"][key] = value;
        }
    }
    bucket["operation_receipts"][operation_id] = ([
        "operation_id": operation_id,
        "fingerprint": fingerprint,
        "committed_generation": _generation + 1,
        "committed_at": time(),
    ]);
    trim_scope_receipts(bucket);
    bucket["updated_at"] = time();
    change = ([
        "scope_key": scope_key,
        "kind": "scope_mutation",
        "operation_id": operation_id,
        "flags": mapp(flags) ? keys(flags) : ({ }),
        "counter_keys": mapp(counter_deltas) ? keys(counter_deltas) : ({ }),
    ]);
    if (!publish_candidate(candidate, _generation + 1, operation_id, change, txn_id))
        return ([ "ok": 0, "state": "persist_failed",
            "operation_id": operation_id ]);
    after_bucket = deep_copy_value(_state_buckets[scope_key]);
    return ([ "ok": 1, "duplicate": 0,
        "generation": query_scope_generation(scope_key),
        "operation_id": operation_id,
        "before_bucket": before_bucket,
        "after_bucket": after_bucket ]);
}

/* Exact snapshot restore is only for the owner transaction compensation path.
 * The generation CAS prevents a rollback from overwriting a newer world write. */
varargs mapping apply_scope_mutation(string scope_key, mapping flags,
    mapping counter_deltas, string operation_id, mixed txn_id);
mapping restore_scope_snapshot(string scope_key, int expected_generation,
    mapping before_bucket, string operation_id)
{
    mapping current_bucket;
    mapping candidate;
    mapping change;

    if (!stringp(scope_key) || scope_key == "" || expected_generation < 0
        || !stringp(operation_id) || operation_id == "")
        return ([ "ok": 0, "state": "invalid_args" ]);
    current_bucket = query_existing_bucket(scope_key);
    if (!mapp(current_bucket)
        || current_bucket["generation"] != expected_generation)
        return ([ "ok": 0, "state": "generation_conflict",
            "scope_key": scope_key,
            "expected_generation": expected_generation,
            "actual_generation": mapp(current_bucket)
                ? current_bucket["generation"] : 0 ]);
    candidate = deep_copy_value(_state_buckets || ([ ]));
    if (!mapp(candidate))
        return ([ "ok": 0, "state": "candidate_failed" ]);
    if (mapp(before_bucket))
        candidate[scope_key] = deep_copy_value(before_bucket);
    else
        map_delete(candidate, scope_key);
    change = ([
        "scope_key": scope_key,
        "kind": "scope_restore",
        "operation_id": operation_id,
        "expected_generation": expected_generation,
    ]);
    if (!publish_candidate(candidate, _generation + 1, operation_id, change, 0))
        return ([ "ok": 0, "state": "persist_failed",
            "scope_key": scope_key ]);
    return ([ "ok": 1, "restored": 1,
        "generation": query_scope_generation(scope_key) ]);
}

int set_scope_flag(string scope_key, string flag_key, mixed value)
{
    return set_scope_flag_with_operation(scope_key, flag_key, value, 0);
}

int set_scope_flag_with_operation(string scope_key, string flag_key,
    mixed value, string operation_id)
{
    mapping response;

    if (!stringp(scope_key) || scope_key == "" || !stringp(flag_key)
        || flag_key == "")
        return 0;
    if (!stringp(operation_id) || operation_id == "")
        operation_id = next_operation_id("flag");
    response = apply_scope_mutation(scope_key, ([ flag_key: value ]),
        ([ ]), operation_id);
    return mapp(response) && response["ok"] == 1;
}

mixed query_scope_flag(string scope_key, string flag_key)
{
    mapping bucket;

    if (!stringp(scope_key) || scope_key == "" || !stringp(flag_key)
        || flag_key == "")
        return 0;
    bucket = query_existing_bucket(scope_key);
    if (!mapp(bucket) || !mapp(bucket["flags"]))
        return 0;
    return deep_copy_value(bucket["flags"][flag_key]);
}

/* 批量 scope flag 写入：单次候选发布（原子），与 set_scope_flag 同构。
 * world 域 owner 事务的 apply 用它一次性提交一组状态标记，避免逐条
 * publish 造成组内部分提交。 */
int set_scope_flags(string scope_key, mapping flags)
{
    return set_scope_flags_with_operation(scope_key, flags, 0);
}

int set_scope_flags_with_operation(string scope_key, mapping flags,
    string operation_id)
{
    mapping response;

    if (!stringp(operation_id) || operation_id == "")
        operation_id = next_operation_id("flags");
    response = apply_scope_mutation(scope_key, flags, ([ ]), operation_id);
    return mapp(response) && response["ok"] == 1;
}

int add_scope_counter(string scope_key, string counter_key, int delta)
{
    return add_scope_counter_with_operation(scope_key, counter_key, delta, 0);
}

int add_scope_counter_with_operation(string scope_key, string counter_key,
    int delta, string operation_id)
{
    mapping response;

    if (!intp(delta))
        delta = 1;
    if (!stringp(operation_id) || operation_id == "")
        operation_id = next_operation_id("counter");
    response = apply_scope_mutation(scope_key, ([ ]),
        ([ counter_key: delta ]), operation_id);
    return mapp(response) && response["ok"] == 1;
}

int set_scope_counter_monotonic(string scope_key, string counter_key, int value)
{
    mapping current_bucket;
    mapping candidate;
    mapping bucket;
    int current;

    if (!stringp(scope_key) || scope_key == "" || !stringp(counter_key)
        || counter_key == "" || !intp(value) || value <= 0)
        return 0;
    current_bucket = query_existing_bucket(scope_key);
    current = mapp(current_bucket) && mapp(current_bucket["counters"])
        && intp(current_bucket["counters"][counter_key])
        ? current_bucket["counters"][counter_key] : 0;
    if (current >= value)
        return 0;
    candidate = deep_copy_value(_state_buckets || ([ ]));
    bucket = candidate_bucket(candidate, scope_key);
    if (!mapp(candidate) || !mapp(bucket))
        return 0;
    bucket["counters"][counter_key] = value;
    bucket["updated_at"] = time();
    return publish_candidate(candidate, _generation + 1,
        next_operation_id("watermark"), ([
            "scope_key": scope_key,
            "kind": "counter",
            "key": counter_key,
            "value": value,
        ]), 0);
}

int query_scope_counter(string scope_key, string counter_key)
{
    mapping bucket;

    if (!stringp(scope_key) || scope_key == "" || !stringp(counter_key)
        || counter_key == "")
        return 0;
    bucket = query_existing_bucket(scope_key);
    if (!mapp(bucket) || !mapp(bucket["counters"]))
        return 0;
    return intp(bucket["counters"][counter_key])
        ? bucket["counters"][counter_key] : 0;
}

int query_world_generation()
{
    return _generation;
}

int query_persisted_generation()
{
    return _persisted_generation;
}

int query_persist_pending()
{
    return _persist_pending;
}

mapping query_persistence_status()
{
    return ([
        "generation": _generation,
        "persisted_generation": _persisted_generation,
        "persist_pending": _persist_pending,
        "pending_generation": _pending_generation,
        "pending_operation_id": _pending_operation_id,
        "pending_scope_count": sizeof(_pending_scope_entries || ([ ])),
        "pending_scopes": keys(_pending_scope_entries || ([ ])),
        "scope_generations": copy(_scope_generations || ([ ])),
        "persisted_scope_generations":
            copy(_persisted_scope_generations || ([ ])),
        "scope_persistence_isolated": 1,
        "last_persist_result": _last_persist_result,
        "last_persist_error": _last_persist_error,
        "save_phase": mapp(_last_world_save_phase)
            ? copy(_last_world_save_phase) : ([ "outcome": "not_attempted" ]),
        "retry_count": _persist_retry_count,
    ]);
}

int compare_and_set_scope_counter(string scope_key, string counter_key,
    int expected, int value)
{
    mapping current_bucket;
    mapping candidate;
    mapping bucket;
    int current;

    if (!stringp(scope_key) || scope_key == "" || !stringp(counter_key)
        || counter_key == "" || !intp(expected) || !intp(value))
        return 0;
    current_bucket = query_existing_bucket(scope_key);
    current = mapp(current_bucket) && mapp(current_bucket["counters"])
        && intp(current_bucket["counters"][counter_key])
        ? current_bucket["counters"][counter_key] : 0;
    if (current != expected)
        return 0;
    candidate = deep_copy_value(_state_buckets || ([ ]));
    bucket = candidate_bucket(candidate, scope_key);
    if (!mapp(candidate) || !mapp(bucket))
        return 0;
    bucket["counters"][counter_key] = value;
    bucket["updated_at"] = time();
    return publish_candidate(candidate, _generation + 1,
        next_operation_id("cas"), ([
            "scope_key": scope_key,
            "kind": "counter_cas",
            "key": counter_key,
            "expected": expected,
            "value": value,
        ]), 0);
}
