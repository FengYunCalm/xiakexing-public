/**
 * @file    login_transition.c
 * @brief   登录交接辅助子服务
 * @author  XiaKeXing Development Team
 * @date    2026-03-30
 * @version 1.0.0
 */

#pragma strict_types

#include <system/services.h>
#include <system/xk_protocol.h>

#define XK_PROTOCOL_CORE "/kernel/framework/xk_protocol_core"
#define PLAYER_LIFECYCLE_D       "/daemons/services/system/player_lifecycle_d"
#define LOGIN_SESSION "/kernel/login/login_session"

private void prime_player_read_models(object player)
{
    if (!objectp(player))
        return;

    PLAYER_ACTOR_ANALYSIS_D->query_player_actor_analysis(player);
    player->schedule_owner_map_grid_read_model_refresh(1);
}

void prime_player_read_caches_by_path(string player_ref)
{
    object player;
    mapping started_at;

    if (!stringp(player_ref) || player_ref == "")
        return;

    player = find_object(player_ref);
    if (!objectp(player) || !interactive(player))
        return;

    started_at = call_other(LOGIN_SESSION, "begin_loadtest_login_phase_timing");
    prime_player_read_models(player);
    call_other("/cmds/player/hp", "xk_prime_hp_dialog_cache", player);
    call_other("/kernel/simul_efun_room_entities_xk", "xk_prime_inventory_payload_cache", player, 1);
    call_other("/cmds/player/score", "xk_prime_score_dialog_cache", player);
    call_other("/kernel/simul_efun_player_xk", "xk_prime_skill_list_cache", player);
    call_other(LOGIN_SESSION, "record_loadtest_login_phase_timing",
        "post_handoff_prime", started_at, 1);
}

void refresh_login_room(object room)
{
    mixed previous_suppress;
    mixed err;

    if (!objectp(room))
        return;

    previous_suppress = room->query_temp("room/suppress_arrival_messages");
    room->set_temp("room/suppress_arrival_messages", 1);

    /* L-56：共享房间的压制标记必须在**所有**退出路径配对释放。旧实现
     * 直接调用房间刷新函数，一旦它抛异常标记就永久留在房间里，之后该
     * 房间再不发送到达消息。 */
    if (function_exists("refresh_room_entities", room))
        err = catch(call_other(room, "refresh_room_entities"));
    else if (function_exists("ensure_room_entities", room))
        err = catch(call_other(room, "ensure_room_entities"));

    if (previous_suppress)
        room->set_temp("room/suppress_arrival_messages", previous_suppress);
    else
        room->delete_temp("room/suppress_arrival_messages");

    if (err)
        log_file("system/session", sprintf(
            "[%s] refresh_login_room failed for %s: %O\n",
            ctime(time()), file_name(room), err));
}

private void destroy_failed_login_object_by_path(string login_ref)
{
    object login_ob;

    if (!stringp(login_ref) || login_ref == "")
        return;

    login_ob = find_object(login_ref);
    if (!objectp(login_ob) || interactive(login_ob)
        || base_name(login_ob) != "/kernel/login")
        return;

    destruct(login_ob);
}

