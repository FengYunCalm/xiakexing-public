/**
 * @file    client_sync_flush/fanout.c
 * @brief   deferred chat fanout 波次与批量提交
 *
 * @par Description:
 * 含 {reset_deferred_chat_fanout_queue_if_idle, reset_deferred_chat_fanout_wave, begin_deferred_chat_fanout_wave, append_deferred_chat_fanout_wave_packet, commit_deferred_chat_fanout_wave, peek_pending_chat_fanout_job, consume_pending_chat_fanout_targets, complete_pending_chat_fanout_job, schedule_deferred_chat_fanout_materialize, record_deferred_chat_fanout_enqueue_diagnostics, record_deferred_chat_fanout_materialize_diagnostics, record_deferred_chat_fanout_callback_diagnostics, materialize_deferred_chat_fanout_job, materialize_deferred_chat_fanout, enqueue_deferred_chat_fanout, submit_chat_message_batch, queue_player_sync, query_pending_player_sync, query_player_sync_pending_status}。
 * 持有 fanout 队列状态（_pending_chat_fanout_* 系列）。
 *
 * 本模块是 `client_sync_flush.c` 继承链的一环，由上层模块继承；
 * 跨模块调用经前向声明 + 运行时动态分派（this_object() 解析到最终对象）。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>
#include <system/xk_protocol.h>

inherit "/daemons/services/system/sync/client_sync_flush/projection";

#define CHAT_ENTRY_DIRECTION_INCOMING 2
#define CHAT_ENTRY_DIRECTION_OTHER 3
#define CHAT_ENTRY_DIRECTION_SELF 1
#define CHAT_ENTRY_LANE_CHAT 1
#define CHAT_ENTRY_LANE_NORMAL 2
#define CHAT_FANOUT_MATERIALIZE_CALLBACK_WALL_BUDGET_NS 8000000
#define CHAT_FANOUT_MATERIALIZE_DELAY_SECONDS 0.001
#define CHAT_FANOUT_MATERIALIZE_TARGET_BUDGET 48
#define CHAT_FANOUT_MATERIALIZE_WALL_BUDGET_NS 2000000

nosave mapping _pending_chat_fanout_queue;
nosave int _pending_chat_fanout_queue_head;
nosave int _pending_chat_fanout_queue_tail;
nosave int _pending_chat_fanout_targets;
nosave int _pending_chat_fanout_active_wave_tail;
nosave mapping _pending_chat_fanout_wave_entries;
nosave object *_pending_chat_fanout_wave_recipients;
nosave int _pending_chat_fanout_wave_recipient_count;
nosave int _chat_fanout_call_out;
nosave int _chat_fanout_generation;

int append_deferred_chat_fanout_wave_packet();
void begin_deferred_chat_fanout_wave();
int can_deliver_to_target();
int chat_batch_diagnostic_int();
int chat_packet_direction_code(mapping packet);
int commit_deferred_chat_fanout_wave();
void complete_pending_chat_fanout_job();
void consume_pending_chat_fanout_targets();
int elapsed_ns_or_unavailable();
void enqueue_deferred_chat_fanout();
int entry_immediate_packet_count(mapping entry);
int has_refresh_work(mapping plan);
void materialize_deferred_chat_fanout();
mapping materialize_deferred_chat_fanout_job();
mapping normalize_sync_plan(mapping plan);
mapping peek_pending_chat_fanout_job();
mapping query_pending_player_sync();
string query_plan_key();
mapping query_player_sync_pending_status();
int queue_chat_pending_wave_entry_without_schedule();
int queue_pending_entry();
void queue_player_sync();
void record_chat_batch_duration();
void record_chat_entry_lane_count_diagnostics();
void record_deferred_chat_fanout_callback_diagnostics();
void record_deferred_chat_fanout_enqueue_diagnostics();
void record_deferred_chat_fanout_materialize_diagnostics();
void record_room_chat_command_phase_diagnostics();
void reset_deferred_chat_fanout_queue_if_idle();
void reset_deferred_chat_fanout_wave();
private void schedule_deferred_chat_fanout_materialize();
void schedule_initial_chat_sync_flush();
void submit_chat_message_batch();

private void reset_deferred_chat_fanout_queue_if_idle()
{
    if (mapp(_pending_chat_fanout_queue)
        && _pending_chat_fanout_queue_head < _pending_chat_fanout_queue_tail)
        return;
    if (_pending_chat_fanout_active_wave_tail > 0)
        return;
    _pending_chat_fanout_queue = ([]);
    _pending_chat_fanout_queue_head = 1;
    _pending_chat_fanout_queue_tail = 1;
    _pending_chat_fanout_targets = 0;
}

void reset_deferred_chat_fanout_wave()
{
    _pending_chat_fanout_active_wave_tail = 0;
    _pending_chat_fanout_wave_entries = ([]);
    _pending_chat_fanout_wave_recipients = ({ });
    _pending_chat_fanout_wave_recipient_count = 0;
}

private void begin_deferred_chat_fanout_wave()
{
    int active_jobs;

    if (_pending_chat_fanout_active_wave_tail
        > _pending_chat_fanout_queue_head)
        return;
    if (!mapp(_pending_chat_fanout_queue)
        || _pending_chat_fanout_queue_head >= _pending_chat_fanout_queue_tail)
        return;

    _pending_chat_fanout_active_wave_tail =
        _pending_chat_fanout_queue_tail;
    _pending_chat_fanout_wave_entries = ([]);
    _pending_chat_fanout_wave_recipients = allocate(8);
    _pending_chat_fanout_wave_recipient_count = 0;
    active_jobs = _pending_chat_fanout_active_wave_tail
        - _pending_chat_fanout_queue_head;
    if (!mapp(_chat_batch_diagnostics))
        _chat_batch_diagnostics = ([]);
    _chat_batch_diagnostics["client_sync_chat_fanout_waves_started"] =
        chat_batch_diagnostic_int("client_sync_chat_fanout_waves_started") + 1;
    _chat_batch_diagnostics["client_sync_chat_fanout_wave_jobs_total"] =
        chat_batch_diagnostic_int("client_sync_chat_fanout_wave_jobs_total")
        + active_jobs;
    if (active_jobs > chat_batch_diagnostic_int(
        "client_sync_chat_fanout_wave_jobs_max"))
        _chat_batch_diagnostics["client_sync_chat_fanout_wave_jobs_max"] =
            active_jobs;
}

private int append_deferred_chat_fanout_wave_packet(object player,
    mapping packet, int queued_at)
{
    mapping entry;
    mixed *grown_packets;
    mixed *packets;
    object *grown_recipients;
    int capacity;
    int count;
    int direction;
    int index;

    if (!objectp(player) || !mapp(packet)
        || packet["kind"] != "chat_message")
        return 0;
    if (!mapp(_pending_chat_fanout_wave_entries))
        _pending_chat_fanout_wave_entries = ([]);
    entry = _pending_chat_fanout_wave_entries[player];
    if (!mapp(entry)) {
        capacity = arrayp(_pending_chat_fanout_wave_recipients)
            ? sizeof(_pending_chat_fanout_wave_recipients) : 0;
        if (capacity <= _pending_chat_fanout_wave_recipient_count) {
            capacity = capacity < 8 ? 8 : capacity * 2;
            grown_recipients = allocate(capacity);
            for (count = 0; count < _pending_chat_fanout_wave_recipient_count;
                count++)
                grown_recipients[count] =
                    _pending_chat_fanout_wave_recipients[count];
            _pending_chat_fanout_wave_recipients = grown_recipients;
        }
        _pending_chat_fanout_wave_recipients[
            _pending_chat_fanout_wave_recipient_count++] = player;
        entry = ([
            "packets": allocate(8),
            "packet_count": 0,
            "queued_at_ns": queued_at,
            "self_count": 0,
            "incoming_count": 0,
            "other_count": 0,
        ]);
        _pending_chat_fanout_wave_entries[player] = entry;
    }

    packets = entry["packets"];
    count = intp(entry["packet_count"]) ? entry["packet_count"] : 0;
    capacity = arrayp(packets) ? sizeof(packets) : 0;
    if (capacity <= count) {
        capacity = capacity < 8 ? 8 : capacity * 2;
        packets = arrayp(packets) ? packets : ({ });
        grown_packets = allocate(capacity);
        for (index = 0; index < count; index++)
            grown_packets[index] = packets[index];
        packets = grown_packets;
    }
    packets[count] = packet;
    entry["packets"] = packets;
    entry["packet_count"] = count + 1;
    if (!intp(entry["queued_at_ns"]) || entry["queued_at_ns"] <= 0
        || (queued_at > 0 && queued_at < entry["queued_at_ns"]))
        entry["queued_at_ns"] = queued_at;
    direction = chat_packet_direction_code(packet);
    if (direction == CHAT_ENTRY_DIRECTION_SELF)
        entry["self_count"] = entry["self_count"] + 1;
    else if (direction == CHAT_ENTRY_DIRECTION_INCOMING)
        entry["incoming_count"] = entry["incoming_count"] + 1;
    else
        entry["other_count"] = entry["other_count"] + 1;
    return 1;
}

private int commit_deferred_chat_fanout_wave()
{
    mapping session_ids;
    mapping wave_entry;
    mixed session_lookup_error;
    object *recipients;
    object player;
    string session_id;
    int chat_incoming;
    int chat_other;
    int chat_self;
    int commit_cpu_finished_at;
    int commit_cpu_started_at;
    int commit_finished_at;
    int commit_started_at;
    int dropped_count;
    int lane;
    int normal_incoming;
    int normal_other;
    int normal_self;
    int packet_count;
    int published_packets;
    int published_recipients;
    int recipient_count;

    recipient_count = _pending_chat_fanout_wave_recipient_count;
    if (recipient_count <= 0
        || !arrayp(_pending_chat_fanout_wave_recipients)) {
        reset_deferred_chat_fanout_wave();
        return 0;
    }
    recipients = _pending_chat_fanout_wave_recipients[0..recipient_count - 1];
    commit_started_at = perf_counter_ns();
    commit_cpu_started_at = thread_cpu_time_ns();
    session_lookup_error = catch(session_ids =
        GATEWAY_SESSION_D->query_object_session_ids(recipients));
    if (session_lookup_error || !mapp(session_ids))
        session_ids = ([]);

    foreach (player in recipients) {
        wave_entry = mapp(_pending_chat_fanout_wave_entries[player])
            ? _pending_chat_fanout_wave_entries[player] : 0;
        packet_count = mapp(wave_entry) && intp(wave_entry["packet_count"])
            ? wave_entry["packet_count"] : 0;
        if (!mapp(wave_entry) || packet_count <= 0)
            continue;
        if (!can_deliver_to_target(player)) {
            dropped_count += packet_count;
            continue;
        }
        session_id = stringp(session_ids[player]) ? session_ids[player] : "";
        lane = queue_chat_pending_wave_entry_without_schedule(
            player, wave_entry, session_id);
        if (lane == 0) {
            dropped_count += packet_count;
            continue;
        }
        published_recipients++;
        published_packets += packet_count;
        if (lane == CHAT_ENTRY_LANE_CHAT) {
            chat_self += intp(wave_entry["self_count"])
                ? wave_entry["self_count"] : 0;
            chat_incoming += intp(wave_entry["incoming_count"])
                ? wave_entry["incoming_count"] : 0;
            chat_other += intp(wave_entry["other_count"])
                ? wave_entry["other_count"] : 0;
        } else {
            normal_self += intp(wave_entry["self_count"])
                ? wave_entry["self_count"] : 0;
            normal_incoming += intp(wave_entry["incoming_count"])
                ? wave_entry["incoming_count"] : 0;
            normal_other += intp(wave_entry["other_count"])
                ? wave_entry["other_count"] : 0;
        }
    }
    commit_cpu_finished_at = thread_cpu_time_ns();
    commit_finished_at = perf_counter_ns();

    record_chat_entry_lane_count_diagnostics(CHAT_ENTRY_LANE_CHAT,
        CHAT_ENTRY_DIRECTION_SELF, chat_self);
    record_chat_entry_lane_count_diagnostics(CHAT_ENTRY_LANE_CHAT,
        CHAT_ENTRY_DIRECTION_INCOMING, chat_incoming);
    record_chat_entry_lane_count_diagnostics(CHAT_ENTRY_LANE_CHAT,
        CHAT_ENTRY_DIRECTION_OTHER, chat_other);
    record_chat_entry_lane_count_diagnostics(CHAT_ENTRY_LANE_NORMAL,
        CHAT_ENTRY_DIRECTION_SELF, normal_self);
    record_chat_entry_lane_count_diagnostics(CHAT_ENTRY_LANE_NORMAL,
        CHAT_ENTRY_DIRECTION_INCOMING, normal_incoming);
    record_chat_entry_lane_count_diagnostics(CHAT_ENTRY_LANE_NORMAL,
        CHAT_ENTRY_DIRECTION_OTHER, normal_other);

    if (!mapp(_chat_batch_diagnostics))
        _chat_batch_diagnostics = ([]);
    _chat_batch_diagnostics["client_sync_chat_fanout_waves_committed"] =
        chat_batch_diagnostic_int("client_sync_chat_fanout_waves_committed") + 1;
    _chat_batch_diagnostics["client_sync_chat_fanout_wave_recipients_total"] =
        chat_batch_diagnostic_int(
            "client_sync_chat_fanout_wave_recipients_total")
        + published_recipients;
    _chat_batch_diagnostics["client_sync_chat_fanout_wave_packets_total"] =
        chat_batch_diagnostic_int("client_sync_chat_fanout_wave_packets_total")
        + published_packets;
    if (recipient_count > chat_batch_diagnostic_int(
        "client_sync_chat_fanout_wave_recipients_max"))
        _chat_batch_diagnostics[
            "client_sync_chat_fanout_wave_recipients_max"] = recipient_count;
    if (dropped_count > 0)
        _chat_batch_diagnostics["client_sync_chat_fanout_targets_dropped"] =
            chat_batch_diagnostic_int(
                "client_sync_chat_fanout_targets_dropped") + dropped_count;
    record_chat_batch_duration("client_sync_chat_fanout_wave_commit_wall",
        elapsed_ns_or_unavailable(commit_started_at, commit_finished_at));
    record_chat_batch_duration("client_sync_chat_fanout_wave_commit_cpu",
        elapsed_ns_or_unavailable(commit_cpu_started_at,
            commit_cpu_finished_at));
    reset_deferred_chat_fanout_wave();
    return published_recipients;
}

private mapping peek_pending_chat_fanout_job()
{
    mapping job;

    if (!mapp(_pending_chat_fanout_queue)
        || _pending_chat_fanout_queue_head >= _pending_chat_fanout_queue_tail)
        return 0;
    job = _pending_chat_fanout_queue[_pending_chat_fanout_queue_head];
    return mapp(job) ? job : 0;
}

private void consume_pending_chat_fanout_targets(int target_count)
{
    if (target_count <= 0)
        return;
    _pending_chat_fanout_targets -= target_count;
    if (_pending_chat_fanout_targets < 0)
        _pending_chat_fanout_targets = 0;
}

private void complete_pending_chat_fanout_job()
{
    if (!mapp(_pending_chat_fanout_queue)
        || _pending_chat_fanout_queue_head >= _pending_chat_fanout_queue_tail)
        return;
    map_delete(_pending_chat_fanout_queue, _pending_chat_fanout_queue_head++);
}

private void schedule_deferred_chat_fanout_materialize()
{
    if (_chat_fanout_call_out || !mapp(_pending_chat_fanout_queue)
        || _pending_chat_fanout_queue_head >= _pending_chat_fanout_queue_tail)
        return;
    _chat_fanout_generation++;
    /* Required CHAT lifecycle work shares Gateway priority with the bounded
     * input/main continuations. Each callback still has count and wall bounds. */
    _chat_fanout_call_out = call_out_walltime_gateway(
        "materialize_deferred_chat_fanout",
        CHAT_FANOUT_MATERIALIZE_DELAY_SECONDS, _chat_fanout_generation);
}

