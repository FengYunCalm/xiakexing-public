/**
 * @file    client_sync_flush/diagnostics.c
 * @brief   诊断记账与批处理计时
 *
 * @par Description:
 * 含 {chat_batch_diagnostic_int, single_thread_mode, record_undeliverable_delivery_drop, query_undeliverable_delivery_drop, record_player_transaction_drop, query_player_transaction_drops, clear_player_transaction_drops, elapsed_ns_or_unavailable, merge_chat_batch_duration_summary, record_chat_batch_duration, record_room_chat_command_phase_diagnostics, record_chat_batch_flush_diagnostics, record_scheduled_chat_flush_delay_diagnostics, record_scheduled_chat_flush_turnover_diagnostics, record_chat_singleton_diagnostics, chat_packet_direction_code, record_chat_entry_lane_count_diagnostics, record_chat_entry_lane_diagnostics, record_pending_chat_age_diagnostics, record_pending_chat_packet_age_summary, record_pending_flush_diagnostics, query_chat_batch_diagnostic_status}。
 * 本模块是 client_sync_flush 继承链最底层，持有诊断记账状态（_chat_batch_diagnostics、_delivery_drop_counters、_player_transaction_drops）；上层模块经继承直接访问。
 *
 * 本模块是 `client_sync_flush.c` 继承链的一环，由上层模块继承；
 * 跨模块调用经前向声明 + 运行时动态分派（this_object() 解析到最终对象）。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>
#include <system/xk_protocol.h>

#define CHAT_ENTRY_DIRECTION_INCOMING 2
#define CHAT_ENTRY_DIRECTION_OTHER 3
#define CHAT_ENTRY_DIRECTION_SELF 1
#define CHAT_ENTRY_LANE_CHAT 1
#define CHAT_ENTRY_LANE_NORMAL 2
#define CLIENT_SYNC_DROP_COMBAT_PACKET 1
#define CLIENT_SYNC_DROP_PENDING 3
#define CLIENT_SYNC_DROP_TRANSACTION 2

nosave mapping _chat_batch_diagnostics;
nosave mapping _delivery_drop_counters;
nosave mapping _player_transaction_drops;

int chat_batch_diagnostic_int();
int chat_packet_direction_code(mapping packet);
void clear_player_transaction_drops();
int elapsed_ns_or_unavailable();
int entry_has_chat_messages(mapping entry);
void merge_chat_batch_duration_summary();
mapping query_chat_batch_diagnostic_status();
int query_player_transaction_drops();
int query_undeliverable_delivery_drop();
void record_chat_batch_duration();
void record_chat_batch_flush_diagnostics();
void record_chat_entry_lane_count_diagnostics();
void record_chat_entry_lane_diagnostics();
void record_chat_singleton_diagnostics();
void record_pending_chat_age_diagnostics();
void record_pending_chat_packet_age_summary();
void record_pending_flush_diagnostics();
void record_player_transaction_drop();
void record_room_chat_command_phase_diagnostics();
void record_scheduled_chat_flush_delay_diagnostics();
void record_scheduled_chat_flush_turnover_diagnostics();
void record_undeliverable_delivery_drop();
int single_thread_mode();

int chat_batch_diagnostic_int(string key)
{
    return mapp(_chat_batch_diagnostics) && intp(_chat_batch_diagnostics[key])
        ? _chat_batch_diagnostics[key] : 0;
}

int single_thread_mode()
{
    int enabled;
    mixed err;

    err = catch(enabled = MULTICORE_CONFIG_D->query_mode_enabled(
        "single_thread"));
    return !err && enabled == 1;
}

/* Keep no-route suppression observable without attaching player, session or
 * object identity to a hot-path metric.  Counters live in the dedicated
 * _delivery_drop_counters mapping (independent of chat batch timing stats);
 * query_status() merges them under the same key names for existing consumers. */
