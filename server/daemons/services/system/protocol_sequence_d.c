#pragma strict_types
#pragma save_types

#include <system/globals.h>
#include <system/services.h>

inherit "/lib/framework/daemon";

#define PROTOCOL_SEQ_RETRY_SECONDS 5
#define PROTOCOL_SEQ_RETRY_LIMIT 3

int _server_seq;
nosave int _persisted_seq;
nosave int _persist_pending;
nosave int _pending_seq;
nosave int _persist_retry_count;
nosave int _persist_retry_handle;
nosave string _last_persist_result;
/* L-234：序号基线未知（恢复失败）时置位，拒发新序号直到显式恢复。 */
nosave int _baseline_unknown;
nosave string _last_persist_error;
nosave int _operation_seq;
nosave string _operation_namespace;
nosave string _pending_operation_id;
nosave int _pending_snapshot_id;

private void retry_pending();

private string query_save_file()
{
    return DATA_DIR + "runtime/protocol_sequence_d";
}

private string next_operation_id()
{
    if (!stringp(_operation_namespace))
        _operation_namespace = sprintf("%016x%016x%016x",
            secure_random(MAX_INT), secure_random(MAX_INT), secure_random(MAX_INT));
    _operation_seq++;
    return sprintf("protocol-seq/%s/%d", _operation_namespace, _operation_seq);
}

private int persist_current(string operation_id)
{
    string runtime_dir;
    mixed err;
    mapping result;

    runtime_dir = DATA_DIR + "runtime";
    if (file_size(runtime_dir) == -1 && !mkdir(runtime_dir)) {
        _last_persist_result = "not_written";
        _last_persist_error = "runtime directory creation failed";
        return 0;
    }
    err = catch(result = owner_save_object_phases(query_save_file(),
        "owner/global/service/system/protocol_sequence", "protocol_sequence", ([
            "server_seq": _server_seq,
            "operation_id": operation_id,
        ]), operation_id));
    _last_persist_result = !err && mapp(result) ? result["outcome"] : "unknown";
    _last_persist_error = err ? sprintf("%O", err) : "";
    if (mapp(result)) {
        _pending_snapshot_id = result["snapshot_id"];
        if (result["persisted"] == 1)
            _persisted_seq = _server_seq;
        if (result["ok"] == 1)
            return 1;
    }
    if (_last_persist_result == "unknown")
        _baseline_unknown = 1;
    return _last_persist_result == "persisted_receipt_pending" ? 2 : 0;
}

private void schedule_retry()
{
    if (_persist_retry_handle || !_persist_pending
        || _persist_retry_count >= PROTOCOL_SEQ_RETRY_LIMIT)
        return;
    _persist_retry_handle = call_out((: retry_pending :), PROTOCOL_SEQ_RETRY_SECONDS);
}

private void retry_pending()
{
    int previous_seq;
    int saved;

    if (_persist_retry_handle)
        remove_call_out(_persist_retry_handle);
    _persist_retry_handle = 0;
    if (!_persist_pending)
        return;
    previous_seq = _server_seq;
    _server_seq = _pending_seq;
    _persist_retry_count++;
    saved = persist_current(_pending_operation_id);
    if (saved == 1) {
        _persist_pending = 0;
        _pending_seq = 0;
        _pending_operation_id = "";
        _pending_snapshot_id = 0;
        _persist_retry_count = 0;
        _baseline_unknown = 0;
        return;
    }
    if (_last_persist_result == "not_written")
        _server_seq = previous_seq;
    schedule_retry();
}

private void restore_pending_save()
{
    mapping result;
    mapping snapshot;
    mapping payload;
    mixed err;

    err = catch(result = OWNER_PERSISTENCE_D->query_pending_publication(
        "owner/global/service/system/protocol_sequence", "protocol_sequence", query_save_file() + ".o"));
    if (err || !mapp(result) || result["ok"] != 1) {
        _baseline_unknown = 1;
        _last_persist_result = "unknown";
        _last_persist_error = err ? sprintf("%O", err) : sprintf("%O", result);
        return;
    }
    snapshot = result["snapshot"];
    if (!mapp(snapshot))
        return;
    payload = snapshot["payload"];
    if (!mapp(payload) || !stringp(payload["operation_id"]) || payload["operation_id"] == ""
        || !intp(payload["server_seq"]) || payload["server_seq"] < _server_seq) {
        _baseline_unknown = 1;
        _last_persist_result = "unknown";
        _last_persist_error = "pending sequence identity is invalid";
        return;
    }
    _pending_operation_id = payload["operation_id"];
    _pending_seq = payload["server_seq"];
    _pending_snapshot_id = snapshot["id"];
    _persist_pending = 1;
    schedule_retry();
}

