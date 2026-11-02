/**
 * @file    alert_check.c
 * @brief   告警检查子模块



 *
 * @par Description:
 * 告警系统内部子模块。
 */

#pragma strict_types
#pragma save_types

#define ALERT_SYSTEM "/lib/framework/alert_system"

int check_alert(string metric_name, float value)
{
    return call_other(ALERT_SYSTEM, "check_alert", metric_name, value);
}
