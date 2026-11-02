/**
 * @file    login_input_ingress.c
 * @brief   登录输入入口分流辅助子服务
 * @author  XiaKeXing Development Team
 * @date    2026-03-30
 * @version 1.0.0
 */

#pragma strict_types

#include <system/services.h>
#include <system/xk_protocol.h>

#define LOGIN_INPUT_SUPPORT  "/kernel/login/login_input_support"
#define LOGIN_PLAYER_DISPATCH "/kernel/login/login_player_dispatch"
#define LOGIN_SESSION        "/kernel/login/login_session"
#define LOGIN_TEXT_ROUTER    "/kernel/login/login_text_router"

private object query_resume_login_target(object login_ob)
{
    object target;

    if (!objectp(login_ob))
        return 0;

    target = call_other(login_ob, "query_login_player");
    return objectp(target) ? target : login_ob;
}

private void emit_resume_login_failure(object login_ob, string message, string level)
{
    call_other(login_ob, "emit_login_feedback",
        query_resume_login_target(login_ob), message, level);
}

private void restart_login_after_resume_failure(object login_ob)
{
    object target;

    target = query_resume_login_target(login_ob);
    if (objectp(target) && function_exists("delete_temp", target))
        target->delete_temp("gateway/logn_prompt_sent_at");
    if (objectp(login_ob) && function_exists("delete_temp", login_ob))
        login_ob->delete_temp("gateway/logn_prompt_sent_at");
    call_other(login_ob, "logon");
}

/* XKSESS 会把登录对象绑定到报文里给出的 session_id，这是 gateway 直连的内部协议：
 * 只接受本机回环来源（gateway 与 driver 同机直连），公网/未知来源的客户端文本不得进入该分支。 */
private int is_trusted_gateway_origin(object login_ob)
{
    string ip;

    if (!objectp(login_ob))
        return 0;
    ip = query_ip_number(login_ob);
    return stringp(ip) && (ip == "127.0.0.1" || ip == "::1");
}

private int handle_gateway_session_sync(object login_ob, string trimmed_input)
{
    mapping login_route;
    string session_id;
    mixed err;

    if (!objectp(login_ob) || !stringp(trimmed_input))
        return 0;
    if (strlen(trimmed_input) <= 8 || trimmed_input[0..6] != "XKSESS ")
        return 0;

    if (!is_trusted_gateway_origin(login_ob)) {
        log_file("system/security", sprintf("[SECURITY] rejected XKSESS from %s\n",
            stringp(query_ip_number(login_ob)) ? query_ip_number(login_ob) : "unknown"));
        emit_resume_login_failure(login_ob, "连接来源不可信，已拒绝同步请求。", "error");
        return 1;
    }

    session_id = call_other(LOGIN_INPUT_SUPPORT, "trim_input", trimmed_input[7..]);
    if (!stringp(session_id) || session_id == "")
        return 1;

    if (!mapp(GATEWAY_SESSION_D->open_session(session_id, ([ "source": "direct_proxy" ])))
        || !GATEWAY_SESSION_D->bind_session_target(session_id, login_ob, "login")) {
        emit_resume_login_failure(login_ob, "登录连接 owner 尚未就绪，请稍后重试。", "error");
        return 1;
    }
    err = catch(login_route = OWNER_RUNTIME_D->query_service_owner_route("login", session_id));
    if (err || !mapp(login_route) || login_route["ok"] != 1) {
        emit_resume_login_failure(login_ob, "登录入口 owner 尚未就绪，请稍后重试。", "error");
        return 1;
    }
    login_route["direct_cross_owner_write"] = 0;
    login_ob->set_temp("owner/login_route", login_route);
    login_ob->delete_temp("client/plain_text_only");
    login_ob->set_temp("xk_protocol_version", XK_PROTOCOL_VERSION_STRING);
    return 1;
}

private int is_command_envelope_candidate(string value)
{
    if (!stringp(value) || strlen(value) < 1 || value[0] != '{')
        return 0;
    return 1;
}