void record_undeliverable_delivery_drop(int reason)
{
    string key;

    switch (reason) {
    case CLIENT_SYNC_DROP_COMBAT_PACKET:
        key = "client_sync_undeliverable_combat_packet_dropped";
        break;
    case CLIENT_SYNC_DROP_TRANSACTION:
        key = "client_sync_undeliverable_transaction_dropped";
        break;
    case CLIENT_SYNC_DROP_PENDING:
        key = "client_sync_undeliverable_pending_dropped";
        break;
    default:
        return;
    }
    if (!mapp(_delivery_drop_counters))
        _delivery_drop_counters = ([]);
    _delivery_drop_counters[key] =
        intp(_delivery_drop_counters[key]) ? _delivery_drop_counters[key] + 1 : 1;
}

int query_undeliverable_delivery_drop(int reason)
{
    string key;

    switch (reason) {
    case CLIENT_SYNC_DROP_COMBAT_PACKET:
        key = "client_sync_undeliverable_combat_packet_dropped";
        break;
    case CLIENT_SYNC_DROP_TRANSACTION:
        key = "client_sync_undeliverable_transaction_dropped";
        break;
    case CLIENT_SYNC_DROP_PENDING:
        key = "client_sync_undeliverable_pending_dropped";
        break;
    default:
        return 0;
    }
    return mapp(_delivery_drop_counters) && intp(_delivery_drop_counters[key])
        ? _delivery_drop_counters[key] : 0;
}

void record_player_transaction_drop(object player)
{
    string key;

    if (!objectp(player))
        return;
    key = file_name(player);
    if (!mapp(_player_transaction_drops))
        _player_transaction_drops = ([]);
    _player_transaction_drops[key] =
        intp(_player_transaction_drops[key]) ? _player_transaction_drops[key] + 1 : 1;
}

int query_player_transaction_drops(object player)
{
    string key;

    if (!objectp(player))
        return 0;
    key = file_name(player);
    return mapp(_player_transaction_drops)
        && intp(_player_transaction_drops[key]) ? _player_transaction_drops[key] : 0;
}

void clear_player_transaction_drops(object player)
{
    string key;

    if (!objectp(player))
        return;
    key = file_name(player);
    if (mapp(_player_transaction_drops))
        map_delete(_player_transaction_drops, key);
}

int elapsed_ns_or_unavailable(mixed started_at, mixed finished_at)
{
    if (!intp(started_at) || !intp(finished_at))
        return -1;
    if (started_at < 0 || finished_at < started_at)
        return -1;
    return finished_at - started_at;
}

void merge_chat_batch_duration_summary(string prefix, int samples,
    int total_us, int max_us, int unavailable_count)
{
    if (!stringp(prefix) || prefix == ""
        || samples < 0 || total_us < 0 || max_us < 0 || unavailable_count < 0)
        return;
    if (samples == 0 && unavailable_count == 0)
        return;
    if (!mapp(_chat_batch_diagnostics))
        _chat_batch_diagnostics = ([]);
    if (unavailable_count > 0)
        _chat_batch_diagnostics[prefix + "_unavailable_count"] =
            chat_batch_diagnostic_int(prefix + "_unavailable_count")
            + unavailable_count;
    if (samples == 0)
        return;
    _chat_batch_diagnostics[prefix + "_samples"] =
        chat_batch_diagnostic_int(prefix + "_samples") + samples;
    _chat_batch_diagnostics[prefix + "_total_us"] =
        chat_batch_diagnostic_int(prefix + "_total_us") + total_us;
    if (max_us > chat_batch_diagnostic_int(prefix + "_max_us"))
        _chat_batch_diagnostics[prefix + "_max_us"] = max_us;
}

void record_chat_batch_duration(string prefix, int elapsed_ns)
{
    int elapsed_us;

    if (!stringp(prefix) || prefix == "")
        return;
    if (!intp(elapsed_ns) || elapsed_ns < 0) {
        merge_chat_batch_duration_summary(prefix, 0, 0, 0, 1);
        return;
    }
    elapsed_us = elapsed_ns / 1000;
    merge_chat_batch_duration_summary(prefix, 1, elapsed_us, elapsed_us, 0);
}

