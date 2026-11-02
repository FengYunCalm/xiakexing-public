/**
 * @file    util_input.c
 * @brief   输入验证工具 - Input Validation Utilities



 *
 * @par Description:
 * 提供输入清理和验证功能，包括字符串清理、名称验证、消息长度验证等。
 */

#pragma strict_types
#pragma save_types

#include <system/globals.h>
#include <system/account.h>
#include <system/ansi.h>
#include <system/constants.h>
#include <system/daemon.h>
#include <system/errors.h>
#include <game_constants.h>

/**
 * @brief 清理输入字符串
 * @param input 原始输入
 * @return 清理后的字符串
 */
string normalize_input_text(string input)
{
    if (!input || input == "")
        return "";

    input = replace_string(input, "\n", "");
    input = replace_string(input, "\r", "");
    input = replace_string(input, "\t", " ");

    return input;
}

/**
 * @brief 验证消息长度
 * @param msg 消息内容
 * @param max_length 最大长度
 * @return 1=合法, 0=不合法
 */
int validate_message_input(string msg, int max_length)
{
    if (!max_length)
        max_length = MAX_MESSAGE_LENGTH;

    if (!msg || msg == "")
        return 0;

    if (strlen(msg) > max_length) {
        call_other(ERROR_D, "notify_fail_code", ERR_INVALID_PARAM,
            sprintf("消息太长了，最多%d个字符", max_length), 0);
        return 0;
    }

    return 1;
}
