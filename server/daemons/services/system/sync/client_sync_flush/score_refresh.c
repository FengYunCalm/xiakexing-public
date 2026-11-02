/**
 * @file    client_sync_flush/score_refresh.c
 * @brief   deferred score refresh 队列与压力分片
 *
 * @par Description:
 * 含 {reset_loadtest_measurement_diagnostics, normalize_score_refresh_state, merge_score_refresh_handoff_state, schedule_deferred_score_refreshes, reset_score_refresh_queue_if_idle, score_refresh_pressure_should_yield, record_score_refresh_slice_diagnostics, record_score_refresh_diagnostic_failure, record_score_refresh_liveness_diagnostics, flush_deferred_score_refreshes, queue_deferred_score_refresh}。
 * 持有 score refresh callout 状态（_score_refresh_call_out、_score_refresh_generation）；队列本体（_pending_score_refreshes、_score_refresh_queue 系列）声明在 routes（handoff 迁入点）。
 *
 * 本模块是 `client_sync_flush.c` 继承链的一环，由上层模块继承；
 * 跨模块调用经前向声明 + 运行时动态分派（this_object() 解析到最终对象）。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>
#include <system/xk_protocol.h>

inherit "/daemons/services/system/sync/client_sync_flush/fanout";

#define DEFERRED_FLUSH_WALLTIME_DELAY_SECONDS 0.001
#define DEFERRED_SCORE_PRESSURE_RETRY_SECONDS 0.05
#define MAX_DEFERRED_SCORE_REFRESHES 4
#define SCORE_REFRESH_FORCED_AGE_NS 1000000000
#define SCORE_REFRESH_MAX_PRESSURE_DEFERRALS 20
#define SCORE_REFRESH_WALL_BUDGET_NS 12000000

nosave int _score_refresh_call_out;
nosave int _score_refresh_generation;

int can_deliver_to_target();
int chat_batch_diagnostic_int();
int elapsed_ns_or_unavailable();
void flush_deferred_score_refreshes();
void merge_chat_batch_duration_summary();
mapping merge_score_refresh_handoff_state(mixed current, mixed incoming);
mapping normalize_score_refresh_state(mixed state);
mixed *peek_pending_score_refresh();
string query_object_session_id(object player);
string query_plan_key();
void queue_deferred_score_refresh();
void record_chat_batch_duration();
void record_score_refresh_diagnostic_failure();
void record_score_refresh_liveness_diagnostics();
void record_score_refresh_slice_diagnostics();
void reset_loadtest_measurement_diagnostics();
void reset_score_refresh_queue_if_idle();
object resolve_sync_target();
object resolve_sync_target_key();
varargs void schedule_deferred_score_refreshes();
int score_refresh_pressure_should_yield();
int score_refresh_projection_is_current(object player, mapping state);
int single_thread_mode();

void reset_loadtest_measurement_diagnostics()
{
    _chat_batch_diagnostics = ([]);
}

mapping normalize_score_refresh_state(mixed state)
{
    object player;
    mapping projection;
    mapping response_context;
    string session_id;
    int pressure_deferrals;
    int queued_at_ns;

    if (mapp(state)) {
        queued_at_ns = intp(state["queued_at_ns"])
            ? state["queued_at_ns"] : 0;
        pressure_deferrals = intp(state["pressure_deferrals"])
            ? state["pressure_deferrals"] : 0;
        projection = mapp(state["projection"])
            ? copy(state["projection"]) : 0;
        response_context = mapp(state["response_context"])
            ? copy(state["response_context"]) : 0;
        player = objectp(state["player"]) ? state["player"] : 0;
        session_id = stringp(state["session_id"]) ? state["session_id"] : "";
    }
    if (queued_at_ns <= 0)
        queued_at_ns = perf_counter_ns();
    if (pressure_deferrals < 0)
        pressure_deferrals = 0;
    return ([
        "queued_at_ns": queued_at_ns,
        "pressure_deferrals": pressure_deferrals,
        "projection": projection,
        "response_context": response_context,
        "player": player,
        "session_id": session_id,
    ]);
}

mapping merge_score_refresh_handoff_state(mixed current, mixed incoming)
{
    mapping current_state;
    mapping incoming_state;

    if (!current)
        return normalize_score_refresh_state(incoming);
    if (!incoming)
        return normalize_score_refresh_state(current);
    current_state = normalize_score_refresh_state(current);
    incoming_state = normalize_score_refresh_state(incoming);
    if (incoming_state["queued_at_ns"] < current_state["queued_at_ns"])
        current_state["queued_at_ns"] = incoming_state["queued_at_ns"];
    if (incoming_state["pressure_deferrals"]
        > current_state["pressure_deferrals"])
        current_state["pressure_deferrals"] =
            incoming_state["pressure_deferrals"];
    if (!mapp(current_state["response_context"])
        && mapp(incoming_state["response_context"]))
        current_state["response_context"] =
            copy(incoming_state["response_context"]);
    else if (mapp(current_state["response_context"])
        && !mapp(incoming_state["response_context"]))
        map_delete(current_state, "response_context");
    else if (mapp(current_state["response_context"])
        && mapp(incoming_state["response_context"])
        && current_state["response_context"]["command_request_id"]
            != incoming_state["response_context"]["command_request_id"])
        map_delete(current_state, "response_context");
    return current_state;
}

private mixed *peek_pending_score_refresh()
{
    mapping refresh_state;
    string key;

    while (mapp(_score_refresh_queue)
        && _score_refresh_queue_head < _score_refresh_queue_tail) {
        key = _score_refresh_queue[_score_refresh_queue_head];
        if (!stringp(key) || key == "" || !_pending_score_refreshes[key]) {
            map_delete(_score_refresh_queue, _score_refresh_queue_head++);
            continue;
        }
        refresh_state = _pending_score_refreshes[key];
        if (!mapp(refresh_state) || !intp(refresh_state["queued_at_ns"])
            || refresh_state["queued_at_ns"] <= 0
            || !intp(refresh_state["pressure_deferrals"])
            || refresh_state["pressure_deferrals"] < 0) {
            refresh_state = normalize_score_refresh_state(refresh_state);
            _pending_score_refreshes[key] = refresh_state;
        }
        return ({ key, refresh_state });
    }
    reset_score_refresh_queue_if_idle();
    return ({ });
}

private varargs void schedule_deferred_score_refreshes(int pressure_retry)
{
    float delay;

    if (_score_refresh_call_out || !mapp(_pending_score_refreshes)
        || sizeof(_pending_score_refreshes) == 0
        || !mapp(_score_refresh_queue)
        || _score_refresh_queue_head >= _score_refresh_queue_tail)
        return;

    _score_refresh_generation++;
    delay = pressure_retry ? DEFERRED_SCORE_PRESSURE_RETRY_SECONDS
        : DEFERRED_FLUSH_WALLTIME_DELAY_SECONDS;
    _score_refresh_call_out = call_out_walltime_background("flush_deferred_score_refreshes",
        delay, _score_refresh_generation);
}

void reset_score_refresh_queue_if_idle()
{
    if (mapp(_pending_score_refreshes)
        && sizeof(_pending_score_refreshes) > 0)
        return;

    _pending_score_refreshes = ([]);
    _score_refresh_queue = ([]);
    _score_refresh_queue_head = 0;
    _score_refresh_queue_tail = 0;
}

private int score_refresh_pressure_should_yield(int mid_slice)
{
    int buffered_input_pending;
    int command_pending;
    int main_queue_pending;

    command_pending = gateway_command_pending();
    main_queue_pending = gateway_main_queue_pending();
    buffered_input_pending = gateway_buffered_input_pending();
    if (command_pending <= 0 && main_queue_pending <= 0
        && buffered_input_pending <= 0)
        return 0;

    if (!mapp(_chat_batch_diagnostics))
        _chat_batch_diagnostics = ([]);
    if (command_pending > 0)
        _chat_batch_diagnostics["client_sync_score_refresh_command_deferred"] =
            chat_batch_diagnostic_int(
                "client_sync_score_refresh_command_deferred") + 1;
    if (main_queue_pending > 0)
        _chat_batch_diagnostics["client_sync_score_refresh_main_queue_deferred"] =
            chat_batch_diagnostic_int(
                "client_sync_score_refresh_main_queue_deferred") + 1;
    if (buffered_input_pending > 0)
        _chat_batch_diagnostics[
            "client_sync_score_refresh_buffered_input_deferred"] =
            chat_batch_diagnostic_int(
                "client_sync_score_refresh_buffered_input_deferred") + 1;
    if (mid_slice)
        _chat_batch_diagnostics[
            "client_sync_score_refresh_mid_slice_pressure_yield"] =
            chat_batch_diagnostic_int(
                "client_sync_score_refresh_mid_slice_pressure_yield") + 1;
    return 1;
}

private void record_score_refresh_slice_diagnostics(int handled, int executed,
    int failed, int wall_samples, int wall_total_us, int wall_max_us,
    int wall_unavailable_count, int slice_wall_ns, int wall_budget_yield,
    int batch_limit_yield, mixed cpu_started_at, mixed cpu_finished_at)
{
    if (!mapp(_chat_batch_diagnostics))
        _chat_batch_diagnostics = ([]);
    _chat_batch_diagnostics["client_sync_score_refresh_executed"] =
        chat_batch_diagnostic_int("client_sync_score_refresh_executed")
        + executed;
    _chat_batch_diagnostics["client_sync_score_refresh_failed"] =
        chat_batch_diagnostic_int("client_sync_score_refresh_failed")
        + failed;
    _chat_batch_diagnostics["client_sync_score_refresh_slices"] =
        chat_batch_diagnostic_int("client_sync_score_refresh_slices") + 1;
    if (handled
        > chat_batch_diagnostic_int("client_sync_score_refresh_recipients_max"))
        _chat_batch_diagnostics["client_sync_score_refresh_recipients_max"] =
            handled;
    merge_chat_batch_duration_summary("client_sync_score_refresh_wall",
        wall_samples, wall_total_us, wall_max_us,
        wall_unavailable_count);
    record_chat_batch_duration("client_sync_score_refresh_slice_wall",
        slice_wall_ns);
    record_chat_batch_duration("client_sync_score_refresh_cpu",
        elapsed_ns_or_unavailable(cpu_started_at, cpu_finished_at));
    if (wall_budget_yield)
        _chat_batch_diagnostics[
            "client_sync_score_refresh_wall_budget_yield"] =
            chat_batch_diagnostic_int(
                "client_sync_score_refresh_wall_budget_yield") + 1;
    if (batch_limit_yield)
        _chat_batch_diagnostics[
            "client_sync_score_refresh_batch_limit_yield"] =
            chat_batch_diagnostic_int(
                "client_sync_score_refresh_batch_limit_yield") + 1;
}

private void record_score_refresh_diagnostic_failure()
{
    if (!mapp(_chat_batch_diagnostics))
        _chat_batch_diagnostics = ([]);
    _chat_batch_diagnostics["client_sync_score_refresh_diagnostic_failed"] =
        chat_batch_diagnostic_int(
            "client_sync_score_refresh_diagnostic_failed") + 1;
}

private void record_score_refresh_liveness_diagnostics(int oldest_age_ns,
    int pressure_deferrals, int forced_liveness, int forced_executed)
{
    if (!mapp(_chat_batch_diagnostics))
        _chat_batch_diagnostics = ([]);
    record_chat_batch_duration("client_sync_score_refresh_oldest_age",
        oldest_age_ns);
    if (pressure_deferrals > chat_batch_diagnostic_int(
        "client_sync_score_refresh_pressure_deferrals_max"))
        _chat_batch_diagnostics[
            "client_sync_score_refresh_pressure_deferrals_max"] =
            pressure_deferrals;
    if (forced_liveness) {
        _chat_batch_diagnostics[
            "client_sync_score_refresh_forced_liveness_slices"] =
            chat_batch_diagnostic_int(
                "client_sync_score_refresh_forced_liveness_slices") + 1;
        _chat_batch_diagnostics[
            "client_sync_score_refresh_forced_liveness_executed"] =
            chat_batch_diagnostic_int(
                "client_sync_score_refresh_forced_liveness_executed")
                + forced_executed;
    }
}

void flush_deferred_score_refreshes(int generation)
{
    mapping refresh_state;
    mixed *pending_head;
    string key;
    object player;
    mixed diagnostic_error;
    mixed refresh_error;
    mixed cpu_started_at;
    mixed cpu_finished_at;
    mixed slice_wall_started_at;
    mixed refresh_wall_ns;
    int executed;
    int failed;
    int handled;
    int wall_max_us;
    int wall_samples;
    int wall_total_us;
    int wall_unavailable_count;
    int slice_wall_elapsed_ns;
    int wall_budget_yield;
    int batch_limit_yield;
    int forced_liveness;
    int oldest_age_ns;
    int pressure_deferrals;
    int pressure_yield;
    int refresh_limit;

    if (generation != _score_refresh_generation)
        return;
    _score_refresh_call_out = 0;
    if (!mapp(_pending_score_refreshes)
        || sizeof(_pending_score_refreshes) == 0)
        return;
    pending_head = peek_pending_score_refresh();
    if (!arrayp(pending_head) || sizeof(pending_head) < 2)
        return;
    refresh_state = mapp(pending_head[1]) ? pending_head[1] : ([]);
    if (score_refresh_pressure_should_yield(0)) {
        refresh_state["pressure_deferrals"]++;
        pressure_deferrals = refresh_state["pressure_deferrals"];
        oldest_age_ns = elapsed_ns_or_unavailable(
            refresh_state["queued_at_ns"], perf_counter_ns());
        forced_liveness = oldest_age_ns >= SCORE_REFRESH_FORCED_AGE_NS
            || pressure_deferrals >= SCORE_REFRESH_MAX_PRESSURE_DEFERRALS;
        if (!forced_liveness) {
            schedule_deferred_score_refreshes(1);
            diagnostic_error = catch(record_score_refresh_liveness_diagnostics(
                oldest_age_ns, pressure_deferrals, 0, 0));
            if (diagnostic_error)
                catch(record_score_refresh_diagnostic_failure());
            return;
        }
    }
    refresh_limit = forced_liveness ? 1 : MAX_DEFERRED_SCORE_REFRESHES;

    cpu_started_at = thread_cpu_time_ns();
    slice_wall_started_at = perf_counter_ns();
    while (handled < refresh_limit
        && _score_refresh_queue_head < _score_refresh_queue_tail) {
        key = _score_refresh_queue[_score_refresh_queue_head];
        map_delete(_score_refresh_queue, _score_refresh_queue_head++);
        if (!_pending_score_refreshes[key])
            continue;
        refresh_state = normalize_score_refresh_state(
            _pending_score_refreshes[key]);
        map_delete(_pending_score_refreshes, key);
        handled++;
        player = resolve_sync_target_key(key);
        /* L-183：与投递门一致——非 interactive 但持有 gateway session 的
         * 目标同样可执行刷新，不再形成“入队成功、每次都投递失败”的循环。 */
        if (objectp(player)
            && (interactive(player) || query_object_session_id(player) != "")
            && score_refresh_projection_is_current(player, refresh_state)
            && function_exists(
                "refresh_owner_score_read_model_with_elapsed", player)) {
            refresh_wall_ns = -1;
            /* This path only rebuilds the shared score read-model cache. It
             * is not a command reply and must never install a saved request
             * ID for a later producer to discover. */
            refresh_error = catch(refresh_wall_ns =
                player->refresh_owner_score_read_model_with_elapsed());
            if (refresh_error) {
                failed++;
            } else if (intp(refresh_wall_ns) && refresh_wall_ns >= 0) {
                wall_samples++;
                /* Accumulate in microseconds: a 300-player wave exceeds the
                 * LPC integer range when nanoseconds are summed directly. */
                wall_total_us += refresh_wall_ns / 1000;
                if (refresh_wall_ns / 1000 > wall_max_us)
                    wall_max_us = refresh_wall_ns / 1000;
            } else {
                wall_unavailable_count++;
            }
            if (!refresh_error)
                executed++;
        } else {
            /* L-182：目标消失/投影过期分别计数，不静默丢弃；恢复可达后由
             * 下一次业务触发重新入队补齐。 */
            if (!mapp(_chat_batch_diagnostics))
                _chat_batch_diagnostics = ([]);
            if (!objectp(player)) {
                _chat_batch_diagnostics[
                    "client_sync_score_refresh_target_unavailable"] =
                    chat_batch_diagnostic_int(
                        "client_sync_score_refresh_target_unavailable") + 1;
            } else {
                _chat_batch_diagnostics[
                    "client_sync_score_refresh_projection_stale"] =
                    chat_batch_diagnostic_int(
                        "client_sync_score_refresh_projection_stale") + 1;
            }
        }

        if (score_refresh_pressure_should_yield(1)) {
            pressure_yield = 1;
            break;
        }
        slice_wall_elapsed_ns = elapsed_ns_or_unavailable(
            slice_wall_started_at, perf_counter_ns());
        if (slice_wall_elapsed_ns >= SCORE_REFRESH_WALL_BUDGET_NS) {
            wall_budget_yield = 1;
            break;
        }
    }
    slice_wall_elapsed_ns = elapsed_ns_or_unavailable(
        slice_wall_started_at, perf_counter_ns());
    if (!pressure_yield && !wall_budget_yield
        && handled >= refresh_limit
        && _score_refresh_queue_head < _score_refresh_queue_tail)
        batch_limit_yield = 1;
    reset_score_refresh_queue_if_idle();
    cpu_finished_at = thread_cpu_time_ns();
    /* Queue liveness is business state.  Arm its continuation before any
     * optional diagnostics so a malformed or unavailable timing sample cannot
     * strand every remaining player. */
    schedule_deferred_score_refreshes();
    if (forced_liveness) {
        diagnostic_error = catch(record_score_refresh_liveness_diagnostics(
            oldest_age_ns, pressure_deferrals, 1, executed));
        if (diagnostic_error)
            catch(record_score_refresh_diagnostic_failure());
    }
    diagnostic_error = catch(record_score_refresh_slice_diagnostics(
        handled, executed, failed, wall_samples, wall_total_us, wall_max_us,
        wall_unavailable_count, slice_wall_elapsed_ns, wall_budget_yield,
        batch_limit_yield, cpu_started_at, cpu_finished_at));
    if (diagnostic_error)
        catch(record_score_refresh_diagnostic_failure());
}

