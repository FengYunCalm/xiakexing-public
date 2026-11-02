/**
 * @file    measurement_snapshot_d.c
 * @brief   ownercheck measure 全量字段快照发射器（S6 自 cmds/admin/ownercheck.c 迁出）
 *
 * 职责：组装并发射 [ownercheck measure] 快照（login_before/after 与
 * command_loop_before/after 两分支），含各域 status_text 渲染器。
 * 字段名真源分层：combat/family 等迭代 telemetry_schema.h 宏；
 * 域诊断单点键（如 quest_read_model_*）允许经带注释的手写 sprintf
 * 进入，字段名真源归该域守护的 query 面——新增时必须挂对应域查询面，
 * 不得在本文件另立第二份 schema。
 * 发射通道契约：字段体压平为单行后必须以换行终止再走 msg_send_raw，
 * 否则 gateway bufio.Scanner 永不吐帧（send_raw_flattened 统一保证）。
 */

#pragma strict_types

#include <system/globals.h>
#include <system/services.h>
#define COMBAT_MATH_HELPER "/daemons/services/gameplay/combat/combat_math"
#include <system/telemetry_schema.h>


inherit "/lib/framework/daemon";

// 测量发射的调用方白名单（策略单点：新增管理命令时在此扩展，不改守卫逻辑）
nosave string *EMIT_CALLER_WHITELIST = ({
    "/cmds/admin/ownercheck",
});

// ===== 前向声明（strict_types 先声明后用；公开原语以 public 声明） =====
private int mapping_int(mapping source, string key);
private int can_use_ownercheck(object me);
public void gateway_probe_suppress_once(object target);
private string owner_projection_status_text(mapping status);
private string owner_projection_timing_status_text(mapping status);
private string login_timing_status_text(mapping status);
private string xk_protocol_phase_timing_status_text(mapping status);
private mapping query_xk_protocol_observability_status();
private string xk_protocol_type_encode_status_text(mapping status);
private string command_family_timing_status_text(mapping status);
private string trade_consume_command_timing_status_text(mapping status);
private string movement_phase_timing_status_text(mapping status);
private string combat_phase_timing_status_text(mapping status);
private string progression_phase_timing_status_text(mapping status);
private string generic_future_watch_status_text(mapping status);
private string owner_protocol_output_status_text(mapping status);
private string session_future_watch_status_text(mapping status);
private string room_output_projection_status_text(mapping status);
private string owner_async_stage_timing_status_text(mapping owner_status,
    mapping gateway_status);
private string look_phase_tail_status_text(mapping status);
private string append_int_status_fields(string text, mapping status,
    string *fields);
private string heartbeat_runtime_status_text(mapping status);
private string client_sync_chat_batch_status_text(mapping status);
private string room_message_event_batch_status_text(mapping status);
void emit_command_loop_measurement_snapshot(object me, object adapter, string phase);

// ===== 自 ownercheck.c 逐字迁出（含共享助手私有副本） =====

private int can_use_ownercheck(object me)
{
    return objectp(me) && (wizardp(me) || CONFIG_D->is_admin_player(me));
}

private int mapping_int(mapping source, string key)
{
    return mapp(source) && intp(source[key]) ? source[key] : 0;
}

/** 探测抑制的延迟清除（call_out 字符串名在本对象内解析；公开供命令面即时解除复用）。 */
public void clear_xk_protocol_probe_suppression(object target)
{
    if (objectp(target))
        target->delete_temp("protocol/xk_probe_suppressed");
}

/** 探测抑制原语（temp 键 protocol/xk_probe_suppressed 的单一属主）。
 * 公开供 ownercheck modecheck 等命令面复用；键名不得在他处手写。 */
public void gateway_probe_suppress_once(object target)
{
    if (!objectp(target))
        return;
    target->set_temp("protocol/xk_probe_suppressed", 1);
    call_out("clear_xk_protocol_probe_suppression", 0, target);
}

private string owner_projection_status_text(mapping status)
{
    string result;
    string projection_kind;
    string outcome;
    string field_name;

    result = "";
    foreach (projection_kind in ({ "inventory", "hp_dialog", "score_dialog", "map_grid" })) {
        foreach (outcome in ({ "owner_completed", "sync_fallback", "owner_stale",
            "owner_failed", "owner_cancelled", "current_hit", "pending_cached",
            "preencoded_hit", "preencoded_sync_fallback", "preencoded_failed" })) {
            field_name = "owner_projection_" + projection_kind + "_"
                + outcome + "_count";
            result += sprintf("%s=%d\n", field_name,
                mapping_int(status, field_name));
        }
    }
    result += sprintf("owner_projection_inventory_inline_completed_count=%d\n",
        mapping_int(status, "owner_projection_inventory_inline_completed_count"));
    foreach (outcome in ({ "checked", "rejected", "committed" })) {
        field_name = "owner_projection_publish_guard_" + outcome + "_count";
        result += sprintf("%s=%d\n", field_name,
            mapping_int(status, field_name));
    }
    return result;
}

private string owner_projection_timing_status_text(mapping status)
{
    string result;
    string projection_kind;
    string phase;
    string prefix;

    result = "";
    foreach (projection_kind in ({ "inventory", "hp_dialog", "score_dialog", "map_grid" })) {
        foreach (phase in ({ "snapshot", "submit", "publish", "current_hit",
            "sync_fallback", "preencoded_send", "refresh_total",
            "snapshot_facts", "snapshot_actor", "live_token" })) {
            prefix = "owner_projection_" + projection_kind + "_" + phase;
            result += sprintf("%s_samples=%d\n%s_total_us=%d\n%s_max_us=%d\n",
                prefix, mapping_int(status, prefix + "_samples"),
                prefix, mapping_int(status, prefix + "_total_us"),
                prefix, mapping_int(status, prefix + "_max_us"));
        }
    }
    return result;
}

private string login_timing_status_text(mapping status)
{
    string result;
    string stage;
    string prefix;

    result = sprintf("login_timing_enabled=%d\nlogin_timing_snapshot_age_us=%d\n",
        mapping_int(status, "login_timing_enabled"),
        mapping_int(status, "login_timing_snapshot_age_us"));
    foreach (stage in ({
        "credential_auth",
        "account_existence",
        "account_read",
        "account_hash",
        "account_serialize",
        "account_tmp_write",
        "account_rename",
        "account_backup_cleanup",
        "account_prepare",
        "character_create",
        "character_save",
        "character_persist",
        "account_final_commit",
        "registration_rollback",
        "character_setup_move",
        "handoff",
        "handoff_connection",
        "handoff_session",
        "handoff_bootstrap",
        "handoff_navigation_emit",
        "handoff_shared_room_refresh",
        "handoff_private_projection",
        "handoff_fifo_fill",
        "handoff_sync_plan",
        "handoff_sync_flush",
        "handoff_success_barrier",
        "post_handoff_prime",
    })) {
        prefix = "login_timing_" + stage + "_";
        result += sprintf("%swall_samples=%d\n%swall_total_us=%d\n%swall_max_us=%d\n"
            "%sthread_cpu_samples=%d\n%sthread_cpu_total_us=%d\n"
            "%sthread_cpu_max_us=%d\n%sthread_cpu_unavailable=%d\n",
            prefix, mapping_int(status, prefix + "wall_samples"),
            prefix, mapping_int(status, prefix + "wall_total_us"),
            prefix, mapping_int(status, prefix + "wall_max_us"),
            prefix, mapping_int(status, prefix + "thread_cpu_samples"),
            prefix, mapping_int(status, prefix + "thread_cpu_total_us"),
            prefix, mapping_int(status, prefix + "thread_cpu_max_us"),
            prefix, mapping_int(status, prefix + "thread_cpu_unavailable"));
        prefix = "login_phase_outcome_" + stage + "_";
        result += sprintf("%ssuccess=%d\n%sfailure=%d\n",
            prefix, mapping_int(status, prefix + "success"),
            prefix, mapping_int(status, prefix + "failure"));
    }
    return result;
}

