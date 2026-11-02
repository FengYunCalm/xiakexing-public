/**
 * @file    login_session.c
 * @brief   登录会话管理门面
 * @author  XiaKeXing Development Team
 * @date    2026-03-20
 * @version 2.0.1
 *
 * @par Description:
 * 登录会话管理门面，负责角色创建、会话建立与进入游戏流程。
 */

#pragma strict_types

#include <system/globals.h>
#include <system/services.h>
#include <game_constants.h>

inherit "/kernel/login/session_auth";
inherit "/kernel/login/session_manager";

#define CHAR_D "/daemons/game/char_d"
#define PLAYER_D "/daemons/services/player/player_d"
#define UTIL_TIMING "/lib/std/utils/util_timing"

private nosave mapping _sessions = ([]);
private nosave mapping _login_attempts = ([]);
private nosave int _max_attempts = 5;
private nosave int _session_timeout = TIME_HOUR;
private nosave object _money_service;
private nosave object _guild_service;
private nosave object _config_service;
private nosave int _loadtest_login_timing_enabled = 0;
private nosave mapping _loadtest_login_timing = ([]);
private nosave mapping _loadtest_login_outcomes = ([]);
private nosave int _loadtest_login_timing_updated_at_ns = -1;

private string *loadtest_login_timing_stages()
{
    return ({
        "credential_auth",
        "account_existence",
        "account_read",
        "account_hash",
        "account_serialize",
        "account_tmp_write",
        "account_rename",
        "account_backup_restore",
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
    });
}

private int is_loadtest_login_timing_stage(string stage)
{
    return member_array(stage, loadtest_login_timing_stages()) >= 0;
}

int set_loadtest_login_timing_enabled(int enabled)
{
    if (base_name(previous_object()) != "/cmds/admin/ownercheck")
        return 0;
    _loadtest_login_timing_enabled = enabled ? 1 : 0;
    return 1;
}

int reset_loadtest_login_timing()
{
    if (base_name(previous_object()) != "/cmds/admin/ownercheck")
        return 0;
    _loadtest_login_timing = ([]);
    _loadtest_login_outcomes = ([]);
    _loadtest_login_timing_updated_at_ns = perf_counter_ns();
    return 1;
}

mapping begin_loadtest_login_phase_timing()
{
    if (!_loadtest_login_timing_enabled)
        return 0;

    return ([
        "wall_ns": performance_wall_time_ns(),
        "thread_cpu_ns": thread_cpu_time_ns(),
    ]);
}

int begin_loadtest_login_timing()
{
    return _loadtest_login_timing_enabled ? perf_counter_ns() : -1;
}

void record_loadtest_login_phase_timing(string stage, mapping started_at,
    int succeeded)
{
    mapping outcome;
    int wall_finished_at;
    int wall_elapsed_ns;
    int cpu_finished_at;
    int cpu_us;
    string outcome_key;

    if (!_loadtest_login_timing_enabled || !is_loadtest_login_timing_stage(stage)
        || !mapp(started_at) || !intp(started_at["wall_ns"])
        || started_at["wall_ns"] < 0)
        return;

    // Keep the CPU interval inside the wall interval for strict conservation.
    cpu_finished_at = thread_cpu_time_ns();
    wall_finished_at = performance_wall_time_ns();
    wall_elapsed_ns = wall_finished_at - started_at["wall_ns"];
    if (wall_elapsed_ns < 0)
        return;

    cpu_us = (!intp(started_at["thread_cpu_ns"])
        || started_at["thread_cpu_ns"] < 0
        || cpu_finished_at < started_at["thread_cpu_ns"])
            ? -1
            : (cpu_finished_at - started_at["thread_cpu_ns"]) / 1000;
    UTIL_TIMING->accumulate_wall_cpu(_loadtest_login_timing, stage,
        wall_elapsed_ns / 1000, cpu_us, "thread_cpu");

    outcome = mapp(_loadtest_login_outcomes[stage])
        ? _loadtest_login_outcomes[stage] : ([]);
    outcome_key = succeeded ? "success" : "failure";
    outcome[outcome_key] = (outcome[outcome_key] || 0) + 1;
    _loadtest_login_outcomes[stage] = outcome;
    _loadtest_login_timing_updated_at_ns = perf_counter_ns();
}