private void record_deferred_chat_fanout_enqueue_diagnostics(int target_count)
{
    int queue_depth;

    if (target_count <= 0)
        return;
    if (!mapp(_chat_batch_diagnostics))
        _chat_batch_diagnostics = ([]);
    _chat_batch_diagnostics["client_sync_chat_fanout_jobs_queued"] =
        chat_batch_diagnostic_int("client_sync_chat_fanout_jobs_queued") + 1;
    _chat_batch_diagnostics["client_sync_chat_fanout_targets_queued"] =
        chat_batch_diagnostic_int("client_sync_chat_fanout_targets_queued")
        + target_count;
    queue_depth = _pending_chat_fanout_queue_tail
        - _pending_chat_fanout_queue_head;
    if (queue_depth > chat_batch_diagnostic_int(
        "client_sync_chat_fanout_queue_depth_max"))
        _chat_batch_diagnostics["client_sync_chat_fanout_queue_depth_max"] =
            queue_depth;
}

private void record_deferred_chat_fanout_materialize_diagnostics(
    int target_count, int materialized_count, int dropped_count,
    int job_completed, int target_budget_yield, int wall_budget_yield,
    int delay_ns, int wall_ns, int cpu_ns)
{
    if (!mapp(_chat_batch_diagnostics))
        _chat_batch_diagnostics = ([]);
    _chat_batch_diagnostics["client_sync_chat_fanout_slices"] =
        chat_batch_diagnostic_int("client_sync_chat_fanout_slices") + 1;
    if (job_completed)
        _chat_batch_diagnostics["client_sync_chat_fanout_jobs_materialized"] =
            chat_batch_diagnostic_int(
                "client_sync_chat_fanout_jobs_materialized") + 1;
    if (target_budget_yield)
        _chat_batch_diagnostics[
            "client_sync_chat_fanout_target_budget_yields"] =
            chat_batch_diagnostic_int(
                "client_sync_chat_fanout_target_budget_yields") + 1;
    if (wall_budget_yield)
        _chat_batch_diagnostics[
            "client_sync_chat_fanout_wall_budget_yields"] =
            chat_batch_diagnostic_int(
                "client_sync_chat_fanout_wall_budget_yields") + 1;
    if (target_count > chat_batch_diagnostic_int(
        "client_sync_chat_fanout_slice_targets_max"))
        _chat_batch_diagnostics[
            "client_sync_chat_fanout_slice_targets_max"] = target_count;
    _chat_batch_diagnostics["client_sync_chat_fanout_targets_materialized"] =
        chat_batch_diagnostic_int(
            "client_sync_chat_fanout_targets_materialized")
        + materialized_count;
    _chat_batch_diagnostics["client_sync_chat_fanout_targets_dropped"] =
        chat_batch_diagnostic_int("client_sync_chat_fanout_targets_dropped")
        + dropped_count;
    record_chat_batch_duration("client_sync_chat_fanout_materialize_delay",
        delay_ns);
    record_chat_batch_duration("client_sync_chat_fanout_materialize_wall",
        wall_ns);
    record_chat_batch_duration("client_sync_chat_fanout_materialize_cpu",
        cpu_ns);
}

