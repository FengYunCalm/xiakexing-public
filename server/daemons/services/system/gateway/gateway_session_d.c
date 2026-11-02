/**
 * @file    gateway_session_d.c
 * @brief   Go 网关逻辑会话真源



 *
 * @par Description:
 * 统一维护 websocket session_id 与 LPC 登录对象/玩家对象之间的绑定关系。
 * 当前阶段先承接：
 * - session open/close 账本
 * - login_ob / player handoff
 * - gateway inbound 的最小输入路由
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

#undef is_player

#define DEFAULT_RESUME_TOKEN_TTL 180
#define WORKER_DRIVER_ADAPTER "/daemons/services/system/multicore/worker_driver_adapter"

inherit "/lib/framework/daemon";

nosave mapping _sessions;
nosave mapping _object_to_session;
nosave mapping _resume_tokens;
nosave mapping _sessioncheck_players;
nosave int _resume_token_ttl;
nosave int _cleanup_call_out;
nosave int _session_cleanup_interval;
nosave int _xk_protocol_send_count;
nosave mapping _xk_protocol_tail_probe = ([
    "xk_protocol_send_tail_25ms_count": 0,
    "xk_protocol_build_tail_25ms_count": 0,
    "xk_protocol_encode_tail_25ms_count": 0,
    "xk_protocol_metadata_tail_25ms_count": 0,
    "xk_protocol_json_tail_25ms_count": 0,
    "xk_protocol_frame_tail_25ms_count": 0,
    "xk_protocol_encode_tail_cpu_samples": 0,
    "xk_protocol_encode_tail_cpu_total_us": 0,
    "xk_protocol_encode_tail_cpu_max_us": 0,
    "xk_protocol_encode_tail_cpu_unavailable_count": 0,
]);
nosave mapping _xk_protocol_phase_timing = ([]);
nosave mapping _xk_protocol_type_encode_timing = ([]);
nosave mapping _owner_protocol_projection_counts = ([
    "owner_projection_publish_guard_checked_count": 0,
    "owner_projection_publish_guard_rejected_count": 0,
    "owner_projection_publish_guard_committed_count": 0,
    "owner_projection_inventory_owner_completed_count": 0,
    "owner_projection_inventory_inline_completed_count": 0,
    "owner_projection_inventory_sync_fallback_count": 0,
    "owner_projection_inventory_owner_stale_count": 0,
    "owner_projection_inventory_owner_failed_count": 0,
    "owner_projection_inventory_preencoded_hit_count": 0,
    "owner_projection_inventory_preencoded_sync_fallback_count": 0,
    "owner_projection_inventory_preencoded_failed_count": 0,
    "owner_projection_hp_dialog_owner_completed_count": 0,
    "owner_projection_hp_dialog_sync_fallback_count": 0,
    "owner_projection_hp_dialog_owner_stale_count": 0,
    "owner_projection_hp_dialog_owner_failed_count": 0,
    "owner_projection_hp_dialog_preencoded_hit_count": 0,
    "owner_projection_hp_dialog_preencoded_sync_fallback_count": 0,
    "owner_projection_hp_dialog_preencoded_failed_count": 0,
    "owner_projection_score_dialog_owner_completed_count": 0,
    "owner_projection_score_dialog_sync_fallback_count": 0,
    "owner_projection_score_dialog_owner_stale_count": 0,
    "owner_projection_score_dialog_owner_failed_count": 0,
    "owner_projection_score_dialog_preencoded_hit_count": 0,
    "owner_projection_score_dialog_preencoded_sync_fallback_count": 0,
    "owner_projection_score_dialog_preencoded_failed_count": 0,
    "owner_projection_map_grid_owner_completed_count": 0,
    "owner_projection_map_grid_sync_fallback_count": 0,
    "owner_projection_map_grid_owner_stale_count": 0,
    "owner_projection_map_grid_owner_failed_count": 0,
    "owner_projection_map_grid_owner_cancelled_count": 0,
    "owner_projection_map_grid_current_hit_count": 0,
    "owner_projection_map_grid_pending_cached_count": 0,
    "owner_projection_map_grid_preencoded_hit_count": 0,
    "owner_projection_map_grid_preencoded_sync_fallback_count": 0,
    "owner_projection_map_grid_preencoded_failed_count": 0,
]);
nosave mapping _owner_protocol_projection_timing = ([]);
nosave int _owner_protocol_projection_timing_enabled;
nosave mapping _owner_protocol_output_counts = ([
    "owner_protocol_output_submitted_count": 0,
    "owner_protocol_output_reserve_failed_count": 0,
    "owner_protocol_output_submit_failed_count": 0,
    "owner_protocol_output_watch_failed_count": 0,
    "owner_protocol_output_sync_fallback_count": 0,
    "owner_protocol_output_completed_count": 0,
    "owner_protocol_output_released_count": 0,
]);
nosave mapping _look_phase_tail_probe = ([
    "look_phase_tail_samples": 0,
    "look_phase_tail_total_us": 0,
    "look_phase_tail_max_us": 0,
    "look_phase_room_view_tail_samples": 0,
    "look_phase_room_view_tail_total_us": 0,
    "look_phase_room_view_tail_max_us": 0,
    "look_phase_room_view_ensure_tail_samples": 0,
    "look_phase_room_view_ensure_tail_total_us": 0,
    "look_phase_room_view_ensure_tail_max_us": 0,
    "look_phase_room_view_ensure_owner_route_tail_samples": 0,
    "look_phase_room_view_ensure_owner_route_tail_total_us": 0,
    "look_phase_room_view_ensure_owner_route_tail_max_us": 0,
    "look_phase_room_view_ensure_gate_tail_samples": 0,
    "look_phase_room_view_ensure_gate_tail_total_us": 0,
    "look_phase_room_view_ensure_gate_tail_max_us": 0,
    "look_phase_room_view_ensure_entities_tail_samples": 0,
    "look_phase_room_view_ensure_entities_tail_total_us": 0,
    "look_phase_room_view_ensure_entities_tail_max_us": 0,
    "look_phase_room_view_shared_tail_samples": 0,
    "look_phase_room_view_shared_tail_total_us": 0,
    "look_phase_room_view_shared_tail_max_us": 0,
    "look_phase_room_view_player_filter_tail_samples": 0,
    "look_phase_room_view_player_filter_tail_total_us": 0,
    "look_phase_room_view_player_filter_tail_max_us": 0,
    "look_phase_room_view_quest_projection_tail_samples": 0,
    "look_phase_room_view_quest_projection_tail_total_us": 0,
    "look_phase_room_view_quest_projection_tail_max_us": 0,
    "look_phase_room_view_finalize_tail_samples": 0,
    "look_phase_room_view_finalize_tail_total_us": 0,
    "look_phase_room_view_finalize_tail_max_us": 0,
    "look_phase_read_summary_tail_samples": 0,
    "look_phase_read_summary_tail_total_us": 0,
    "look_phase_read_summary_tail_max_us": 0,
    "look_phase_render_tail_samples": 0,
    "look_phase_render_tail_total_us": 0,
    "look_phase_render_tail_max_us": 0,
    "look_phase_emit_tail_samples": 0,
    "look_phase_emit_tail_total_us": 0,
    "look_phase_emit_tail_max_us": 0,
]);

private void schedule_resume_cleanup(int delay);
void cleanup_expired_resume_tokens();
void cleanup_stale_sessions();
mapping query_session(string session_id);
string query_object_session_id(object target);
object query_session_target(string session_id);
mapping query_gateway_session_route(string session_id);
int record_owner_protocol_output(string outcome);
void dispatch_sessioncheck_fifo(string player_id, string session_id,
    mapping before);
void dispatch_sessioncheck_watch(string player_id, mapping before,
    int submitted_success, int submitted_failed, int registered_timeout,
    int rejected_registration);
void dispatch_sessioncheck_stale(string player_id, mapping before,
    string token_before, string token_after);
void dispatch_sessioncheck_cancel(string player_id, mapping before,
    int direct_cancelled, int direct_taken, int registered);
void dispatch_sessioncheck_destruct(string player_id, mapping before,
    int registered);
int register_sessioncheck_player(string player_id, object player);
object take_sessioncheck_player(string player_id);

mapping health_check()
{
    return ([
        "ok": mapp(_sessions) && mapp(_object_to_session) && mapp(_resume_tokens),
        "service": "gateway_session",
    ]);
}

private object sessioncheck_command()
{
    object command;
    mixed err;

    err = catch(command = load_object("/cmds/admin/sessioncheck"));
    return err ? 0 : command;
}

int register_sessioncheck_player(string player_id, object player)
{
    if (!stringp(player_id) || player_id == "" || !objectp(player))
        return 0;
    if (!mapp(_sessioncheck_players))
        _sessioncheck_players = ([]);
    _sessioncheck_players[player_id] = player;
    return 1;
}

object take_sessioncheck_player(string player_id)
{
    object player;

    if (!mapp(_sessioncheck_players) || !stringp(player_id)
        || player_id == "")
        return 0;
    player = _sessioncheck_players[player_id];
    map_delete(_sessioncheck_players, player_id);
    return objectp(player) ? player : 0;
}

void dispatch_sessioncheck_fifo(string player_id, string session_id,
    mapping before)
{
    object command;

    command = sessioncheck_command();
    if (objectp(command))
        command->emit_fifo_check(player_id, session_id, before);
}

void dispatch_sessioncheck_watch(string player_id, mapping before,
    int submitted_success, int submitted_failed, int registered_timeout,
    int rejected_registration)
{
    object command;

    command = sessioncheck_command();
    if (objectp(command))
        command->emit_watch_check(player_id, before, submitted_success,
            submitted_failed, registered_timeout, rejected_registration);
}

void dispatch_sessioncheck_stale(string player_id, mapping before,
    string token_before, string token_after)
{
    object command;

    command = sessioncheck_command();
    if (objectp(command))
        command->emit_stale_check(player_id, before, token_before, token_after);
}

void dispatch_sessioncheck_cancel(string player_id, mapping before,
    int direct_cancelled, int direct_taken, int registered)
{
    object command;

    command = sessioncheck_command();
    if (objectp(command))
        command->emit_cancel_check(player_id, before, direct_cancelled,
            direct_taken, registered);
}

void dispatch_sessioncheck_destruct(string player_id, mapping before,
    int registered)
{
    object command;

    command = sessioncheck_command();
    if (objectp(command))
        command->emit_destruct_check(player_id, before, registered);
}

private mapping create_gateway_session(string session_id)
{
    mapping route;

    route = query_gateway_session_route(session_id);
    if (!mapp(route) || route["ok"] != 1)
        return 0;
    return ([
        "id": session_id,
        "state": "open",
        "owner_scope": "global/login",
        "target": 0,
        "created_at": time(),
        "last_active": time(),
        "output_count": 0,
        "last_server_seq": 0,
        "last_completed_protocol_seq": 0,
        "fifo_violations": 0,
        "pending_protocol_reservations": 0,
        "completed_protocol_reservations": 0,
        "released_protocol_reservations": 0,
        "protocol_reservations": ([]),
        "owner_output_reservations": ([]),
        "pending_owner_outputs": 0,
        "completed_owner_outputs": 0,
        "released_owner_outputs": 0,
        "last_packet_type": "",
        "last_stream": "",
        "owner_id": "owner/global",
        "owner_epoch": 0,
        "service_owner_id": route["owner_id"],
        "service_policy": route["policy"],
        "service_map_v2": 1,
    ]);
}

mapping query_gateway_session_route(string session_id)
{
    mapping route;
    string route_key;
    mixed err;

    route_key = stringp(session_id) && session_id != "" ? session_id : "anonymous";
    err = catch(route = OWNER_RUNTIME_D->query_service_owner_route("gateway_session", route_key));
    if (err || !mapp(route) || route["ok"] != 1) {
        /* 路由 fail-closed 必须可观测：否则 login handoff 只剩下
         * “连接切换失败”，无法定位是 policy、registry 还是 readiness。 */
        log_file("system/gateway_session", sprintf(
            "%s gateway session owner route failed: session=%s err=%O reason=%s "
            "readiness=%O\n",
            ctime(time()), route_key, err,
            mapp(route) ? route["failure_reason"] : "owner route unavailable",
            mapp(route) ? route["readiness"] : ([])));
        return ([
            "ok": 0,
            "service": "gateway_session",
            "key": route_key,
            "failure_reason": err ? sprintf("owner route query failed: %O", err)
                : (mapp(route) ? route["failure_reason"] : "owner route unavailable"),
            "readiness": mapp(route) ? route["readiness"] : ([ "ok": 0 ]),
            "dependencies": mapp(route) ? route["dependencies"] : ([ ]),
            "direct_cross_owner_write": 0,
        ]);
    }

    route["gateway_fifo_required"] = 1;
    route["direct_cross_owner_write"] = 0;
    return route;
}

