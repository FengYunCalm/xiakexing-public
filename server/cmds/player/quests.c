/**
 * @file    quests.c
 * @brief   任务列表命令



 *
 * @par Description:
 * 查看当前系统中可用的任务，并按类型筛选。
 */

#pragma strict_types

#include <system/ansi.h>
#include <system/services.h>
#include <game/command.h>
#include <game/quest.h>
#include <framework/message_facade.h>

inherit "/lib/framework/command";

private int is_valid_quest_type(string type);

/* 可用任务展示的阅读摘要记账（all/类型两分支共用同一结构，避免重复构造 mapping）。 */
private void record_available_quests_summary(object me, string filter, string output)
{
    READ_SUMMARY_D->query_named_summary(me, "quests/" + filter, ([
        "type": "available_quests_summary",
        "filter": filter,
        "render_length": strlen(output),
    ]));
}

private int is_valid_quest_type(string type)
{
    return member_array(type, ({
        QUEST_TYPE_KILL,
        QUEST_TYPE_COLLECT,
        QUEST_TYPE_TALK,
        QUEST_TYPE_EXPLORE,
        QUEST_TYPE_ESCORT,
        QUEST_TYPE_DELIVERY,
        QUEST_TYPE_PUZZLE
    })) != -1;
}

int main(object me, string arg)
{
    string output;
    string type;

    if (!me)
        return 0;

    if (!arg || arg == "") {
        output = QUEST_D->render_available_quests_text(0, me);
        record_available_quests_summary(me, "all", output);
        msg_send(me, output);
        return 1;
    }

    if (strsrch(arg, " ") != -1) {
        msg_send(me, "用法：quests [任务类型]\n");
        return 1;
    }

    type = lower_case(arg);
    if (!is_valid_quest_type(type)) {
        msg_send(me, "有效类型：kill、collect、talk、explore、escort、delivery、puzzle\n");
        return 1;
    }

    output = QUEST_D->render_available_quests_text(type, me);
    record_available_quests_summary(me, type, output);
    msg_send(me, output);
    return 1;
}

int help(object me)
{
    msg_send(me, @HELP
命令：quests（任务列表）

查看当前系统中可用的任务。

用法：
  quests              显示所有可用任务
  quests kill         显示杀怪任务
  quests collect      显示收集任务
  quests talk         显示对话任务
  quests explore      显示探索任务
  quests escort       显示护送任务
  quests delivery     显示送信/递送任务
  quests puzzle       显示解密任务

相关命令：
  quest   - 任务管理

HELP
    );
    return 1;
}