private void record_deferred_chat_fanout_callback_diagnostics(
    int slice_count, int target_count, int wall_budget_yield)
{
    if (!mapp(_chat_batch_diagnostics))
        _chat_batch_diagnostics = ([]);
    _chat_batch_diagnostics["client_sync_chat_fanout_callbacks"] =
        chat_batch_diagnostic_int("client_sync_chat_fanout_callbacks") + 1;
    _chat_batch_diagnostics["client_sync_chat_fanout_callback_slices_total"] =
        chat_batch_diagnostic_int(
            "client_sync_chat_fanout_callback_slices_total") + slice_count;
    if (slice_count > chat_batch_diagnostic_int(
        "client_sync_chat_fanout_callback_slices_max"))
        _chat_batch_diagnostics[
            "client_sync_chat_fanout_callback_slices_max"] = slice_count;
    if (target_count > chat_batch_diagnostic_int(
        "client_sync_chat_fanout_callback_targets_max"))
        _chat_batch_diagnostics[
            "client_sync_chat_fanout_callback_targets_max"] = target_count;
    if (wall_budget_yield)
        _chat_batch_diagnostics[
            "client_sync_chat_fanout_callback_wall_budget_yields"] =
            chat_batch_diagnostic_int(
                "client_sync_chat_fanout_callback_wall_budget_yields") + 1;
}