private int handle_plain_xk_capability(object login_ob, string trimmed_input)
{
    if (!objectp(login_ob) || !stringp(trimmed_input))
        return 0;
    if (trimmed_input != "XKCAPA" && (strlen(trimmed_input) <= 7 || trimmed_input[0..6] != "XKCAPA "))
        return 0;

    login_ob->delete_temp("client/plain_text_only");
    login_ob->set_temp("xk_protocol_version", XK_PROTOCOL_VERSION_STRING);
    return 1;
}

private int handle_resume_login(object login_ob, string trimmed_input)
{
    mapping resume_record;
    object player;
    string account_id;
    string resume_token;
    string session_id;

    if (!objectp(login_ob) || !stringp(trimmed_input))
        return 0;
    if (strlen(trimmed_input) <= 7 || trimmed_input[0..6] != "XKRESM ")
        return 0;

    resume_token = call_other(LOGIN_INPUT_SUPPORT, "trim_input", trimmed_input[7..]);
    session_id = GATEWAY_SESSION_D->query_object_session_id(login_ob);
    if (!stringp(session_id) || session_id == "") {
        emit_resume_login_failure(login_ob, "恢复会话已失效，请重新登录", "warning");
        restart_login_after_resume_failure(login_ob);
        return 1;
    }

    resume_record = GATEWAY_SESSION_D->consume_resume_record(resume_token);
    account_id = mapp(resume_record) && stringp(resume_record["account_id"])
        ? resume_record["account_id"]
        : "";

    if (account_id == "") {
        emit_resume_login_failure(login_ob, "恢复会话已失效，请重新登录", "warning");
        restart_login_after_resume_failure(login_ob);
        return 1;
    }

    GATEWAY_SESSION_D->open_session(session_id, ([
        "source": "resume_token",
        "resume_token": resume_token,
    ]));
    player = call_other(LOGIN_SESSION, "enter_game", account_id, login_ob);
    if (!objectp(player)) {
        emit_resume_login_failure(login_ob, "恢复会话失败，请重新登录", "error");
        restart_login_after_resume_failure(login_ob);
        return 1;
    }

    if (!call_other(login_ob, "finish_login_transition", player, "会话恢复成功！", 1)) {
        emit_resume_login_failure(login_ob, "恢复会话连接切换失败", "error");
        return 1;
    }

    return 1;
}

string process_login_ingress(object login_ob, string raw_input, string trimmed_input)
{
    object recipient;

    /* A typed command is a post-login player command.  Before login it must
     * never fall through to username/password parsing or the XK control-line
     * handlers. */
    if (is_command_envelope_candidate(trimmed_input)) {
        if (login_ob->query_is_logged_in() && objectp(login_ob->query_player_ob()))
            return call_other(LOGIN_PLAYER_DISPATCH, "dispatch_player_command",
                login_ob, trimmed_input);
        call_other(login_ob, "emit_login_feedback", login_ob,
            "无效命令封套，已拒绝。", "warning");
        return "";
    }

    if (handle_gateway_session_sync(login_ob, trimmed_input))
        return "";
    if (handle_plain_xk_capability(login_ob, trimmed_input))
        return "";
    if (handle_resume_login(login_ob, trimmed_input))
        return "";

    if (call_other(LOGIN_INPUT_SUPPORT, "is_cancel_command", trimmed_input)
        && login_ob->query_reg_stage() != 0) {
        login_ob->handle_registration_cancel();
        login_ob->logon();
        return "";
    }

    if (login_ob->query_is_logged_in() && objectp(login_ob->query_player_ob()))
        return call_other(LOGIN_PLAYER_DISPATCH, "dispatch_player_command", login_ob, trimmed_input);

    recipient = login_ob->query_login_player();
    if (!objectp(recipient))
        recipient = login_ob;

    if (strlen(trimmed_input) > 3 && trimmed_input[0..2] == XK_HEADER)
        return login_ob->handle_xk_protocol(trimmed_input, recipient);

    if (strlen(trimmed_input) > 8 && trimmed_input[0..8] == "XKCREATE ")
        return login_ob->handle_xk_create_json(trimmed_input[9..]);

    return call_other(LOGIN_TEXT_ROUTER, "process_standard_login", login_ob, raw_input);
}
