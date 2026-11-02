/**
 * @file    client_sync_flush.c
 * @brief   客户端同步 pending 账本与 flush 编排模块
 *
 * @par Description:
 * 本文件是 client_sync_flush 继承链的 facade：保留 flush 编排与公开 API，
 * 内部按机制簇拆分为 /daemons/services/system/sync/client_sync_flush/ 下的链式继承模块
 * （diagnostics -> routes -> schedule -> delivery -> packets -> projection
 *  -> fanout -> score_refresh），状态随各 owner 模块声明，本门面经继承访问。
 * 本链不能独立实例化：create_sync_plan/normalize_sync_plan/merge_sync_plan/
 * plan_projection/has_refresh_work/apply_*_packet_buffer 等定义在 client_sync_d.c
 * 的兄弟继承（client_sync_plan/client_sync_packets），须经 client_sync_d 加载。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>
#include <system/xk_protocol.h>

inherit "/daemons/services/system/sync/client_sync_flush/score_refresh";

#define CLIENT_SYNC_DROP_TRANSACTION 2

mixed *apply_entry_immediate_packets();
mixed *apply_immediate_packets(object player, mixed packets);
int apply_sync_plan(object player, mapping plan);
int can_deliver_to_target();
mapping create_sync_plan();
void detach_rescued_overdue_chat_flush_callout();
void discard_player_pending();
void discard_session_pending();
int elapsed_ns_or_unavailable();
int entry_has_chat_messages(mapping entry);
int entry_has_only_chat_messages();
int entry_immediate_packet_count(mapping entry);
mixed *entry_immediate_packets(mapping entry);
int entry_projection_is_current(object player, mapping entry);
int flush_due_normal_lane_chat_entries();
void flush_pending_chat_slice();
varargs void flush_pending_syncs(int flush_chat, int scheduled_callout,
    int scheduled_generation, int only_chat_entries, int suppress_reschedule);
mapping flush_player_command_sync();
mapping flush_player_sync();
int has_refresh_work(mapping plan);
mapping normalize_sync_plan(mapping plan);
mapping normalize_sync_transaction(mapping tx);
mapping partition_immediate_packets(mixed packets);
int pending_entry_matches_session(string key, mapping entry,
    string session_id);
mapping plan_projection(mapping plan, mapping projection);
mapping plan_status(mapping plan);
mapping query_chat_batch_diagnostic_status();
string query_plan_key();
string query_session_key();
mapping query_status();
int queue_pending_entry();
int queue_pending_transaction();
int queue_pending_room_snapshot();
void queue_player_sync();
void record_pending_chat_age_diagnostics();
void record_pending_flush_diagnostics();
void record_player_transaction_drop();
void record_scheduled_chat_flush_delay_diagnostics();
void record_scheduled_chat_flush_turnover_diagnostics();
void record_undeliverable_delivery_drop();
mapping refresh_plan_projection_for_current(object player, mapping plan);
void remove_pending_chat_entry(string key);
void replace_entry_immediate_packets(mapping entry, mixed packets);
void request_inventory();
void request_inventory_room();
void request_light_status_delayed();
void request_room_snapshot();
void request_status();
void request_status_delayed();
void request_status_inventory();
void request_status_inventory_room();
void request_status_inventory_score_refresh();
void request_status_room_snapshot();
void reset_deferred_chat_fanout_wave();
void reset_pending_chat_queue();
void reset_score_refresh_queue_if_idle();
object resolve_sync_target();
object resolve_sync_target_key();
void schedule_deferred_chat_sync_flush();
void schedule_deferred_normal_sync_flush();
void schedule_initial_chat_sync_flush();
varargs void schedule_sync_flush();
int scheduled_chat_flush_is_due();
int scheduled_chat_flush_wall_budget_exhausted();
int score_refresh_entry_matches_session(string key, mapping state,
    string session_id);
void send_light_status_now();
void send_light_status_now_by_key();
void setup_client_sync_flush();
void store_pending_entry(string key, mapping entry);
int submit_immediate_chat_ack();
int submit_transaction();
void sync_player_state();
mixed *take_pending_chat_entry();
mapping take_refreshed_plan_with_chat_retention(object player,
    mapping entry, string key);
int transaction_carries_room_snapshot();
int transaction_has_only_chat_messages();
mapping tx_plan_inventory(mapping tx);
mapping tx_plan_inventory_room(mapping tx, object room);
mapping tx_plan_room_snapshot(mapping tx, object room);
mapping tx_plan_status(mapping tx);
mapping tx_plan_status_inventory(mapping tx);
mapping tx_plan_status_inventory_room(mapping tx, object room);
mapping tx_plan_status_inventory_score_refresh(mapping tx);
mapping tx_plan_status_room_snapshot(mapping tx, object room);
mapping tx_with_response_context(mapping tx, mapping response_context);
mapping plan_with_response_context(mapping plan, mapping response_context);
mapping capture_command_response_context(object player);

int submit_immediate_chat_ack(object player, mapping tx)
{
    mapping packet;
    mapping payload;
    mixed *immediate;
    mixed *undelivered;

    if (!can_deliver_to_target(player))
        return 0;
    tx = normalize_sync_transaction(tx);
    immediate = tx["immediate"];
    if (has_refresh_work(tx["refresh"]) || !arrayp(immediate)
        || sizeof(immediate) != 1)
        return 0;
    packet = immediate[0];
    payload = mapp(packet) && mapp(packet["protocol_payload"])
        ? packet["protocol_payload"] : 0;
    if (!mapp(packet) || packet["kind"] != "chat_message"
        || !mapp(payload) || payload["direction"] != "self")
        return 0;

    undelivered = apply_immediate_packets(player, immediate);
    return arrayp(undelivered) && sizeof(undelivered) == 0;
}

mapping query_status()
{
    int chat_consumer_armed;
    int chat_consumer_callout_valid;
    int chat_pending;

    chat_pending = mapp(_pending_chat_sync_plans)
        ? sizeof(_pending_chat_sync_plans) : 0;
    chat_consumer_armed = chat_pending > 0 && _flush_call_out ? 1 : 0;
    chat_consumer_callout_valid = chat_consumer_armed
        && find_call_out(_flush_call_out) >= 0 ? 1 : 0;

    return ([
        "mode": "client_sync_owner_route_facade",
        "service_map_v2": 1,
        "service": "client_sync",
        "policy": "owner_scheduler",
        "pending_entries": mapp(_pending_sync_plans) ? sizeof(_pending_sync_plans) : 0,
        "client_sync_chat_pending": chat_pending,
        "client_sync_chat_consumer_armed": chat_consumer_armed,
        "client_sync_chat_consumer_callout_valid":
            chat_consumer_callout_valid,
        "client_sync_chat_consumer_orphaned": chat_pending > 0
            && !chat_consumer_callout_valid ? 1 : 0,
        "client_sync_chat_queue_depth": mapp(_pending_chat_sync_queue_ticket)
            ? sizeof(_pending_chat_sync_queue_ticket) : 0,
        "client_sync_chat_active_wave_remaining":
            _pending_chat_sync_active_wave_tail > _pending_chat_sync_queue_head
                ? _pending_chat_sync_active_wave_tail
                    - _pending_chat_sync_queue_head : 0,
        "client_sync_chat_fanout_queue_depth":
            mapp(_pending_chat_fanout_queue)
            && _pending_chat_fanout_queue_head < _pending_chat_fanout_queue_tail
                ? _pending_chat_fanout_queue_tail
                    - _pending_chat_fanout_queue_head : 0,
        "client_sync_chat_fanout_pending_targets":
            _pending_chat_fanout_targets,
        "client_sync_chat_fanout_active_wave_jobs_remaining":
            _pending_chat_fanout_active_wave_tail
                > _pending_chat_fanout_queue_head
                    ? _pending_chat_fanout_active_wave_tail
                        - _pending_chat_fanout_queue_head : 0,
        "client_sync_chat_fanout_wave_recipient_count":
            _pending_chat_fanout_wave_recipient_count,
        /* Retain the report schema while retiring CHAT admission polling. */
        "client_sync_chat_admission_deferrals": 0,
        "client_sync_score_refresh_pending": mapp(_pending_score_refreshes)
            ? sizeof(_pending_score_refreshes) : 0,
        "client_sync_score_refresh_queue_depth": mapp(_score_refresh_queue)
            && _score_refresh_queue_head < _score_refresh_queue_tail
                ? _score_refresh_queue_tail - _score_refresh_queue_head : 0,
        "enabled_by_default": 0,
        "owner_output_boundary": 1,
        "message_only_cross_owner": 1,
        "direct_cross_owner_write": 0,
    ]) + query_chat_batch_diagnostic_status() + (_delivery_drop_counters || ([]));
}

