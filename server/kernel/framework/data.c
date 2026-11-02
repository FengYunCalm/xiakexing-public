/**
 * @file    data.c
 * @brief   数据管理框架
 * @author  XiaKeXing Development Team
 * @date    2026-03-20
 * @version 1.0.1
 */

#pragma strict_types
#pragma save_types

#include <system/globals.h>
#include <system/constants.h>
#include <system/errors.h>
#include <system/services.h>

nosave mapping _dbase_data;
nosave mapping _dbase_temp;
nosave string _dbase_path;
nosave int _dbase_modified;
nosave int _dbase_auto_save;
nosave int _dbase_save_delay;
nosave int _dbase_save_handle;
nosave int _dbase_schema_version;
nosave mapping _dbase_last_error;
nosave mapping _dbase_last_save_phase;
nosave mapping _dbase_owner_snapshot;
nosave mapping _dbase_owner_receipt_pending;
nosave int _dbase_receipt_lookup_pending;
nosave int _dbase_owner_save_sequence;
nosave int _dbase_change_sequence;
nosave string _dbase_owner_save_epoch;

int save();
private void _schedule_save();
private void schedule_receipt_lookup();
private void _flush_scheduled_save();
private void clear_save_error();
private mixed clone_data_value(mixed value);
private int data_value_is_serializable(mixed value);
private int assign_data_value(mapping node, string key, mixed value, string path);
private int set_data_value_internal(string path, mixed value);
private mixed query_data_value_internal(string path);
private int delete_data_value_internal(string path);
private int set_data_internal(mapping data);
private int owner_write_allowed();
private int owner_snapshot_enabled();
private string query_owner_player_id();
private mapping ensure_owner_snapshot(string content);
private int mark_owner_snapshot(string state, string reason);
private int complete_owner_receipt();
private void record_owner_receipt_failure();
private void complete_data_save();
private void record_save_phase(string outcome, int persisted, int save_result,
    string reason);
private int _dbase_save_failures;
mixed query_temp(string path);
mixed query(string path);

void create()
{
    _dbase_data = ([]);
    _dbase_temp = ([]);
    _dbase_path = "";
    _dbase_modified = 0;
    _dbase_auto_save = 0;
    _dbase_save_delay = 2;
    _dbase_save_handle = 0;
    _dbase_schema_version = 0;
    _dbase_last_error = ([ ]);
    _dbase_last_save_phase = ([
        "ok": 0,
        "persisted": 0,
        "outcome": "not_attempted",
        "save_result": 0,
    ]);
    _dbase_owner_snapshot = 0;
    _dbase_owner_receipt_pending = 0;
    _dbase_receipt_lookup_pending = 0;
    _dbase_owner_save_sequence = 0;
    _dbase_change_sequence = 0;
    _dbase_owner_save_epoch = 0;
    _dbase_save_failures = 0;
}

void set_save_path(string path)
{
    if (stringp(path))
        _dbase_path = path;
}

string query_save_path()
{
    return _dbase_path;
}

void set_auto_save(int flag)
{
    _dbase_auto_save = flag ? 1 : 0;
    schedule_receipt_lookup();
}

void set_auto_save_delay(int seconds)
{
    if (!intp(seconds) || seconds < 0)
        return;

    _dbase_save_delay = seconds;
}

int query_auto_save_delay()
{
    return _dbase_save_delay;
}

private string *_parse_path(string path)
{
    string *parts;

    if (!stringp(path) || path == "")
        return ({});

    /*
     * 嵌套路径按 '/' 逐层展开，连续 '/' 与首尾 '/' 会被过滤掉，
     * 因此 "combat/attack"、"/combat/attack/" 会落到同一条逻辑路径。
     */
    parts = explode(path, "/");
    parts = filter(parts, (: stringp($1) && $1 != "" :));

    return parts;
}

private mapping _get_node(mapping data, string *path, int create)
{
    mapping current;
    int i, n;

    if (!mapp(data) || !arrayp(path))
        return 0;

    current = data;
    n = sizeof(path);

    /*
     * 只允许在缺失节点下创建 mapping。若已有节点是标量或数组，
     * 深层写入必须失败，不能静默覆盖有效状态。
     */
    for (i = 0; i < n; i++) {
        if (undefinedp(current[path[i]])) {
            if (create)
                current[path[i]] = ([]);
            else
                return 0;
        } else if (!mapp(current[path[i]])) {
            return 0;
        }
        current = current[path[i]];
    }

    return current;
}

private int _path_exists(string path)
{
    string *parts;
    mapping node;
    int n;

    if (!mapp(_dbase_data))
        _dbase_data = ([]);

    if (!stringp(path) || path == "")
        return 0;

    parts = _parse_path(path);
    n = sizeof(parts);
    if (n == 0)
        return 0;

    if (n == 1)
        return !undefinedp(_dbase_data[parts[0]]);

    node = _get_node(_dbase_data, parts[0..n-2], 0);
    if (!mapp(node))
        return 0;

    return !undefinedp(node[parts[n-1]]);
}

private void _set_modified()
{
    _dbase_modified = 1;
    _dbase_change_sequence++;
}

