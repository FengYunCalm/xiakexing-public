/**
 * @file    timer_d.c
 * @brief   定时器守护进程



 *
 * @par Description:
 * 提供全局定时器管理服务，包括延迟调用、取消、查询和周期性任务支持。
 * 这是运行期定时调度真源，负责句柄和索引一致性，不是单纯的 call_out 语法包装。
 */

#pragma strict_types
#pragma save_types

#include <system/globals.h>
#include <system/constants.h>
#include <system/daemon.h>
#include <system/errors.h>
#include <system/services.h>


inherit CORE_DAEMON_BASE;
inherit "/lib/framework/call_dispatch";
#define TIMER_LOG_PREFIX "[TIMER_D]"

nosave mapping _timers;
nosave int _timer_sequence;
nosave mapping _callback_index;
nosave mapping _owner_index;
nosave int _object_payload_rejections;

/*
 * timer_data 结构:
 * ([
 *     "handle": 定时器句柄,
 *     "callback": 回调函数名,
 *     "owner": 所有者对象,
 *     "delay": 延迟秒数,
 *     "args": 参数数组,
 *     "created": 创建时间,
 *     "call_out_handle": call_out句柄
 * ])
 */

void create();

varargs int call_later(string callback, int delay, object owner, mixed args...);
varargs int call_later_walltime(string callback, int delay, object owner, mixed args...);
varargs int call_later_walltime_background(string callback, int delay,
    object owner, mixed args...);
int cancel_timer(int handle);
int find_timer(string callback, object owner);
int find_timer_by_callback(string callback);
int *find_timers_by_owner(object owner);
int cancel_timers_by_owner(object owner);
mapping query_timer(int handle);

private void execute_timer(int handle);
private int schedule_later(string callback, int delay, object owner, mixed *args,
    int walltime, int background);
private void remove_timer_entry(int handle, mapping timer);
private int generate_handle();
private void add_to_callback_index(string callback, int handle);
private void remove_from_callback_index(string callback, int handle);
private void add_to_owner_index(object owner, int handle);
private void remove_from_owner_index(object owner, int handle);

void create()
{
    ::create();

    _timers = ([]);
    _timer_sequence = 0;
    _callback_index = ([]);
    _owner_index = ([]);
    _object_payload_rejections = 0;

}

private int timer_args_owner_safe(mixed *args)
{
    mixed arg;

    if (!arrayp(args))
        return 1;
    foreach (arg in args) {
        if (objectp(arg))
            return 0;
    }
    return 1;
}

mapping query_timer_owner_route(object owner, string callback)
{
    mapping route;
    string route_key;
    mixed err;

    route_key = objectp(owner) ? file_name(owner) : "missing-owner";
    if (stringp(callback) && callback != "")
        route_key += ":" + callback;
    err = catch(route = OWNER_RUNTIME_D->query_service_owner_route("delayed_callback", route_key));
    if (err || !mapp(route) || route["ok"] != 1)
        return ([
            "ok": 0,
            "service": "delayed_callback",
            "key": route_key,
            "callback": stringp(callback) ? callback : "",
            "owner_path": objectp(owner) ? file_name(owner) : "",
            "failure_reason": err ? sprintf("owner route query failed: %O", err)
                : (mapp(route) ? route["failure_reason"] : "owner route unavailable"),
            "readiness": mapp(route) ? route["readiness"] : ([ "ok": 0 ]),
            "dependencies": mapp(route) ? route["dependencies"] : ([ ]),
            "direct_cross_owner_write": 0,
        ]);

    route["callback"] = stringp(callback) ? callback : "";
    route["owner_path"] = objectp(owner) ? file_name(owner) : "";
    route["direct_cross_owner_write"] = 0;
    return route;
}

mapping query_timer_owner_status()
{
    return ([
        "service_map_v2": 1,
        "service": "delayed_callback",
        "policy": "owner_scheduler",
        "active_timers": mapp(_timers) ? sizeof(_timers) : 0,
        "payload_contract": "spec_snapshot_or_commit_proposal_only",
        "object_payload_allowed": 0,
        "object_payload_rejected": 1,
        "object_payload_rejections": _object_payload_rejections,
        "message_only_cross_owner": 1,
        "direct_cross_owner_write": 0,
    ]);
}
/**
 * @brief 延迟调用函数
 * @param callback 回调函数名
 * @param delay 延迟秒数
 * @param owner 回调函数所属对象（默认this_object()）
 * @param args 可变参数
 * @return 定时器句柄（失败返回0）
 */
