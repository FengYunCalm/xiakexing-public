/**
 * @file    heartbeat_d.c
 * @brief   全局心跳调度器



 *
 * @par Description:
 * 全局心跳调度器，统一管理需要定时触发的系统，避免每个系统单独创建定时器。
 * 这里负责 handler 注册与触发顺序，具体业务状态仍归各自服务或模块所有。
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

#define HEARTBEAT_INTERVAL      1
#define HEARTBEAT_DEBUG_LOG_INTERVAL 600
#define HEARTBEAT_HANDLER_BUDGET 16
#define HEARTBEAT_WALL_BUDGET_NS 4000000
#define HEARTBEAT_CONTINUATION_DELAY_US 1000
#define HEARTBEAT_CONTINUATION_DELAY_SECONDS (HEARTBEAT_CONTINUATION_DELAY_US / 1000000.0)

#define MAX_HANDLERS            10000

#define HANDLER_ID_PREFIX       "hb_"

nosave mapping _handlers;
nosave int _handler_sequence;
nosave int _is_running;
nosave int _heartbeat_handle;
nosave int _heartbeat_ticks;
nosave int _object_payload_rejections;
nosave int _heartbeat_generation;
nosave string *_heartbeat_due_ids;
nosave int _heartbeat_due_cursor;
nosave int _heartbeat_due_time;
nosave int _heartbeat_tick_processed;
nosave int _heartbeat_slice_runs;
nosave int _heartbeat_slice_handlers_total;
nosave int _heartbeat_slice_handlers_max;
nosave int _heartbeat_slice_wall_total_ns;
nosave int _heartbeat_slice_wall_max_ns;
nosave int _heartbeat_slice_budget_yields;
nosave int _heartbeat_slice_wall_yields;
nosave int _heartbeat_continuations_scheduled;
nosave int _heartbeat_continuations_executed;
nosave int _heartbeat_schedule_failures;
nosave string _heartbeat_last_schedule_error;

/*
 * handler_data 结构:
 * ([
 *     "id": 处理器ID,
 *     "object": 处理器对象,
 *     "function": 回调函数名,
 *     "interval": 触发间隔(秒),
 *     "args": 参数数组,
 *     "next_trigger": 下次触发时间戳,
 *     "active": 是否激活 (1=激活, 0=暂停),
 *     "registered": 注册时间,
 *     "trigger_count": 触发次数统计
 * ])
 */

void create();
void setup();
void destroy();

void start_heartbeat();
void stop_heartbeat();
void do_heartbeat();
void continue_heartbeat(int generation, int continuation);

string register_handler(object ob, string func, int interval, mixed args...);
string register_handler_with_options(object ob, string func, int interval, mapping options, mixed args...);
int unregister_handler(string handler_id);
int pause_handler(string handler_id);
int resume_handler(string handler_id);

mapping query_handler(string handler_id);
mapping query_all_handlers();
int query_handler_count();
mapping query_stats();
mapping query_profile_stats();
mapping query_batch_stats();
mixed *query_handlers_by_profile(string profile_id);
mixed *query_handlers_by_batch(string batch_name);

private string generate_handler_id();
private void execute_handler(string handler_id);
private mapping require_handler(string handler_id);
private string query_handler_profile(mapping handler);
private string query_handler_batch(mapping handler);
private int compare_heartbeat_handler_ids(string left, string right);
private string *heartbeat_due_handler_ids(int current_time);
private void schedule_next_heartbeat();
private int fail_heartbeat_schedule(string phase, mixed err);

private int heartbeat_args_owner_safe(mixed *args)
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

mapping query_heartbeat_owner_route(object ob, string func)
{
    mapping route;
    string route_key;
    mixed err;

    route_key = objectp(ob) ? file_name(ob) : "missing-handler";
    if (stringp(func) && func != "")
        route_key += ":" + func;
    err = catch(route = OWNER_RUNTIME_D->query_service_owner_route("heartbeat", route_key));
    if (err || !mapp(route) || route["ok"] != 1)
        return ([
            "ok": 0,
            "service": "heartbeat",
            "key": route_key,
            "handler_path": objectp(ob) ? file_name(ob) : "",
            "handler_function": stringp(func) ? func : "",
            "failure_reason": err ? sprintf("owner route query failed: %O", err)
                : (mapp(route) ? route["failure_reason"] : "owner route unavailable"),
            "readiness": mapp(route) ? route["readiness"] : ([ "ok": 0 ]),
            "dependencies": mapp(route) ? route["dependencies"] : ([ ]),
            "direct_cross_owner_write": 0,
        ]);

    route["handler_path"] = objectp(ob) ? file_name(ob) : "";
    route["handler_function"] = stringp(func) ? func : "";
    route["direct_cross_owner_write"] = 0;
    return route;
}