private void record_data_error(string code, string path, mixed old_value,
    mixed new_value)
{
    _dbase_last_error = ([
        "ok": 0,
        "code": stringp(code) ? code : "DATA_WRITE_FAILED",
        "path": stringp(path) ? path : "",
        "old_type": mapp(old_value) ? "mapping"
            : arrayp(old_value) ? "array"
            : intp(old_value) ? "int"
            : stringp(old_value) ? "string"
            : objectp(old_value) ? "object" : "missing",
        "new_type": mapp(new_value) ? "mapping"
            : arrayp(new_value) ? "array"
            : intp(new_value) ? "int"
            : stringp(new_value) ? "string"
            : objectp(new_value) ? "object" : "zero_or_other",
        "recorded_at": time(),
    ]);
}

private mixed clone_data_value(mixed value)
{
    mixed cloned;
    mixed err;

    err = catch(cloned = restore_variable(save_variable(value)));
    return err ? 0 : cloned;
}

private int data_value_is_serializable(mixed value)
{
    string serialized;
    mixed err;

    err = catch(serialized = save_variable(value));
    return !err && stringp(serialized);
}

private int assign_data_value(mapping node, string key, mixed value, string path)
{
    mixed cloned;

    if (!mapp(node) || !stringp(key) || key == "")
        return 0;
    if (!data_value_is_serializable(value)) {
        record_data_error("DATA_VALUE_NOT_SERIALIZABLE", path,
            node[key], value);
        return 0;
    }
    cloned = clone_data_value(value);
    if (!data_value_is_serializable(cloned)) {
        record_data_error("DATA_VALUE_NOT_SERIALIZABLE", path,
            node[key], value);
        return 0;
    }
    node[key] = cloned;
    _dbase_last_error = ([ "ok": 1, "code": "" ]);
    return 1;
}

/* 只有角色状态 owner service 可以绕过公开 set facade 执行原子候选写入。 */
private int owner_write_allowed()
{
    object caller;

    caller = previous_object();
    return objectp(caller)
        && base_name(caller) == "/daemons/services/player/player_state_d";
}

/* 非持久化测试夹具（沙箱诊断）是唯一被允许直接写玩家状态的例外。 */
private int is_non_persistent_fixture()
{
    return this_object()->query_temp("test_fixture/non_persistent") == 1;
}

/* 玩家对象的受管状态路径只允许 PLAYER_STATE_D 经基类入口写。
 * object/living 层的 set()/set_data() 会先走 owner 校验，但直接调用基类入口
 * （data::set、外部直接 call 基类实现）会整体跳过 owner、schema 与 generation CAS。 */
private int managed_player_write_denied(string path)
{
    object target;

    target = this_object();
    /* 用 call_other 而不是 ->is_player()：globals.h 的 is_player(ob) 函数宏会
     * 把 `->is_player()` 误当宏调用展开。 */
    if (!objectp(target) || !function_exists("is_player", target))
        return 0;
    if (!call_other(target, "is_player"))
        return 0;
    if (is_non_persistent_fixture() || owner_write_allowed())
        return 0;
    if (stringp(path) && !call_other(PLAYER_STATE_D, "is_managed_state_path", path))
        return 0;
    return 1;
}

private int managed_player_bulk_write_denied()
{
    object target;

    target = this_object();
    /* 用 call_other 而不是 ->is_player()：globals.h 的 is_player(ob) 函数宏会
     * 把 `->is_player()` 误当宏调用展开。 */
    if (!objectp(target) || !function_exists("is_player", target))
        return 0;
    if (!call_other(target, "is_player"))
        return 0;
    if (is_non_persistent_fixture() || owner_write_allowed())
        return 0;
    return 1;
}

#define DATA_SAVE_RETRY_BASE_DELAY  5
#define DATA_SAVE_RETRY_MAX_DELAY   300

/* 保存失败后按有界退避重新排程：失败状态保留在 _dbase_modified 与
 * query_data_error() 里，调用方不会以为已经落盘。 */
private void schedule_save_retry()
{
    int delay;

    if (!_dbase_auto_save)
        return;

    _dbase_save_failures++;
    delay = DATA_SAVE_RETRY_BASE_DELAY;
    if (_dbase_save_failures > 1)
        delay = DATA_SAVE_RETRY_BASE_DELAY << (_dbase_save_failures - 1);
    if (delay <= 0 || delay > DATA_SAVE_RETRY_MAX_DELAY)
        delay = DATA_SAVE_RETRY_MAX_DELAY;

    if (_dbase_save_handle)
        remove_call_out(_dbase_save_handle);
    _dbase_save_handle = call_out((: _flush_scheduled_save :), delay);
}

private void record_save_failure()
{
    _dbase_last_error = ([
        "ok": 0,
        "code": "DATA_SAVE_FAILED",
        "path": stringp(_dbase_path) ? _dbase_path : "",
        "failures": _dbase_save_failures + 1,
    ]);
    schedule_save_retry();
}

private void record_save_phase(string outcome, int persisted, int save_result,
    string reason)
{
    mapping snapshot;

    _dbase_last_save_phase = ([
        "ok": outcome == "committed" ? 1 : 0,
        "persisted": persisted,
        "outcome": stringp(outcome) && outcome != "" ? outcome : "unknown",
        "save_result": save_result,
        "error": stringp(reason) ? reason : "",
    ]);
    snapshot = mapp(_dbase_owner_snapshot) ? _dbase_owner_snapshot
        : _dbase_owner_receipt_pending;
    if (mapp(snapshot)) {
        if (intp(snapshot["snapshot_id"]))
            _dbase_last_save_phase["snapshot_id"] = snapshot["snapshot_id"];
        if (stringp(snapshot["operation_id"]))
            _dbase_last_save_phase["retry_token"] = snapshot["operation_id"];
    }
}

