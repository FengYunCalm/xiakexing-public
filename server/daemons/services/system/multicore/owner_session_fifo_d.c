/**
 * @file    owner_session_fifo_d.c
 * @brief   owner 输出进入 session FIFO 的统一门面
 *
 * owner 只提交不携带对象的冻结输出载荷；本服务负责分配 Gateway
 * reservation、保持每会话顺序，并在唯一 dispatch 入口填充 driver FIFO。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

#define MAX_SESSION_OUTPUT_HISTORY 64
#define MAX_BOUNDARY_TOKEN_HISTORY 8192
#define BOUNDARY_TOKEN_EVICT_BATCH 256

nosave mapping _stream_epochs;
nosave mapping _queued_outputs;
nosave mapping _inflight_outputs;
nosave mapping _local_targets;
nosave mapping _boundary_tokens;
nosave mapping _boundary_token_order;
nosave int _boundary_token_order_head;
nosave int _boundary_token_order_tail;
nosave int _next_boundary_token;
nosave int _direct_socket_rejections;
nosave int _boundary_token_evictions;
nosave int _completed_outputs;
nosave int _released_outputs;
/* L-42：入口背压之外出现的超限只计数，不再按历史裁剪未完成输出。 */
nosave int _queue_overflow_pending;

private void mark_boundary_terminal(mapping entry, string state, string reason);
private int release_gateway_reservation(object target, mapping entry);
mapping dispatch_owner_output(mapping item);
mapping release_owner_output(string session_id, int reservation_id, string reason);

void create()
{
    ::create();
    _stream_epochs = ([]);
    _queued_outputs = ([]);
    _inflight_outputs = ([]);
    _local_targets = ([]);
    _boundary_tokens = ([]);
    _boundary_token_order = ([]);
    _boundary_token_order_head = 1;
    _boundary_token_order_tail = 1;
    _next_boundary_token = 1;
    _direct_socket_rejections = 0;
    _boundary_token_evictions = 0;
    _completed_outputs = 0;
    _released_outputs = 0;
    _queue_overflow_pending = 0;
}

private string query_session_stream_key(string session_id, string stream)
{
    return session_id + ":" + (stringp(stream) && stream != "" ? stream : "main");
}

private string query_reservation_key(string session_id, int reservation_id)
{
    return session_id + ":" + reservation_id;
}

private void prune_boundary_tokens()
{
    string token;
    int current_count;
    int evict_count;
    int i;

    current_count = _boundary_token_order_tail - _boundary_token_order_head;
    if (current_count <= MAX_BOUNDARY_TOKEN_HISTORY + BOUNDARY_TOKEN_EVICT_BATCH)
        return;

    evict_count = current_count - MAX_BOUNDARY_TOKEN_HISTORY;
    for (i = 0; i < evict_count; i++) {
        token = _boundary_token_order[_boundary_token_order_head];
        if (stringp(token) && token != "")
            map_delete(_boundary_tokens, token);
        map_delete(_boundary_token_order, _boundary_token_order_head);
        _boundary_token_order_head++;
    }
    _boundary_token_evictions += evict_count;
}

int query_stream_epoch(string session_id, string stream)
{
    string key;

    if (!stringp(session_id) || session_id == "")
        return 0;
    key = query_session_stream_key(session_id, stream);
    return intp(_stream_epochs[key]) ? _stream_epochs[key] : 0;
}

int bump_stream_epoch(string session_id, string stream)
{
    string key;

    if (!stringp(session_id) || session_id == "")
        return 0;
    key = query_session_stream_key(session_id, stream);
    _stream_epochs[key] = query_stream_epoch(session_id, stream) + 1;
    return _stream_epochs[key];
}

private mapping create_boundary_token(string session_id, string owner_id,
    string packet_type, string stream, int epoch, int server_seq,
    int reservation_id, mapping meta)
{
    string token;
    mapping entry;

    token = sprintf("fifo/%d/%s/%d", _next_boundary_token++, session_id,
        reservation_id);
    entry = ([
        "token": token,
        "session_id": session_id,
        "owner_id": owner_id,
        "packet_type": packet_type,
        "stream": stream,
        "stream_epoch": epoch,
        "server_seq": server_seq,
        "reservation_id": reservation_id,
        "owner_fifo_token": token,
        "state": "queued",
        "meta": mapp(meta) ? copy(meta) : ([]),
        "created_at": time(),
        "owner_output_boundary": 1,
        "fifo_required": 1,
        "direct_socket_write": 0,
    ]);
    _boundary_tokens[token] = entry;
    _boundary_token_order[_boundary_token_order_tail++] = token;
    prune_boundary_tokens();
    return entry;
}

/* Pure candidate construction.  It must not allocate tokens, bump epochs or
 * mutate Gateway reservation/accounting state. */