void record_room_chat_command_phase_diagnostics(string phase, int wall_ns,
    int cpu_ns)
{
    string prefix;

    if (member_array(phase, ({ "speaker_flush", "fanout_enqueue" })) == -1)
        return;
    prefix = "client_sync_room_chat_" + phase;
    record_chat_batch_duration(prefix + "_wall", wall_ns);
    record_chat_batch_duration(prefix + "_cpu", cpu_ns);
}

void record_chat_batch_flush_diagnostics(int message_count, int build_wall_ns,
    int build_cpu_ns, int send_wall_ns, int send_cpu_ns, int preencoded_success)
{
    if (message_count < 2)
        return;
    if (!mapp(_chat_batch_diagnostics))
        _chat_batch_diagnostics = ([]);
    _chat_batch_diagnostics["client_sync_chat_batch_samples"] =
        chat_batch_diagnostic_int("client_sync_chat_batch_samples") + 1;
    _chat_batch_diagnostics["client_sync_chat_batch_messages_total"] =
        chat_batch_diagnostic_int("client_sync_chat_batch_messages_total") + message_count;
    if (message_count > chat_batch_diagnostic_int("client_sync_chat_batch_messages_max"))
        _chat_batch_diagnostics["client_sync_chat_batch_messages_max"] = message_count;
    _chat_batch_diagnostics[preencoded_success
        ? "client_sync_chat_batch_preencoded_success"
        : "client_sync_chat_batch_preencoded_fallback"] = chat_batch_diagnostic_int(
        preencoded_success
            ? "client_sync_chat_batch_preencoded_success"
            : "client_sync_chat_batch_preencoded_fallback") + 1;
    record_chat_batch_duration("client_sync_chat_batch_build_wall", build_wall_ns);
    record_chat_batch_duration("client_sync_chat_batch_build_cpu", build_cpu_ns);
    record_chat_batch_duration("client_sync_chat_batch_send_wall", send_wall_ns);
    record_chat_batch_duration("client_sync_chat_batch_send_cpu", send_cpu_ns);
}

void record_scheduled_chat_flush_delay_diagnostics(int due_ns)
{
    int elapsed_ns;

    if (due_ns <= 0)
        return;
    elapsed_ns = perf_counter_ns() - due_ns;
    record_chat_batch_duration("client_sync_scheduled_chat_flush_delay", elapsed_ns);
}

void record_scheduled_chat_flush_turnover_diagnostics(string event)
{
    string key;

    if (member_array(event, ({ "scheduled", "cancelled", "executed",
        "sliced", "continued", "command_preserved", "command_chat_only",
        "command_mixed", "command_mixed_due_preserved", "wall_budget_yield",
        "wave_started", "wave_completed" })) == -1)
        return;
    if (!mapp(_chat_batch_diagnostics))
        _chat_batch_diagnostics = ([]);
    key = "client_sync_scheduled_chat_flush_" + event;
    _chat_batch_diagnostics[key] = chat_batch_diagnostic_int(key) + 1;
}

void record_chat_singleton_diagnostics(mapping packet, int delivered,
    int send_wall_ns, int send_cpu_ns)
{
    mapping extra;
    string direction;

    if (!mapp(packet))
        return;
    if (!mapp(_chat_batch_diagnostics))
        _chat_batch_diagnostics = ([]);
    direction = "other";
    extra = mapp(packet["extra"]) ? packet["extra"] : ([]);
    if (stringp(extra["direction"]) && extra["direction"] != "")
        direction = extra["direction"];
    _chat_batch_diagnostics["client_sync_chat_singleton_samples"] =
        chat_batch_diagnostic_int("client_sync_chat_singleton_samples") + 1;
    _chat_batch_diagnostics[delivered
        ? "client_sync_chat_singleton_delivery_success"
        : "client_sync_chat_singleton_delivery_failure"] = chat_batch_diagnostic_int(delivered
        ? "client_sync_chat_singleton_delivery_success"
        : "client_sync_chat_singleton_delivery_failure") + 1;
    _chat_batch_diagnostics[direction == "self"
        ? "client_sync_chat_singleton_self_samples"
        : direction == "incoming"
            ? "client_sync_chat_singleton_incoming_samples"
            : "client_sync_chat_singleton_other_samples"] = chat_batch_diagnostic_int(direction == "self"
        ? "client_sync_chat_singleton_self_samples"
        : direction == "incoming"
            ? "client_sync_chat_singleton_incoming_samples"
            : "client_sync_chat_singleton_other_samples") + 1;
    record_chat_batch_duration("client_sync_chat_singleton_send_wall", send_wall_ns);
    record_chat_batch_duration("client_sync_chat_singleton_send_cpu", send_cpu_ns);
}

