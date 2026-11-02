/**
 * @file    login_security_flow.c
 * @brief   登录安全窗口辅助子服务
 * @author  XiaKeXing Development Team
 * @date    2026-03-30
 * @version 1.0.0
 */

#pragma strict_types

#define LOGIN_CONTEXT        "/kernel/login/login_context"
#define LOGIN_INPUT_SECURITY "/kernel/login/login_input_security"

varargs int validate_login_security(object login_ob, string account)
{
    object recipient;

    recipient = call_other(LOGIN_CONTEXT, "current_login_recipient", login_ob);
    if (!call_other(LOGIN_INPUT_SECURITY, "validate_login_security", recipient, login_ob, account)) {
        login_ob->emit_login_feedback(recipient, "当前连接已被限制登录，请稍后再试", "error");
        return 0;
    }

    return 1;
}

void reset_login_security_window(object login_ob)
{
    object recipient;

    recipient = call_other(LOGIN_CONTEXT, "current_login_recipient", login_ob);
    call_other(LOGIN_INPUT_SECURITY, "reset_login_security_window", recipient, login_ob);
}