private int owner_snapshot_enabled()
{
    object target;

    target = this_object();
    if (is_non_persistent_fixture() || !objectp(target)
        || !function_exists("is_player", target))
        return 0;
    return call_other(target, "is_player") ? 1 : 0;
}

private string query_owner_player_id()
{
    mixed value;
    string player_id;

    if (function_exists("query_id", this_object())) {
        value = call_other(this_object(), "query_id");
        if (stringp(value) && value != "")
            return value;
    }
    player_id = get_object_id(this_object());
    if (stringp(player_id) && player_id != "")
        return player_id;
    value = query("account_id");
    return stringp(value) && value != "" ? value : "";
}

private int mark_owner_snapshot(string state, string reason)
{
    object lifecycle;
    mapping result;
    mixed err;

    if (!mapp(_dbase_owner_snapshot))
        return 0;
    lifecycle = _dbase_owner_snapshot["lifecycle"];
    if (!objectp(lifecycle) || !intp(_dbase_owner_snapshot["snapshot_id"]))
        return 0;
    err = catch(result = lifecycle->finalize_player_json_snapshot(
        _dbase_owner_snapshot["snapshot_id"], state, reason));
    return !err && mapp(result) && result["ok"] == 1;
}

private mapping ensure_owner_snapshot(string content)
{
    object lifecycle;
    mapping payload;
    mapping snapshot;
    mapping publication;
    mixed err;
    string digest;
    string player_id;
    string operation_id;

    if (!owner_snapshot_enabled())
        return ([ "ok": 1, "enabled": 0 ]);
    player_id = query_owner_player_id();
    if (player_id == "")
        return ([ "ok": 0, "error_code": "PLAYER_ID_REQUIRED" ]);
    digest = hash("sha256", content);
    if (mapp(_dbase_owner_snapshot)
        && _dbase_owner_snapshot["content_digest"] == digest)
        return ([ "ok": 1, "duplicate": 1 ]);
    if (mapp(_dbase_owner_snapshot)) {
        if (!mark_owner_snapshot("save_failed",
                "player JSON candidate changed before retry"))
            return ([ "ok": 0, "error_code": "OWNER_SNAPSHOT_RETRY_PENDING" ]);
        _dbase_owner_snapshot = 0;
    }
    payload = ([
        "player_id": player_id,
        "save_path": _dbase_path,
        "content_digest": digest,
        "state": clone_data_value(_dbase_data),
    ]);
    if (!mapp(payload["state"]))
        return ([ "ok": 0, "error_code": "PLAYER_STATE_NOT_SERIALIZABLE" ]);
    err = catch(lifecycle = load_object(
        "/daemons/services/system/player_lifecycle_d"));
    if (err || !objectp(lifecycle))
        return ([ "ok": 0, "error_code": "PLAYER_LIFECYCLE_UNAVAILABLE" ]);
    err = catch(snapshot = lifecycle->query_pending_player_json_snapshot(
        "owner/player/" + player_id, digest));
    if (err)
        return ([ "ok": 0, "error_code": "OWNER_SNAPSHOT_LOOKUP_FAILED",
            "detail": sprintf("%O", err) ]);
    if (mapp(snapshot))
        operation_id = snapshot["idempotency_key"];
    else {
        if (!_dbase_owner_save_epoch)
            _dbase_owner_save_epoch = sprintf("%016x%016x%016x",
                secure_random(MAX_INT), secure_random(MAX_INT), secure_random(MAX_INT));
        _dbase_owner_save_sequence++;
        operation_id = sprintf("player/json/%s/%s/%d", player_id,
            _dbase_owner_save_epoch, _dbase_owner_save_sequence);
    }
    // A pending in-memory snapshot is not evidence that admission reached disk.
    err = catch(snapshot = lifecycle->prepare_player_json_snapshot(
        this_object(), "owner/player/" + player_id, payload, operation_id));
    if (err || !mapp(snapshot) || snapshot["ok"] != 1
        || !mapp(snapshot["snapshot"])) {
        if (err)
            return ([ "ok": 0, "error_code": "OWNER_SNAPSHOT_REJECTED",
                "detail": sprintf("%O", err) ]);
        return ([ "ok": 0, "error_code": "OWNER_SNAPSHOT_REJECTED",
            "detail": mapp(snapshot) ? snapshot : ([ ]) ]);
    }
    _dbase_owner_snapshot = ([
        "lifecycle": lifecycle,
        "snapshot_id": snapshot["snapshot"]["id"],
        "save_path": _dbase_path,
        "content_digest": digest,
        "change_sequence": _dbase_change_sequence,
        "operation_id": stringp(operation_id) ? operation_id
            : snapshot["snapshot"]["idempotency_key"],
    ]);
    err = catch(publication = lifecycle->prepare_player_json_publication(
        snapshot["snapshot"]["id"], _dbase_path, "sha256:" + digest));
    if (err || !mapp(publication) || publication["ok"] != 1) {
        _dbase_owner_snapshot = 0;
        return ([ "ok": 0, "error_code": "OWNER_PUBLICATION_REJECTED",
            "detail": err ? ([ "error": sprintf("%O", err) ])
                : mapp(publication) ? publication : ([ ]) ]);
    }
    return ([ "ok": 1, "snapshot": snapshot["snapshot"],
        "publication": publication ]);
}

