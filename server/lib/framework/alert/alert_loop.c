/**
 * @file    alert_loop.c
 * @brief   告警检查循环兼容转发层



 */

#pragma strict_types
#pragma save_types

#define ALERT_SYSTEM "/lib/framework/alert_system"

void alert_check_loop()
{
    call_other(ALERT_SYSTEM, "alert_check_loop");
}

float query_memory_usage()
{
    mixed result;

    result = call_other(ALERT_SYSTEM, "query_memory_usage");
    if (floatp(result))
        return result;
    if (intp(result))
        return to_float(result);

    return 0.0;
}

string get_level_string(int level)
{
    mixed result;

    result = call_other(ALERT_SYSTEM, "get_level_string", level);
    return stringp(result) ? result : "UNKNOWN";
}

mapping get_alert_stats()
{
    mixed stats;

    stats = call_other(ALERT_SYSTEM, "get_alert_stats");
    return mapp(stats) ? stats : ([]);
}
