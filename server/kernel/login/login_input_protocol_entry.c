/**
 * @file    login_input_protocol_entry.c
 * @brief   登录协议与文本入口门面子服务
 * @author  XiaKeXing Development Team
 * @date    2026-03-30
 * @version 1.0.0
 */

#pragma strict_types

string handle_xk_create_json(object login_ob, string json_str)
{
    return call_other("/kernel/login/login_xk_create", "handle_xk_create_json", login_ob, json_str);
}

string process_standard_login(object login_ob, string str)
{
    return call_other("/kernel/login/login_text_router", "process_standard_login", login_ob, str);
}