private string xk_protocol_phase_timing_status_text(mapping status)
{
    string result;
    string phase;
    string prefix;

    result = "";
    foreach (phase in ({ "metadata", "json", "encode", "encode_cpu", "frame", "frame_cpu",
        "session_trace_cpu" })) {
        prefix = "xk_protocol_" + phase;
        result += sprintf("%s_samples=%d\n%s_total_us=%d\n%s_max_us=%d\n",
            prefix, mapping_int(status, prefix + "_samples"),
            prefix, mapping_int(status, prefix + "_total_us"),
            prefix, mapping_int(status, prefix + "_max_us"));
    }
    result += sprintf("xk_protocol_encode_cpu_unavailable_count=%d\n",
        mapping_int(status, "xk_protocol_encode_cpu_unavailable_count"));
    result += sprintf("xk_protocol_frame_cpu_unavailable_count=%d\n",
        mapping_int(status, "xk_protocol_frame_cpu_unavailable_count"));
    result += sprintf("xk_protocol_session_trace_cpu_unavailable_count=%d\n",
        mapping_int(status, "xk_protocol_session_trace_cpu_unavailable_count"));
    result += sprintf("xk_protocol_json_native_count=%d\n",
        mapping_int(status, "xk_protocol_json_native_count"));
    result += sprintf("xk_protocol_json_legacy_fallback_count=%d\n",
        mapping_int(status, "xk_protocol_json_legacy_fallback_count"));
    return result;
}

private mapping query_xk_protocol_observability_status()
{
    mapping encoder_status;
    mapping status;
    mixed err;

    err = catch(status = GATEWAY_SESSION_D->query_xk_protocol_phase_timing_status());
    if (err || !mapp(status))
        return 0;
    err = catch(encoder_status = XK_PROTOCOL_CORE->query_xk_protocol_json_encoder_status());
    if (!err && mapp(encoder_status))
        status += encoder_status;
    return status;
}

private string xk_protocol_type_encode_status_text(mapping status)
{
    string result;
    string protocol_type;
    string prefix;

    result = "";
    foreach (protocol_type in ({ "bach", "maps", "dlog", "ivty", "skls" })) {
        prefix = "xk_protocol_" + protocol_type + "_encode_cpu";
        result += sprintf("%s_samples=%d\n%s_total_us=%d\n%s_max_us=%d\n",
            prefix, mapping_int(status, prefix + "_samples"),
            prefix, mapping_int(status, prefix + "_total_us"),
            prefix, mapping_int(status, prefix + "_max_us"));
        result += sprintf("xk_protocol_%s_payload_bytes_total=%d\n"
            "xk_protocol_%s_payload_bytes_max=%d\n",
            protocol_type,
            mapping_int(status, "xk_protocol_" + protocol_type + "_payload_bytes_total"),
            protocol_type,
            mapping_int(status, "xk_protocol_" + protocol_type + "_payload_bytes_max"));
    }
    return result;
}

private string command_family_timing_status_text(mapping status)
{
    string result;
    string family;
    string field;
    string prefix;

    result = "";
    foreach (family in ({
        "look", "inventory", "skills", "hp", "map_xk", "score",
        "movement", "combat", "trade_consume", "progression", "chat",
        "equipment", "other"
    })) {
        prefix = "gateway_command_family_" + family + "_";
        foreach (field in GATEWAY_TIMING_FIELD_SUFFIXES)
            result += sprintf("%s%s=%d\n", prefix, field,
                mapping_int(status, prefix + field));
    }
    return result;
}

private string trade_consume_command_timing_status_text(mapping status)
{
    string result;
    string command;
    string field;
    string prefix;

    result = "";
    foreach (command in ({ "buy", "eat" })) {
        prefix = "gateway_trade_consume_command_" + command + "_";
        foreach (field in GATEWAY_TIMING_FIELD_SUFFIXES)
            result += sprintf("%s%s=%d\n", prefix, field,
                mapping_int(status, prefix + field));
    }
    return result;
}

private string movement_phase_timing_status_text(mapping status)
{
    string result;
    string phase;
    string field;
    string prefix;

    result = "";
    foreach (phase in ({
        "framework_total", "completion_navigation", "navigation_meta_query",
        "navigation_payload_build", "navigation_batch_send", "exits_name_resolve",
        "exits_quest_marker", "exits_indoor_scan", "exits_payload_assembly", "completion_entities",
        "completion_world", "completion_quest_escort", "completion_quest_arrive",
        "completion_dungeon", "completion_followers",
        "completion_total"
    })) {
        prefix = "gateway_movement_phase_" + phase + "_";
        foreach (field in GATEWAY_TIMING_FIELD_SUFFIXES)
            result += sprintf("%s%s=%d\n", prefix, field,
                mapping_int(status, prefix + field));
    }
    return result;
}

private string combat_phase_timing_status_text(mapping status)
{
    string result;
    string phase;
    string field;
    string prefix;

    result = "";
    foreach (phase in GATEWAY_COMBAT_PHASES) {
        prefix = "gateway_combat_phase_" + phase + "_";
        foreach (field in GATEWAY_TIMING_FIELD_SUFFIXES)
            result += sprintf("%s%s=%d\n", prefix, field,
                mapping_int(status, prefix + field));
    }
    return result;
}

private string progression_phase_timing_status_text(mapping status)
{
    string result;
    string phase;
    string field;
    string prefix;

    result = "";
    foreach (phase in ({
        "learn_teacher_lookup", "learn_levels", "learn_message_event",
        "learn_quest_progress", "learn_skill_list"
    })) {
        prefix = "gateway_progression_phase_" + phase + "_";
        foreach (field in GATEWAY_TIMING_FIELD_SUFFIXES)
            result += sprintf("%s%s=%d\n", prefix, field,
                mapping_int(status, prefix + field));
    }
    return result;
}

private string generic_future_watch_status_text(mapping status)
{
    return sprintf("gateway_generic_future_watch_pending=%d\ngateway_generic_future_watches_registered=%d\ngateway_generic_future_watches_rejected=%d\ngateway_generic_future_watches_completed=%d\ngateway_generic_future_watches_failed=%d\ngateway_generic_future_watches_timed_out=%d\ngateway_generic_future_watches_cancelled=%d\ngateway_generic_future_watch_callbacks=%d\ngateway_generic_future_watch_callback_failures=%d\n",
        mapping_int(status, "gateway_generic_future_watch_pending"),
        mapping_int(status, "gateway_generic_future_watches_registered"),
        mapping_int(status, "gateway_generic_future_watches_rejected"),
        mapping_int(status, "gateway_generic_future_watches_completed"),
        mapping_int(status, "gateway_generic_future_watches_failed"),
        mapping_int(status, "gateway_generic_future_watches_timed_out"),
        mapping_int(status, "gateway_generic_future_watches_cancelled"),
        mapping_int(status, "gateway_generic_future_watch_callbacks"),
        mapping_int(status, "gateway_generic_future_watch_callback_failures"));
}

private string owner_protocol_output_status_text(mapping status)
{
    return sprintf("owner_protocol_output_pending=%d\nowner_protocol_output_fifo_violations=%d\nowner_protocol_output_submitted_count=%d\nowner_protocol_output_reserve_failed_count=%d\nowner_protocol_output_submit_failed_count=%d\nowner_protocol_output_watch_failed_count=%d\nowner_protocol_output_sync_fallback_count=%d\nowner_protocol_output_completed_count=%d\nowner_protocol_output_released_count=%d\n",
        mapping_int(status, "owner_protocol_output_pending"),
        mapping_int(status, "owner_protocol_output_fifo_violations"),
        mapping_int(status, "owner_protocol_output_submitted_count"),
        mapping_int(status, "owner_protocol_output_reserve_failed_count"),
        mapping_int(status, "owner_protocol_output_submit_failed_count"),
        mapping_int(status, "owner_protocol_output_watch_failed_count"),
        mapping_int(status, "owner_protocol_output_sync_fallback_count"),
        mapping_int(status, "owner_protocol_output_completed_count"),
        mapping_int(status, "owner_protocol_output_released_count"));
}

private string session_future_watch_status_text(mapping status)
{
    return sprintf("gateway_future_watch_pending=%d\ngateway_future_watches_registered=%d\ngateway_future_watches_rejected=%d\ngateway_future_watches_completed=%d\ngateway_future_watches_failed=%d\ngateway_future_watches_timed_out=%d\ngateway_future_watches_cancelled=%d\ngateway_future_watch_callbacks=%d\ngateway_future_watch_callback_failures=%d\n",
        mapping_int(status, "gateway_future_watch_pending"),
        mapping_int(status, "gateway_future_watches_registered"),
        mapping_int(status, "gateway_future_watches_rejected"),
        mapping_int(status, "gateway_future_watches_completed"),
        mapping_int(status, "gateway_future_watches_failed"),
        mapping_int(status, "gateway_future_watches_timed_out"),
        mapping_int(status, "gateway_future_watches_cancelled"),
        mapping_int(status, "gateway_future_watch_callbacks"),
        mapping_int(status, "gateway_future_watch_callback_failures"));
}

