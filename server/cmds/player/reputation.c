/**
 * @file    reputation.c
 * @brief   声望与称号查看命令
 */

#pragma strict_types

#include <system/ansi.h>
#include <system/services.h>

inherit "/lib/framework/network";
inherit "/lib/std/command_utils";

int main(object me, string arg);
int help(object me);

private string format_row(string content)
{
    return sprintf(HIC "║" NOR " %-38s" HIC "║" NOR "\n", content || "");
}

private int query_scope_value(mapping snapshot, string scope)
{
    return mapp(snapshot) && intp(snapshot[scope]) ? snapshot[scope] : 0;
}

private string query_alignment_label(int alignment)
{
    if (alignment >= 200)
        return "侠义昭彰";
    if (alignment >= 50)
        return "行止向善";
    if (alignment > -50)
        return "中立平常";
    if (alignment > -200)
        return "行事乖张";
    return "邪气深重";
}

private string build_reputation_status(object me)
{
    mapping snapshot;
    string output;
    string title;
    int fame;
    int credit;
    int alignment;

    snapshot = REPUTATION_D->query_reputation_snapshot(me);
    title = me->query("title") || "";
    fame = query_scope_value(snapshot, "fame");
    credit = query_scope_value(snapshot, "credit");
    alignment = query_scope_value(snapshot, "alignment");
    READ_SUMMARY_D->query_named_summary(me, "reputation/status", ([
        "type": "reputation_status_summary",
        "title": title,
        "fame": fame,
        "credit": credit,
        "alignment": alignment,
    ]));

    output = HIC "╔════════════════ 声望状态 ════════════════╗" NOR "\n";
    output += format_row(HIY "当前称号" NOR "：" + (title != "" ? title : "未获称号"));
    output += format_row(sprintf(HIG "江湖声望" NOR "：%d", fame));
    output += format_row(sprintf(HIW "江湖信誉" NOR "：%d", credit));
    output += format_row(sprintf(HIM "善恶倾向" NOR "：%d（%s）", alignment, query_alignment_label(alignment)));
    output += HIC "╚════════════════════════════════════════╝" NOR "\n";
    return output;
}

int main(object me, string arg)
{
    mapping response_context;

    if (!objectp(me))
        return 0;

    response_context = CLIENT_SYNC_D->capture_command_response_context(me);
    if (stringp(arg) && arg != "" && arg != "help")
        return help(me);

    msg_send(me, build_reputation_status(me), response_context);
    send_xk_quick_cmds(me,
        "声望|查看属性:score|查看成长:growth",
        1, response_context);
    return 1;
}

int help(object me)
{
    msg_send(me, @HELP
命令：reputation（声望）

用法：
  reputation      查看当前声望、信誉、善恶与称号

说明：
  - 江湖声望反映你在江湖中的名望
  - 江湖信誉反映你在组织与势力中的信用
  - 善恶倾向反映你当前的行事立场
  - 当前称号由奖励系统统一发放与更新
HELP
    );
    return 1;
}
