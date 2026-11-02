/**
 * @file    event_d.c
 * @brief   事件总线真源



 *
 * @par Description:
 * 提供全局事件监听、一次性监听和广播分发能力，是当前运行期事件总线真源。
 * 其他模块应把这里视为“事件路由与分发中心”，而不是各自维护第二套监听器表。
 *
 * 当前职责：
 * - 监听器注册/注销
 * - 按优先级分发事件
 * - once 监听器生命周期管理
 * - 热点事件与监听统计视图
 *
 * 不承担：
 * - 业务事件语义真源
 * - 监控/健康判断真源
 * - 告警历史或审计日志真源
 */

#pragma strict_types
#pragma save_types

inherit "/lib/framework/daemon";

nosave mapping _listeners;
nosave mapping _once_listeners;
nosave mapping _event_counts;
nosave int _event_count;

private string normalize_event(string event)
{
    if (!stringp(event))
        return "";
    event = lower_case(event);
    event = replace_string(event, " ", "_");
    return event;
}

private int is_listener_registered(mapping *event_listeners, object listener, string method)
{
    mapping existing;

    if (!arrayp(event_listeners))
        return 0;
    foreach (existing in event_listeners) {
        if (existing["object"] == listener && existing["method"] == method)
            return 1;
    }
    return 0;
}

private mapping *insert_listener_by_priority(mapping *event_listeners, mapping new_listener)
{
    int i;
    int insert_pos;
    int priority;

    /*
     * 监听器按 priority 降序插入，
     * 不是为了排序美观，而是为了确保高优先级监听器总能先消费事件。
     * 对权限拦截、状态同步、日志旁路等监听器来说，这个顺序是有语义的。
     */
    priority = new_listener["priority"];
    insert_pos = sizeof(event_listeners);
    for (i = 0; i < sizeof(event_listeners); i++) {
        if (event_listeners[i]["priority"] < priority) {
            insert_pos = i;
            break;
        }
    }
    if (insert_pos == sizeof(event_listeners))
        return event_listeners + ({ new_listener });
    return event_listeners[0..insert_pos-1] + ({ new_listener }) + event_listeners[insert_pos..];
}

private void invoke_listener(mapping listener, string event, mixed data)
{
    object ob;
    string method;
    mixed err;

    ob = listener["object"];
    method = listener["method"];
    if (!objectp(ob) || !stringp(method) || method == "")
        return;
    err = catch(call_other(ob, method, event, data));
    if (err)
        log_file("system/event", sprintf("[%s] invoke_listener: %s::%s for %s failed: %O\n", ctime(time())[4..19], file_name(ob), method, event, err));
}

void create()
{
    ::create();
    _listeners = ([]);
    _once_listeners = ([]);
    _event_counts = ([]);
    _event_count = 0;
    log_file("system/event", sprintf("[%s] Event daemon initialized\n", ctime(time())[4..19]));
}

void on(string event, object listener, string method, int priority)
{
    mapping *event_listeners;
    mapping new_listener;

    if (!stringp(event) || event == "" || !objectp(listener))
        return;
    event = normalize_event(event);
    if (!stringp(method) || method == "")
        method = "on_" + replace_string(event, ".", "_");
    if (!intp(priority))
        priority = 0;
    event_listeners = _listeners[event];
    if (!arrayp(event_listeners))
        event_listeners = ({ });
    if (is_listener_registered(event_listeners, listener, method))
        return;
    new_listener = (["object": listener, "method": method, "priority": priority]);
    _listeners[event] = insert_listener_by_priority(event_listeners, new_listener);
}

void once(string event, object listener, string method)
{
    mapping *listeners;

    if (!stringp(event) || event == "" || !objectp(listener))
        return;
    event = normalize_event(event);
    if (!stringp(method) || method == "")
        method = "on_" + replace_string(event, ".", "_");
    listeners = _once_listeners[event];
    if (!arrayp(listeners))
        listeners = ({ });
    if (is_listener_registered(listeners, listener, method))
        return;
    _once_listeners[event] = listeners + ({ (["object": listener, "method": method, "priority": 0]) });
}