private mapping materialize_deferred_chat_fanout_job(mapping job)
{
    mapping result;
    mapping packet;
    object *all_players;
    object *players;
    object player;
    int actual_target_count;
    int dropped_count;
    int cursor;
    int finished_at;
    int limit;
    int materialized_count;
    int processed_count;
    int queued_at;
    int slice_started_at;
    int target_count;
    int wall_budget_yield;

    if (!mapp(job))
        return ([ "targets": 0, "materialized": 0, "dropped": 0,
            "complete": 1 ]);
    all_players = job["players"];
    packet = job["packet"];
    queued_at = intp(job["queued_at_ns"]) ? job["queued_at_ns"] : 0;
    slice_started_at = intp(job["slice_started_at_ns"])
        ? job["slice_started_at_ns"] : perf_counter_ns();
    target_count = intp(job["target_count"]) ? job["target_count"] : 0;
    cursor = intp(job["cursor"]) ? job["cursor"] : 0;
    if (cursor < 0)
        cursor = 0;
    if (target_count < 0)
        target_count = 0;
    /* The queue owns a frozen recipient array. Keep its saved accounting
     * conservative if a malformed or older job carries a stale count: never
     * slice past the actual recipients or falsely clear final pending work. */
    if (arrayp(all_players)) {
        actual_target_count = sizeof(all_players);
        if (target_count != actual_target_count) {
            target_count = actual_target_count;
            job["target_count"] = target_count;
        }
    }
    limit = cursor + CHAT_FANOUT_MATERIALIZE_TARGET_BUDGET;
    if (limit > target_count)
        limit = target_count;
    if (cursor >= limit) {
        job["cursor"] = target_count;
        return ([ "targets": 0, "materialized": 0, "dropped": 0,
            "complete": 1 ]);
    }
    if (!arrayp(all_players) || !mapp(packet)
        || packet["kind"] != "chat_message") {
        processed_count = limit - cursor;
        job["cursor"] = cursor + processed_count;
        return ([
            "targets": processed_count,
            "materialized": 0,
            "dropped": processed_count,
            "complete": job["cursor"] >= target_count,
            "target_budget_yield": job["cursor"] < target_count,
            "wall_budget_yield": 0,
        ]);
    }

    players = all_players[cursor..limit - 1];
    foreach (player in players) {
        processed_count++;
        if (!append_deferred_chat_fanout_wave_packet(player, packet,
            queued_at))
            dropped_count++;
        else
            materialized_count++;
        finished_at = perf_counter_ns();
        if (processed_count < sizeof(players)
            && finished_at >= slice_started_at
            && finished_at - slice_started_at
                >= CHAT_FANOUT_MATERIALIZE_WALL_BUDGET_NS) {
            wall_budget_yield = 1;
            break;
        }
    }
    job["cursor"] = cursor + processed_count;
    result = ([
        "targets": processed_count,
        "materialized": materialized_count,
        "dropped": dropped_count,
        "complete": job["cursor"] >= target_count,
        "target_budget_yield": !wall_budget_yield
            && job["cursor"] < target_count,
        "wall_budget_yield": wall_budget_yield,
    ]);
    return result;
}