void record_loadtest_login_timing(string stage, int started_at)
{
    mapping outcome;
    int elapsed_ns;

    if (!_loadtest_login_timing_enabled || !is_loadtest_login_timing_stage(stage)
        || !intp(started_at) || started_at < 0)
        return;

    elapsed_ns = perf_counter_ns() - started_at;
    if (elapsed_ns < 0)
        return;

    /* 该阶段无 CPU 配对采样：cpu_us 恒为 -1，等价于原实现的
     * thread_cpu_unavailable 无条件累加。 */
    UTIL_TIMING->accumulate_wall_cpu(_loadtest_login_timing, stage,
        elapsed_ns / 1000, -1, "thread_cpu");

    outcome = mapp(_loadtest_login_outcomes[stage])
        ? _loadtest_login_outcomes[stage] : ([]);
    outcome["success"] = (outcome["success"] || 0) + 1;
    _loadtest_login_outcomes[stage] = outcome;
    _loadtest_login_timing_updated_at_ns = perf_counter_ns();
}

void record_loadtest_login_outcome(string stage, int succeeded)
{
    mapping outcome;
    string key;

    if (!_loadtest_login_timing_enabled || !is_loadtest_login_timing_stage(stage))
        return;

    outcome = mapp(_loadtest_login_outcomes[stage])
        ? _loadtest_login_outcomes[stage] : ([]);
    key = succeeded ? "success" : "failure";
    outcome[key] = (outcome[key] || 0) + 1;
    _loadtest_login_outcomes[stage] = outcome;
    _loadtest_login_timing_updated_at_ns = perf_counter_ns();
}

mapping query_loadtest_login_timing_status()
{
    mapping status;
    mapping timing;
    mapping outcome;
    string stage;
    string prefix;
    int now_ns;
    int snapshot_age_us;

    now_ns = perf_counter_ns();
    snapshot_age_us = _loadtest_login_timing_updated_at_ns >= 0
        && now_ns >= _loadtest_login_timing_updated_at_ns
            ? (now_ns - _loadtest_login_timing_updated_at_ns) / 1000 : -1;
    status = ([
        "login_timing_enabled": _loadtest_login_timing_enabled,
        "login_timing_snapshot_age_us": snapshot_age_us,
    ]);
    foreach (stage in loadtest_login_timing_stages()) {
        timing = mapp(_loadtest_login_timing[stage]) ? _loadtest_login_timing[stage] : ([]);
        prefix = "login_timing_" + stage + "_";
        foreach (string field in ({
            "wall_samples", "wall_total_us", "wall_max_us",
            "thread_cpu_samples", "thread_cpu_total_us", "thread_cpu_max_us",
            "thread_cpu_unavailable",
        }))
            status[prefix + field] = timing[field] || 0;

        outcome = mapp(_loadtest_login_outcomes[stage])
            ? _loadtest_login_outcomes[stage] : ([]);
        prefix = "login_phase_outcome_" + stage + "_";
        status[prefix + "success"] = outcome["success"] || 0;
        status[prefix + "failure"] = outcome["failure"] || 0;
    }
    return status;
}

private object resolve_config_service()
{
    object config_d;

    if (objectp(_config_service))
        return _config_service;

    config_d = find_object(CONFIG_D);
    if (!objectp(config_d))
        catch(load_object(CONFIG_D));
    config_d = find_object(CONFIG_D);

    if (objectp(config_d))
        _config_service = config_d;

    return config_d;
}

private object resolve_money_service()
{
    object service;

    if (objectp(_money_service))
        return _money_service;

    service = find_object(MONEY_D);
    if (!objectp(service))
        service = load_object(MONEY_D);

    if (objectp(service))
        _money_service = service;

    return service;
}

private object resolve_guild_service()
{
    object service;

    if (objectp(_guild_service))
        return _guild_service;

    service = find_object(GUILD_D);
    if (!objectp(service))
        service = load_object(GUILD_D);

    if (objectp(service))
        _guild_service = service;

    return service;
}

private int query_configured_session_timeout()
{
    object config_d;
    int timeout;

    config_d = resolve_config_service();

    if (!objectp(config_d) || !function_exists("query_session_timeout", config_d))
        return TIME_HOUR;

    timeout = config_d->query_session_timeout();
    if (!intp(timeout) || timeout <= 0)
        return TIME_HOUR;

    return timeout;
}

