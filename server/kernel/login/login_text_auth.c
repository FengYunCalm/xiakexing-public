/**
 * @file    login_text_auth.c
 * @brief   登录文本认证辅助子服务
 * @author  XiaKeXing Development Team
 * @date    2026-03-30
 * @version 1.0.0
 */

#pragma strict_types

#include <system/errors.h>

#define LOGIN_AUTH      "/kernel/login/login_auth"
#define LOGIN_SESSION   "/kernel/login/login_session"

mapping parse_credentials(string input)
{
    mapping result;

    result = (["username": "", "password": "", "valid": 0]);

    if (strsrch(input, ":") <= 0)
        return result;

    sscanf(input, "%s:%s", result["username"], result["password"]);
    result["valid"] = 1;
    return result;
}

int validate_username_format(string username)
{
    return call_other(LOGIN_AUTH, "valid_username", username);
}

private int finish_credential_auth_timing(mapping started_at, int result)
{
    call_other(LOGIN_SESSION, "record_loadtest_login_phase_timing",
        "credential_auth", started_at, result);
    return result;
}

int handle_user_authentication(object login_ob, string username, string password)
{
    int verify_result;
    object player;
    mapping started_at;

    started_at = call_other(LOGIN_SESSION, "begin_loadtest_login_phase_timing");

    if (!call_other(login_ob, "validate_login_security", username))
        return finish_credential_auth_timing(started_at, 0);

    verify_result = call_other(LOGIN_AUTH, "authenticate_user", username, password);

    if (verify_result == ERR_PLAYER_NOT_FOUND) {
        return finish_credential_auth_timing(started_at,
            call_other(login_ob, "start_character_creation", username, password, "manual"));
    }

    if (verify_result == 2) {
        return finish_credential_auth_timing(started_at,
            call_other(login_ob, "start_character_creation", username, password, "existing"));
    }

    if (verify_result != 1) {
        call_other(login_ob, "emit_login_feedback",
            call_other(login_ob, "query_login_player"), "账号或密码错误", "error");
        return finish_credential_auth_timing(started_at, 0);
    }

    player = call_other(LOGIN_SESSION, "enter_game", username, login_ob);
    if (!player) {
        call_other(login_ob, "emit_login_feedback",
            call_other(login_ob, "query_login_player"), "进入游戏失败", "error");
        return finish_credential_auth_timing(started_at, 0);
    }

    if (!call_other(login_ob, "finish_login_transition", player, "登录成功！", 1)) {
        call_other(login_ob, "emit_login_feedback",
            call_other(login_ob, "query_login_player"), "进入游戏连接切换失败", "error");
        return finish_credential_auth_timing(started_at, 0);
    }

    return finish_credential_auth_timing(started_at, 1);
}

void handle_login_stage(object login_ob, string input)
{
    mapping creds;

    creds = parse_credentials(input);
    if (!creds["valid"]) {
        call_other(login_ob, "emit_login_feedback",
            call_other(login_ob, "query_login_player"), "请使用 账号:密码 格式登录", "info");
        return;
    }

    if (!validate_username_format(creds["username"])) {
        call_other(login_ob, "emit_login_feedback",
            call_other(login_ob, "query_login_player"), "账号格式不正确", "error");
        return;
    }

    handle_user_authentication(login_ob, creds["username"], creds["password"]);
}
