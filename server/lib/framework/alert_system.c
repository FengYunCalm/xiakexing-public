/**
 * @file    alert_system.c
 * @brief   告警系统入口



 *
 * @par Description:
 * 告警真源实现。
 * `lib/framework/alert/` 子模块当前主要承担 forwarding shell 身份，
 * 状态、规则和分发顺序都以本文件为准。
 */

#pragma strict_types
#pragma save_types

#include <system/globals.h>
#include <system/ansi.h>
#include <framework/message_facade.h>
#include <game_constants.h>

#define ALERT_CHECK_INTERVAL 60
#define ALERT_LEVEL_INFO 0
#define ALERT_LEVEL_WARNING 1
#define ALERT_LEVEL_CRITICAL 2
#define ALERT_LEVEL_EMERGENCY 3
#define ALERT_COOLDOWN 300
#define MAX_ALERT_HISTORY 1000

nosave mapping alert_rules;
nosave mixed *alert_history;
nosave mapping alert_handlers;
nosave int system_initialized;

private mixed clone_alert_value(mixed value);
private int alert_rule_cooldown(mapping rule);

void alert_init();
int register_alert_handler(string name, string func);
int set_alert_threshold(string metric_name, float threshold, int level, string message, int cooldown);
int remove_alert_rule(string metric_name);
int check_alert(string metric_name, float value);
void add_alert_history(mapping record);
void dispatch_alert(mapping record);
void log_alert(mapping record);
void dispatch_recovery(mapping record);
mixed *get_alert_rules(string metric_name);
mixed *get_alert_history(string metric_name, int level);
void clear_alert_history();
void alert_check_loop();
float query_memory_usage();
string get_level_string(int level);
mapping get_alert_stats();

private mixed clone_alert_value(mixed value)
{
    mixed cloned;

    if (catch(cloned = restore_variable(save_variable(value))))
        return 0;
    return cloned;
}

private int alert_rule_cooldown(mapping rule)
{
    if (mapp(rule) && intp(rule["cooldown"]) && rule["cooldown"] >= 0)
        return rule["cooldown"];
    return ALERT_COOLDOWN;
}

void alert_init()
{
    if (system_initialized)
        return;

    /*
     * 所有 nosave 运行时状态都在这里一次性建立，
     * forwarding shell 只应复用这个入口，不能各自维护平行副本。
     */
    alert_rules = ([]);
    alert_history = ({ });
    alert_handlers = ([]);
    system_initialized = 1;

    register_alert_handler("log", "alert_handler_log");
    register_alert_handler("game", "alert_handler_game");

    set_alert_threshold("response_time", 100.0, ALERT_LEVEL_WARNING, "响应时间过长: %.0fms", 300);
    set_alert_threshold("response_time", 500.0, ALERT_LEVEL_CRITICAL, "响应时间严重超标: %.0fms", 60);
    set_alert_threshold("online_users", 1800.0, ALERT_LEVEL_WARNING, "在线人数接近上限: %.0f", 600);
    set_alert_threshold("online_users", 1900.0, ALERT_LEVEL_CRITICAL, "在线人数危险: %.0f", 300);
    set_alert_threshold("error_rate", 5.0, ALERT_LEVEL_WARNING, "错误率偏高: %.1f%%", 300);
    set_alert_threshold("error_rate", 10.0, ALERT_LEVEL_CRITICAL, "错误率严重: %.1f%%", 120);

    call_out("alert_check_loop", ALERT_CHECK_INTERVAL);
    log_file("system/alert", sprintf("[%s] Alert system initialized\n", ctime(time())));
}

int register_alert_handler(string name, string func)
{
    if (!stringp(name) || name == "" || !stringp(func) || func == "")
        return 0;
    alert_handlers[name] = func;
    return 1;
}

int set_alert_threshold(string metric_name, float threshold, int level, string message, int cooldown)
{
    mapping rule;

    /* 同一 metric 允许挂多条阈值规则，触发时按记录内容独立冷却。 */
    if (!stringp(metric_name) || metric_name == "")
        return 0;
    if (!intp(level) || level < 0 || level > 3)
        level = ALERT_LEVEL_WARNING;
    if (!stringp(message) || message == "")
        message = "Alert: " + metric_name + " = %.1f";
    if (!intp(cooldown) || cooldown < 0)
        cooldown = ALERT_COOLDOWN;
    if (!arrayp(alert_rules[metric_name]))
        alert_rules[metric_name] = ({ });

    rule = ([
        "metric_name": metric_name,
        "threshold": threshold,
        "level": level,
        "message": message,
        "cooldown": cooldown,
        "last_alert": 0,
        "alert_count": 0
    ]);
    alert_rules[metric_name] += ({ rule });
    return 1;
}

int remove_alert_rule(string metric_name)
{
    if (!stringp(metric_name) || metric_name == ""
        || !arrayp(alert_rules[metric_name])
        || sizeof(alert_rules[metric_name]) == 0)
        return 0;
    map_delete(alert_rules, metric_name);
    return 1;
}

mixed *get_alert_rules(string metric_name)
{
    mixed *rules;
    mapping rule;
    mixed cloned;

    if (!stringp(metric_name) || metric_name == "")
        return ({ });
    rules = ({ });
    foreach (rule in (arrayp(alert_rules[metric_name]) ? alert_rules[metric_name] : ({ }))) {
        cloned = clone_alert_value(rule);
        if (mapp(cloned))
            rules += ({ cloned });
    }
    return rules;
}