int chat_packet_direction_code(mapping packet)
{
    mapping extra;

    if (!mapp(packet))
        return CHAT_ENTRY_DIRECTION_OTHER;
    extra = mapp(packet["extra"]) ? packet["extra"] : ([]);
    if (extra["direction"] == "self")
        return CHAT_ENTRY_DIRECTION_SELF;
    if (extra["direction"] == "incoming")
        return CHAT_ENTRY_DIRECTION_INCOMING;
    return CHAT_ENTRY_DIRECTION_OTHER;
}

void record_chat_entry_lane_count_diagnostics(int lane, int direction,
    int count)
{
    string key;

    if (count <= 0 || (lane != CHAT_ENTRY_LANE_CHAT
        && lane != CHAT_ENTRY_LANE_NORMAL))
        return;
    if (!mapp(_chat_batch_diagnostics))
        _chat_batch_diagnostics = ([]);
    if (lane == CHAT_ENTRY_LANE_CHAT) {
        key = direction == CHAT_ENTRY_DIRECTION_SELF
            ? "client_sync_chat_entry_chat_self"
            : direction == CHAT_ENTRY_DIRECTION_INCOMING
                ? "client_sync_chat_entry_chat_incoming"
                : "client_sync_chat_entry_chat_other";
    } else {
        key = direction == CHAT_ENTRY_DIRECTION_SELF
            ? "client_sync_chat_entry_normal_self"
            : direction == CHAT_ENTRY_DIRECTION_INCOMING
                ? "client_sync_chat_entry_normal_incoming"
                : "client_sync_chat_entry_normal_other";
    }
    _chat_batch_diagnostics[key] = chat_batch_diagnostic_int(key) + count;
}

void record_chat_entry_lane_diagnostics(string lane, mixed packets)
{
    mixed packet;
    int lane_code;

    lane_code = lane == "chat" ? CHAT_ENTRY_LANE_CHAT
        : lane == "normal" ? CHAT_ENTRY_LANE_NORMAL : 0;
    if (lane_code == 0 || !arrayp(packets))
        return;
    foreach (packet in packets) {
        if (!mapp(packet) || packet["kind"] != "chat_message")
            continue;
        record_chat_entry_lane_count_diagnostics(lane_code,
            chat_packet_direction_code(packet), 1);
    }
}

void record_pending_chat_age_diagnostics(mapping entry)
{
    int queued_at;
    int elapsed_ns;

    if (!mapp(entry))
        return;
    queued_at = intp(entry["chat_pending_since_ns"])
        ? entry["chat_pending_since_ns"] : 0;
    if (queued_at <= 0) {
        if (!entry_has_chat_messages(entry))
            return;
        if (!mapp(_chat_batch_diagnostics))
            _chat_batch_diagnostics = ([]);
        _chat_batch_diagnostics["client_sync_pending_chat_age_unavailable_count"] =
            chat_batch_diagnostic_int("client_sync_pending_chat_age_unavailable_count") + 1;
        return;
    }
    if (!mapp(_chat_batch_diagnostics))
        _chat_batch_diagnostics = ([]);
    elapsed_ns = perf_counter_ns() - queued_at;
    if (elapsed_ns < 0) {
        _chat_batch_diagnostics["client_sync_pending_chat_age_unavailable_count"] =
            chat_batch_diagnostic_int("client_sync_pending_chat_age_unavailable_count") + 1;
        return;
    }
    _chat_batch_diagnostics["client_sync_pending_chat_age_samples"] =
        chat_batch_diagnostic_int("client_sync_pending_chat_age_samples") + 1;
    _chat_batch_diagnostics["client_sync_pending_chat_age_total_us"] =
        chat_batch_diagnostic_int("client_sync_pending_chat_age_total_us") + elapsed_ns / 1000;
    if (elapsed_ns / 1000 > chat_batch_diagnostic_int("client_sync_pending_chat_age_max_us"))
        _chat_batch_diagnostics["client_sync_pending_chat_age_max_us"] = elapsed_ns / 1000;
}

