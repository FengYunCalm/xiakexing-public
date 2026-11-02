/**
 * @file    command.c
 * @brief   命令处理框架
 * @author  XiaKeXing Development Team
 * @date    2026-03-20
 * @version 1.0.1
 *
 * @par Description:
 * 提供命令注册、解析、权限检查和帮助生成基础能力。
 */

#pragma strict_types
#pragma save_types

#include <system/globals.h>
#include <system/ansi.h>
#include <system/services.h>

mixed query(string path);
mixed query_temp(string path);
int set_temp(string path, mixed value);
int delete_temp(string path);
private mapping split_command_input(string input);
private mixed invoke_command_action(object target, string action, string args);

private mapping split_command_input(string input)
{
    int space_pos;

    if (!stringp(input) || input == "")
        return ([ "cmd": "", "args": "" ]);

    input = trim(input);
    if (input == "")
        return ([ "cmd": "", "args": "" ]);

    space_pos = strsrch(input, " ");
    if (space_pos == -1)
        return ([ "cmd": input, "args": "" ]);

    return ([
        "cmd": input[0..space_pos-1],
        "args": input[space_pos+1..]
    ]);
}

private mixed invoke_command_action(object target, string action, string args)
{
    if (!stringp(args) || args == "")
        return call_other(target, action);

    return call_other(target, action, args);
}

/**
 * @brief 启用命令处理
 * @return 无返回值
 * @note 将此对象添加到命令处理链
 */
void enable_commands()
{
    set_temp("command/enabled", 1);
}

/**
 * @brief 禁用命令处理
 * @return 无返回值
 */
void disable_commands()
{
    delete_temp("command/enabled");
}

/**
 * @brief 检查是否启用了命令处理
 * @return 1启用，0禁用
 */
int query_commands_enabled()
{
    return query_temp("command/enabled") || 0;
}

/**
 * @brief 添加命令动作
 * @param cmd 命令名称
 * @param action 动作函数名
 * @return 1成功，0失败
 */
int add_action(string cmd, string action)
{
    mapping actions;

    if (!stringp(cmd) || cmd == "")
        return 0;

    if (!stringp(action) || action == "")
        action = "do_" + cmd;

    actions = query_temp("command/actions");
    if (!mapp(actions))
        actions = ([]);

    actions[cmd] = action;
    set_temp("command/actions", actions);

    return 1;
}

/**
 * @brief 删除命令动作
 * @param cmd 命令名称
 * @return 1成功，0失败
 */
int remove_action(string cmd)
{
    mapping actions;

    if (!stringp(cmd) || cmd == "")
        return 0;

    actions = query_temp("command/actions");
    if (!mapp(actions))
        return 0;

    map_delete(actions, cmd);
    set_temp("command/actions", actions);

    return 1;
}

/**
 * @brief 查询命令对应的动作函数
 * @param cmd 命令名称
 * @return 动作函数名，未找到返回0
 */
string query_action(string cmd)
{
    mapping actions;

    if (!stringp(cmd) || cmd == "")
        return 0;

    actions = query_temp("command/actions");
    if (!mapp(actions))
        return 0;

    return actions[cmd];
}

/**
 * @brief 获取所有注册的命令
 * @return 命令名称数组
 */
string *query_all_actions()
{
    mapping actions;
    actions = query_temp("command/actions");
    return mapp(actions) ? keys(actions) : ({});
}

/**
 * @brief 添加命令别名
 * @param alias 别名
 * @param cmd 实际命令
 * @return 1成功，0失败
 */
int add_command_alias(string alias, string cmd)
{
    mapping aliases;

    if (!stringp(alias) || alias == "" || !stringp(cmd) || cmd == "")
        return 0;

    aliases = query_temp("command/aliases");
    if (!mapp(aliases))
        aliases = ([]);

    aliases[alias] = cmd;
    set_temp("command/aliases", aliases);

    return 1;
}

/**
 * @brief 删除命令别名
 * @param alias 别名
 * @return 1成功，0失败
 */
int remove_command_alias(string alias)
{
    mapping aliases;

    if (!stringp(alias) || alias == "")
        return 0;

    aliases = query_temp("command/aliases");
    if (!mapp(aliases))
        return 0;

    map_delete(aliases, alias);
    set_temp("command/aliases", aliases);

    return 1;
}

/**
 * @brief 查询别名对应的实际命令
 * @param alias 别名
 * @return 实际命令名，未找到返回0
 */