private string room_output_projection_status_text(mapping status)
{
    return append_int_status_fields("", status, ({
        "gateway_room_output_projection_snapshot_samples",
        "gateway_room_output_projection_snapshot_total_us",
        "gateway_room_output_projection_snapshot_max_us",
        "gateway_room_output_projection_submit_watch_samples",
        "gateway_room_output_projection_submit_watch_total_us",
        "gateway_room_output_projection_submit_watch_max_us",
        "gateway_room_output_projection_worker_samples",
        "gateway_room_output_projection_worker_total_us",
        "gateway_room_output_projection_worker_max_us",
        "gateway_room_output_projection_worker_thread_cpu_samples",
        "gateway_room_output_projection_worker_thread_cpu_total_us",
        "gateway_room_output_projection_worker_thread_cpu_max_us",
        "gateway_room_output_projection_worker_thread_cpu_unavailable",
        "gateway_room_output_projection_inline_thread_cpu_samples",
        "gateway_room_output_projection_inline_thread_cpu_total_us",
        "gateway_room_output_projection_inline_thread_cpu_max_us",
        "gateway_room_output_projection_inline_thread_cpu_unavailable",
        "gateway_room_output_projection_publish_samples",
        "gateway_room_output_projection_publish_total_us",
        "gateway_room_output_projection_publish_max_us",
        "gateway_room_output_projection_submitted",
        "gateway_room_output_projection_completed",
        "gateway_room_output_projection_released",
        "gateway_room_output_projection_pending",
        "gateway_room_output_projection_inline_fallbacks",
        "gateway_room_output_projection_failed",
        "gateway_room_output_projection_forced_cleanup",
    }));
}

private string owner_async_stage_timing_status_text(mapping owner_status,
    mapping gateway_status)
{
    return sprintf("owner_async_queue_wait_samples=%d\nowner_async_queue_wait_total_us=%d\nowner_async_queue_wait_max_us=%d\nowner_async_lpc_execute_samples=%d\nowner_async_lpc_execute_total_us=%d\nowner_async_lpc_execute_max_us=%d\nowner_async_lpc_execute_thread_cpu_total_us=%d\nowner_async_lpc_execute_thread_cpu_unavailable=%d\nowner_async_result_completion_samples=%d\nowner_async_result_completion_total_us=%d\nowner_async_result_completion_max_us=%d\nowner_async_result_completion_thread_cpu_total_us=%d\nowner_async_result_completion_thread_cpu_unavailable=%d\ngateway_output_reserve_samples=%d\ngateway_output_reserve_total_us=%d\ngateway_output_reserve_max_us=%d\ngateway_future_watch_register_samples=%d\ngateway_future_watch_register_total_us=%d\ngateway_future_watch_register_max_us=%d\ngateway_future_watch_terminal_lag_samples=%d\ngateway_future_watch_terminal_lag_total_us=%d\ngateway_future_watch_terminal_lag_max_us=%d\ngateway_future_watch_take_samples=%d\ngateway_future_watch_take_total_us=%d\ngateway_future_watch_take_max_us=%d\ngateway_future_watch_callback_samples=%d\ngateway_future_watch_callback_total_us=%d\ngateway_future_watch_callback_max_us=%d\ngateway_future_watch_end_to_end_samples=%d\ngateway_future_watch_end_to_end_total_us=%d\ngateway_future_watch_end_to_end_max_us=%d\ngateway_future_watch_main_completion_thread_cpu_total_us=%d\ngateway_future_watch_main_completion_thread_cpu_unavailable=%d\ngateway_future_watch_completion_event_ready=%d\ngateway_future_watch_completion_notifications=%d\ngateway_future_watch_completion_wakeups=%d\ngateway_future_watch_timer_wakeups=%d\n",
        mapping_int(owner_status, "owner_async_queue_wait_samples"),
        mapping_int(owner_status, "owner_async_queue_wait_total_us"),
        mapping_int(owner_status, "owner_async_queue_wait_max_us"),
        mapping_int(owner_status, "owner_async_lpc_execute_samples"),
        mapping_int(owner_status, "owner_async_lpc_execute_total_us"),
        mapping_int(owner_status, "owner_async_lpc_execute_max_us"),
        mapping_int(owner_status, "owner_async_lpc_execute_thread_cpu_total_us"),
        mapping_int(owner_status, "owner_async_lpc_execute_thread_cpu_unavailable"),
        mapping_int(owner_status, "owner_async_result_completion_samples"),
        mapping_int(owner_status, "owner_async_result_completion_total_us"),
        mapping_int(owner_status, "owner_async_result_completion_max_us"),
        mapping_int(owner_status, "owner_async_result_completion_thread_cpu_total_us"),
        mapping_int(owner_status, "owner_async_result_completion_thread_cpu_unavailable"),
        mapping_int(gateway_status, "gateway_output_reserve_samples"),
        mapping_int(gateway_status, "gateway_output_reserve_total_us"),
        mapping_int(gateway_status, "gateway_output_reserve_max_us"),
        mapping_int(gateway_status, "gateway_future_watch_register_samples"),
        mapping_int(gateway_status, "gateway_future_watch_register_total_us"),
        mapping_int(gateway_status, "gateway_future_watch_register_max_us"),
        mapping_int(gateway_status, "gateway_future_watch_terminal_lag_samples"),
        mapping_int(gateway_status, "gateway_future_watch_terminal_lag_total_us"),
        mapping_int(gateway_status, "gateway_future_watch_terminal_lag_max_us"),
        mapping_int(gateway_status, "gateway_future_watch_take_samples"),
        mapping_int(gateway_status, "gateway_future_watch_take_total_us"),
        mapping_int(gateway_status, "gateway_future_watch_take_max_us"),
        mapping_int(gateway_status, "gateway_future_watch_callback_samples"),
        mapping_int(gateway_status, "gateway_future_watch_callback_total_us"),
        mapping_int(gateway_status, "gateway_future_watch_callback_max_us"),
        mapping_int(gateway_status, "gateway_future_watch_end_to_end_samples"),
        mapping_int(gateway_status, "gateway_future_watch_end_to_end_total_us"),
        mapping_int(gateway_status, "gateway_future_watch_end_to_end_max_us"),
        mapping_int(gateway_status, "gateway_future_watch_main_completion_thread_cpu_total_us"),
        mapping_int(gateway_status, "gateway_future_watch_main_completion_thread_cpu_unavailable"),
        mapping_int(gateway_status, "gateway_future_watch_completion_event_ready"),
        mapping_int(gateway_status, "gateway_future_watch_completion_notifications"),
        mapping_int(gateway_status, "gateway_future_watch_completion_wakeups"),
        mapping_int(gateway_status, "gateway_future_watch_timer_wakeups"));
}

