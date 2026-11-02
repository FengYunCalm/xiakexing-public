/**
 * @file    player_alias.c
 * @brief   玩家别名系统



 *
 * @par Description:
 * 玩家命令别名子系统，负责默认别名、玩家自定义别名与链式输入拆分。
 */

#pragma strict_types

#include <system/ansi.h>
#include <system/services.h>
#include <framework/message_facade.h>

int command_hook(string input);
string gateway_receive(mixed data);
void touch_command_time();
mixed query(string path);
mixed query_temp(string path);
int set_temp(string path, mixed value);
private string trim(string str);
private string *split_command_chain(string input, int limit);
private int execute_command_chain(string input);
private void ensure_player_aliases();
private int commit_player_aliases(mapping aliases);
int delete_alias(string verb);
private mapping expand_alias_template(string template, string *args, string argstr);
private string expand_alias(string cmd);
private int can_run_while_dying(string cmd);
private int block_if_incapacitated(string input);

#define MAX_PLAYER_ALIASES  50    // 玩家别名上限
#define MAX_ALIAS_LENGTH    256   // 别名最大长度
#define MAX_CHAIN_COMMANDS  20    // 单次链式命令上限

nosave private mapping _global_aliases = ([
    "i": "inventory",
    "l": "look",
    "ex": "look",

    "n": "go north",
    "s": "go south",
    "e": "go east",
    "w": "go west",
    "ne": "go northeast",
    "nw": "go northwest",
    "se": "go southeast",
    "sw": "go southwest",
    "u": "go up",
    "d": "go down",
    "north": "go north",
    "south": "go south",
    "east": "go east",
    "west": "go west",
    "northeast": "go northeast",
    "northwest": "go northwest",
    "southeast": "go southeast",
    "southwest": "go southwest",
    "up": "go up",
    "down": "go down",
    "northup": "go northup",
    "northdown": "go northdown",
    "southup": "go southup",
    "southdown": "go southdown",
    "eastup": "go eastup",
    "eastdown": "go eastdown",
    "westup": "go westup",
    "westdown": "go westdown",
    "enter": "go enter",
    "out": "go out",

    "eq": "equip",
    "uneq": "unequip",
    "inv": "inventory",
    "sc": "score",
    "sk": "skills",
    "st": "status",
    "hp": "hp",
    "h": "help",
    "?": "help",

    "t": "tell",
    "f": "finger",
    "say": "say",
    "sh": "shout",
    "wh": "whisper",
    "em": "emote",

    "k": "kill",
    "a": "attack",
    "fi": "fight",
    "fl": "flee",

    "g": "get",
    "dr": "drop",
    "gi": "give",

    "q": "quest",
    "qs": "quests",

    "b": "bank",
    "ba": "bank",
    "m": "map",
    "time": "time",
    "ti": "time",
    "save": "save",
    "sa": "save",
    "quit": "quit",
    "qu": "quit",
    "exa": "look"
]);

nosave private mapping _player_aliases;

/**
 * @brief 修剪字符串（去除首尾空格）
 * @param str 输入字符串
 * @return 修剪后的字符串
 */
private string trim(string str)
{
    int start, end, len;

    if (!str || str == "")
        return "";

    len = strlen(str);

    start = 0;
    while (start < len && str[start] == ' ')
        start++;

    end = len - 1;
    while (end >= start && str[end] == ' ')
        end--;

    if (start > end)
        return "";

    return str[start..end];
}

private string *split_command_chain(string input, int limit)
{
    string *raw_parts;
    string *commands;
    string part;
    int i;
    int max_commands;

    if (!stringp(input) || input == "")
        return ({ });

    max_commands = (intp(limit) && limit > 0) ? limit : MAX_CHAIN_COMMANDS;
    if (max_commands <= 0)
        return ({ });

    raw_parts = explode(input, ";");
    commands = ({ });

    for (i = 0; i < sizeof(raw_parts); i++) {
        part = trim(raw_parts[i]);
        if (part == "")
            continue;

        commands += ({ part });
        if (sizeof(commands) >= max_commands)
            break;
    }

    return commands;
}

private int can_run_while_dying(string cmd)
{
    return member_array(cmd, ({ "score", "status", "help", "finger", "quit" })) != -1;
}