private mapping prepare_owner_output_boundary(object target, string owner_id,
    string packet_type, mapping meta)
{
    mapping fifo;
    string session_id;
    string stream;
    int epoch;
    int server_seq;
    int queue_sequence;

    if (!objectp(target) || !stringp(owner_id) || owner_id == ""
        || !stringp(packet_type) || packet_type == "")
        return ([ "error": "target, owner_id and packet_type are required" ]);

    session_id = GATEWAY_SESSION_D->query_object_session_id(target);
    if (!stringp(session_id) || session_id == "")
        return ([ "error": "session not found" ]);

    stream = mapp(meta) && stringp(meta["stream"]) && meta["stream"] != ""
        ? meta["stream"] : "main";
    epoch = mapp(meta) && intp(meta["epoch"]) && meta["epoch"] > 0
        ? meta["epoch"] : query_stream_epoch(session_id, stream);
    if (epoch <= 0)
        epoch = 1;

    fifo = GATEWAY_SESSION_D->query_session_fifo_snapshot(session_id);
    queue_sequence = mapp(fifo) && intp(fifo["last_server_seq"])
        && fifo["last_server_seq"] >= 0 ? fifo["last_server_seq"] + 1 : 1;
    server_seq = 0;
    if (mapp(meta) && meta["payload_contract"] == "encoded_protocol_frame") {
        /* 已编码协议帧必须自带 durable 正数序号：FIFO 不为它代填 wire 身份。 */
        if (!intp(meta["server_seq"]) || meta["server_seq"] <= 0)
            return ([ "error": "sequence_unassigned" ]);
        server_seq = meta["server_seq"];
        queue_sequence = server_seq;
    } else if (mapp(meta) && intp(meta["server_seq"]) && meta["server_seq"] > 0) {
        server_seq = meta["server_seq"];
        queue_sequence = server_seq;
    }
    return ([
        "session_id": session_id,
        "owner_id": owner_id,
        "packet_type": packet_type,
        "stream": stream,
        "stream_epoch": epoch,
        "server_seq": server_seq,
        "queue_sequence": queue_sequence,
        "state": "candidate",
        "meta": mapp(meta) ? copy(meta) : ([]),
        "owner_output_boundary": 1,
        "fifo_required": 1,
        "direct_socket_write": 0,
    ]);
}

mapping validate_owner_output_boundary(object target, string owner_id,
    string packet_type, mapping meta)
{
    mapping boundary;

    boundary = prepare_owner_output_boundary(target, owner_id, packet_type, meta);
    if (!mapp(boundary) || stringp(boundary["error"]))
        return ([ "ok": 0, "reason": mapp(boundary)
            ? (boundary["error"] || "boundary preparation failed")
            : "boundary preparation failed" ]);
    return ([ "ok": 1, "boundary": copy(boundary) ]);
}

/* Read-only diagnostics retain the old output/boundary shape without
 * allocating a reservation or placing an item in the queue. */
mapping preview_owner_output_boundary(object target, string owner_id,
    string packet_type, mapping meta)
{
    mapping result;
    mapping output;
    string session_id;

    result = validate_owner_output_boundary(target, owner_id, packet_type, meta);
    if (!mapp(result) || result["ok"] != 1)
        return result;
    output = copy(result["boundary"]);
    output["state"] = "queued";
    output["reservation_id"] = 0;
    session_id = output["session_id"];
    return ([
        "ok": 1,
        "output": output,
        "boundary": copy(output),
        "fifo": GATEWAY_SESSION_D->query_session_fifo_snapshot(session_id),
        "preview": 1,
    ]);
}

/* Compatibility probes use the pure validator.  They must never leave an
 * output reservation behind. */
int record_owner_output_boundary(object target, string owner_id,
    string packet_type, mapping meta)
{
    mapping result;

    result = validate_owner_output_boundary(target, owner_id, packet_type, meta);
    return mapp(result) && result["ok"] == 1;
}

int *record_owner_output_boundaries_batch(object *targets, string *owner_ids,
    string packet_type, mapping *metas)
{
    int *recorded;
    int count;
    int index;

    if (!arrayp(targets) || !arrayp(owner_ids) || !arrayp(metas)
        || !stringp(packet_type) || packet_type == "")
        return ({ });
    count = sizeof(targets);
    if (count <= 0 || sizeof(owner_ids) != count || sizeof(metas) != count)
        return ({ });

    recorded = allocate(count);
    for (index = 0; index < count; index++)
        recorded[index] = record_owner_output_boundary(targets[index],
            owner_ids[index], packet_type, metas[index]);
    return recorded;
}

private mapping query_local_target(string session_id)
{
    return objectp(_local_targets[session_id]) ? ([
        "target": _local_targets[session_id],
    ]) : 0;
}

private int uses_local_transport_session(string session_id)
{
    mapping session;
    mapping meta;

    if (!stringp(session_id) || session_id == "")
        return 0;
    session = GATEWAY_SESSION_D->query_session(session_id);
    meta = mapp(session) && mapp(session["meta"])
        ? session["meta"] : 0;
    return mapp(meta) && meta["source"] == "direct_proxy";
}

private object resolve_output_target(string session_id)
{
    object target;
    mapping local;

    target = GATEWAY_SESSION_D->query_session_target(session_id);
    if (objectp(target))
        return target;
    local = query_local_target(session_id);
    return mapp(local) && objectp(local["target"]) ? local["target"] : 0;
}

/* L-42：已接受的未完成输出不得按历史裁剪。容量在 enqueue_* 入口按
 * MAX_SESSION_OUTPUT_HISTORY 拒绝（不预留、不遗留 reservation）；此处只
 * 保留防御性计数，绝不 release 或切掉已接受的 pending 项。 */
