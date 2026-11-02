/**
 * @file    login_input_support.c
 * @brief   登录输入辅助子服务
 * @author  XiaKeXing Development Team
 * @date    2026-03-30
 * @version 1.0.0
 */

#pragma strict_types

private int is_trim_whitespace(int ch)
{
    return ch == 32 || ch == 9 || ch == 10 || ch == 13;
}

string trim_input(string input)
{
    int start, end;

    if (!input || input == "")
        return "";

    start = 0;
    while (start < strlen(input) && is_trim_whitespace(input[start]))
        start++;

    end = strlen(input) - 1;
    while (end >= start && is_trim_whitespace(input[end]))
        end--;

    if (start > end)
        return "";

    return input[start..end];
}

int is_cancel_command(string input)
{
    if (!input || input == "")
        return 0;

    input = lower_case(trim_input(input));
    return (input == "cancel" || input == "quit" || input == "退出" || input == "取消");
}

void emit_login_feedback(object recipient, string message, string level)
{
    if (!objectp(recipient) || !stringp(message))
        return;

    call_other("/kernel/simul_efun", "xk_send_system",
        recipient, message, stringp(level) ? level : "info");
}
