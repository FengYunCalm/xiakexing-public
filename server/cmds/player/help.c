/**
 * @file    help.c
 * @brief   帮助命令 - 显示常用命令和帮助信息



 *
 * @par Description:
 * 显示常用命令分类，并支持查看特定命令的帮助信息。
 *
 * @par Usage:
 *   help              - 显示常用命令列表
 *   help <命令>       - 显示指定命令的帮助
 *
 */

#pragma strict_types

#include <system/ansi.h>
#include <system/services.h>
#include <game/command.h>
#include <framework/message_facade.h>

inherit "/lib/framework/command";

private mapping cmd_categories = ([
    "基础": ({"look", "go", "map", "inventory", "get", "drop", "score", "reputation", "hp", "status", "time"}),
    "战斗": ({"fight", "attack", "kill", "flee", "equip", "unequip", "skills"}),
    "交流": ({"say", "tell", "whisper", "shout", "emote", "talk", "ask", "follow", "team", "mail"}),
    "物品": ({"buy", "list", "sell", "give", "eat", "drink"}),
    "任务": ({"quest", "quests"}),
    "系统": ({"help", "save", "who", "finger", "alias", "update", "quit"}),
]);

private string format_help_box_row(string content)
{
    return sprintf(HIC "║" NOR " %-38s" HIC "║" NOR "\n", content || "");
}

private string resolve_help_command_path(object me, string verb)
{
    string cmd_file;

    if (!stringp(verb) || strsrch(verb, " ") >= 0
        || !COMMAND_DISPATCH_D->command_exists(me, verb))
        return 0;

    if (is_admin_operator(me)) {
        cmd_file = "/cmds/admin/" + verb;
        if (file_size(cmd_file + ".c") > 0)
            return cmd_file;

        cmd_file = "/cmds/admin/" + verb + "_cmd";
        if (file_size(cmd_file + ".c") > 0)
            return cmd_file;
    }

    cmd_file = "/cmds/player/" + verb;
    if (file_size(cmd_file + ".c") > 0)
        return cmd_file;

    cmd_file = "/cmds/player/" + verb + "_cmd";
    if (file_size(cmd_file + ".c") > 0)
        return cmd_file;

    return 0;
}

/**
 * 显示命令列表
 */
private void show_command_list(object me)
{
    string result;
    string category_text;

    result = HIC "╔════════════════════════════════════════╗" NOR "\n";
    result += HIC "║" HIG "           侠客行命令列表" HIC "                 ║" NOR "\n";
    result += HIC "╠════════════════════════════════════════╣" NOR "\n";

    foreach (string category, string *cmd_list in cmd_categories) {
        category_text = sprintf(HIY "【%s】" NOR " %s", category, implode(cmd_list, " "));
        result += format_help_box_row(category_text);
    }

    result += HIC "╠════════════════════════════════════════╣" NOR "\n";
    result += format_help_box_row("使用 help <命令> 查看详细帮助");
    result += format_help_box_row("首页仅列常用命令，其他专用命令可直接 help 查询");
    result += HIC "╚════════════════════════════════════════╝" NOR "\n";

    msg_send(me, result);

    /* 发送快捷操作 - XK协议已禁用
    {
        mapping *cmds = ({
            (["label":"基础命令", "cmd":"help 基础"]),
            (["label":"战斗命令", "cmd":"help 战斗"]),
            (["label":"交流命令", "cmd":"help 交流"])
        });
        // send_xk_quick_cmds(me, cmds, 1);
    }
    */
}
/**
 * 显示分类命令详情
 */
private void show_category_help(object me, string category)
{
    string result;
    string *cmd_list;
    string title;

    if (!cmd_categories[category]) {
        msg_send(me, HIR "没有找到该分类的帮助信息。" NOR "\n");
        return;
    }

    cmd_list = cmd_categories[category];
    result = HIC "╔════════════════════════════════════════╗" NOR "\n";
    title = HIG + category + "命令帮助" + NOR;
    result += format_help_box_row(title);
    result += HIC "╠════════════════════════════════════════╣" NOR "\n";

    foreach (string cmd in cmd_list) {
        result += format_help_box_row(cmd);
    }

    result += HIC "╚════════════════════════════════════════╝" NOR "\n";

    msg_send(me, result);
}

/**
 * 命令主入口
 */
int main(object me, string arg)
{
    string cmd_file;
    mixed err;

    if (!me) return 0;

    if (!arg || arg == "") {
        show_command_list(me);
        return 1;
    }

    /* 检查是否是分类 */
    if (cmd_categories[arg]) {
        show_category_help(me, arg);
        return 1;
    }

    cmd_file = resolve_help_command_path(me, arg);
    if (stringp(cmd_file)) {
        err = catch(call_other(cmd_file, "help", me));
        if (!err)
            return 1;
    }

    msg_send(me, HIR "没有找到该命令的帮助信息。" NOR "\n");
    msg_send(me, HIC "输入 help 查看所有可用命令。" NOR "\n");
    return 1;
}

/**
 * 帮助信息
 */
int help(object me)
{
    msg_send(me, @HELP
命令：help（帮助）

查看常用命令帮助与分类。

用法：
  help              显示常用命令列表
  help <命令>       显示指定命令的详细帮助
  help <分类>       显示分类下的命令

分类：
  基础  - 移动、查看等基础命令
  战斗  - 攻击、装备等战斗命令
  交流  - 说话、私聊等交流命令
  物品  - 买卖、使用物品命令
  任务  - 任务相关命令
  系统  - 系统功能命令

示例：
  help              查看所有命令
  help look         查看 look 命令帮助
  help 基础          查看基础命令列表

提示：
  - 如果某个命令没有帮助信息，先确认命令名是否正确
  - 管理员命令的帮助只会在管理员角色下显示
  - 首页只列常用命令；房屋、诊断、剧情/测试类专用命令可直接用 help <命令> 查询

HELP
    );
    return 1;
}