private void record_owner_receipt_failure()
{
    _dbase_last_error = ([
        "ok": 0,
        "code": "DATA_SAVE_RECEIPT_PENDING",
        "path": stringp(_dbase_path) ? _dbase_path : "",
        "failures": _dbase_save_failures + 1,
    ]);
    schedule_save_retry();
}

private int recover_owner_receipt()
{
    object lifecycle;
    mapping snapshot;
    mapping lookup;
    mapping evidence;
    string content;
    string digest;
    mixed err;

    if (!owner_snapshot_enabled())
        return 1;
    if (file_size(_dbase_path) == -2)
        return 0;
    if (file_size(_dbase_path) == -1)
        digest = "missing";
    else {
        err = catch(content = read_file(_dbase_path));
        if (err || !stringp(content))
            return 0;
        digest = "sha256:" + hash("sha256", content);
    }
    err = catch(lifecycle = load_object(
        "/daemons/services/system/player_lifecycle_d"));
    if (err || !objectp(lifecycle))
        return 0;

    if (mapp(_dbase_owner_receipt_pending)) {
        snapshot = _dbase_owner_receipt_pending;
    } else {
        /* Path lookup finds the frozen candidate even when the disk still
         * contains the before-image.  Digest matching is decided only by the
         * persisted publication evidence below. */
        lookup = lifecycle->query_pending_player_json_publication(
            "owner/player/" + query_owner_player_id(), _dbase_path);
        if (!mapp(lookup) || lookup["ok"] != 1) {
            /* A captured snapshot without publication evidence is not a
             * receipt. If the target is absent, the same explicit operation
             * may retry durable admission; existing-file ambiguity and
             * corrupt ledgers remain fail-closed. */
            if (digest == "missing" && mapp(lookup)
                && lookup["error_code"] == "PUBLICATION_EVIDENCE_MISSING")
                return 1;
            return 0;
        }
        snapshot = lookup["snapshot"];
        if (!mapp(snapshot))
            return 1;
        if (!mapp(snapshot["payload"])
            || snapshot["payload"]["save_path"] != _dbase_path
            || !intp(snapshot["id"]) || snapshot["id"] <= 0)
            return 0;
        _dbase_owner_snapshot = ([
            "lifecycle": lifecycle,
            "snapshot_id": snapshot["id"],
            "save_path": _dbase_path,
            "content_digest": snapshot["payload"]["content_digest"],
            "operation_id": snapshot["idempotency_key"],
            /* A clean object was restored from the current file. A dirty
             * object has no proof that its memory is the published candidate. */
            "change_sequence": _dbase_modified ? -1 : _dbase_change_sequence,
        ]);
        _dbase_owner_receipt_pending = copy(_dbase_owner_snapshot);
    }

    evidence = lifecycle->query_player_json_publication(
        _dbase_owner_receipt_pending["snapshot_id"], _dbase_path, digest);
    if (!mapp(evidence) || evidence["outcome"] == "unknown"
        || evidence["outcome"] == "persisted_receipt_pending"
            && evidence["matches_candidate"] != 1)
        return 0;
    _dbase_owner_receipt_pending["publication_outcome"] = evidence["outcome"];
    _dbase_owner_receipt_pending["observed_digest"] = digest;
    return 1;
}

private int clear_backup_file(string backup_path)
{
    if (!stringp(backup_path) || backup_path == "")
        return 0;
    if (file_size(backup_path) == -1)
        return 1;
    if (file_size(backup_path) == -2)
        return 0;
    rm(backup_path);
    return file_size(backup_path) == -1;
}

private int complete_owner_receipt()
{
    if (!mapp(_dbase_owner_receipt_pending))
        return 1;
    if (!mark_owner_snapshot("saved", "player JSON save receipt retry")) {
        record_owner_receipt_failure();
        return 0;
    }
    _dbase_owner_receipt_pending = 0;
    _dbase_owner_snapshot = 0;
    return 1;
}

mapping query_save_phase()
{
    return mapp(_dbase_last_save_phase) ? copy(_dbase_last_save_phase) : ([
        "ok": 0,
        "persisted": 0,
        "outcome": "not_attempted",
        "save_result": 0,
    ]);
}

/* 只补玩家 JSON 的原收据。此入口绝不序列化或发布当前内存，业务
 * 恢复责任仍由原 operation owner 持有。 */
