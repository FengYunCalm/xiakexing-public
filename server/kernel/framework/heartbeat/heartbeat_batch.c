/**
 * @file    heartbeat_batch.c
 * @brief   living 侧心跳批处理 facade
 * @author  XiaKeXing Development Team
 * @date    2026-03-20
 * @version 1.1.0
 *
 * @par Description:
 * 为 living 家族暴露一层轻量 batched heartbeat facade。
 * 它只负责把对象侧的局部 handler 注册/注销转发到 HEARTBEAT_D，
 * 不改变原生 `heart_beat()` 行为，也不在这里持有全局调度真相。
 */

#pragma strict_types

#include <system/errors.h>
#include <system/services.h>

nosave mapping _heartbeat_batch_handlers;
nosave string _heartbeat_batch_name;

int unregister_batched_heartbeat(string key);
void clear_batched_heartbeats();

string query_heartbeat_batch_name()
{
    if (stringp(_heartbeat_batch_name) && _heartbeat_batch_name != "")
        return _heartbeat_batch_name;

    return replace_string(base_name(this_object()), "/", ".");
}

void set_heartbeat_batch_name(string batch_name)
{
    if (!stringp(batch_name) || batch_name == "")
        return;

    _heartbeat_batch_name = batch_name;
}

mapping query_batched_heartbeat_handlers()
{
    if (!mapp(_heartbeat_batch_handlers))
        _heartbeat_batch_handlers = ([]);

    return copy(_heartbeat_batch_handlers);
}

string register_batched_heartbeat(string key, string func, int interval, mixed args...)
{
    mapping profile;
    string handler_id;

    if (!mapp(_heartbeat_batch_handlers))
        _heartbeat_batch_handlers = ([]);
    if (!stringp(key) || key == "")
        return 0;

    profile = function_exists("query_heartbeat_profile", this_object())
        ? call_other(this_object(), "query_heartbeat_profile")
        : ([ "id": "default", "interval": 1 ]);
    if (!intp(interval) || interval < 1)
        interval = profile["interval"] || 1;

    if (stringp(_heartbeat_batch_handlers[key]))
        unregister_batched_heartbeat(key);

    handler_id = HEARTBEAT_D->register_handler_with_options(this_object(), func, interval, ([
        "batch": query_heartbeat_batch_name(),
        "profile": profile["id"] || "default",
    ]), args);
    if (!stringp(handler_id) || handler_id == "")
        return 0;

    _heartbeat_batch_handlers[key] = handler_id;
    return handler_id;
}

int unregister_batched_heartbeat(string key)
{
    string handler_id;

    if (!mapp(_heartbeat_batch_handlers) || !stringp(key) || key == "")
        return ERR_INVALID_PARAM;

    handler_id = _heartbeat_batch_handlers[key];
    if (!stringp(handler_id) || handler_id == "")
        return ERR_NOT_FOUND;

    map_delete(_heartbeat_batch_handlers, key);
    return HEARTBEAT_D->unregister_handler(handler_id);
}

void clear_batched_heartbeats()
{
    string key;

    if (!mapp(_heartbeat_batch_handlers))
        return;

    foreach (key in keys(_heartbeat_batch_handlers))
        unregister_batched_heartbeat(key);
}
