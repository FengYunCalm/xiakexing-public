/**
 * @file    login_input_security.c
 * @brief   登录安全窗口辅助子服务
 * @author  XiaKeXing Development Team
 * @date    2026-03-30
 * @version 1.0.0
 */

#pragma strict_types

#include <system/daemon.h>

string query_login_ip(object recipient, object fallback)
{
    string ip;

    if (objectp(recipient))
        ip = query_ip_number(recipient);

    if ((!stringp(ip) || ip == "") && objectp(fallback))
        ip = query_ip_number(fallback);

    return stringp(ip) ? ip : "";
}

varargs int validate_login_security(object recipient, object fallback, string account)
{
    string ip;

    // 地址只取驱动提供的连接对端（query_ip_number），不接受客户端自报字段。
    ip = query_login_ip(recipient, fallback);
    if (!stringp(ip) || ip == "") {
        // 取不到可信地址时 fail-closed：unknown 不等豁免，否则限流与封禁都可绕过。
        log_file("system/security", sprintf("[SECURITY] login address unknown, denied account=%s\n",
            stringp(account) ? account : ""));
        return 0;
    }

    if (call_other(SECURITY_MONITOR_D, "is_banned", ip))
        return 0;

    if (stringp(account) && account != "") {
        if (!call_other(SECURITY_MONITOR_D, "check_login_rate_limit", ip, account))
            return 0;
        return 1;
    }

    if (!call_other(SECURITY_MONITOR_D, "check_rate_limit", ip, "login"))
        return 0;

    return 1;
}

void reset_login_security_window(object recipient, object fallback)
{
    string ip;

    ip = query_login_ip(recipient, fallback);
    if (!stringp(ip) || ip == "")
        return;

    call_other(SECURITY_MONITOR_D, "reset_rate_counter", ip, "login");
    call_other(SECURITY_MONITOR_D, "reset_rate_counter", ip, "login_loadtest");
}
