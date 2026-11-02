/**
 * @file    quest.c
 * @brief   任务命令 - 公共 quest 文本入口



 *
 * @par Description:
 * 统一任务文本入口，负责参数解析、目标 `do_quest` hook 分发与任务服务调用。
 * 任务状态、奖励与进度真逻辑统一归 `QUEST_D` 持有。
 */

#pragma strict_types

#include <system/ansi.h>
#include <system/services.h>
#include <game/command.h>
#include <game/quest.h>
#include <framework/message_facade.h>

inherit "/lib/framework/command";
inherit "/lib/std/command_utils";

private mapping parse_quest_command(string arg);
private void emit_quest_dialog(object me, string title, string content,
    string dialog_id, mapping response_context);
private string query_provider_dialog_title(string provider_id);
private mapping *build_provider_progress_actions(object me, string quest_id, string provider_id);
private void emit_provider_quest_dialog(object me, string quest_id,
    string content, mapping response_context);
private string render_quest_info_dialog_copy(object me, string quest_id);
private int show_active_quests(object me, mapping response_context);
private int show_completed_quests(object me, mapping response_context);
private int show_abandoned_quests(object me, mapping response_context);
private int show_quest_details(object me, string quest_id, mapping response_context);
private int accept_quest_action(object me, string quest_id, mapping response_context);
private int complete_quest_action(object me, string quest_id, mapping response_context);
private int abandon_quest_action(object me, string quest_id, mapping response_context);
private int recover_quest_action(object me, string quest_id, mapping response_context);
private int record_quest_action_event(object me, string arg, mapping response_context);
private int dispatch_target_quest_hook(object me, string arg);

private mapping parse_quest_command(string arg)
{
    mapping parsed;

    if (!stringp(arg) || arg == "")
        return ([ "valid": 1, "action": "list", "target": 0, "error": "" ]);

    if (strsrch(arg, " ") == -1)
        return ([ "valid": 1, "action": lower_case(arg), "target": 0, "error": "" ]);

    parsed = parse_command_args(arg, "%s %s");
    if (!parsed["valid"]) {
        return ([
            "valid": 0,
            "action": "",
            "target": 0,
            "error": "用法：quest [list|active|completed|abandoned|info|accept|complete|abandon|recover] [任务ID]\n"
        ]);
    }

    return ([
        "valid": 1,
        "action": lower_case(parsed["args"][0]),
        "target": parsed["args"][1],
        "error": ""
    ]);
}

private int dispatch_target_quest_hook(object me, string arg)
{
    object target;
    object env;
    mixed hook_result;
    mixed err;

    if (!objectp(me) || !stringp(arg) || arg == "")
        return 0;

    env = environment(me);
    if (!objectp(env))
        return 0;

    /*
     * quest <npc> 只应命中同房可交互角色，不让同名非角色物件抢占目标。
     * 所有同房角色统一导入 base do_quest 应答（按钮显隐
     * 仍由 query_quest_supported 投影门控，文本应答归 do_quest）。
     */
    target = find_living_in_room(env, arg, me);
    if (!objectp(target))
        target = QUEST_ACTOR_D->resolve_actor_target(me, env, arg);
    if (objectp(target) && environment(target) != env)
        target = 0;
    /* 非 provider NPC（query_quest_supported 假）也交 do_quest
     * 统一应答——否则穿透 default 分支把 npc_id 当任务 id 报「没有找到该
     * 任务」；do_quest→query_provider_click_hint 会给出无任务提示。 */
    if (!objectp(target) || !function_exists("do_quest", target))
        return 0;

    err = catch(hook_result = target->do_quest(arg));
    if (!err && hook_result)
        return 1;

    return 0;
}