private int schedule_later(string callback, int delay, object owner, mixed *args,
    int walltime, int background)
{
    int handle;
    mapping timer;
    mapping owner_route;
    int call_out_handle;

    if (!stringp(callback) || callback == "")
    {
        return ERR_INVALID_PARAM;
    }

    if (!intp(delay) || delay < 0)
    {
        return ERR_INVALID_PARAM;
    }

    if (!objectp(owner))
    {
        owner = previous_object();
    }

    if (!timer_args_owner_safe(args))
    {
        _object_payload_rejections++;
        return ERR_INVALID_PARAM;
    }

    owner_route = query_timer_owner_route(owner, callback);
    if (!mapp(owner_route) || owner_route["ok"] != 1)
        return ERR_SYSTEM_ERROR;

    handle = generate_handle();

    timer = ([
        "handle": handle,
        "callback": callback,
        "owner": owner,
        "delay": delay,
        "walltime": walltime ? 1 : 0,
        "walltime_priority": background ? "background"
            : (walltime ? "normal" : "gametick"),
        "args": args,
        "created": time(),
        "owner_route": owner_route,
        "payload_contract": "spec_snapshot_or_commit_proposal_only",
        "args_owner_safe": 1,
        "object_payload_rejected": 1,
        "direct_cross_owner_write": 0
    ]);

    if (background)
        call_out_handle = call_out_walltime_background("execute_timer", delay, handle);
    else if (walltime)
        call_out_handle = call_out_walltime("execute_timer", delay, handle);
    else
        call_out_handle = call_out("execute_timer", delay, handle);
    timer["call_out_handle"] = call_out_handle;

    _timers[handle] = timer;

    add_to_callback_index(callback, handle);
    add_to_owner_index(owner, handle);

    return handle;
}

varargs int call_later(string callback, int delay, object owner, mixed args...)
{
    return schedule_later(callback, delay, owner, args, 0, 0);
}

/*
 * Walltime delayed callbacks keep the same handle, owner route, arguments and
 * cancellation lifecycle as call_later(), but do not join a saturated game
 * tick's all-eligible call_out drain.
 */
varargs int call_later_walltime(string callback, int delay, object owner,
    mixed args...)
{
    return schedule_later(callback, delay, owner, args, 1, 0);
}

/*
 * Background walltime callbacks retain TIMER_D ownership and cancellation
 * semantics while yielding to normal commands and Gateway I/O when due.
 */
varargs int call_later_walltime_background(string callback, int delay,
    object owner, mixed args...)
{
    return schedule_later(callback, delay, owner, args, 1, 1);
}

/**
 * @brief 取消定时器
 * @param handle 定时器句柄
 * @return ERR_OK=成功取消, ERR_NOT_FOUND=未找到, ERR_INVALID_PARAM=无效参数
 */
int cancel_timer(int handle)
{
    mapping timer;
    int call_out_handle;

    if (!intp(handle))
    {
        return ERR_INVALID_PARAM;
    }

    timer = _timers[handle];
    if (!mapp(timer))
    {
        return ERR_NOT_FOUND;
    }

    call_out_handle = timer["call_out_handle"];
    if (intp(call_out_handle) && call_out_handle > 0)
    {
        remove_call_out(call_out_handle);
    }

    remove_timer_entry(handle, timer);

    return ERR_OK;
}

/**
 * @brief 查找定时器 (兼容旧接口)
 * @param callback 回调函数名
 * @param owner 所有者对象（可选）
 * @return 定时器句柄（未找到返回ERR_NOT_FOUND）
 */
int find_timer(string callback, object owner)
{
    int handle;
    mapping timer;

    if (!stringp(callback))
    {
        return ERR_INVALID_PARAM;
    }

    if (!objectp(owner))
    {
        return find_timer_by_callback(callback);
    }

    foreach (handle in find_timers_by_owner(owner))
    {
        timer = _timers[handle];
        if (mapp(timer) && timer["callback"] == callback)
        {
            return handle;
        }
    }

    return ERR_NOT_FOUND;
}

/**
 * @brief 通过回调函数名查找定时器
 * @param callback 回调函数名
 * @return 定时器句柄（未找到返回ERR_NOT_FOUND）
 */
int find_timer_by_callback(string callback)
{
    mixed *handles;

    if (!stringp(callback))
    {
        return ERR_INVALID_PARAM;
    }

    handles = _callback_index[callback];
    if (arrayp(handles) && sizeof(handles) > 0)
    {
        foreach (int handle in handles)
        {
            if (mapp(_timers[handle]))
            {
                return handle;
            }
        }
    }

    return ERR_NOT_FOUND;
}

