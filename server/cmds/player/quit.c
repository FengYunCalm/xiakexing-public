/**
 * @file    quit.c
 * @brief   退出命令 - 安全退出游戏



 */

#pragma strict_types

#include <system/ansi.h>
#include <framework/message_facade.h>
#include <system/services.h>

inherit "/lib/framework/command";

int main(object me, string arg)
{
    object env;
    string player_name;
    string message;
    mapping termination;

    if (!me) return 0;

    env = environment(me);
    player_name = me->query_name();
    if (!stringp(player_name) || player_name == "")
        player_name = me->query("name") || "有人";

    /* 终结服务在保存成功、析构前发送告别语；失败路径只提示可重试，
     * 不让玩家先看到已经退出的假象。房间广播等终结成功后再发送。 */
    message = HIC "╔════════════════════════════════════════╗" NOR "\n";
    message += HIC "║" HIG "       感谢游玩侠客行！" HIC "                 ║" NOR "\n";
    message += HIC "║" NOR "     江湖路远，后会有期！" HIC "               ║" NOR "\n";
    message += HIC "╚════════════════════════════════════════╝" NOR "\n";

    termination = PLAYER_LIFECYCLE_D->terminate_player(me, "quit", message);
    if (!mapp(termination) || !termination["ok"]) {
        msg_send(me, "退出尚未完成，角色仍在线；请稍后重试。\n");
        log_file("system/error", sprintf("[%s] quit termination pending for %s: %O\n",
            ctime(time()), get_object_id(me) || "?", termination));
        return 0;
    }

    if (objectp(env))
        msg_to_room(env, player_name + "离开了游戏。\n");
    return 1;
}

int help(object me)
{
    msg_send(me, @HELP
命令：quit（退出）

安全退出游戏。

用法：
  quit              退出游戏

注意：
  - 退出前会自动保存角色数据
  - 请使用 quit 命令安全退出

HELP
    );
    return 1;
}