mapping flush_player_sync(object player)
{
    mapping entry;
    mixed *immediate;
    mapping plan;
    string key;

    key = query_plan_key(player);
    if (key == "")
        return 0;

    entry = mapp(_pending_sync_plans[key]) ? _pending_sync_plans[key]
        : mapp(_pending_chat_sync_plans[key]) ? _pending_chat_sync_plans[key] : 0;
    if (!mapp(entry))
        return create_sync_plan();
    if (!entry_projection_is_current(player, entry)) {
        mapping retained_plan;

        /*
         * 投影过期（玩家对象重建后 advance_projection 推进）：统一 helper
         * 取出原 plan 刷新投影并保留 chat 入队，保证客户端 REAL 同步链不断
         * （否则客户端 gate 以旧投影拒绝后续所有房间包，表现为“后端移动、
         * 前端永不刷新”）；这里直接消费 plan，不再走下方 immediate 逻辑。
         */
        retained_plan = take_refreshed_plan_with_chat_retention(
            player, entry, key);
        plan = normalize_sync_plan(retained_plan);
        if (has_refresh_work(plan) && !can_deliver_to_target(player)) {
            queue_pending_entry(player, plan, 0, 1);
            return plan;
        }
        if (!apply_sync_plan(player, plan)) {
            queue_pending_entry(player, plan, 0, 1);
            return plan;
        }
        return plan;
    }
    plan = normalize_sync_plan(entry["plan"]);
    map_delete(_pending_sync_plans, key);
    remove_pending_chat_entry(key);

    immediate = apply_entry_immediate_packets(player, entry, 0);
    if (sizeof(immediate) > 0 || (has_refresh_work(plan) && !can_deliver_to_target(player))) {
        queue_pending_entry(player, plan, immediate, 1);
        return plan;
    }

    if (!apply_sync_plan(player, plan)) {
        queue_pending_entry(player, plan, 0, 1);
        return plan;
    }

    return plan;
}

