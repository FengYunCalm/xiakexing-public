/**
 * @file    talk.c
 * @brief   对话命令 - 公共 talk 动词入口



 *
 * @par Description:
 * talk 的公共动词入口，负责目标解析与 `do_talk -> do_ask` 的兼容回退分发。
 * 对话内容真逻辑仍由目标对象或对应 service 持有。
 */

#pragma strict_types

#include <system/services.h>
#include <framework/message_facade.h>

inherit "/lib/framework/command";
inherit "/lib/std/command_utils";

int help(object me);

private string query_talk_fallback_subject(object target, string raw_arg)
{
    string target_id;
    string target_name;

    if (!objectp(target))
        return "";

    if (stringp(raw_arg) && raw_arg != "" && function_exists("id", target) && target->id(raw_arg))
        return raw_arg;

    target_id = get_object_display_id(target, "");
    if (stringp(target_id) && target_id != "")
        return target_id;

    target_name = get_object_display_name(target, "");
    if (stringp(target_name) && target_name != "")
        return target_name;

    return stringp(raw_arg) ? raw_arg : "";
}

int main(object me, string arg)
{
    object target;
    object env;
    mixed hook_result;
    mixed err;
    int updated;
    string *quest_ids;
    string fallback_subject;

    if (!objectp(me))
        return 0;

    env = environment(me);
    target = resolve_talk_target(me, arg);
    if (!objectp(target))
        target = QUEST_ACTOR_D->resolve_actor_target(me, env, arg);
    if (objectp(target) && environment(target) != env)
        target = 0;
    if (!objectp(target)) {
        msg_send(me, "这里没有这个对象可以交谈。\n");
        return 1;
    }

    if (function_exists("do_talk", target)) {
        me->set_temp("dialog/skip_provider_progress", 1);
        err = catch(hook_result = target->do_talk(arg));
        me->delete_temp("dialog/skip_provider_progress");
        if (!err && hook_result) {
            quest_ids = QUEST_D->query_matching_dialog_quests(me, target, "talk", arg);
            updated = QUEST_D->record_dialog_progress(me, target, "talk", arg);
            if (updated)
                NPC_VIEW_D->present_specific_quest_progress(me, target, quest_ids);
            return 1;
        }
    }

    if (function_exists("do_ask", target)) {
        fallback_subject = query_talk_fallback_subject(target, arg);
        me->set_temp("dialog/skip_provider_progress", 1);
        err = catch(hook_result = target->do_ask(fallback_subject));
        me->delete_temp("dialog/skip_provider_progress");
        if (!err && hook_result) {
            quest_ids = QUEST_D->query_matching_dialog_quests(me, target, "ask", fallback_subject);
            updated = QUEST_D->record_dialog_progress(me, target, "ask", fallback_subject);
            if (updated)
                NPC_VIEW_D->present_specific_quest_progress(me, target, quest_ids);
            return 1;
        }
    }

    me->set_temp("dialog/skip_provider_progress", 1);
    if (NPC_VIEW_D->present_profile_interaction(me, target, "talk", arg)) {
        me->delete_temp("dialog/skip_provider_progress");
        fallback_subject = query_talk_fallback_subject(target, arg);
        quest_ids = QUEST_D->query_matching_dialog_quests(me, target, "talk",
            stringp(fallback_subject) && fallback_subject != "" ? fallback_subject : arg);
        updated = QUEST_D->record_dialog_progress(me, target, "talk",
            stringp(fallback_subject) && fallback_subject != "" ? fallback_subject : arg);
        if (updated)
            NPC_VIEW_D->present_specific_quest_progress(me, target, quest_ids);
        return 1;
    }
    me->delete_temp("dialog/skip_provider_progress");

    msg_send(me, "对方没有回应你。\n");
    return 1;
}

int help(object me)
{
    msg_send(me, @HELP
命令：talk（对话）

向当前场景中的可交互对象发起直接对话。

用法：
  talk <目标>      与目标直接对话

说明：
  - 优先触发目标的 do_talk
  - 若目标未实现 do_talk，则仅按目标自身标识回退到 do_ask
  - 目标解析不只限于 NPC，也可以是房间中的其他可交互对象

相关命令：
  ask    - 按话题询问
  quest  - 查看任务入口

HELP
    );
    return 1;
}