void create()
{
    int restored;

    ::create();
    _server_seq = 0;
    _persisted_seq = 0;
    _persist_pending = 0;
    _pending_seq = 0;
    _persist_retry_count = 0;
    _persist_retry_handle = 0;
    _last_persist_result = "not_attempted";
    _last_persist_error = "";
    if (file_size(query_save_file() + ".o") != -1
        && (catch(restored = restore_object(query_save_file())) || !restored)) {
        _server_seq = 0;
        _last_persist_result = "restore_failed";
        _last_persist_error = "protocol sequence restore failed";
        /* L-234：恢复失败时不能假装“从 0 开始”继续发放序号——客户端可能
         * 仍持有更高的 latestEpoch，复用低位会被当成旧帧丢弃/去重。恢复
         * 未知即 fail-closed：拒发新序号（不发帧），并暴露待恢复状态；
         * 由 recover_sequence_baseline() 用可核对的基线恢复。 */
        _baseline_unknown = 1;
    }
    if (!intp(_server_seq) || _server_seq < 0)
        _server_seq = 0;
    _persisted_seq = _server_seq;
    restore_pending_save();
}

int reserve_server_seq_block(int count)
{
    int previous_seq;
    int first_seq;
    int candidate_seq;

    if (consume_test_fault("server_seq"))
        return 0;
    /* L-234：基线未知时拒绝分配，避免下发与客户端旧 epoch 冲突的低位序号。 */
    if (_baseline_unknown && !_persist_pending)
        return 0;
    if (!intp(count) || count <= 0)
        return 0;
    if (_persist_pending) {
        retry_pending();
        if (_persist_pending)
            return 0;
    }
    if (count > MAX_INT - _server_seq)
        return 0;
    previous_seq = _server_seq;
    first_seq = previous_seq + 1;
    candidate_seq = previous_seq + count;
    _server_seq = candidate_seq;
    /* The range is not returned until its high watermark is durable. */
    _pending_operation_id = next_operation_id();
    if (persist_current(_pending_operation_id) != 1) {
        if (_last_persist_result == "not_written")
            _server_seq = previous_seq;
        _pending_seq = candidate_seq;
        _persist_pending = 1;
        _persist_retry_count = 0;
        schedule_retry();
        return 0;
    }
    _pending_operation_id = "";
    _pending_snapshot_id = 0;
    return first_seq;
}

/* Reserve uses a durable high-watermark commit before returning the range. */
int query_next_server_seq()
{
    return reserve_server_seq_block(1);
}

int persist_protocol_counters()
{
    if (_persist_pending) {
        retry_pending();
        return !_persist_pending;
    }
    return !_baseline_unknown && _persisted_seq == _server_seq;
}

int query_server_seq_watermark()
{
    return _persisted_seq;
}

/* L-234：恢复入口。基线必须高于任何可能已下发的序号，否则拒绝。
 * 恢复成功后清除“基线未知”，序号重新可发放。返回结构化结果。 */
mapping recover_sequence_baseline(int baseline)
{
    if (_persist_pending)
        return ([ "ok": 0, "recovered": 0, "error": "original publication remains pending" ]);
    if (!intp(baseline) || baseline <= _server_seq)
        return ([ "ok": 0, "recovered": 0,
            "error": "baseline must be an integer greater than the current low value",
            "current_low": _server_seq ]);
    _server_seq = baseline;
    _pending_operation_id = next_operation_id();
    if (persist_current(_pending_operation_id) != 1) {
        _baseline_unknown = 1;
        _persist_pending = 1;
        _pending_seq = baseline;
        _persist_retry_count = 0;
        schedule_retry();
        return ([ "ok": 0, "recovered": 0,
            "error": "baseline persist failed", "baseline": baseline ]);
    }
    _baseline_unknown = 0;
    _pending_operation_id = "";
    _pending_snapshot_id = 0;
    return ([ "ok": 1, "recovered": 1, "baseline": baseline ]);
}

mapping query_persistence_status()
{
    return ([
        "server_seq": _server_seq,
        "baseline_unknown": _baseline_unknown ? 1 : 0,
        "persisted_seq": _persisted_seq,
        "persist_pending": _persist_pending,
        "pending_seq": _pending_seq,
        "pending_operation_id": _pending_operation_id,
        "pending_snapshot_id": _pending_snapshot_id,
        "retry_count": _persist_retry_count,
        "last_persist_result": _last_persist_result,
        "last_persist_error": _last_persist_error,
    ]);
}