private int fail_closed_login_handoff(object login_ob, object player,
    string gateway_session_id)
{
    string login_ref;
    string login_session_id;
    mapping termination;

    if (objectp(login_ob)) {
        login_ref = file_name(login_ob);
        login_ob->set_player_ob(0);
        login_ob->set_login_player(0);
        login_ob->set_is_logged_in(0);
    }

    if (objectp(player)) {
        login_session_id = player->query_temp("session_id");
        player->delete_temp("owner/login_route");
        player->delete_temp("owner/player_session_route");
        player->delete_temp("owner/player_session_route_reason");
        player->delete_temp("login/bootstrap_deferred_pending");
        player->delete_temp("login/bootstrap_room_sync");
        player->delete_temp("login/bootstrap_sync");
        /* 清理本玩家滞留的计划/计分/计数条目（force 入队快照不再孤儿化）。 */
        catch(CLIENT_SYNC_D->discard_player_pending(player));
    }

    if (stringp(login_session_id) && login_session_id != "")
        catch(call_other(LOGIN_SESSION, "destroy_session", login_session_id));

    if (stringp(gateway_session_id) && gateway_session_id != "") {
        catch(GATEWAY_SESSION_D->terminate_session_without_resume(gateway_session_id));
        catch(gateway_destroy_session(gateway_session_id));
    }

    /* L-57：交接失败后的对象回收必须与正常登出共用同一生命周期收口，
     * 否则护送租约等由生命周期拥有的资源会随 destruct 泄漏。已提交的
     * 角色数据不动（存档即角色真源），只终结本次运行时对象。 */
    if (objectp(player)) {
        catch(termination = PLAYER_LIFECYCLE_D->terminate_player(player,
            "login_handoff_failed"));
        if (!mapp(termination) || !termination["ok"])
            log_file("system/session", sprintf(
                "[%s] login handoff termination pending for %s: %O\n",
                ctime(time()), get_object_id(player) || "?",
                termination));
    }
    if (objectp(player) && interactive(player))
        catch(remove_interactive(player));
    if (objectp(player)) {
        if (function_exists("save", player))
            catch(call_other(player, "save"));
        destruct(player);
    }

    if (stringp(login_ref) && login_ref != "")
        call_out_walltime_background("destroy_failed_login_object_by_path", 1,
            login_ref);
    return 0;
}