private int update_session_service_route(mapping session)
{
    mapping route;

    if (!mapp(session) || !stringp(session["id"]) || session["id"] == "")
        return 0;

    route = query_gateway_session_route(session["id"]);
    if (!mapp(route) || route["ok"] != 1)
        return 0;
    session["service_owner_id"] = route["owner_id"];
    session["service_policy"] = route["policy"];
    session["service_map_v2"] = 1;
    return 1;
}

private mapping query_target_owner_snapshot(object target)
{
    object adapter;
    mapping snapshot;
    mixed err;

    snapshot = ([ "owner_id": "owner/global", "owner_epoch": 0 ]);
    if (!objectp(target))
        return snapshot;

    adapter = load_object(WORKER_DRIVER_ADAPTER);
    if (!objectp(adapter))
        return snapshot;

    err = catch(snapshot["owner_id"] = adapter->query_owner_id(target));
    if (err || !stringp(snapshot["owner_id"]) || snapshot["owner_id"] == "")
        snapshot["owner_id"] = "owner/global";
    err = catch(snapshot["owner_epoch"] = adapter->query_owner_epoch(target));
    if (err || !intp(snapshot["owner_epoch"]))
        snapshot["owner_epoch"] = 0;
    return snapshot;
}

private void refresh_session_owner_snapshot(mapping session, object target)
{
    mapping owner_snapshot;

    if (!mapp(session) || !objectp(target))
        return;

    owner_snapshot = query_target_owner_snapshot(target);
    session["owner_id"] = owner_snapshot["owner_id"];
    session["owner_epoch"] = owner_snapshot["owner_epoch"];
    session["owner_updated_at"] = time();
}

private int resolve_resume_token_ttl()
{
    mapping cfg;
    mixed raw_ttl;

    cfg = CONFIG_D->query_gateway_config();
    raw_ttl = mapp(cfg) ? cfg["session_resume_ttl_seconds"] : 0;
    if (stringp(raw_ttl))
        raw_ttl = to_int(raw_ttl);
    if (!intp(raw_ttl) || raw_ttl <= 0)
        return DEFAULT_RESUME_TOKEN_TTL;
    return raw_ttl;
}

private string generate_resume_token()
{
    string chars;
    string token;
    int index;
    int i;

    chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    token = "";

    // 恢复 token 等价于免密凭据：使用驱动的密码学随机（secure_random），
    // 不用可预测的 random() 序列。
    for (i = 0; i < 40; i++) {
        index = secure_random(strlen(chars));
        token += chars[index..index];
    }

    return token;
}

private string query_target_account_id(object target)
{
    string account_id;

    if (!objectp(target))
        return "";

    account_id = function_exists("query_account_id", target)
        ? (target->query_account_id() || "")
        : "";
    if (stringp(account_id) && account_id != "")
        return account_id;

    account_id = function_exists("query", target)
        ? (target->query("account_id") || "")
        : "";
    if (stringp(account_id) && account_id != "")
        return account_id;

    return get_object_id(target);
}

private string query_target_protocol_version(object target)
{
    mixed version;

    if (!objectp(target) || !function_exists("query_temp", target))
        return "";

    version = target->query_temp("xk_protocol_version");
    return stringp(version) ? version : "";
}

private string query_target_runtime_session_id(object target)
{
    mixed session_id;

    if (!objectp(target) || !function_exists("query_temp", target))
        return "";

    session_id = target->query_temp("gateway/session_id");
    if (stringp(session_id) && session_id != "")
        return session_id;

    session_id = target->query_temp("session_id");
    return stringp(session_id) ? session_id : "";
}

private void store_resume_record(mapping session, object target)
{
    mapping record;
    string account_id;
    string protocol_version;
    string resume_token;

    if (!mapp(session) || !objectp(target))
        return;

    account_id = query_target_account_id(target);
    if (!stringp(account_id) || account_id == "")
        return;
    if (session["resume_token_consumed"])
        return;

    resume_token = stringp(session["resume_token"]) ? session["resume_token"] : "";
    if (resume_token == "") {
        do {
            resume_token = generate_resume_token();
        } while (mapp(_resume_tokens[resume_token]));
    }

    protocol_version = query_target_protocol_version(target);
    session["resume_token"] = resume_token;
    session["account_id"] = account_id;
    if (protocol_version != "")
        session["xk_protocol_version"] = protocol_version;

    record = ([
        "token": resume_token,
        "account_id": account_id,
        "session_id": session["id"],
        "state": session["state"],
        "updated_at": time(),
        "expires_at": time() + _resume_token_ttl,
    ]);
    if (intp(session["created_at"]))
        record["created_at"] = session["created_at"];
    if (protocol_version != "")
        record["xk_protocol_version"] = protocol_version;

    _resume_tokens[resume_token] = record;
    schedule_resume_cleanup(_resume_token_ttl);
}

private mapping sanitize_resume_record(mapping record)
{
    string token;

    if (!mapp(record))
        return 0;

    token = stringp(record["token"]) ? record["token"] : "";
    if (!intp(record["expires_at"]) || record["expires_at"] <= time()) {
        if (token != "")
            map_delete(_resume_tokens, token);
        return 0;
    }

    return copy(record);
}

private void schedule_resume_cleanup(int delay)
{
    if (_cleanup_call_out)
        return;
    _cleanup_call_out = call_out("cleanup_expired_resume_tokens", delay > 0 ? delay : 1);
}

void cleanup_expired_resume_tokens()
{
    mapping record;
    string token;
    string *expired;

    _cleanup_call_out = 0;
    expired = ({ });

    foreach (token, record in _resume_tokens) {
        if (!mapp(record)
            || !intp(record["expires_at"])
            || record["expires_at"] <= time())
            expired += ({ token });
    }

    foreach (token in expired)
        map_delete(_resume_tokens, token);

    if (sizeof(_resume_tokens) > 0)
        schedule_resume_cleanup(_resume_token_ttl);
}

private int is_player_target(object target)
{
    return objectp(target) && function_exists("is_player", target)
        && target->is_player();
}

private int set_object_session_state(object target, string session_id)
{
    mapping change;
    mapping response;

    if (!is_player_target(target))
        return target->set("xk/gateway_session_id", session_id);
    change = ([ "xk/gateway_session_id": session_id ]);
    response = PLAYER_STATE_D->commit_state_fields(target, change,
        PLAYER_STATE_D->build_state_idempotency_key(target,
            "gateway/session_binding", change));
    if (!mapp(response) || !response["ok"])
        log_file("system/gateway_session", sprintf(
            "%s session state commit failed: target=%s code=%s message=%s "
            "interactive=%d owner_ready=%d transaction=%O\n",
            ctime(time()), get_object_id(target) || file_name(target),
            mapp(response) ? response["code"] : "no_response",
            mapp(response) ? response["message"] : "",
            interactive(target) ? 1 : 0,
            function_exists("query_temp", target)
                && target->query_temp("player/state_owner_ready") ? 1 : 0,
            mapp(response) ? response["transaction"] : 0));
    return mapp(response) && response["ok"];
}

private int delete_object_session_state(object target)
{
    mapping response;

    if (!is_player_target(target))
        return target->delete("xk/gateway_session_id");
    response = PLAYER_STATE_D->commit_state_deletions(target,
        ({ "xk/gateway_session_id" }),
        PLAYER_STATE_D->build_state_idempotency_key(target,
            "gateway/session_binding_clear", "xk/gateway_session_id"));
    return mapp(response) && response["ok"];
}

/* 内存绑定（temp + _object_to_session）与持久绑定（xk/gateway_session_id）
 * 对玩家目标必须一致：持久写失败时整体失败并回滚内存标记，不允许只成功
 * 一半。持久副本的用途是玩家对象被重建（destruct+reload）后恢复绑定。
 * 非玩家对象（登录对象等）的持久副本只是 best effort——其 dbase 可能含
 * 不可序列化引用（如 login_player 对象），写失败不得阻塞登录链路；
 * 内存绑定即真源。 */
private int set_object_session(object target, string session_id, string state)
{
    mixed previous_session_id;
    mixed previous_state;

    if (!objectp(target) || !stringp(session_id) || session_id == "")
        return 0;

    /* L-46：失败路径必须恢复目标原有的内存标记，而不是删除唯一旧状态；
     * 重绑场景下 target 可能已持有另一个（或同一个）会话标记。 */
    previous_session_id = function_exists("query_temp", target)
        ? target->query_temp("gateway/session_id") : 0;
    previous_state = function_exists("query_temp", target)
        ? target->query_temp("gateway/state") : 0;
    target->set_temp("gateway/session_id", session_id);
    if (stringp(state) && state != "")
        target->set_temp("gateway/state", state);
    if (is_player_target(target)) {
        if (!set_object_session_state(target, session_id)) {
            if (stringp(previous_session_id))
                target->set_temp("gateway/session_id", previous_session_id);
            else
                target->delete_temp("gateway/session_id");
            if (stringp(previous_state))
                target->set_temp("gateway/state", previous_state);
            else
                target->delete_temp("gateway/state");
            return 0;
        }
    } else if (function_exists("set", target)) {
        set_object_session_state(target, session_id);
    }
    _object_to_session[target] = session_id;
    return 1;
}

/* 解绑同样先动持久副本：持久删除失败时保留内存标记并返回 0，
 * 调用方决定重试或把会话留在 pending，避免“内存已解绑、磁盘仍绑定”。 */
private int clear_object_session(object target)
{
    mixed existing;
    int delete_ok;

    if (!objectp(target))
        return 0;

    existing = function_exists("query", target)
        ? target->query("xk/gateway_session_id") : 0;
    /* 未绑定过视为已解绑；只有确实存在持久绑定且删除失败才算失败。 */
    if (stringp(existing) && existing != "" && function_exists("delete", target)) {
        delete_ok = delete_object_session_state(target);
        if (!delete_ok) {
            /* 目标已不是在线角色（登录对象、被 login_replace 替换的旧角色、
             * 终止中的对象）时 owner 事务会拒绝写入；此时持久副本已无恢复
             * 意义，记录后继续解绑，不能因此阻塞 login handoff 与会话清理。
             * 仍在线角色的解绑失败保持 fail-closed。 */
            if (is_player_target(target) && interactive(target))
                return 0;
            log_file("system/gateway_session", sprintf(
                "%s session unbind skipped: target not an online player (%s)\n",
                ctime(time()), get_object_id(target) || file_name(target)));
        }
    }
    map_delete(_object_to_session, target);
    if (function_exists("delete_temp", target)) {
        target->delete_temp("gateway/session_id");
        target->delete_temp("gateway/state");
    }
    return 1;
}

/* L-47：残余 reservation 释放必须返回结构化真实结果。native release 失败或
 * 抛异常时保留原登记（不清账），只对确认释放的项累计 released；失败项与
 * 已填充待对账项保留到其合法终态，失败指标进入白名单可观测。 */
private mapping release_session_protocol_reservations(mapping session, object target)
{
    mixed detach_error;
    mixed release_error;
    mixed release_ok;
    mapping reservations;
    mapping reservation;
    mapping retained;
    int reservation_key;
    int released_count;
    int failed_count;
    int reservation_id;

    if (!mapp(session))
        return ([ "released": 0, "failed": 0, "retained": 0 ]);
    retained = ([]);
    if (objectp(target)) {
        detach_error = catch(MESSAGE_D->discard_room_message_events_for_target(
            target));
        if (detach_error)
            record_owner_protocol_output("release_failed");
    }
    reservations = mapp(session["protocol_reservations"])
        ? session["protocol_reservations"] : ([]);
    foreach (reservation_key, reservation in reservations) {
        reservation_id = mapp(reservation) && intp(reservation["reservation_id"])
            ? reservation["reservation_id"] : 0;
        if (reservation_id <= 0) {
            /* 无身份项无法确认释放：保留原登记，不计 released。 */
            retained[reservation_key] = reservation;
            failed_count++;
            record_owner_protocol_output("release_failed");
            continue;
        }
        release_ok = 0;
        if (objectp(target)) {
            release_error = catch(
                release_ok = gateway_session_release(target, reservation_id));
            if (release_error)
                release_ok = 0;
        }
        if (release_ok) {
            released_count++;
            record_owner_protocol_output("released");
        } else {
            failed_count++;
            record_owner_protocol_output("release_failed");
            retained[reservation_key] = reservation;
        }
    }
    session["protocol_reservations"] = retained;
    session["pending_protocol_reservations"] = sizeof(retained);
    session["released_protocol_reservations"] =
        (intp(session["released_protocol_reservations"])
            ? session["released_protocol_reservations"] : 0) + released_count;
    if (released_count > 0 || failed_count > 0)
        session["reservation_updated_at"] = time();
    return ([ "released": released_count, "failed": failed_count,
        "retained": sizeof(retained) ]);
}

