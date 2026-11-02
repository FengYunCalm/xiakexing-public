/**
 * @file    alert_d.c
 * @brief   告警兼容视图守护进程



 *
 * @par Description:
 * 历史告警兼容视图守护进程。
 * 当前仓库里真正被 framework / health / alert 子模块消费的 canonical alert
 * true source 已是 `/lib/framework/alert_system.c`；本文件保留是为了兼容旧路径与
 * 轻量试验入口，不应继续维护与 framework alert system 平行增长的第二套真源状态。
 *
 * 当前职责：
 * - 为旧路径调用保留最小兼容入口
 * - 提供与 alert_system 对齐的轻量兼容查询/试验入口
 *
 * 不承担：
 * - 业务健康状态真源
 * - 服务注册或监控真源
 * - framework alert system 的替身真源
 */

#pragma strict_types
#pragma save_types

#include <system/daemon.h>

inherit "/lib/framework/daemon";

#define ALERT_SYSTEM "/lib/framework/alert_system"

#define ALERT_LEVEL_INFO 0
#define ALERT_LEVEL_WARNING 1
#define ALERT_LEVEL_CRITICAL 2
#define ALERT_LEVEL_EMERGENCY 3

void ensure_alert_system();
int legacy_level_to_system(string level);
string system_level_to_legacy(mixed level);
private int legacy_level_matches(string requested, string actual);
mapping make_compat_alert_view(mapping record);
mixed *load_compat_history(string category, string level);

void create()
{
    ::create();
    ensure_alert_system();
}

void ensure_alert_system()
{
    call_other(ALERT_SYSTEM, "alert_init");
}

int legacy_level_to_system(string level)
{
    switch (level) {
        case "debug":
        case "info":
            return ALERT_LEVEL_INFO;
        case "error":
        case "critical":
            return ALERT_LEVEL_CRITICAL;
        case "emergency":
            return ALERT_LEVEL_EMERGENCY;
    }

    return ALERT_LEVEL_WARNING;
}

string system_level_to_legacy(mixed level)
{
    if (stringp(level) && level != "")
        return level;

    switch (to_int(level)) {
        case ALERT_LEVEL_INFO:
            return "info";
        case ALERT_LEVEL_CRITICAL:
            return "critical";
        case ALERT_LEVEL_EMERGENCY:
            return "emergency";
    }

    return "warning";
}

private int legacy_level_matches(string requested, string actual)
{
    if (!stringp(requested) || requested == "")
        return 1;
    if (requested == actual)
        return 1;
    return (requested == "debug" && actual == "info")
        || (requested == "error" && actual == "critical");
}

mapping make_compat_alert_view(mapping record)
{
    string category;
    string title;
    mapping compat_data;

    if (!mapp(record))
        return 0;

    category = record["compat_category"] || record["metric_name"] || "framework";
    title = record["compat_title"] || category;
    compat_data = mapp(record["compat_data"]) ? copy(record["compat_data"]) : copy(record);

    return ([
        "id": record["id"] || sprintf("compat_%d", record["timestamp"] || time()),
        "timestamp": record["timestamp"] || 0,
        "level": system_level_to_legacy(record["level"]),
        "category": category,
        "title": title,
        "message": record["message"] || "",
        "data": compat_data
    ]);
}

mixed *load_compat_history(string category, string level)
{
    mixed *records;
    mixed *slice;
    int *levels;
    int system_level;

    ensure_alert_system();
    records = ({ });

    if (stringp(level) && level != "") {
        levels = ({ legacy_level_to_system(level) });
    } else {
        levels = ({
            ALERT_LEVEL_INFO,
            ALERT_LEVEL_WARNING,
            ALERT_LEVEL_CRITICAL,
            ALERT_LEVEL_EMERGENCY,
        });
    }

    foreach (system_level in levels) {
        slice = call_other(ALERT_SYSTEM, "get_alert_history",
            stringp(category) && category != "" ? category : 0,
            system_level);
        if (arrayp(slice) && sizeof(slice) > 0)
            records += slice;
    }

    return sort_array(records, (: ($1["timestamp"] || 0) - ($2["timestamp"] || 0) :));
}

mapping get_alert_stats()
{
    mixed stats;

    ensure_alert_system();
    stats = call_other(ALERT_SYSTEM, "get_alert_stats");
    return mapp(stats) ? stats : ([]);
}

void send_alert(string level, string category, string title, string message, mapping data)
{
    ensure_alert_system();
    call_other(ALERT_SYSTEM, "dispatch_alert", ([
        "id": sprintf("alert_%d_%d", time(), random(10000)),
        "timestamp": time(),
        "metric_name": stringp(category) && category != "" ? category : "compat",
        "value": 0.0,
        "threshold": 0.0,
        "level": legacy_level_to_system(level),
        "message": stringp(message) && message != "" ? message : (title || ""),
        "resolved": 0,
        "resolved_time": 0,
        "compat_category": category,
        "compat_title": title,
        "compat_data": mapp(data) ? copy(data) : ([])
    ]));
}

void alert(string category, string target, string message)
{
    send_alert("warning", category, target, message, (["target": target]));
}

mixed *query_alert_history(string level, string category, int count)
{
    mixed *result;
    mapping entry;

    result = ({ });
    foreach (entry in load_compat_history(category, level)) {
        entry = make_compat_alert_view(entry);
        if (!mapp(entry))
            continue;
        if (!legacy_level_matches(level, entry["level"]))
            continue;
        if (stringp(category) && category != "" && entry["category"] != category)
            continue;
        result += ({ entry });
    }
    if (count > 0 && sizeof(result) > count)
        result = result[<count..];
    return result;
}

void clear_alert_history()
{
    ensure_alert_system();
    call_other(ALERT_SYSTEM, "clear_alert_history");
}

void test_alert()
{
    send_alert("info", "test", "Test Alert", "This is a test alert message", ([]));
}