private string look_phase_tail_status_text(mapping status)
{
    return sprintf("look_phase_tail_samples=%d\nlook_phase_tail_total_us=%d\nlook_phase_tail_max_us=%d\nlook_phase_room_view_tail_samples=%d\nlook_phase_room_view_tail_total_us=%d\nlook_phase_room_view_tail_max_us=%d\nlook_phase_room_view_ensure_tail_samples=%d\nlook_phase_room_view_ensure_tail_total_us=%d\nlook_phase_room_view_ensure_tail_max_us=%d\nlook_phase_room_view_shared_tail_samples=%d\nlook_phase_room_view_shared_tail_total_us=%d\nlook_phase_room_view_shared_tail_max_us=%d\nlook_phase_room_view_player_filter_tail_samples=%d\nlook_phase_room_view_player_filter_tail_total_us=%d\nlook_phase_room_view_player_filter_tail_max_us=%d\nlook_phase_room_view_quest_projection_tail_samples=%d\nlook_phase_room_view_quest_projection_tail_total_us=%d\nlook_phase_room_view_quest_projection_tail_max_us=%d\nlook_phase_room_view_finalize_tail_samples=%d\nlook_phase_room_view_finalize_tail_total_us=%d\nlook_phase_room_view_finalize_tail_max_us=%d\nlook_phase_read_summary_tail_samples=%d\nlook_phase_read_summary_tail_total_us=%d\nlook_phase_read_summary_tail_max_us=%d\nlook_phase_render_tail_samples=%d\nlook_phase_render_tail_total_us=%d\nlook_phase_render_tail_max_us=%d\nlook_phase_emit_tail_samples=%d\nlook_phase_emit_tail_total_us=%d\nlook_phase_emit_tail_max_us=%d\n",
        mapping_int(status, "look_phase_tail_samples"),
        mapping_int(status, "look_phase_tail_total_us"),
        mapping_int(status, "look_phase_tail_max_us"),
        mapping_int(status, "look_phase_room_view_tail_samples"),
        mapping_int(status, "look_phase_room_view_tail_total_us"),
        mapping_int(status, "look_phase_room_view_tail_max_us"),
        mapping_int(status, "look_phase_room_view_ensure_tail_samples"),
        mapping_int(status, "look_phase_room_view_ensure_tail_total_us"),
        mapping_int(status, "look_phase_room_view_ensure_tail_max_us"),
        mapping_int(status, "look_phase_room_view_shared_tail_samples"),
        mapping_int(status, "look_phase_room_view_shared_tail_total_us"),
        mapping_int(status, "look_phase_room_view_shared_tail_max_us"),
        mapping_int(status, "look_phase_room_view_player_filter_tail_samples"),
        mapping_int(status, "look_phase_room_view_player_filter_tail_total_us"),
        mapping_int(status, "look_phase_room_view_player_filter_tail_max_us"),
        mapping_int(status, "look_phase_room_view_quest_projection_tail_samples"),
        mapping_int(status, "look_phase_room_view_quest_projection_tail_total_us"),
        mapping_int(status, "look_phase_room_view_quest_projection_tail_max_us"),
        mapping_int(status, "look_phase_room_view_finalize_tail_samples"),
        mapping_int(status, "look_phase_room_view_finalize_tail_total_us"),
        mapping_int(status, "look_phase_room_view_finalize_tail_max_us"),
        mapping_int(status, "look_phase_read_summary_tail_samples"),
        mapping_int(status, "look_phase_read_summary_tail_total_us"),
        mapping_int(status, "look_phase_read_summary_tail_max_us"),
        mapping_int(status, "look_phase_render_tail_samples"),
        mapping_int(status, "look_phase_render_tail_total_us"),
        mapping_int(status, "look_phase_render_tail_max_us"),
        mapping_int(status, "look_phase_emit_tail_samples"),
        mapping_int(status, "look_phase_emit_tail_total_us"),
        mapping_int(status, "look_phase_emit_tail_max_us"))
        + sprintf("look_phase_room_view_ensure_owner_route_tail_samples=%d\nlook_phase_room_view_ensure_owner_route_tail_total_us=%d\nlook_phase_room_view_ensure_owner_route_tail_max_us=%d\nlook_phase_room_view_ensure_gate_tail_samples=%d\nlook_phase_room_view_ensure_gate_tail_total_us=%d\nlook_phase_room_view_ensure_gate_tail_max_us=%d\nlook_phase_room_view_ensure_entities_tail_samples=%d\nlook_phase_room_view_ensure_entities_tail_total_us=%d\nlook_phase_room_view_ensure_entities_tail_max_us=%d\n",
        mapping_int(status, "look_phase_room_view_ensure_owner_route_tail_samples"),
        mapping_int(status, "look_phase_room_view_ensure_owner_route_tail_total_us"),
        mapping_int(status, "look_phase_room_view_ensure_owner_route_tail_max_us"),
        mapping_int(status, "look_phase_room_view_ensure_gate_tail_samples"),
        mapping_int(status, "look_phase_room_view_ensure_gate_tail_total_us"),
        mapping_int(status, "look_phase_room_view_ensure_gate_tail_max_us"),
        mapping_int(status, "look_phase_room_view_ensure_entities_tail_samples"),
        mapping_int(status, "look_phase_room_view_ensure_entities_tail_total_us"),
        mapping_int(status, "look_phase_room_view_ensure_entities_tail_max_us"));
}

private string append_int_status_fields(string text, mapping status, string *fields)
{
    string *chunks;
    int i;

    chunks = allocate(sizeof(fields) + 1);
    chunks[0] = stringp(text) ? text : "";
    for (i = 0; i < sizeof(fields); i++)
        chunks[i + 1] = sprintf("%s=%d\n", fields[i],
            mapping_int(status, fields[i]));
    return implode(chunks, "");
}

private string heartbeat_runtime_status_text(mapping status)
{
    mapping output_status;

    output_status = ([
        "heartbeat_slice_budget": mapping_int(status, "slice_budget"),
        "heartbeat_slice_wall_budget_us": mapping_int(status, "slice_wall_budget_us"),
        "heartbeat_continuation_delay_us": mapping_int(status, "continuation_delay_us"),
        "heartbeat_slice_runs": mapping_int(status, "slice_runs"),
        "heartbeat_slice_handlers_total": mapping_int(status, "slice_handlers_total"),
        "heartbeat_slice_handlers_max": mapping_int(status, "slice_handlers_max"),
        "heartbeat_slice_wall_total_us": mapping_int(status, "slice_wall_total_us"),
        "heartbeat_slice_wall_max_us": mapping_int(status, "slice_wall_max_us"),
        "heartbeat_slice_budget_yields": mapping_int(status, "slice_budget_yields"),
        "heartbeat_slice_wall_yields": mapping_int(status, "slice_wall_yields"),
        "heartbeat_continuations_scheduled": mapping_int(status, "continuations_scheduled"),
        "heartbeat_continuations_executed": mapping_int(status, "continuations_executed"),
        "heartbeat_due_snapshot_size": mapping_int(status, "due_snapshot_size"),
        "heartbeat_due_snapshot_cursor": mapping_int(status, "due_snapshot_cursor"),
        "heartbeat_due_snapshot_pending": mapping_int(status, "due_snapshot_pending"),
    ]);
    return append_int_status_fields("", output_status, ({
        "heartbeat_slice_budget",
        "heartbeat_slice_wall_budget_us",
        "heartbeat_continuation_delay_us",
        "heartbeat_slice_runs",
        "heartbeat_slice_handlers_total",
        "heartbeat_slice_handlers_max",
        "heartbeat_slice_wall_total_us",
        "heartbeat_slice_wall_max_us",
        "heartbeat_slice_budget_yields",
        "heartbeat_slice_wall_yields",
        "heartbeat_continuations_scheduled",
        "heartbeat_continuations_executed",
        "heartbeat_due_snapshot_size",
        "heartbeat_due_snapshot_cursor",
        "heartbeat_due_snapshot_pending",
    }));
}