private mapping remove_session(string session_id, int issue_resume)
{
    mapping session;
    mapping output_release;
    mapping output_state;
    mapping reservation_release;
    object target;
    string resume_token;

    if (!stringp(session_id) || session_id == "")
        return ([ "ok": 0, "session_id": session_id,
            "error": "session id required" ]);

    session = _sessions[session_id];
    if (!mapp(session))
        return ([ "ok": 1, "session_id": session_id, "state": "absent" ]);

    target = objectp(session["target"]) ? session["target"] : 0;

    /* 先收口 owner 输出 FIFO：释放未完成时不删除会话与待清理记录，
     * 让 logout 生命周期能重试，不把失败清理伪装成已终结。 */
    output_release = OWNER_SESSION_FIFO_D->release_session_outputs(session_id,
        "gateway session removed");
    if (!mapp(output_release) || output_release["failed"] > 0) {
        record_owner_protocol_output("release_failed");
        return ([ "ok": 0, "session_id": session_id,
            "state": "release_pending",
            "release_failed": mapp(output_release) ? output_release["failed"] : 1,
            "output_release": output_release,
            "error": "owner output release pending" ]);
    }
    output_state = OWNER_SESSION_FIFO_D->clear_session_output_state(
        session_id);
    if (!mapp(output_state) || output_state["ok"] != 1) {
        record_owner_protocol_output("release_failed");
        return ([ "ok": 0, "session_id": session_id,
            "state": "release_pending",
            "output_state": output_state,
            "error": mapp(output_state) ? output_state["reason"]
                : "owner output state not cleared" ]);
    }

    if (issue_resume)
        store_resume_record(session, target);
    else {
        resume_token = stringp(session["resume_token"]) ? session["resume_token"] : "";
        if (resume_token != "")
            map_delete(_resume_tokens, resume_token);
    }
    /* L-47：残余 reservation 未确认释放时保留会话与登记，返回待释放状态，
     * 让 logout 生命周期重试，不把失败清理伪装成已终结。 */
    reservation_release = release_session_protocol_reservations(session, target);
    if (mapp(reservation_release) && reservation_release["failed"] > 0) {
        record_owner_protocol_output("release_failed");
        return ([ "ok": 0, "session_id": session_id,
            "state": "release_pending",
            "reservation_release": reservation_release,
            "error": "protocol reservation release pending" ]);
    }
    /* 持久解绑失败不能删会话：留着让 logout 生命周期重试，
     * 否则内存已无会话、磁盘仍留旧绑定。 */
    if (objectp(target) && !clear_object_session(target)) {
        log_file("system/gateway_session", sprintf(
            "%s persistent session unbind pending: session=%s target=%s\n",
            ctime(time()), session_id, get_object_id(target) || file_name(target)));
        return ([ "ok": 0, "session_id": session_id,
            "state": "unbind_pending",
            "error": "persistent session unbind pending" ]);
    }
    CLIENT_SYNC_D->discard_session_pending(session_id);
    map_delete(_sessions, session_id);
    return ([ "ok": 1, "session_id": session_id, "state": "removed",
        "released_owner_outputs": mapp(output_release)
            ? output_release["released"] : 0 ]);
}

mapping ensure_session(string session_id)
{
    if (!stringp(session_id) || session_id == "")
        return 0;
    if (!mapp(_sessions[session_id])) {
        _sessions[session_id] = create_gateway_session(session_id);
        if (!mapp(_sessions[session_id]))
            return 0;
    }
    return _sessions[session_id];
}

mapping open_session(string session_id, mapping meta)
{
    mapping session;

    session = ensure_session(session_id);
    if (!mapp(session))
        return 0;

    if (!update_session_service_route(session))
        return 0;
    session["state"] = "open";
    session["last_active"] = time();
    if (mapp(meta) && sizeof(meta) > 0)
        session["meta"] = copy(meta);
    return session;
}

void touch_session(string session_id)
{
    mapping session;

    session = ensure_session(session_id);
    if (!mapp(session))
        return;
    session["last_active"] = time();
}

int bind_session_target(string session_id, object target, string state)
{
    mapping session;
    mapping output_release;
    mapping reservation_release;
    object previous_target;

    session = ensure_session(session_id);
    if (!mapp(session) || !objectp(target))
        return 0;

    if (!update_session_service_route(session))
        return 0;
    previous_target = objectp(session["target"]) ? session["target"] : 0;
    if (objectp(previous_target) && previous_target != target) {
        /* L-46：先收口旧目标的未完成输出与残留 reservation；任何未确认
         * 释放都中止交接并保持旧绑定一致，不先删唯一旧状态。 */
        output_release = OWNER_SESSION_FIFO_D->release_session_outputs(
            session_id, "gateway session target rebound");
        if (!mapp(output_release) || output_release["failed"] > 0) {
            record_owner_protocol_output("release_failed");
            return 0;
        }
        reservation_release = release_session_protocol_reservations(
            session, previous_target);
        if (mapp(reservation_release) && reservation_release["failed"] > 0) {
            record_owner_protocol_output("release_failed");
            return 0;
        }
    }

    /* 先落新绑定；失败时旧绑定完好，保持“内存/持久一致”。 */
    if (!set_object_session(target, session_id,
        stringp(state) && state != "" ? state : "bound"))
        return 0;
    /* L-46：新绑定提交后再解绑旧目标；旧目标解绑失败则回滚新绑定，
     * 维持旧绑定可用，而不是留下悬空会话。 */
    if (objectp(previous_target) && previous_target != target
        && !clear_object_session(previous_target)) {
        if (!clear_object_session(target))
            log_file("system/gateway_session", sprintf(
                "%s session binding conflict: session=%s previous=%s target=%s\n",
                ctime(time()), session_id,
                get_object_id(previous_target) || file_name(previous_target),
                get_object_id(target) || file_name(target)));
        return 0;
    }
    session["bound_once"] = 1;
    session["target"] = target;
    session["state"] = stringp(state) && state != "" ? state : "bound";
    session["last_active"] = time();
    refresh_session_owner_snapshot(session, target);
    store_resume_record(session, target);
    /* Binding makes only this session's pending target deliverable. */
    CLIENT_SYNC_D->flush_player_command_sync(target);
    return 1;
}

int handoff_session(string session_id, object from, object to, string state)
{
    mapping session;
    mapping output_release;
    mapping reservation_release;
    object previous_target;

    session = ensure_session(session_id);
    if (!mapp(session) || !objectp(to))
        return 0;

    if (!update_session_service_route(session))
        return 0;
    previous_target = objectp(session["target"]) ? session["target"] : 0;
    if (objectp(previous_target) && previous_target != to) {
        /* L-46：旧目标的未完成输出与残留 reservation 必须先确认收口；
         * 任一失败即中止交接，保持旧绑定一致（不先删唯一旧状态）。 */
        output_release = OWNER_SESSION_FIFO_D->release_session_outputs(
            session_id, "gateway session target transitioned");
        if (!mapp(output_release) || output_release["failed"] > 0) {
            record_owner_protocol_output("release_failed");
            return 0;
        }
        reservation_release = release_session_protocol_reservations(
            session, previous_target);
        if (mapp(reservation_release) && reservation_release["failed"] > 0) {
            record_owner_protocol_output("release_failed");
            return 0;
        }
    }
    if (objectp(from) && from != previous_target
        && !clear_object_session(from))
        return 0;
    /* 先落新绑定；失败时旧绑定完好。 */
    if (!set_object_session(to, session_id,
        stringp(state) && state != "" ? state : "player"))
        return 0;
    /* L-46：新绑定提交后再解绑旧目标；旧目标解绑失败则回滚新绑定。 */
    if (objectp(previous_target) && previous_target != to
        && !clear_object_session(previous_target)) {
        if (!clear_object_session(to))
            log_file("system/gateway_session", sprintf(
                "%s session handoff binding conflict: session=%s previous=%s target=%s\n",
                ctime(time()), session_id,
                get_object_id(previous_target) || file_name(previous_target),
                get_object_id(to) || file_name(to)));
        return 0;
    }
    session["bound_once"] = 1;
    session["target"] = to;
    session["state"] = stringp(state) && state != "" ? state : "player";
    session["last_active"] = time();
    refresh_session_owner_snapshot(session, to);
    store_resume_record(session, to);
    return 1;
}

mapping close_session(string session_id)
{
    return remove_session(session_id, 1);
}

/* logout 生命周期 handler 包装（player 签名统一）——查玩家会话
 * 并关闭；无会话时无操作。输出释放未完成时返回非成功，
 * 让 PLAYER_LIFECYCLE_D 保留 pending 清理而不是继续 save/destruct。 */
mapping handle_player_logout(object player)
{
    string session_id;
    mapping closed;

    if (!objectp(player))
        return ([ "ok": 0, "error": "player unavailable" ]);
    session_id = query_object_session_id(player);
    if (stringp(session_id) && session_id != "") {
        closed = close_session(session_id);
        if (!mapp(closed) || closed["ok"] != 1)
            return ([ "ok": 0, "state": "gateway_release_pending",
                "session_id": session_id,
                "error": mapp(closed) ? closed["error"]
                    : "gateway session cleanup failed",
                "cleanup": closed ]);
    }
    return ([ "ok": 1, "state": "gateway_closed" ]);
}

void terminate_session_without_resume(string session_id)
{
    remove_session(session_id, 0);
}

mapping query_session(string session_id)
{
    if (!stringp(session_id) || session_id == "")
        return 0;
    return _sessions[session_id];
}

int update_session_owner_scope(string session_id, string owner_scope_id)
{
    mapping session;

    if (!stringp(session_id) || session_id == "")
        return 0;
    if (!stringp(owner_scope_id) || owner_scope_id == "")
        return 0;

    session = ensure_session(session_id);
    if (!mapp(session))
        return 0;

    if (!update_session_service_route(session))
        return 0;
    session["owner_scope"] = owner_scope_id;
    session["route_updated_at"] = time();
    return 1;
}

string query_session_owner_scope(string session_id)
{
    mapping session;

    session = query_session(session_id);
    if (!mapp(session) || !stringp(session["owner_scope"]))
        return "";
    return session["owner_scope"];
}

mapping query_session_route_snapshot(string session_id)
{
    mapping session;

    session = query_session(session_id);
    if (!mapp(session))
        return 0;

    return ([
        "session_id": session_id,
        "owner_scope_id": stringp(session["owner_scope"]) ? session["owner_scope"] : "",
        "state": stringp(session["state"]) ? session["state"] : "",
        "service_owner_id": stringp(session["service_owner_id"]) ? session["service_owner_id"] : "",
        "service_policy": stringp(session["service_policy"]) ? session["service_policy"] : "",
        "service_map_v2": session["service_map_v2"] ? 1 : 0,
        "direct_cross_owner_write": 0,
        "updated_at": intp(session["route_updated_at"]) ? session["route_updated_at"] : session["last_active"],
    ]);
}

void record_xk_protocol_tail_event(string tail_key)
{
    switch (tail_key) {
    case "xk_protocol_send_tail_25ms_count":
    case "xk_protocol_build_tail_25ms_count":
    case "xk_protocol_encode_tail_25ms_count":
    case "xk_protocol_metadata_tail_25ms_count":
    case "xk_protocol_json_tail_25ms_count":
    case "xk_protocol_frame_tail_25ms_count":
        break;
    default:
        return;
    }

    _xk_protocol_tail_probe[tail_key] =
        (intp(_xk_protocol_tail_probe[tail_key]) ? _xk_protocol_tail_probe[tail_key] : 0) + 1;
}

void record_xk_protocol_encode_tail_cpu_sample(int cpu_elapsed_ns)
{
    int cpu_elapsed_us;

    if (!intp(cpu_elapsed_ns) || cpu_elapsed_ns < 0) {
        _xk_protocol_tail_probe["xk_protocol_encode_tail_cpu_unavailable_count"] =
            (intp(_xk_protocol_tail_probe["xk_protocol_encode_tail_cpu_unavailable_count"])
                ? _xk_protocol_tail_probe["xk_protocol_encode_tail_cpu_unavailable_count"] : 0) + 1;
        return;
    }

    cpu_elapsed_us = cpu_elapsed_ns / 1000;
    _xk_protocol_tail_probe["xk_protocol_encode_tail_cpu_samples"] =
        (intp(_xk_protocol_tail_probe["xk_protocol_encode_tail_cpu_samples"])
            ? _xk_protocol_tail_probe["xk_protocol_encode_tail_cpu_samples"] : 0) + 1;
    _xk_protocol_tail_probe["xk_protocol_encode_tail_cpu_total_us"] =
        (intp(_xk_protocol_tail_probe["xk_protocol_encode_tail_cpu_total_us"])
            ? _xk_protocol_tail_probe["xk_protocol_encode_tail_cpu_total_us"] : 0) + cpu_elapsed_us;
    if (!intp(_xk_protocol_tail_probe["xk_protocol_encode_tail_cpu_max_us"])
        || cpu_elapsed_us > _xk_protocol_tail_probe["xk_protocol_encode_tail_cpu_max_us"])
        _xk_protocol_tail_probe["xk_protocol_encode_tail_cpu_max_us"] = cpu_elapsed_us;
}

