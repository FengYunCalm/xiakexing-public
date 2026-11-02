#pragma strict_types

#include <system/services.h>
#include <framework/message_facade.h>

inherit "/lib/framework/command";

int help(object me);

int main(object me, string arg)
{
    if (!objectp(me))
        return 0;
    msg_send(me, sprintf("现在是%s。%s正是%s。\n",
        TIME_D->query_full_time_text(),
        TIME_D->query_period_text(),
        TIME_D->query_hour_name()));
    return 1;
}

int help(object me)
{
    msg_send(me, @HELP
命令：time（时辰）

查看当前的游戏时辰与昼夜。
HELP
    );
    return 1;
}