private void append_queued_output(string session_id, mapping entry)
{
    if (!arrayp(_queued_outputs[session_id]))
        _queued_outputs[session_id] = ({ });
    _queued_outputs[session_id] += ({ entry });
    if (sizeof(_queued_outputs[session_id]) > MAX_SESSION_OUTPUT_HISTORY)
        _queue_overflow_pending++;
}

/* L-42：接收前的容量背压。返回 1 表示可以继续；0 表示拒绝本次输出，
 * 调用方不得继续 reserve。 */
private int owner_output_queue_has_capacity(string session_id)
{
    return !arrayp(_queued_outputs[session_id])
        || sizeof(_queued_outputs[session_id]) < MAX_SESSION_OUTPUT_HISTORY;
}

private mapping remove_queued_output(string session_id, int reservation_id)
{
    mapping *queued;
    mapping entry;
    int index;

    queued = arrayp(_queued_outputs[session_id])
        ? _queued_outputs[session_id] : ({ });
    for (index = 0; index < sizeof(queued); index++) {
        entry = queued[index];
        if (!mapp(entry) || entry["reservation_id"] != reservation_id)
            continue;
        if (index == 0)
            queued = sizeof(queued) > 1 ? queued[1..] : ({ });
        else if (index == sizeof(queued) - 1)
            queued = queued[0..index - 1];
        else
            queued = queued[0..index - 1] + queued[index + 1..];
        _queued_outputs[session_id] = queued;
        if (sizeof(queued) == 0)
            map_delete(_queued_outputs, session_id);
        return entry;
    }
    return 0;
}

private void mark_boundary_terminal(mapping entry, string state, string reason)
{
    string token;

    if (!mapp(entry))
        return;
    entry["state"] = state;
    if (stringp(reason) && reason != "")
        entry["terminal_reason"] = reason;
    token = stringp(entry["boundary_token"]) ? entry["boundary_token"] : "";
    if (token != "" && mapp(_boundary_tokens[token])) {
        _boundary_tokens[token]["state"] = state;
        if (stringp(reason) && reason != "")
            _boundary_tokens[token]["terminal_reason"] = reason;
    }
}

/* 物理释放与登记分别报告：native 已释放而登记失败时只补对账，不重发消息。
 * 重试时依靠 entry 上的 native_released/ledger_recorded 标记，不重复物理释放。 */
private mapping release_gateway_reservation_detail(object target, mapping entry)
{
    int reservation_id;
    int native_released;
    int ledger_recorded;

    if (!mapp(entry))
        return ([ "ok": 0, "native_released": 0, "ledger_recorded": 0 ]);
    if (entry["local_output"])
        return ([ "ok": 1, "native_released": 1, "ledger_recorded": 1 ]);
    reservation_id = intp(entry["reservation_id"]) ? entry["reservation_id"] : 0;
    if (reservation_id <= 0)
        return ([ "ok": 0, "native_released": 0, "ledger_recorded": 0 ]);

    native_released = entry["native_released"] ? 1 : 0;
    if (!native_released) {
        native_released = objectp(target)
            && gateway_session_release(target, reservation_id) ? 1 : 0;
        if (native_released)
            entry["native_released"] = 1;
    }
    ledger_recorded = entry["ledger_recorded"] ? 1 : 0;
    if (native_released && !ledger_recorded) {
        ledger_recorded = objectp(target)
            && GATEWAY_SESSION_D->record_session_output_release(target,
                reservation_id) ? 1 : 0;
        if (ledger_recorded)
            entry["ledger_recorded"] = 1;
    }
    return ([ "ok": native_released && ledger_recorded ? 1 : 0,
        "native_released": native_released,
        "ledger_recorded": ledger_recorded ]);
}

private int release_gateway_reservation(object target, mapping entry)
{
    int reservation_id;

    if (!objectp(target) || !mapp(entry))
        return 0;
    reservation_id = intp(entry["reservation_id"]) ? entry["reservation_id"] : 0;
    if (reservation_id <= 0 || entry["local_output"])
        return 1;

    return release_gateway_reservation_detail(target, entry)["ok"] == 1;
}

/* Reserve native output first, then publish the corresponding LPC ledger
 * entry.  If either step fails, no queued item survives. */