mapping query_xk_protocol_tail_probe_status()
{
    return ([
        "xk_protocol_send_count": _xk_protocol_send_count,
        "xk_protocol_send_tail_25ms_count":
            intp(_xk_protocol_tail_probe["xk_protocol_send_tail_25ms_count"])
                ? _xk_protocol_tail_probe["xk_protocol_send_tail_25ms_count"] : 0,
        "xk_protocol_build_tail_25ms_count":
            intp(_xk_protocol_tail_probe["xk_protocol_build_tail_25ms_count"])
                ? _xk_protocol_tail_probe["xk_protocol_build_tail_25ms_count"] : 0,
        "xk_protocol_encode_tail_25ms_count":
            intp(_xk_protocol_tail_probe["xk_protocol_encode_tail_25ms_count"])
                ? _xk_protocol_tail_probe["xk_protocol_encode_tail_25ms_count"] : 0,
        "xk_protocol_metadata_tail_25ms_count":
            intp(_xk_protocol_tail_probe["xk_protocol_metadata_tail_25ms_count"])
                ? _xk_protocol_tail_probe["xk_protocol_metadata_tail_25ms_count"] : 0,
        "xk_protocol_json_tail_25ms_count":
            intp(_xk_protocol_tail_probe["xk_protocol_json_tail_25ms_count"])
                ? _xk_protocol_tail_probe["xk_protocol_json_tail_25ms_count"] : 0,
        "xk_protocol_frame_tail_25ms_count":
            intp(_xk_protocol_tail_probe["xk_protocol_frame_tail_25ms_count"])
                ? _xk_protocol_tail_probe["xk_protocol_frame_tail_25ms_count"] : 0,
        "xk_protocol_encode_tail_cpu_samples":
            intp(_xk_protocol_tail_probe["xk_protocol_encode_tail_cpu_samples"])
                ? _xk_protocol_tail_probe["xk_protocol_encode_tail_cpu_samples"] : 0,
        "xk_protocol_encode_tail_cpu_total_us":
            intp(_xk_protocol_tail_probe["xk_protocol_encode_tail_cpu_total_us"])
                ? _xk_protocol_tail_probe["xk_protocol_encode_tail_cpu_total_us"] : 0,
        "xk_protocol_encode_tail_cpu_max_us":
            intp(_xk_protocol_tail_probe["xk_protocol_encode_tail_cpu_max_us"])
                ? _xk_protocol_tail_probe["xk_protocol_encode_tail_cpu_max_us"] : 0,
        "xk_protocol_encode_tail_cpu_unavailable_count":
            intp(_xk_protocol_tail_probe["xk_protocol_encode_tail_cpu_unavailable_count"])
                ? _xk_protocol_tail_probe["xk_protocol_encode_tail_cpu_unavailable_count"] : 0,
    ]);
}

private void record_xk_protocol_phase_timing_value(string phase, int elapsed_ns)
{
    mapping timing;
    int elapsed_us;

    if (!stringp(phase) || phase == "" || !intp(elapsed_ns) || elapsed_ns < 0)
        return;
    if (!mapp(_xk_protocol_phase_timing))
        _xk_protocol_phase_timing = ([]);

    timing = mapp(_xk_protocol_phase_timing[phase])
        ? _xk_protocol_phase_timing[phase] : ([]);
    elapsed_us = elapsed_ns / 1000;
    timing["samples"] = (intp(timing["samples"]) ? timing["samples"] : 0) + 1;
    timing["total_us"] = (intp(timing["total_us"])
        ? timing["total_us"] : 0) + elapsed_us;
    if (!intp(timing["max_us"]) || elapsed_us > timing["max_us"])
        timing["max_us"] = elapsed_us;
    _xk_protocol_phase_timing[phase] = timing;
}

private string xk_protocol_type_metric_key(string protocol_type)
{
    switch (protocol_type) {
    case "BACH": return "bach";
    case "MAPS": return "maps";
    case "DLOG": return "dlog";
    case "IVTY": return "ivty";
    case "SKLS": return "skls";
    default: return "";
    }
}

private void record_xk_protocol_type_encode_timing(string protocol_type,
    int elapsed_ns, int payload_bytes)
{
    mapping timing;
    string metric_key;
    int elapsed_us;

    metric_key = xk_protocol_type_metric_key(protocol_type);
    if (metric_key == "" || !intp(elapsed_ns) || elapsed_ns < 0
        || !intp(payload_bytes) || payload_bytes < 0)
        return;
    if (!mapp(_xk_protocol_type_encode_timing))
        _xk_protocol_type_encode_timing = ([]);

    timing = mapp(_xk_protocol_type_encode_timing[metric_key])
        ? _xk_protocol_type_encode_timing[metric_key] : ([]);
    elapsed_us = elapsed_ns / 1000;
    timing["samples"] = (timing["samples"] || 0) + 1;
    timing["total_us"] = (timing["total_us"] || 0) + elapsed_us;
    timing["bytes_total"] = (timing["bytes_total"] || 0) + payload_bytes;
    if (!intp(timing["max_us"]) || elapsed_us > timing["max_us"])
        timing["max_us"] = elapsed_us;
    if (!intp(timing["bytes_max"]) || payload_bytes > timing["bytes_max"])
        timing["bytes_max"] = payload_bytes;
    _xk_protocol_type_encode_timing[metric_key] = timing;
}

/** 批量记录一次 XK 发送的全样本阶段耗时，避免逐阶段跨 daemon 调用。 */
void record_xk_protocol_phase_timings(mapping timings_ns)
{
    string phase;
    int elapsed_ns;
    int session_trace_cpu_unavailable;

    if (!mapp(timings_ns))
        return;
    foreach (phase in ({ "metadata", "json", "encode", "frame" })) {
        elapsed_ns = timings_ns[phase];
        if (intp(elapsed_ns) && elapsed_ns >= 0)
            record_xk_protocol_phase_timing_value(phase, elapsed_ns);
    }

    elapsed_ns = timings_ns["encode_cpu"];
    if (!intp(elapsed_ns) || elapsed_ns < 0) {
        _xk_protocol_phase_timing["encode_cpu_unavailable_count"] =
            (intp(_xk_protocol_phase_timing["encode_cpu_unavailable_count"])
                ? _xk_protocol_phase_timing["encode_cpu_unavailable_count"] : 0) + 1;
    } else
        record_xk_protocol_phase_timing_value("encode_cpu", elapsed_ns);
    record_xk_protocol_type_encode_timing(timings_ns["protocol_type"],
        elapsed_ns, timings_ns["payload_bytes"]);

    elapsed_ns = timings_ns["frame_cpu"];
    if (!intp(elapsed_ns) || elapsed_ns < 0) {
        _xk_protocol_phase_timing["frame_cpu_unavailable_count"] =
            (_xk_protocol_phase_timing["frame_cpu_unavailable_count"] || 0) + 1;
    } else
        record_xk_protocol_phase_timing_value("frame_cpu", elapsed_ns);

    session_trace_cpu_unavailable = 0;
    elapsed_ns = timings_ns["session_trace_cpu"];
    if (!intp(elapsed_ns) || elapsed_ns < 0)
        session_trace_cpu_unavailable = 1;
    else
        record_xk_protocol_phase_timing_value("session_trace_cpu", elapsed_ns);
    if (session_trace_cpu_unavailable)
        _xk_protocol_phase_timing["session_trace_cpu_unavailable_count"] =
            (_xk_protocol_phase_timing["session_trace_cpu_unavailable_count"] || 0) + 1;
}

/** 返回 ownercheck/loadtest 使用的 XK 全样本阶段计时快照。 */
mapping query_xk_protocol_phase_timing_status()
{
    mapping status;
    mapping timing;
    string phase;
    string prefix;
    string protocol_type;

    status = ([]);
    foreach (phase in ({ "metadata", "json", "encode", "encode_cpu", "frame", "frame_cpu",
        "session_trace_cpu" })) {
        timing = mapp(_xk_protocol_phase_timing[phase])
            ? _xk_protocol_phase_timing[phase] : ([]);
        prefix = "xk_protocol_" + phase;
        status[prefix + "_samples"] = intp(timing["samples"])
            ? timing["samples"] : 0;
        status[prefix + "_total_us"] = intp(timing["total_us"])
            ? timing["total_us"] : 0;
        status[prefix + "_max_us"] = intp(timing["max_us"])
            ? timing["max_us"] : 0;
    }
    status["xk_protocol_encode_cpu_unavailable_count"] =
        intp(_xk_protocol_phase_timing["encode_cpu_unavailable_count"])
            ? _xk_protocol_phase_timing["encode_cpu_unavailable_count"] : 0;
    status["xk_protocol_frame_cpu_unavailable_count"] =
        _xk_protocol_phase_timing["frame_cpu_unavailable_count"] || 0;
    status["xk_protocol_session_trace_cpu_unavailable_count"] =
        _xk_protocol_phase_timing["session_trace_cpu_unavailable_count"] || 0;
    foreach (protocol_type in ({ "bach", "maps", "dlog", "ivty", "skls" })) {
        timing = mapp(_xk_protocol_type_encode_timing[protocol_type])
            ? _xk_protocol_type_encode_timing[protocol_type] : ([]);
        prefix = "xk_protocol_" + protocol_type + "_encode_cpu";
        status[prefix + "_samples"] = timing["samples"] || 0;
        status[prefix + "_total_us"] = timing["total_us"] || 0;
        status[prefix + "_max_us"] = timing["max_us"] || 0;
        status["xk_protocol_" + protocol_type + "_payload_bytes_total"] =
            timing["bytes_total"] || 0;
        status["xk_protocol_" + protocol_type + "_payload_bytes_max"] =
            timing["bytes_max"] || 0;
    }
    return status;
}

private void record_look_phase_tail_value(string prefix, int elapsed_ns)
{
    string max_key;
    string samples_key;
    string total_key;
    int elapsed_us;

    if (!stringp(prefix) || prefix == "" || !intp(elapsed_ns) || elapsed_ns < 0)
        return;

    elapsed_us = elapsed_ns / 1000;
    samples_key = prefix + "_samples";
    total_key = prefix + "_total_us";
    max_key = prefix + "_max_us";
    _look_phase_tail_probe[samples_key] =
        (intp(_look_phase_tail_probe[samples_key]) ? _look_phase_tail_probe[samples_key] : 0) + 1;
    _look_phase_tail_probe[total_key] =
        (intp(_look_phase_tail_probe[total_key]) ? _look_phase_tail_probe[total_key] : 0)
        + elapsed_us;
    if (!intp(_look_phase_tail_probe[max_key])
        || elapsed_us > _look_phase_tail_probe[max_key])
        _look_phase_tail_probe[max_key] = elapsed_us;
}

void record_look_phase_tail(mapping timing_ns)
{
    string phase;

    if (!mapp(timing_ns) || !intp(timing_ns["total"])
        || timing_ns["total"] < 25000000)
        return;
    foreach (phase in ({
        "total", "room_view", "read_summary", "render", "emit",
        "room_view_ensure", "room_view_ensure_owner_route", "room_view_ensure_gate",
        "room_view_ensure_entities", "room_view_shared", "room_view_player_filter",
        "room_view_quest_projection", "room_view_finalize",
    })) {
        if (!intp(timing_ns[phase]) || timing_ns[phase] < 0)
            return;
    }

    record_look_phase_tail_value("look_phase_tail", timing_ns["total"]);
    record_look_phase_tail_value("look_phase_room_view_tail", timing_ns["room_view"]);
    record_look_phase_tail_value("look_phase_read_summary_tail", timing_ns["read_summary"]);
    record_look_phase_tail_value("look_phase_render_tail", timing_ns["render"]);
    record_look_phase_tail_value("look_phase_emit_tail", timing_ns["emit"]);
    if (timing_ns["room_view"] >= 25000000) {
        record_look_phase_tail_value("look_phase_room_view_ensure_tail",
            timing_ns["room_view_ensure"]);
        record_look_phase_tail_value("look_phase_room_view_ensure_owner_route_tail",
            timing_ns["room_view_ensure_owner_route"]);
        record_look_phase_tail_value("look_phase_room_view_ensure_gate_tail",
            timing_ns["room_view_ensure_gate"]);
        record_look_phase_tail_value("look_phase_room_view_ensure_entities_tail",
            timing_ns["room_view_ensure_entities"]);
        record_look_phase_tail_value("look_phase_room_view_shared_tail",
            timing_ns["room_view_shared"]);
        record_look_phase_tail_value("look_phase_room_view_player_filter_tail",
            timing_ns["room_view_player_filter"]);
        record_look_phase_tail_value("look_phase_room_view_quest_projection_tail",
            timing_ns["room_view_quest_projection"]);
        record_look_phase_tail_value("look_phase_room_view_finalize_tail",
            timing_ns["room_view_finalize"]);
    }
}

