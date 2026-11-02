/**
 * @file    status.c
 * @brief   服务器状态命令



 *
 * @par Description:
 * 查看服务器运行状态，显示在线玩家和系统负载，并发送 XK 协议消息。
 */

#pragma strict_types

#include <game_constants.h>
#include <system/ansi.h>
#include <system/services.h>
#include <system/xk_protocol.h>
#include <game/command.h>
#include <framework/message_facade.h>

#define MONITOR_D "/daemons/core/monitor_d"

inherit "/lib/framework/network";

#define ROSTER_DIGEST_REPEAT 1

private nosave object _monitor_service;

private void emit_status_dialog(object me, string content,
    mapping response_context)
{
    if (!objectp(me) || !stringp(content) || content == "")
        return;

    xk_send_dialog(me, "服务器状态", content,
        DIALOG_PROJECTION_D->build_dialog_extra("status", "status_panel", ([
            "replace_mode": "replace",
            "actions": ({
                (["label":"刷新状态", "cmd":"status"]),
                (["label":"在线玩家", "cmd":"who"]),
                (["label":"查看自己", "cmd":"score"]),
                (["label":"查看帮助", "cmd":"help status"]),
            }),
        ])), response_context);
}

private object load_monitor_service()
{
    object monitord;

    if (objectp(_monitor_service))
        return _monitor_service;

    monitord = find_object(MONITOR_D);
    if (!objectp(monitord))
        monitord = load_object(MONITOR_D);

    if (objectp(monitord))
        _monitor_service = monitord;

    return monitord;
}

private void send_status_report(object me, mapping roster, int uptime_secs,
    mapping response_context)
{
    mixed *entries;
    mixed *player_reports;
    mapping digest;
    object monitord;
    mixed cpu;
    mixed mem;
    int count;

    entries = mapp(roster) && arrayp(roster["entries"]) ? roster["entries"] : ({ });
    digest = mapp(roster) && mapp(roster["digest"]) ? roster["digest"] : 0;
    player_reports = ({ });
    foreach (mapping entry in entries) {
        if (!mapp(entry) || count >= 10)
            continue;

        player_reports += ({ ([
            "name": entry["name"],
            "idle": entry["idle"]
        ]) });
        count++;
    }

    cpu = 0;
    mem = 0;
    monitord = load_monitor_service();
    if (objectp(monitord) && function_exists("query_metric", monitord)) {
        cpu = monitord->query_metric("sys.cpu.usage", 0);
        mem = monitord->query_metric("sys.memory.usage", 0);
    }

    xk_send_report(me,
        XK_REPORT_STATUS, ([
            "online_count": mapp(roster) ? (roster["total"] || 0) : 0,
            "uptime_seconds": uptime_secs,
            "cpu": intp(cpu) ? cpu : 0,
            "memory": intp(mem) ? mem : 0,
            "read_side_worker": mapp(digest) && digest["async"] ? 1 : 0,
            "read_side_worker_state": mapp(digest) ? (digest["state"] || "") : "",
            "read_side_worker_error": mapp(digest) ? (digest["error"] || "") : "",
            "players": player_reports
        ]), response_context);
}

int main(object me, string arg);
string format_uptime(int seconds);

/**
 * @brief 获取服务器运行时间
 * @return int 运行秒数
 */
private int get_server_uptime()
{
    return uptime();
}

/**
 * @brief 构建状态头部
 * @return string 状态头部字符串
 */
private string build_status_header()
{
    string result;

    result = HIY "╔════════════════════════════════════════╗" NOR "\n";
    result += sprintf(HIY "║" HIG "          侠客行 服务器状态" HIY "              ║" NOR "\n");
    result += HIY "╠════════════════════════════════════════╣" NOR "\n";
    return result;
}

/**
 * @brief 构建基本信息部分
 * @param players 玩家数组
 * @param uptime_secs 运行秒数
 * @return string 基本信息字符串
 */
private string build_basic_info(mapping roster, int uptime_secs)
{
    string result;
    int total;

    result = "";
    total = mapp(roster) ? (roster["total"] || 0) : 0;
    result += sprintf(HIY "║" NOR " 当前在线: %s%4d%s 位玩家" HIY "                   ║" NOR "\n",
        HIG, total, NOR);
    result += sprintf(HIY "║" NOR " 运行时间: %-30s" HIY "║" NOR "\n",
        format_uptime(uptime_secs));
    return result;
}

private string format_idle_label(int idle_sec)
{
    if (idle_sec > 300)
        return HIY "[发呆]" NOR;
    return "";
}

/**
 * @brief 构建系统状态部分
 * @return string 系统状态字符串
 */