mapping recover_save_receipt()
{
    mixed err;
    int can_clear;

    if (!owner_snapshot_enabled()) {
        record_save_phase("committed", 1, 1, "owner persistence disabled");
        return query_save_phase();
    }
    err = catch {
        if (!recover_owner_receipt())
            error("player JSON receipt evidence unavailable");
    };
    if (err) {
        record_save_phase("unknown", -1, 0, sprintf("%O", err));
        return query_save_phase();
    }
    if (!mapp(_dbase_owner_receipt_pending)) {
        record_save_phase("committed", 1, 1, "no pending receipt");
        return query_save_phase();
    }
    if (mapp(_dbase_owner_receipt_pending)
        && _dbase_owner_receipt_pending["publication_outcome"] == "not_written") {
        if (!mark_owner_snapshot("save_failed",
                "publication evidence proves player JSON was not written")) {
            record_save_phase("unknown", -1, 0,
                "player JSON not-written receipt could not be closed");
            return query_save_phase();
        }
        _dbase_owner_receipt_pending = 0;
        _dbase_owner_snapshot = 0;
        record_save_phase("not_written", 0, 0,
            "publication evidence proves player JSON was not written");
        return query_save_phase();
    }
    can_clear = intp(_dbase_owner_receipt_pending["change_sequence"])
        && _dbase_owner_receipt_pending["change_sequence"]
            == _dbase_change_sequence;
    if (!clear_backup_file(_dbase_path + ".bak")) {
        record_owner_receipt_failure();
        record_save_phase("persisted_receipt_pending", 1, 2,
            "player JSON backup cleanup remains pending");
        return query_save_phase();
    }
    if (!complete_owner_receipt()) {
        record_save_phase("persisted_receipt_pending", 1, 2,
            "player JSON receipt remains pending");
        return query_save_phase();
    }
    if (can_clear)
        complete_data_save();
    record_save_phase("committed", 1, 1, can_clear
        ? "player JSON receipt recovered"
        : "player JSON receipt recovered; current memory remains dirty");
    return query_save_phase();
}

/* 统一玩家保存阶段 façade。调用者必须消费 outcome，而不是把非零
 * save_result 当作 committed。 */
mapping save_phases()
{
    int saved;
    mixed err;
    mapping phase;

    err = catch(saved = call_other(this_object(), "save"));
    if (err) {
        record_save_phase("unknown", -1, 0, sprintf("%O", err));
        return query_save_phase();
    }
    phase = query_save_phase();
    if (!mapp(phase))
        phase = ([]);
    phase["save_result"] = saved;
    if (saved == 1 && phase["outcome"] != "committed")
        phase["outcome"] = "committed";
    if (saved == 1)
        phase["ok"] = 1;
    return phase;
}

mapping query_data_error()
{
    return mapp(_dbase_last_error) ? copy(_dbase_last_error) : ([ ]);
}

private void schedule_receipt_lookup()
{
    if (_dbase_auto_save && !_dbase_save_handle
        && (_dbase_receipt_lookup_pending || mapp(_dbase_owner_receipt_pending)))
        _dbase_save_handle = call_out((: _flush_scheduled_save :), DATA_SAVE_RETRY_BASE_DELAY);
}

private void _flush_scheduled_save()
{
    int receipt_only;

    _dbase_save_handle = 0;
    if (_dbase_receipt_lookup_pending || mapp(_dbase_owner_receipt_pending)) {
        if (!recover_owner_receipt()) {
            record_owner_receipt_failure();
            return;
        }
        _dbase_receipt_lookup_pending = 0;
        if (mapp(_dbase_owner_receipt_pending)
            && _dbase_owner_receipt_pending["publication_outcome"] == "not_written") {
            if (!mark_owner_snapshot("save_failed",
                    "publication evidence proves player JSON was not written"))
                return;
            _dbase_owner_receipt_pending = 0;
            _dbase_owner_snapshot = 0;
            clear_save_error();
            _schedule_save();
            return;
        }
        receipt_only = mapp(_dbase_owner_receipt_pending)
            && intp(_dbase_owner_receipt_pending["change_sequence"])
            && _dbase_owner_receipt_pending["change_sequence"]
                == _dbase_change_sequence;
        if (!clear_backup_file(_dbase_path + ".bak")) {
            record_owner_receipt_failure();
            return;
        }
        if (!complete_owner_receipt())
            return;
        if (receipt_only)
            _dbase_modified = 0;
        clear_save_error();
        // 补收据不发布当前内存；期间的新修改留给下一次普通保存。
        if (_dbase_modified)
            _schedule_save();
        return;
    }
    if (_dbase_modified)
        save();   // 失败时 save() 内部按有界退避重新排程
}

/*
 * 非 set() 路径（物品转移/移动等）也可触发自动落盘：
 * 标记 modified 并按当前 delay 调度一次保存。
 */
int schedule_save()
{
    _dbase_modified = 1;
    _schedule_save();
    return 1;
}

private void _schedule_save()
{
    if (!_dbase_auto_save)
        return;

    if (_dbase_save_delay <= 0) {
        save();
        return;
    }

    if (_dbase_save_handle)
        return;

    _dbase_save_handle = call_out((: _flush_scheduled_save :), _dbase_save_delay);
}

private int set_data_value_internal(string path, mixed value)
{
    string *parts;
    mapping node;
    mapping candidate;
    int n;

    if (!mapp(_dbase_data))
        _dbase_data = ([]);

    if (!stringp(path) || path == "")
        return 0;

    parts = _parse_path(path);
    n = sizeof(parts);

    if (n == 0)
        return 0;
    if (!data_value_is_serializable(value)) {
        record_data_error("DATA_VALUE_NOT_SERIALIZABLE", path,
            query_data_value_internal(path), value);
        return 0;
    }

    /* Work on a detached candidate. This keeps both an existing value and a
     * newly-created parent path unchanged when validation or serialization
     * fails halfway through a deep write. */
    candidate = clone_data_value(_dbase_data);
    if (!mapp(candidate)) {
        record_data_error("DATA_SNAPSHOT_NOT_SERIALIZABLE", path,
            _dbase_data, value);
        return 0;
    }
    if (n == 1) {
        if (!assign_data_value(candidate, parts[0], value, path))
            return 0;
    } else {
        node = _get_node(candidate, parts[0..n-2], 1);
        if (!mapp(node)) {
            record_data_error("DATA_PATH_TYPE_CONFLICT", path,
                query_data_value_internal(parts[0]), value);
            return 0;
        }
        if (!assign_data_value(node, parts[n-1], value, path))
            return 0;
    }

    _dbase_data = candidate;
    _set_modified();
    _schedule_save();

    return 1;
}