private void emit_quest_dialog(object me, string title, string content,
    string dialog_id, mapping response_context)
{
    string resolved_title;
    string resolved_dialog_id;

    if (!objectp(me) || !stringp(content) || content == "")
        return;

    resolved_title = stringp(title) ? title : "任务";
    resolved_dialog_id = stringp(dialog_id) ? dialog_id : "quest";

    xk_send_dialog(me,
        resolved_title,
        content,
        DIALOG_PROJECTION_D->build_dialog_extra("quest",
            resolved_dialog_id, ([])), response_context);
    xk_emit_message_event(me, content, ([
        "channel": "dialog",
        "intent": "replace",
        "priority": "normal",
        "reliability": "important",
        "display_mode": "dialog",
        "response_context": response_context,
        "collapse_key": "quest/" + resolved_dialog_id,
        "payload": ([
            "kind": "quest_dialog",
            "title": resolved_title,
            "dialog_id": resolved_dialog_id,
        ]),
    ]));
}

private string query_provider_dialog_title(string provider_id)
{
    if (!stringp(provider_id) || provider_id == "")
        return "任务";

    switch (provider_id) {
    case "gate_guard":
        return "丁守门";
    case "innkeeper":
        return "鲍景和";
    case "runner":
    case "porter":
        return "阿满";
    case "village_elder":
        return "陆村长";
    case "martial_coach":
        return "石教头";
    case "tailor":
        return "苏裁缝";
    case "hunter":
        return "陈猎户";
    case "blacksmith":
        return "张铁匠";
    case "pharmacist":
        return "孙大夫";
    default:
        return "任务";
    }
}

private mapping *build_provider_progress_actions(object me, string quest_id, string provider_id)
{
    /* 实现已上提 QUEST_D->query_quest_progress_actions 单一权威
     * （交互面板管线同源消费），本函数保留为调用点薄委托。 */
    return QUEST_D->query_quest_progress_actions(me, quest_id, provider_id);
}

private mapping build_provider_progress_action_group(mapping *actions)
{
    if (!arrayp(actions) || sizeof(actions) == 0)
        return 0;
    return ([
        "id": "quest",
        "title": "任务",
        "surface": "quest",
        "actions": actions,
    ]);
}

private void emit_provider_quest_dialog(object me, string quest_id,
    string content, mapping response_context)
{
    mapping quest_def;
    mapping extra;
    mapping *actions;
    string provider_id;

    if (!objectp(me) || !stringp(content) || content == "")
        return;
    quest_def = QUEST_D->query_visible_quest_definition(me, quest_id);
    provider_id = mapp(quest_def) && stringp(quest_def["provider"])
        ? quest_def["provider"]
        : "";
    if (!stringp(provider_id) || provider_id == "") {
        emit_quest_dialog(me, "任务", content, stringp(quest_id) && quest_id != ""
            ? "quest:" + quest_id
            : "quest_action", response_context);
        return;
    }

    extra = DIALOG_PROJECTION_D->build_provider_dialog_extra(
        "npc_provider_progress",
        provider_id + "_progress",
        provider_id,
        "progress",
        ([ "speaker_id": provider_id ])
    );
    actions = build_provider_progress_actions(me, quest_id, provider_id);
    if (sizeof(actions) > 0)
        extra["action_groups"] = ({ build_provider_progress_action_group(actions) });

    xk_send_dialog(me, query_provider_dialog_title(provider_id), content,
        extra, response_context);
    xk_emit_message_event(me, content, ([
        "channel": "dialog",
        "intent": "replace",
        "priority": "normal",
        "reliability": "important",
        "display_mode": "dialog",
        "response_context": response_context,
        "collapse_key": "quest/" + quest_id,
        "payload": ([
            "kind": "quest_dialog",
            "quest_id": quest_id,
            "provider_id": provider_id,
            "title": query_provider_dialog_title(provider_id),
            "dialog_id": provider_id + "_progress",
        ]),
    ]));
}

private string render_quest_info_dialog_copy(object me, string quest_id)
{
    string dialog_text;

    dialog_text = QUEST_D->render_quest_info_dialog_text(me, quest_id);
    if (!stringp(dialog_text) || dialog_text == "")
        return QUEST_D->render_quest_info_text(me, quest_id);
    return dialog_text;
}