/**
 * @brief O(k)查找 - 通过所有者 (k为该owner的定时器数量)
 * @param owner 所有者对象
 * @return 定时器句柄数组
 */
int *find_timers_by_owner(object owner)
{
    mixed *handles;

    if (!objectp(owner))
    {
        return ({});
    }

    handles = _owner_index[owner];
    if (arrayp(handles))
    {
        // 过滤掉已失效的句柄
        return filter(handles, (: mapp(_timers[$1]) :));
    }

    return ({});
}

/**
 * @brief 批量取消某所有者的所有定时器
 * @param owner 所有者对象
 * @return 取消的定时器数量
 */
int cancel_timers_by_owner(object owner)
{
    int *handles;
    int handle;
    int count = 0;

    if (!objectp(owner))
    {
        return 0;
    }

    handles = find_timers_by_owner(owner);

    foreach (handle in handles)
    {
        if (cancel_timer(handle) == ERR_OK)
        {
            count++;
        }
    }

    return count;
}

/**
 * @brief 查询定时器数据
 * @param handle 定时器句柄
 * @return 定时器数据映射
 */
mapping query_timer(int handle)
{
    if (!intp(handle))
    {
        return 0;
    }

    return _timers[handle];
}

/**
 * @brief 查询所有定时器
 * @return 定时器表
 */
mapping query_all_timers()
{
    return copy(_timers);
}

// 内部函数
private void remove_timer_entry(int handle, mapping timer)
{
    remove_from_callback_index(timer["callback"], handle);
    remove_from_owner_index(timer["owner"], handle);
    map_delete(_timers, handle);
}

/**
 * @brief 执行定时器回调（内部）
 * @param handle 定时器句柄
 */
private void execute_timer(int handle)
{
    mapping timer;
    object owner;
    string callback;
    mixed *args;

    timer = _timers[handle];
    if (!mapp(timer))
    {
        return;
    }

    owner = timer["owner"];
    callback = timer["callback"];
    args = timer["args"];

    // 检查所有者是否有效
    if (!objectp(owner))
    {
        remove_timer_entry(handle, timer);
        return;
    }

    // 检查函数是否存在
    if (!function_exists(callback, owner))
    {
        remove_timer_entry(handle, timer);
        return;
    }

    // 从表中移除（防止重入）并清理索引
    remove_timer_entry(handle, timer);

    dispatch_call(owner, callback, args);
}

/**
 * @brief 生成定时器句柄（内部）
 * @return 新的句柄
 */
private int generate_handle()
{
    _timer_sequence++;
    if (_timer_sequence > 1000000000)
    {
        _timer_sequence = 1;
    }
    return _timer_sequence;
}

/**
 * @brief 添加到callback索引
 * @param callback 回调函数名
 * @param handle 定时器句柄
 */
private void add_to_callback_index(string callback, int handle)
{
    mixed *handles;

    handles = _callback_index[callback];
    if (!arrayp(handles))
    {
        handles = ({});
    }

    // 避免重复添加
    if (member_array(handle, handles) == -1)
    {
        handles += ({ handle });
        _callback_index[callback] = handles;
    }
}

/**
 * @brief 从callback索引移除
 * @param callback 回调函数名
 * @param handle 定时器句柄
 */
private void remove_from_callback_index(string callback, int handle)
{
    mixed *handles;

    handles = _callback_index[callback];
    if (arrayp(handles))
    {
        handles -= ({ handle });
        if (sizeof(handles) > 0)
        {
            _callback_index[callback] = handles;
        }
        else
        {
            map_delete(_callback_index, callback);
        }
    }
}

/**
 * @brief 添加到owner索引
 * @param owner 所有者对象
 * @param handle 定时器句柄
 */
private void add_to_owner_index(object owner, int handle)
{
    mixed *handles;

    handles = _owner_index[owner];
    if (!arrayp(handles))
    {
        handles = ({});
    }

    // 避免重复添加
    if (member_array(handle, handles) == -1)
    {
        handles += ({ handle });
        _owner_index[owner] = handles;
    }
}

/**
 * @brief 从owner索引移除
 * @param owner 所有者对象
 * @param handle 定时器句柄
 */
private void remove_from_owner_index(object owner, int handle)
{
    mixed *handles;

    handles = _owner_index[owner];
    if (arrayp(handles))
    {
        handles -= ({ handle });
        if (sizeof(handles) > 0)
        {
            _owner_index[owner] = handles;
        }
        else
        {
            map_delete(_owner_index, owner);
        }
    }
}