void materialize_deferred_chat_fanout(int generation)
{
    mapping job;
    mapping materialized;
    int callback_started_at;
    int callback_targets;
    int callback_wall_budget_yield;
    int callback_slices;
    int committed;
    int cpu_finished_at;
    int cpu_started_at;
    int delay_ns;
    int materialized_at;
    int wall_finished_at;

    if (generation != _chat_fanout_generation)
        return;
    _chat_fanout_call_out = 0;
    begin_deferred_chat_fanout_wave();
    job = peek_pending_chat_fanout_job();
    if (!mapp(job)) {
        reset_deferred_chat_fanout_wave();
        reset_deferred_chat_fanout_queue_if_idle();
        return;
    }

    callback_started_at = perf_counter_ns();
    while (mapp(job) && _pending_chat_fanout_queue_head
        < _pending_chat_fanout_active_wave_tail) {
        materialized_at = perf_counter_ns();
        job["slice_started_at_ns"] = materialized_at;
        cpu_started_at = thread_cpu_time_ns();
        materialized = materialize_deferred_chat_fanout_job(job);
        cpu_finished_at = thread_cpu_time_ns();
        wall_finished_at = perf_counter_ns();
        delay_ns = elapsed_ns_or_unavailable(job["queued_at_ns"],
            materialized_at);
        consume_pending_chat_fanout_targets(materialized["targets"]);
        if (materialized["complete"])
            complete_pending_chat_fanout_job();
        map_delete(job, "slice_started_at_ns");
        callback_slices++;
        callback_targets += materialized["targets"];
        if (_pending_chat_fanout_queue_head
            >= _pending_chat_fanout_active_wave_tail) {
            committed = commit_deferred_chat_fanout_wave();
            if (committed > 0)
                schedule_initial_chat_sync_flush();
        }
        /* Arm the next head before diagnostics.  The current callback may
         * still catch up while quiet, but an unavailable timing metric must
         * never strand the FIFO after this slice. */
        schedule_deferred_chat_fanout_materialize();
        record_deferred_chat_fanout_materialize_diagnostics(
            materialized["targets"], materialized["materialized"],
            materialized["dropped"], materialized["complete"],
            materialized["target_budget_yield"],
            materialized["wall_budget_yield"], delay_ns,
            elapsed_ns_or_unavailable(materialized_at, wall_finished_at),
            elapsed_ns_or_unavailable(cpu_started_at, cpu_finished_at));

        /* Each recipient slice keeps its own wall bound. This second bound
         * amortizes timer admission without making one callback unbounded. */
        if (wall_finished_at < callback_started_at
            || wall_finished_at - callback_started_at
                >= CHAT_FANOUT_MATERIALIZE_CALLBACK_WALL_BUDGET_NS) {
            callback_wall_budget_yield = 1;
            break;
        }
        if (_pending_chat_fanout_active_wave_tail <=
            _pending_chat_fanout_queue_head)
            begin_deferred_chat_fanout_wave();
        job = peek_pending_chat_fanout_job();
    }

    /* Rearm business state before optional diagnostics so a timing failure
     * cannot strand later room messages in the FIFO. */
    schedule_deferred_chat_fanout_materialize();
    reset_deferred_chat_fanout_queue_if_idle();
    record_deferred_chat_fanout_callback_diagnostics(callback_slices,
        callback_targets, callback_wall_budget_yield);
}