int check_alert(string metric_name, float value)
{
    mixed *rules;
    mapping rule;
    int triggered_level;
    int now;

    if (!stringp(metric_name) || metric_name == "")
        return 0;
    if (!system_initialized)
        alert_init();

    /* 判定必须在 owner 真源上更新 last_alert；公开查询只返回快照。 */
    rules = arrayp(alert_rules[metric_name]) ? alert_rules[metric_name] : ({ });
    if (sizeof(rules) == 0)
        return 0;

    /*
     * 返回值只汇总本轮命中的最高等级。
     * 具体告警记录和 handler 分发在循环内部即时完成。
     */
    triggered_level = 0;
    now = time();
    foreach (rule in rules) {
        if (!mapp(rule))
            continue;
        if (value >= to_float(rule["threshold"])) {
            if (now - (rule["last_alert"] || 0) >= alert_rule_cooldown(rule)) {
                rule["last_alert"] = now;
                rule["alert_count"] = (rule["alert_count"] || 0) + 1;
                dispatch_alert(([
                    "timestamp": now,
                    "metric_name": metric_name,
                    "value": value,
                    "threshold": rule["threshold"],
                    "level": rule["level"],
                    "message": sprintf(rule["message"], value),
                    "resolved": 0,
                    "resolved_time": 0
                ]));
                if (rule["level"] > triggered_level)
                    triggered_level = rule["level"];
            }
        }
    }
    return triggered_level;
}

void add_alert_history(mapping record)
{
    mixed cloned;

    if (!mapp(record))
        return;
    cloned = clone_alert_value(record);
    if (!mapp(cloned))
        return;
    alert_history += ({ cloned });
    if (sizeof(alert_history) > MAX_ALERT_HISTORY)
        alert_history = alert_history[sizeof(alert_history) - MAX_ALERT_HISTORY..];
}

void dispatch_alert(mapping record)
{
    string *handlers;
    string handler_name;
    string func_name;
    mixed handler_error;

    /*
     * 顺序固定为：入历史，再通知已注册 handler。
     * 日志由 canonical log handler 单独负责，避免同一告警重复写日志。
     */
    if (!mapp(record))
        return;
    record = clone_alert_value(record);
    if (!mapp(record))
        return;
    add_alert_history(record);
    handlers = keys(alert_handlers);
    foreach (handler_name in handlers) {
        func_name = alert_handlers[handler_name];
        if (stringp(func_name) && function_exists(func_name, this_object())) {
            handler_error = catch(call_other(this_object(), func_name, record));
            if (handler_error)
                log_file("system/alert", sprintf(
                    "[%s] handler %s failed: %O\\n", ctime(time()),
                    handler_name, handler_error));
        }
    }
}

void dispatch_recovery(mapping record)
{
    /* 当前恢复链只保留日志占位，后续扩展恢复通知时应继续以本文件为真源。 */
    if (!mapp(record))
        return;
    log_file("system/alert", sprintf("[%s] RECOVERED: %s\n", ctime(time()), record["metric_name"] || "unknown"));
}

void alert_handler_log(mapping record)
{
    if (!mapp(record))
        return;
    log_file("system/alert", sprintf("[%s] [%s] %s\n", ctime(time()), get_level_string(record["level"]), record["message"] || ""));
}

void alert_handler_game(mapping record)
{
    object *admins;
    object admin;
    string msg;

    if (!mapp(record) || (record["level"] || 0) < ALERT_LEVEL_CRITICAL)
        return;
    msg = sprintf(HIR "[系统告警] %s" NOR "\n", record["message"] || "");
    admins = filter(users(), (: wizardp($1) :));
    foreach (admin in admins)
        msg_send(admin, msg);
}

void log_alert(mapping record)
{
    if (!mapp(record))
        return;
    log_file("system/alert", sprintf("[%s] %s\n", ctime(time()), record["message"] || ""));
}

mixed *get_alert_history(string metric_name, int level)
{
    mixed *results;
    mapping record;
    mixed cloned;

    if (!arrayp(alert_history))
        return ({ });

    results = ({ });
    foreach (record in alert_history) {
        if (!mapp(record))
            continue;
        if (stringp(metric_name) && record["metric_name"] != metric_name)
            continue;
        if (intp(level) && record["level"] != level)
            continue;
        cloned = clone_alert_value(record);
        if (mapp(cloned))
            results += ({ cloned });
    }
    return results;
}

void clear_alert_history()
{
    alert_history = ({ });
}

float query_memory_usage()
{
    return -1.0;
}

string get_level_string(int level)
{
    switch (level) {
        case ALERT_LEVEL_INFO: return "INFO";
        case ALERT_LEVEL_WARNING: return "WARNING";
        case ALERT_LEVEL_CRITICAL: return "CRITICAL";
        case ALERT_LEVEL_EMERGENCY: return "EMERGENCY";
    }
    return "UNKNOWN";
}

mapping get_alert_stats()
{
    int unresolved_count;
    int total_rules;
    mapping record;
    string metric_name;

    unresolved_count = 0;
    total_rules = 0;
    foreach (metric_name in keys(alert_rules || ([])))
        total_rules += arrayp(alert_rules[metric_name]) ? sizeof(alert_rules[metric_name]) : 0;
    foreach (record in alert_history) {
        if (mapp(record) && !record["resolved"])
            unresolved_count++;
    }
    return ([
        "total_rules": total_rules,
        "total_history": sizeof(alert_history),
        "unresolved_alerts": unresolved_count,
        "handlers": sizeof(keys(alert_handlers))
    ]);
}

void alert_check_loop()
{
    /* 定时链只覆盖本文件当前负责的基础指标，避免子壳各自启动重复循环。 */
    check_alert("online_users", to_float(sizeof(users())));
    call_out("alert_check_loop", ALERT_CHECK_INTERVAL);
}

void create()
{
    alert_init();
}
