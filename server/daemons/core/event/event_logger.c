/**
 * @file    event_logger.c
 * @brief   事件日志子模块



 *
 * @par Description:
 * 事件系统内部子模块，供 event_d 或相关门面组合使用。
 * `core/event/*` 仅保留 compatibility wrapper 职责，事件状态与调度顺序都以 /daemons/core/event_d 为准。
 */

#pragma strict_types
#pragma save_types

int query_event_count()
{
    return "/daemons/core/event_d"->query_event_count();
}

string *get_events()
{
    return "/daemons/core/event_d"->get_events();
}

mapping query_event_stats()
{
    return "/daemons/core/event_d"->query_event_stats();
}

mixed *query_hot_events(int top_n)
{
    return "/daemons/core/event_d"->query_hot_events(top_n);
}