private void enqueue_deferred_chat_fanout(object *players, mapping packet,
    int queued_at)
{
    int target_count;

    if (!arrayp(players) || !mapp(packet) || packet["kind"] != "chat_message")
        return;
    target_count = sizeof(players);
    if (target_count <= 0)
        return;
    if (!mapp(_pending_chat_fanout_queue))
        reset_deferred_chat_fanout_queue_if_idle();
    _pending_chat_fanout_queue[_pending_chat_fanout_queue_tail++] = ([
        "players": players + ({ }),
        "packet": packet,
        "queued_at_ns": queued_at,
        "target_count": target_count,
        "cursor": 0,
    ]);
    _pending_chat_fanout_targets += target_count;
    record_deferred_chat_fanout_enqueue_diagnostics(target_count);
    schedule_deferred_chat_fanout_materialize();
}

void submit_chat_message_batch(object *players, string text, string channel,
    mapping extra, int chat_origin_ns)
{
    mapping packet;
    mapping protocol_batch_child;
    mapping protocol_payload;
    mixed encode_error;
    string stable_child_json;
    int queued_at;
    int wall_started_at;
    int cpu_started_at;
    int cpu_finished_at;

    if (!arrayp(players) || sizeof(players) == 0 || !stringp(text) || text == "")
        return;

    wall_started_at = perf_counter_ns();
    cpu_started_at = thread_cpu_time_ns();
    channel = stringp(channel) && channel != "" ? channel : "main";
    queued_at = perf_counter_ns();
    protocol_payload = mapp(extra) ? copy(extra) : ([]);
    protocol_payload["content"] = text;
    protocol_payload["channel"] = channel;
    protocol_batch_child = ([
        "type": XK_CHAT,
        "payload": protocol_payload,
    ]);
    /* The business child is immutable across the room fanout.  Only the
     * recipient's server sequence, epoch and send time remain dynamic. */
    encode_error = catch(stable_child_json = call_other(
        "/kernel/framework/xk_protocol_core",
        "xk_preencode_chat_batch_child", protocol_payload));
    if (encode_error || !stringp(stable_child_json))
        stable_child_json = "";
    /* The room-chat fact is identical for every recipient.  Freeze it before
     * fanout; each pending entry still owns its own array, route and schedule,
     * while the protocol boundary copies the stable child before adding
     * per-session sequence, epoch and send-time metadata. */
    packet = ([
        "kind": "chat_message",
        "text": text,
        "channel": channel,
        "extra": protocol_payload,
        "protocol_payload": protocol_payload,
        "protocol_batch_child": protocol_batch_child,
        "protocol_stable_child_json": stable_child_json,
        "chat_pending_since_ns": queued_at,
    ]);
    if (chat_origin_ns > 0)
        packet["chat_origin_ns"] = chat_origin_ns;
    enqueue_deferred_chat_fanout(players, packet, queued_at);
    cpu_finished_at = thread_cpu_time_ns();
    record_room_chat_command_phase_diagnostics("fanout_enqueue",
        perf_counter_ns() - wall_started_at,
        elapsed_ns_or_unavailable(cpu_started_at, cpu_finished_at));
}

