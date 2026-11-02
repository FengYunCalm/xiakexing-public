/**
 * @file    xk_protocol/delivery.c
 * @brief   XK 协议核心 - 发送链、reservation 与记账
 *
 * @par Description:
 * 含 {msg_send, _create_message, record_xk_protocol_tail_probe, record_xk_protocol_encode_tail_cpu_sample, record_xk_protocol_phase_timings, record_owner_protocol_output, should_probe_xk_protocol_target, record_session_output_fifo, record_session_output_reservation, record_session_output_completion, record_session_output_release, encode_reserved_protocol_frame, xk_reserve_protocol_meta, reserve_batch_protocol, xk_register_room_message_reservations, xk_register_sparse_room_message_reservations, xk_fill_reserved_preencoded_room_message_events, xk_fill_reserved_native_room_message_events, xk_retry_reserved_native_room_message_event_entry, owner_protocol_mode_enabled, native_room_message_batch_result_valid, xk_fill_reserved_native_room_message_event_entries, xk_reserve_protocol_payload, xk_fill_reserved_protocol, xk_release_reserved_protocol, xk_release_reserved_protocol_id, owner_protocol_output_enabled, fill_owner_protocol_output_fallback, submit_owner_protocol_output, _send_frame, _send_raw_sync, _send_raw, xk_send, xk_send_message, xk_send_message_ex, xk_send_text_packet, xk_send_system, xk_send_error, xk_send_command_error, xk_send_message_event, xk_send_dialog, xk_send_pong, xk_fill_reserved_preencoded_artifact, xk_send_preencoded_artifact, xk_send_preencoded_chat_batch, xk_send_batch, xk_send_report, xk_send_combat, xk_send_heartbeat, xk_broadcast, xk_broadcast_message, xk_broadcast_system, xk_broadcast_room, xk_send_capa_response}。
 *
 * 本模块是 delivery 域协议片段，由 `xk_protocol_core.c` 继承；
 * 只有本簇声明的 nosave 状态归属本簇，跨簇调用走 public 原型。
 */

#pragma strict_types
#pragma save_types

#include <system/globals.h>
#include <system/constants.h>
#include <system/errors.h>
#include <system/services.h>
#include <system/xk_protocol.h>
#include <framework/message_facade.h>

#define OWNER_PROTOCOL_OUTPUT_TIMEOUT_MS 2000
#define XK_PROTOCOL_TAIL_25MS_US 25000
/* 跨簇调用原型 */
private mapping apply_command_response_context(mapping payload,
    mixed response_context);
mapping attach_protocol_meta(object ob, string type, mapping raw_payload);
mapping build_map_grid_dynamic_payload(object ob);
mapping build_preencoded_dynamic_payload(object ob, string type,
    mapping artifact, mapping response_context);
string combine_preencoded_object_members(string stable_json, string dynamic_json);
mapping decode_preencoded_stable_payload(mapping artifact);
string encode_protocol_json(mixed value);
int preencoded_artifact_matches(string type, mapping dynamic_payload, mapping artifact);
int query_target_stream_epoch(object ob, string stream);
void record_preencoded_protocol_result(string type, mapping artifact, string outcome, int started_at);
int reserve_contiguous_server_seq_block(int count);
mapping xk_build_dialog_payload(object ob, string title, string content, mapping extra);
/* 本模块前向原型 */
int xk_release_reserved_protocol(object ob, mapping reservation);
int xk_release_reserved_protocol_id(object ob, int reservation_id);
int xk_send_message_ex(object ob, string text, string channel, mapping extra);
int xk_send_pong(object target, mixed seq);