mapping query_look_phase_tail_status()
{
    return copy(_look_phase_tail_probe);
}

int record_owner_protocol_projection(string projection_kind, string outcome)
{
    string field_name;

    switch (projection_kind) {
    case "inventory":
    case "hp_dialog":
    case "score_dialog":
    case "map_grid":
        break;
    default:
        return 0;
    }
    switch (outcome) {
    case "owner_completed":
    case "inline_completed":
    case "sync_fallback":
    case "owner_stale":
    case "owner_failed":
    case "owner_cancelled":
    case "current_hit":
    case "pending_cached":
    case "preencoded_hit":
    case "preencoded_sync_fallback":
    case "preencoded_failed":
        break;
    default:
        return 0;
    }
    if (outcome == "inline_completed" && projection_kind != "inventory")
        return 0;

    field_name = "owner_projection_" + projection_kind + "_" + outcome + "_count";
    _owner_protocol_projection_counts[field_name] =
        (intp(_owner_protocol_projection_counts[field_name])
            ? _owner_protocol_projection_counts[field_name] : 0) + 1;
    return 1;
}

int record_owner_protocol_projection_publish_guard(string outcome)
{
    string field_name;

    if (outcome != "checked" && outcome != "rejected"
        && outcome != "committed")
        return 0;
    field_name = "owner_projection_publish_guard_" + outcome + "_count";
    _owner_protocol_projection_counts[field_name] =
        (intp(_owner_protocol_projection_counts[field_name])
            ? _owner_protocol_projection_counts[field_name] : 0) + 1;
    return 1;
}

mapping query_owner_protocol_projection_status()
{
    return ([
        "owner_projection_publish_guard_checked_count":
            intp(_owner_protocol_projection_counts["owner_projection_publish_guard_checked_count"])
                ? _owner_protocol_projection_counts["owner_projection_publish_guard_checked_count"] : 0,
        "owner_projection_publish_guard_rejected_count":
            intp(_owner_protocol_projection_counts["owner_projection_publish_guard_rejected_count"])
                ? _owner_protocol_projection_counts["owner_projection_publish_guard_rejected_count"] : 0,
        "owner_projection_publish_guard_committed_count":
            intp(_owner_protocol_projection_counts["owner_projection_publish_guard_committed_count"])
                ? _owner_protocol_projection_counts["owner_projection_publish_guard_committed_count"] : 0,
        "owner_projection_inventory_owner_completed_count":
            intp(_owner_protocol_projection_counts["owner_projection_inventory_owner_completed_count"])
                ? _owner_protocol_projection_counts["owner_projection_inventory_owner_completed_count"] : 0,
        "owner_projection_inventory_inline_completed_count":
            intp(_owner_protocol_projection_counts["owner_projection_inventory_inline_completed_count"])
                ? _owner_protocol_projection_counts["owner_projection_inventory_inline_completed_count"] : 0,
        "owner_projection_inventory_sync_fallback_count":
            intp(_owner_protocol_projection_counts["owner_projection_inventory_sync_fallback_count"])
                ? _owner_protocol_projection_counts["owner_projection_inventory_sync_fallback_count"] : 0,
        "owner_projection_inventory_owner_stale_count":
            intp(_owner_protocol_projection_counts["owner_projection_inventory_owner_stale_count"])
                ? _owner_protocol_projection_counts["owner_projection_inventory_owner_stale_count"] : 0,
        "owner_projection_inventory_owner_failed_count":
            intp(_owner_protocol_projection_counts["owner_projection_inventory_owner_failed_count"])
                ? _owner_protocol_projection_counts["owner_projection_inventory_owner_failed_count"] : 0,
        "owner_projection_inventory_owner_cancelled_count":
            intp(_owner_protocol_projection_counts["owner_projection_inventory_owner_cancelled_count"])
                ? _owner_protocol_projection_counts["owner_projection_inventory_owner_cancelled_count"] : 0,
        "owner_projection_inventory_current_hit_count":
            intp(_owner_protocol_projection_counts["owner_projection_inventory_current_hit_count"])
                ? _owner_protocol_projection_counts["owner_projection_inventory_current_hit_count"] : 0,
        "owner_projection_inventory_pending_cached_count":
            intp(_owner_protocol_projection_counts["owner_projection_inventory_pending_cached_count"])
                ? _owner_protocol_projection_counts["owner_projection_inventory_pending_cached_count"] : 0,
        "owner_projection_inventory_preencoded_hit_count":
            intp(_owner_protocol_projection_counts["owner_projection_inventory_preencoded_hit_count"])
                ? _owner_protocol_projection_counts["owner_projection_inventory_preencoded_hit_count"] : 0,
        "owner_projection_inventory_preencoded_sync_fallback_count":
            intp(_owner_protocol_projection_counts["owner_projection_inventory_preencoded_sync_fallback_count"])
                ? _owner_protocol_projection_counts["owner_projection_inventory_preencoded_sync_fallback_count"] : 0,
        "owner_projection_inventory_preencoded_failed_count":
            intp(_owner_protocol_projection_counts["owner_projection_inventory_preencoded_failed_count"])
                ? _owner_protocol_projection_counts["owner_projection_inventory_preencoded_failed_count"] : 0,
        "owner_projection_hp_dialog_owner_completed_count":
            intp(_owner_protocol_projection_counts["owner_projection_hp_dialog_owner_completed_count"])
                ? _owner_protocol_projection_counts["owner_projection_hp_dialog_owner_completed_count"] : 0,
        "owner_projection_hp_dialog_sync_fallback_count":
            intp(_owner_protocol_projection_counts["owner_projection_hp_dialog_sync_fallback_count"])
                ? _owner_protocol_projection_counts["owner_projection_hp_dialog_sync_fallback_count"] : 0,
        "owner_projection_hp_dialog_owner_stale_count":
            intp(_owner_protocol_projection_counts["owner_projection_hp_dialog_owner_stale_count"])
                ? _owner_protocol_projection_counts["owner_projection_hp_dialog_owner_stale_count"] : 0,
        "owner_projection_hp_dialog_owner_failed_count":
            intp(_owner_protocol_projection_counts["owner_projection_hp_dialog_owner_failed_count"])
                ? _owner_protocol_projection_counts["owner_projection_hp_dialog_owner_failed_count"] : 0,
        "owner_projection_hp_dialog_owner_cancelled_count":
            intp(_owner_protocol_projection_counts["owner_projection_hp_dialog_owner_cancelled_count"])
                ? _owner_protocol_projection_counts["owner_projection_hp_dialog_owner_cancelled_count"] : 0,
        "owner_projection_hp_dialog_current_hit_count":
            intp(_owner_protocol_projection_counts["owner_projection_hp_dialog_current_hit_count"])
                ? _owner_protocol_projection_counts["owner_projection_hp_dialog_current_hit_count"] : 0,
        "owner_projection_hp_dialog_pending_cached_count":
            intp(_owner_protocol_projection_counts["owner_projection_hp_dialog_pending_cached_count"])
                ? _owner_protocol_projection_counts["owner_projection_hp_dialog_pending_cached_count"] : 0,
        "owner_projection_hp_dialog_preencoded_hit_count":
            intp(_owner_protocol_projection_counts["owner_projection_hp_dialog_preencoded_hit_count"])
                ? _owner_protocol_projection_counts["owner_projection_hp_dialog_preencoded_hit_count"] : 0,
        "owner_projection_hp_dialog_preencoded_sync_fallback_count":
            intp(_owner_protocol_projection_counts["owner_projection_hp_dialog_preencoded_sync_fallback_count"])
                ? _owner_protocol_projection_counts["owner_projection_hp_dialog_preencoded_sync_fallback_count"] : 0,
        "owner_projection_hp_dialog_preencoded_failed_count":
            intp(_owner_protocol_projection_counts["owner_projection_hp_dialog_preencoded_failed_count"])
                ? _owner_protocol_projection_counts["owner_projection_hp_dialog_preencoded_failed_count"] : 0,
        "owner_projection_score_dialog_owner_completed_count":
            intp(_owner_protocol_projection_counts["owner_projection_score_dialog_owner_completed_count"])
                ? _owner_protocol_projection_counts["owner_projection_score_dialog_owner_completed_count"] : 0,
        "owner_projection_score_dialog_sync_fallback_count":
            intp(_owner_protocol_projection_counts["owner_projection_score_dialog_sync_fallback_count"])
                ? _owner_protocol_projection_counts["owner_projection_score_dialog_sync_fallback_count"] : 0,
        "owner_projection_score_dialog_owner_stale_count":
            intp(_owner_protocol_projection_counts["owner_projection_score_dialog_owner_stale_count"])
                ? _owner_protocol_projection_counts["owner_projection_score_dialog_owner_stale_count"] : 0,
        "owner_projection_score_dialog_owner_failed_count":
            intp(_owner_protocol_projection_counts["owner_projection_score_dialog_owner_failed_count"])
                ? _owner_protocol_projection_counts["owner_projection_score_dialog_owner_failed_count"] : 0,
        "owner_projection_score_dialog_owner_cancelled_count":
            intp(_owner_protocol_projection_counts["owner_projection_score_dialog_owner_cancelled_count"])
                ? _owner_protocol_projection_counts["owner_projection_score_dialog_owner_cancelled_count"] : 0,
        "owner_projection_score_dialog_current_hit_count":
            intp(_owner_protocol_projection_counts["owner_projection_score_dialog_current_hit_count"])
                ? _owner_protocol_projection_counts["owner_projection_score_dialog_current_hit_count"] : 0,
        "owner_projection_score_dialog_pending_cached_count":
            intp(_owner_protocol_projection_counts["owner_projection_score_dialog_pending_cached_count"])
                ? _owner_protocol_projection_counts["owner_projection_score_dialog_pending_cached_count"] : 0,
        "owner_projection_score_dialog_preencoded_hit_count":
            intp(_owner_protocol_projection_counts["owner_projection_score_dialog_preencoded_hit_count"])
                ? _owner_protocol_projection_counts["owner_projection_score_dialog_preencoded_hit_count"] : 0,
        "owner_projection_score_dialog_preencoded_sync_fallback_count":
            intp(_owner_protocol_projection_counts["owner_projection_score_dialog_preencoded_sync_fallback_count"])
                ? _owner_protocol_projection_counts["owner_projection_score_dialog_preencoded_sync_fallback_count"] : 0,
        "owner_projection_score_dialog_preencoded_failed_count":
            intp(_owner_protocol_projection_counts["owner_projection_score_dialog_preencoded_failed_count"])
                ? _owner_protocol_projection_counts["owner_projection_score_dialog_preencoded_failed_count"] : 0,
        "owner_projection_map_grid_owner_completed_count":
            intp(_owner_protocol_projection_counts["owner_projection_map_grid_owner_completed_count"])
                ? _owner_protocol_projection_counts["owner_projection_map_grid_owner_completed_count"] : 0,
        "owner_projection_map_grid_sync_fallback_count":
            intp(_owner_protocol_projection_counts["owner_projection_map_grid_sync_fallback_count"])
                ? _owner_protocol_projection_counts["owner_projection_map_grid_sync_fallback_count"] : 0,
        "owner_projection_map_grid_owner_stale_count":
            intp(_owner_protocol_projection_counts["owner_projection_map_grid_owner_stale_count"])
                ? _owner_protocol_projection_counts["owner_projection_map_grid_owner_stale_count"] : 0,
        "owner_projection_map_grid_owner_failed_count":
            intp(_owner_protocol_projection_counts["owner_projection_map_grid_owner_failed_count"])
                ? _owner_protocol_projection_counts["owner_projection_map_grid_owner_failed_count"] : 0,
        "owner_projection_map_grid_owner_cancelled_count":
            intp(_owner_protocol_projection_counts["owner_projection_map_grid_owner_cancelled_count"])
                ? _owner_protocol_projection_counts["owner_projection_map_grid_owner_cancelled_count"] : 0,
        "owner_projection_map_grid_current_hit_count":
            intp(_owner_protocol_projection_counts["owner_projection_map_grid_current_hit_count"])
                ? _owner_protocol_projection_counts["owner_projection_map_grid_current_hit_count"] : 0,
        "owner_projection_map_grid_pending_cached_count":
            intp(_owner_protocol_projection_counts["owner_projection_map_grid_pending_cached_count"])
                ? _owner_protocol_projection_counts["owner_projection_map_grid_pending_cached_count"] : 0,
        "owner_projection_map_grid_preencoded_hit_count":
            intp(_owner_protocol_projection_counts["owner_projection_map_grid_preencoded_hit_count"])
                ? _owner_protocol_projection_counts["owner_projection_map_grid_preencoded_hit_count"] : 0,
        "owner_projection_map_grid_preencoded_sync_fallback_count":
            intp(_owner_protocol_projection_counts["owner_projection_map_grid_preencoded_sync_fallback_count"])
                ? _owner_protocol_projection_counts["owner_projection_map_grid_preencoded_sync_fallback_count"] : 0,
        "owner_projection_map_grid_preencoded_failed_count":
            intp(_owner_protocol_projection_counts["owner_projection_map_grid_preencoded_failed_count"])
                ? _owner_protocol_projection_counts["owner_projection_map_grid_preencoded_failed_count"] : 0,
    ]);
}