private int replace_online_character(string account_id)
{
    object *player_objects;
    object existing_player;
    mapping termination;
    string object_id;
    string bound_account_id;

    if (!stringp(account_id) || account_id == "")
        return 1;

    player_objects = children(PLAYER);
    foreach (existing_player in player_objects) {
        if (!objectp(existing_player))
            continue;

        object_id = get_object_id(existing_player);

        bound_account_id = existing_player->query("account_id");
        if (!stringp(bound_account_id) && function_exists("query_account_id", existing_player))
            bound_account_id = existing_player->query_account_id();

        if (object_id != account_id && bound_account_id != account_id)
            continue;

        termination = PLAYER_LIFECYCLE_D->terminate_player(existing_player,
            "login_replace");
        if (!mapp(termination) || !termination["ok"]) {
            log_file("system/session", sprintf(
                "[%s] replace_online_character blocked for %s: %O\n",
                ctime(time()), account_id, termination));
            return 0;
        }
    }
    return 1;
}

/**
 * @brief 查询当前会话映射
 * @return 会话映射
 */
mapping query_sessions() { return _sessions; }

/**
 * @brief 查询登录尝试记录
 * @return 登录尝试映射
 */
mapping query_login_attempts() { return _login_attempts; }

/**
 * @brief 查询最大登录尝试次数
 * @return 最大尝试次数
 */
int query_max_attempts() { return _max_attempts; }

/**
 * @brief 查询会话超时时间
 * @return 超时时间（秒）
 */
int query_session_timeout()
{
    _session_timeout = query_configured_session_timeout();
    return _session_timeout;
}

void create()
{
    _sessions = ([]);
    _login_attempts = ([]);
    _max_attempts = 5;
    _session_timeout = query_configured_session_timeout();
    _loadtest_login_timing_enabled = 0;
    _loadtest_login_timing = ([]);
    _loadtest_login_outcomes = ([]);
    _loadtest_login_timing_updated_at_ns = -1;

    schedule_session_cleanup();

    log_file("system/session", sprintf("[%s] Login session manager initialized\n", ctime(time())));
}

object do_create_character(string account_id, string password, string char_name,
    string gender, int is_manual, object login_ob)
{
    object char_ob;
    mapping registration;
    mapping session;
    string registration_token;
    int is_new_account;
    int character_preexisted;

    if (!stringp(account_id) || account_id == "")
        return 0;
    if (!stringp(char_name) || char_name == "")
        return 0;
    if (!stringp(gender) || gender == "")
        gender = "男性";

    is_new_account = !call_other(PLAYER_D, "account_exists", account_id);
    if (is_new_account) {
        registration = call_other(PLAYER_D, "prepare_new_account_registration",
            account_id, password);
        if (!mapp(registration) || !stringp(registration["token"])) {
            log_file("system/char_create", sprintf("account prepare failed account=%s\n", account_id));
            return 0;
        }
        registration_token = registration["token"];
    }

    character_preexisted = call_other(CHAR_D, "character_exists", account_id);
    char_ob = call_other(CHAR_D, "create_character", account_id, char_name, gender);
    if (!objectp(char_ob)) {
        log_file("system/char_create", sprintf("char_d.create_character failed account=%s\n", account_id));
        if (is_new_account)
            call_other(PLAYER_D, "rollback_new_account_registration",
                account_id, registration_token);
        if (!character_preexisted)
            call_other(CHAR_D, "delete_character", account_id, char_name);
        return 0;
    }

    if ((is_new_account
            && !call_other(PLAYER_D, "commit_new_account_registration",
                account_id, registration_token, account_id, char_name))
        || (!is_new_account
            && !call_other(PLAYER_D, "finalize_character_registration",
                account_id, account_id, char_name))) {
        log_file("system/char_create", sprintf("account finalize failed account=%s\n", account_id));
        if (is_new_account)
            call_other(PLAYER_D, "rollback_new_account_registration",
                account_id, registration_token);
        call_other(CHAR_D, "delete_character", account_id, char_name);
        if (objectp(char_ob))
            destruct(char_ob);
        return 0;
    }

    if (objectp(login_ob) && stringp(login_ob->query_temp("xk_protocol_version"))
        && login_ob->query_temp("xk_protocol_version") != "") {
        char_ob->set_temp("xk_protocol_version", login_ob->query_temp("xk_protocol_version"));
    }

    /*
     * 进入游戏前先给角色创建正式 session，再把同一个 session_id 回写到登录对象。
     * login_ob 这里只保留交接凭据，本体所有权从这里开始转到角色对象。
     */
    session = is_new_account
        && call_other(PLAYER_D, "consume_registration_test_fault",
            account_id, "session_create")
        ? 0 : create_session(char_ob);
    if (!mapp(session)) {
        if (is_new_account)
            call_other(PLAYER_D, "rollback_new_account_registration",
                account_id, registration_token);
        call_other(CHAR_D, "delete_character", account_id, char_name);
        if (objectp(char_ob))
            destruct(char_ob);
        return 0;
    }

    if (is_new_account
        && !call_other(PLAYER_D, "confirm_new_account_registration",
            account_id, registration_token)) {
        destroy_session(session["id"]);
        call_other(PLAYER_D, "rollback_new_account_registration",
            account_id, registration_token);
        call_other(CHAR_D, "delete_character", account_id, char_name);
        if (objectp(char_ob))
            destruct(char_ob);
        return 0;
    }

    char_ob->set_temp("session_id", session["id"]);
    if (objectp(login_ob)) {
        login_ob->set_temp("session_id", session["id"]);
    }

    return char_ob;
}