mapping enqueue_owner_output(string session_id, mapping frozen_payload)
{
    object target;
    mapping boundary;
    mapping safe_meta;
    mapping token;
    mapping entry;
    mapping fifo;
    string owner_id;
    string packet_type;
    string frame;
    int reservation_id;
    int epoch;
    int server_seq;

    if (!stringp(session_id) || session_id == "" || !mapp(frozen_payload))
        return ([ "ok": 0, "reason": "session_id and frozen payload are required" ]);
    /* L-42：接收前背压；拒绝项不 reserve、不遗留 reservation。 */
    if (!owner_output_queue_has_capacity(session_id))
        return ([ "ok": 0, "reason": "session output queue full",
            "queue_overflow": 1, "reservation_state": "none" ]);
    target = resolve_output_target(session_id);
    if (!objectp(target))
        return ([ "ok": 0, "reason": "session target unavailable" ]);

    owner_id = stringp(frozen_payload["owner_id"])
        && frozen_payload["owner_id"] != ""
            ? frozen_payload["owner_id"] : "owner/output/" + session_id;
    packet_type = stringp(frozen_payload["packet_type"])
        && frozen_payload["packet_type"] != ""
            ? frozen_payload["packet_type"] : "raw_text";
    frame = stringp(frozen_payload["frame"]) ? frozen_payload["frame"]
        : (stringp(frozen_payload["payload"]) ? frozen_payload["payload"] : "");
    if (frame == "")
        return ([ "ok": 0, "reason": "frozen output frame is required" ]);

    safe_meta = mapp(frozen_payload["meta"])
        ? copy(frozen_payload["meta"]) : ([ ]);
    boundary = prepare_owner_output_boundary(target, owner_id, packet_type,
        safe_meta);
    if (!mapp(boundary) || stringp(boundary["error"]))
        return ([ "ok": 0, "reason": mapp(boundary)
            ? (boundary["error"] || "boundary preparation failed")
            : "boundary preparation failed" ]);

    epoch = boundary["stream_epoch"];
    if (query_stream_epoch(session_id, boundary["stream"]) <= 0)
        epoch = bump_stream_epoch(session_id, boundary["stream"]);
    server_seq = boundary["server_seq"];
    safe_meta["stream"] = boundary["stream"];
    safe_meta["epoch"] = epoch;
    safe_meta["owner_fifo_reservation"] = 1;
    safe_meta["owner_fifo_state"] = "queued";
    /* raw text 只有排队序号；XK 协议序号只来自真的 durable 分配。 */
    safe_meta["queue_sequence"] = boundary["queue_sequence"];
    safe_meta["owner_fifo_sequence"] = boundary["queue_sequence"];
    if (boundary["server_seq"] > 0) {
        safe_meta["server_seq"] = boundary["server_seq"];
        safe_meta["owner_fifo_reserved_seq"] = boundary["server_seq"];
    }

    reservation_id = gateway_session_reserve(target);
    if (reservation_id <= 0)
        return ([ "ok": 0, "reason": "gateway output reservation failed" ]);
    if (!GATEWAY_SESSION_D->record_session_output_reservation(target,
        packet_type, safe_meta, reservation_id)) {
        gateway_session_release(target, reservation_id);
        return ([ "ok": 0, "reason": "gateway reservation ledger failed" ]);
    }

    token = create_boundary_token(session_id, owner_id, packet_type,
        boundary["stream"], epoch, server_seq, reservation_id, safe_meta);
    entry = ([
        "session_id": session_id,
        "owner_id": owner_id,
        "packet_type": packet_type,
        "stream": boundary["stream"],
        "stream_epoch": epoch,
        "server_seq": server_seq,
        "sequence": boundary["queue_sequence"],
        "reservation_id": reservation_id,
        "boundary_token": token["token"],
        "meta": safe_meta,
        "frame": frame,
        "state": "queued",
        "queued_at": time(),
        "owner_output_boundary": 1,
        "fifo_required": 1,
        "direct_socket_write": 0,
        "local_output": 0,
    ]);
    append_queued_output(session_id, entry);
    fifo = GATEWAY_SESSION_D->query_session_fifo_snapshot(session_id);
    return ([
        "ok": 1,
        "output": copy(entry),
        "boundary": copy(token),
        "fifo": fifo,
    ]);
}

/* 本地/direct-proxy 输出没有真实 session close：终态必须清理
 * 按目标稳定的 _local_targets 与 _stream_epochs，状态不随消息数增长。 */
private void clear_local_output_state(string session_id)
{
    mapping epoch_snapshot;
    string key;

    if (!stringp(session_id) || session_id == ""
        || strsrch(session_id, "local/") != 0)
        return;
    map_delete(_local_targets, session_id);
    epoch_snapshot = copy(_stream_epochs);
    foreach (key, mixed epoch in epoch_snapshot) {
        if (strsrch(key, session_id + ":") != 0)
            continue;
        map_delete(_stream_epochs, key);
    }
}

private mapping enqueue_local_owner_output(object target, string owner_id,
    string packet_type, mapping meta, string frame)
{
    mapping boundary;
    mapping token;
    mapping entry;
    string session_id;
    int reservation_id;
    int epoch;
    int server_seq;

    if (!objectp(target) || !stringp(frame) || frame == "")
        return ([ "ok": 0, "reason": "local output target and frame are required" ]);
    /* 按目标稳定的 key，不再按消息生成新会话。 */
    session_id = "local/" + file_name(target);
    /* L-42：local 会话与普通会话同一背压合同,拒绝项不 reserve。 */
    if (!owner_output_queue_has_capacity(session_id))
        return ([ "ok": 0, "reason": "session output queue full",
            "queue_overflow": 1, "reservation_state": "none" ]);
    epoch = query_stream_epoch(session_id, meta["stream"]);
    if (epoch <= 0)
        epoch = bump_stream_epoch(session_id, meta["stream"]);
    server_seq = 1;
    reservation_id = -_next_boundary_token++;
    _local_targets[session_id] = target;
    boundary = ([
        "session_id": session_id,
        "owner_id": owner_id,
        "packet_type": packet_type,
        "stream": meta["stream"],
        "stream_epoch": epoch,
        "server_seq": server_seq,
    ]);
    token = create_boundary_token(session_id, owner_id, packet_type,
        meta["stream"], epoch, server_seq, reservation_id, meta);
    entry = ([
        "session_id": session_id,
        "owner_id": owner_id,
        "packet_type": packet_type,
        "stream": meta["stream"],
        "stream_epoch": epoch,
        "server_seq": server_seq,
        "sequence": server_seq,
        "reservation_id": reservation_id,
        "boundary_token": token["token"],
        "meta": copy(meta),
        "frame": frame,
        "state": "queued",
        "queued_at": time(),
        "owner_output_boundary": 1,
        "fifo_required": 1,
        "direct_socket_write": 0,
        "local_output": 1,
    ]);
    append_queued_output(session_id, entry);
    return ([ "ok": 1, "output": copy(entry), "boundary": copy(token) ]);
}