private int owner_protocol_projection_timing_key_supported(string projection_kind,
    string phase)
{
    if (projection_kind != "inventory" && projection_kind != "hp_dialog"
        && projection_kind != "score_dialog" && projection_kind != "map_grid")
        return 0;
    return phase == "snapshot" || phase == "submit" || phase == "publish"
        || phase == "current_hit" || phase == "sync_fallback"
        || phase == "preencoded_send" || phase == "refresh_total"
        || phase == "snapshot_facts" || phase == "snapshot_actor"
        || phase == "live_token";
}

/** 记录 read-model 主线程阶段耗时，字段限定为低基数 kind/phase 白名单。 */
int record_owner_protocol_projection_timing(string projection_kind, string phase,
    int elapsed_ns)
{
    mapping projection_timing;
    mapping phase_timing;
    int elapsed_us;

    if (!_owner_protocol_projection_timing_enabled)
        return 0;
    if (!owner_protocol_projection_timing_key_supported(projection_kind, phase)
        || !intp(elapsed_ns) || elapsed_ns < 0)
        return 0;
    if (!mapp(_owner_protocol_projection_timing))
        _owner_protocol_projection_timing = ([]);

    elapsed_us = elapsed_ns / 1000;
    projection_timing = mapp(_owner_protocol_projection_timing[projection_kind])
        ? _owner_protocol_projection_timing[projection_kind] : ([]);
    phase_timing = mapp(projection_timing[phase])
        ? projection_timing[phase] : ([]);
    phase_timing["samples"] =
        (intp(phase_timing["samples"]) ? phase_timing["samples"] : 0) + 1;
    phase_timing["total_us"] =
        (intp(phase_timing["total_us"]) ? phase_timing["total_us"] : 0)
            + elapsed_us;
    if (!intp(phase_timing["max_us"]) || elapsed_us > phase_timing["max_us"])
        phase_timing["max_us"] = elapsed_us;
    projection_timing[phase] = phase_timing;
    _owner_protocol_projection_timing[projection_kind] = projection_timing;
    return 1;
}

int set_owner_protocol_projection_timing_enabled(int enabled)
{
    object caller;

    caller = previous_object();
    if (!objectp(caller) || base_name(caller) != "/cmds/admin/projectioncheck")
        return 0;
    _owner_protocol_projection_timing_enabled = enabled ? 1 : 0;
    if (!_owner_protocol_projection_timing_enabled)
        _owner_protocol_projection_timing = ([]);
    return 1;
}

/* The synchronized loadtest owns this narrow observability window. */
int set_loadtest_projection_timing_enabled(int enabled)
{
    object caller;

    caller = previous_object();
    if (!objectp(caller) || base_name(caller) != "/cmds/admin/ownercheck")
        return 0;
    _owner_protocol_projection_timing_enabled = enabled ? 1 : 0;
    return 1;
}

int reset_loadtest_projection_timing()
{
    object caller;

    caller = previous_object();
    if (!objectp(caller) || base_name(caller) != "/cmds/admin/ownercheck")
        return 0;
    _owner_protocol_projection_timing = ([]);
    return 1;
}

int query_owner_protocol_projection_timing_enabled()
{
    return _owner_protocol_projection_timing_enabled ? 1 : 0;
}

/** 返回 ownercheck/loadtest 使用的扁平化阶段计时快照。 */
mapping query_owner_protocol_projection_timing_status()
{
    mapping status;
    string projection_kind;
    string phase;
    string prefix;
    string field_name;
    string metric;
    mapping projection_timing;
    mapping phase_timing;

    status = ([
        "owner_projection_timing_enabled":
            _owner_protocol_projection_timing_enabled ? 1 : 0,
    ]);
    foreach (projection_kind in ({ "inventory", "hp_dialog", "score_dialog", "map_grid" })) {
        foreach (phase in ({ "snapshot", "submit", "publish", "current_hit",
            "sync_fallback", "preencoded_send", "refresh_total",
            "snapshot_facts", "snapshot_actor", "live_token" })) {
            prefix = "owner_projection_" + projection_kind + "_" + phase;
            projection_timing = mapp(
                _owner_protocol_projection_timing[projection_kind])
                ? _owner_protocol_projection_timing[projection_kind] : ([]);
            phase_timing = mapp(projection_timing[phase])
                ? projection_timing[phase] : ([]);
            foreach (metric in ({ "samples", "total_us", "max_us" })) {
                field_name = prefix + "_" + metric;
                status[field_name] = intp(phase_timing[metric])
                    ? phase_timing[metric] : 0;
            }
        }
    }
    return status;
}

int record_session_output_reservation(object target, string packet_type, mapping meta,
    int reservation_id)
{
    mapping session;
    mapping reservations;
    int reservation_key;
    string session_id;
    int server_seq;
    int queue_sequence;
    int last_server_seq;

    if (!objectp(target) || !stringp(packet_type) || packet_type == ""
        || !mapp(meta) || !intp(reservation_id) || reservation_id <= 0)
        return 0;

    session_id = query_object_session_id(target);
    session = stringp(session_id) && session_id != "" ? query_session(session_id) : 0;
    if (!mapp(session))
        return 0;

    reservation_key = reservation_id;
    reservations = mapp(session["protocol_reservations"])
        ? session["protocol_reservations"] : ([]);
    if (mapp(reservations[reservation_key]))
        return 0;

    server_seq = intp(meta["server_seq"]) && meta["server_seq"] > 0 ? meta["server_seq"] : 0;
    queue_sequence = intp(meta["queue_sequence"]) && meta["queue_sequence"] > 0
        ? meta["queue_sequence"] : 0;
    /* raw text 只有 FIFO 排队序号，不占用也不推进 XK 协议序号水位。 */
    if (server_seq <= 0 && queue_sequence <= 0)
        return 0;
    if (server_seq > 0) {
        last_server_seq = intp(session["last_server_seq"]) ? session["last_server_seq"] : 0;
        if (last_server_seq > 0 && server_seq <= last_server_seq) {
            session["fifo_violations"] =
                (intp(session["fifo_violations"]) ? session["fifo_violations"] : 0) + 1;
            return 0;
        }
    }

    reservations[reservation_key] = ([
        "reservation_id": reservation_id,
        "packet_type": packet_type,
        "server_seq": server_seq,
        "queue_sequence": queue_sequence,
        "stream": stringp(meta["stream"]) ? meta["stream"] : "",
        "reserved_at": time(),
    ]);
    session["protocol_reservations"] = reservations;
    if (meta["owner_fifo_reservation"]) {
        session["owner_output_reservations"] = mapp(
            session["owner_output_reservations"])
                ? session["owner_output_reservations"] : ([]);
        session["owner_output_reservations"][reservation_key] =
            reservations[reservation_key];
        session["pending_owner_outputs"] =
            (intp(session["pending_owner_outputs"])
                ? session["pending_owner_outputs"] : 0) + 1;
    }
    session["pending_protocol_reservations"] =
        (intp(session["pending_protocol_reservations"])
            ? session["pending_protocol_reservations"] : 0) + 1;
    if (server_seq > 0)
        session["last_server_seq"] = server_seq;
    session["reservation_updated_at"] = time();
    return 1;
}

int record_session_output_reservation_batch(object *targets,
    int *reservation_ids, int first_server_seq, int *reused)
{
    mapping reservations;
    mapping session;
    mapping seen_sessions;
    mapping *sessions;
    int reservation_key;
    string session_id;
    int index;
    int last_server_seq;
    int server_seq;

    if (!arrayp(targets) || sizeof(targets) == 0
        || !arrayp(reservation_ids) || first_server_seq <= 0
        || !arrayp(reused) || sizeof(reservation_ids) != sizeof(targets)
        || sizeof(reused) != sizeof(targets))
        return 0;

    sessions = allocate(sizeof(targets));
    seen_sessions = ([]);
    for (index = 0; index < sizeof(targets); index++) {
        if (reused[index])
            continue;
        server_seq = first_server_seq + index * 2;
        if (!objectp(targets[index]) || reservation_ids[index] <= 0
            || server_seq <= 0)
            return 0;
        session_id = query_object_session_id(targets[index]);
        if (!stringp(session_id) || session_id == ""
            || seen_sessions[session_id])
            return 0;
        seen_sessions[session_id] = 1;
        session = query_session(session_id);
        if (!mapp(session))
            return 0;
        reservations = mapp(session["protocol_reservations"])
            ? session["protocol_reservations"] : ([]);
        reservation_key = reservation_ids[index];
        last_server_seq = intp(session["last_server_seq"])
            ? session["last_server_seq"] : 0;
        if (mapp(reservations[reservation_key])
            || (last_server_seq > 0
                && server_seq <= last_server_seq))
            return 0;
        sessions[index] = session;
    }

    for (index = 0; index < sizeof(targets); index++) {
        if (reused[index])
            continue;
        session = sessions[index];
        reservations = mapp(session["protocol_reservations"])
            ? session["protocol_reservations"] : ([]);
        reservation_key = reservation_ids[index];
        server_seq = first_server_seq + index * 2;
        reservations[reservation_key] = ([
            "reservation_id": reservation_ids[index],
            "packet_type": "MSGE",
            "server_seq": server_seq,
            "stream": "message",
            "reserved_at": time(),
        ]);
        session["protocol_reservations"] = reservations;
        session["pending_protocol_reservations"] =
            (intp(session["pending_protocol_reservations"])
                ? session["pending_protocol_reservations"] : 0) + 1;
        session["last_server_seq"] = server_seq;
        session["reservation_updated_at"] = time();
    }
    return 1;
}

int record_session_output_sparse_reservation_batch(object *targets,
    int *reservation_ids, int *server_seqs)
{
    mapping reservations;
    mapping session;
    mapping seen_sessions;
    mapping *sessions;
    int reservation_key;
    string session_id;
    int index;
    int last_server_seq;
    int server_seq;

    if (!arrayp(targets) || sizeof(targets) == 0
        || !arrayp(reservation_ids) || !arrayp(server_seqs)
        || sizeof(reservation_ids) != sizeof(targets)
        || sizeof(server_seqs) != sizeof(targets))
        return 0;

    sessions = allocate(sizeof(targets));
    seen_sessions = ([]);
    for (index = 0; index < sizeof(targets); index++) {
        server_seq = server_seqs[index];
        if (!objectp(targets[index]) || reservation_ids[index] <= 0
            || server_seq <= 0)
            return 0;
        session_id = query_object_session_id(targets[index]);
        if (!stringp(session_id) || session_id == ""
            || seen_sessions[session_id])
            return 0;
        seen_sessions[session_id] = 1;
        session = query_session(session_id);
        if (!mapp(session))
            return 0;
        reservations = mapp(session["protocol_reservations"])
            ? session["protocol_reservations"] : ([]);
        reservation_key = reservation_ids[index];
        last_server_seq = intp(session["last_server_seq"])
            ? session["last_server_seq"] : 0;
        if (mapp(reservations[reservation_key])
            || (last_server_seq > 0 && server_seq <= last_server_seq))
            return 0;
        sessions[index] = session;
    }

    for (index = 0; index < sizeof(targets); index++) {
        session = sessions[index];
        reservations = mapp(session["protocol_reservations"])
            ? session["protocol_reservations"] : ([]);
        reservation_key = reservation_ids[index];
        server_seq = server_seqs[index];
        reservations[reservation_key] = ([
            "reservation_id": reservation_ids[index],
            "packet_type": "MSGE",
            "server_seq": server_seq,
            "stream": "message",
            "reserved_at": time(),
        ]);
        session["protocol_reservations"] = reservations;
        session["pending_protocol_reservations"] =
            (intp(session["pending_protocol_reservations"])
                ? session["pending_protocol_reservations"] : 0) + 1;
        session["last_server_seq"] = server_seq;
        session["reservation_updated_at"] = time();
    }
    return 1;
}