private void record_xk_protocol_tail_probe(string prefix, int elapsed_ns)
{
    string tail_key;
    int elapsed_us;
    mixed err;

    if (!stringp(prefix) || prefix == "" || !intp(elapsed_ns) || elapsed_ns < 0)
        return;

    tail_key = prefix + "_tail_25ms_count";
    elapsed_us = elapsed_ns / 1000;
    if (elapsed_us < XK_PROTOCOL_TAIL_25MS_US)
        return;

    err = catch(GATEWAY_SESSION_D->record_xk_protocol_tail_event(tail_key));
    if (err)
        debug_message(sprintf("[xk_protocol_core] tail probe failed key=%s err=%O", tail_key, err));
}
private void record_xk_protocol_encode_tail_cpu_sample(int elapsed_ns, int cpu_elapsed_ns)
{
    int elapsed_us;
    mixed err;

    if (!intp(elapsed_ns) || elapsed_ns < 0)
        return;

    elapsed_us = elapsed_ns / 1000;
    if (elapsed_us < XK_PROTOCOL_TAIL_25MS_US)
        return;

    err = catch(GATEWAY_SESSION_D->record_xk_protocol_encode_tail_cpu_sample(cpu_elapsed_ns));
    if (err)
        debug_message(sprintf("[xk_protocol_core] encode CPU tail probe failed err=%O", err));
}
private void record_xk_protocol_phase_timings(mapping timings_ns)
{
    mixed err;

    err = catch(GATEWAY_SESSION_D->record_xk_protocol_phase_timings(timings_ns));
    if (err)
        debug_message(sprintf("[xk_protocol_core] phase timing failed err=%O", err));
}
private void record_owner_protocol_output(string outcome)
{
    mixed err;

    if (!stringp(outcome) || outcome == "")
        return;
    err = catch(GATEWAY_SESSION_D->record_owner_protocol_output(outcome));
    if (err)
        debug_message(sprintf("[xk_protocol_core] owner protocol output trace failed outcome=%s err=%O",
            outcome, err));
}
private int should_probe_xk_protocol_target(object ob)
{
    string gateway_session_id;
    mixed suppressed;

    if (!objectp(ob))
        return 0;
    catch(suppressed = ob->query_temp("protocol/xk_probe_suppressed"));
    if (suppressed)
        return 0;
    if (interactive(ob))
        return 1;

    gateway_session_id = GATEWAY_SESSION_D->query_object_session_id(ob);
    return stringp(gateway_session_id) && gateway_session_id != "";
}
void msg_send(object ob, string msg)
{
    call_other("/kernel/simul_efun", "msg_send_raw", ob, msg);
}
private mapping _create_message(string type, mapping payload)
{
    mapping msg;

    msg = ([
        "type": type,
        "payload": mapp(payload) ? payload : ([]),
    ]);

    return msg;
}
private int record_session_output_fifo(object ob, string type, mapping payload,
    mapping timings_ns)
{
    mixed err;
    mixed result;
    int cpu_started_at;
    int cpu_finished_at;
    string owner_id;

    if (!objectp(ob) || !stringp(type) || !mapp(payload))
        return 0;

    owner_id = "owner/protocol/" + file_name(ob);
    if (mapp(timings_ns))
        cpu_started_at = thread_cpu_time_ns();
    err = catch(result = OWNER_SESSION_FIFO_D->validate_owner_output_boundary(
        ob, owner_id, type, payload["meta"]));
    if (mapp(timings_ns)) {
        cpu_finished_at = thread_cpu_time_ns();
        timings_ns["session_trace_cpu"] = cpu_started_at >= 0
            && cpu_finished_at >= cpu_started_at
                ? cpu_finished_at - cpu_started_at : -1;
    }
    if (err)
        debug_message(sprintf(
            "[xk_protocol_core] session fifo boundary validation failed type=%s "
            + "ob=%s err=%O",
            type, file_name(ob) || "(null)", err));
    return !err && mapp(result) && result["ok"] == 1;
}
private int record_session_output_reservation(object ob, string type, mapping payload,
    int reservation_id)
{
    mixed err;
    mixed result;

    err = catch(result = GATEWAY_SESSION_D->record_session_output_reservation(
        ob, type, payload["meta"], reservation_id));
    if (err || result != 1) {
        debug_message(sprintf("[xk_protocol_core] protocol reservation trace failed type=%s ob=%s err=%O",
            type, file_name(ob) || "(null)", err));
        return 0;
    }
    return 1;
}
private int record_session_output_completion(object ob, string type, mapping payload,
    int reservation_id)
{
    mixed err;
    mixed result;

    err = catch(result = GATEWAY_SESSION_D->record_session_output_completion(
        ob, type, payload, reservation_id));
    if (err || result != 1) {
        debug_message(sprintf("[xk_protocol_core] protocol completion trace failed type=%s ob=%s err=%O",
            type, file_name(ob) || "(null)", err));
        /* 已填充但登记失败：进入待对账计数，不当作完整发送成功。 */
        GATEWAY_SESSION_D->record_owner_protocol_output("completion_failed");
        return 0;
    }
    return 1;
}
private int record_session_output_release(object ob, int reservation_id)
{
    mixed err;
    mixed result;

    err = catch(result = GATEWAY_SESSION_D->record_session_output_release(ob, reservation_id));
    if (err || result != 1) {
        debug_message(sprintf("[xk_protocol_core] protocol reservation release trace failed ob=%s err=%O",
            file_name(ob) || "(null)", err));
        return 0;
    }
    return 1;
}
/* durable 序号缺失时的统一诊断：不得编码或发送零序号帧。 */
private void record_unassigned_protocol_sequence(object ob, string type)
{
    debug_message(sprintf("[xk_protocol_core] sequence_unassigned type=%s ob=%s",
        stringp(type) ? type : "(null)",
        objectp(ob) ? file_name(ob) : "(null)"));
    GATEWAY_SESSION_D->record_owner_protocol_output("reserve_failed");
}
private string encode_reserved_protocol_frame(string type, mapping payload)
{
    string payload_json;

    if (!stringp(type) || strlen(type) != 4 || !mapp(payload))
        return "";

    payload_json = encode_protocol_json(payload);
    if (!stringp(payload_json))
        return "";
    return XK_HEADER + type + payload_json + "\e\n";
}
mapping xk_reserve_protocol_meta(object ob, string type, mapping payload)
{
    mapping reserved_payload;
    int reservation_id;

    if (!objectp(ob) || !stringp(type) || strlen(type) != 4)
        return ([ "ok": 0, "reason": "target and four-character type are required" ]);
    if (type == XK_BACH)
        return ([ "ok": 0, "reason": "batch protocol output requires batch reservation" ]);

    reservation_id = gateway_session_reserve(ob);
    if (reservation_id <= 0)
        return ([ "ok": 0, "reason": "gateway output reservation failed" ]);

    payload = mapp(payload) ? copy(payload) : ([]);
    reserved_payload = attach_protocol_meta(ob, type, payload);
    if (!mapp(reserved_payload) || !mapp(reserved_payload["meta"])) {
        gateway_session_release(ob, reservation_id);
        return ([ "ok": 0, "reason": "protocol metadata reservation failed" ]);
    }

    if (!record_session_output_reservation(ob, type, reserved_payload, reservation_id)) {
        gateway_session_release(ob, reservation_id);
        return ([ "ok": 0, "reason": "protocol reservation trace failed" ]);
    }
    return ([
        "ok": 1,
        "reservation_id": reservation_id,
        "target_path": file_name(ob),
        "target_owner_epoch": vm_owner_epoch(ob),
        "type": type,
        "meta": copy(reserved_payload["meta"]),
        "payload": copy(reserved_payload),
        "single_frame": 1,
        "raw_gateway_fifo_reserved": 1,
        "reserved_at": time(),
    ]);
}
private mapping reserve_batch_protocol(object ob)
{
    mapping reserved_payload;
    mixed reservation_error;
    int reservation_id;

    if (!objectp(ob))
        return ([ "ok": 0, "reason": "target is required" ]);

    reservation_id = gateway_session_reserve(ob);
    if (reservation_id <= 0)
        return ([ "ok": 0, "reason": "gateway output reservation failed" ]);

    reservation_error = catch(reserved_payload = attach_protocol_meta(
        ob, XK_BACH, ([ "messages": ({ }) ])));
    if (reservation_error || !mapp(reserved_payload)
        || !mapp(reserved_payload["meta"])) {
        gateway_session_release(ob, reservation_id);
        return ([ "ok": 0, "reason": "batch protocol metadata reservation failed" ]);
    }
    if (!record_session_output_reservation(ob, XK_BACH, reserved_payload,
        reservation_id)) {
        gateway_session_release(ob, reservation_id);
        return ([ "ok": 0, "reason": "batch protocol reservation trace failed" ]);
    }
    return ([
        "ok": 1,
        "reservation_id": reservation_id,
        "target_path": file_name(ob),
        "target_owner_epoch": vm_owner_epoch(ob),
        "type": XK_BACH,
        "payload": copy(reserved_payload),
        "batch_frame": 1,
        "raw_gateway_fifo_reserved": 1,
        "reserved_at": time(),
    ]);
}
int xk_register_room_message_reservations(object *targets,
    int *reservation_ids, int *reused, int first_server_seq)
{
    int count;

    if (!arrayp(targets) || sizeof(targets) == 0
        || !arrayp(reservation_ids) || !arrayp(reused)
        || first_server_seq <= 0)
        return 0;
    count = sizeof(targets);
    if (sizeof(reservation_ids) != count || sizeof(reused) != count
        || count > (MAX_INT - first_server_seq + 1) / 2)
        return 0;
    return GATEWAY_SESSION_D->record_session_output_reservation_batch(
        targets, reservation_ids, first_server_seq, reused);
}
int xk_register_sparse_room_message_reservations(object *targets,
    int *reservation_ids, int *slot_server_seqs)
{
    if (!arrayp(targets) || sizeof(targets) == 0
        || !arrayp(reservation_ids) || !arrayp(slot_server_seqs)
        || sizeof(reservation_ids) != sizeof(targets)
        || sizeof(slot_server_seqs) != sizeof(targets))
        return 0;
    return GATEWAY_SESSION_D->record_session_output_sparse_reservation_batch(
        targets, reservation_ids, slot_server_seqs);
}
mapping xk_fill_reserved_preencoded_room_message_events(object ob,
    string *stable_children_json, string *scope_types, string scope_id,
    int *message_seqs, int *server_seqs, int *message_epochs,
    int *sent_ats, mapping reservation)
{
    mixed completion_error;
    mixed fill_error;
    string packet_type;
    int count;
    int completion_result;
    int fill_result;
    int index;
    int released;
    int reservation_id;
    int slot_epoch;

    if (!objectp(ob) || !arrayp(stable_children_json)
        || !arrayp(scope_types) || !arrayp(message_seqs)
        || !arrayp(server_seqs) || !arrayp(message_epochs)
        || !arrayp(sent_ats) || !stringp(scope_id) || scope_id == ""
        || !mapp(reservation) || reservation["ok"] != 1
        || !intp(reservation["reservation_id"])
        || reservation["reservation_id"] <= 0
        || !intp(reservation["slot_server_seq"])
        || reservation["slot_server_seq"] <= 0
        || !intp(reservation["slot_sent_at"])
        || reservation["slot_sent_at"] <= 0
        || !stringp(reservation["target_path"])
        || !intp(reservation["target_owner_epoch"]))
        return ([ "ok": 0, "terminal": 0, "released": 0,
            "outcome": "invalid_entry" ]);
    if (reservation["target_path"] != file_name(ob)
        || reservation["target_owner_epoch"] != vm_owner_epoch(ob))
        return ([ "ok": 0, "terminal": 0, "released": 0,
            "outcome": "target_identity_changed" ]);
    count = sizeof(stable_children_json);
    if (count <= 0 || sizeof(scope_types) != count
        || sizeof(message_seqs) != count || sizeof(server_seqs) != count
        || sizeof(message_epochs) != count || sizeof(sent_ats) != count)
        return ([ "ok": 0, "terminal": 0, "released": 0,
            "outcome": "invalid_entry" ]);
    for (index = 0; index < count; index++) {
        if (!stringp(stable_children_json[index])
            || stable_children_json[index] == ""
            || !stringp(scope_types[index]) || scope_types[index] == ""
            || !intp(message_seqs[index]) || message_seqs[index] <= 0
            || !intp(server_seqs[index]) || server_seqs[index] <= 0
            || !intp(message_epochs[index]) || message_epochs[index] < 0
            || !intp(sent_ats[index]) || sent_ats[index] <= 0)
            return ([ "ok": 0, "terminal": 0, "released": 0,
                "outcome": "invalid_entry" ]);
    }

    reservation_id = reservation["reservation_id"];
    packet_type = count == 1 ? XK_MSGE : XK_BACH;

    slot_epoch = query_target_stream_epoch(ob, "system");
    fill_error = catch(fill_result =
        gateway_session_fill_preencoded_message_event_batch(ob,
            reservation_id, stable_children_json, scope_types, scope_id,
            message_seqs, server_seqs, message_epochs, sent_ats,
            reservation["slot_server_seq"], slot_epoch,
            reservation["slot_sent_at"]));
    if (fill_error || !fill_result) {
        released = xk_release_reserved_protocol(ob, reservation);
        if (released)
            reservation["terminal"] = 1;
        return ([ "ok": 0, "terminal": released ? 1 : 0,
            "released": released ? 1 : 0,
            "outcome": "native_fill_failed" ]);
    }
    completion_error = catch(completion_result =
        GATEWAY_SESSION_D->complete_session_output_reservation(
            ob, reservation_id, packet_type,
            packet_type == XK_BACH ? "system" : "message",
            reservation["slot_server_seq"]));
    reservation["terminal"] = 1;
    if (completion_error || !completion_result)
        return ([ "ok": 1, "terminal": 1, "released": 0,
            "outcome": "completion_failed" ]);
    return ([ "ok": 1, "terminal": 1, "released": 0,
        "outcome": "filled" ]);
}
mapping xk_fill_reserved_native_room_message_events(object ob,
    string scope_id, int event_count, mapping reservation)
{
    mixed completion_error;
    mixed fill_error;
    string packet_type;
    int completion_result;
    int fill_result;
    int released;
    int reservation_id;
    int slot_epoch;

    if (!objectp(ob) || !stringp(scope_id) || scope_id == ""
        || event_count <= 0 || !mapp(reservation)
        || reservation["ok"] != 1
        || !intp(reservation["reservation_id"])
        || reservation["reservation_id"] <= 0
        || !intp(reservation["slot_server_seq"])
        || reservation["slot_server_seq"] <= 0
        || !intp(reservation["slot_sent_at"])
        || reservation["slot_sent_at"] <= 0
        || !stringp(reservation["target_path"])
        || !intp(reservation["target_owner_epoch"]))
        return ([ "ok": 0, "terminal": 0, "released": 0,
            "outcome": "invalid_entry" ]);
    if (reservation["target_path"] != file_name(ob)
        || reservation["target_owner_epoch"] != vm_owner_epoch(ob))
        return ([ "ok": 0, "terminal": 0, "released": 0,
            "outcome": "target_identity_changed" ]);

    reservation_id = reservation["reservation_id"];
    packet_type = event_count == 1 ? XK_MSGE : XK_BACH;
    slot_epoch = query_target_stream_epoch(ob, "system");
    fill_error = catch(fill_result =
        gateway_session_fill_pending_message_event_batch(ob,
            reservation_id, scope_id, slot_epoch));
    if (fill_error || !fill_result) {
        released = xk_release_reserved_protocol(ob, reservation);
        if (released)
            reservation["terminal"] = 1;
        return ([ "ok": 0, "terminal": released ? 1 : 0,
            "released": released ? 1 : 0,
            "outcome": "native_fill_failed" ]);
    }
    completion_error = catch(completion_result =
        GATEWAY_SESSION_D->complete_session_output_reservation(
            ob, reservation_id, packet_type,
            packet_type == XK_BACH ? "system" : "message",
            reservation["slot_server_seq"]));
    reservation["terminal"] = 1;
    if (completion_error || !completion_result)
        return ([ "ok": 1, "terminal": 1, "released": 0,
            "outcome": "completion_failed" ]);
    return ([ "ok": 1, "terminal": 1, "released": 0,
        "outcome": "filled" ]);
}
private mapping xk_retry_reserved_native_room_message_event_entry(
    object target, mapping entry)
{
    mapping reservation;
    mapping status;
    mixed status_error;