mapping query_heartbeat_owner_status()
{
    return ([
        "service_map_v2": 1,
        "service": "heartbeat",
        "policy": "owner_scheduler",
        "handler_count": mapp(_handlers) ? sizeof(_handlers) : 0,
        "payload_contract": "spec_snapshot_or_commit_proposal_only",
        "object_payload_allowed": 0,
        "object_payload_rejected": 1,
        "object_payload_rejections": _object_payload_rejections,
        "message_only_cross_owner": 1,
        "direct_cross_owner_write": 0,
    ]);
}

private void log_with_level(string level, string msg)
{
    log_file("system/heartbeat", sprintf("[%s] %s\n", level, msg));
}

private mapping require_handler(string handler_id)
{
    if (!stringp(handler_id))
        return 0;
    return _handlers[handler_id];
}

void log_debug(string msg)
{
    log_with_level("DEBUG", msg);
}

void log_info(string msg)
{
    log_with_level("INFO", msg);
}

void log_warning(string msg)
{
    log_with_level("WARN", msg);
}

void log_error(string msg)
{
    log_with_level("ERROR", msg);
}

/**
 * @brief 守护进程创建
 */
void create()
{
    ::create();
    setup();
}

/**
 * @brief 守护进程初始化
 */
void setup()
{
    _handlers = ([]);
    _handler_sequence = 0;
    _is_running = 0;
    _heartbeat_handle = 0;
    _heartbeat_ticks = 0;
    _object_payload_rejections = 0;
    _heartbeat_generation = 0;
    _heartbeat_due_ids = ({ });
    _heartbeat_due_cursor = 0;
    _heartbeat_due_time = 0;
    _heartbeat_tick_processed = 0;
    _heartbeat_slice_runs = 0;
    _heartbeat_slice_handlers_total = 0;
    _heartbeat_slice_handlers_max = 0;
    _heartbeat_slice_wall_total_ns = 0;
    _heartbeat_slice_wall_max_ns = 0;
    _heartbeat_slice_budget_yields = 0;
    _heartbeat_slice_wall_yields = 0;
    _heartbeat_continuations_scheduled = 0;
    _heartbeat_continuations_executed = 0;
    _heartbeat_schedule_failures = 0;
    _heartbeat_last_schedule_error = "";

    start_heartbeat();

    log_info("Heartbeat daemon initialized (interval=" + HEARTBEAT_INTERVAL + "s)");
}

/**
 * @brief 守护进程销毁
 */
void destroy()
{
    stop_heartbeat();
    _handlers = ([]);
    log_info("Heartbeat daemon destroyed");
}

/**
 * @brief 启动心跳
 */
void start_heartbeat()
{
    if (_is_running)
        return;

    _is_running = 1;
    _heartbeat_generation++;
    if (fail_heartbeat_schedule("start",
        catch(_heartbeat_handle = call_out("do_heartbeat", HEARTBEAT_INTERVAL))))
        return;

    log_debug("Heartbeat started (handle=" + _heartbeat_handle + ")");
}

/**
 * @brief 停止心跳
 */
void stop_heartbeat()
{
    if (!_is_running)
        return;

    _is_running = 0;
    _heartbeat_generation++;

    if (intp(_heartbeat_handle) && _heartbeat_handle > 0)
    {
        remove_call_out(_heartbeat_handle);
        _heartbeat_handle = 0;
    }

    _heartbeat_due_ids = ({ });
    _heartbeat_due_cursor = 0;
    _heartbeat_due_time = 0;
    _heartbeat_tick_processed = 0;

    log_debug("Heartbeat stopped");
}

/**
 * @brief 执行心跳 - 批量处理所有处理器
 */
void do_heartbeat()
{
    int current_time;

    if (!_is_running)
        return;

    _heartbeat_handle = 0;
    current_time = time();
    _heartbeat_ticks++;
    _heartbeat_due_ids = heartbeat_due_handler_ids(current_time);
    _heartbeat_due_cursor = 0;
    _heartbeat_due_time = current_time;
    _heartbeat_tick_processed = 0;
    continue_heartbeat(_heartbeat_generation, 0);
}

