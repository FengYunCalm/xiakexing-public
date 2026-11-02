/**
 * @file    login_context.c
 * @brief   登录会话上下文辅助子服务
 * @author  XiaKeXing Development Team
 * @date    2026-03-30
 * @version 1.0.0
 */

#pragma strict_types

#define LOGIN_INPUT_SECURITY "/kernel/login/login_input_security"
#define LOGIN_INPUT_SUPPORT  "/kernel/login/login_input_support"

object current_login_recipient(object login_ob)
{
    object player;

    player = login_ob->query_login_player();
    return player ? player : login_ob;
}

string query_login_ip(object login_ob)
{
    return call_other(LOGIN_INPUT_SECURITY, "query_login_ip",
        current_login_recipient(login_ob), login_ob);
}

void emit_login_feedback(object login_ob, object target, string message, string level)
{
    object recipient;

    recipient = target ? target : login_ob;
    call_other(LOGIN_INPUT_SUPPORT, "emit_login_feedback", recipient, message, level);
}