private string build_system_status()
{
    string result;
    object monitord;
    mixed cpu;
    mixed mem;

    result = "";
    cpu = 0;
    mem = 0;
    monitord = load_monitor_service();

    if (objectp(monitord) && function_exists("query_metric", monitord)) {
        cpu = monitord->query_metric("sys.cpu.usage", 0);
        mem = monitord->query_metric("sys.memory.usage", 0);
    }

    if (!intp(cpu)) cpu = 0;
    if (!intp(mem)) mem = 0;

    result += HIY "╠════════════════════════════════════════╣" NOR "\n";
    result += sprintf(HIY "║" NOR " 系统负载: CPU %s%3d%%%s    内存 %s%3d%%%s" HIY "          ║" NOR "\n",
        cpu > 80 ? HIR : HIG, cpu, NOR,
        mem > 80 ? HIR : HIG, mem, NOR);
    return result;
}

/**
 * @brief 构建玩家列表部分
 * @param players 玩家数组
 * @return string 玩家列表字符串
 */
private string build_player_list(mapping roster)
{
    mixed *entries;
    mapping entry;
    string result;
    int player_count;
    int count;

    result = "";
    entries = mapp(roster) && arrayp(roster["entries"]) ? roster["entries"] : ({ });
    player_count = sizeof(entries);

    if (player_count <= 0) {
        return result;
    }

    result += HIY "╠════════════════════════════════════════╣" NOR "\n";
    result += sprintf(HIY "║" HIC " 在线玩家 (%d/%d):" HIY "%27s" "║" NOR "\n",
        player_count > 10 ? 10 : player_count,
        player_count, "");

    foreach(entry in entries) {
        string name;
        string idle_str;
        int idle_sec;

        if (!mapp(entry)) continue;
        if (count >= 10) break;

        name = entry["name"] || "无名";
        idle_sec = entry["idle"] || 0;
        idle_str = format_idle_label(idle_sec);

        result += sprintf(HIY "║" NOR "  %-16s %s" HIY "%18s" "║" NOR "\n",
            name, idle_str, "");
        count++;
    }

    if (player_count > 10) {
        result += sprintf(HIY "║" NOR "  ... 还有 %d 位玩家" HIY "%23s" "║" NOR "\n",
            player_count - 10, "");
    }

    return result;
}

/**
 * @brief 构建状态底部
 * @return string 状态底部字符串
 */
private string build_status_footer()
{
    return HIY "╚════════════════════════════════════════╝" NOR "\n";
}

/**
 * @brief 发送状态快捷操作
 * @param me 玩家对象
 */
private void send_status_quick_cmds(object me, mapping response_context)
{
    send_xk_quick_cmds(me,
        "操作|查看在线:who|查看自己:score|查看地图:map",
        1, response_context);
}

int main(object me, string arg)
{
    mapping roster;
    mapping response_context;
    string result;
    int uptime_secs;

    if (!me || !objectp(me)) {
        return 0;
    }
    response_context = CLIENT_SYNC_D->capture_command_response_context(me);

    /* 第三个参数是摘要 worker 的 repeat，不是分页大小；状态页固定使用小 repeat。 */
    roster = PLAYER_ACTOR_ANALYSIS_D->query_online_player_roster_for(
        me, 1, ROSTER_DIGEST_REPEAT);
    uptime_secs = get_server_uptime();

    READ_SUMMARY_D->query_named_summary(me, "status/server", ([
        "type": "server_status_summary",
        "online_count": mapp(roster) ? (roster["total"] || 0) : 0,
        "entry_count": mapp(roster) && arrayp(roster["entries"]) ? sizeof(roster["entries"]) : 0,
        "uptime_seconds": uptime_secs,
    ]));

    result = build_status_header();
    result += build_basic_info(roster, uptime_secs);
    result += build_system_status();
    result += build_player_list(roster);
    result += build_status_footer();

    if (!is_xk_client(me))
        msg_send(me, result, response_context);
    emit_status_dialog(me, result, response_context);

    send_status_report(me, roster, uptime_secs, response_context);
    send_status_quick_cmds(me, response_context);

    return 1;
}

/**
 * 格式化运行时间
 */
string format_uptime(int seconds)
{
    int days, hours, mins;

    if (seconds <= 0) return "未知";

    days = seconds / 86400;
    hours = (seconds % TIME_DAY) / TIME_HOUR;
    mins = (seconds % TIME_HOUR) / TIME_MINUTE;

    if (days > 0) {
        return sprintf("%d天%d小时%d分", days, hours, mins);
    } else if (hours > 0) {
        return sprintf("%d小时%d分", hours, mins);
    } else {
        return sprintf("%d分钟", mins);
    }
}

/**
 * @brief 帮助信息
 */
int help(object me)
{
    msg_send(me, @HELP
命令：status（服务器状态）

查看服务器运行状态。

用法：
  status    - 显示服务器状态

显示内容：
  - 当前在线人数
  - 服务器运行时间
  - 系统负载（CPU/内存）
  - 在线玩家列表

说明：
  - 这是玩家可见的服务器概览，不等同于管理员运维检查

相关命令：
  who    - 查看在线玩家列表
  finger - 查看玩家详细信息
  score  - 查看自己状态

HELP
    );
    return 1;
}