private string client_sync_chat_batch_status_text(mapping status)
{
    mapping output_status;

    output_status = (mapp(status) ? status : ([])) + ([
        "client_sync_pending_entries": mapping_int(status, "pending_entries"),
    ]);
    return append_int_status_fields("", output_status, ({
        "client_sync_pending_entries",
        "client_sync_undeliverable_combat_packet_dropped",
        "client_sync_undeliverable_transaction_dropped",
        "client_sync_undeliverable_pending_dropped",
        "client_sync_chat_batch_samples",
        "client_sync_chat_batch_messages_total",
        "client_sync_chat_batch_messages_max",
        "client_sync_chat_batch_preencoded_success",
        "client_sync_chat_batch_preencoded_fallback",
        "client_sync_chat_batch_build_wall_samples",
        "client_sync_chat_batch_build_wall_total_us",
        "client_sync_chat_batch_build_wall_max_us",
        "client_sync_chat_batch_build_cpu_samples",
        "client_sync_chat_batch_build_cpu_total_us",
        "client_sync_chat_batch_build_cpu_max_us",
        "client_sync_chat_batch_build_cpu_unavailable_count",
        "client_sync_chat_batch_send_wall_samples",
        "client_sync_chat_batch_send_wall_total_us",
        "client_sync_chat_batch_send_wall_max_us",
        "client_sync_chat_batch_send_cpu_samples",
        "client_sync_chat_batch_send_cpu_total_us",
        "client_sync_chat_batch_send_cpu_max_us",
        "client_sync_chat_batch_send_cpu_unavailable_count",
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
        "client_sync_chat_fanout_queue_depth",
        "client_sync_chat_fanout_pending_targets",
        "client_sync_chat_fanout_active_wave_jobs_remaining",
        "client_sync_chat_fanout_wave_recipient_count",
        "client_sync_chat_fanout_materialize_delay_samples",
        "client_sync_chat_fanout_materialize_delay_total_us",
        "client_sync_chat_fanout_materialize_delay_max_us",
        "client_sync_chat_fanout_materialize_delay_unavailable_count",
        "client_sync_chat_fanout_materialize_wall_samples",
        "client_sync_chat_fanout_materialize_wall_total_us",
        "client_sync_chat_fanout_materialize_wall_max_us",
        "client_sync_chat_fanout_materialize_cpu_samples",
        "client_sync_chat_fanout_materialize_cpu_total_us",
        "client_sync_chat_fanout_materialize_cpu_max_us",
        "client_sync_chat_fanout_materialize_cpu_unavailable_count",
        "client_sync_chat_fanout_wave_commit_wall_samples",
        "client_sync_chat_fanout_wave_commit_wall_total_us",
        "client_sync_chat_fanout_wave_commit_wall_max_us",
        "client_sync_chat_fanout_wave_commit_wall_unavailable_count",
        "client_sync_chat_fanout_wave_commit_cpu_samples",
        "client_sync_chat_fanout_wave_commit_cpu_total_us",
        "client_sync_chat_fanout_wave_commit_cpu_max_us",
        "client_sync_chat_fanout_wave_commit_cpu_unavailable_count",
        "client_sync_chat_singleton_send_wall_samples",
        "client_sync_chat_singleton_send_wall_total_us",
        "client_sync_chat_singleton_send_wall_max_us",
        "client_sync_chat_singleton_send_cpu_samples",
        "client_sync_chat_singleton_send_cpu_total_us",
        "client_sync_chat_singleton_send_cpu_max_us",
        "client_sync_chat_singleton_send_cpu_unavailable_count",
        "client_sync_pending_flush_samples",
        "client_sync_pending_flush_entries_total",
        "client_sync_pending_flush_entries_max",
        "client_sync_pending_flush_recipients_total",
        "client_sync_pending_flush_recipients_max",
        "client_sync_pending_flush_packets_total",
        "client_sync_pending_flush_packets_max",
        "client_sync_pending_flush_wall_samples",
        "client_sync_pending_flush_wall_total_us",
        "client_sync_pending_flush_wall_max_us",
        "client_sync_pending_flush_cpu_samples",
        "client_sync_pending_flush_cpu_total_us",
        "client_sync_pending_flush_cpu_max_us",
        "client_sync_pending_flush_cpu_unavailable_count",
        "client_sync_pending_chat_age_samples",
        "client_sync_pending_chat_age_total_us",
        "client_sync_pending_chat_age_max_us",
        "client_sync_pending_chat_age_unavailable_count",
        "client_sync_pending_chat_self_age_samples",
        "client_sync_pending_chat_self_age_total_us",
        "client_sync_pending_chat_self_age_max_us",
        "client_sync_pending_chat_self_age_unavailable_count",
        "client_sync_pending_chat_incoming_age_samples",
        "client_sync_pending_chat_incoming_age_total_us",
        "client_sync_pending_chat_incoming_age_max_us",
        "client_sync_pending_chat_incoming_age_unavailable_count",
        "client_sync_pending_chat_other_age_samples",
        "client_sync_pending_chat_other_age_total_us",
        "client_sync_pending_chat_other_age_max_us",
        "client_sync_pending_chat_other_age_unavailable_count",
        "client_sync_room_chat_origin_age_samples",
        "client_sync_room_chat_origin_age_total_us",
        "client_sync_room_chat_origin_age_max_us",
        "client_sync_room_chat_origin_age_unavailable_count",
        "client_sync_room_chat_speaker_flush_wall_samples",
        "client_sync_room_chat_speaker_flush_wall_total_us",
        "client_sync_room_chat_speaker_flush_wall_max_us",
        "client_sync_room_chat_speaker_flush_cpu_samples",
        "client_sync_room_chat_speaker_flush_cpu_total_us",
        "client_sync_room_chat_speaker_flush_cpu_max_us",
        "client_sync_room_chat_speaker_flush_cpu_unavailable_count",
        "client_sync_room_chat_fanout_enqueue_wall_samples",
        "client_sync_room_chat_fanout_enqueue_wall_total_us",
        "client_sync_room_chat_fanout_enqueue_wall_max_us",
        "client_sync_room_chat_fanout_enqueue_cpu_samples",
        "client_sync_room_chat_fanout_enqueue_cpu_total_us",
        "client_sync_room_chat_fanout_enqueue_cpu_max_us",
        "client_sync_room_chat_fanout_enqueue_cpu_unavailable_count",
        "client_sync_scheduled_chat_flush_delay_samples",
        "client_sync_scheduled_chat_flush_delay_total_us",
        "client_sync_scheduled_chat_flush_delay_max_us",
        "client_sync_scheduled_chat_flush_delay_unavailable_count",
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
        "client_sync_score_refresh_pending",
        "client_sync_score_refresh_queue_depth",
        "client_sync_chat_pending",
        "client_sync_chat_queue_depth",
        "client_sync_chat_active_wave_remaining",
        "client_sync_chat_consumer_armed",
        "client_sync_chat_consumer_callout_valid",
        "client_sync_chat_consumer_orphaned",
        "client_sync_chat_admission_deferrals",
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
        "client_sync_score_refresh_oldest_age_samples",
        "client_sync_score_refresh_oldest_age_total_us",
        "client_sync_score_refresh_oldest_age_max_us",
        "client_sync_score_refresh_oldest_age_unavailable_count",
        "client_sync_score_refresh_pressure_deferrals_max",
        "client_sync_score_refresh_slice_wall_samples",
        "client_sync_score_refresh_slice_wall_total_us",
        "client_sync_score_refresh_slice_wall_max_us",
        "client_sync_score_refresh_slice_wall_unavailable_count",
        "client_sync_score_refresh_cpu_unavailable_count"
    }));
}

private string room_message_event_batch_status_text(mapping status)
{
    return append_int_status_fields("", status, ({
        "room_message_event_jobs_enqueued",
        "room_message_event_targets_enqueued",
        "room_message_event_flush_callbacks",
        "room_message_event_recipients_processed",
        "room_message_event_batch_samples",
        "room_message_event_batch_messages_total",
        "room_message_event_batch_messages_max",
        "room_message_event_singleton_messages",
        "room_message_event_fallback_messages",
        "room_message_event_filled_recipients",
        "room_message_event_fallback_recipients",
        "room_message_event_dropped_recipients",
        "room_message_event_released_recipients",
        "room_message_event_invalid_entry_recipients",
        "room_message_event_target_identity_changed_recipients",
        "room_message_event_promotion_failed_recipients",
        "room_message_event_native_fill_failed_recipients",
        "room_message_event_native_append_failures",
        "room_message_event_native_rollback_failures",
        "room_message_event_completion_failed_recipients",
        "room_message_event_dropped_messages",
        "room_message_event_noninteractive_dropped_messages",
        "room_message_event_native_filled_messages",
        "room_message_event_owner_submitted_recipients",
        "room_message_event_owner_submitted_messages",
        "room_message_event_owner_completed_recipients",
        "room_message_event_owner_completed_messages",
        "room_message_event_owner_released_recipients",
        "room_message_event_owner_released_messages",
        "room_message_event_preencode_samples",
        "room_message_event_preencode_total_us",
        "room_message_event_preencode_max_us",
        "room_message_event_preencode_failures",
        "room_message_event_reserve_wave_samples",
        "room_message_event_reserve_wave_total_us",
        "room_message_event_reserve_wave_max_us",
        "room_message_event_sequence_block_samples",
        "room_message_event_sequence_block_total_us",
        "room_message_event_sequence_block_max_us",
        "room_message_event_reservation_register_samples",
        "room_message_event_reservation_register_total_us",
        "room_message_event_reservation_register_max_us",
        "room_message_event_columnar_queue_samples",
        "room_message_event_columnar_queue_total_us",
        "room_message_event_columnar_queue_max_us",
        "room_message_event_reservation_reused_targets",
        "room_message_event_reservation_new_targets",
        "room_message_event_reservation_reuse_waves",
        "room_message_event_player_seq_cache_hits",
        "room_message_event_player_seq_cache_misses",
        "room_message_event_player_seq_syncs",
        "room_message_event_native_prepare_samples",
        "room_message_event_native_prepare_total_us",
        "room_message_event_native_prepare_max_us",
        "room_message_event_native_fill_samples",
        "room_message_event_native_fill_total_us",
        "room_message_event_native_fill_max_us",
        "room_message_event_native_fill_batch_samples",
        "room_message_event_native_fill_batch_total_us",
        "room_message_event_native_fill_batch_max_us",
        "room_message_event_native_fill_batch_recipients_total",
        "room_message_event_native_fill_batch_recipients_max",
        "room_message_event_fallback_materialize_samples",
        "room_message_event_fallback_materialize_total_us",
        "room_message_event_fallback_materialize_max_us",
        "room_message_event_flush_delay_samples",
        "room_message_event_flush_delay_total_us",
        "room_message_event_flush_delay_max_us",
        "room_message_event_flush_lateness_samples",
        "room_message_event_flush_lateness_total_us",
        "room_message_event_flush_lateness_max_us",
        "room_message_event_promotion_scan_callbacks",
        "room_message_event_promotion_scan_entries_total",
        "room_message_event_promotion_scan_entries_max",
        "room_message_event_promotion_scan_cpu_total_us",
        "room_message_event_promotion_scan_cpu_max_us",
        "room_message_event_promotion_scan_cursor_wraps",
        "room_message_event_flush_wall_samples",
        "room_message_event_flush_wall_total_us",
        "room_message_event_flush_wall_max_us",
        "room_message_event_flush_recipient_budget_yields",
        "room_message_event_flush_wall_budget_yields",
        "room_message_event_flush_recipients_max",
        "room_message_event_full_ready_recipients",
        "room_message_event_full_fifo_blocked_recipients",
        "room_message_event_idle_ready_recipients",
        "room_message_event_max_hold_ready_recipients",
        "room_message_event_pressure_deferrals",
        "room_message_event_command_pressure_deferrals",
        "room_message_event_main_queue_pressure_deferrals",
        "room_message_event_buffered_input_pressure_deferrals",
        "room_message_event_schedule_failure_forced_recipients",
        "room_message_event_session_boundary_yields",
        "room_message_event_flush_recipient_budget",
        "room_message_event_flush_wall_budget_us",
        "room_message_event_columnar_layout",
        "room_message_event_native_recipient_batch_fill",
        "room_message_event_flush_priority",
        "room_message_event_coalesce_window_us",
        "room_message_event_continuation_delay_us",
        "room_message_event_pressure_retry_us",
        "room_message_event_promotion_scan_budget",
        "room_message_event_max_hold_us",
        "room_message_event_partial_depth",
        "room_message_event_ready_depth",
        "room_message_event_pending_recipients",
        "room_message_event_queue_depth",
    }));
}