void continue_heartbeat(int generation, int continuation)
{
    string handler_id;
    mapping handler;
    int processed_count;
    int slice_started_at;
    int slice_elapsed_ns;
    int wall_budget_yield;

    if (!_is_running || generation != _heartbeat_generation)
        return;

    _heartbeat_handle = 0;
    if (continuation)
        _heartbeat_continuations_executed++;
    slice_started_at = perf_counter_ns();
    while (_is_running
        && generation == _heartbeat_generation
        && _heartbeat_due_cursor < sizeof(_heartbeat_due_ids)
        && processed_count < HEARTBEAT_HANDLER_BUDGET)
    {
        handler_id = _heartbeat_due_ids[_heartbeat_due_cursor++];
        processed_count++;
        handler = require_handler(handler_id);
        if (mapp(handler) && handler["active"]
            && _heartbeat_due_time >= handler["next_trigger"])
        {
            execute_handler(handler_id);
            _heartbeat_tick_processed++;
        }

        if (_heartbeat_due_cursor < sizeof(_heartbeat_due_ids)
            && perf_counter_ns() - slice_started_at >= HEARTBEAT_WALL_BUDGET_NS)
        {
            wall_budget_yield = 1;
            break;
        }
    }

    slice_elapsed_ns = perf_counter_ns() - slice_started_at;
    _heartbeat_slice_runs++;
    _heartbeat_slice_handlers_total += processed_count;
    if (processed_count > _heartbeat_slice_handlers_max)
        _heartbeat_slice_handlers_max = processed_count;
    _heartbeat_slice_wall_total_ns += slice_elapsed_ns;
    if (slice_elapsed_ns > _heartbeat_slice_wall_max_ns)
        _heartbeat_slice_wall_max_ns = slice_elapsed_ns;

    if (!_is_running || generation != _heartbeat_generation) {
        if (!_is_running) {
            _heartbeat_due_ids = ({ });
            _heartbeat_due_cursor = 0;
        }
        return;
    }
    if (_heartbeat_due_cursor < sizeof(_heartbeat_due_ids)) {
        if (wall_budget_yield)
            _heartbeat_slice_wall_yields++;
        else
            _heartbeat_slice_budget_yields++;
        if (fail_heartbeat_schedule("continuation",
            catch(_heartbeat_handle = call_out_walltime("continue_heartbeat",
                HEARTBEAT_CONTINUATION_DELAY_SECONDS, generation, 1))))
            return;
        _heartbeat_continuations_scheduled++;
        return;
    }

    _heartbeat_due_ids = ({ });
    _heartbeat_due_cursor = 0;
    _heartbeat_due_time = 0;
    schedule_next_heartbeat();
}

// 处理器管理
/**
 * @brief 注册心跳处理器
 * @param ob 处理器对象
 * @param func 回调函数名
 * @param interval 触发间隔（秒）
 * @param args 可变参数（传递给回调函数）
 * @return 处理器ID（失败返回0）
 */
varargs string register_handler(object ob, string func, int interval, mixed args...)
{
    return register_handler_with_options(ob, func, interval, ([]), args);
}

varargs string register_handler_with_options(object ob, string func, int interval, mapping options, mixed args...)
{
    string handler_id;
    mapping handler;
    mapping owner_route;
    mixed *normalized_args;

    // 参数验证
    if (!objectp(ob))
    {
        log_error("register_handler: invalid object");
        return 0;
    }

    if (!stringp(func) || func == "")
    {
        log_error("register_handler: invalid function name");
        return 0;
    }

    if (!intp(interval) || interval < 1)
    {
        log_error("register_handler: invalid interval (must >= 1)");
        return 0;
    }

    // 检查处理器数量限制
    if (sizeof(_handlers) >= MAX_HANDLERS)
    {
        log_error("register_handler: max handlers reached (" + MAX_HANDLERS + ")");
        return 0;
    }

    // 生成处理器ID
    handler_id = generate_handler_id();

    if (sizeof(args) == 1 && arrayp(args[0]))
        normalized_args = copy(args[0]);
    else
        normalized_args = args;

    if (!heartbeat_args_owner_safe(normalized_args))
    {
        _object_payload_rejections++;
        log_error("register_handler: object payload args are not owner safe");
        return 0;
    }

    // 创建处理器数据
    owner_route = query_heartbeat_owner_route(ob, func);
    if (!mapp(owner_route) || owner_route["ok"] != 1) {
        log_error("register_handler: owner executor is not ready");
        return 0;
    }

    handler = ([
        "id": handler_id,
        "object": ob,
        "function": func,
        "interval": interval,
        "args": normalized_args,
        "options": mapp(options) ? copy(options) : ([]),
        "owner_route": owner_route,
        "payload_contract": "spec_snapshot_or_commit_proposal_only",
        "args_owner_safe": 1,
        "object_payload_rejected": 1,
        "direct_cross_owner_write": 0,
        "next_trigger": time() + interval,
        "active": 1,
        "registered": time(),
        "sequence": _handler_sequence,
        "attempt_count": 0,
        "trigger_count": 0,
        "failure_count": 0,
        "last_failure_at": 0,
        "last_failure": ""
    ]);

    // 存储处理器
    _handlers[handler_id] = handler;

    log_debug(sprintf("register_handler: id=%s, obj=%s, func=%s, interval=%d",
        handler_id, base_name(ob), func, interval));

    return handler_id;
}