private int show_active_quests(object me, mapping response_context)
{
    string text;
    string dialog_text;
    mapping summary;

    summary = READ_SUMMARY_D->query_quest_summary(me, "active");

    if (is_xk_client(me)) {
        QUEST_PROTOCOL->xk_send_quest_list(me, response_context);
        return 1;
    }

    text = QUEST_D->render_player_quests_text(me, "active");
    if (mapp(summary) && stringp(summary["async_state"]) && summary["async_state"] != "ready")
        text += sprintf("读侧摘要后台重算：%s\n", summary["async_state"]);
    dialog_text = QUEST_D->render_player_quests_dialog_text(me, "active");
    msg_send(me, text, response_context);
    emit_quest_dialog(me, "进行中的任务", dialog_text, "quest_active",
        response_context);
    return 1;
}

private int show_completed_quests(object me, mapping response_context)
{
    string text;
    string dialog_text;
    mapping summary;

    summary = READ_SUMMARY_D->query_quest_summary(me, "completed");

    text = QUEST_D->render_player_quests_text(me, "completed");
    if (mapp(summary) && stringp(summary["async_state"]) && summary["async_state"] != "ready")
        text += sprintf("读侧摘要后台重算：%s\n", summary["async_state"]);
    dialog_text = QUEST_D->render_player_quests_dialog_text(me, "completed");
    if (!is_xk_client(me))
        msg_send(me, text, response_context);
    emit_quest_dialog(me, "已完成任务", dialog_text, "quest_completed",
        response_context);
    return 1;
}

private int show_abandoned_quests(object me, mapping response_context)
{
    string text;
    string dialog_text;
    mapping summary;

    summary = READ_SUMMARY_D->query_quest_summary(me, "abandoned");

    text = QUEST_D->render_player_quests_text(me, "abandoned");
    if (mapp(summary) && stringp(summary["async_state"]) && summary["async_state"] != "ready")
        text += sprintf("读侧摘要后台重算：%s\n", summary["async_state"]);
    dialog_text = QUEST_D->render_player_quests_dialog_text(me, "abandoned");
    if (!is_xk_client(me))
        msg_send(me, text, response_context);
    emit_quest_dialog(me, "失败/放弃任务", dialog_text, "quest_abandoned",
        response_context);
    return 1;
}

private int show_quest_details(object me, string quest_id, mapping response_context)
{
    string text;
    string dialog_text;
    mapping quest_def;

    if (!stringp(quest_id) || quest_id == "") {
        msg_send(me, "用法：quest info <任务ID>\n", response_context);
        return 1;
    }

    /* 未知/无效任务 id 存在性门控——仅回一条 msg_send 文本，
     * 绝不 emit_*_dialog 双发（历史行为：回退链恒产出非空“没有找到该任务。”
     * 导致弹窗+消息事件双出）。合法任务详情不受影响。 */
    quest_def = QUEST_D->query_visible_quest_definition(me, quest_id);
    if (!mapp(quest_def)) {
        msg_send(me, "没有找到该任务。\n", response_context);
        return 1;
    }

    /* xk 客户端详情呈现权归客户端结构化渲染（QUEST 包已带
     * steps/summary 全量数据，dock 详情面板即据此绘制）。此处只回发结构化
     * 刷新包；再 emit 带标签全文会被 MSGE 合并分支拼接进已有结构化行，
     * 产出双"描述"（summary 与 description 语义分叉各占一行）。
     * 注：能走到这里的 id 已过 query_visible_quest_definition 门控（runtime
     * 不可用时该门直接拦下），故 xk 客户端在此恒早退；内层判断保留为
     * 防御性双检。legacy 文本客户端保持原路径不变。 */
    if (is_xk_client(me)) {
        QUEST_PROTOCOL->xk_send_quest_update(me, quest_id, "upsert", -1, "",
            response_context);
        return 1;
    }

    text = QUEST_D->render_quest_info_text(me, quest_id);
    dialog_text = render_quest_info_dialog_copy(me, quest_id);
    msg_send(me, text, response_context);
    if (stringp(quest_def["provider"]) && quest_def["provider"] != "")
        emit_provider_quest_dialog(me, quest_id, dialog_text, response_context);
    else
        emit_quest_dialog(me, "任务详情", dialog_text, "quest_info:" + quest_id,
            response_context);
    return 1;
}

