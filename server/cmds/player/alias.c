/**
 * @file    alias.c
 * @brief   别名命令



 *
 * @par Description:
 * 管理玩家自定义命令别名，支持：
 * - 查看所有别名
 * - 设置新别名（支持参数替换 $1, $2, $*）
 * - 删除别名
 *
 * @par Usage:
 *   alias              - 查看所有别名
 *   alias <缩写>       - 删除该别名
 *   alias <缩写> <命令> - 设置别名
 *
 * @par Examples:
 *   alias sc score              - sc 替代 score
 *   alias pb put $1 in $2       - pb bandage bag → put bandage in bag
 *   alias gg get $* from corpse - gg gold → get gold from corpse
 */

#pragma strict_types

#include <system/ansi.h>
#include <game/command.h>
#include <framework/message_facade.h>

inherit "/lib/framework/command";

int main(object me, string arg);
int help(object me);

/**
 * @brief 显示全局默认别名
 */
private void show_global_aliases(object me)
{
    msg_send(me, HIC "\n══════════ 系统默认别名 ══════════" NOR "\n");
    msg_send(me, HIY "方向:" NOR " n/s/e/w/ne/nw/se/sw/u/d = go xxx\n");
    msg_send(me, HIY "查看:" NOR " i=inventory, l=look, ex=look\n");
    msg_send(me, HIY "常用:" NOR " eq=equip, sc=score, sk=skills\n");
    msg_send(me, HIY "社交:" NOR " t=tell, f=finger, wh=whisper\n");
}

/**
 * @brief 显示玩家自定义别名
 */
private void show_player_aliases(object me)
{
    mapping aliases;
    string *verbs;
    int i, size;

    aliases = me->query_player_aliases();

    if (!mapp(aliases) || sizeof(aliases) == 0)
    {
        msg_send(me, HIG "\n你目前没有设置任何自定义别名。" NOR "\n");
        msg_send(me, HIC "使用: alias <缩写> <命令> 来设置别名。" NOR "\n");
        return;
    }

    verbs = sort_array(keys(aliases), 1);
    size = sizeof(verbs);

    msg_send(me, HIC "\n══════════ 你的自定义别名 (" + size + "/50) ══════════" NOR "\n");

    for (i = 0; i < size; i++)
    {
        msg_send(me, sprintf(HIY "%-12s" NOR " = " HIG "%s" NOR "\n",
            verbs[i], aliases[verbs[i]]));
    }
}

int main(object me, string arg)
{
    string verb, replacement;
    mapping player_aliases;

    if (!me)
        return 0;

    if (!arg || arg == "")
    {
        show_global_aliases(me);
        show_player_aliases(me);

        msg_send(me, HIC "\n══════════ 使用说明 ══════════" NOR "\n");
        msg_send(me, "设置: alias sc score\n");
        msg_send(me, "删除: alias sc\n");
        msg_send(me, "参数: alias tg tell $1 $2\n");
        return 1;
    }

    if (sscanf(arg, "%s %s", verb, replacement) != 2)
    {
        verb = arg;

        if (verb == "") {
            msg_send(me, "请指定要删除的别名。\n");
            return 1;
        }

        player_aliases = me->query_player_aliases();
        if (!mapp(player_aliases) || undefinedp(player_aliases[verb])) {
            msg_send(me, "你没有设置 \"" + verb + "\" 这个别名。\n");
            return 1;
        }

        if (me->delete_alias(verb))
        {
            msg_send(me, HIG "【别名】已删除 \"" + verb + "\"" NOR "\n");
            return 1;
        }

        msg_send(me, "删除别名失败。\n");
        return 1;
    }

    if (verb == "") {
        msg_send(me, "别名不能为空。\n");
        return 1;
    }

    if (verb == "alias") {
        msg_send(me, "不能为 alias 命令本身设置别名。\n");
        return 1;
    }

    if (me->set_alias(verb, replacement))
    {
        msg_send(me, HIG "【别名】设置成功！" NOR "\n");
        msg_send(me, sprintf("  " HIY "%s" NOR " → " HIC "%s" NOR "\n", verb, replacement));

        if (strsrch(replacement, "$") != -1)
        {
            msg_send(me, HIC "  支持参数替换: $1, $2, ... $*" NOR "\n");
        }
        return 1;
    }

    msg_send(me, "设置别名失败。\n");
    return 1;
}

int help(object me)
{
    msg_send(me, @HELP
命令：alias（别名管理）

管理你的自定义命令别名，简化常用操作。

用法：
  alias              - 查看所有别名
  alias <缩写>       - 删除该别名
  alias <缩写> <命令> - 设置新别名

参数替换：
  $1, $2, $3... - 第N个参数
  $*           - 所有参数

示例：
  alias sc score
    → 以后输入 sc 就相当于输入 score

  alias tg tell $1 $2
    → 输入 tg friend 你好
    → 相当于 tell friend 你好

  alias gg get $* from corpse
    → 输入 gg gold silver
    → 相当于 get gold silver from corpse

  alias kill
    → 删除 kill 这个别名

限制：
  - 最多可设置 50 个自定义别名
  - 不能覆盖系统默认别名（如 n, s, e, w）
  - 不能为 alias 命令本身设置别名

HELP
    );
    return 1;
}
