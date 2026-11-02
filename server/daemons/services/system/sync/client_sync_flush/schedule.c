/**
 * @file    client_sync_flush/schedule.c
 * @brief   flush 调度与 chat 车道判定
 *
 * @par Description:
 * 含 {pending_has_chat_messages, pending_normal_has_chat_messages, scheduled_chat_flush_is_due, detach_rescued_overdue_chat_flush_callout, schedule_initial_chat_sync_flush, schedule_deferred_normal_sync_flush, schedule_deferred_chat_sync_flush, schedule_sync_flush, flush_due_normal_lane_chat_entries, can_deliver_to_target, can_deliver_combat_packet_to_target}。
 * 持有 chat 车道队列与 callout 状态（_pending_chat_sync_queue 系列、_flush_call_out 系列）；chat pending 条目随 handoff 迁移（_pending_chat_sync_plans 声明在 routes），上层模块经继承直接访问。
 *
 * 本模块是 `client_sync_flush.c` 继承链的一环，由上层模块继承；
 * 跨模块调用经前向声明 + 运行时动态分派（this_object() 解析到最终对象）。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>
#include <system/xk_protocol.h>

inherit "/daemons/services/system/sync/client_sync_flush/routes";

#define CHAT_COALESCE_WALLTIME_DELAY_SECONDS 0.05
#define CLIENT_SYNC_DROP_COMBAT_PACKET 1
#define DEFERRED_FLUSH_WALLTIME_DELAY_SECONDS 0.001

nosave mapping _pending_chat_sync_queue;
nosave mapping _pending_chat_sync_queue_ticket;
nosave int _pending_chat_sync_queue_head;
nosave int _pending_chat_sync_queue_tail;
nosave int _pending_chat_sync_active_wave_tail;
nosave int _flush_call_out;
nosave int _flush_call_out_delay;
nosave int _flush_call_out_due_ns;
nosave int _flush_call_out_chat;
nosave int _flush_call_out_generation;

int can_deliver_combat_packet_to_target();
int can_deliver_to_target();
void detach_rescued_overdue_chat_flush_callout();
int entry_has_chat_messages(mapping entry);
int flush_due_normal_lane_chat_entries();
varargs void flush_pending_syncs(int flush_chat, int scheduled_callout,
    int scheduled_generation, int only_chat_entries, int suppress_reschedule);
int pending_has_chat_messages();
int pending_normal_has_chat_messages();
string query_object_session_id(object player);
void record_scheduled_chat_flush_turnover_diagnostics();
void record_undeliverable_delivery_drop();
object resolve_sync_target();
void schedule_deferred_chat_sync_flush();
void schedule_deferred_normal_sync_flush();
void schedule_initial_chat_sync_flush();
varargs void schedule_sync_flush();
int scheduled_chat_flush_is_due();

private int pending_has_chat_messages()
{
    string key;

    if (!mapp(_pending_sync_plans))
        return 0;
    foreach (key in keys(_pending_sync_plans)) {
        if (entry_has_chat_messages(_pending_sync_plans[key]))
            return 1;
    }
    if (mapp(_pending_chat_sync_plans) && sizeof(_pending_chat_sync_plans) > 0)
        return 1;
    return 0;
}

private int pending_normal_has_chat_messages()
{
    string key;

    if (!mapp(_pending_sync_plans))
        return 0;
    foreach (key in keys(_pending_sync_plans)) {
        if (entry_has_chat_messages(_pending_sync_plans[key]))
            return 1;
    }
    return 0;
}

int scheduled_chat_flush_is_due()
{
    return _flush_call_out && _flush_call_out_chat && _flush_call_out_due_ns > 0
        && perf_counter_ns() >= _flush_call_out_due_ns;
}

void detach_rescued_overdue_chat_flush_callout()
{
    if (!scheduled_chat_flush_is_due())
        return;

    _flush_call_out = 0;
    _flush_call_out_delay = 0;
    _flush_call_out_due_ns = 0;
    _flush_call_out_chat = 0;
    _flush_call_out_generation++;
    if (mapp(_pending_chat_sync_plans) && sizeof(_pending_chat_sync_plans) > 0)
        schedule_deferred_chat_sync_flush();
}

void schedule_initial_chat_sync_flush()
{
    if (_flush_call_out) {
        if (_flush_call_out_chat || _flush_call_out_delay == 0)
            return;
        if (_flush_call_out_due_ns > 0) {
            if (remove_call_out(_flush_call_out) != -1)
                record_scheduled_chat_flush_turnover_diagnostics("cancelled");
        } else
            remove_call_out(_flush_call_out);
    }

    _flush_call_out_generation++;
    _flush_call_out = call_out_walltime_gateway("flush_pending_syncs",
        CHAT_COALESCE_WALLTIME_DELAY_SECONDS, 1, 1,
        _flush_call_out_generation);
    /* Positive means a delayed CHAT callout for the existing priority rules;
     * the exact deadline is stored separately at nanosecond precision. */
    _flush_call_out_delay = 1;
    _flush_call_out_due_ns = perf_counter_ns()
        + to_int(CHAT_COALESCE_WALLTIME_DELAY_SECONDS * 1000000000.0);
    _flush_call_out_chat = 1;
    record_scheduled_chat_flush_turnover_diagnostics("scheduled");
}