void off(string event, object listener, string method)
{
    mapping *listeners;
    mapping *new_listeners;
    mapping item;

    if (!objectp(listener))
        return;
    if (event == "*") {
        string evt;
        foreach (evt in keys(_listeners))
            off(evt, listener, method);
        foreach (evt in keys(_once_listeners))
            off(evt, listener, method);
        return;
    }
    event = normalize_event(event);
    listeners = _listeners[event];
    if (arrayp(listeners)) {
        new_listeners = ({ });
        foreach (item in listeners) {
            if (item["object"] == listener && (!stringp(method) || method == "" || item["method"] == method))
                continue;
            new_listeners += ({ item });
        }
        if (sizeof(new_listeners)) _listeners[event] = new_listeners; else map_delete(_listeners, event);
    }
    listeners = _once_listeners[event];
    if (arrayp(listeners)) {
        new_listeners = ({ });
        foreach (item in listeners) {
            if (item["object"] == listener && (!stringp(method) || method == "" || item["method"] == method))
                continue;
            new_listeners += ({ item });
        }
        if (sizeof(new_listeners)) _once_listeners[event] = new_listeners; else map_delete(_once_listeners, event);
    }
}

object *get_listeners(string event)
{
    mapping *listeners;
    object *out;
    mapping item;

    event = normalize_event(event);
    listeners = _listeners[event];
    out = ({ });
    if (!arrayp(listeners))
        return out;
    foreach (item in listeners)
        if (objectp(item["object"])) out += ({ item["object"] });
    return out;
}

int has_listener(string event)
{
    event = normalize_event(event);
    return arrayp(_listeners[event]) && sizeof(_listeners[event]) > 0;
}

void emit(string event, mixed data)
{
    mapping *listeners;
    mapping *once_listeners;
    mapping item;

    /*
     * emit 的分发顺序是刻意设计的：
     * 1. 普通监听器
     * 2. once 监听器（执行后即移除）
     * 3. `*` 通配监听器
     * 这样能保证具体事件监听器先拿到最原始的数据，
     * 再由全局监听器做监控、统计或日志记录。
     */
    if (!stringp(event) || event == "")
        return;
    event = normalize_event(event);
    _event_count++;
    _event_counts[event] = (_event_counts[event] || 0) + 1;

    listeners = copy(_listeners[event] || ({ }));
    foreach (item in listeners)
        invoke_listener(item, event, data);

    once_listeners = copy(_once_listeners[event] || ({ }));
    map_delete(_once_listeners, event);
    foreach (item in once_listeners)
        invoke_listener(item, event, data);

    listeners = copy(_listeners["*"] || ({ }));
    foreach (item in listeners)
        invoke_listener(item, event, data);
}

void trigger_event(string event, mixed data)
{
    emit(event, data);
}

void broadcast(string event, mixed data)
{
    emit(event, data);
}

void clear_all_listeners()
{
    _listeners = ([]);
    _once_listeners = ([]);
}

void reset_event_count()
{
    _event_count = 0;
    _event_counts = ([]);
}

mapping query_event_trigger_stats()
{
    return copy(_event_counts || ([]));
}

int query_event_count()
{
    return _event_count;
}

string *get_events()
{
    return keys(_listeners || ([]));
}

mapping query_event_stats()
{
    mapping stats;
    string event;

    stats = ([]);
    foreach (event in keys(_listeners))
        stats[event] = sizeof(_listeners[event]);
    return stats;
}

mixed *query_hot_events(int top_n)
{
    mapping stats;
    string *events;
    mixed *result;
    int i;
    int j;
    string tmp;

    if (!intp(top_n) || top_n <= 0)
        top_n = 10;
    stats = query_event_trigger_stats();
    events = keys(stats);
    for (i = 0; i < sizeof(events); i++) {
        for (j = i + 1; j < sizeof(events); j++) {
            if ((stats[events[j]] || 0) > (stats[events[i]] || 0)) {
                tmp = events[i];
                events[i] = events[j];
                events[j] = tmp;
            }
        }
    }
    result = ({ });
    for (i = 0; i < sizeof(events) && i < top_n; i++)
        result += ({ ({ events[i], stats[events[i]] }) });
    return result;
}