mapping flush_player_command_sync(object player)
{
    mapping entry;
    mapping partition;
    mixed *immediate;
    mixed *chat_messages;
    mapping plan;
    string key;
    int flush_chat;
    int pending_chat_age_now;

    key = query_plan_key(player);
    if (key == "")
        return 0;

    entry = mapp(_pending_sync_plans[key]) ? _pending_sync_plans[key]
        : mapp(_pending_chat_sync_plans[key]) ? _pending_chat_sync_plans[key] : 0;
    if (!mapp(entry))
        return 0;

    if (!entry_projection_is_current(player, entry)) {
        mapping retained_plan;

        /*
         * 与 flush_player_sync 同语义（统一 helper）：投影过期时取出原 plan
         * 刷新投影直接消费，保证 REAL/房间快照同步链不断；chat 单独入队。
         */
        retained_plan = take_refreshed_plan_with_chat_retention(
            player, entry, key);
        plan = normalize_sync_plan(retained_plan);
        if (has_refresh_work(plan) && !can_deliver_to_target(player)) {
            queue_pending_entry(player, plan, 0, 1);
            return plan;
        }
        if (!apply_sync_plan(player, plan)) {
            queue_pending_entry(player, plan, 0, 1);
            return plan;
        }
        return plan;
    }
    plan = normalize_sync_plan(entry["plan"]);
    map_delete(_pending_sync_plans, key);
    remove_pending_chat_entry(key);
    flush_chat = scheduled_chat_flush_is_due();
    pending_chat_age_now = 0;
    if (entry_has_chat_messages(entry)) {
        if (entry_has_only_chat_messages(entry)) {
            if (!flush_chat) {
                record_scheduled_chat_flush_turnover_diagnostics("command_preserved");
                record_scheduled_chat_flush_turnover_diagnostics("command_chat_only");
                return plan;
            }
        } else {
            record_scheduled_chat_flush_turnover_diagnostics("command_preserved");
            record_scheduled_chat_flush_turnover_diagnostics("command_mixed");
            if (flush_chat)
                record_scheduled_chat_flush_turnover_diagnostics("command_mixed_due_preserved");

            partition = partition_immediate_packets(entry_immediate_packets(entry));
            chat_messages = partition["chat"];
            immediate = partition["normal"];
            map_delete(_pending_sync_plans, key);
            remove_pending_chat_entry(key);
            if (!can_deliver_to_target(player)) {
                store_pending_entry(key, entry);
                return plan;
            }

            immediate = apply_immediate_packets(player, immediate);
            if (sizeof(immediate) > 0) {
                entry["player"] = player;
                replace_entry_immediate_packets(entry, chat_messages + immediate);
                entry["plan"] = plan;
                store_pending_entry(key, entry);
                return plan;
            }

            apply_sync_plan(player, plan);
            entry["player"] = player;
            replace_entry_immediate_packets(entry, chat_messages);
            entry["plan"] = create_sync_plan();
            store_pending_entry(key, entry);
            if (!_flush_call_out)
                schedule_initial_chat_sync_flush();
            return plan;
        }
    }

    map_delete(_pending_sync_plans, key);
    remove_pending_chat_entry(key);
    if (flush_chat) {
        record_pending_chat_age_diagnostics(entry);
        pending_chat_age_now = perf_counter_ns();
    }

    immediate = apply_entry_immediate_packets(player, entry,
        pending_chat_age_now);
    if (sizeof(immediate) > 0 || (has_refresh_work(plan) && !can_deliver_to_target(player))) {
        queue_pending_entry(player, plan, immediate, 1);
        return plan;
    }

    apply_sync_plan(player, plan);
    return plan;
}

