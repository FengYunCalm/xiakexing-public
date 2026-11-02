/**
 * @file    login_input_endpoint.c
 * @brief   登录消息端点辅助子服务
 * @author  XiaKeXing Development Team
 * @date    2026-03-30
 * @version 1.0.0
 */

#pragma strict_types

void receive_message(object login_ob, string msgclass, string msg)
{
    if (!objectp(login_ob))
        return;
    if (!stringp(msg) || msg == "")
        return;
    if (!interactive(login_ob))
        return;

    login_ob->receive(msg);
}