    reservation = mapp(entry) ? entry["reservation"] : 0;
    if (!objectp(target) || !mapp(entry) || !mapp(reservation)
        || !intp(reservation["reservation_id"])
        || reservation["reservation_id"] <= 0
        || !stringp(entry["scope_id"]) || entry["scope_id"] == "")
        return ([ "ok": 0, "terminal": 0, "released": 0,
            "outcome": "invalid_entry" ]);
    status_error = catch(status =
        gateway_session_pending_message_event_batch_status(
            target, reservation["reservation_id"]));
    if (status_error || !mapp(status) || status["ok"] != 1
        || !intp(status["event_count"]) || status["event_count"] <= 0
        || !intp(status["text_length_total"])
        || status["text_length_total"] <= 0
        || !intp(status["created_at_ns"]) || status["created_at_ns"] <= 0
        || !intp(status["last_append_at_ns"])
        || status["last_append_at_ns"] < status["created_at_ns"])
        return ([ "ok": 0, "terminal": 0, "released": 0,
            "outcome": "invalid_entry" ]);
    entry["event_count"] = status["event_count"];
    entry["text_length_total"] = status["text_length_total"];
    entry["created_at_ns"] = status["created_at_ns"];
    entry["last_append_at_ns"] = status["last_append_at_ns"];
    return xk_fill_reserved_native_room_message_events(target,
        entry["scope_id"], entry["event_count"], reservation);
}
private int owner_protocol_mode_enabled(mixed mode)
{
    return mode == "owner_thread_compact" ||
        mode == "owner_thread_full";
}
private int native_room_message_batch_result_valid(mapping native_result,
    int count, int owner_mode)
{
    int index;

    if (!mapp(native_result) || native_result["ok"] != 1 || count <= 0
        || !arrayp(native_result["event_counts"])
        || sizeof(native_result["event_counts"]) != count
        || !arrayp(native_result["text_length_totals"])
        || sizeof(native_result["text_length_totals"]) != count
        || !arrayp(native_result["slot_server_seqs"])
        || sizeof(native_result["slot_server_seqs"]) != count)
        return 0;
    if (owner_mode) {
        if (!arrayp(native_result["submitted"])
            || sizeof(native_result["submitted"]) != count
            || !arrayp(native_result["filled_inline"])
            || sizeof(native_result["filled_inline"]) != count)
            return 0;
    } else if (!arrayp(native_result["filled"])
        || sizeof(native_result["filled"]) != count)
        return 0;

    for (index = 0; index < count; index++) {
        if (!intp(native_result["event_counts"][index])
            || native_result["event_counts"][index] <= 0
            || !intp(native_result["text_length_totals"][index])
            || native_result["text_length_totals"][index] <= 0
            || !intp(native_result["slot_server_seqs"][index])
            || native_result["slot_server_seqs"][index] <= 0)
            return 0;
        if (owner_mode) {
            if (!intp(native_result["submitted"][index])
                || (native_result["submitted"][index] != 0
                    && native_result["submitted"][index] != 1)
                || !intp(native_result["filled_inline"][index])
                || (native_result["filled_inline"][index] != 0
                    && native_result["filled_inline"][index] != 1)
                || (native_result["submitted"][index]
                    && native_result["filled_inline"][index]))
                return 0;
        } else if (!intp(native_result["filled"][index])
            || (native_result["filled"][index] != 0
                && native_result["filled"][index] != 1))
            return 0;
    }
    return 1;
}
mapping xk_fill_reserved_native_room_message_event_entries(object *targets,
    mapping *entries)
{
    mapping native_result;
    mapping reservation;
    mapping retry_result;
    mixed completion_error;
    mixed native_error;
    string *outcomes;
    string mode;
    string *scope_ids;
    int *completed;
    int *event_counts;
    int *filled;
    int *filled_inline;
    int *released;
    int *reservation_ids;
    int *slot_epochs;
    int *slot_server_seqs;
    int *submitted;
    int *text_length_totals;
    int batch_input_valid;
    int count;
    int index;
    int owner_mode;
    mixed mode_error;

    if (!arrayp(targets) || !arrayp(entries) || sizeof(targets) == 0
        || sizeof(entries) != sizeof(targets))
        return ([ "ok": 0, "outcomes": ({ }), "event_counts": ({ }),
            "text_length_totals": ({ }), "released": ({ }) ]);
    count = sizeof(targets);
    batch_input_valid = 1;
    reservation_ids = allocate(count);
    scope_ids = allocate(count);
    slot_epochs = allocate(count);
    for (index = 0; index < count; index++) {
        reservation = mapp(entries[index]) ? entries[index]["reservation"] : 0;
        if (!objectp(targets[index]) || !mapp(entries[index])
            || !mapp(reservation) || reservation["ok"] != 1
            || !intp(reservation["reservation_id"])
            || reservation["reservation_id"] <= 0
            || !intp(reservation["slot_server_seq"])
            || reservation["slot_server_seq"] <= 0
            || !intp(reservation["slot_sent_at"])
            || reservation["slot_sent_at"] <= 0
            || !stringp(reservation["target_path"])
            || !intp(reservation["target_owner_epoch"])
            || !intp(entries[index]["event_count"])
            || entries[index]["event_count"] <= 0
            || !stringp(entries[index]["scope_id"])
            || entries[index]["scope_id"] == ""
            || reservation["target_path"] != file_name(targets[index])
            || reservation["target_owner_epoch"] != vm_owner_epoch(targets[index])) {
            batch_input_valid = 0;
            continue;
        }
        reservation_ids[index] = reservation["reservation_id"];
        scope_ids[index] = entries[index]["scope_id"];
        slot_epochs[index] = query_target_stream_epoch(targets[index], "system");
    }

    if (batch_input_valid) {
        mode_error = catch(mode = MULTICORE_CONFIG_D->query_mode());
        owner_mode = !mode_error && owner_protocol_mode_enabled(mode);
        if (owner_mode) {
            native_error = catch(native_result =
                gateway_sessions_submit_pending_message_event_batches(targets,
                    reservation_ids, scope_ids, slot_epochs,
                    OWNER_PROTOCOL_OUTPUT_TIMEOUT_MS));
        } else {
            native_error = catch(native_result =
                gateway_sessions_fill_pending_message_event_batches(targets,
                    reservation_ids, scope_ids, slot_epochs));
        }
    } else {
        native_error = 1;
    }
    outcomes = allocate(count);
    event_counts = allocate(count);
    text_length_totals = allocate(count);
    released = allocate(count);
    slot_server_seqs = allocate(count);
    filled = allocate(count);
    filled_inline = allocate(count);
    submitted = allocate(count);
    if (!native_error && mapp(native_result)
        && native_room_message_batch_result_valid(
            native_result, count, owner_mode)) {
        if (owner_mode) {
            submitted = native_result["submitted"];
            filled_inline = native_result["filled_inline"];
            filled = filled_inline;
        } else {
            filled = native_result["filled"];
        }
    } else {
        native_result = 0;
    }
    if (mapp(native_result)) {
        event_counts = native_result["event_counts"];
        text_length_totals = native_result["text_length_totals"];
        slot_server_seqs = native_result["slot_server_seqs"];
        completion_error = catch(completed =
            GATEWAY_SESSION_D->complete_session_output_reservation_batch(
                targets, reservation_ids, event_counts, slot_server_seqs,
                filled));
        if (completion_error || !arrayp(completed)
            || sizeof(completed) != count)
            completed = allocate(count);
    } else {
        completed = allocate(count);
    }

    for (index = 0; index < count; index++) {
        reservation = mapp(entries[index]) ? entries[index]["reservation"] : 0;
        if (submitted[index]) {
            outcomes[index] = "submitted";
            record_owner_protocol_output("submitted");
            continue;
        }
        if (filled[index]) {
            outcomes[index] = completed[index]
                ? "filled" : "completion_failed";
            reservation["terminal"] = 1;
            continue;
        }
        retry_result = xk_retry_reserved_native_room_message_event_entry(
            targets[index], entries[index]);
        if (!mapp(retry_result))
            retry_result = ([ "ok": 0, "terminal": 0, "released": 0,
                "outcome": "native_fill_failed" ]);
        outcomes[index] = stringp(retry_result["outcome"])
            ? retry_result["outcome"] : "native_fill_failed";
        released[index] = retry_result["released"] ? 1 : 0;
        if (mapp(reservation) && retry_result["terminal"])
            reservation["terminal"] = 1;
        if (!event_counts[index])
            event_counts[index] = mapp(entries[index])
                && intp(entries[index]["event_count"])
                    ? entries[index]["event_count"] : 0;
        if (!text_length_totals[index])
            text_length_totals[index] = mapp(entries[index])
                && intp(entries[index]["text_length_total"])
                    ? entries[index]["text_length_total"] : 0;
    }
    return ([
        "ok": 1,
        "outcomes": outcomes,
        "event_counts": event_counts,
        "text_length_totals": text_length_totals,
        "released": released,
        "submitted": submitted,
    ]);
}
mapping xk_reserve_protocol_payload(object ob, string type, mapping payload)
{
    if (!objectp(ob) || !stringp(type) || strlen(type) != 4)
        return ([ "ok": 0, "reason": "target and four-character type are required" ]);

    payload = mapp(payload) ? copy(payload) : ([]);
    if (type == XK_MAPS)
        payload += build_map_grid_dynamic_payload(ob);
    if (type == XK_DLOG) {
        payload = xk_build_dialog_payload(ob,
            payload["title"] || "",
            payload["content"] || "",
            payload);
    }
    return xk_reserve_protocol_meta(ob, type, payload);
}
int xk_fill_reserved_protocol(object ob, string type, mapping raw_payload, mapping reservation)
{
    mapping payload;
    string frame;
    int reservation_id;

    if (!objectp(ob) || !stringp(type) || strlen(type) != 4 || !mapp(reservation)
        || reservation["ok"] != 1 || reservation["single_frame"] != 1
        || !intp(reservation["reservation_id"]) || reservation["reservation_id"] <= 0
        || !stringp(reservation["target_path"])
        || reservation["target_path"] != file_name(ob)
        || !stringp(reservation["type"]) || reservation["type"] != type
        || !mapp(reservation["meta"]))
        return 0;
    /* reservation 一次性：已 native fill 的槽位不得重复填充（登记失败待对账时
     * 调用方回退也不能重发同一 payload）。 */
    if (reservation["filled"] == 1)
        return 0;

    reservation_id = reservation["reservation_id"];
    if (intp(reservation["target_owner_epoch"])
        && reservation["target_owner_epoch"] != vm_owner_epoch(ob)) {
        xk_release_reserved_protocol(ob, reservation);
        return 0;
    }

    payload = mapp(reservation["payload"])
        ? copy(reservation["payload"]) : ([]);
    if (mapp(raw_payload))
        payload += copy(raw_payload);
    payload["meta"] = copy(reservation["meta"]);
    frame = encode_reserved_protocol_frame(type, payload);
    if (!stringp(frame) || frame == "") {
        xk_release_reserved_protocol(ob, reservation);
        return 0;
    }
    if (!gateway_session_fill(ob, reservation_id, frame)) {
        xk_release_reserved_protocol(ob, reservation);
        return 0;
    }
    reservation["filled"] = 1;
    if (!record_session_output_completion(ob, type, payload, reservation_id)) {
        reservation["fill_state"] = "filled_pending_reconcile";
        /* L-36：帧已进入原 FIFO，只有 ledger 对账未完成。返回 -1 区别于
         * “未填充失败”（0）：调用方不得据此重发同一业务包。 */
        return -1;
    }
    reservation["fill_state"] = "completed";
    return 1;
}
int xk_release_reserved_protocol(object ob, mapping reservation)
{
    if (!objectp(ob) || !mapp(reservation) || reservation["ok"] != 1
        || !intp(reservation["reservation_id"]) || reservation["reservation_id"] <= 0
        || !stringp(reservation["target_path"])
        || reservation["target_path"] != file_name(ob))
        return 0;

    return xk_release_reserved_protocol_id(ob, reservation["reservation_id"]);
}
int xk_release_reserved_protocol_id(object ob, int reservation_id)
{
    mixed err;
    mixed registered;

    if (!objectp(ob) || !intp(reservation_id) || reservation_id <= 0)
        return 0;

    if (!gateway_session_release(ob, reservation_id)) {
        GATEWAY_SESSION_D->record_owner_protocol_output("release_failed");
        return 0;
    }
    /* 物理释放与登记分别报告：物理已释放而登记失败只补对账，不重发消息。 */
    err = catch(registered = record_session_output_release(ob, reservation_id));
    if (err || registered != 1) {
        GATEWAY_SESSION_D->record_owner_protocol_output("release_failed");
        return 0;
    }
    return 1;
}
private int owner_protocol_output_enabled(object ob, string type)
{
    mixed explicit_opt_in;
    mixed err;
    mixed mode;

    if (!objectp(ob) || !stringp(type) || strlen(type) != 4
        || type == XK_BACH || type == XK_MAPS
        || !function_exists("owner_encode_protocol_frame", ob))
        return 0;
    err = catch(explicit_opt_in =
        ob->query_temp("owner/generic_protocol_output_opt_in"));
    if (err || !explicit_opt_in)
        return 0;
    err = catch(mode = MULTICORE_CONFIG_D->query_mode());
    return !err && owner_protocol_mode_enabled(mode);
}
private int fill_owner_protocol_output_fallback(object ob, string type,
    mapping reservation)
{
    record_owner_protocol_output("sync_fallback");
    return xk_fill_reserved_protocol(ob, type, 0, reservation);
}
private int submit_owner_protocol_output(object ob, string type, mapping raw_payload)
{
    mapping reservation;
    mapping submitted;
    int future_id;
    int reservation_id;

    if (!owner_protocol_output_enabled(ob, type))
        return 0;

    reservation = xk_reserve_protocol_meta(ob, type, raw_payload);
    if (!mapp(reservation) || reservation["ok"] != 1) {
        record_owner_protocol_output("reserve_failed");
        return 0;
    }
    reservation_id = reservation["reservation_id"];
    submitted = owner_call_async(ob, "owner_encode_protocol_frame", ([
        "type": type,
        "payload": copy(reservation["payload"]),
    ]));
    future_id = mapp(submitted) && intp(submitted["future_id"])
        ? submitted["future_id"] : 0;
    if (!mapp(submitted) || submitted["success"] != 1 || future_id <= 0) {
        if (future_id > 0) {
            owner_future_cancel(future_id, "owner protocol output submit failed");
            owner_future_take(future_id);
        }
        record_owner_protocol_output("submit_failed");
        return fill_owner_protocol_output_fallback(ob, type, reservation);
    }

    if (!gateway_session_watch_future_output(ob, reservation_id, future_id,
        OWNER_PROTOCOL_OUTPUT_TIMEOUT_MS)) {
        owner_future_cancel(future_id, "owner protocol output watch failed");
        owner_future_take(future_id);
        record_owner_protocol_output("watch_failed");
        return fill_owner_protocol_output_fallback(ob, type, reservation);
    }
    record_owner_protocol_output("submitted");
    return 1;
}
private int _send_frame(object ob, string type, string payload_json,
    mapping payload, mapping timings_ns, int session_fifo_recorded)
{
    mapping meta;
    mapping result;
    mixed err;
    string frame;
    string owner_id;

    if (!objectp(ob) || !stringp(type) || strlen(type) != 4)
        return 0;

    if (!stringp(payload_json))
        payload_json = "{}";

    frame = XK_HEADER + type + payload_json + "\e\n";
    owner_id = "owner/protocol/" + file_name(ob);
    meta = mapp(payload) && mapp(payload["meta"])
        ? copy(payload["meta"]) : ([ ]);
    meta["payload_contract"] = "encoded_protocol_frame";
    meta["protocol_frame_type"] = type;
    meta["session_fifo_boundary"] = 1;
    meta["direct_socket_write"] = 0;
    meta["boundary_prevalidated"] = session_fifo_recorded ? 1 : 0;
    /* 缺少正数 durable 序号：拒绝生成 frame，也不进入 FIFO。 */
    if (!intp(meta["server_seq"]) || meta["server_seq"] <= 0) {
        record_unassigned_protocol_sequence(ob, type);
        return 0;
    }

    /* All synchronous protocol frames use the same owner/session FIFO as raw
     * text.  The FIFO owns native reservation, fill, completion and release;
     * this function never writes to the target directly. */
    err = catch(result = OWNER_SESSION_FIFO_D->enqueue_owner_output_for_target(
        ob, owner_id, type, meta, frame));
    if (err || !mapp(result) || result["ok"] != 1) {
        debug_message(sprintf(
            "[xk_protocol_core] owner fifo dispatch failed type=%s ob=%s stage=%s state=%s err=%O",
            type, file_name(ob) || "(null)",
            mapp(result) && stringp(result["stage"]) ? result["stage"] : "",
            mapp(result) && stringp(result["reservation_state"])
                ? result["reservation_state"] : "",
            err));
        /* L-36：已填充待对账不是“未发送”。返回 -1 让调用方知道消息已在
         * FIFO 中（不得重发），只有未填充分支才可安全回退（返回 0）。 */
        if (mapp(result)
            && stringp(result["reservation_state"])
            && result["reservation_state"] == "filled_pending_reconcile")
            return -1;
        return 0;
    }
    return 1;
}
private int _send_raw_sync(object ob, mapping msg)
{
    string type;
    mapping attached_payload;
    string payload_json;
    mapping error_payload;
    mapping frame_timings_ns;
    string error_payload_json;
    int encode_started_at;
    int metadata_started_at;
    int json_started_at;
    int encode_finished_at;
    int encode_cpu_started_at;
    int encode_cpu_finished_at;
    int encode_cpu_elapsed_ns;
    int metadata_elapsed_ns;
    int json_elapsed_ns;
    int frame_started_at;
    int frame_elapsed_ns;
    int frame_cpu_started_at;
    int frame_cpu_finished_at;
    int frame_cpu_elapsed_ns;
    int probe;
    int result;
    int session_fifo_recorded;

    if (!objectp(ob) || !mapp(msg))
        return 0;

    type = msg["type"];
    if (!stringp(type) || strlen(type) != 4)
        return 0;

    probe = should_probe_xk_protocol_target(ob);
    frame_timings_ns = probe ? ([ ]) : 0;
    if (probe) {
        encode_started_at = perf_counter_ns();
        metadata_started_at = encode_started_at;
        encode_cpu_started_at = thread_cpu_time_ns();
    }

    /*
     * 协议核心只负责把 payload 编码后交给目标对象的消息通道。
     * 连接、socket 与实际发送对象所有权仍在 ob 一侧，这里不缓存发送状态。
    */
    attached_payload = attach_protocol_meta(ob, type, msg["payload"] || ([]));
    /* 元数据分配失败（含序号不可得）不得继续编码零序号帧。 */
    if (!mapp(attached_payload) || !mapp(attached_payload["meta"])
        || !intp(attached_payload["meta"]["server_seq"])
        || attached_payload["meta"]["server_seq"] <= 0) {
        record_unassigned_protocol_sequence(ob, type);
        return 0;
    }
    if (probe) {
        json_started_at = perf_counter_ns();
        metadata_elapsed_ns = json_started_at - metadata_started_at;
    }
    payload_json = encode_protocol_json(attached_payload);
    if (probe) {
        encode_cpu_finished_at = thread_cpu_time_ns();
        encode_finished_at = perf_counter_ns();
        json_elapsed_ns = encode_finished_at - json_started_at;
        encode_cpu_elapsed_ns = encode_cpu_started_at >= 0
            && encode_cpu_finished_at >= encode_cpu_started_at
                ? encode_cpu_finished_at - encode_cpu_started_at : -1;
        record_xk_protocol_tail_probe("xk_protocol_metadata", metadata_elapsed_ns);
        record_xk_protocol_tail_probe("xk_protocol_json", json_elapsed_ns);
        record_xk_protocol_tail_probe("xk_protocol_encode",
            encode_finished_at - encode_started_at);
        record_xk_protocol_encode_tail_cpu_sample(encode_finished_at - encode_started_at,
            encode_cpu_elapsed_ns);
    }

    /* 编码失败不能伪装成合法业务空包，否则客户端会静默吞掉字段损坏。 */
    if (!stringp(payload_json)) {
        debug_message(sprintf("[xk_protocol_core] json_encode failed for type=%s ob=%s",
            type, file_name(ob) || "(null)"));
        if (type == XK_ERRO)
            return 0;
        error_payload = attach_protocol_meta(ob, XK_ERRO, ([
            "message": "协议编码失败，请稍后再试。",
            "code": 500,
            "source_type": type,
            "reason": "json_encode_failed",
        ]));
        error_payload_json = encode_protocol_json(error_payload);
        if (!stringp(error_payload_json))
            return 0;
        if (probe) {
            frame_started_at = perf_counter_ns();
            frame_cpu_started_at = thread_cpu_time_ns();
        }
        session_fifo_recorded = record_session_output_fifo(ob, XK_ERRO,
            error_payload, frame_timings_ns);
        /* L-19：错误提示是给玩家的通知，不是原业务的成功。原包编码失败时
         * 必须让调用方看到失败（返回 0），否则错误提示发送成功会覆盖掉
         * 原操作的失败结论。 */
        result = _send_frame(ob, XK_ERRO, error_payload_json, error_payload,
            frame_timings_ns, session_fifo_recorded);
        if (probe) {
            frame_elapsed_ns = perf_counter_ns() - frame_started_at;
            frame_cpu_finished_at = thread_cpu_time_ns();
            frame_cpu_elapsed_ns = frame_cpu_started_at >= 0
                && frame_cpu_finished_at >= frame_cpu_started_at
                    ? frame_cpu_finished_at - frame_cpu_started_at : -1;
            record_xk_protocol_tail_probe("xk_protocol_frame", frame_elapsed_ns);
            record_xk_protocol_phase_timings(([
                "metadata": metadata_elapsed_ns,
                "json": json_elapsed_ns,
                "encode": encode_finished_at - encode_started_at,
                "encode_cpu": encode_cpu_elapsed_ns,
                "frame": frame_elapsed_ns,
                "frame_cpu": frame_cpu_elapsed_ns,
            ]) + frame_timings_ns);
        }
        return 0;
    }

    if (probe) {
        frame_timings_ns["protocol_type"] = type;
        frame_timings_ns["payload_bytes"] = strlen(payload_json);
        frame_started_at = perf_counter_ns();
        frame_cpu_started_at = thread_cpu_time_ns();
    }
    session_fifo_recorded = record_session_output_fifo(ob, type,
        attached_payload, frame_timings_ns);
    result = _send_frame(ob, type, payload_json, attached_payload,
        frame_timings_ns, session_fifo_recorded);
    if (probe) {
        frame_elapsed_ns = perf_counter_ns() - frame_started_at;
        frame_cpu_finished_at = thread_cpu_time_ns();
        frame_cpu_elapsed_ns = frame_cpu_started_at >= 0
            && frame_cpu_finished_at >= frame_cpu_started_at
                ? frame_cpu_finished_at - frame_cpu_started_at : -1;
        record_xk_protocol_tail_probe("xk_protocol_frame", frame_elapsed_ns);
        record_xk_protocol_phase_timings(([
            "metadata": metadata_elapsed_ns,
            "json": json_elapsed_ns,
            "encode": encode_finished_at - encode_started_at,
            "encode_cpu": encode_cpu_elapsed_ns,
            "frame": frame_elapsed_ns,
            "frame_cpu": frame_cpu_elapsed_ns,
        ]) + frame_timings_ns);
    }
    return result;
}
private int _send_raw(object ob, mapping msg)
{
    string type;
    int result;

    if (!objectp(ob) || !mapp(msg))
        return 0;
    type = msg["type"];
    if (!stringp(type) || strlen(type) != 4)
        return 0;

    result = submit_owner_protocol_output(ob, type, msg["payload"] || ([]));
    if (result)
        return result;
    return _send_raw_sync(ob, msg);
}
int xk_fill_reserved_preencoded_artifact(object ob, string type,
    mapping artifact, mapping reservation)
{
    mapping dynamic_payload;
    mapping stable_payload;
    string dynamic_json;
    string frame;
    string payload_json;
    int fill_result;
    int reservation_id;
    int started_at;

    if (!objectp(ob) || !mapp(reservation) || reservation["ok"] != 1
        || !intp(reservation["reservation_id"])
        || reservation["reservation_id"] <= 0
        || reservation["target_path"] != file_name(ob))
        return 0;
    started_at = perf_counter_ns();
    if (!stringp(type) || strlen(type) != 4 || !mapp(artifact)
        || reservation["single_frame"] != 1
        || reservation["type"] != type || !mapp(reservation["payload"])) {
        record_preencoded_protocol_result(type, artifact,
            "preencoded_failed", started_at);
        return 0;
    }
    /* reservation 一次性：已 native fill 的槽位不得重复填充。 */
    if (reservation["filled"] == 1)
        return 0;
    if (intp(reservation["target_owner_epoch"])
        && reservation["target_owner_epoch"] != vm_owner_epoch(ob)) {
        record_preencoded_protocol_result(type, artifact,
            "preencoded_failed", started_at);
        return 0;
    }

    dynamic_payload = copy(reservation["payload"]);
    if (!preencoded_artifact_matches(type, dynamic_payload, artifact)) {
        record_preencoded_protocol_result(type, artifact,
            "preencoded_failed", started_at);
        return 0;
    }
    dynamic_json = encode_protocol_json(dynamic_payload);
    payload_json = combine_preencoded_object_members(
        artifact["stable_json"], dynamic_json);
    if (!stringp(payload_json) || payload_json == "") {
        stable_payload = decode_preencoded_stable_payload(artifact);
        if (mapp(stable_payload)) {
            fill_result = xk_fill_reserved_protocol(ob, type, stable_payload,
                reservation);
            if (fill_result == 1) {
                record_preencoded_protocol_result(type, artifact,
                    "preencoded_sync_fallback", started_at);
                return 1;
            }
            if (fill_result == -1) {
                /* L-36：已填充待对账，不得让调用方回退重发。 */
                record_preencoded_protocol_result(type, artifact,
                    "preencoded_completion_pending", started_at);
                return -1;
            }
        }
        record_preencoded_protocol_result(type, artifact,
            "preencoded_failed", started_at);
        return 0;
    }

    reservation_id = reservation["reservation_id"];
    frame = XK_HEADER + type + payload_json + "\e\n";
    if (!gateway_session_fill(ob, reservation_id, frame)) {
        stable_payload = decode_preencoded_stable_payload(artifact);
        if (mapp(stable_payload)
            && xk_fill_reserved_protocol(ob, type, stable_payload, reservation)) {
            record_preencoded_protocol_result(type, artifact,
                "preencoded_sync_fallback", started_at);
            return 1;
        }
        record_preencoded_protocol_result(type, artifact,
            "preencoded_failed", started_at);
        return 0;
    }
    reservation["filled"] = 1;
    if (!record_session_output_completion(ob, type, reservation["payload"],
        reservation_id)) {
        /* L-36：已填充但登记失败——保留待对账终态，返回 -1 表示消息已在原
         * FIFO 中（不得重复填充，也不得回退重发）。 */
        reservation["fill_state"] = "filled_pending_reconcile";
        record_preencoded_protocol_result(type, artifact,
            "preencoded_completion_pending", started_at);
        return -1;
    }
    reservation["fill_state"] = "completed";
    record_preencoded_protocol_result(type, artifact, "preencoded_hit", started_at);
    return 1;
}
int xk_send_preencoded_artifact(object ob, string type,
    mapping artifact, mapping response_context)
{
    mapping dynamic_payload;
    mapping full_payload;
    mapping reservation;
    mapping stable_payload;
    string dynamic_json;
    string payload_json;
    string frame;
    int reservation_id;
    int result;
    int started_at;

    if (!objectp(ob) || !stringp(type) || strlen(type) != 4 || !mapp(artifact))
        return 0;
    started_at = perf_counter_ns();

    dynamic_payload = build_preencoded_dynamic_payload(ob, type, artifact,
        response_context);
    if (!preencoded_artifact_matches(type, dynamic_payload, artifact)) {
        record_preencoded_protocol_result(type, artifact,
            "preencoded_failed", started_at);
        return 0;
    }

    reservation = xk_reserve_protocol_meta(ob, type, dynamic_payload);
    if (!mapp(reservation) || reservation["ok"] != 1) {
        stable_payload = decode_preencoded_stable_payload(artifact);
        full_payload = mapp(stable_payload)
            ? copy(stable_payload) + copy(dynamic_payload) : 0;
        if (mapp(full_payload)) {
            result = _send_raw_sync(ob, _create_message(type, full_payload));
            if (result == -1) {
                /* L-36：已填充待对账，不得回退重发。 */
                record_preencoded_protocol_result(type, artifact,
                    "preencoded_completion_pending", started_at);
                return -1;
            }
            record_preencoded_protocol_result(type, artifact,
                result ? "preencoded_sync_fallback" : "preencoded_failed",
                started_at);
            return result;
        }
        record_preencoded_protocol_result(type, artifact,
            "preencoded_failed", started_at);
        return 0;
    }
    reservation_id = reservation["reservation_id"];
    dynamic_json = encode_protocol_json(reservation["payload"]);
    payload_json = combine_preencoded_object_members(
        artifact["stable_json"], dynamic_json);
    if (!stringp(payload_json) || payload_json == "") {
        stable_payload = decode_preencoded_stable_payload(artifact);
        if (mapp(stable_payload)) {
            result = xk_fill_reserved_protocol(ob, type, stable_payload, reservation);
            if (result == -1) {
                /* L-36：已填充待对账，不得回退重发。 */
                record_preencoded_protocol_result(type, artifact,
                    "preencoded_completion_pending", started_at);
                return -1;
            }
            record_preencoded_protocol_result(type, artifact,
                result ? "preencoded_sync_fallback" : "preencoded_failed",
                started_at);
            return result;
        }
        xk_release_reserved_protocol(ob, reservation);
        record_preencoded_protocol_result(type, artifact,
            "preencoded_failed", started_at);
        return 0;
    }

    frame = XK_HEADER + type + payload_json + "\e\n";
    if (!gateway_session_fill(ob, reservation_id, frame)) {
        xk_release_reserved_protocol(ob, reservation);
        record_preencoded_protocol_result(type, artifact,
            "preencoded_failed", started_at);
        return 0;
    }
    reservation["filled"] = 1;
    if (!record_session_output_completion(ob, type, reservation["payload"],
        reservation_id)) {
        /* L-36：消息已在原 FIFO（filled），只有登记未完成；返回 -1。 */
        reservation["fill_state"] = "filled_pending_reconcile";
        record_preencoded_protocol_result(type, artifact,
            "preencoded_completion_pending", started_at);
        return -1;
    }
    reservation["fill_state"] = "completed";
    record_preencoded_protocol_result(type, artifact, "preencoded_hit", started_at);
    return 1;
}
varargs int xk_send(object ob, string type, mapping payload,
    mapping response_context)
{
    mapping msg;
    int build_started_at;
    int send_started_at;
    int probe;
    int result;

    if (!objectp(ob) || !stringp(type))
        return 0;

    payload = mapp(payload) ? copy(payload) : ([]);
    if (!undefinedp(response_context))
        payload = apply_command_response_context(payload, response_context);
    if (type == XK_MAPS)
        payload += build_map_grid_dynamic_payload(ob);

    probe = should_probe_xk_protocol_target(ob);
    if (probe) {
        send_started_at = perf_counter_ns();
        build_started_at = send_started_at;
    }

    /*
     * 统一从这里进协议封包，调用方只交业务 payload，
     * 不应该在业务层自己拼 XK_HEADER 或终止符。
     */
    msg = _create_message(type, payload);
    if (probe)
        record_xk_protocol_tail_probe("xk_protocol_build",
            perf_counter_ns() - build_started_at);
    result = _send_raw(ob, msg);
    if (probe)
        record_xk_protocol_tail_probe("xk_protocol_send",
            perf_counter_ns() - send_started_at);
    return result;
}
int xk_send_message(object ob, string text, string channel)
{
    return xk_send_message_ex(ob, text, channel, ([]));
}
int xk_send_message_ex(object ob, string text, string channel, mapping extra)
{
    mapping payload;

    if (!objectp(ob) || !stringp(text))
        return 0;

    payload = mapp(extra) ? copy(extra) : ([]);
    payload += ([
        "content": text,
        "channel": stringp(channel) ? channel : "main"
    ]);

    return xk_send(ob, XK_CHAT, payload);
}
int xk_send_text_packet(object ob, string text, mapping options)
{
    mapping payload;

    if (!objectp(ob) || !stringp(text) || text == "")
        return 0;

    options = mapp(options) ? options : ([]);
    payload = copy(options);
    payload["text"] = text;
    payload["channel"] = stringp(options["channel"]) && options["channel"] != ""
        ? options["channel"] : "main";
    payload["mode"] = stringp(options["mode"]) && options["mode"] != ""
        ? options["mode"] : "append";
    payload["done"] = undefinedp(options["done"]) ? 1 : (options["done"] ? 1 : 0);

    return xk_send(ob, XK_TEXT, payload);
}
private mapping apply_command_response_context(mapping payload,
    mixed response_context)
{
    mapping meta;
    string response_id;

    payload = mapp(payload) ? copy(payload) : ([]);
    if (mapp(response_context)
        && stringp(response_context["command_request_id"])
        && response_context["command_request_id"] != "") {
        response_id = response_context["command_request_id"];
        payload["command_request_id"] = response_id;
        meta = mapp(payload["meta"]) ? copy(payload["meta"]) : ([]);
        meta["command_request_id"] = response_id;
        payload["meta"] = meta;
    } else
        payload["suppress_command_request_id"] = 1;
    return payload;
}

