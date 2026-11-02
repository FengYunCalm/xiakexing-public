/**
 * @file    alert_history.c
 * @brief   告警历史子模块



 *
 * @par Description:
 * 告警系统内部子模块。
 */

#pragma strict_types
#pragma save_types

#define ALERT_SYSTEM "/lib/framework/alert_system"

void add_alert_history(mapping record)
{
    call_other(ALERT_SYSTEM, "add_alert_history", record);
}

mixed *get_alert_history(string metric_name, int level)
{
    return call_other(ALERT_SYSTEM, "get_alert_history", metric_name, level);
}

void clear_alert_history()
{
    call_other(ALERT_SYSTEM, "clear_alert_history");
}
