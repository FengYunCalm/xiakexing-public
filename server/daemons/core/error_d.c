/**
 * @file    error_d.c
 * @brief   错误处理守护进程门面



 *
 * @par Description:
 * 错误处理守护进程门面，聚合错误处理和错误日志能力。
 * 本文件是 facade，错误消息表与日志入口在这里汇总，具体子能力仍由 inherit 的兼容组件提供。
 */

#pragma strict_types
#pragma save_types

#include <system/daemon.h>
#include <system/globals.h>
#include <system/constants.h>
#include <system/errors.h>
#include <framework/message_facade.h>

inherit CORE_DAEMON_BASE;
inherit "/daemons/core/error_handler";
inherit "/daemons/core/error_logger";

mapping query_error_messages() { return _error_messages; }
mapping query_error_contexts() { return _error_contexts; }
mixed *query_error_history_arr() { return _error_history; }
mapping query_error_stats() { return _error_stats; }

private string format_error_message(int error_code, string detail)
{
    string msg;

    msg = get_error_message(error_code);
    if (!stringp(msg) || msg == "")
        msg = "未知错误";

    if (stringp(detail) && detail != "") {
        if (msg == "未知错误")
            return detail;
        return sprintf("%s：%s", msg, detail);
    }

    return msg;
}

varargs int notify_fail_code(int error_code, string detail, int log_it)
{
    string msg = format_error_message(error_code, detail);

    notify_fail(msg + "\n");
    if (log_it)
        log_error(error_code, detail || "");

    return 0;
}

varargs int send_error(object target, int error_code, string detail, int log_it)
{
    string msg = format_error_message(error_code, detail);

    if (objectp(target))
        msg_send(target, msg + "\n");
    if (log_it)
        log_error(error_code, detail || "");

    return 0;
}

string query_error_message(int error_code, string detail)
{
    return format_error_message(error_code, detail);
}

void create()
{
    ::create();

    _error_messages = ERROR_MESSAGES;
    _error_contexts = ([]);
    _error_history = ({});
    _error_stats = ([]);

    if (!mapp(_error_messages))
        _error_messages = ([]);

    log_file("system/error", sprintf("[%s] Error daemon initialized (Facade)\n", ctime(time())));
}
