/**
 * @file    error_logger.c
 * @brief   错误日志模块 - Error Logger
 */

#pragma strict_types
#pragma save_types

#include <system/globals.h>
#include <system/constants.h>


mapping _error_messages;
mixed *_error_history;
mapping _error_stats;

void log_error(int error_code, string details)
{
    mapping error_entry;

    error_entry = ([
        "code": error_code,
        "details": details,
        "time": time(),
        "timestamp": ctime(time())
    ]);

    _error_history += ({ error_entry });

    // 限制历史大小
    if (sizeof(_error_history) > MAX_ERROR_HISTORY)
        _error_history = _error_history[1..];

    // 更新统计
    if (!intp(_error_stats[error_code]))
        _error_stats[error_code] = 0;
    _error_stats[error_code]++;
}

mixed *query_error_history(int count)
{
    int size;

    size = sizeof(_error_history);

    if (count <= 0 || count > size)
        count = size;

    return _error_history[<count..];
}

int query_error_count(int error_code)
{
    if (!intp(_error_stats[error_code]))
        return 0;

    return _error_stats[error_code];
}

void clear_error_history()
{
    _error_history = ({});
    _error_stats = ([]);
}
