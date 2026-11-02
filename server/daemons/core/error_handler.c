/**
 * @file    error_handler.c
 * @brief   错误处理模块 - Error Handler
 */

#pragma strict_types
#pragma save_types

#include <system/globals.h>
#include <system/constants.h>
#include <system/errors.h>

#define MAX_ERROR_CONTEXT_DEPTH     10

mapping _error_messages;
mapping _error_contexts;
mixed *_error_history;

string get_error_message(int error_code)
{
    string msg;

    if (!mapp(_error_messages))
        _error_messages = ([]);

    msg = _error_messages[error_code];
    if (!stringp(msg))
        msg = GET_ERROR_MESSAGE(error_code);

    return msg;
}

void set_error_message(int error_code, string message)
{
    if (!stringp(message))
        return;

    if (!mapp(_error_messages))
        _error_messages = ([]);

    _error_messages[error_code] = message;
}

mixed *get_error_context(int error_code)
{
    mixed *context;

    if (!mapp(_error_contexts))
        return ({});

    context = _error_contexts[error_code];
    if (!arrayp(context))
        return ({});

    return context;
}

void set_error_context(int error_code, mixed *context)
{
    if (!arrayp(context))
        return;

    if (!mapp(_error_contexts))
        _error_contexts = ([]);

    if (sizeof(context) > MAX_ERROR_CONTEXT_DEPTH)
        context = context[0..MAX_ERROR_CONTEXT_DEPTH-1];

    _error_contexts[error_code] = context;
}

int is_recoverable(int error_code)
{
    // 可恢复错误判断
    if (error_code >= 1000 && error_code < 2000)
        return 1;  // 系统级错误可恢复

    return 0;
}
