/**
 * @file    login_security.c
 * @brief   登录安全检查模块
 * @author  XiaKeXing Development Team
 * @date    2026-03-20
 * @version 1.0.1
 *
 * @par Description:
 * 提供登录输入安全相关能力，包括 JSON/HTML 转义与输入长度验证。
 */

#pragma strict_types

#define MAX_INPUT_LENGTH       256

string xk_escape_json(string str);
string escape_output(string str);
int validate_input_length(string str);

/**
 * @brief JSON 字符串转义
 * @param str 原始字符串
 * @return 转义后的字符串
 *
 * 防止 JSON 注入攻击。
 */
string xk_escape_json(string str)
{
    if (!str)
        return "";

    if (strlen(str) > MAX_INPUT_LENGTH) {
        str = str[0..MAX_INPUT_LENGTH-1];
    }

    str = replace_string(str, "\\", "\\\\");
    str = replace_string(str, "\"", "\\\"");
    str = replace_string(str, "\n", "\\n");
    str = replace_string(str, "\r", "\\r");

    str = replace_string(str, "<", "\\u003c");
    str = replace_string(str, ">", "\\u003e");
    str = replace_string(str, "&", "\\u0026");

    return str;
}

/**
 * @brief 输出转义（HTML 安全）
 * @param str 原始字符串
 * @return 转义后的字符串
 *
 * 防止 XSS 攻击。
 */
string escape_output(string str)
{
    if (!str)
        return "";

    if (strlen(str) > MAX_INPUT_LENGTH) {
        str = str[0..MAX_INPUT_LENGTH-1];
    }

    str = replace_string(str, "<", "&lt;");
    str = replace_string(str, ">", "&gt;");
    str = replace_string(str, "&", "&amp;");
    str = replace_string(str, "\"", "&quot;");
    str = replace_string(str, "'", "&#x27;");

    return str;
}

/**
 * @brief 验证输入长度
 * @param str 输入字符串
 * @return 1=合法, 0=超长
 */
int validate_input_length(string str)
{
    if (!str)
        return 1;

    return strlen(str) <= MAX_INPUT_LENGTH;
}