private int block_if_incapacitated(string input)
{
    int blocked;
    string cmd;
    mapping parsed;
    int hp;

    hp = query("combat/hp");
    if (!intp(hp))
        hp = 0;

    blocked = query_temp("no_command") || query_temp("command_blocked");

    if (!blocked && hp > 0 && !query_temp("is_dying"))
        return 0;

    parsed = ([ "cmd": "", "args": "" ]);
    if (stringp(input) && input != "")
        parsed = ([ "cmd": explode(input, " ")[0], "args": "" ]);

    cmd = parsed["cmd"];
    if (can_run_while_dying(cmd))
        return 0;

    msg_send(this_object(), HIR "你已重伤倒地，无法执行这个动作。" NOR "\n");
    return 1;
}

private int execute_command_chain(string input)
{
    string *commands;
    string *expanded_commands;
    string command;
    string expanded_cmd;
    int executed;
    int remaining;

    remaining = MAX_CHAIN_COMMANDS;
    commands = split_command_chain(input, remaining);
    if (!sizeof(commands))
        return 0;

    foreach (command in commands) {
        if (remaining <= 0)
            break;

        expanded_cmd = trim(expand_alias(command));
        if (expanded_cmd == "")
            continue;

        expanded_commands = split_command_chain(expanded_cmd, remaining);
        if (!sizeof(expanded_commands))
            expanded_commands = ({ expanded_cmd });

        foreach (command in expanded_commands) {
            if (remaining <= 0)
                break;
            if (block_if_incapacitated(command)) {
                executed = 1;
                remaining--;
                continue;
            }
            if (command_hook(command)) {
                executed = 1;
            } else {
                msg_send(this_object(), "What?\n");
            }
            remaining--;
        }
    }

    return executed;
}

/* 从持久状态惰性加载，避免角色数据尚未恢复时提前覆盖。 */
private void ensure_player_aliases()
{
    mixed persisted;

    if (mapp(_player_aliases))
        return;
    persisted = query("player/aliases");
    _player_aliases = mapp(persisted) ? persisted : ([]);
}

private int commit_player_aliases(mapping aliases)
{
    mapping result;

    if (!mapp(aliases))
        return notify_fail("别名保存失败：数据格式无效。\n");
    result = PLAYER_STATE_D->set_state(this_object(), "player/aliases",
        aliases, ([]));
    if (!mapp(result) || !result["ok"]) {
        if (mapp(result) && stringp(result["message"])
            && result["message"] != "")
            return notify_fail("别名保存失败：" + result["message"] + "\n");
        return notify_fail("别名保存失败。\n");
    }
    _player_aliases = copy(aliases);
    return 1;
}

/**
 * @brief 获取所有别名（全局 + 玩家自定义）
 * @return 合并后的别名映射
 */
mapping query_all_aliases()
{
    mapping all = ([]);

    ensure_player_aliases();
    if (mapp(_global_aliases))
        all += _global_aliases;

    if (mapp(_player_aliases))
        all += _player_aliases;

    return all;
}

/**
 * @brief 查询玩家自定义别名
 * @return 玩家别名映射
 */
mapping query_player_aliases()
{
    ensure_player_aliases();
    return copy(_player_aliases);
}

/**
 * @brief 设置玩家别名
 * @param verb 别名动词
 * @param replacement 替换的命令
 * @return 1=成功, 0=失败
 * @description
 * 玩家自定义别名。replacement为0时删除别名。
 * 支持参数替换：$1, $2, ... $*
 */
int set_alias(string verb, string replacement)
{
    mapping next_aliases;

    if (!stringp(verb) || verb == "")
        return notify_fail("别名不能为空。\n");

    ensure_player_aliases();
    if (_global_aliases[verb] && replacement)
        return notify_fail("\"" + verb + "\" 是系统保留别名，不能覆盖。\n");

    if (verb == "alias")
        return notify_fail("不能为 alias 命令本身设置别名。\n");

    if (!stringp(replacement) || replacement == "")
        return delete_alias(verb);

    if (strlen(replacement) > MAX_ALIAS_LENGTH)
        return notify_fail("别名内容太长（最多" + MAX_ALIAS_LENGTH + "字符）。\n");

    if (sizeof(_player_aliases) >= MAX_PLAYER_ALIASES
        && undefinedp(_player_aliases[verb]))
        return notify_fail("你的别名数量已达上限（" + MAX_PLAYER_ALIASES + "个），请先删除一些。\n");

    next_aliases = copy(_player_aliases);
    next_aliases[verb] = replacement;
    return commit_player_aliases(next_aliases);
}

/**
 * @brief 删除玩家别名
 * @param verb 要删除的别名
 * @return 1=成功, 0=失败
 */
int delete_alias(string verb)
{
    mapping next_aliases;

    if (!stringp(verb) || verb == "")
        return notify_fail("请指定要删除的别名。\n");

    ensure_player_aliases();
    if (undefinedp(_player_aliases[verb]))
        return notify_fail("你没有设置 \"" + verb + "\" 这个别名。\n");

    next_aliases = copy(_player_aliases);
    map_delete(next_aliases, verb);
    return commit_player_aliases(next_aliases);
}