private void flush_pending_chat_slice(int scheduled_callout)
{
    mapping entry;
    mapping plan;
    mixed *immediate;
    mixed *next_chat_entry;
    mixed flush_started_at;
    mixed flush_finished_at;
    mixed flush_cpu_started_at;
    mixed flush_cpu_finished_at;
    object player;
    string plan_key;
    string session_id;
    int chat_slice_deferred;
    int chat_wall_elapsed_ns;
    int due_normal_chat_deferred;
    int initial_chat_backlog;
    int pending_chat_age_now;
    int pending_packet_count;
    int recipient_count;
    int slice_ticket_limit;

    initial_chat_backlog = mapp(_pending_chat_sync_plans)
        ? sizeof(_pending_chat_sync_plans) : 0;
    if (initial_chat_backlog <= 0) {
        reset_pending_chat_queue();
        if (scheduled_callout) {
            due_normal_chat_deferred = flush_due_normal_lane_chat_entries();
            if (due_normal_chat_deferred)
                schedule_deferred_chat_sync_flush();
            else if (mapp(_pending_sync_plans)
                && sizeof(_pending_sync_plans) > 0)
                schedule_deferred_normal_sync_flush();
        }
        return;
    }

    if (_pending_chat_sync_active_wave_tail <= _pending_chat_sync_queue_head) {
        _pending_chat_sync_active_wave_tail = 0;
        _pending_chat_sync_active_wave_tail = _pending_chat_sync_queue_tail;
        record_scheduled_chat_flush_turnover_diagnostics("wave_started");
    }
    slice_ticket_limit = _pending_chat_sync_active_wave_tail;
    flush_started_at = perf_counter_ns();
    flush_cpu_started_at = thread_cpu_time_ns();
    while (_pending_chat_sync_queue_head < slice_ticket_limit) {
        next_chat_entry = take_pending_chat_entry();
        if (!arrayp(next_chat_entry) || sizeof(next_chat_entry) < 2)
            break;
        plan_key = stringp(next_chat_entry[0]) ? next_chat_entry[0] : "";
        entry = mapp(next_chat_entry[1]) ? next_chat_entry[1] : ([]);
        if (plan_key == "" || sizeof(entry) == 0)
            continue;

        recipient_count++;
        pending_packet_count += entry_immediate_packet_count(entry);
        player = objectp(entry["player"]) ? entry["player"] : 0;
        session_id = stringp(entry["session_id"])
            ? entry["session_id"] : "";
        /* 目标解析（非 gate）：与 resolve_sync_target 同语义的 entry 重建路径，
         * 保留内联以免破坏 entry 上下文（记账：有意保留的第三拷贝）。 */
        if ((!objectp(player) || !interactive(player)) && session_id != "")
            player = GATEWAY_SESSION_D->query_session_target(session_id);
        plan = normalize_sync_plan(entry["plan"]);
        record_pending_chat_age_diagnostics(entry);
        pending_chat_age_now = perf_counter_ns();
        immediate = apply_entry_immediate_packets(player, entry,
            pending_chat_age_now);
        if (!can_deliver_to_target(player)) {
            entry["player"] = player;
            replace_entry_immediate_packets(entry, immediate);
            entry["plan"] = plan;
            store_pending_entry(plan_key, entry);
        } else if (sizeof(immediate) > 0) {
            entry["player"] = player;
            replace_entry_immediate_packets(entry, immediate);
            entry["plan"] = plan;
            store_pending_entry(plan_key, entry);
        } else
            apply_sync_plan(player, plan);

        chat_wall_elapsed_ns = elapsed_ns_or_unavailable(
            flush_started_at, perf_counter_ns());
        if (scheduled_chat_flush_wall_budget_exhausted(chat_wall_elapsed_ns))
            break;
    }

    if (_pending_chat_sync_active_wave_tail <= _pending_chat_sync_queue_head) {
        _pending_chat_sync_active_wave_tail = 0;
        record_scheduled_chat_flush_turnover_diagnostics("wave_completed");
    }

    if ((!mapp(_pending_chat_sync_plans)
        || sizeof(_pending_chat_sync_plans) == 0)
        && _pending_chat_sync_queue_head >= _pending_chat_sync_queue_tail)
        reset_pending_chat_queue();

    if (scheduled_callout)
        due_normal_chat_deferred = flush_due_normal_lane_chat_entries();
    chat_slice_deferred = mapp(_pending_chat_sync_plans)
        && sizeof(_pending_chat_sync_plans) > 0;
    if (chat_slice_deferred)
        record_scheduled_chat_flush_turnover_diagnostics("sliced");
    if (chat_slice_deferred || due_normal_chat_deferred)
        schedule_deferred_chat_sync_flush();
    if (mapp(_pending_sync_plans) && sizeof(_pending_sync_plans) > 0
        && !_flush_call_out)
        schedule_deferred_normal_sync_flush();

    flush_cpu_finished_at = thread_cpu_time_ns();
    flush_finished_at = perf_counter_ns();
    record_pending_flush_diagnostics(recipient_count, recipient_count,
        pending_packet_count,
        elapsed_ns_or_unavailable(flush_started_at, flush_finished_at),
        elapsed_ns_or_unavailable(flush_cpu_started_at,
            flush_cpu_finished_at));
}