void record_pending_chat_packet_age_summary(int *samples, int *totals_us,
    int *max_us, int *unavailable, int *origin_metrics)
{
    if (!arrayp(samples) || !arrayp(totals_us) || !arrayp(max_us)
        || !arrayp(unavailable) || sizeof(samples) < 3
        || sizeof(totals_us) < 3 || sizeof(max_us) < 3
        || sizeof(unavailable) < 3 || !arrayp(origin_metrics)
        || sizeof(origin_metrics) < 4)
        return;
    merge_chat_batch_duration_summary("client_sync_pending_chat_self_age",
        samples[0], totals_us[0], max_us[0], unavailable[0]);
    merge_chat_batch_duration_summary("client_sync_pending_chat_incoming_age",
        samples[1], totals_us[1], max_us[1], unavailable[1]);
    merge_chat_batch_duration_summary("client_sync_pending_chat_other_age",
        samples[2], totals_us[2], max_us[2], unavailable[2]);
    merge_chat_batch_duration_summary("client_sync_room_chat_origin_age",
        origin_metrics[0], origin_metrics[1], origin_metrics[2],
        origin_metrics[3]);
}

void record_pending_flush_diagnostics(int entry_count, int recipient_count,
    int packet_count, int wall_ns, int cpu_ns)
{
    if (entry_count < 1)
        return;
    if (!mapp(_chat_batch_diagnostics))
        _chat_batch_diagnostics = ([]);
    _chat_batch_diagnostics["client_sync_pending_flush_samples"] =
        chat_batch_diagnostic_int("client_sync_pending_flush_samples") + 1;
    _chat_batch_diagnostics["client_sync_pending_flush_entries_total"] =
        chat_batch_diagnostic_int("client_sync_pending_flush_entries_total") + entry_count;
    if (entry_count > chat_batch_diagnostic_int("client_sync_pending_flush_entries_max"))
        _chat_batch_diagnostics["client_sync_pending_flush_entries_max"] = entry_count;
    _chat_batch_diagnostics["client_sync_pending_flush_recipients_total"] =
        chat_batch_diagnostic_int("client_sync_pending_flush_recipients_total") + recipient_count;
    if (recipient_count > chat_batch_diagnostic_int("client_sync_pending_flush_recipients_max"))
        _chat_batch_diagnostics["client_sync_pending_flush_recipients_max"] = recipient_count;
    _chat_batch_diagnostics["client_sync_pending_flush_packets_total"] =
        chat_batch_diagnostic_int("client_sync_pending_flush_packets_total") + packet_count;
    if (packet_count > chat_batch_diagnostic_int("client_sync_pending_flush_packets_max"))
        _chat_batch_diagnostics["client_sync_pending_flush_packets_max"] = packet_count;
    record_chat_batch_duration("client_sync_pending_flush_wall", wall_ns);
    record_chat_batch_duration("client_sync_pending_flush_cpu", cpu_ns);
}

