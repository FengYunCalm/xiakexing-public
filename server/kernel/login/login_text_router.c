/**
 * @file    login_text_router.c
 * @brief   登录文本阶段路由辅助子服务
 * @author  XiaKeXing Development Team
 * @date    2026-03-30
 * @version 1.0.0
 */

#pragma strict_types

#include <system/xk_protocol.h>

#define LOGIN_INPUT_SUPPORT     "/kernel/login/login_input_support"
#define LOGIN_TEXT_AUTH         "/kernel/login/login_text_auth"
#define LOGIN_TEXT_REGISTRATION "/kernel/login/login_text_registration"

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

string process_standard_login(object login_ob, string str)
{
    string trimmed_input;
    int stage;

    trimmed_input = call_other(LOGIN_INPUT_SUPPORT, "trim_input", str);
    stage = login_ob->query_reg_stage();

    if (handle_plain_xk_capability(login_ob, trimmed_input))
        return "";

    if (objectp(login_ob) && !login_ob->query_temp("xk_protocol_version"))
        login_ob->set_temp("client/plain_text_only", 1);

    if (call_other(LOGIN_INPUT_SUPPORT, "is_cancel_command", trimmed_input)) {
        call_other(LOGIN_TEXT_REGISTRATION, "handle_cancel_input", login_ob, stage);
        return "";
    }

    switch (stage) {
        case 0:
            call_other(LOGIN_TEXT_AUTH, "handle_login_stage", login_ob, trimmed_input);
            break;
        case 1:
            call_other(LOGIN_TEXT_REGISTRATION, "handle_name_stage", login_ob, trimmed_input);
            break;
        case 2:
            call_other(LOGIN_TEXT_REGISTRATION, "handle_gender_stage", login_ob, trimmed_input);
            break;
    }

    return "";
}