varargs void flush_pending_syncs(int flush_chat, int scheduled_callout,
    int scheduled_generation, int only_chat_entries, int suppress_reschedule)
{
    mapping pending;
    mapping remaining;
    string *plan_keys;
    string plan_key;
    int pending_entries;
    int pending_packet_count;
    int recipient_count;
    mixed flush_started_at;
    mixed flush_finished_at;
    mixed flush_cpu_started_at;
    mixed flush_cpu_finished_at;
    int flush_due_ns;

    flush_chat = flush_chat ? 1 : 0;
    scheduled_callout = scheduled_callout ? 1 : 0;
    only_chat_entries = only_chat_entries ? 1 : 0;
    suppress_reschedule = suppress_reschedule ? 1 : 0;
    if (scheduled_callout && scheduled_generation != _flush_call_out_generation)
        return;
    if (scheduled_callout) {
        flush_due_ns = _flush_call_out_due_ns;
        _flush_call_out = 0;
        _flush_call_out_delay = 0;
        _flush_call_out_due_ns = 0;
        _flush_call_out_chat = 0;
        if (flush_chat) {
            if (flush_due_ns > 0)
                record_scheduled_chat_flush_turnover_diagnostics("executed");
            record_scheduled_chat_flush_delay_diagnostics(flush_due_ns);
        }
    }
    else if (!flush_chat && _flush_call_out && !_flush_call_out_chat
        && _flush_call_out_delay == 0) {
        remove_call_out(_flush_call_out);
        _flush_call_out = 0;
        _flush_call_out_delay = 0;
        _flush_call_out_due_ns = 0;
        _flush_call_out_chat = 0;
    }
    if (!flush_chat && scheduled_chat_flush_is_due()
        && mapp(_pending_chat_sync_plans) && sizeof(_pending_chat_sync_plans) > 0) {
        flush_pending_syncs(1);
        detach_rescued_overdue_chat_flush_callout();
    }
    if (flush_chat) {
        flush_pending_chat_slice(scheduled_callout);
        return;
    }
    if (!flush_chat && only_chat_entries) {
        pending = ([]);
        foreach (plan_key in keys(_pending_sync_plans)) {
            if (!entry_has_chat_messages(_pending_sync_plans[plan_key]))
                continue;
            pending[plan_key] = _pending_sync_plans[plan_key];
            map_delete(_pending_sync_plans, plan_key);
        }
    }
    else if (!flush_chat) {
        pending = _pending_sync_plans;
        _pending_sync_plans = ([]);
    }

    if (!mapp(pending) || sizeof(pending) == 0) {
        /* A normal callout can be the only consumer promised to CHAT that was
         * published behind its handle.  Its normal entry may be consumed by a
         * command before this callback runs, but the empty callback must still
         * hand the already-waiting CHAT lane to a real continuation. */
        if (!flush_chat && !suppress_reschedule
            && mapp(_pending_chat_sync_plans)
            && sizeof(_pending_chat_sync_plans) > 0 && !_flush_call_out)
            schedule_deferred_chat_sync_flush();
        return;
    }

    flush_started_at = perf_counter_ns();
    flush_cpu_started_at = thread_cpu_time_ns();
    remaining = ([]);
    plan_keys = keys(pending);
    pending_entries = sizeof(plan_keys);
    foreach (plan_key in plan_keys) {
        mapping entry;
        mapping partition;
        mixed *immediate;
        mixed *chat_messages;
        mapping plan;
        object player;
        string session_id;
        int pending_chat_age_now;

        recipient_count++;
        entry = mapp(pending[plan_key]) ? pending[plan_key] : ([]);
        pending_packet_count += entry_immediate_packet_count(entry);
        player = objectp(entry["player"]) ? entry["player"] : 0;
        session_id = stringp(entry["session_id"]) ? entry["session_id"] : "";
        /* 目标解析（非 gate）：与 resolve_sync_target 同语义的 entry 重建路径，
         * 保留内联以免破坏 entry 上下文（记账：有意保留的第三拷贝）。 */
        if ((!objectp(player) || !interactive(player)) && session_id != "")
            player = GATEWAY_SESSION_D->query_session_target(session_id);
        if (objectp(player) && !entry_projection_is_current(player, entry)) {
            /*
             * 与 flush_player_sync 同语义（统一 helper）：投影过期时刷新 plan
             * 投影继续投递（REAL/房间快照为服务端现算），chat 单独保留入队，
             * 不再整包丢弃 refresh；apply 失败回退排队。
             */
            mapping retained_plan;

            retained_plan = take_refreshed_plan_with_chat_retention(
                player, entry, plan_key);
            plan = normalize_sync_plan(retained_plan);
            if (has_refresh_work(plan) && !can_deliver_to_target(player)) {
                queue_pending_entry(player, plan, 0, 1);
            } else if (has_refresh_work(plan)
                && !apply_sync_plan(player, plan)) {
                queue_pending_entry(player, plan, 0, 1);
            }
            continue;
        }
        plan = normalize_sync_plan(entry["plan"]);
        pending_chat_age_now = 0;
        if (!flush_chat && entry_has_chat_messages(entry)) {
            record_scheduled_chat_flush_turnover_diagnostics("command_preserved");
            if (entry_has_only_chat_messages(entry)) {
                record_scheduled_chat_flush_turnover_diagnostics("command_chat_only");
                remaining[plan_key] = entry;
                continue;
            }
            record_scheduled_chat_flush_turnover_diagnostics("command_mixed");
            partition = partition_immediate_packets(entry_immediate_packets(entry));
            chat_messages = partition["chat"];
            immediate = partition["normal"];
            if (!can_deliver_to_target(player)) {
                remaining[plan_key] = entry;
                continue;
            }
            immediate = apply_immediate_packets(player, immediate);
            if (sizeof(immediate) > 0) {
                entry["player"] = player;
                replace_entry_immediate_packets(entry, chat_messages + immediate);
                entry["plan"] = plan;
                remaining[plan_key] = entry;
                continue;
            }
            apply_sync_plan(player, plan);
            entry["player"] = player;
            replace_entry_immediate_packets(entry, chat_messages + immediate);
            entry["plan"] = create_sync_plan();
            remaining[plan_key] = entry;
            continue;
        }
        immediate = apply_entry_immediate_packets(player, entry,
            pending_chat_age_now);
        if (!can_deliver_to_target(player)) {
            entry["player"] = player;
            replace_entry_immediate_packets(entry, immediate);
            entry["plan"] = plan;
            remaining[plan_key] = entry;
            continue;
        }
        if (sizeof(immediate) > 0) {
            entry["player"] = player;
            replace_entry_immediate_packets(entry, immediate);
            entry["plan"] = plan;
            remaining[plan_key] = entry;
            continue;
        }
        apply_sync_plan(player, plan);
    }

    if (sizeof(remaining) > 0) {
        foreach (plan_key in keys(remaining))
            store_pending_entry(plan_key, remaining[plan_key]);
        if (!suppress_reschedule) {
            if (!_flush_call_out && mapp(_pending_chat_sync_plans)
                && sizeof(_pending_chat_sync_plans) > 0)
                schedule_initial_chat_sync_flush();
            else if (!_flush_call_out)
                schedule_sync_flush(1, 0);
        }
    }
    if (!flush_chat && mapp(_pending_chat_sync_plans)
        && sizeof(_pending_chat_sync_plans) > 0 && !_flush_call_out
        && !suppress_reschedule)
        schedule_initial_chat_sync_flush();
    if (!flush_chat && scheduled_chat_flush_is_due()
        && mapp(_pending_chat_sync_plans) && sizeof(_pending_chat_sync_plans) > 0) {
        flush_pending_syncs(1);
        detach_rescued_overdue_chat_flush_callout();
    }
    flush_cpu_finished_at = thread_cpu_time_ns();
    flush_finished_at = perf_counter_ns();
    record_pending_flush_diagnostics(pending_entries, recipient_count,
        pending_packet_count,
        elapsed_ns_or_unavailable(flush_started_at, flush_finished_at),
        elapsed_ns_or_unavailable(flush_cpu_started_at,
            flush_cpu_finished_at));
}

