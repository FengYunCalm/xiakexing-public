/**
 * @file    event_listener.c
 * @brief   事件监听子模块



 *
 * @par Description:
 * 事件系统内部子模块，供 event_d 或相关门面组合使用。
 * `core/event/*` 仅保留 compatibility wrapper 职责，监听器集合与触发顺序都以 /daemons/core/event_d 为准。
 */

#pragma strict_types
#pragma save_types

void on(string event, object listener, string method, int priority)
{
    "/daemons/core/event_d"->on(event, listener, method, priority);
}

void once(string event, object listener, string method)
{
    "/daemons/core/event_d"->once(event, listener, method);
}

void off(string event, object listener, string method)
{
    "/daemons/core/event_d"->off(event, listener, method);
}

object *get_listeners(string event)
{
    return "/daemons/core/event_d"->get_listeners(event);
}

int has_listener(string event)
{
    return "/daemons/core/event_d"->has_listener(event);
}
