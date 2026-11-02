/**
 * @file    ask.c
 * @brief   询问命令 - 公共 ask 动词入口



 *
 * @par Description:
 * ask 的公共动词入口，负责参数解析、目标校验与 `do_ask` hook 分发。
 * 任务、剧情或对象专属问答真逻辑仍归目标对象或对应 service 持有。
 *
 * @par Usage:
 *   ask <目标> about <话题>  - 向目标询问某话题
 *
 */

#pragma strict_types

#include <system/ansi.h>
#include <system/services.h>
#include <game/command.h>
#include <framework/message_facade.h>
inherit "/lib/framework/command";
inherit "/lib/std/command_utils";

private object resolve_ask_target(object me, string target_name);
private int validate_target(object me, object target);
private mixed dispatch_target_ask_hook(object target, string raw_arg, string npc_name, string topic);
private void send_player_message(object me, object target, string topic);


/**
 * @brief 解析 ask 目标
 * @param me 玩家对象
 * @param target_name 目标名称
 * @return 同房可交互角色对象，未找到返回0
 */
private object resolve_ask_target(object me, string target_name)
{
    object env;

    if (!objectp(me) || !stringp(target_name) || target_name == "")
        return 0;

    env = environment(me);
    if (!objectp(env))
        return 0;

    return find_living_in_room(env, target_name, me);
}

/**
 * @brief 验证目标是否有效
 * @param me 玩家对象
 * @param target 目标对象
 * @return 1=有效, 0=无效
 */
private int validate_target(object me, object target)
{
    if (!target) {
        msg_send(me, "这里没有该人物。\n");
        return 0;
    }

    if (target == me) {
        msg_send(me, "你不能问自己。\n");
        return 0;
    }

    return 1;
}

private mixed dispatch_target_ask_hook(object target, string raw_arg, string npc_name, string topic)
{
    string *candidates;
    string candidate;
    string target_name;
    mixed hook_result;
    mixed err;

    if (!objectp(target) || !function_exists("do_ask", target))
        return 0;

    target_name = get_object_display_name(target, "");
    /*
     * ask 作为公共动词入口保留在命令层，对象只通过 do_ask 定制响应。
     * 候选参数优先传纯话题，避免现代 NPC hook 把整条原始输入当作未知话题接管。
     * 旧式 hook 仍可通过后续候选参数兼容。
     */
    candidates = ({
        topic,
        raw_arg,
        npc_name,
        target_name,
        npc_name + " " + topic,
        target_name + " " + topic
    });

    foreach (candidate in candidates) {
        if (!stringp(candidate) || trim(candidate) == "")
            continue;

        err = catch(hook_result = target->do_ask(trim(candidate)));
        if (!err && hook_result) {
            if (stringp(hook_result) && hook_result != "")
                return hook_result;
            return topic;
        }
    }

    return 0;
}

/**
 * @brief 发送玩家间消息
 * @param me 玩家对象
 * @param target 目标玩家
 * @param topic 话题
 */
private void send_player_message(object me, object target, string topic)
{
    msg_send(target, get_object_display_name(me, "有人") + "向你询问关于「" + topic + "」的事情。\n");
}

/**
 * @brief 询问命令主函数
 * @param me 玩家对象
 * @param arg 参数
 * @return 1=成功, 0=失败
 *
 * 使用 Guard Clauses 与查找表降低命令复杂度。
 */
int main(object me, string arg)
{
    mapping parsed;
    object target;
    mixed recorded_topic;
    int updated;
    string *quest_ids;
    string target_name, topic;

    // Guard Clauses
    if (!me) return 0;

    if (!arg || arg == "") {
        msg_send(me, "你想问什么？格式：ask <NPC> about <话题>\n");
        return 1;
    }

    parsed = parse_command_args(arg, "%s about %s");
    if (!parsed["valid"]) {
        msg_send(me, "格式错误。正确格式：ask <NPC> about <话题>\n");
        return 1;
    }

    target_name = parsed["args"][0];
    topic = parsed["args"][1];

    // 只命中同房可交互角色，不让同名非角色物件抢占 ask 目标。
    target = resolve_ask_target(me, target_name);
    if (!objectp(target))
        target = QUEST_ACTOR_D->resolve_actor_target(me, environment(me), target_name);
    if (objectp(target) && environment(target) != environment(me))
        target = 0;
    if (!validate_target(me, target))
        return 1;

    // NPC回应
    if (!userp(target))
    {
        /* 目标 hook 优先，只有 hook 未接管时才落到命令层默认话题表。 */
        me->set_temp("dialog/skip_provider_progress", 1);
        recorded_topic = dispatch_target_ask_hook(target, arg, target_name, topic);
        me->delete_temp("dialog/skip_provider_progress");
        if (recorded_topic) {
            quest_ids = QUEST_D->query_matching_dialog_quests(me, target, "ask",
                stringp(recorded_topic) && recorded_topic != "" ? recorded_topic : topic);
            updated = QUEST_D->record_dialog_progress(me, target, "ask",
                stringp(recorded_topic) && recorded_topic != "" ? recorded_topic : topic);
            if (updated)
                NPC_VIEW_D->present_specific_quest_progress(me, target, quest_ids);
            return 1;
        }

        me->set_temp("dialog/skip_provider_progress", 1);
        if (NPC_VIEW_D->present_profile_interaction(me, target, "ask", topic)) {
            me->delete_temp("dialog/skip_provider_progress");
            quest_ids = QUEST_D->query_matching_dialog_quests(me, target, "ask", topic);
            updated = QUEST_D->record_dialog_progress(me, target, "ask", topic);
            if (updated)
                NPC_VIEW_D->present_specific_quest_progress(me, target, quest_ids);
            return 1;
        }
        me->delete_temp("dialog/skip_provider_progress");

        msg_send(me, "对方似乎没有正面回答这个问题。\n");
    }
    else
    {
        // 目标是玩家
        send_player_message(me, target, topic);
    }

    return 1;
}

int help(object me)
{
msg_send(me, @HELP
命令：ask（询问）

向同房目标询问特定话题。

用法：
  ask <目标> about <话题>  - 向目标询问某话题

示例：
  ask guard about help     - 向守卫询问帮助
  ask guard about newbie   - 向守卫询问新手引导
  ask guard about quest    - 向守卫询问任务

注意：
  - 不同的NPC知道不同的信息
  - 某些话题可能需要完成前置任务才能解锁

相关命令：
  talk   - 与目标直接对话
  quest  - 查看与提交任务

HELP
    );
    return 1;
}