void sync_player_state(object player, mapping plan)
{
    queue_player_sync(player, plan);
}

private mapping resolve_request_response_context(object player,
    mapping response_context)
{
    /* Background/read-model producers must opt in with an already frozen
     * context; never infer one from the player's mutable latest request. */
    if (undefinedp(response_context))
        return 0;
    return response_context;
}

private mapping freeze_transaction_response_context(mapping tx)
{
    mapping context;
    mixed *immediate;
    int index;

    tx = normalize_sync_transaction(tx);
    context = mapp(tx["response_context"]) ? tx["response_context"] : 0;
    if (!mapp(context) && mapp(tx["refresh"]["response_context"]))
        context = tx["refresh"]["response_context"];
    if (!mapp(context) || !stringp(context["command_request_id"])
        || context["command_request_id"] == "")
        return tx;

    context = copy(context);
    tx["response_context"] = context;
    tx["refresh"]["response_context"] = copy(context);
    immediate = tx["immediate"];
    if (arrayp(immediate))
        for (index = 0; index < sizeof(immediate); index++)
            if (mapp(immediate[index])
                && !mapp(immediate[index]["response_context"]))
                immediate[index]["response_context"] = copy(context);
    return tx;
}

int submit_transaction(object player, mapping tx)
{
    mapping current_projection;
    mapping partition;
    mixed *immediate;

    tx = freeze_transaction_response_context(tx);
    if (!can_deliver_to_target(player)) {
        /*
         * 登录交接窗口内的完整事务必须保留 refresh + immediate，不能只保留
         * room_snapshot，也不能把尚未可达误记成 drop；普通业务事务仍保持
         * 原有 drop 语义，避免断线期间重放过期的次要反馈。
         */
        tx = normalize_sync_transaction(tx);
        if (objectp(player) && player->query_temp("login/bootstrap_sync")
            && queue_pending_transaction(player, tx))
            return 1;
        if (transaction_carries_room_snapshot(tx))
            return queue_pending_room_snapshot(player, tx["refresh"]);
        record_undeliverable_delivery_drop(CLIENT_SYNC_DROP_TRANSACTION);
        record_player_transaction_drop(player);
        return 0;
    }
    tx = normalize_sync_transaction(tx);
    if (transaction_has_only_chat_messages(tx)) {
        return queue_pending_entry(player, tx["refresh"], tx["immediate"], 1);
    }
    current_projection = LOGIN_PROJECTION_D->query_projection_identity(player);
    if (mapp(tx["refresh"]["projection"])) {
        if (!LOGIN_PROJECTION_D->projection_identity_matches(player,
            tx["refresh"]["projection"])) {
            /*
             * 与 queue_pending_entry_without_schedule 同语义（统一 helper）：
             * 投影过期时刷新 plan 投影继续（REAL/房间快照为服务端现算），
             * immediate 缓存包只保 chat，不再整包丢弃 refresh。
             */
            tx["refresh"] = refresh_plan_projection_for_current(player,
                tx["refresh"]);
            partition = partition_immediate_packets(tx["immediate"]);
            tx["immediate"] = partition["chat"];
        }
    } else {
        tx["refresh"] = plan_projection(tx["refresh"], current_projection);
    }
    immediate = apply_immediate_packets(player, tx["immediate"]);
    if (sizeof(immediate) > 0 || has_refresh_work(tx["refresh"]))
        return queue_pending_entry(player, tx["refresh"], immediate,
            has_refresh_work(tx["refresh"]) ? 0 : 1);
    return 1;
}

