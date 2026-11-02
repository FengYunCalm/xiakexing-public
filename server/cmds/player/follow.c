/**
 * @file    follow.c
 * @brief   跟随命令 - 跟随指定玩家移动



 *
 * @par Description:
 * 跟随指定玩家移动，停止跟随，显示跟随状态，发送XK协议消息。
 *
 * @par Usage:
 *   follow              - 显示跟随状态
 *   follow <玩家>       - 开始跟随玩家
 *   follow none         - 停止跟随
 *
 */

#pragma strict_types

#include <system/ansi.h>
#include <game/command.h>
#include <framework/message_facade.h>

inherit "/lib/std/command_utils";
inherit "/lib/framework/network";

private object resolve_follow_target(object me, string target_name)
{
    object env;
    object target;

    if (!objectp(me) || !stringp(target_name) || target_name == "")
        return 0;

    env = environment(me);
    if (!objectp(env))
        return 0;

    target = find_living_in_room(env, target_name, me);
    if (!objectp(target) || !userp(target))
        return 0;

    return target;
}

private string format_follow_box_row(string content)
{
    return sprintf(HIC "║" NOR " %-38s" HIC "║" NOR "\n", content || "");
}

private string format_follower_summary(mapping snapshot)
{
    string *follower_ids;

    follower_ids = mapp(snapshot) && arrayp(snapshot["follower_ids"])
        ? snapshot["follower_ids"]
        : ({ });
    if (!sizeof(follower_ids))
        return "";
    if (sizeof(follower_ids) <= 2)
        return implode(follower_ids, "、");
    return sprintf("%d 人", sizeof(follower_ids));
}

/**
 * 显示跟随状态
 */
private void emit_follow_status_ui(object me, mapping response_context)
{
    object leader;
    mapping snapshot;
    string follower_summary;
    string result;

    leader = me->query_leader();
    snapshot = me->query_follow_snapshot();
    follower_summary = format_follower_summary(snapshot);

    if (!objectp(leader)) {
        result = HIC "╔════════════════════════════════════════╗" NOR "\n";
        result += format_follow_box_row(YEL "你目前没有跟随任何人" NOR);
        if (follower_summary != "")
            result += format_follow_box_row(HIY "正在跟随你的玩家：" NOR + follower_summary);
        result += HIC "╚════════════════════════════════════════╝" NOR "\n";
        result += HIC "使用 follow <玩家> 来跟随他人。" NOR "\n";
        msg_send(me, result, response_context);

        send_xk_quick_cmds(me,
            "跟随|开始跟随:follow|组队:team",
            1, response_context);
        return;
    }

    result = HIC "╔════════════════════════════════════════╗" NOR "\n";
    result += format_follow_box_row(HIG "你正在跟随 " NOR + get_object_display_name(leader, "对方"));
    if (follower_summary != "")
        result += format_follow_box_row(HIY "正在跟随你的玩家：" NOR + follower_summary);
    result += HIC "╚════════════════════════════════════════╝" NOR "\n";
    msg_send(me, result, response_context);

    send_xk_quick_cmds(me,
        "跟随|停止跟随:follow none|查看状态:follow",
        1, response_context);
}

/**
 * 命令主入口
 */
int main(object me, string arg)
{
    object target;
    mapping response_context;

    if (!me) return 0;

    response_context = CLIENT_SYNC_D->capture_command_response_context(me);
    if (!arg || arg == "") {
        emit_follow_status_ui(me, response_context);
        return 1;
    }

    if (arg == "none" || arg == "stop") {
        me->set_leader(0);
        msg_send(me, HIG "你停止跟随任何人。" NOR "\n", response_context);
        return 1;
    }

    /* 跟随只解析同房玩家，不跨房间或全服搜索目标。 */
    target = resolve_follow_target(me, arg);

    if (!target) {
        msg_send(me, HIR "没有找到 " + arg + "。" NOR "\n", response_context);
        return 1;
    }

    if (target == me) {
        msg_send(me, HIR "你不能跟随自己。" NOR "\n", response_context);
        return 1;
    }

    if (me->query_leader() == target) {
        msg_send(me, HIR "你已经在跟随对方了。" NOR "\n", response_context);
        return 1;
    }

    if (target->query_leader() == me) {
        msg_send(me, HIR "对方正在跟随你，不能互相跟随。" NOR "\n", response_context);
        return 1;
    }

    /* 设置跟随 */
    me->set_leader(target);

    msg_send(me, HIG "你开始跟随 " + get_object_display_name(target, "对方") + "。" NOR "\n",
        response_context);
    /* 跨玩家通知不继承目标玩家的最新命令上下文。 */
    msg_send(target, HIY + get_object_display_name(me, "有人") + " 开始跟随你。" NOR "\n", 0);

    send_xk_quick_cmds(me,
        "跟随|停止跟随:follow none|查看状态:follow",
        1, response_context);

    return 1;
}

/**
 * 帮助信息
 */
int help(object me)
{
    msg_send(me, @HELP
命令：follow（跟随）

跟随指定玩家移动。

用法：
  follow              显示跟随状态
  follow <玩家>       开始跟随玩家
  follow none         停止跟随

说明：
  - 跟随后，对方移动时你会自动跟随
  - 自动跟随会遵守当前出口与离场规则
  - 若你无法继续离开原房间，或正陷于战斗，跟随会中断
  - 跟随不会绕过副本、房屋等需要单独准入的实例入口
  - 可以随时停止跟随

示例：
  follow xiaoming     跟随小明
  follow none         停止跟随

相关命令：
  team    - 组队
  go      - 移动

HELP
    );
    return 1;
}