/**
 * @brief 扩展命令别名（支持参数替换）
 * @param cmd 原始命令
 * @return 扩展后的命令
 * @description
 * 处理命令别名映射，支持参数替换：
 * - $1, $2, $3... 替换为第N个参数
 * - $* 替换为所有参数
 * 示例: alias pb put $1 in $2
 *       pb bandage bag → put bandage in bag
 */
private int is_alias_digit(int value)
{
    return value >= '0' && value <= '9';
}

private int parse_alias_index(string digits)
{
    int index;
    int i;

    index = 0;
    for (i = 0; i < strlen(digits); i++)
        index = index * 10 + digits[i] - '0';
    return index;
}

private mapping expand_alias_template(string template, string *args, string argstr)
{
    string result;
    string digits;
    int i;
    int j;
    int index;
    int length;
    int has_placeholder;

    result = "";
    length = strlen(template);
    has_placeholder = 0;
    for (i = 0; i < length; i++) {
        if (template[i] != '$') {
            result += template[i..i];
            continue;
        }
        if (i + 1 < length && template[i + 1] == '*') {
            result += argstr;
            has_placeholder = 1;
            i++;
            continue;
        }
        if (i + 1 >= length || !is_alias_digit(template[i + 1])) {
            result += "$";
            continue;
        }
        j = i + 1;
        while (j < length && is_alias_digit(template[j]))
            j++;
        digits = template[i + 1..j - 1];
        index = parse_alias_index(digits);
        if (index > 0) {
            has_placeholder = 1;
            if (index <= sizeof(args))
                result += args[index - 1];
            i = j - 1;
            continue;
        }
        result += template[i..j - 1];
        i = j - 1;
    }
    return (["text": result, "has_placeholder": has_placeholder]);
}

private string expand_alias(string cmd)
{
    string *parts;
    string alias_expansion;
    string argstr;
    string *args;
    string verb;
    mapping all_aliases;
    mapping expanded;
    int i;

    if (!stringp(cmd) || cmd == "")
        return cmd;

    all_aliases = query_all_aliases();
    if (!mapp(all_aliases) || sizeof(all_aliases) == 0)
        return cmd;

    parts = explode(cmd, " ");
    if (!arrayp(parts) || sizeof(parts) == 0)
        return cmd;

    verb = parts[0];
    if (undefinedp(all_aliases[verb]))
        return cmd;

    alias_expansion = all_aliases[verb];
    if (!stringp(alias_expansion) || alias_expansion == verb)
        return cmd;

    args = ({ });
    for (i = 1; i < sizeof(parts); i++) {
        if (parts[i] != "")
            args += ({ parts[i] });
    }
    argstr = implode(args, " ");
    expanded = expand_alias_template(alias_expansion, args, argstr);
    alias_expansion = expanded["text"];
    if (!expanded["has_placeholder"] && argstr != "")
        alias_expansion += " " + argstr;

    return trim(replace_string(alias_expansion, "  ", " "));
}

/**
 * @brief 处理玩家输入（命令别名扩展）
 * @param str 原始输入字符串
 * @return 处理后的输入字符串
 * @description
 * 这是FluffOS驱动调用的特殊函数，在命令执行前预处理输入。
 * 子类可以通过继承覆盖此函数来添加额外处理。
 */
string process_input(string str)
{
    string trimmed_input;
    string expanded_cmd;

    // 基本清理
    if (!str || str == "")
        return str;

    touch_command_time();

    // 去除首尾空格
    trimmed_input = trim(str);
    if (trimmed_input == "")
        return str;

    /* Direct TCP mode carries the same typed command envelope as driver mode.
     * Route it through player_session so the request scope is installed before
     * any command producer emits a protocol response; malformed candidates are
     * consumed instead of becoming JSON/username commands. */
    if (strlen(trimmed_input) >= 1 && trimmed_input[0] == '{')
        return gateway_receive(trimmed_input);

    if (strsrch(trimmed_input, ";") != -1) {
        execute_command_chain(trimmed_input);
        return "";
    }

    // 扩展别名
    expanded_cmd = expand_alias(trimmed_input);

    if (block_if_incapacitated(expanded_cmd))
        return "";

    if (strsrch(expanded_cmd, ";") != -1) {
        execute_command_chain(expanded_cmd);
        return "";
    }

    if (command_hook(expanded_cmd))
        return "";

    msg_send(this_object(), "What?\n");
    return "";
}