mapping query_chat_batch_diagnostic_status()
{
    mapping status;
    string field;

    status = ([]);
    foreach (field in ({
        "client_sync_undeliverable_combat_packet_dropped",
        "client_sync_undeliverable_transaction_dropped",
        "client_sync_undeliverable_pending_dropped",
        "client_sync_chat_batch_samples",
        "client_sync_chat_batch_messages_total",
        "client_sync_chat_batch_messages_max",
        "client_sync_chat_batch_preencoded_success",
        "client_sync_chat_batch_preencoded_fallback",
        "client_sync_chat_singleton_samples",
        "client_sync_chat_singleton_delivery_success",
        "client_sync_chat_singleton_delivery_failure",
        "client_sync_chat_singleton_self_samples",
        "client_sync_chat_singleton_incoming_samples",
        "client_sync_chat_singleton_other_samples",
        "client_sync_chat_entry_chat_self",
        "client_sync_chat_entry_chat_incoming",
        "client_sync_chat_entry_chat_other",
        "client_sync_chat_entry_normal_self",
        "client_sync_chat_entry_normal_incoming",
        "client_sync_chat_entry_normal_other",
        "client_sync_chat_fanout_jobs_queued",
        "client_sync_chat_fanout_jobs_materialized",
        "client_sync_chat_fanout_targets_queued",
        "client_sync_chat_fanout_targets_materialized",
        "client_sync_chat_fanout_targets_dropped",
        "client_sync_chat_fanout_queue_depth_max",
        "client_sync_chat_fanout_slices",
        "client_sync_chat_fanout_target_budget_yields",
        "client_sync_chat_fanout_wall_budget_yields",
        "client_sync_chat_fanout_slice_targets_max",
        "client_sync_chat_fanout_pressure_deferrals",
        "client_sync_chat_fanout_command_deferred",
        "client_sync_chat_fanout_main_queue_deferred",
        "client_sync_chat_fanout_buffered_input_deferred",
        "client_sync_chat_fanout_forced_liveness_callbacks",
        "client_sync_chat_fanout_forced_liveness_targets",
        "client_sync_chat_fanout_callbacks",
        "client_sync_chat_fanout_callback_slices_total",
        "client_sync_chat_fanout_callback_slices_max",
        "client_sync_chat_fanout_callback_targets_max",
        "client_sync_chat_fanout_callback_wall_budget_yields",
        "client_sync_chat_fanout_waves_started",
        "client_sync_chat_fanout_waves_committed",
        "client_sync_chat_fanout_wave_jobs_total",
        "client_sync_chat_fanout_wave_jobs_max",
        "client_sync_chat_fanout_wave_recipients_total",
        "client_sync_chat_fanout_wave_recipients_max",
        "client_sync_chat_fanout_wave_packets_total",
        "client_sync_pending_flush_samples",
        "client_sync_pending_flush_entries_total",
        "client_sync_pending_flush_entries_max",
        "client_sync_pending_flush_recipients_total",
        "client_sync_pending_flush_recipients_max",
        "client_sync_pending_flush_packets_total",
        "client_sync_pending_flush_packets_max",
        "client_sync_pending_chat_age_samples",
        "client_sync_pending_chat_age_total_us",
        "client_sync_pending_chat_age_max_us",
        "client_sync_pending_chat_age_unavailable_count",
        "client_sync_pending_chat_self_age_unavailable_count",
        "client_sync_pending_chat_incoming_age_unavailable_count",
        "client_sync_pending_chat_other_age_unavailable_count",
        "client_sync_room_chat_origin_age_unavailable_count",
        "client_sync_scheduled_chat_flush_scheduled",
        "client_sync_scheduled_chat_flush_cancelled",
        "client_sync_scheduled_chat_flush_executed",
        "client_sync_scheduled_chat_flush_sliced",
        "client_sync_scheduled_chat_flush_continued",
        "client_sync_scheduled_chat_flush_command_preserved",
        "client_sync_scheduled_chat_flush_command_chat_only",
        "client_sync_scheduled_chat_flush_command_mixed",
        "client_sync_scheduled_chat_flush_command_mixed_due_preserved",
        "client_sync_scheduled_chat_flush_command_pressure_yield",
        "client_sync_scheduled_chat_flush_main_queue_pressure_yield",
        "client_sync_scheduled_chat_flush_wall_budget_yield",
        "client_sync_scheduled_chat_flush_buffered_input_admission_yield",
        "client_sync_scheduled_chat_flush_buffered_input_admission_forced",
        "client_sync_scheduled_chat_flush_wave_started",
        "client_sync_scheduled_chat_flush_wave_completed",
        "client_sync_score_refresh_queued",
        "client_sync_score_refresh_single_thread_skipped",
        "client_sync_score_refresh_coalesced",
        "client_sync_score_refresh_executed",
        "client_sync_score_refresh_failed",
        "client_sync_score_refresh_diagnostic_failed",
        "client_sync_score_refresh_slices",
        "client_sync_score_refresh_recipients_max",
        "client_sync_score_refresh_wall_samples",
        "client_sync_score_refresh_wall_total_us",
        "client_sync_score_refresh_wall_max_us",
        "client_sync_score_refresh_wall_unavailable_count",
        "client_sync_score_refresh_cpu_samples",
        "client_sync_score_refresh_cpu_total_us",
        "client_sync_score_refresh_cpu_max_us",
        "client_sync_score_refresh_command_deferred",
        "client_sync_score_refresh_main_queue_deferred",
        "client_sync_score_refresh_buffered_input_deferred",
        "client_sync_score_refresh_mid_slice_pressure_yield",
        "client_sync_score_refresh_wall_budget_yield",
        "client_sync_score_refresh_batch_limit_yield",
        "client_sync_score_refresh_forced_liveness_slices",
        "client_sync_score_refresh_forced_liveness_executed",
        "client_sync_score_refresh_oldest_age_unavailable_count",
        "client_sync_score_refresh_pressure_deferrals_max",
        "client_sync_score_refresh_slice_wall_unavailable_count",
        "client_sync_chat_batch_build_cpu_unavailable_count",
        "client_sync_chat_batch_send_cpu_unavailable_count",
        "client_sync_chat_singleton_send_cpu_unavailable_count",
        "client_sync_pending_flush_cpu_unavailable_count",
        "client_sync_room_chat_speaker_flush_cpu_unavailable_count",
        "client_sync_room_chat_fanout_enqueue_cpu_unavailable_count",
        "client_sync_chat_fanout_materialize_cpu_unavailable_count",
        "client_sync_score_refresh_cpu_unavailable_count"
    }))
        status[field] = chat_batch_diagnostic_int(field);
    foreach (field in ({
        "client_sync_chat_batch_build_wall",
        "client_sync_chat_batch_build_cpu",
        "client_sync_chat_batch_send_wall",
        "client_sync_chat_batch_send_cpu",
        "client_sync_chat_singleton_send_wall",
        "client_sync_chat_singleton_send_cpu",
        "client_sync_pending_chat_self_age",
        "client_sync_pending_chat_incoming_age",
        "client_sync_pending_chat_other_age",
        "client_sync_room_chat_origin_age",
        "client_sync_room_chat_speaker_flush_wall",
        "client_sync_room_chat_speaker_flush_cpu",
        "client_sync_room_chat_fanout_enqueue_wall",
        "client_sync_room_chat_fanout_enqueue_cpu",
        "client_sync_chat_fanout_materialize_delay",
        "client_sync_chat_fanout_materialize_wall",
        "client_sync_chat_fanout_materialize_cpu",
        "client_sync_chat_fanout_wave_commit_wall",
        "client_sync_chat_fanout_wave_commit_cpu",
        "client_sync_pending_flush_wall",
        "client_sync_pending_flush_cpu",
        "client_sync_scheduled_chat_flush_delay",
        "client_sync_score_refresh_wall",
        "client_sync_score_refresh_cpu",
        "client_sync_score_refresh_slice_wall",
        "client_sync_score_refresh_oldest_age"
    })) {
        status[field + "_samples"] = chat_batch_diagnostic_int(field + "_samples");
        status[field + "_total_us"] = chat_batch_diagnostic_int(field + "_total_us");
        status[field + "_max_us"] = chat_batch_diagnostic_int(field + "_max_us");
    }
    return status;
}