/**
 * @brief 注销心跳处理器
 * @param handler_id 处理器ID
 * @return ERR_OK=成功, ERR_NOT_FOUND=未找到, ERR_INVALID_PARAM=无效参数
 */
int unregister_handler(string handler_id)
{
    if (!stringp(handler_id))
    {
        return ERR_INVALID_PARAM;
    }

    if (!mapp(require_handler(handler_id)))
    {
        return ERR_NOT_FOUND;
    }

    map_delete(_handlers, handler_id);

    log_debug("unregister_handler: id=" + handler_id);

    return ERR_OK;
}

/**
 * @brief 暂停心跳处理器
 * @param handler_id 处理器ID
 * @return ERR_OK=成功, ERR_NOT_FOUND=未找到
 */
int pause_handler(string handler_id)
{
    mapping handler;

    if (!stringp(handler_id))
    {
        return ERR_INVALID_PARAM;
    }

    handler = require_handler(handler_id);
    if (!mapp(handler))
    {
        return ERR_NOT_FOUND;
    }

    handler["active"] = 0;

    log_debug("pause_handler: id=" + handler_id);

    return ERR_OK;
}

/**
 * @brief 恢复心跳处理器
 * @param handler_id 处理器ID
 * @return ERR_OK=成功, ERR_NOT_FOUND=未找到
 */
int resume_handler(string handler_id)
{
    mapping handler;

    if (!stringp(handler_id))
    {
        return ERR_INVALID_PARAM;
    }

    handler = require_handler(handler_id);
    if (!mapp(handler))
    {
        return ERR_NOT_FOUND;
    }

    handler["active"] = 1;
    handler["next_trigger"] = time() + handler["interval"];

    log_debug("resume_handler: id=" + handler_id);

    return ERR_OK;
}

/**
 * @brief 批量注销某对象的所有处理器
 * @param ob 对象
 * @return 注销的处理器数量
 */
int unregister_by_object(object ob)
{
    string handler_id;
    mapping handler;
    int count = 0;

    if (!objectp(ob))
    {
        return 0;
    }

    foreach (handler_id, handler in _handlers)
    {
        if (mapp(handler) && handler["object"] == ob)
        {
            map_delete(_handlers, handler_id);
            count++;
        }
    }

    if (count > 0)
    {
        log_debug(sprintf("unregister_by_object: obj=%s, count=%d",
            base_name(ob), count));
    }

    return count;
}

// 查询接口
/**
 * @brief 查询处理器信息
 * @param handler_id 处理器ID
 * @return 处理器数据映射
 */
mapping query_handler(string handler_id)
{
    if (!stringp(handler_id))
    {
        return 0;
    }

    return copy(_handlers[handler_id]);
}

/**
 * @brief 查询所有处理器
 * @return 处理器表（副本）
 */
mapping query_all_handlers()
{
    return copy(_handlers);
}

/**
 * @brief 查询处理器数量
 * @return 处理器数量
 */
int query_handler_count()
{
    return sizeof(_handlers);
}

/**
 * @brief 查询调度器统计信息
 * @return 统计信息映射
 */