int complete_login_handoff(object login_ob, object player)
{
    mapping login_route;
    mapping player_session_route;
    mapping client_sync_pending;
    mapping fifo_snapshot;
    mapping resume_payload;
    string gateway_session_id;
    mixed xk_protocol_version;
    mixed err;
    object room;
    int handoff_transaction_drop_baseline;
    mapping phase_started_at;
    mapping bootstrap_started_at;

    if (!objectp(login_ob) || !objectp(player))
        return 0;

    phase_started_at = call_other(LOGIN_SESSION, "begin_loadtest_login_phase_timing");
    gateway_session_id = GATEWAY_SESSION_D->query_object_session_id(login_ob);
    if (!stringp(gateway_session_id) || gateway_session_id == "") {
        log_file("system/session", sprintf(
            "login handoff failed: missing gateway session login=%O temp=%s interactive=%d\n",
            login_ob, login_ob->query_temp("gateway/session_id") || "",
            interactive(login_ob)));
        call_other(LOGIN_SESSION, "record_loadtest_login_phase_timing",
            "handoff_session", phase_started_at, 0);
        return 0;
    }

    phase_started_at = call_other(LOGIN_SESSION, "begin_loadtest_login_phase_timing");
    if (!call_other(login_ob, "handoff_login_connection", player, login_ob)) {
        call_other(LOGIN_SESSION, "record_loadtest_login_phase_timing",
            "handoff_connection", phase_started_at, 0);
        log_file("system/session", sprintf("[%s] login handoff failed: player=%O login=%O\n",
            ctime(time()), player, login_ob));
        return 0;
    }
    call_other(LOGIN_SESSION, "record_loadtest_login_phase_timing",
        "handoff_connection", phase_started_at, 1);

    phase_started_at = call_other(LOGIN_SESSION, "begin_loadtest_login_phase_timing");
    xk_protocol_version = login_ob->query_temp("xk_protocol_version");
    if (stringp(xk_protocol_version) && xk_protocol_version != "")
        player->set_temp("xk_protocol_version", xk_protocol_version);

    if (!GATEWAY_SESSION_D->handoff_session(
            gateway_session_id, login_ob, player, "player")) {
        log_file("system/session", "login handoff failed: gateway session handoff\n");
        call_other(LOGIN_SESSION, "record_loadtest_login_phase_timing",
            "handoff_session", phase_started_at, 0);
        return fail_closed_login_handoff(login_ob, player, gateway_session_id);
    }
    /* 登录 bootstrap 期间允许完整事务暂存到 pending；路由切换的瞬时窗口
     * 不得把状态/任务事务误记为 drop。成功或失败收口时清除该标记。 */
    player->set_temp("login/bootstrap_sync", 1);
    err = catch(login_route = OWNER_RUNTIME_D->query_service_owner_route(
        "login", gateway_session_id));
    if (err || !mapp(login_route) || login_route["ok"] != 1) {
        log_file("system/session", sprintf(
            "login handoff failed: login owner route %O\n", err || login_route));
        call_other(LOGIN_SESSION, "record_loadtest_login_phase_timing",
            "handoff_session", phase_started_at, 0);
        return fail_closed_login_handoff(login_ob, player, gateway_session_id);
    }
    login_route["direct_cross_owner_write"] = 0;
    player->set_temp("owner/login_route", login_route);

    if (!function_exists("refresh_player_session_owner_route", player)
        || !mapp(player_session_route =
            player->refresh_player_session_owner_route("login_handoff"))) {
        log_file("system/session", "login handoff failed: player owner route\n");
        call_other(LOGIN_SESSION, "record_loadtest_login_phase_timing",
            "handoff_session", phase_started_at, 0);
        return fail_closed_login_handoff(login_ob, player, gateway_session_id);
    }
    player->set_temp("owner/player_session_route", player_session_route);

    resume_payload = GATEWAY_SESSION_D->issue_resume_payload(
        gateway_session_id, player);
    if (!mapp(resume_payload) || sizeof(resume_payload) == 0
        || !call_other(XK_PROTOCOL_CORE, "xk_send_report",
            player, XK_REPORT_SESSION_RESUME, resume_payload)) {
        log_file("system/session", "login handoff failed: resume payload\n");
        call_other(LOGIN_SESSION, "record_loadtest_login_phase_timing",
            "handoff_session", phase_started_at, 0);
        return fail_closed_login_handoff(login_ob, player, gateway_session_id);
    }
    call_other(LOGIN_SESSION, "record_loadtest_login_phase_timing",
        "handoff_session", phase_started_at, 1);

    bootstrap_started_at = call_other(LOGIN_SESSION, "begin_loadtest_login_phase_timing");
    room = environment(player);
    if (!objectp(room)) {
        log_file("system/session", "login handoff failed: player has no room\n");
        call_other(LOGIN_SESSION, "record_loadtest_login_phase_timing",
            "handoff_bootstrap", bootstrap_started_at, 0);
        return fail_closed_login_handoff(login_ob, player, gateway_session_id);
    }

    /* 投递 drop 基线（按玩家隔离）：bootstrap 期间新增 drop（非入队路径）即视为投递失败。 */
    handoff_transaction_drop_baseline =
        CLIENT_SYNC_D->query_player_transaction_drops(player);

    phase_started_at = call_other(LOGIN_SESSION, "begin_loadtest_login_phase_timing");
    err = catch(refresh_login_room(room));
    call_other(LOGIN_SESSION, "record_loadtest_login_phase_timing",
        "handoff_shared_room_refresh", phase_started_at, err ? 0 : 1);
    if (err) {
        log_file("system/session", sprintf(
            "login handoff failed: room refresh %O\n", err));
        call_other(LOGIN_SESSION, "record_loadtest_login_phase_timing",
            "handoff_bootstrap", bootstrap_started_at, 0);
        return fail_closed_login_handoff(login_ob, player, gateway_session_id);
    }

    phase_started_at = call_other(LOGIN_SESSION, "begin_loadtest_login_phase_timing");
    CLIENT_SYNC_D->handoff_player_sync(player);
    call_other(LOGIN_SESSION, "record_loadtest_login_phase_timing",
        "handoff_sync_plan", phase_started_at, 1);

    /* 离线挂机结算：重连时一次性结算，结果随首次快照下发 */
    catch(call_other(IDLE_RUNTIME_D, "settle_offline_session", player));
    catch(call_other(ANNOUNCE_D, "push_to_player", player));

    /* 登录恢复事件移到 full snapshot 之前——handler 的
     * changed domains（货币/quest/库存/状态）自然进入同一 bootstrap
     * snapshot 下发；kernel 只发稳定系统事件，不直接依赖业务 daemon。
     * dispatcher 完全不可用时由 manifest critical 字段决定（当前全
     * 非 critical，记录 health 不阻断登录）。 */
    catch(call_other(PLAYER_LIFECYCLE_D, "dispatch", "login", player));

    phase_started_at = call_other(LOGIN_SESSION, "begin_loadtest_login_phase_timing");
    CLIENT_SYNC_D->request_full_player_snapshot(player);
    call_other(LOGIN_SESSION, "record_loadtest_login_phase_timing",
        "handoff_private_projection", phase_started_at, 1);

    phase_started_at = call_other(LOGIN_SESSION, "begin_loadtest_login_phase_timing");
    CLIENT_SYNC_D->flush_player_sync(player);
    call_other(LOGIN_SESSION, "record_loadtest_login_phase_timing",
        "handoff_sync_flush", phase_started_at, 1);

    phase_started_at = call_other(LOGIN_SESSION, "begin_loadtest_login_phase_timing");
    client_sync_pending = CLIENT_SYNC_D->query_player_sync_pending_status(player);
    fifo_snapshot = GATEWAY_SESSION_D->query_session_fifo_snapshot(gateway_session_id);
    if (!mapp(client_sync_pending) || client_sync_pending["core_pending"] != 0
        || !mapp(fifo_snapshot)
        /* owner protocol reservation 由异步编码完成；bootstrap_complete
         * 也进入同一 FIFO，队列顺序保证其晚于已登记的快照包，不能在
         * 这里要求 reservation 已归零。真正的 FIFO 违规仍必须失败。 */
        || fifo_snapshot["fifo_violations"] != 0
        /* 投递 drop 增量核对（按玩家隔离）：bootstrap 期间本玩家有事务被 drop 即失败。 */
        || CLIENT_SYNC_D->query_player_transaction_drops(player)
            != handoff_transaction_drop_baseline) {
        log_file("system/session", sprintf(
            "login handoff failed: fifo fill pending=%O fifo=%O drops=%d/%d\n",
            client_sync_pending, fifo_snapshot,
            CLIENT_SYNC_D->query_player_transaction_drops(player),
            handoff_transaction_drop_baseline));
        call_other(LOGIN_SESSION, "record_loadtest_login_phase_timing",
            "handoff_fifo_fill", phase_started_at, 0);
        call_other(LOGIN_SESSION, "record_loadtest_login_phase_timing",
            "handoff_bootstrap", bootstrap_started_at, 0);
        return fail_closed_login_handoff(login_ob, player, gateway_session_id);
    }
    player->set_temp("login/bootstrap_deferred_pending",
        client_sync_pending["deferred_pending"]);
    CLIENT_SYNC_D->clear_player_transaction_drops(player);
    call_other(LOGIN_SESSION, "record_loadtest_login_phase_timing",
        "handoff_fifo_fill", phase_started_at, 1);

    if (!call_other(XK_PROTOCOL_CORE, "xk_send", player, XK_LOGN, ([
        "action": "bootstrap_complete",
        "message": "",
    ]))) {
        log_file("system/session", "login handoff failed: bootstrap complete send\n");
        call_other(LOGIN_SESSION, "record_loadtest_login_phase_timing",
            "handoff_success_barrier", phase_started_at, 0);
        call_other(LOGIN_SESSION, "record_loadtest_login_phase_timing",
            "handoff_bootstrap", bootstrap_started_at, 0);
        return fail_closed_login_handoff(login_ob, player, gateway_session_id);
    }
    call_other(LOGIN_SESSION, "record_loadtest_login_phase_timing",
        "handoff_success_barrier", phase_started_at, 1);
    call_other(LOGIN_SESSION, "record_loadtest_login_phase_timing",
        "handoff_bootstrap", bootstrap_started_at, 1);
    call_out_walltime_background("prime_player_read_caches_by_path", 1,
        file_name(player));
    CLIENT_SYNC_D->request_light_status_delayed(player, 1);
    player->delete_temp("login/bootstrap_room_sync");
    player->delete_temp("login/bootstrap_sync");
    return 1;
}