string query_alias(string alias)
{
    mapping aliases;

    if (!stringp(alias) || alias == "")
        return 0;

    aliases = query_temp("command/aliases");
    if (!mapp(aliases))
        return 0;

    return aliases[alias];
}

/**
 * @brief 解析命令（包括别名解析）
 * @param input 输入字符串
 * @return ([ "cmd": 命令, "args": 参数 ])
 */
mapping parse_user_command(string input)
{
    string cmd;
    string args;
    string real_cmd;
    mapping result;

    result = split_command_input(input);
    cmd = result["cmd"];
    args = result["args"];

    // 检查别名
    real_cmd = query_alias(cmd);
    if (stringp(real_cmd) && real_cmd != "") {
        cmd = real_cmd;
    }

    result["cmd"] = cmd;
    result["args"] = args;

    return result;
}

// 命令执行
/**
 * @brief 处理输入的命令
 * @param input 用户输入
 * @return 1命令已处理，0未处理
 */
int process_command(string input)
{
    mapping parsed;
    string cmd;
    string args;
    string action;
    int result;

    if (!stringp(input) || input == "")
        return 0;

    if (!query_commands_enabled())
        return 0;

    // 解析命令
    parsed = parse_user_command(input);
    cmd = parsed["cmd"];
    args = parsed["args"];

    if (cmd == "")
        return 0;

    // 查找动作函数
    action = query_action(cmd);
    if (!stringp(action))
        return 0;

    result = invoke_command_action(this_object(), action, args);

    return result;
}

/**
 * @brief 尝试执行命令
 * @param input 用户输入
 * @return 1成功执行，0失败
 * @note 这是外部调用的入口
 */
int command_hook(string input)
{
    string verb;
    mixed err;
    mixed cmd_result;
    mapping parsed;

    if (!stringp(input) || input == "")
        return 0;

    parsed = split_command_input(input);
    verb = parsed["cmd"];
    if (!stringp(verb) || verb == "")
        return 0;

    /* /cmds is the owner-contract route. It returns -1 only when no /cmds
     * route or room command exists; every resolved command is terminal even
     * when owner preparation or command execution fails. */
    err = catch(cmd_result = COMMAND_DISPATCH_D->dispatch_command(
        this_object(), input));
    if (err)
        return 0;
    if (!intp(cmd_result) || cmd_result != -1)
        return intp(cmd_result) ? cmd_result : 1;

    /* Local actions are a fallback only for commands absent from /cmds; they
     * cannot shadow a registered owner command. */
    if (query_commands_enabled() && process_command(input))
        return 1;
    return 0;
}

// 命令权限
/**
 * @brief 检查命令权限
 * @param cmd 命令名
 * @return 1有权限，0无权限
 */
int check_command_permission(string cmd)
{
    string *allowed_cmds;
    string *denied_cmds;

    if (!stringp(cmd) || cmd == "")
        return 0;

    // 检查允许列表
    allowed_cmds = query_temp("command/allowed");
    if (arrayp(allowed_cmds) && sizeof(allowed_cmds) > 0) {
        if (member_array(cmd, allowed_cmds) == -1)
            return 0;
    }

    // 检查禁止列表
    denied_cmds = query_temp("command/denied");
    if (arrayp(denied_cmds) && sizeof(denied_cmds) > 0) {
        if (member_array(cmd, denied_cmds) != -1)
            return 0;
    }

    return 1;
}

/**
 * @brief 设置允许的命令列表
 * @param cmds 命令名称数组
 * @return 无返回值
 */
void set_allowed_commands(string *cmds)
{
    if (arrayp(cmds))
        set_temp("command/allowed", cmds);
}

/**
 * @brief 设置禁止的命令列表
 * @param cmds 命令名称数组
 * @return 无返回值
 */
void set_denied_commands(string *cmds)
{
    if (arrayp(cmds))
        set_temp("command/denied", cmds);
}

// 辅助函数
/**
 * @brief 去除字符串首尾空格
 * @param str 输入字符串
 * @return 处理后的字符串
 */
private string trim(string str)
{
    int start, end;

    if (!stringp(str))
        return "";

    start = 0;
    end = strlen(str) - 1;

    while (start <= end && str[start] == ' ')
        start++;

    while (end >= start && str[end] == ' ')
        end--;

    if (start > end)
        return "";

    return str[start..end];
}

// 创建和清理
/**
 * @brief 重置命令系统
 * @return 无返回值
 */
void reset_command_system()
{
    delete_temp("command/actions");
    delete_temp("command/aliases");
}