int set(string path, mixed value)
{
    if (managed_player_write_denied(path)) {
        record_data_error("DATA_OWNER_REQUIRED", path, 0, value);
        return 0;
    }
    return set_data_value_internal(path, value);
}

private mixed query_data_value_internal(string path)
{
    string *parts;
    mapping node;
    int n;

    if (!mapp(_dbase_data))
        _dbase_data = ([]);

    if (!stringp(path) || path == "")
        return 0;

    parts = _parse_path(path);
    n = sizeof(parts);

    if (n == 0)
        return 0;

    if (n == 1) {
        return clone_data_value(_dbase_data[parts[0]]);
    } else {
        node = _get_node(_dbase_data, parts[0..n-2], 0);
        if (!mapp(node))
            return 0;
        return clone_data_value(node[parts[n-1]]);
    }
}

mixed query(string path)
{
    return query_data_value_internal(path);
}

mixed query_default(string path, mixed default_val)
{
    mixed value;

    value = query(path);
    /*
     * 使用路径存在性判断，而不是把 0 当作“未取到值”的哨兵。
     * 这样显式存储 0 的调用方也能得到真实值。
     */
    return _path_exists(path) ? value : default_val;
}

int add(string path, int value)
{
    int current;

    current = query(path);
    if (!intp(current))
        current = 0;

    current += value;
    set(path, current);

    return current;
}

private int delete_data_value_internal(string path)
{
    string *parts;
    mapping node;
    int n;

    if (!stringp(path) || path == "")
        return 0;

    parts = _parse_path(path);
    n = sizeof(parts);

    if (n == 0)
        return 0;

    if (n == 1) {
        map_delete(_dbase_data, parts[0]);
    } else {
        node = _get_node(_dbase_data, parts[0..n-2], 0);
        if (!mapp(node))
            return 0;
        map_delete(node, parts[n-1]);
    }

    _set_modified();
    _schedule_save();

    return 1;
}

int delete(string path)
{
    if (managed_player_write_denied(path)) {
        record_data_error("DATA_OWNER_REQUIRED", path, 0, 0);
        return 0;
    }
    return delete_data_value_internal(path);
}

int delete_temp(string path)
{
    string *parts;
    mapping node;
    int n;

    if (!stringp(path) || path == "")
        return 0;

    parts = _parse_path(path);
    n = sizeof(parts);

    if (n == 0)
        return 0;

    if (n == 1) {
        map_delete(_dbase_temp, parts[0]);
    } else {
        node = _get_node(_dbase_temp, parts[0..n-2], 0);
        if (!mapp(node))
            return 0;
        map_delete(node, parts[n-1]);
    }

    return 1;
}

// 批量操作
private int set_data_internal(mapping data)
{
    mapping cloned;

    if (!mapp(data))
        return 0;
    cloned = clone_data_value(data);
    if (!data_value_is_serializable(data) || !mapp(cloned)) {
        record_data_error("DATA_SNAPSHOT_NOT_SERIALIZABLE", "", _dbase_data, data);
        return 0;
    }

    _dbase_data = cloned;
    _set_modified();
    _schedule_save();

    return 1;
}

int set_data(mapping data)
{
    if (managed_player_bulk_write_denied()) {
        record_data_error("DATA_OWNER_REQUIRED", "", mapp(data) ? data : 0, 0);
        return 0;
    }
    return set_data_internal(data);
}

mapping query_data()
{
    mixed cloned;

    cloned = clone_data_value(_dbase_data || ([]));
    return data_value_is_serializable(_dbase_data || ([]))
        && mapp(cloned) ? cloned : ([]);
}

/* Owner-local raw mutation primitives. They intentionally do not expose the
 * internal mapping and can only be called by PLAYER_STATE_D. */
mixed data_owner_query(string path)
{
    if (!owner_write_allowed())
        return 0;
    return query_data_value_internal(path);
}

mapping data_owner_query_data()
{
    if (!owner_write_allowed())
        return ([ ]);
    return query_data();
}

int data_owner_set(string path, mixed value)
{
    if (!owner_write_allowed())
        return 0;
    return set_data_value_internal(path, value);
}

int data_owner_delete(string path)
{
    if (!owner_write_allowed())
        return 0;
    return delete_data_value_internal(path);
}

int data_owner_replace(mapping data)
{
    if (!owner_write_allowed())
        return 0;
    return set_data_internal(data);
}

private void clear_save_error()
{
    _dbase_save_failures = 0;
    if (mapp(_dbase_last_error)
        && member_array(_dbase_last_error["code"],
            ({ "DATA_SAVE_FAILED", "DATA_SAVE_RECEIPT_PENDING" })) >= 0)
        _dbase_last_error = ([ "ok": 1, "code": "" ]);
}