private int accept_quest_action(object me, string quest_id, mapping response_context)
{
    if (!stringp(quest_id) || quest_id == "") {
        msg_send(me, "用法：quest accept <任务ID>\n", response_context);
        return 1;
    }

    /* 接取反馈直达主文本区（#9）：不再下发 provider dialog 弹窗，避免接取后
     * 弹出任务面板；任务状态仍由 QUEST_D 事件包同步，面板可随时手动打开。
     * 反馈升级为完整任务链文本块（目标逐条 desc(p/t) + 新规格奖励行）。 */
    if (QUEST_D->accept_quest(me, quest_id)) {
        string chain_text;

        /* 接取反馈只发任务块本身：分隔线外不得出现任何任务相关文字。 */
        chain_text = QUEST_D->render_quest_accept_text(me, quest_id);
        if (chain_text != "")
            msg_send(me, chain_text, response_context);
        return 1;
    }

    msg_send(me, "接受任务失败。请确认任务存在且尚未接取。\n",
        response_context);
    return 1;
}

private int complete_quest_action(object me, string quest_id, mapping response_context)
{
    if (!stringp(quest_id) || quest_id == "") {
        msg_send(me, "用法：quest complete <任务ID>\n", response_context);
        return 1;
    }

    if (QUEST_D->turn_in_quest(me, quest_id)) {
        emit_provider_quest_dialog(me, quest_id,
            render_quest_info_dialog_copy(me, quest_id), response_context);
        return 1;
    }

    if (!is_xk_client(me))
        msg_send(me, "完成任务失败。请确认任务已接取。\n", response_context);
    emit_provider_quest_dialog(me, quest_id,
        "完成任务失败。请确认任务已接取。\n", response_context);
    return 1;
}

private int abandon_quest_action(object me, string quest_id, mapping response_context)
{
    if (!stringp(quest_id) || quest_id == "") {
        msg_send(me, "用法：quest abandon <任务ID>\n", response_context);
        return 1;
    }

    if (QUEST_D->abandon_quest(me, quest_id)) {
        if (!is_xk_client(me))
            msg_send(me, HIG "已放弃任务。" NOR "\n", response_context);
        emit_provider_quest_dialog(me, quest_id,
            "已放弃任务。\n\n" + render_quest_info_dialog_copy(me, quest_id),
            response_context);
        return 1;
    }

    if (!is_xk_client(me))
        msg_send(me, "放弃任务失败。请确认任务当前处于进行中。\n",
            response_context);
    emit_provider_quest_dialog(me, quest_id,
        "放弃任务失败。请确认任务当前处于进行中。\n", response_context);
    return 1;
}

private int recover_quest_action(object me, string quest_id, mapping response_context)
{
    if (!stringp(quest_id) || quest_id == "") {
        msg_send(me, "用法：quest recover <任务ID>\n", response_context);
        return 1;
    }

    if (QUEST_D->recover_quest(me, quest_id)) {
        if (!is_xk_client(me))
            msg_send(me, HIG "已恢复任务进度。" NOR "\n", response_context);
        emit_provider_quest_dialog(me, quest_id,
            "已恢复任务进度。\n\n" + render_quest_info_dialog_copy(me, quest_id),
            response_context);
        return 1;
    }

    if (!is_xk_client(me))
        msg_send(me, "恢复任务失败。请确认任务当前处于失败/放弃状态并且可恢复。\n",
            response_context);
    emit_provider_quest_dialog(me, quest_id,
        "恢复任务失败。请确认任务当前处于失败/放弃状态并且可恢复。\n",
        response_context);
    return 1;
}

