/**
 * @file    monitor_d.c
 * @brief   监控守护进程



 *
 * @par Description:
 * 这里只维护可选的守护进程监控视图和运行期指标，不承担 health 真源职责。
 *
 * 当前职责：
 * - 注册需要观测的 daemon
 * - 记录 heartbeat/timeout 视图
 * - 暴露监控状态给维护链或诊断命令读取
 *
 * 不承担：
 * - 服务健康真源（应由 health/registry 链路负责）
 * - 告警历史真源
 * - daemon 自恢复编排真源
 */

#pragma strict_types
#pragma save_types

#include <system/globals.h>
#include <system/constants.h>

inherit "/lib/framework/daemon";

nosave mapping _registered_daemons;
nosave mapping _heartbeat_records;
nosave int _monitor_active;
nosave int _check_call_out;

void register_daemon(string name, string path);
void unregister_daemon(string name);
void report_heartbeat(string daemon_name, string status);
void report_status(string daemon_name, string status);
void start_monitoring();
void stop_monitoring();
void check_all_daemons();
void check_daemon_health(string name, int current_time);
void send_alert(string type, string target, string message);
mapping query_status();

void create()
{
    ::create();
    _registered_daemons = ([]);
    _heartbeat_records = ([]);
    _monitor_active = 0;
    log_file("system/monitor", sprintf("[%s] Monitor daemon initialized; awaiting explicit heartbeat producers\n", ctime(time())[4..19]));
}

void register_daemon(string name, string path)
{
    if (!stringp(name) || !stringp(path))
        return;
    _registered_daemons[name] = (["path": path, "registered_at": time(), "status": "registered"]);
    _heartbeat_records[name] = (["last_heartbeat": 0, "heartbeat_count": 0, "timeout_count": 0]);
}

void unregister_daemon(string name)
{
    map_delete(_registered_daemons, name);
    map_delete(_heartbeat_records, name);
}

void report_heartbeat(string daemon_name, string status)
{
    if (!mapp(_heartbeat_records[daemon_name]))
        return;
    _heartbeat_records[daemon_name]["last_heartbeat"] = time();
    _heartbeat_records[daemon_name]["heartbeat_count"]++;
    _heartbeat_records[daemon_name]["status"] = status || "alive";
    if (mapp(_registered_daemons[daemon_name]))
        _registered_daemons[daemon_name]["status"] = "active";
}

void report_status(string daemon_name, string status)
{
    report_heartbeat(daemon_name, status);
}

void start_monitoring()
{
    if (_monitor_active)
        return;
    _monitor_active = 1;
    _check_call_out = call_out("check_all_daemons", 30);
}

void stop_monitoring()
{
    _monitor_active = 0;
    if (_check_call_out)
        remove_call_out(_check_call_out);
}

void check_all_daemons()
{
    int current_time;
    string name;

    if (!_monitor_active)
        return;
    current_time = time();
    foreach (name in keys(_registered_daemons))
        check_daemon_health(name, current_time);
    _check_call_out = call_out("check_all_daemons", 30);
}

void check_daemon_health(string name, int current_time)
{
    mapping record;

    record = _heartbeat_records[name];
    if (!mapp(record))
        return;
    if (!(record["last_heartbeat"] || 0))
        return;
    if (current_time - record["last_heartbeat"] > 180) {
        record["timeout_count"]++;
        if (mapp(_registered_daemons[name]))
            _registered_daemons[name]["status"] = "timeout";
    }
}

void send_alert(string type, string target, string message)
{
    if (find_object("/daemons/core/alert_d"))
        "/daemons/core/alert_d"->alert(type, target, message);
}

mapping query_status()
{
    return ([
        "active": _monitor_active,
        "registered": copy(_registered_daemons),
        "heartbeat": copy(_heartbeat_records),
        "heartbeat_producer_contract": "explicit_register_and_report"
    ]);
}