void schedule_deferred_normal_sync_flush()
{
    if (_flush_call_out)
        return;

    _flush_call_out_generation++;
    _flush_call_out = call_out_walltime("flush_pending_syncs", DEFERRED_FLUSH_WALLTIME_DELAY_SECONDS, 0, 1,
        _flush_call_out_generation);
    _flush_call_out_delay = 0;
    _flush_call_out_due_ns = 0;
    _flush_call_out_chat = 0;
}

void schedule_deferred_chat_sync_flush()
{
    if (_flush_call_out)
        return;

    _flush_call_out_generation++;
    _flush_call_out = call_out_walltime_gateway("flush_pending_syncs", DEFERRED_FLUSH_WALLTIME_DELAY_SECONDS, 1, 1,
        _flush_call_out_generation);
    _flush_call_out_delay = 0;
    _flush_call_out_due_ns = 0;
    _flush_call_out_chat = 1;
    record_scheduled_chat_flush_turnover_diagnostics("continued");
}

varargs void schedule_sync_flush(int delay, int flush_chat)
{
    delay = delay < 0 ? 0 : delay;
    flush_chat = flush_chat ? 1 : 0;
    if (_flush_call_out && _flush_call_out_chat && _flush_call_out_delay > 0 && delay == 0
        && pending_has_chat_messages())
        return;
    if (_flush_call_out && delay > _flush_call_out_delay)
        return;
    if (_flush_call_out && delay == _flush_call_out_delay
        && flush_chat == _flush_call_out_chat)
        return;
    if (_flush_call_out && delay == _flush_call_out_delay
        && !flush_chat && _flush_call_out_chat)
        return;
    if (_flush_call_out) {
        if (_flush_call_out_due_ns > 0) {
            if (remove_call_out(_flush_call_out) != -1)
                record_scheduled_chat_flush_turnover_diagnostics("cancelled");
        }
        else
            remove_call_out(_flush_call_out);
    }
    _flush_call_out_generation++;
    if (flush_chat && delay > 0)
        _flush_call_out = call_out_walltime_gateway("flush_pending_syncs", delay,
            flush_chat, 1, _flush_call_out_generation);
    else
        _flush_call_out = call_out("flush_pending_syncs", delay, flush_chat, 1,
            _flush_call_out_generation);
    _flush_call_out_delay = delay;
    _flush_call_out_due_ns = perf_counter_ns() + delay * 1000000000;
    _flush_call_out_chat = flush_chat;
    if (delay > 0)
        record_scheduled_chat_flush_turnover_diagnostics("scheduled");
}

int flush_due_normal_lane_chat_entries()
{
    if (!pending_normal_has_chat_messages())
        return 0;

    /* Preserve each mixed entry's state-before-chat FIFO without draining
     * unrelated normal-only players in the scheduled chat callback.  Move the
     * now-due CHAT residual onto the chat lane, but never consume another
     * recipient slice in this stack.  The caller rearms the deferred
     * walltime continuation after it restores any untouched chat backlog. */
    flush_pending_syncs(0, 0, 0, 1, 1);
    return mapp(_pending_chat_sync_plans) && sizeof(_pending_chat_sync_plans) > 0;
}

int can_deliver_to_target(object player)
{
    player = resolve_sync_target(player);
    if (!objectp(player))
        return 0;
    if (interactive(player))
        return 1;
    /*
     * 与 xk_send_report 同 gate（interactive || has-session）：非 interactive 但
     * 有 gateway session 即允许投递（gateway 转接模式）。原 bridge 分支在 driver
     * 模式下被 config_game 强制关闭（死代码），删除后语义统一；顶号/断开窗口
     * （session 也空）由 submit_transaction 入队重试兜底。
     */
    return query_object_session_id(player) != "";
}

int can_deliver_combat_packet_to_target(object player)
{
    if (can_deliver_to_target(player))
        return 1;
    record_undeliverable_delivery_drop(CLIENT_SYNC_DROP_COMBAT_PACKET);
    return 0;
}