mapping enqueue_owner_output_for_target(object target, string owner_id,
    string packet_type, mapping meta, string frame)
{
    mapping result;
    mapping frozen_payload;
    string session_id;
    mixed err;

    if (!objectp(target) || !stringp(frame) || frame == "")
        return ([ "ok": 0, "reason": "target and frame are required" ]);
    meta = mapp(meta) ? copy(meta) : ([ ]);
    session_id = GATEWAY_SESSION_D->query_object_session_id(target);
    frozen_payload = ([
        "owner_id": stringp(owner_id) ? owner_id : "",
        "packet_type": stringp(packet_type) ? packet_type : "raw_text",
        "meta": meta,
        "frame": frame,
    ]);
    if (uses_local_transport_session(session_id))
        result = enqueue_local_owner_output(target, owner_id,
            stringp(packet_type) ? packet_type : "raw_text", meta, frame);
    else if (stringp(session_id) && session_id != "")
        result = enqueue_owner_output(session_id, frozen_payload);
    else
        result = enqueue_local_owner_output(target, owner_id,
            stringp(packet_type) ? packet_type : "raw_text", meta, frame);
    if (!mapp(result) || result["ok"] != 1)
        return mapp(result) ? result
            : ([ "ok": 0, "reason": "owner output enqueue failed",
                "stage": "queued", "reservation_state": "none" ]);

    err = catch(result["dispatch"] = dispatch_owner_output(result["output"]));
    if (err || !mapp(result["dispatch"]))
        return ([ "ok": 0, "stage": "queued",
            "reservation_state": "unknown",
            "reason": err
                ? sprintf("owner output dispatch failed: %O", err)
                : "owner output dispatch failed",
            "output": result["output"] ]);
    if (result["dispatch"]["ok"] != 1) {
        /* 分发阶段失败不得被入队成功遮蔽：stage 区分 fill 失败与已填充待对账，
         * 调用方不得据此重发同一 payload。 */
        return ([
            "ok": 0,
            "stage": stringp(result["dispatch"]["stage"])
                ? result["dispatch"]["stage"] : "fill_failed",
            "reservation_state": stringp(result["dispatch"]["reservation_state"])
                ? result["dispatch"]["reservation_state"] : "unknown",
            "reason": result["dispatch"]["reason"]
                || "owner output dispatch failed",
            "output": result["output"],
            "dispatch": result["dispatch"],
        ]);
    }
    return result;
}

/* L-41：只读查看当前队头，不改变队列/inflight 状态。 */
mapping peek_owner_output(string session_id)
{
    if (!stringp(session_id) || session_id == ""
        || !arrayp(_queued_outputs[session_id])
        || sizeof(_queued_outputs[session_id]) == 0)
        return 0;
    return copy(_queued_outputs[session_id][0]);
}

mapping dequeue_owner_output(string session_id)
{
    mapping *queued;
    mapping entry;
    string key;

    if (!stringp(session_id) || session_id == ""
        || !arrayp(_queued_outputs[session_id])
        || sizeof(_queued_outputs[session_id]) == 0)
        return 0;
    queued = _queued_outputs[session_id];
    entry = queued[0];
    _queued_outputs[session_id] = sizeof(queued) > 1 ? queued[1..] : ({ });
    if (sizeof(_queued_outputs[session_id]) == 0)
        map_delete(_queued_outputs, session_id);
    entry["state"] = "dispatching";
    key = query_reservation_key(session_id, entry["reservation_id"]);
    _inflight_outputs[key] = entry;
    return copy(entry);
}

private mapping take_inflight_owner_output(string session_id, int reservation_id)
{
    string key;
    mapping entry;

    key = query_reservation_key(session_id, reservation_id);
    entry = _inflight_outputs[key];
    if (!mapp(entry))
        return 0;
    map_delete(_inflight_outputs, key);
    return entry;
}

private void dispatch_next_owner_output(string session_id)
{
    mapping next;

    next = arrayp(_queued_outputs[session_id])
        && sizeof(_queued_outputs[session_id]) > 0
            ? _queued_outputs[session_id][0] : 0;
    if (mapp(next))
        dispatch_owner_output(next);
}