varargs int xk_send_system(object ob, string text, string level,
    mapping response_context)
{
    mapping payload;

    if (!objectp(ob) || !stringp(text))
        return 0;

    payload = ([
        "content": text,
        "level": stringp(level) ? level : "info"
    ]);

    if (!undefinedp(response_context))
        payload = apply_command_response_context(payload, response_context);
    return !undefinedp(response_context)
        ? xk_send(ob, XK_SYST, payload, response_context)
        : xk_send(ob, XK_SYST, payload);
}
varargs int xk_send_error(object ob, string error, int code,
    mapping response_context)
{
    mapping payload;

    if (!objectp(ob) || !stringp(error))
        return 0;

    payload = ([
        "message": error,
        "code": intp(code) ? code : 0
    ]);

    if (!undefinedp(response_context))
        payload = apply_command_response_context(payload, response_context);
    return xk_send(ob, XK_ERRO, payload);
}
varargs int xk_send_command_error(object ob, string error, int code,
    string command, string reason, string request, mapping response_context)
{
    mapping payload;

    if (!objectp(ob) || !stringp(error))
        return 0;

    payload = ([
        "message": error,
        "code": intp(code) ? code : 0,
    ]);
    if (stringp(command) && command != "")
        payload["command"] = command;
    if (stringp(reason) && reason != "")
        payload["reason"] = reason;
    if (stringp(request) && request != "")
        payload["request"] = request;
    if (!undefinedp(response_context))
        payload = apply_command_response_context(payload, response_context);

    return xk_send(ob, XK_ERRO, payload);
}
varargs int xk_send_message_event(object ob, mapping payload,
    mapping response_context)
{
    if (!objectp(ob) || !mapp(payload))
        return 0;
    if (!undefinedp(response_context))
        payload = apply_command_response_context(payload, response_context);

    return xk_send(ob, XK_MSGE, payload);
}
int xk_send_dialog(object ob, string title, string content, mapping extra)
{
    mapping payload;
    mixed response_context;
    int has_response_context;

    if (!objectp(ob))
        return 0;

    if (mapp(extra) && !undefinedp(extra["response_context"])) {
        has_response_context = 1;
        response_context = extra["response_context"];
        extra = copy(extra);
        map_delete(extra, "response_context");
    }
    payload = xk_build_dialog_payload(ob, title, content, extra);
    if (!mapp(payload))
        return 0;
    if (has_response_context)
        payload = apply_command_response_context(payload, response_context);
    return xk_send(ob, XK_DLOG, payload);
}
varargs int xk_send_preencoded_chat_batch(object ob,
    string *stable_children_json, mapping response_context)
{
    mapping reservation;
    mapping outer_dynamic_payload;
    mixed encode_error;
    string dynamic_json;
    int first_server_seq;
    int message_epoch;
    int reservation_id;
    int sent_at;
    int fill_result;

    /* Stable batch artifacts cannot safely carry a per-command response ID.
     * Return zero so the caller uses the ordinary batch path, which applies
     * the already-frozen context without decoding every child here. */
    if (mapp(response_context)
        && stringp(response_context["command_request_id"])
        && response_context["command_request_id"] != "")
        return 0;

    if (!objectp(ob) || !arrayp(stable_children_json)
        || sizeof(stable_children_json) == 0)
        return 0;

    reservation = reserve_batch_protocol(ob);
    if (!mapp(reservation) || reservation["ok"] != 1)
        return 0;
    reservation_id = reservation["reservation_id"];
    if (!intp(reservation_id) || reservation_id <= 0
        || reservation["target_path"] != file_name(ob)
        || reservation["target_owner_epoch"] != vm_owner_epoch(ob)
        || !mapp(reservation["payload"])) {
        xk_release_reserved_protocol(ob, reservation);
        return 0;
    }

    first_server_seq = reserve_contiguous_server_seq_block(
        sizeof(stable_children_json));
    if (first_server_seq <= 0) {
        xk_release_reserved_protocol(ob, reservation);
        return 0;
    }
    message_epoch = query_target_stream_epoch(ob, "message");
    sent_at = time();
    outer_dynamic_payload = ([
        "meta": reservation["payload"]["meta"],
    ]);
    encode_error = catch(dynamic_json = encode_protocol_json(
        outer_dynamic_payload));
    if (encode_error || !stringp(dynamic_json) || dynamic_json == "") {
        xk_release_reserved_protocol(ob, reservation);
        return 0;
    }

    encode_error = catch(fill_result =
        gateway_session_fill_preencoded_chat_batch(ob, reservation_id,
            stable_children_json, message_epoch, first_server_seq, sent_at,
            dynamic_json));
    if (encode_error || !fill_result) {
        xk_release_reserved_protocol(ob, reservation);
        return 0;
    }
    reservation["filled"] = 1;
    if (!record_session_output_completion(ob, XK_BACH, reservation["payload"],
        reservation_id)) {
        /* L-36：批帧已在原 FIFO，只有登记未完成；返回 -1 禁止重发。 */
        reservation["fill_state"] = "filled_pending_reconcile";
        return -1;
    }
    reservation["fill_state"] = "completed";
    return 1;
}
varargs int xk_send_batch(object ob, mixed messages,
    mapping response_context)
{
    mapping payload;
    mapping child;
    mixed *children;
    int index;

    if (!objectp(ob))
        return 0;

    if (mapp(messages)) {
        payload = copy(messages);
        if (!arrayp(payload["messages"]))
            payload["messages"] = ({ });
    } else if (arrayp(messages)) {
        payload = ([ "messages": messages ]);
    } else {
        payload = ([ "messages": ({ }) ]);
    }

    if (!undefinedp(response_context)) {
        children = payload["messages"];
        for (index = 0; index < sizeof(children); index++) {
            child = children[index];
            if (!mapp(child))
                continue;
            if (mapp(child["payload"])) {
                child = copy(child);
                child["payload"] = apply_command_response_context(
                    child["payload"], response_context);
                children[index] = child;
            } else
                children[index] = apply_command_response_context(
                    child, response_context);
        }
        payload["messages"] = children;
    }

    return !undefinedp(response_context)
        ? xk_send(ob, XK_BACH, payload, response_context)
        : xk_send(ob, XK_BACH, payload);
}
varargs int xk_send_report(object ob, string command, mapping data,
    mapping response_context)
{
    mapping payload;
    string gateway_session_id;

    if (!objectp(ob) || !stringp(command) || command == "")
        return 0;

    gateway_session_id = GATEWAY_SESSION_D->query_object_session_id(ob);
    if (!interactive(ob) && (!stringp(gateway_session_id) || gateway_session_id == ""))
        return 0;

    payload = ([
        "schema_version": XK_REPORT_SCHEMA_VERSION,
        "command": command,
        "data": mapp(data) ? copy(data) : ([]),
        "timestamp": time()
    ]);

    if (!undefinedp(response_context))
        payload = apply_command_response_context(payload, response_context);
    return xk_send(ob, XK_RPRT, payload);
}
varargs int xk_send_combat(object ob, string type, mapping data,
    mapping response_context)
{
    mapping payload;

    if (!objectp(ob) || !stringp(type))
        return 0;

    payload = mapp(data) ? copy(data) : ([]);
    payload["timestamp"] = time();
    if (!undefinedp(response_context))
        payload = apply_command_response_context(payload, response_context);
    return !undefinedp(response_context)
        ? xk_send(ob, type, payload, response_context)
        : xk_send(ob, type, payload);
}
int xk_send_heartbeat(object ob)
{
    return xk_send_pong(ob, 0);
}
void xk_broadcast(string type, mapping payload, object exclude)
{
    object *users;
    object ob;

    if (!stringp(type) || strlen(type) != 4)
        return;

    users = users();

    foreach (ob in users) {
        if (objectp(ob) && ob != exclude) {
            _send_raw(ob, _create_message(type, payload));
        }
    }
}
void xk_broadcast_message(string text, object exclude)
{
    mapping payload;

    if (!stringp(text))
        return;

    payload = ([
        "content": text,
        "channel": "broadcast"
    ]);

    xk_broadcast(XK_CHAT, payload, exclude);
}
void xk_broadcast_system(string text, string level, object exclude)
{
    mapping payload;

    if (!stringp(text))
        return;

    payload = ([
        "content": text,
        "level": stringp(level) ? level : "info"
    ]);

    xk_broadcast(XK_SYST, payload, exclude);
}
void xk_broadcast_room(object room, string type, mapping payload, object exclude)
{
    object *inv;
    object ob;

    if (!objectp(room) || !stringp(type))
        return;

    if (strlen(type) != 4)
        return;

    inv = all_inventory(room);

    foreach (ob in inv) {
        if (objectp(ob) && living(ob) && ob != exclude) {
            _send_raw(ob, _create_message(type, payload));
        }
    }
}
int xk_send_capa_response(object target, mapping data)
{
    mapping payload;
    string *features;

    if (!objectp(target))
        return 0;

    features = mapp(data) ? keys(data) : ({});

    payload = ([
        "version": XK_PROTOCOL_VERSION_STRING,
        "major": XK_PROTOCOL_VERSION_MAJOR,
        "minor": XK_PROTOCOL_VERSION_MINOR,
        "features": features,
        "capabilities": mapp(data) ? data : ([])
    ]);

    return xk_send(target, XK_CAPA, payload);
}
int xk_send_pong(object target, mixed seq)
{
    mapping payload;

    if (!objectp(target))
        return 0;

    payload = ([
        "seq": intp(seq) ? seq : 0,
        "server_time": time(),
        "timestamp": time(),
        "status": "alive"
    ]);

    return xk_send(target, XK_PONG, payload);
}