void discard_player_pending(object player)
{
    string key;

    if (!objectp(player))
        return;
    key = file_name(player);
    if (mapp(_pending_sync_plans))
        map_delete(_pending_sync_plans, key);
    remove_pending_chat_entry(key);
    if (mapp(_pending_score_refreshes))
        map_delete(_pending_score_refreshes, key);
    if (mapp(_player_transaction_drops))
        map_delete(_player_transaction_drops, key);
    if (function_exists("reset_owner_read_model_projection", player))
        player->reset_owner_read_model_projection();
}

void discard_session_pending(string session_id)
{
    mapping entry;
    mapping state;
    mapping seen;
    string *candidate_keys;
    string key;

    key = query_session_key(session_id);
    if (key == "")
        return;

    candidate_keys = ({ key });
    if (mapp(_pending_sync_plans))
        candidate_keys += keys(_pending_sync_plans);
    if (mapp(_pending_chat_sync_plans))
        candidate_keys += keys(_pending_chat_sync_plans);
    seen = ([]);
    foreach (key in candidate_keys) {
        if (seen[key])
            continue;
        seen[key] = 1;
        entry = mapp(_pending_sync_plans[key]) ? _pending_sync_plans[key]
            : mapp(_pending_chat_sync_plans[key])
                ? _pending_chat_sync_plans[key] : 0;
        if (!pending_entry_matches_session(key, entry, session_id))
            continue;
        map_delete(_pending_sync_plans, key);
        remove_pending_chat_entry(key);
        if (mapp(entry) && objectp(entry["player"])
            && function_exists("reset_owner_read_model_projection",
                entry["player"]))
            entry["player"]->reset_owner_read_model_projection();
    }

    if (mapp(_pending_score_refreshes))
        foreach (key in keys(_pending_score_refreshes)) {
            state = _pending_score_refreshes[key];
            if (!score_refresh_entry_matches_session(key, state, session_id))
                continue;
            map_delete(_pending_score_refreshes, key);
            if (mapp(state) && objectp(state["player"])
                && function_exists("reset_owner_read_model_projection",
                    state["player"]))
                state["player"]->reset_owner_read_model_projection();
        }
    reset_score_refresh_queue_if_idle();
}

varargs void request_status(object player, mapping response_context)
{
    mapping tx;

    response_context = resolve_request_response_context(player,
        response_context);
    tx = tx_plan_status(0);
    if (mapp(response_context))
        tx = tx_with_response_context(tx, response_context);
    submit_transaction(player, tx);
}