mapping dispatch_owner_output(mapping item)
{
    object target;
    mapping entry;
    mapping authority;
    mapping payload;
    string session_id;
    string key;
    string packet_type;
    int reservation_id;
    int filled;
    int completed;

    if (!mapp(item) || !stringp(item["session_id"])
        || item["session_id"] == "" || item["state"] != "queued")
        return ([ "ok": 0, "reason": "queued owner output item is required" ]);
    session_id = item["session_id"];
    /* L-41：先校验当前队头身份再移除；非法/过期请求不得消耗合法队头。 */
    authority = peek_owner_output(session_id);
    if (!mapp(authority) || authority["reservation_id"] != item["reservation_id"])
        return ([ "ok": 0, "reason": "owner output queue head changed",
            "stage": "queued", "reservation_state": "queued" ]);
    entry = dequeue_owner_output(session_id);
    if (!mapp(entry) || entry["reservation_id"] != item["reservation_id"])
        return ([ "ok": 0, "reason": "owner output queue head changed" ]);

    target = resolve_output_target(session_id);
    reservation_id = intp(entry["reservation_id"]) ? entry["reservation_id"] : 0;
    packet_type = stringp(entry["packet_type"]) ? entry["packet_type"] : "raw_text";
    if (!objectp(target)) {
        release_owner_output(session_id, reservation_id, "target unavailable");
        return ([ "ok": 0, "reason": "session target unavailable",
            "stage": "fill_failed", "state": "released",
            "reservation_state": "released" ]);
    }

    if (consume_test_fault("fifo_fill")) {
        filled = 0;
    } else if (entry["local_output"]) {
        filled = catch(tell_object(target, entry["frame"])) ? 0 : 1;
    } else {
        filled = gateway_session_fill(target, reservation_id, entry["frame"]);
    }
    if (!filled) {
        release_owner_output(session_id, reservation_id, "output fill failed");
        return ([ "ok": 0, "reason": "output fill failed",
            "stage": "fill_failed", "state": "released",
            "reservation_state": "released" ]);
    }

    payload = ([ "meta": mapp(entry["meta"]) ? entry["meta"] : ([ ]) ]);
    if (consume_test_fault("fifo_completion")) {
        completed = 0;
    } else if (!entry["local_output"]) {
        completed = GATEWAY_SESSION_D->record_session_output_completion(
            target, packet_type, payload, reservation_id);
    } else {
        completed = 1;
    }
    if (!completed) {
        /* native fill 已成功：保留“已填充、待对账”终态，不返回普通成功，
         * 也不允许调用方重发同一 payload。L-40：inflight 权威 entry 必须
         * 与 dequeue 返回的副本一并更新，恢复入口才能找到待对账项。 */
        key = query_reservation_key(session_id, reservation_id);
        authority = _inflight_outputs[key];
        if (mapp(authority))
            mark_boundary_terminal(authority, "completion_pending",
                "gateway completion ledger failed");
        mark_boundary_terminal(entry, "completion_pending",
            "gateway completion ledger failed");
        GATEWAY_SESSION_D->record_owner_protocol_output("completion_failed");
        return ([
            "ok": 0,
            "state": "completion_pending",
            "stage": "filled",
            "reservation_state": "filled_pending_reconcile",
            "reservation_id": reservation_id,
            "ledger_completed": 0,
            "retry_token": sprintf("owner-output-completion/%s/%d",
                session_id, reservation_id),
        ]);
    }
    entry = take_inflight_owner_output(session_id, reservation_id);
    if (!mapp(entry))
        entry = item;
    mark_boundary_terminal(entry, "completed", "");
    if (entry["local_output"])
        clear_local_output_state(session_id);
    _completed_outputs++;
    dispatch_next_owner_output(session_id);
    return ([
        "ok": 1,
        "state": "completed",
        "stage": "completion_recorded",
        "reservation_state": "completed",
        "reservation_id": reservation_id,
        "ledger_completed": 1,
    ]);
}

int query_pending_owner_output_completion_count()
{
    int count;

    foreach (string key, mapping entry in _inflight_outputs)
        if (mapp(entry) && entry["state"] == "completion_pending")
            count++;
    return count;
}

/* L-40：仅重试 ledger 完成登记（不重新填充帧）。dispatch 已把 inflight
 * 权威 entry 标为 completion_pending；成功后清理 inflight 并推进同会话队列。
 * 返回结构化统计供诊断/运维核查。 */
mapping recover_pending_owner_output_completions(int limit)
{
    mapping snapshot;
    mapping candidate;
    mapping authority;
    mapping result;
    object target;
    string session_id;
    string key;
    string packet_type;
    int reservation_id;
    int scanned;
    int completed;
    int failed;

    if (!intp(limit) || limit <= 0)
        limit = 16;
    if (limit > 128)
        limit = 128;
    result = ([ "scanned": 0, "completed": 0, "failed": 0, "remaining": 0 ]);
    snapshot = copy(_inflight_outputs);
    foreach (key, candidate in snapshot) {
        if (scanned >= limit)
            break;
        if (!mapp(candidate) || candidate["state"] != "completion_pending")
            continue;
        scanned++;
        session_id = candidate["session_id"];
        reservation_id = intp(candidate["reservation_id"])
            ? candidate["reservation_id"] : 0;
        packet_type = stringp(candidate["packet_type"])
            ? candidate["packet_type"] : "raw_text";
        target = resolve_output_target(session_id);
        if (!objectp(target) || reservation_id <= 0) {
            failed++;
            continue;
        }
        if (!GATEWAY_SESSION_D->record_session_output_completion(target,
            packet_type, ([ "meta": mapp(candidate["meta"])
                ? candidate["meta"] : ([ ]) ]), reservation_id)) {
            failed++;
            continue;
        }
        authority = _inflight_outputs[key];
        if (mapp(authority)) {
            mark_boundary_terminal(authority, "completed",
                "completion recovered");
            map_delete(_inflight_outputs, key);
            _completed_outputs++;
            dispatch_next_owner_output(session_id);
        }
        completed++;
    }
    result["scanned"] = scanned;
    result["completed"] = completed;
    result["failed"] = failed;
    result["remaining"] = query_pending_owner_output_completion_count();
    return result;
}