mapping query_stats()
{
    string handler_id;
    mapping handler;
    int active_count = 0;
    int paused_count = 0;
    int total_triggers = 0;

    foreach (handler_id, handler in _handlers)
    {
        if (mapp(handler))
        {
            if (handler["active"])
                active_count++;
            else
                paused_count++;

            total_triggers += handler["trigger_count"];
        }
    }

    return ([
        "total_handlers": sizeof(_handlers),
        "active": active_count,
        "paused": paused_count,
        "total_triggers": total_triggers,
        "tick_count": _heartbeat_ticks,
        "generation": _heartbeat_generation,
        "is_running": _is_running,
        "interval": HEARTBEAT_INTERVAL,
        "slice_budget": HEARTBEAT_HANDLER_BUDGET,
        "slice_wall_budget_us": HEARTBEAT_WALL_BUDGET_NS / 1000,
        "continuation_delay_us": HEARTBEAT_CONTINUATION_DELAY_US,
        "slice_runs": _heartbeat_slice_runs,
        "slice_handlers_total": _heartbeat_slice_handlers_total,
        "slice_handlers_max": _heartbeat_slice_handlers_max,
        "slice_wall_total_us": _heartbeat_slice_wall_total_ns / 1000,
        "slice_wall_max_us": _heartbeat_slice_wall_max_ns / 1000,
        "slice_budget_yields": _heartbeat_slice_budget_yields,
        "slice_wall_yields": _heartbeat_slice_wall_yields,
        "continuations_scheduled": _heartbeat_continuations_scheduled,
        "continuations_executed": _heartbeat_continuations_executed,
        "schedule_failures": _heartbeat_schedule_failures,
        "last_schedule_error": _heartbeat_last_schedule_error,
        "due_snapshot_size": sizeof(_heartbeat_due_ids),
        "due_snapshot_cursor": _heartbeat_due_cursor,
        "due_snapshot_pending": sizeof(_heartbeat_due_ids) - _heartbeat_due_cursor,
        "service_map_v2": 1,
        "owner_route_coverage": 1,
        "payload_contract": "spec_snapshot_or_commit_proposal_only",
        "object_payload_rejected": 1,
        "object_payload_rejections": _object_payload_rejections,
        "direct_cross_owner_write": 0,
        "profiles": query_profile_stats(),
        "batches": query_batch_stats(),
    ]);
}

mapping query_profile_stats()
{
    string handler_id;
    string profile_id;
    mapping handler;
    mapping stats;
    mapping profile_stats;

    stats = ([]);
    foreach (handler_id, handler in _handlers)
    {
        if (!mapp(handler))
            continue;

        profile_id = query_handler_profile(handler);
        profile_stats = stats[profile_id];
        if (!mapp(profile_stats)) {
            profile_stats = ([
                "total": 0,
                "active": 0,
                "paused": 0,
            ]);
            stats[profile_id] = profile_stats;
        }

        profile_stats["total"]++;
        if (handler["active"])
            profile_stats["active"]++;
        else
            profile_stats["paused"]++;
    }

    return stats;
}

mapping query_batch_stats()
{
    string handler_id;
    string batch_name;
    mapping handler;
    mapping stats;
    mapping batch_stats;

    stats = ([]);
    foreach (handler_id, handler in _handlers)
    {
        if (!mapp(handler))
            continue;

        batch_name = query_handler_batch(handler);
        batch_stats = stats[batch_name];
        if (!mapp(batch_stats)) {
            batch_stats = ([
                "total": 0,
                "active": 0,
                "paused": 0,
            ]);
            stats[batch_name] = batch_stats;
        }

        batch_stats["total"]++;
        if (handler["active"])
            batch_stats["active"]++;
        else
            batch_stats["paused"]++;
    }

    return stats;
}

mixed *query_handlers_by_profile(string profile_id)
{
    mixed *handlers;
    string handler_id;
    mapping handler;

    handlers = ({ });
    if (!stringp(profile_id) || profile_id == "")
        return handlers;

    foreach (handler_id, handler in _handlers)
    {
        if (!mapp(handler))
            continue;
        if (query_handler_profile(handler) != profile_id)
            continue;
        handlers += ({ copy(handler) });
    }

    return handlers;
}

mixed *query_handlers_by_batch(string batch_name)
{
    mixed *handlers;
    string handler_id;
    mapping handler;

    handlers = ({ });
    if (!stringp(batch_name) || batch_name == "")
        return handlers;

    foreach (handler_id, handler in _handlers)
    {
        if (!mapp(handler))
            continue;
        if (query_handler_batch(handler) != batch_name)
            continue;
        handlers += ({ copy(handler) });
    }

    return handlers;
}

// 内部函数
/**
 * @brief 生成处理器ID
 * @return 新的处理器ID
 */