private void complete_data_save()
{
    _dbase_modified = 0;
    clear_save_error();
}

// 持久化操作
int save()
{
    string temp_path;
    string backup_path;
    string content;
    mixed err;
    mapping owner_result;
    int receipt_only;
    int backup_moved;
    int backup_restored;

    if (!stringp(_dbase_path) || _dbase_path == "") {
        record_save_phase("not_written", 0, 0, "save path is empty");
        return 0;
    }

    if (_dbase_save_handle) {
        remove_call_out(_dbase_save_handle);
        _dbase_save_handle = 0;
    }

    temp_path = _dbase_path + ".tmp";
    backup_path = _dbase_path + ".bak";
    backup_moved = 0;
    backup_restored = 1;

    err = catch(content = json_encode(_dbase_data));
    if (err || !stringp(content)) {
        record_save_failure();
        record_save_phase("not_written", 0, 0, "JSON serialization failed");
        return 0;
    }

    if (!recover_owner_receipt()) {
        record_save_failure();
        record_save_phase("unknown", -1, 0,
            "player JSON receipt evidence unavailable");
        return 0;
    }
    _dbase_receipt_lookup_pending = 0;
    receipt_only = mapp(_dbase_owner_receipt_pending)
        && _dbase_owner_receipt_pending["content_digest"] == hash("sha256", content);
    if (mapp(_dbase_owner_receipt_pending)
        && _dbase_owner_receipt_pending["publication_outcome"] == "not_written") {
        if (!mark_owner_snapshot("save_failed",
                "publication evidence proves player JSON was not written")) {
            record_owner_receipt_failure();
            record_save_phase("unknown", -1, 0,
                "player JSON not-written receipt could not be closed");
            return 0;
        }
        _dbase_owner_receipt_pending = 0;
        _dbase_owner_snapshot = 0;
    }
    if (mapp(_dbase_owner_receipt_pending)) {
        if (receipt_only) {
            if (!clear_backup_file(backup_path)) {
                record_owner_receipt_failure();
                record_save_phase("persisted_receipt_pending", 1, 2,
                    "player JSON backup cleanup remains pending");
                return 2;
            }
            if (!complete_owner_receipt()) {
                record_save_phase("persisted_receipt_pending", 1, 2,
                    "player JSON receipt remains pending");
                return 2;
            }
            complete_data_save();
            record_save_phase("committed", 1, 1,
                "player JSON receipt completed");
            return 1;
        }
        /* 当前内存候选已经变化。先完成原文件的收据；不能把新候选
         * 当作原操作重试，也不能在收据失败时写入新文件。 */
        if (!complete_owner_receipt()) {
            record_save_phase("blocked_by_prior_receipt", 0, 0,
                "prior player JSON receipt remains pending");
            return 0;
        }
    }
    owner_result = ensure_owner_snapshot(content);
    if (!mapp(owner_result) || owner_result["ok"] != 1) {
        _dbase_last_error = ([
            "ok": 0,
            "code": "DATA_SAVE_SNAPSHOT_FAILED",
            "path": _dbase_path,
            "detail": mapp(owner_result) ? owner_result : ([ ]),
        ]);
        schedule_save_retry();
        record_save_phase(mapp(owner_result)
            && owner_result["error_code"] == "OWNER_SNAPSHOT_RETRY_PENDING"
            ? "blocked_by_prior_receipt" : "not_written", 0, 0,
            mapp(owner_result) && stringp(owner_result["error_code"])
            ? owner_result["error_code"] : "player JSON snapshot rejected");
        return 0;
    }

    /* 文件层失败（目录缺失、权限问题）在 LPC 里是抛错而不是返回 0：
     * 必须包住，否则 save() 会直接抛回调用方，失败重排程也不会发生。 */
    err = catch {
        if (file_size(_dbase_path) == -2 || file_size(temp_path) == -2
            || file_size(backup_path) == -2)
            error("data save: publication path is a directory");
        if (file_size(_dbase_path) == -1
            && (file_size(temp_path) >= 0 || file_size(backup_path) >= 0))
            error("data save: recovery files require restoration before publication");
        rm(temp_path);
        if (!write_file(temp_path, content, 1))
            error("data save: temp write failed");
        if (file_size(_dbase_path) >= 0) {
            if (rename(_dbase_path, backup_path) != 0)
                error("data save: backup rename failed");
            backup_moved = 1;
            if (file_size(_dbase_path) >= 0 || file_size(backup_path) == -1)
                error("data save: backup rename postcondition failed");
        }
        if (rename(temp_path, _dbase_path) != 0
            || file_size(_dbase_path) == -1 || file_size(_dbase_path) == -2) {
            if (backup_moved) {
                backup_restored = rename(backup_path, _dbase_path) == 0
                    && file_size(_dbase_path) >= 0
                    && file_size(backup_path) == -1;
                if (!backup_restored)
                    error("data save: commit rename failed and backup restore failed");
            }
            error("data save: commit rename failed");
        }
    };
    if (err) {
        if (owner_snapshot_enabled() && mapp(_dbase_owner_snapshot))
            mark_owner_snapshot("save_failed", sprintf("%O", err));
        record_save_failure();
        record_save_phase("not_written", 0, 0,
            backup_moved && !backup_restored
                ? "player JSON publication failed; backup restore failed"
                : "player JSON publication failed");
        return 0;
    }

    if (file_size(backup_path) >= 0 && !clear_backup_file(backup_path)) {
        if (owner_snapshot_enabled() && mapp(_dbase_owner_snapshot)) {
            _dbase_owner_receipt_pending = copy(_dbase_owner_snapshot);
            record_owner_receipt_failure();
        } else {
            record_save_failure();
        }
        record_save_phase("persisted_receipt_pending", 1, 2,
            "player JSON published; backup cleanup remains pending");
        return 2;
    }

    if (owner_snapshot_enabled() && mapp(_dbase_owner_snapshot)) {
        if (!mark_owner_snapshot("saved", "player JSON save applied")) {
            _dbase_owner_receipt_pending = copy(_dbase_owner_snapshot);
            _dbase_modified = 1;
            record_owner_receipt_failure();
            record_save_phase("persisted_receipt_pending", 1, 2,
                "player JSON published but receipt is pending");
            return 2;
        }
        _dbase_owner_snapshot = 0;
    }

    complete_data_save();
    record_save_phase("committed", 1, 1, "player JSON save committed");
    return 1;
}