int complete_session_output_reservation(object target, int reservation_id,
    string packet_type, string stream, int server_seq)
{
    mapping reservation;
    mapping reservations;
    mapping session;
    int reservation_key;
    string session_id;
    int last_completed_server_seq;

    if (!objectp(target) || reservation_id <= 0 || server_seq <= 0
        || !stringp(packet_type) || packet_type == ""
        || !stringp(stream) || stream == "")
        return 0;
    session_id = query_object_session_id(target);
    session = stringp(session_id) && session_id != ""
        ? query_session(session_id) : 0;
    if (!mapp(session))
        return 0;
    reservation_key = reservation_id;
    reservations = mapp(session["protocol_reservations"])
        ? session["protocol_reservations"] : ([]);
    reservation = reservations[reservation_key];
    if (!mapp(reservation) || reservation["server_seq"] != server_seq
        || !intp(session["pending_protocol_reservations"])
        || session["pending_protocol_reservations"] <= 0
        || (reservation["packet_type"] != packet_type
            && !(reservation["packet_type"] == "MSGE"
                && reservation["stream"] == "message"
                && packet_type == "BACH" && stream == "system"))
        || (reservation["packet_type"] == packet_type
            && reservation["stream"] != stream))
        return 0;

    /* The raw gateway slot is intentionally untyped.  Finalize its trace only
     * after native fill succeeds, so BACH does not need a separate observable
     * promotion state and a failed fill can still release the original slot. */
    reservation["packet_type"] = packet_type;
    reservation["stream"] = stream;
    map_delete(reservations, reservation_key);
    session["protocol_reservations"] = reservations;
    session["pending_protocol_reservations"] -= 1;
    session["completed_protocol_reservations"] =
        (intp(session["completed_protocol_reservations"])
            ? session["completed_protocol_reservations"] : 0) + 1;
    session["output_count"] = (intp(session["output_count"])
        ? session["output_count"] : 0) + 1;
    last_completed_server_seq = intp(session["last_completed_protocol_seq"])
        ? session["last_completed_protocol_seq"] : 0;
    if (server_seq >= last_completed_server_seq) {
        session["last_completed_protocol_seq"] = server_seq;
        session["last_packet_type"] = packet_type;
        session["last_stream"] = stream;
    }
    session["output_updated_at"] = time();
    _xk_protocol_send_count += 1;
    return 1;
}

int *complete_session_output_reservation_batch(object *targets,
    int *reservation_ids, int *event_counts, int *server_seqs, int *filled)
{
    int *completed;
    int count;
    int index;
    string packet_type;
    string stream;

    if (!arrayp(targets) || !arrayp(reservation_ids)
        || !arrayp(event_counts) || !arrayp(server_seqs) || !arrayp(filled))
        return ({ });
    count = sizeof(targets);
    if (count <= 0 || sizeof(reservation_ids) != count
        || sizeof(event_counts) != count || sizeof(server_seqs) != count
        || sizeof(filled) != count)
        return ({ });

    completed = allocate(count);
    for (index = 0; index < count; index++) {
        if (!filled[index])
            continue;
        packet_type = event_counts[index] == 1 ? "MSGE" : "BACH";
        stream = event_counts[index] == 1 ? "message" : "system";
        completed[index] = complete_session_output_reservation(
            targets[index], reservation_ids[index], packet_type, stream,
            server_seqs[index]);
    }
    return completed;
}

int promote_session_output_reservation(object target, int reservation_id,
    string packet_type, string stream)
{
    mapping reservation;
    mapping reservations;
    mapping session;
    int reservation_key;
    string session_id;

    if (!objectp(target) || reservation_id <= 0
        || !stringp(packet_type) || packet_type == ""
        || !stringp(stream) || stream == "")
        return 0;
    session_id = query_object_session_id(target);
    session = stringp(session_id) && session_id != ""
        ? query_session(session_id) : 0;
    if (!mapp(session))
        return 0;
    reservation_key = reservation_id;
    reservations = mapp(session["protocol_reservations"])
        ? session["protocol_reservations"] : ([]);
    reservation = reservations[reservation_key];
    if (!mapp(reservation))
        return 0;
    reservation["packet_type"] = packet_type;
    reservation["stream"] = stream;
    reservations[reservation_key] = reservation;
    session["protocol_reservations"] = reservations;
    session["reservation_updated_at"] = time();
    return 1;
}

int record_session_output_completion(object target, string packet_type, mapping payload,
    int reservation_id)
{
    mapping session;
    mapping reservations;
    mapping reservation;
    mapping meta;
    int reservation_key;
    string session_id;
    int completed_server_seq;
    int last_completed_server_seq;

    if (!objectp(target) || !stringp(packet_type) || packet_type == ""
        || !mapp(payload) || !intp(reservation_id) || reservation_id <= 0)
        return 0;

    session_id = query_object_session_id(target);
    session = stringp(session_id) && session_id != "" ? query_session(session_id) : 0;
    if (!mapp(session))
        return 0;

    reservation_key = reservation_id;
    reservations = mapp(session["protocol_reservations"])
        ? session["protocol_reservations"] : ([]);
    reservation = reservations[reservation_key];
    meta = mapp(payload["meta"]) ? payload["meta"] : ([]);
    if (!mapp(reservation) || reservation["packet_type"] != packet_type
        || reservation["server_seq"] != meta["server_seq"])
        return 0;

    map_delete(reservations, reservation_key);
    session["protocol_reservations"] = reservations;
    if (mapp(session["owner_output_reservations"])
        && mapp(session["owner_output_reservations"][reservation_key])) {
        map_delete(session["owner_output_reservations"], reservation_key);
        session["pending_owner_outputs"] =
            (intp(session["pending_owner_outputs"])
                && session["pending_owner_outputs"] > 0)
                    ? session["pending_owner_outputs"] - 1 : 0;
        session["completed_owner_outputs"] =
            (intp(session["completed_owner_outputs"])
                ? session["completed_owner_outputs"] : 0) + 1;
    }
    session["pending_protocol_reservations"] =
        (intp(session["pending_protocol_reservations"])
            && session["pending_protocol_reservations"] > 0)
                ? session["pending_protocol_reservations"] - 1 : 0;
    session["completed_protocol_reservations"] =
        (intp(session["completed_protocol_reservations"])
            ? session["completed_protocol_reservations"] : 0) + 1;
    session["output_count"] = (intp(session["output_count"])
        ? session["output_count"] : 0) + 1;
    completed_server_seq = intp(meta["server_seq"]) ? meta["server_seq"] : 0;
    last_completed_server_seq = intp(session["last_completed_protocol_seq"])
        ? session["last_completed_protocol_seq"] : 0;
    if (completed_server_seq >= last_completed_server_seq) {
        session["last_completed_protocol_seq"] = completed_server_seq;
        session["last_packet_type"] = packet_type;
        session["last_stream"] = stringp(meta["stream"]) ? meta["stream"] : "";
    }
    session["output_updated_at"] = time();
    _xk_protocol_send_count += 1;
    return 1;
}

int record_session_output_release(object target, int reservation_id)
{
    mapping session;
    mapping reservations;
    int reservation_key;
    string session_id;

    if (!objectp(target) || !intp(reservation_id) || reservation_id <= 0)
        return 0;

    session_id = query_object_session_id(target);
    session = stringp(session_id) && session_id != "" ? query_session(session_id) : 0;
    if (!mapp(session))
        return 0;

    reservation_key = reservation_id;
    reservations = mapp(session["protocol_reservations"])
        ? session["protocol_reservations"] : ([]);
    if (!mapp(reservations[reservation_key]))
        return 0;

    map_delete(reservations, reservation_key);
    session["protocol_reservations"] = reservations;
    if (mapp(session["owner_output_reservations"])
        && mapp(session["owner_output_reservations"][reservation_key])) {
        map_delete(session["owner_output_reservations"], reservation_key);
        session["pending_owner_outputs"] =
            (intp(session["pending_owner_outputs"])
                && session["pending_owner_outputs"] > 0)
                    ? session["pending_owner_outputs"] - 1 : 0;
        session["released_owner_outputs"] =
            (intp(session["released_owner_outputs"])
                ? session["released_owner_outputs"] : 0) + 1;
    }
    session["pending_protocol_reservations"] =
        (intp(session["pending_protocol_reservations"])
            && session["pending_protocol_reservations"] > 0)
                ? session["pending_protocol_reservations"] - 1 : 0;
    session["released_protocol_reservations"] =
        (intp(session["released_protocol_reservations"])
            ? session["released_protocol_reservations"] : 0) + 1;
    session["reservation_updated_at"] = time();
    return 1;
}

int release_session_output_reservation_exact(object target,
    int reservation_id, int server_seq)
{
    mapping reservation;
    mapping reservations;
    mapping session;
    int reservation_key;
    string session_id;

    if (!objectp(target) || reservation_id <= 0 || server_seq <= 0)
        return 0;
    session_id = query_object_session_id(target);
    session = stringp(session_id) && session_id != ""
        ? query_session(session_id) : 0;
    if (!mapp(session))
        return 0;
    reservation_key = reservation_id;
    reservations = mapp(session["protocol_reservations"])
        ? session["protocol_reservations"] : ([]);
    reservation = reservations[reservation_key];
    if (!mapp(reservation) || reservation["server_seq"] != server_seq
        || reservation["packet_type"] != "MSGE"
        || reservation["stream"] != "message"
        || !intp(session["pending_protocol_reservations"])
        || session["pending_protocol_reservations"] <= 0)
        return 0;

    map_delete(reservations, reservation_key);
    session["protocol_reservations"] = reservations;
    session["pending_protocol_reservations"] -= 1;
    session["released_protocol_reservations"] =
        (intp(session["released_protocol_reservations"])
            ? session["released_protocol_reservations"] : 0) + 1;
    session["reservation_updated_at"] = time();
    record_owner_protocol_output("released");
    return 1;
}

int record_owner_protocol_output(string outcome)
{
    string field_name;

    if (member_array(outcome, ({
            "submitted", "reserve_failed", "submit_failed", "watch_failed", "sync_fallback",
            "completion_failed", "completed", "released", "release_failed"
        })) == -1)
        return 0;
    if (!mapp(_owner_protocol_output_counts))
        _owner_protocol_output_counts = ([]);
    field_name = "owner_protocol_output_" + outcome + "_count";
    _owner_protocol_output_counts[field_name] =
        (intp(_owner_protocol_output_counts[field_name])
            ? _owner_protocol_output_counts[field_name] : 0) + 1;
    return 1;
}

mapping query_owner_protocol_output_status()
{
    mapping status;
    mapping session;
    string outcome;
    string field_name;
    string session_id;
    int fifo_violations;
    int pending;

    status = ([]);
    foreach (outcome in ({
        "submitted", "reserve_failed", "submit_failed", "watch_failed", "sync_fallback",
        "completion_failed", "completed", "released", "release_failed"
    })) {
        field_name = "owner_protocol_output_" + outcome + "_count";
        status[field_name] = mapp(_owner_protocol_output_counts)
            && intp(_owner_protocol_output_counts[field_name])
                ? _owner_protocol_output_counts[field_name] : 0;
    }
    foreach (session_id, session in _sessions) {
        if (!mapp(session))
            continue;
        if (mapp(session["protocol_reservations"]))
            pending += sizeof(session["protocol_reservations"]);
        if (intp(session["fifo_violations"]))
            fifo_violations += session["fifo_violations"];
    }
    status["owner_protocol_output_pending"] = pending;
    status["owner_protocol_output_fifo_violations"] = fifo_violations;
    return status;
}

int record_session_output_owner_result(object target, int reservation_id, string state)
{
    mapping session;
    mapping reservations;
    mapping reservation;
    int reservation_key;
    string session_id;
    int completed_server_seq;
    int last_completed_server_seq;

    if (!objectp(target) || !intp(reservation_id) || reservation_id <= 0
        || (state != "completed" && state != "released"))
        return 0;

    session_id = query_object_session_id(target);
    session = stringp(session_id) && session_id != "" ? query_session(session_id) : 0;
    if (!mapp(session))
        return 0;
    reservation_key = reservation_id;
    reservations = mapp(session["protocol_reservations"])
        ? session["protocol_reservations"] : ([]);
    reservation = reservations[reservation_key];
    if (!mapp(reservation))
        return 0;

    map_delete(reservations, reservation_key);
    session["protocol_reservations"] = reservations;
    session["pending_protocol_reservations"] =
        (intp(session["pending_protocol_reservations"])
            && session["pending_protocol_reservations"] > 0)
                ? session["pending_protocol_reservations"] - 1 : 0;
    if (state == "completed") {
        session["completed_protocol_reservations"] =
            (intp(session["completed_protocol_reservations"])
                ? session["completed_protocol_reservations"] : 0) + 1;
        session["output_count"] =
            (intp(session["output_count"]) ? session["output_count"] : 0) + 1;
        completed_server_seq = intp(reservation["server_seq"])
            ? reservation["server_seq"] : 0;
        last_completed_server_seq = intp(session["last_completed_protocol_seq"])
            ? session["last_completed_protocol_seq"] : 0;
        if (completed_server_seq >= last_completed_server_seq) {
            session["last_completed_protocol_seq"] = completed_server_seq;
            session["last_packet_type"] = reservation["packet_type"];
            session["last_stream"] = reservation["stream"];
        }
        session["output_updated_at"] = time();
        _xk_protocol_send_count += 1;
    } else {
        session["released_protocol_reservations"] =
            (intp(session["released_protocol_reservations"])
                ? session["released_protocol_reservations"] : 0) + 1;
        session["reservation_updated_at"] = time();
    }
    record_owner_protocol_output(state);
    return 1;
}