/** 压平＋探测抑制＋msg_send_raw 三连的单一出口；保证单行以 \n 终止。 */
private void send_raw_flattened(object me, int header_length, string text)
{
    text = text[0..header_length - 1]
        + replace_string(text[header_length..], "\n", " ") + "\n";
    gateway_probe_suppress_once(me);
    call_other("/kernel/simul_efun", "msg_send_raw", me, text);
}

// ===== 快照发射主体（原样迁移，仅三连改走 send_raw_flattened） =====

void emit_command_loop_measurement_snapshot(object me, object adapter, string phase)
{
    object config;
    mapping command_dispatch_status;
    mapping client_sync_status;
    mapping driver_runtime_status;
    mapping xk_protocol_probe_status;
    mapping xk_protocol_phase_timing_status;
    mapping look_phase_tail_status;
    mapping gateway_runtime_status;
    mapping heartbeat_runtime_status;
    mapping owner_protocol_output_status;
    mapping owner_projection_status;
    mapping owner_projection_timing_status;
    mapping login_timing_status;
    mapping account_registration_status;
    mapping room_message_event_batch_status;
    mapping runtime_mode_status;
    string text;
    string *text_chunks;
    int header_length;
    mixed err;

    /* 调用方约束（S6 显式决策）：迁移前本发射器为命令对象 private，仅
     * ownercheck 命令路径可达；迁入共享守护后以 previous_object 归属校验
     * 保住同一不变量，白名单常量 EMIT_CALLER_WHITELIST 为策略单点。 */
    if (!objectp(previous_object())
        || member_array(explode(base_name(previous_object()), "#")[0],
                        EMIT_CALLER_WHITELIST) < 0)
        return;
    if (!objectp(me) || !objectp(adapter) || !can_use_ownercheck(me)
        || member_array(phase, ({ "login_before", "login_after",
            "command_loop_before", "command_loop_after" })) < 0)
        return;

    config = load_object(MULTICORE_CONFIG_D);
    err = catch(runtime_mode_status = objectp(config) ? config->query_status() : 0);
    if (err || !mapp(runtime_mode_status))
        runtime_mode_status = 0;
    err = catch(login_timing_status = LOGIN_SESSION->query_loadtest_login_timing_status());
    if (err || !mapp(login_timing_status))
        login_timing_status = 0;
    err = catch(account_registration_status =
        PLAYER_D->query_account_registration_transaction_status());
    if (err || !mapp(account_registration_status))
        account_registration_status = 0;
    err = catch(gateway_runtime_status = gateway_status());
    if (err || !mapp(gateway_runtime_status))
        gateway_runtime_status = 0;
    err = catch(client_sync_status = CLIENT_SYNC_D->query_status());
    if (err || !mapp(client_sync_status))
        client_sync_status = 0;
    if (phase == "login_before" || phase == "login_after") {
        if (!mapp(runtime_mode_status) || !mapp(login_timing_status)
            || !mapp(account_registration_status)
            || !mapp(gateway_runtime_status) || !mapp(client_sync_status)) {
            call_other("/kernel/simul_efun", "msg_send_raw", me,
                sprintf("[ownercheck measure] FAIL\nmeasurement_phase=%s\n"
                    "reason=login timing snapshot unavailable\n", phase));
            return;
        }
        text = sprintf("[ownercheck measure] PASS\nmeasurement_phase=%s\nmode=%s\n",
            phase, runtime_mode_status["mode"] || "");
        header_length = strlen(text);
        text += login_timing_status_text(login_timing_status);
        text += sprintf("account_registration_pending=%d\n"
            "account_registration_committed=%d\n"
            "bootstrap_core_pending=%d\n"
            "bootstrap_deferred_pending=%d\n",
            mapping_int(account_registration_status, "pending"),
            mapping_int(account_registration_status, "committed"),
            mapping_int(client_sync_status, "pending_entries"),
            mapping_int(client_sync_status, "client_sync_chat_pending")
                + mapping_int(client_sync_status, "client_sync_score_refresh_pending")
                + mapping_int(client_sync_status,
                    "client_sync_chat_fanout_pending_targets"));
        text = append_int_status_fields(text, gateway_runtime_status, ({
            "gateway_receive_enqueue_to_dispatch_samples",
            "gateway_receive_enqueue_to_dispatch_total_us",
            "gateway_receive_enqueue_to_dispatch_max_us",
            "gateway_receive_apply_samples",
            "gateway_receive_apply_total_us",
            "gateway_receive_apply_max_us",
            "gateway_receive_apply_thread_cpu_samples",
            "gateway_receive_apply_thread_cpu_total_us",
            "gateway_receive_apply_thread_cpu_max_us",
            "gateway_receive_apply_thread_cpu_unavailable",
            "session_fifo_pending_reservations",
        }));
        send_raw_flattened(me, header_length, text);
        return;
    }
    err = catch(command_dispatch_status = COMMAND_DISPATCH_D->query_status());
    if (err || !mapp(command_dispatch_status))
        command_dispatch_status = 0;
    err = catch(driver_runtime_status = adapter->query_owner_runtime_status());
    if (err || !mapp(driver_runtime_status))
        driver_runtime_status = 0;
    err = catch(xk_protocol_probe_status =
        GATEWAY_SESSION_D->query_xk_protocol_tail_probe_status());
    if (err || !mapp(xk_protocol_probe_status))
        xk_protocol_probe_status = 0;
    err = catch(xk_protocol_phase_timing_status =
        query_xk_protocol_observability_status());
    if (err || !mapp(xk_protocol_phase_timing_status))
        xk_protocol_phase_timing_status = 0;
    err = catch(look_phase_tail_status =
        GATEWAY_SESSION_D->query_look_phase_tail_status());
    if (err || !mapp(look_phase_tail_status))
        look_phase_tail_status = 0;
    err = catch {
        heartbeat_runtime_status = HEARTBEAT_D->query_stats();
    };
    if (err || !mapp(heartbeat_runtime_status))
        heartbeat_runtime_status = 0;
    err = catch(owner_protocol_output_status =
        GATEWAY_SESSION_D->query_owner_protocol_output_status());
    if (err || !mapp(owner_protocol_output_status))
        owner_protocol_output_status = 0;
    err = catch(owner_projection_status =
        GATEWAY_SESSION_D->query_owner_protocol_projection_status());
    if (err || !mapp(owner_projection_status))
        owner_projection_status = 0;
    err = catch(owner_projection_timing_status =
        GATEWAY_SESSION_D->query_owner_protocol_projection_timing_status());
    if (err || !mapp(owner_projection_timing_status))
        owner_projection_timing_status = 0;
    err = catch(room_message_event_batch_status =
        MESSAGE_D->query_room_message_event_batch_status());
    if (err || !mapp(room_message_event_batch_status))
        room_message_event_batch_status = 0;
    if (!mapp(runtime_mode_status) || !mapp(command_dispatch_status)
        || !mapp(client_sync_status)
        || !mapp(driver_runtime_status)
        || !mapp(xk_protocol_probe_status)
        || !mapp(xk_protocol_phase_timing_status)
        || !mapp(look_phase_tail_status) || !mapp(gateway_runtime_status)
        || !mapp(heartbeat_runtime_status)
        || !mapp(owner_protocol_output_status)
        || !mapp(owner_projection_status)
        || !mapp(owner_projection_timing_status)
        || !mapp(room_message_event_batch_status) || !mapp(login_timing_status)) {
        call_other("/kernel/simul_efun", "msg_send_raw", me,
            sprintf("[ownercheck measure] FAIL\nmeasurement_phase=%s\nreason=runtime snapshot unavailable\n",
                phase));
        return;
    }

    text = sprintf("[ownercheck measure] PASS\nmeasurement_phase=%s\nmode=%s\n",
        phase, runtime_mode_status["mode"] || "");
    header_length = strlen(text);
    text = append_int_status_fields(text, command_dispatch_status, ({
        "gateway_active_receive_count",
        "gateway_active_receive_total_us",
        "gateway_active_receive_max_us",
        "gateway_active_receive_tail_25ms_count",
        "gateway_receive_phase_session_lookup_samples",
        "gateway_receive_phase_session_lookup_total_us",
        "gateway_receive_phase_session_lookup_max_us",
        "gateway_receive_phase_session_touch_samples",
        "gateway_receive_phase_session_touch_total_us",
        "gateway_receive_phase_session_touch_max_us",
        "gateway_receive_phase_route_ensure_samples",
        "gateway_receive_phase_route_ensure_total_us",
        "gateway_receive_phase_route_ensure_max_us",
        "gateway_receive_phase_input_decode_samples",
        "gateway_receive_phase_input_decode_total_us",
        "gateway_receive_phase_input_decode_max_us",
        "gateway_receive_phase_process_input_samples",
        "gateway_receive_phase_process_input_total_us",
        "gateway_receive_phase_process_input_max_us",
        "gateway_receive_phase_finish_measurement_samples",
        "gateway_receive_phase_finish_measurement_total_us",
        "gateway_receive_phase_finish_measurement_max_us",
        "gateway_active_chat_receive_count",
        "gateway_active_chat_receive_total_us",
        "gateway_active_chat_receive_max_us",
        "gateway_active_chat_receive_tail_25ms_count",
        "gateway_active_dispatch_count",
        "gateway_active_dispatch_total_us",
        "gateway_active_dispatch_max_us",
        "gateway_active_dispatch_tail_25ms_count",
        "gateway_active_command_main_count",
        "gateway_active_command_main_total_us",
        "gateway_active_command_main_max_us",
        "gateway_active_command_main_tail_25ms_count",
        "gateway_active_command_main_tail_cpu_samples",
        "gateway_active_command_main_tail_cpu_total_us",
        "gateway_active_command_main_tail_cpu_max_us",
        "gateway_active_command_main_tail_cpu_unavailable_count",
        "gateway_active_command_main_tail_25ms_look_count",
        "gateway_active_command_main_tail_25ms_inventory_count",
        "gateway_active_command_main_tail_25ms_skills_count",
        "gateway_active_command_main_tail_25ms_hp_count",
        "gateway_active_command_main_tail_25ms_map_xk_count",
        "gateway_active_command_main_tail_25ms_score_count",
        "gateway_active_command_main_tail_25ms_other_count",
        "gateway_active_flush_count",
        "gateway_active_flush_total_us",
        "gateway_active_flush_max_us",
        "gateway_active_flush_tail_25ms_count",
        "gateway_command_dispatch_owner_trace_samples",
        "gateway_command_dispatch_owner_trace_total_us",
        "gateway_command_dispatch_owner_trace_max_us",
        "gateway_command_dispatch_transaction_prepare_samples",
        "gateway_command_dispatch_transaction_prepare_total_us",
        "gateway_command_dispatch_transaction_prepare_max_us",
        "gateway_command_dispatch_command_main_samples",
        "gateway_command_dispatch_command_main_total_us",
        "gateway_command_dispatch_command_main_max_us",
        "gateway_command_dispatch_actor_flush_samples",
        "gateway_command_dispatch_actor_flush_total_us",
        "gateway_command_dispatch_actor_flush_max_us",
        "gateway_command_dispatch_transaction_finalize_samples",
        "gateway_command_dispatch_transaction_finalize_total_us",
        "gateway_command_dispatch_transaction_finalize_max_us",
        "gateway_command_dispatch_dispatch_total_samples",
        "gateway_command_dispatch_dispatch_total_total_us",
        "gateway_command_dispatch_dispatch_total_max_us",
    }));
    text_chunks = ({
        text,
        client_sync_chat_batch_status_text(client_sync_status),
        command_family_timing_status_text(command_dispatch_status),
        trade_consume_command_timing_status_text(command_dispatch_status),
        movement_phase_timing_status_text(command_dispatch_status),
        combat_phase_timing_status_text(command_dispatch_status),
        progression_phase_timing_status_text(command_dispatch_status),
        room_message_event_batch_status_text(room_message_event_batch_status),
    });
    text = implode(text_chunks, "");
    // B2 读模型诊断：hits Δ＝窗口内命中的 list 发送次数（登录推送同样
    // 经发送器并首建条目，故 entries Δ＝新登录玩家数；正确性权威仍是 pull 元组比对）
    err = catch(text += sprintf(
        "quest_read_model_entries=%d\nquest_read_model_hits=%d\n"
        "quest_read_model_misses=%d\n",
        QUEST_PROTOCOL->query_quest_list_read_model_size(),
        (QUEST_PROTOCOL->query_quest_list_read_model_stats())["hits"],
        (QUEST_PROTOCOL->query_quest_list_read_model_stats())["misses"]));
    // B1 派生属性读模型诊断（域单点键，字段名真源＝combat_math stats 面）：
    // hits/misses Δ 证缓存参与度；enabled 为旁路开关读数。
    // 表达式形 catch＋内联三调（QUEST 先例）：此 FluffOS 块形 catch 内不接
    // 受局部声明，会腐化后续作用域解析（实测 Illegal LHS）
    err = catch(text += sprintf(
        "gateway_combat_derived_model_hits=%d\n"
        "gateway_combat_derived_model_misses=%d\n"
        "gateway_combat_derived_model_enabled=%d\n",
        (COMBAT_MATH_HELPER->query_derived_attr_model_stats())["hits"] || 0,
        (COMBAT_MATH_HELPER->query_derived_attr_model_stats())["misses"] || 0,
        (COMBAT_MATH_HELPER->query_derived_attr_model_stats())["enabled"] || 0));
#undef COMBAT_MATH_HELPER
    text = append_int_status_fields(text, xk_protocol_probe_status, ({
        "xk_protocol_send_count",
        "xk_protocol_send_tail_25ms_count",
        "xk_protocol_build_tail_25ms_count",
        "xk_protocol_encode_tail_25ms_count",
        "xk_protocol_encode_tail_cpu_samples",
        "xk_protocol_encode_tail_cpu_total_us",
        "xk_protocol_encode_tail_cpu_max_us",
        "xk_protocol_encode_tail_cpu_unavailable_count",
        "xk_protocol_metadata_tail_25ms_count",
        "xk_protocol_json_tail_25ms_count",
        "xk_protocol_frame_tail_25ms_count",
    }));
    text_chunks = ({
        text,
        xk_protocol_phase_timing_status_text(xk_protocol_phase_timing_status),
        xk_protocol_type_encode_status_text(xk_protocol_phase_timing_status),
        look_phase_tail_status_text(look_phase_tail_status),
        owner_async_stage_timing_status_text(driver_runtime_status,
            gateway_runtime_status),
        generic_future_watch_status_text(gateway_runtime_status),
        owner_protocol_output_status_text(owner_protocol_output_status),
        session_future_watch_status_text(gateway_runtime_status),
        room_output_projection_status_text(gateway_runtime_status),
        owner_projection_status_text(owner_projection_status),
        owner_projection_timing_status_text(owner_projection_timing_status),
        login_timing_status_text(login_timing_status),
        heartbeat_runtime_status_text(heartbeat_runtime_status),
    });
    text = implode(text_chunks, "");
    text = append_int_status_fields(text, gateway_runtime_status, ({
        "gateway_stale_master_frames_rejected",
        "gateway_sessions_detached_total",
        "gateway_session_rebind_attempts",
        "gateway_session_rebind_completed",
        "gateway_session_rebind_rejected",
        "gateway_session_reconnect_expired",
        "gateway_ingress_sequence_duplicates",
        "gateway_ingress_sequence_gaps",
        "gateway_ingress_sequence_stream_mismatches",
        "gateway_ingress_sequence_stream_resets",
        "gateway_ingress_ack_frames_failed",
        "gateway_receive_apply_samples",
        "gateway_receive_apply_total_us",
        "gateway_receive_apply_max_us",
        "gateway_receive_apply_thread_cpu_samples",
        "gateway_receive_apply_thread_cpu_total_us",
        "gateway_receive_apply_thread_cpu_max_us",
        "gateway_receive_apply_thread_cpu_unavailable",
        "gateway_receive_inline_drain_calls",
        "gateway_receive_deferred_drain_requests",
        "gateway_receive_main_queue_depth_samples",
        "gateway_receive_main_queue_depth_total",
        "gateway_receive_main_queue_depth_max",
        "gateway_main_drain_runs",
        "gateway_main_drain_tasks_total",
        "gateway_main_drain_tasks_max",
        "gateway_main_drain_budget_hits",
        "gateway_main_drain_deferred_scheduled",
        "gateway_main_drain_deferred_coalesced",
        "gateway_main_drain_deferred_executed",
        "gateway_main_drain_deferred_task_budget",
        "gateway_main_drain_deferred_base_wall_budget_us",
        "gateway_main_drain_deferred_backlog_wall_budget_us",
        "gateway_main_drain_deferred_backlog_threshold",
        "gateway_main_drain_deferred_backlog_boosted",
        "gateway_main_drain_deferred_tasks_total",
        "gateway_main_drain_deferred_tasks_max",
        "gateway_main_drain_deferred_wall_samples",
        "gateway_main_drain_deferred_wall_total_us",
        "gateway_main_drain_deferred_wall_avg_us",
        "gateway_main_drain_deferred_wall_max_us",
        "gateway_main_drain_deferred_wall_budget_yields",
        "gateway_main_drain_deferred_task_budget_yields",
        "gateway_main_drain_deferred_remaining_samples",
        "gateway_main_drain_deferred_remaining_total",
        "gateway_main_drain_deferred_remaining_avg",
        "gateway_main_drain_deferred_remaining_max",
        "gateway_main_drain_deferred_main_task_wall_max_us",
        "gateway_main_drain_deferred_main_tasks_exceeding_wall_budget",
        "gateway_read_batch_drain_task_budget",
        "gateway_read_batch_drain_wall_budget_us",
        "gateway_read_batch_drain_runs",
        "gateway_read_batch_drain_tasks_total",
        "gateway_read_batch_drain_tasks_max",
        "gateway_read_batch_drain_backlog_rescheduled",
        "gateway_read_batch_drain_wall_samples",
        "gateway_read_batch_drain_wall_total_us",
        "gateway_read_batch_drain_wall_avg_us",
        "gateway_read_batch_drain_wall_max_us",
        "gateway_read_batch_drain_wall_budget_yields",
        "gateway_read_batch_drain_task_budget_yields",
        "gateway_read_batch_drain_remaining_samples",
        "gateway_read_batch_drain_remaining_total",
        "gateway_read_batch_drain_remaining_avg",
        "gateway_read_batch_drain_remaining_max",
        "gateway_read_batch_drain_main_task_wall_max_us",
        "gateway_read_batch_drain_main_tasks_exceeding_wall_budget",
        "gateway_main_drain_deferred_wait_samples",
        "gateway_main_drain_deferred_wait_total_us",
        "gateway_main_drain_deferred_wait_max_us",
        "gateway_main_drain_deferred_wait_timer_queue_only",
        "gateway_read_dispatch_budget",
        "gateway_read_dispatch_runs",
        "gateway_read_dispatch_frames_total",
        "gateway_read_dispatch_frames_max",
        "gateway_read_dispatch_budget_hits",
        "gateway_read_dispatch_deferred_scheduled",
        "gateway_read_dispatch_deferred_coalesced",
        "gateway_read_dispatch_deferred_executed",
        "gateway_read_dispatch_input_paused",
        "gateway_read_dispatch_input_resumed",
        "gateway_read_dispatch_buffer_compactions",
        "gateway_read_dispatch_buffer_compacted_bytes",
        "gateway_read_dispatch_front_shift_bytes_avoided",
        "gateway_read_dispatch_buffer_peak_bytes",
        "gateway_command_input_pending_sessions",
        "gateway_command_task_pending_sessions",
        "gateway_read_dispatch_pending_masters",
        "gateway_command_pressure",
        "gateway_main_queue_pending",
        "gateway_main_queue_read_high_watermark",
        "gateway_main_queue_read_low_watermark",
        "gateway_main_queue_read_admission_limited",
        "gateway_main_queue_read_pressure_events",
        "gateway_main_queue_read_paused",
        "gateway_main_queue_read_resumed",
        "gateway_main_queue_read_paused_masters",
        "gateway_event_priority_levels",
        "gateway_normal_event_priority",
        "gateway_io_event_priority",
        "gateway_background_dispatch_max_interval_us",
        "gateway_background_dispatch_max_callbacks",
        "gateway_background_dispatch_min_priority",
        "gateway_backend_tick_slice_budget",
        "gateway_backend_tick_slice_wall_budget_us",
        "gateway_backend_tick_continuation_delay_us",
        "gateway_backend_tick_slice_runs",
        "gateway_backend_tick_slice_callbacks_total",
        "gateway_backend_tick_slice_callbacks_max",
        "gateway_backend_tick_slice_budget_yields",
        "gateway_backend_tick_slice_wall_yields",
        "gateway_backend_tick_continuations_scheduled",
        "gateway_backend_tick_continuations_executed",
        "gateway_backend_owner_main_slice_budget",
        "gateway_backend_owner_main_slice_wall_budget_us",
        "gateway_backend_owner_main_continuation_delay_us",
        "gateway_backend_owner_main_slice_runs",
        "gateway_backend_owner_main_slice_tasks_total",
        "gateway_backend_owner_main_slice_tasks_max",
        "gateway_backend_owner_main_slice_task_budget_yields",
        "gateway_backend_owner_main_slice_wall_yields",
        "gateway_backend_owner_main_tasks_exceeding_wall_budget",
        "gateway_backend_owner_main_continuations_scheduled",
        "gateway_backend_owner_main_continuations_executed",
        "gateway_backend_owner_main_continuation_pending",
        "gateway_reply_enqueue_to_dispatch_samples",
        "gateway_reply_enqueue_to_dispatch_total_us",
        "gateway_reply_enqueue_to_dispatch_max_us",
        "gateway_reply_execute_samples",
        "gateway_reply_execute_total_us",
        "gateway_reply_execute_max_us",
        "gateway_output_enqueue_to_dispatch_samples",
        "gateway_output_enqueue_to_dispatch_total_us",
        "gateway_output_enqueue_to_dispatch_max_us",
        "gateway_output_execute_samples",
        "gateway_output_execute_total_us",
        "gateway_output_execute_max_us",
        "gateway_message_event_template_cache_hits",
        "gateway_message_event_template_cache_misses",
        "gateway_message_event_template_cache_evictions",
        "gateway_message_event_template_cache_bypasses",
        "gateway_output_fifo_head_blocked_fills",
        "gateway_output_fifo_head_blocked_predecessors_total",
        "gateway_output_fifo_head_blocked_predecessors_max",
        "gateway_output_fifo_enqueued",
        "gateway_output_fifo_flushed",
        "gateway_output_fifo_released",
        "gateway_output_fifo_oversize_dropped",
        "gateway_output_fifo_writer_failures",
        "gateway_output_fifo_destroyed_ready",
        "gateway_output_fifo_destroyed_pending",
        "gateway_raw_writes_failed",
        "session_fifo_depth",
        "session_fifo_pending_reservations",
    }));
    send_raw_flattened(me, header_length, text);
}
