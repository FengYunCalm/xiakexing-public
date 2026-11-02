/**
 * @file    alert_dispatch.c
 * @brief   告警分发子模块



 *
 * @par Description:
 * 告警系统内部子模块。
 */

#pragma strict_types
#pragma save_types

#define ALERT_SYSTEM "/lib/framework/alert_system"

int register_alert_handler(string name, string func)
{
    return call_other(ALERT_SYSTEM, "register_alert_handler", name, func);
}

void dispatch_alert(mapping record)
{
    call_other(ALERT_SYSTEM, "dispatch_alert", record);
}

void dispatch_recovery(mapping record)
{
    call_other(ALERT_SYSTEM, "dispatch_recovery", record);
}

void alert_handler_log(mapping record)
{
    call_other(ALERT_SYSTEM, "alert_handler_log", record);
}

void alert_handler_game(mapping record)
{
    call_other(ALERT_SYSTEM, "alert_handler_game", record);
}

void log_alert(mapping record)
{
    call_other(ALERT_SYSTEM, "log_alert", record);
}