int record_session_output(object target, string packet_type, mapping meta, int xk_protocol_output)
{
    mapping session;
    string session_id;
    int server_seq;
    int last_server_seq;
    int last_completed_server_seq;

    if (!objectp(target) || !stringp(packet_type) || packet_type == "")
        return 0;

    session_id = query_object_session_id(target);
    if (!stringp(session_id) || session_id == "")
        return 0;

    session = query_session(session_id);
    if (!mapp(session))
        return 0;

    if (xk_protocol_output)
        _xk_protocol_send_count += 1;
    server_seq = mapp(meta) && intp(meta["server_seq"]) ? meta["server_seq"] : 0;
    last_server_seq = intp(session["last_server_seq"]) ? session["last_server_seq"] : 0;
    if (server_seq > 0 && last_server_seq > 0 && server_seq <= last_server_seq)
        session["fifo_violations"] = (intp(session["fifo_violations"]) ? session["fifo_violations"] : 0) + 1;

    session["output_count"] = (intp(session["output_count"]) ? session["output_count"] : 0) + 1;
    if (server_seq > 0)
        session["last_server_seq"] = server_seq;
    last_completed_server_seq = intp(session["last_completed_protocol_seq"])
        ? session["last_completed_protocol_seq"] : 0;
    if (server_seq >= last_completed_server_seq) {
        session["last_completed_protocol_seq"] = server_seq;
        session["last_packet_type"] = packet_type;
        session["last_stream"] = mapp(meta) && stringp(meta["stream"])
            ? meta["stream"] : "";
    }
    session["output_updated_at"] = time();
    return 1;
}

mapping query_session_fifo_snapshot(string session_id)
{
    mapping session;

    session = query_session(session_id);
    if (!mapp(session))
        return 0;

    return ([
        "session_id": session_id,
        "state": stringp(session["state"]) ? session["state"] : "",
        "owner_scope_id": stringp(session["owner_scope"]) ? session["owner_scope"] : "",
        "owner_id": stringp(session["owner_id"]) ? session["owner_id"] : "owner/global",
        "owner_epoch": intp(session["owner_epoch"]) ? session["owner_epoch"] : 0,
        "output_count": intp(session["output_count"]) ? session["output_count"] : 0,
        "last_server_seq": intp(session["last_server_seq"]) ? session["last_server_seq"] : 0,
        "last_completed_protocol_seq": intp(session["last_completed_protocol_seq"])
            ? session["last_completed_protocol_seq"] : 0,
        "fifo_violations": intp(session["fifo_violations"]) ? session["fifo_violations"] : 0,
        "pending_protocol_reservations": intp(session["pending_protocol_reservations"])
            ? session["pending_protocol_reservations"] : 0,
        "completed_protocol_reservations": intp(session["completed_protocol_reservations"])
            ? session["completed_protocol_reservations"] : 0,
        "released_protocol_reservations": intp(session["released_protocol_reservations"])
            ? session["released_protocol_reservations"] : 0,
        "pending_owner_outputs": intp(session["pending_owner_outputs"])
            ? session["pending_owner_outputs"] : 0,
        "completed_owner_outputs": intp(session["completed_owner_outputs"])
            ? session["completed_owner_outputs"] : 0,
        "released_owner_outputs": intp(session["released_owner_outputs"])
            ? session["released_owner_outputs"] : 0,
        "last_packet_type": stringp(session["last_packet_type"]) ? session["last_packet_type"] : "",
        "last_stream": stringp(session["last_stream"]) ? session["last_stream"] : "",
        "service_owner_id": stringp(session["service_owner_id"]) ? session["service_owner_id"] : "",
        "service_policy": stringp(session["service_policy"]) ? session["service_policy"] : "",
        "service_map_v2": session["service_map_v2"] ? 1 : 0,
        "direct_cross_owner_write": 0,
        "updated_at": intp(session["output_updated_at"]) ? session["output_updated_at"] : 0,
    ]);
}

mapping query_resume_record(string resume_token)
{
    if (!stringp(resume_token) || resume_token == "")
        return 0;
    return sanitize_resume_record(_resume_tokens[resume_token]);
}

mapping consume_resume_record(string resume_token)
{
    mapping record;
    mapping session;
    string session_id;

    record = query_resume_record(resume_token);
    if (!mapp(record))
        return 0;

    session_id = stringp(record["session_id"]) ? record["session_id"] : "";
    session = session_id != "" ? _sessions[session_id] : 0;
    if (mapp(session) && session["resume_token"] == resume_token) {
        map_delete(session, "resume_token");
        session["resume_token_consumed"] = 1;
    }

    map_delete(_resume_tokens, resume_token);
    return record;
}

mapping issue_resume_payload(string session_id, object target)
{
    mapping record;
    mapping session;
    string resume_token;

    session = ensure_session(session_id);
    if (!mapp(session) || !objectp(target))
        return 0;

    store_resume_record(session, target);
    resume_token = stringp(session["resume_token"]) ? session["resume_token"] : "";
    record = query_resume_record(resume_token);
    if (!mapp(record))
        return 0;

    return ([
        "resume_token": resume_token,
        "ttl_seconds": _resume_token_ttl,
        "expires_at": record["expires_at"],
        "session_id": session_id,
    ]);
}

mapping issue_resume_payload_for_target(object target)
{
    mapping record;
    string account_id;
    string protocol_version;
    string resume_token;
    string session_id;

    if (!objectp(target))
        return 0;

    account_id = query_target_account_id(target);
    if (!stringp(account_id) || account_id == "")
        return 0;

    do {
        resume_token = generate_resume_token();
    } while (mapp(_resume_tokens[resume_token]));

    session_id = query_target_runtime_session_id(target);
    protocol_version = query_target_protocol_version(target);
    record = ([
        "token": resume_token,
        "account_id": account_id,
        "session_id": session_id,
        "state": "player",
        "updated_at": time(),
        "expires_at": time() + _resume_token_ttl,
    ]);
    if (protocol_version != "")
        record["xk_protocol_version"] = protocol_version;

    _resume_tokens[resume_token] = record;
    schedule_resume_cleanup(_resume_token_ttl);

    return ([
        "resume_token": resume_token,
        "ttl_seconds": _resume_token_ttl,
        "expires_at": record["expires_at"],
        "session_id": session_id,
    ]);
}

string query_object_session_id(object target)
{
    if (!objectp(target))
        return "";
    if (stringp(_object_to_session[target]))
        return _object_to_session[target];
    if (function_exists("query_temp", target))
        return target->query_temp("gateway/session_id") || "";
    return "";
}

/* 玩家对象在活跃会话中被重建（destruct+reload / owner 迁移 / 异常恢复）后,
 * _object_to_session（以对象身份为键）与 gateway temp 均丢失,
 * 但持久属性 xk/gateway_session_id 仍在。此处按持久属性重绑 session,
 * 使会话路由恢复有效，XK 帧重新进入当前 direct/driver delivery。
 * 返回 0=无持久绑定或 session 已不存在（无法恢复）,
 *       1=已有绑定无需恢复,
 *       2=通过持久属性重新绑定成功。 */
int restore_object_session_binding(object target)
{
    string session_id;
    mapping session;
    object session_target;
    mixed err;
    int bound;

    if (!objectp(target))
        return 0;
    if (stringp(_object_to_session[target]))
        return 1;
    if (!function_exists("query", target))
        return 0;
    session_id = target->query("xk/gateway_session_id");
    if (!stringp(session_id) || session_id == "")
        return 0;
    session = query_session(session_id);
    if (!mapp(session))
        return 0;
    session_target = objectp(session["target"]) ? session["target"] : 0;
    if (objectp(session_target) && session_target != target)
        return 0; /* session 已被其他对象占用（顶号），不抢绑 */
    /* L-45：消费真实绑定结果并核验正/反向索引一致；绑定失败不得虚报恢复。 */
    bound = 0;
    err = catch(bound = bind_session_target(session_id, target,
        stringp(session["state"]) ? session["state"] : "player"));
    if (err || bound != 1)
        return 0;
    if (query_object_session_id(target) != session_id)
        return 0;
    if (query_session_target(session_id) != target)
        return 0;
    return 2;
}

/* Room fanout already owns a stable main-thread target snapshot. Resolve its
 * session identities in one daemon crossing instead of repeating the same
 * call_other once per recipient while CHAT entries are appended. */
mapping query_object_session_ids(object *targets)
{
    mapping session_ids;
    object target;
    string session_id;

    session_ids = ([]);
    if (!arrayp(targets) || sizeof(targets) == 0)
        return session_ids;

    foreach (target in targets) {
        if (!objectp(target))
            continue;
        session_id = stringp(_object_to_session[target])
            ? _object_to_session[target] : "";
        if (session_id == "" && function_exists("query_temp", target))
            session_id = target->query_temp("gateway/session_id") || "";
        if (session_id != "")
            session_ids[target] = session_id;
    }
    return session_ids;
}

object query_session_target(string session_id)
{
    mapping session;

    session = query_session(session_id);
    return mapp(session) && objectp(session["target"]) ? session["target"] : 0;
}

int dispatch_input(string session_id, mixed payload)
{
    object target;
    string input;

    target = query_session_target(session_id);
    if (!objectp(target))
        return 0;

    touch_session(session_id);
    if (stringp(payload))
        input = payload;
    else if (mapp(payload) && stringp(payload["payload"]))
        input = payload["payload"];
    else
        input = sprintf("%O", payload);

    if (function_exists("gateway_receive", target)) {
        call_other(target, "gateway_receive", payload);
        return 1;
    }
    if (function_exists("process_input", target)) {
        call_other(target, "process_input", input);
        return 1;
    }

    return 0;
}

void cleanup_stale_sessions()
{
    string *stale;
    string session_id;
    mapping session;
    int last_active;
    int now;

    /* L-48：区分从未绑定、曾绑定但对象已消失（destruct 后引用为 0）与
     * 异常引用；清理交给 remove_session，由其释放守卫保护未完成输出。 */
    now = time();
    stale = ({ });
    foreach (session_id, session in _sessions) {
        if (!mapp(session)) {
            stale += ({ session_id });
            continue;
        }
        if (objectp(session["target"]))
            continue;   /* 活跃目标 */
        if (session["target"] != 0) {
            stale += ({ session_id });   /* 异常引用：交给释放守卫处理 */
            continue;
        }
        if (session["bound_once"]) {
            stale += ({ session_id });   /* 曾绑定、对象已消失 */
            continue;
        }
        /* 从未绑定的新会话：超过一个清理节拍仍未活跃才回收，避免误删
         * 刚建立、尚未完成登录绑定的会话。 */
        last_active = intp(session["last_active"]) ? session["last_active"]
            : (intp(session["created_at"]) ? session["created_at"] : 0);
        if (last_active > 0 && now - last_active > _session_cleanup_interval)
            stale += ({ session_id });
    }

    foreach (session_id in stale) {
        remove_session(session_id, 0);
    }

    call_out("cleanup_stale_sessions", _session_cleanup_interval);
}

void setup()
{
    if (!mapp(_sessions))
        _sessions = ([]);
    if (!mapp(_object_to_session))
        _object_to_session = ([]);
    if (!mapp(_resume_tokens))
        _resume_tokens = ([]);
    if (!mapp(_sessioncheck_players))
        _sessioncheck_players = ([]);
    if (!mapp(_owner_protocol_projection_counts))
        _owner_protocol_projection_counts = ([]);
    if (!mapp(_owner_protocol_output_counts))
        _owner_protocol_output_counts = ([]);
    if (!mapp(_xk_protocol_phase_timing))
        _xk_protocol_phase_timing = ([]);
    if (!mapp(_owner_protocol_projection_timing))
        _owner_protocol_projection_timing = ([]);
    _owner_protocol_projection_timing_enabled = 0;
    if (!mapp(_look_phase_tail_probe))
        _look_phase_tail_probe = ([]);
    _resume_token_ttl = resolve_resume_token_ttl();
    _cleanup_call_out = 0;
    _session_cleanup_interval = 300;
    call_out("cleanup_stale_sessions", _session_cleanup_interval);
}