varargs void request_status_delayed(object player, int delay,
    mapping response_context)
{
    mapping plan;

    response_context = resolve_request_response_context(player,
        response_context);
    plan = plan_status(0);
    if (mapp(response_context))
        plan = plan_with_response_context(plan, response_context);
    queue_pending_entry(player, plan, 0, delay);
}

void send_light_status_now(object player)
{
    player = resolve_sync_target(player);
    if (objectp(player) && interactive(player))
        call_other("/kernel/simul_efun", "xk_send_status_light", player);
}

void send_light_status_now_by_key(string key, mapping projection)
{
    object player;

    player = resolve_sync_target_key(key);
    if (objectp(player) && interactive(player)
        && (!mapp(projection)
            || LOGIN_PROJECTION_D->projection_identity_matches(player, projection)))
        call_other("/kernel/simul_efun", "xk_send_status_light", player);
}

void request_light_status_delayed(object player, int delay)
{
    mapping projection;
    string key;

    key = query_plan_key(player);
    if (key == "")
        return;

    projection = LOGIN_PROJECTION_D->query_projection_identity(player);
    call_out("send_light_status_now_by_key", delay < 0 ? 0 : delay, key,
        projection);
}

varargs void request_inventory(object player, mapping response_context)
{
    mapping tx;

    response_context = resolve_request_response_context(player,
        response_context);
    tx = tx_plan_inventory(0);
    if (mapp(response_context))
        tx = tx_with_response_context(tx, response_context);
    submit_transaction(player, tx);
}

varargs void request_status_inventory(object player, mapping response_context)
{
    mapping tx;

    response_context = resolve_request_response_context(player,
        response_context);
    tx = tx_plan_status_inventory(0);
    if (mapp(response_context))
        tx = tx_with_response_context(tx, response_context);
    submit_transaction(player, tx);
}

varargs void request_status_inventory_score_refresh(object player,
    mapping response_context)
{
    mapping tx;

    response_context = resolve_request_response_context(player,
        response_context);
    tx = tx_plan_status_inventory_score_refresh(0);
    if (mapp(response_context))
        tx = tx_with_response_context(tx, response_context);
    submit_transaction(player, tx);
}

varargs void request_status_room_snapshot(object player, object room,
    mapping response_context)
{
    mapping tx;

    response_context = resolve_request_response_context(player,
        response_context);
    tx = tx_plan_status_room_snapshot(0, room);
    if (mapp(response_context))
        tx = tx_with_response_context(tx, response_context);
    submit_transaction(player, tx);
}

varargs void request_room_snapshot(object player, object room,
    mapping response_context)
{
    mapping tx;

    response_context = resolve_request_response_context(player,
        response_context);
    tx = tx_plan_room_snapshot(0, room);
    if (mapp(response_context))
        tx = tx_with_response_context(tx, response_context);
    submit_transaction(player, tx);
}

varargs void request_status_inventory_room(object player, object room,
    mapping response_context)
{
    mapping tx;

    response_context = resolve_request_response_context(player,
        response_context);
    tx = tx_plan_status_inventory_room(0, room);
    if (mapp(response_context))
        tx = tx_with_response_context(tx, response_context);
    submit_transaction(player, tx);
}

varargs void request_inventory_room(object player, object room,
    mapping response_context)
{
    mapping tx;

    response_context = resolve_request_response_context(player,
        response_context);
    tx = tx_plan_inventory_room(0, room);
    if (mapp(response_context))
        tx = tx_with_response_context(tx, response_context);
    submit_transaction(player, tx);
}

void setup_client_sync_flush()
{
    if (!mapp(_pending_sync_plans))
        _pending_sync_plans = ([]);
    if (!mapp(_pending_chat_sync_plans))
        _pending_chat_sync_plans = ([]);
    reset_pending_chat_queue();
    _pending_chat_fanout_queue = ([]);
    _pending_chat_fanout_queue_head = 1;
    _pending_chat_fanout_queue_tail = 1;
    _pending_chat_fanout_targets = 0;
    reset_deferred_chat_fanout_wave();
    _chat_fanout_call_out = 0;
    _chat_fanout_generation = 0;
    if (!mapp(_pending_score_refreshes))
        _pending_score_refreshes = ([]);
    _score_refresh_queue = ([]);
    _score_refresh_queue_head = 0;
    _score_refresh_queue_tail = 0;
    _flush_call_out = 0;
    _flush_call_out_delay = 0;
    _flush_call_out_due_ns = 0;
    _flush_call_out_chat = 0;
    _flush_call_out_generation = 0;
    _score_refresh_call_out = 0;
    _score_refresh_generation = 0;
    _chat_batch_diagnostics = ([]);
}
