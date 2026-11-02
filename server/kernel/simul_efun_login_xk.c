/**
 * @file    simul_efun_login_xk.c
 * @brief   登录 XK 轻量协议发送辅助子服务
 * @author  XiaKeXing Development Team
 * @date    2026-03-30
 * @version 1.0.0
 */

#pragma strict_types

#include <system/xk_protocol.h>

#define XK_PROTOCOL_CORE "/kernel/framework/xk_protocol_core"

void xk_send_register(object me, string stage, string message)
{
    mapping payload;

    if (!me || !interactive(me))
        return;

    payload = ([
        "stage": stage || "",
        "message": message || "",
    ]);
    call_other(XK_PROTOCOL_CORE, "xk_send", me, XK_REGI, payload);
}

void xk_send_login_prompt(object me)
{
    if (!me || !interactive(me))
        return;

    call_other(XK_PROTOCOL_CORE, "xk_send", me, XK_LOGN, ([
        "action": "prompt",
        "message": ""
    ]));
}
