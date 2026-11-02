/**
 * @file    login_error_snapshot.c
 * @brief   登录错误快照辅助子服务
 * @author  XiaKeXing Development Team
 * @date    2026-03-30
 * @version 1.0.0
 */

#pragma strict_types

#include <system/services.h>

#define MAX_RUNTIME_ERROR_TRACE 8192
#define LOGIN_RUNTIME_ERROR_MESSAGE "登录处理出现异常，请稍后重试。"

private string compact_error_text(mixed err)
{
    string text;

    text = sprintf("%O", err);
    if (strlen(text) > MAX_RUNTIME_ERROR_TRACE)
        return text[0..MAX_RUNTIME_ERROR_TRACE - 1] + "\n...<truncated>\n";
    return text;
}

mapping write_runtime_error_snapshot(mixed err)
{
    string detail_log;
    string trace_text;
    mixed write_error;
    mapping result;

    trace_text = sprintf("[%s] %s\n", ctime(time()), compact_error_text(err));
    detail_log = "";
    write_error = catch(detail_log = call_other(SIMUL_EFUN_LOG_HELPER, "write_error_report",
                                                "runtime", trace_text));
    if (write_error)
        debug_message(sprintf("[login_error_snapshot] report write failed: %O\n", write_error));

    result = ([
        "detail_log": detail_log,
        "trace_text": trace_text
    ]);

    return result;
}

void finalize_runtime_error_snapshot(object login_ob, mixed err)
{
    mapping snapshot;
    object recipient;
    string detail_log;

    snapshot = write_runtime_error_snapshot(err);
    detail_log = mapp(snapshot) ? snapshot["detail_log"] : "";

    log_file("system/errors", sprintf("[LOGIN_INPUT] process_input error: %s | %s\n",
        compact_error_text(err), detail_log));
    recipient = objectp(login_ob) ? login_ob->query_login_player() : 0;
    if (!objectp(recipient))
        recipient = login_ob;
    if (objectp(login_ob))
        login_ob->emit_login_feedback(recipient, LOGIN_RUNTIME_ERROR_MESSAGE, "error");
}