private mixed resolve_quest_action_target(object me, string target_name)
{
    object env;
    object target;

    if (!objectp(me) || !stringp(target_name) || target_name == "")
        return 0;

    env = environment(me);
    if (!objectp(env))
        return target_name;

    target = find_living_in_room(env, target_name, me);
    if (!objectp(target))
        target = present(target_name, me);
    if (!objectp(target))
        target = present(target_name, env);
    if (!objectp(target))
        target = QUEST_ACTOR_D->resolve_actor_target(me, env, target_name);
    if (objectp(target) && environment(target) != env && environment(target) != me)
        target = 0;

    if (objectp(target))
        return target;
    return target_name;
}

private int record_quest_action_event(object me, string arg, mapping response_context)
{
    string action_id;
    string target_name;
    mixed target;

    if (!objectp(me))
        return 0;
    if (!stringp(arg) || arg == "") {
        msg_send(me, "用法：quest action <动作> [目标]\n", response_context);
        return 1;
    }

    if (sscanf(arg, "%s %s", action_id, target_name) != 2) {
        action_id = arg;
        target = 0;
    } else {
        target = resolve_quest_action_target(me, target_name);
    }

    if (QUEST_D->attempt_scene_action(me, action_id, target))
        return 1;

    msg_error(me, "当前没有任务需要这样做。\n",
        "quest", "scene_action_unavailable", action_id, 0,
        response_context);
    return 1;
}

int main(object me, string arg)
{
    mapping parsed;
    mapping response_context;
    string action;
    string target;

    if (!me)
        return 0;

    response_context = CLIENT_SYNC_D->capture_command_response_context(me);

    if (!arg || arg == "")
        return show_active_quests(me, response_context);

    if (dispatch_target_quest_hook(me, arg))
        return 1;

    parsed = parse_quest_command(arg);
    if (!parsed["valid"]) {
        msg_send(me, parsed["error"], response_context);
        return 1;
    }

    action = parsed["action"];
    target = parsed["target"];

    switch (action) {
    case "list":
    case "active":
        return show_active_quests(me, response_context);

    case "completed":
        return show_completed_quests(me, response_context);

    case "abandoned":
        return show_abandoned_quests(me, response_context);

    case "info":
        return show_quest_details(me, target, response_context);

    case "accept":
        return accept_quest_action(me, target, response_context);

    case "complete":
        return complete_quest_action(me, target, response_context);

    case "abandon":
        return abandon_quest_action(me, target, response_context);

    case "recover":
        return recover_quest_action(me, target, response_context);

    case "action":
        return record_quest_action_event(me, target, response_context);

    default:
        return show_quest_details(me, arg, response_context);
    }
}

int help(object me)
{
    msg_send(me, @HELP
命令：quest（任务管理）

查看、接取、完成、放弃或恢复任务。

用法：
  quest                           显示进行中的任务
  quest list                      显示进行中的任务
  quest completed                 显示已完成任务
  quest abandoned                 显示失败/放弃任务
  quest info <任务ID>             查看任务详情
  quest accept <任务ID>           接受任务
  quest complete <任务ID>         完成并提交任务
  quest abandon <任务ID>          放弃任务
  quest recover <任务ID>          恢复失败/放弃任务的当前进度
  quest action <动作> [目标]       执行当前任务的场景互动

示例：
  quest <任务ID>
  quest accept <任务ID>
  quest complete <任务ID>
  quest recover <任务ID>

说明：
  - 直接输入 quest 默认显示进行中的任务
  - quest action 只适用于当前任务明确提供的场景互动
  - 学习、装备、制作、治疗、修理和突破会在真实结算成功后自动推进
  - 某些 NPC 会通过 do_quest 提供内容化任务入口

相关命令：
  ask     - 询问任务线索
  talk    - 与任务 NPC 对话
  quests  - 查看所有可用任务

HELP
    );
    return 1;
}