mapping release_owner_output(string session_id, int reservation_id, string reason)
{
    mapping entry;
    mapping release_result;
    object target;
    string key;
    int native_released;
    int ledger_recorded;

    if (!stringp(session_id) || session_id == "" || reservation_id == 0)
        return ([ "ok": 0, "reason": "session and reservation are required" ]);

    entry = remove_queued_output(session_id, reservation_id);
    if (!mapp(entry)) {
        key = query_reservation_key(session_id, reservation_id);
        entry = _inflight_outputs[key];
        if (mapp(entry))
            map_delete(_inflight_outputs, key);
    }
    if (!mapp(entry)) {
        foreach (string token, mapping candidate in _boundary_tokens) {
            if (candidate["session_id"] == session_id
                && candidate["reservation_id"] == reservation_id
                && (candidate["state"] == "released"
                    || candidate["state"] == "completed"))
                return ([ "ok": 1, "idempotent": 1,
                    "state": candidate["state"] ]);
        }
        return ([ "ok": 0, "reason": "owner output reservation not found" ]);
    }

    target = resolve_output_target(session_id);
    if (consume_test_fault("fifo_release")) {
        release_result = ([ "native_released": 0, "ledger_recorded": 0 ]);
    } else {
        release_result = release_gateway_reservation_detail(target, entry);
    }
    native_released = release_result["native_released"] ? 1 : 0;
    ledger_recorded = release_result["ledger_recorded"] ? 1 : 0;

    if (!native_released) {
        /* 释放失败不得写完整终态：保留可重试记录，重试必须重新执行释放。 */
        entry["state"] = "release_pending";
        _inflight_outputs[query_reservation_key(session_id, reservation_id)] = entry;
        mark_boundary_terminal(entry, "release_failed", reason);
        return ([
            "ok": 0,
            "state": "release_pending",
            "stage": "release_failed",
            "reservation_state": "release_pending",
            "reservation_id": reservation_id,
            "retry_token": sprintf("owner-output-release/%s/%d",
                session_id, reservation_id),
            "reason": stringp(reason) ? reason : "",
        ]);
    }

    if (!ledger_recorded) {
        /* 物理释放已完成：只保留对账记录，绝不重发消息。 */
        entry["state"] = "release_ledger_pending";
        _inflight_outputs[query_reservation_key(session_id, reservation_id)] = entry;
        mark_boundary_terminal(entry, "release_ledger_pending", reason);
        GATEWAY_SESSION_D->record_owner_protocol_output("release_failed");
        return ([
            "ok": 0,
            "state": "release_ledger_pending",
            "stage": "released",
            "reservation_state": "release_ledger_pending",
            "reservation_id": reservation_id,
            "retry_token": sprintf("owner-output-release-ledger/%s/%d",
                session_id, reservation_id),
            "reason": stringp(reason) ? reason : "",
        ]);
    }

    mark_boundary_terminal(entry, "released", reason);
    _released_outputs++;
    if (entry["local_output"])
        clear_local_output_state(session_id);
    return ([
        "ok": 1,
        "state": "released",
        "stage": "released",
        "reservation_state": "released",
        "reservation_id": reservation_id,
        "reason": stringp(reason) ? reason : "",
    ]);
}

mapping release_session_outputs(string session_id, string reason)
{
    mapping result;
    mapping entry;
    mapping *queued;
    mapping inflight;
    mapping inflight_snapshot;
    string key;
    int released;
    int failed;

    result = ([ "session_id": session_id, "released": 0, "failed": 0 ]);
    if (!stringp(session_id) || session_id == "")
        return result;

    queued = arrayp(_queued_outputs[session_id]) ? _queued_outputs[session_id] : ({ });
    foreach (entry in queued) {
        if (!mapp(entry) || !intp(entry["reservation_id"]))
            continue;
        if (release_owner_output(session_id, entry["reservation_id"], reason)["ok"])
            released++;
        else
            failed++;
    }
    inflight_snapshot = copy(_inflight_outputs);
    foreach (key, inflight in inflight_snapshot) {
        if (!mapp(inflight) || inflight["session_id"] != session_id)
            continue;
        if (release_owner_output(session_id, inflight["reservation_id"], reason)["ok"])
            released++;
        else
            failed++;
    }
    result["released"] = released;
    result["failed"] = failed;
    return result;
}