private string generate_handler_id()
{
    string id;

    _handler_sequence++;

    id = sprintf("%s%d_%d", HANDLER_ID_PREFIX, time(), _handler_sequence);

    return id;
}

private string query_handler_profile(mapping handler)
{
    mapping options;

    options = mapp(handler) ? handler["options"] : 0;
    if (!mapp(options) || !stringp(options["profile"]) || options["profile"] == "")
        return "default";

    return options["profile"];
}

private string query_handler_batch(mapping handler)
{
    mapping options;

    options = mapp(handler) ? handler["options"] : 0;
    if (!mapp(options) || !stringp(options["batch"]) || options["batch"] == "")
        return "default";

    return options["batch"];
}

private int compare_heartbeat_handler_ids(string left, string right)
{
    mapping left_handler;
    mapping right_handler;
    int left_sequence;
    int right_sequence;

    left_handler = require_handler(left);
    right_handler = require_handler(right);
    left_sequence = mapp(left_handler) ? left_handler["sequence"] || 0 : 0;
    right_sequence = mapp(right_handler) ? right_handler["sequence"] || 0 : 0;
    if (left_sequence != right_sequence)
        return left_sequence - right_sequence;
    return strcmp(left, right);
}

private string *heartbeat_due_handler_ids(int current_time)
{
    string handler_id;
    string *due_ids;
    mapping handler;

    due_ids = ({ });
    foreach (handler_id in keys(_handlers)) {
        handler = require_handler(handler_id);
        if (!mapp(handler) || !handler["active"])
            continue;
        if (current_time >= handler["next_trigger"])
            due_ids += ({ handler_id });
    }
    return sort_array(due_ids, (: compare_heartbeat_handler_ids :));
}

private int fail_heartbeat_schedule(string phase, mixed err)
{
    if (!err && intp(_heartbeat_handle) && _heartbeat_handle > 0)
        return 0;

    _heartbeat_schedule_failures++;
    _heartbeat_last_schedule_error = sprintf("%s: %O", phase,
        err ? err : "callout returned an invalid handle");
    _is_running = 0;
    _heartbeat_generation++;
    _heartbeat_handle = 0;
    _heartbeat_due_ids = ({ });
    _heartbeat_due_cursor = 0;
    _heartbeat_due_time = 0;
    log_error("Heartbeat scheduling stopped: " + _heartbeat_last_schedule_error);
    return 1;
}

private void schedule_next_heartbeat()
{
    if (!_is_running)
        return;

    if (fail_heartbeat_schedule("tick",
        catch(_heartbeat_handle = call_out("do_heartbeat", HEARTBEAT_INTERVAL))))
        return;
    if (_heartbeat_ticks % HEARTBEAT_DEBUG_LOG_INTERVAL == 0)
        log_debug(sprintf("Heartbeat tick: processed=%d, total_handlers=%d",
            _heartbeat_tick_processed, sizeof(_handlers)));
}

/**
 * @brief 执行处理器（内部）
 * @param handler_id 处理器ID
 */
private void execute_handler(string handler_id)
{
    mapping handler;
    object ob;
    string func;
    mixed *args;
    mixed err;
    string error_text;
    int scheduled_trigger;

    handler = _handlers[handler_id];
    if (!mapp(handler))
        return;

    ob = handler["object"];
    func = handler["function"];
    args = handler["args"];

    // 检查对象是否有效
    if (!objectp(ob))
    {
        log_warning("execute_handler: object invalid, removing handler " + handler_id);
        map_delete(_handlers, handler_id);
        return;
    }

    // 检查函数是否存在
    if (!function_exists(func, ob))
    {
        log_error(sprintf("execute_handler: function %s not found in %s",
            func, base_name(ob)));
        map_delete(_handlers, handler_id);
        return;
    }

    /* 按原计划时间推进，避免回调耗时把下一次触发不断向后漂移。 */
    scheduled_trigger = handler["next_trigger"];
    handler["next_trigger"] = scheduled_trigger + handler["interval"];
    handler["attempt_count"]++;
    err = catch(dispatch_call(ob, func, args));
    if (err) {
        handler["failure_count"]++;
        handler["last_failure_at"] = time();
        error_text = sprintf("%O", err);
        handler["last_failure"] = sizeof(error_text) > 256
            ? error_text[0 .. 255] : error_text;
        log_warning(sprintf("execute_handler: callback failed id=%s func=%s error=%O",
            handler_id, func, err));
        return;
    }
    handler["trigger_count"]++;
}