void queue_player_sync(object player, mapping plan)
{
    queue_pending_entry(player, plan, 0, 0);
}

mapping query_pending_player_sync(object player)
{
    string key;

    key = query_plan_key(player);
    if (key == "")
        return 0;

    return normalize_sync_plan(mapp(_pending_sync_plans[key])
        ? _pending_sync_plans[key]["plan"]
        : 0);
}

mapping query_player_sync_pending_status(object player)
{
    mapping chat_entry;
    mapping normal_entry;
    string key;
    int chat_immediate_packets;
    int chat_pending;
    int core_pending;
    int normal_immediate_packets;
    int normal_refresh_pending;
    int score_refresh_pending;

    key = query_plan_key(player);
    if (key == "")
        return 0;

    normal_entry = mapp(_pending_sync_plans[key])
        ? _pending_sync_plans[key] : 0;
    chat_entry = mapp(_pending_chat_sync_plans[key])
        ? _pending_chat_sync_plans[key] : 0;
    normal_immediate_packets = entry_immediate_packet_count(normal_entry);
    normal_refresh_pending = mapp(normal_entry)
        && has_refresh_work(normalize_sync_plan(normal_entry["plan"])) ? 1 : 0;
    chat_immediate_packets = entry_immediate_packet_count(chat_entry);
    chat_pending = mapp(chat_entry) ? 1 : 0;
    score_refresh_pending = mapp(_pending_score_refreshes)
        && _pending_score_refreshes[key] ? 1 : 0;
    core_pending = (mapp(normal_entry) ? 1 : 0)
        + normal_immediate_packets + normal_refresh_pending;

    return ([
        "key": key,
        "normal_entry_pending": mapp(normal_entry) ? 1 : 0,
        "normal_immediate_packets": normal_immediate_packets,
        "normal_refresh_pending": normal_refresh_pending,
        "chat_entry_pending": chat_pending,
        "chat_immediate_packets": chat_immediate_packets,
        "score_refresh_pending": score_refresh_pending,
        "core_pending": core_pending,
        "deferred_pending": chat_pending + chat_immediate_packets
            + score_refresh_pending,
        "total_pending": core_pending + chat_pending + chat_immediate_packets
            + score_refresh_pending,
    ]);
}