/* Called only after the Gateway has stopped producing for this session. */
mapping clear_session_output_state(string session_id)
{
    mapping result;
    mapping epoch_snapshot;
    string key;
    int cleared_epochs;

    result = ([ "session_id": session_id, "ok": 0 ]);
    if (!stringp(session_id) || session_id == "")
        return result;
    if (arrayp(_queued_outputs[session_id])
        && sizeof(_queued_outputs[session_id]) > 0)
        return ([ "session_id": session_id, "ok": 0,
            "reason": "queued owner output remains" ]);
    foreach (string inflight_key, mapping entry in _inflight_outputs)
        if (mapp(entry) && entry["session_id"] == session_id)
            return ([ "session_id": session_id, "ok": 0,
                "reason": "inflight owner output remains" ]);

    epoch_snapshot = copy(_stream_epochs);
    foreach (key, mixed epoch in epoch_snapshot) {
        if (strsrch(key, session_id + ":") != 0)
            continue;
        map_delete(_stream_epochs, key);
        cleared_epochs++;
    }
    map_delete(_queued_outputs, session_id);
    map_delete(_local_targets, session_id);
    result["ok"] = 1;
    result["cleared_epochs"] = cleared_epochs;
    return result;
}

mapping reject_direct_socket_write(string owner_id, string packet_type, string reason)
{
    _direct_socket_rejections++;
    return ([
        "ok": 0,
        "reason": stringp(reason) && reason != ""
            ? reason : "owner output must enter session fifo",
        "owner_id": stringp(owner_id) ? owner_id : "",
        "packet_type": stringp(packet_type) ? packet_type : "",
        "session_fifo_boundary": 1,
        "direct_socket_write": 0,
        "rejected": 1,
    ]);
}

mapping query_session_fifo(string session_id)
{
    mapping fifo;

    fifo = GATEWAY_SESSION_D->query_session_fifo_snapshot(session_id);
    if (!mapp(fifo)) {
        /* L-40：无底层 session snapshot 也必须报告真实 inflight，不得报 0。 */
        mapping local_result;
        local_result = ([
            "session_id": session_id,
            "queued_outputs": arrayp(_queued_outputs[session_id])
                ? sizeof(_queued_outputs[session_id]) : 0,
            "inflight_outputs": 0,
            "completion_pending_outputs": 0,
        ]);
        foreach (string key, mapping entry in _inflight_outputs) {
            if (!mapp(entry) || entry["session_id"] != session_id)
                continue;
            local_result["inflight_outputs"]++;
            if (entry["state"] == "completion_pending")
                local_result["completion_pending_outputs"]++;
        }
        return local_result;
    }
    fifo["queued_outputs"] = arrayp(_queued_outputs[session_id])
        ? sizeof(_queued_outputs[session_id]) : 0;
    fifo["inflight_outputs"] = 0;
    fifo["completion_pending_outputs"] = 0;
    foreach (string key, mapping entry in _inflight_outputs) {
        if (!mapp(entry) || entry["session_id"] != session_id)
            continue;
        fifo["inflight_outputs"]++;
        if (entry["state"] == "completion_pending")
            fifo["completion_pending_outputs"]++;
    }
    return fifo;
}

mapping query_status()
{
    int queued;
    int inflight;
    int local_state_entries;
    string session_id;
    string local_key;
    mapping *entries;
    mapping entry;

    foreach (session_id, entries in _queued_outputs)
        if (arrayp(entries))
            queued += sizeof(entries);
    foreach (string key, entry in _inflight_outputs)
        if (mapp(entry))
            inflight++;
    foreach (local_key, object local_target in _local_targets)
        if (strsrch(local_key, "local/") == 0)
            local_state_entries++;
    foreach (local_key, mixed local_epoch in _stream_epochs)
        if (strsrch(local_key, "local/") == 0)
            local_state_entries++;
    return ([
        "stream_epochs": sizeof(_stream_epochs),
        "sessions_with_outputs": sizeof(_queued_outputs),
        "queued_outputs": queued,
        "inflight_outputs": inflight,
        "completion_pending_outputs":
            query_pending_owner_output_completion_count(),
        "queue_overflow_pending": _queue_overflow_pending,
        "local_state_entries": local_state_entries,
        "boundary_tokens": sizeof(_boundary_tokens),
        "boundary_token_order":
            _boundary_token_order_tail - _boundary_token_order_head,
        "max_session_output_history": MAX_SESSION_OUTPUT_HISTORY,
        "max_boundary_token_history": MAX_BOUNDARY_TOKEN_HISTORY,
        "boundary_token_evictions": _boundary_token_evictions,
        "completed_outputs": _completed_outputs,
        "released_outputs": _released_outputs,
        "direct_socket_rejections": _direct_socket_rejections,
        "mode": "owner_session_fifo_enforced_boundary",
        "session_fifo_boundary": 1,
        "owner_output_boundary": 1,
        "business_output_must_use_fifo": 1,
        "message_runtime_fifo_boundary": 1,
        "raw_text_fifo_boundary": 1,
        "direct_socket_rejection_ready": 1,
        "facade_only_runtime_claims": 0,
        "fifo_required": 1,
        "direct_socket_write": 0,
    ]);
}