void queue_deferred_score_refresh(object player, mapping response_context)
{
    mapping projection;
    mapping existing;
    string key;
    int force_refresh;

    player = resolve_sync_target(player);
    if (!can_deliver_to_target(player)) {
        /* L-182：目标暂不可达不静默丢弃；计数留给运营核查，下一次业务
         * 触发重新入队补齐。 */
        if (!mapp(_chat_batch_diagnostics))
            _chat_batch_diagnostics = ([]);
        _chat_batch_diagnostics["client_sync_score_refresh_undeliverable"] =
            chat_batch_diagnostic_int(
                "client_sync_score_refresh_undeliverable") + 1;
        return;
    }
    if (single_thread_mode()) {
        if (!mapp(_chat_batch_diagnostics))
            _chat_batch_diagnostics = ([]);
        _chat_batch_diagnostics[
            "client_sync_score_refresh_single_thread_skipped"] =
            chat_batch_diagnostic_int(
                "client_sync_score_refresh_single_thread_skipped") + 1;
        return;
    }
    if (!mapp(_chat_batch_diagnostics))
        _chat_batch_diagnostics = ([]);
    _chat_batch_diagnostics["client_sync_score_refresh_queued"] =
        chat_batch_diagnostic_int("client_sync_score_refresh_queued") + 1;

    key = query_plan_key(player);
    if (key == "")
        return;
    projection = LOGIN_PROJECTION_D->query_projection_identity(player);
    if (mapp(_pending_score_refreshes) && _pending_score_refreshes[key]) {
        existing = normalize_score_refresh_state(_pending_score_refreshes[key]);
        if (!score_refresh_projection_is_current(player, existing)) {
            map_delete(_pending_score_refreshes, key);
            force_refresh = 1;
        } else {
            _chat_batch_diagnostics["client_sync_score_refresh_coalesced"] =
                chat_batch_diagnostic_int("client_sync_score_refresh_coalesced") + 1;
            return;
        }
    }
    if (!function_exists("claim_owner_read_model_refresh", player)
        || !player->claim_owner_read_model_refresh("score_dialog", force_refresh)) {
        /* L-182：认领失败是可重试暂态（read model 已被占用），计数后交给
         * 下一次业务触发重新入队，不静默丢弃也不在此无限重排。 */
        if (!mapp(_chat_batch_diagnostics))
            _chat_batch_diagnostics = ([]);
        _chat_batch_diagnostics["client_sync_score_refresh_claim_deferred"] =
            chat_batch_diagnostic_int(
                "client_sync_score_refresh_claim_deferred") + 1;
        return;
    }
    if (!mapp(_pending_score_refreshes))
        _pending_score_refreshes = ([]);
    if (!mapp(_score_refresh_queue)) {
        _score_refresh_queue = ([]);
        _score_refresh_queue_head = 0;
        _score_refresh_queue_tail = 0;
    }
    _pending_score_refreshes[key] = ([
        "queued_at_ns": perf_counter_ns(),
        "pressure_deferrals": 0,
        "projection": copy(projection),
        "response_context": mapp(response_context)
            ? copy(response_context) : 0,
        "player": player,
        "session_id": query_object_session_id(player),
    ]);
    _score_refresh_queue[_score_refresh_queue_tail++] = key;
    schedule_deferred_score_refreshes();
}