int restore()
{
    mapping data;
    mapping contract;
    mapping migrated;
    mixed err;
    string content;
    int stored_version;
    int current_version;

    if (!stringp(_dbase_path) || _dbase_path == "")
        return 0;

    content = read_file(_dbase_path);
    if (!stringp(content) || content == "")
        return 0;

    err = catch(data = json_decode(content));
    if (err || !mapp(data))
        return 0;

    /* Restore always validates a detached candidate before replacing the
     * currently valid runtime state. Object-specific migrations are optional,
     * but unsupported future versions are never silently accepted. */
    stored_version = 0;
    if (mapp(data["persistence"])
        && intp(data["persistence"]["schema_version"]))
        stored_version = data["persistence"]["schema_version"];
    current_version = stored_version;
    if (function_exists("query_data_schema_contract", this_object())) {
        contract = this_object()->query_data_schema_contract();
        if (mapp(contract) && intp(contract["schema_version"]))
            current_version = contract["schema_version"];
        if (stored_version > current_version)
            return 0;
        if (stored_version < current_version
            && function_exists("migrate_data_snapshot", this_object())) {
            err = catch(migrated = this_object()->migrate_data_snapshot(
                data, stored_version));
            if (err || !mapp(migrated))
                return 0;
            data = migrated;
        }
        if (function_exists("validate_data_snapshot", this_object())
            && !this_object()->validate_data_snapshot(data))
            return 0;
    }

    data = clone_data_value(data);
    if (!mapp(data) || !data_value_is_serializable(data))
        return 0;
    _dbase_data = data;
    _dbase_schema_version = current_version;
    _dbase_last_error = ([ "ok": 1, "code": "" ]);
    _dbase_modified = 0;
    _dbase_receipt_lookup_pending = owner_snapshot_enabled();
    schedule_receipt_lookup();

    return 1;
}

int query_data_schema_version()
{
    if (mapp(_dbase_data["persistence"])
        && intp(_dbase_data["persistence"]["schema_version"]))
        return _dbase_data["persistence"]["schema_version"];
    return _dbase_schema_version;
}

int is_modified()
{
    return _dbase_modified;
}

// 临时数据操作
int set_temp(string path, mixed value)
{
    string *parts;
    mapping node;
    int n;

    if (!mapp(_dbase_temp))
        _dbase_temp = ([]);

    if (!stringp(path) || path == "")
        return 0;

    parts = _parse_path(path);
    n = sizeof(parts);

    if (n == 0)
        return 0;

    if (n == 1) {
        _dbase_temp[parts[0]] = value;
    } else {
        node = _get_node(_dbase_temp, parts[0..n-2], 1);
        if (!mapp(node))
            return 0;
        node[parts[n-1]] = value;
    }

    return 1;
}

mixed query_temp(string path)
{
    string *parts;
    mapping node;
    int n;

    if (!mapp(_dbase_temp))
        _dbase_temp = ([]);

    if (!stringp(path) || path == "")
        return 0;

    parts = _parse_path(path);
    n = sizeof(parts);

    if (n == 0)
        return 0;

    if (n == 1) {
        return _dbase_temp[parts[0]];
    } else {
        node = _get_node(_dbase_temp, parts[0..n-2], 0);
        if (!mapp(node))
            return 0;
        return node[parts[n-1]];
    }
}

void clear_temp()
{
    _dbase_temp = ([]);
}

// 工具函数
string *dbase_keys()
{
    return keys(_dbase_data);
}

mixed *dbase_values()
{
    mixed *raw_values;
    mixed *detached_values;
    mixed value;
    int i;

    raw_values = values(_dbase_data);
    detached_values = ({ });
    for (i = 0; i < sizeof(raw_values); i++) {
        value = clone_data_value(raw_values[i]);
        if (!data_value_is_serializable(value))
            return ({ });
        detached_values += ({ value });
    }
    return detached_values;
}

int exists(string path)
{
    return _path_exists(path);
}

void clear()
{
    if (managed_player_bulk_write_denied()) {
        record_data_error("DATA_OWNER_REQUIRED", "", _dbase_data, 0);
        return;
    }
    _dbase_data = ([]);
    _set_modified();
    _schedule_save();
}

int dbase_size()
{
    return sizeof(_dbase_data);
}