object enter_game(string account_id, object login_ob)
{
    object char_ob;
    mapping session;
    mapping projection_restore;
    string *character_ids;
    mixed projection_err;

    if (!stringp(account_id) || account_id == "")
        return 0;
    if (!call_other(PLAYER_D, "account_exists", account_id))
        return 0;

    character_ids = call_other(PLAYER_D, "query_account_chars", account_id);
    if (!arrayp(character_ids) || sizeof(character_ids) == 0)
        return 0;

    if (!replace_online_character(account_id))
        return 0;

    char_ob = call_other(CHAR_D, "load_character", account_id);
    if (!objectp(char_ob))
        return 0;

    /*
     * 登录对象负责认证，角色对象负责落游戏态。
     * 这里补齐 account/setup/初始房间后，后续在线状态都应围绕 char_ob 展开。
     */
    if (function_exists("set_account_id", char_ob)) {
        call_other(char_ob, "set_account_id", account_id);
    }
    if (function_exists("setup", char_ob)) {
        call_other(char_ob, "setup");
    }

    if (objectp(login_ob) && stringp(login_ob->query_temp("xk_protocol_version"))
        && login_ob->query_temp("xk_protocol_version") != "") {
        char_ob->set_temp("xk_protocol_version", login_ob->query_temp("xk_protocol_version"));
    }

    if (objectp(resolve_money_service()))
        _money_service->sync_player_money(char_ob);
    if (objectp(resolve_guild_service()))
        _guild_service->sync_player_cache(char_ob);

    char_ob->set_temp("login/bootstrap_room_sync", 1);
    projection_err = catch(projection_restore = call_other(
        "/kernel/login", "restore_login_projection_for_login", char_ob));
    if (projection_err || !mapp(projection_restore) || !projection_restore["ok"]) {
        log_file("system/session", sprintf(
            "[%s] enter_game: login projection restore failed for %s: %O\n",
            ctime(time()), account_id, projection_err || projection_restore));
        if (!projection_err && mapp(projection_restore) && projection_restore["fatal"]) {
            destruct(char_ob);
            return 0;
        }
        if (objectp(char_ob))
            destruct(char_ob);
        return 0;
    }

    /* 结构化 reason：登录落点恢复来源、请求路径、最终房间与持久化结果。 */
    {
        mapping loc = projection_restore["location"];
        log_file("system/session", sprintf(
            "[%s] enter_game: login projection for %s: stage=%s source=%s requested=%s final=%s persisted=%d\n",
            ctime(time()), account_id,
            projection_restore["stage"],
            mapp(loc) ? (loc["source"] || "n/a") : "n/a",
            mapp(loc) ? (loc["requested_path"] || "") : "",
            mapp(loc) ? (loc["runtime_room"] || "") : "",
            projection_restore["persisted"]));
    }

    /*
     * 会话 id 同时写给 char_ob 与 login_ob，便于登录态完成最后一跳交接，
     * 但在线会话的真实 user 指针始终绑定 char_ob。
     */
    session = create_session(char_ob);
    if (mapp(session)) {
        char_ob->set_temp("session_id", session["id"]);
        if (objectp(login_ob)) {
            login_ob->set_temp("session_id", session["id"]);
        }
    }

    call_other(PLAYER_D, "update_login_time", account_id);

    return char_ob;
}
