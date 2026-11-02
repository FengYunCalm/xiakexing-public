/**
 * @file    util_args.c
 * @brief   参数解析工具 - Argument Parsing Utilities



 *
 * @par Description:
 * 提供命令参数解析功能，包括通用参数解析和特定命令的参数解析。
 */

#pragma strict_types
#pragma save_types

#include <system/globals.h>
#include <system/ansi.h>
#include <system/constants.h>
#include <game_constants.h>

inherit "/lib/std/utils/util_input";

/**
 * @brief 解析命令参数
 * @param arg 输入参数
 * @param pattern sscanf 格式字符串，如 "%s %s"
 * @return mapping (["valid": 0/1, "args": ({}), "error": ""])
 */
mapping parse_command_args(string arg, string pattern)
{
    mapping result = (["valid": 0, "args": ({}), "error": ""]);
    mixed *args;
    int count;
    int num_args;
    int i;

    if (!arg || arg == "") {
        result["error"] = "命令格式错误\n";
        return result;
    }

    if (strlen(arg) > MAX_INPUT_LENGTH) {
        result["error"] = "输入太长了\n";
        return result;
    }

    if (!stringp(pattern) || pattern == "") {
        result["error"] = "不支持的参数格式\n";
        return result;
    }

    num_args = 0;
    for (i = 0; i < strlen(pattern); i++) {
        if (pattern[i] != '%')
            continue;
        if (i + 1 >= strlen(pattern)) {
            result["error"] = "不支持的参数格式\n";
            return result;
        }
        i++;
        if (pattern[i] == '%')
            continue;
        if (pattern[i] != 's' && pattern[i] != 'd') {
            result["error"] = "不支持的参数格式\n";
            return result;
        }
        num_args++;
    }

    if (num_args < 1 || num_args > 6) {
        result["error"] = "不支持的参数数量\n";
        return result;
    }

    args = allocate(num_args);

    switch (num_args) {
        case 1:
            count = sscanf(arg, pattern, args[0]);
            break;
        case 2:
            count = sscanf(arg, pattern, args[0], args[1]);
            break;
        case 3:
            count = sscanf(arg, pattern, args[0], args[1], args[2]);
            break;
        case 4:
            count = sscanf(arg, pattern, args[0], args[1], args[2], args[3]);
            break;
        case 5:
            count = sscanf(arg, pattern, args[0], args[1], args[2], args[3], args[4]);
            break;
        case 6:
            count = sscanf(arg, pattern, args[0], args[1], args[2], args[3], args[4], args[5]);
            break;
        default:
            result["error"] = "不支持的参数数量\n";
            return result;
    }

    if (count != num_args) {
        result["error"] = "格式错误\n";
        return result;
    }

    result["valid"] = 1;
    result["args"] = args;
    return result;
}

/**
 * @brief 解析子命令参数（动作 + 剩余参数）
 * @param arg 输入参数
 * @return mapping (['action': "", 'rest': "", 'valid': 0/1, 'error': ""])
 */
mapping parse_subcommand_args(string arg)
{
    mapping result = (["action": "", "rest": "", "valid": 0, "error": ""]);
    int space_pos;

    if (!stringp(arg) || arg == "") {
        result["error"] = "参数不足。\n";
        return result;
    }

    arg = normalize_input_text(arg);
    if (arg == "") {
        result["error"] = "参数不足。\n";
        return result;
    }

    space_pos = strsrch(arg, " ");
    if (space_pos == -1) {
        result["action"] = arg;
        result["rest"] = "";
        result["valid"] = 1;
        return result;
    }

    result["action"] = arg[0..space_pos - 1];
    result["rest"] = arg[space_pos + 1..];
    result["valid"] = 1;
    return result;
}

/**
 * @brief 解析 tell 命令参数
 * @param arg 输入参数
 * @return mapping (["target": "", "message": "", "valid": 0/1, "error": ""])
 */
mapping parse_tell_args(string arg)
{
    mapping result = (["target": "", "message": "", "valid": 0, "error": ""]);

    if (!stringp(arg) || arg == "") {
        result["error"] = "私聊谁？格式：tell <玩家名> <消息>\n";
        return result;
    }

    if (sscanf(arg, "%s %s", result["target"], result["message"]) != 2) {
        result["error"] = "格式错误。用法：tell <玩家名> <消息>\n";
        return result;
    }

    result["message"] = normalize_input_text(result["message"]);
    if (result["message"] == "") {
        result["error"] = "你要说什么？\n";
        return result;
    }

    if (strlen(result["message"]) > MAX_TELL_LENGTH) {
        result["error"] = "消息太长了，最多" + MAX_TELL_LENGTH + "个字符。\n";
        return result;
    }

    result["valid"] = 1;
    return result;
}

/**
 * @brief 解析 give 命令参数
 * @param arg 输入参数
 * @return mapping (["item": "", "target": "", "valid": 0/1, "error": ""])
 */
mapping parse_give_args(string arg)
{
    mapping result = (["item": "", "target": "", "valid": 0, "error": ""]);

    if (!arg || arg == "") {
        result["error"] = "命令格式：give <物品> to <人物>\n";
        return result;
    }

    if (sscanf(arg, "%s to %s", result["item"], result["target"]) != 2 &&
        sscanf(arg, "%s %s", result["item"], result["target"]) != 2) {
        result["error"] = "命令格式：give <物品> to <人物>\n";
        return result;
    }

    if (!result["item"] || result["item"] == "" ||
        !result["target"] || result["target"] == "") {
        result["error"] = "命令格式：give <物品> to <人物>\n";
        return result;
    }

    result["valid"] = 1;
    return result;
}

/**
 * @brief 解析 whisper 命令参数
 * @param arg 输入参数
 * @return mapping (["target_name": "", "message": "", "valid": 0, "error": ""])
 */
mapping parse_whisper_args(string arg)
{
    mapping result = (["target": "", "message": "", "valid": 0, "error": ""]);

    if (!stringp(arg) || arg == "") {
        result["error"] = "用法：whisper <玩家> <消息>\n";
        return result;
    }

    if (sscanf(arg, "%s %s", result["target"], result["message"]) != 2) {
        result["error"] = "用法：whisper <玩家> <消息>\n";
        return result;
    }

    result["message"] = normalize_input_text(result["message"]);
    if (result["message"] == "") {
        result["error"] = "你要说什么？\n";
        return result;
    }

    if (strlen(result["message"]) > MAX_TELL_LENGTH) {
        result["error"] = "消息太长了，最多" + MAX_TELL_LENGTH + "个字符。\n";
        return result;
    }

    result["valid"] = 1;
    return result;
}

/**
 * @brief 解析 say/shout 等消息命令参数
 * @param arg 输入参数
 * @return mapping (["message": "", "valid": 0/1, "error": ""])
 */
mapping parse_say_args(string arg)
{
    mapping result = (["message": "", "valid": 0, "error": ""]);

    if (!arg || arg == "") {
        result["error"] = "说什么？\n";
        return result;
    }

    arg = normalize_input_text(arg);

    if (arg == "") {
        result["error"] = "说什么？\n";
        return result;
    }

    if (strlen(arg) > MAX_SAY_LENGTH) {
        result["error"] = "消息太长了，最多" + MAX_SAY_LENGTH + "个字符。\n";
        return result;
    }

    result["message"] = arg;
    result["valid"] = 1;
    return result;
}

// Forward declaration
string normalize_input_text(string input);
