/**
 * @file    gather.c
 * @brief   采集命令 - 公共 gather 动词入口



 *
 * @par Description:
 * gather 的公共入口只负责房间目标解析、gather contract 提取与玩家兼容 seam 分发。
 * 采集结算真链继续留在 LIFE_SKILL_D / gather runtime，命令层不新增第二套规则。
 */

#pragma strict_types

#include <system/services.h>
#include <framework/message_facade.h>

inherit "/lib/framework/command";
inherit "/lib/std/command_utils";

int help(object me);
int main(object me, string arg);
private object resolve_gather_target(object me, string target_name);
private mapping query_gather_contract(object me, object target);
private string query_target_display_name(object target, string fallback);
private string query_skill_display_name(string skill_id);
private string query_invalid_contract_message(object target, string fallback);
private string query_failure_message(mapping result, object target, string fallback_name);
private string build_success_message(mapping result);

private object resolve_gather_target(object me, string target_name)
{
    object env;
    object target;

    if (!objectp(me) || !stringp(target_name) || target_name == "")
        return 0;

    env = environment(me);
    if (!objectp(env))
        return 0;

    target = find_living_in_room(env, target_name, me);
    if (objectp(target))
        return target;

    target = QUEST_ACTOR_D->resolve_actor_target(me, env, target_name);
    if (objectp(target) && environment(target) == env)
        return target;

    return present(target_name, env);
}

private mapping query_gather_contract(object me, object target)
{
    mapping contract;
    mixed err;

    if (!objectp(target))
        return 0;

    if (function_exists("query_gather_target", target)) {
        err = catch(contract = target->query_gather_target(me));
        if (!err && mapp(contract))
            return contract;
    }

    if (function_exists("query_gather_contract", target)) {
        err = catch(contract = target->query_gather_contract(me));
        if (!err && mapp(contract))
            return contract;
    }

    contract = target->query("gather_target");
    if (mapp(contract))
        return contract;

    contract = target->query("gather_contract");
    if (mapp(contract))
        return contract;

    return 0;
}

private string query_target_display_name(object target, string fallback)
{
    string name;

    if (!objectp(target))
        return fallback;

    name = target->query("name");
    if (stringp(name) && name != "")
        return name;

    name = get_object_id(target);
    if (stringp(name) && name != "")
        return name;

    return fallback;
}

private string query_skill_display_name(string skill_id)
{
    mapping skill_def;

    if (!stringp(skill_id) || skill_id == "")
        return "采集";

    skill_def = LIFE_SKILL_D->query_skill_definition(skill_id);
    if (mapp(skill_def) && stringp(skill_def["name"]) && skill_def["name"] != "")
        return skill_def["name"];

    return skill_id;
}

private string query_invalid_contract_message(object target, string fallback)
{
    return "这里的「" + query_target_display_name(target, fallback) + "」没有可用的采集契约。\n";
}

private string query_failure_message(mapping result, object target, string fallback_name)
{
    string error_code;
    string skill_name;
    string target_name;

    if (!mapp(result))
        return "采集失败。\n";

    error_code = result["error"];
    skill_name = query_skill_display_name(result["skill_id"]);
    target_name = query_target_display_name(target,
        stringp(result["target_name"]) && result["target_name"] != ""
            ? result["target_name"]
            : fallback_name);

    switch (error_code) {
    case "missing_gather_runtime":
        return "采集功能当前不可用。\n";

    case "invalid_gather_skill":
        return "这里的「" + target_name + "」没有有效的采集技能配置。\n";

    case "insufficient_skill_level":
        return "你的「" + skill_name + "」等级不足，无法采集「" + target_name + "」。\n";

    case "action_interval":
        return "你采集得太急，稍候再试。\n";

    case "action_cooldown":
        return "你暂时还不能再次采集「" + target_name + "」。\n";

    case "insufficient_resource":
        return "你的气力不足，暂时无法采集「" + target_name + "」。\n";

    case "invalid_target":
        return "这里的「" + target_name + "」没有有效的采集契约。\n";

    case "reward_apply_failed":
        return "采集「" + target_name + "」失败：奖励结算未完成。\n";

    default:
        return "采集「" + target_name + "」失败。\n";
    }
}

private string build_success_message(mapping result)
{
    string *lines;
    string skill_name;
    string target_name;
    string line;

    lines = ({ });
    skill_name = query_skill_display_name(result["skill_id"]);
    target_name = stringp(result["target_name"]) && result["target_name"] != ""
        ? result["target_name"]
        : "目标";

    lines += ({ "你采集了「" + target_name + "」。" });
    foreach (line in REWARD_FEEDBACK_D->build_reward_gain_lines(result["applied"])) {
        if (stringp(line) && line != "")
            lines += ({ line });
    }

    if (intp(result["exp_awarded"]) && result["exp_awarded"] > 0)
        lines += ({ sprintf("%s经验 +%d", skill_name, result["exp_awarded"]) });

    if (result["levelled_up"])
        lines += ({ sprintf("你的「%s」提升到了 %d 级。", skill_name, result["level_after"]) });

    return implode(lines, "\n") + "\n";
}

private void send_gather_result_event(object me, string message, int ok,
    string target_name, mapping result, mapping response_context)
{
    mapping payload;

    payload = ([
        "kind": "gather_result",
        "ok": ok ? 1 : 0,
        "target": stringp(target_name) ? target_name : "",
    ]);
    if (mapp(result)) {
        payload["skill_id"] = result["skill_id"] || "";
        payload["target_id"] = result["target_id"] || "";
        payload["target_name"] = result["target_name"] || target_name || "";
        payload["exp_awarded"] = result["exp_awarded"] || 0;
        payload["level_before"] = result["level_before"] || 0;
        payload["level_after"] = result["level_after"] || 0;
        payload["levelled_up"] = result["levelled_up"] ? 1 : 0;
        payload["error"] = result["error"] || "";
    }
    msg_event(me, message, ([
        "channel": "main",
        "intent": "append",
        "priority": ok ? "normal" : "high",
        "reliability": "important",
        "display_mode": "paced",
        "response_context": mapp(response_context) ? response_context : 0,
        "payload": payload,
    ]));
}

int main(object me, string arg)
{
    object target;
    mapping contract;
    mapping result;
    string message;
    mapping response_context;

    if (!objectp(me))
        return 0;

    response_context = CLIENT_SYNC_D->capture_command_response_context(me);
    if (!stringp(arg) || arg == "") {
        msg_send(me, "用法：gather <目标>\n", response_context);
        return 1;
    }

    target = resolve_gather_target(me, arg);
    if (!objectp(target)) {
        send_gather_result_event(me, "这里没有「" + arg + "」可供采集。\n", 0, arg, 0,
            response_context);
        return 1;
    }

    contract = query_gather_contract(me, target);
    if (!mapp(contract)) {
        send_gather_result_event(me, query_invalid_contract_message(target, arg), 0,
            query_target_display_name(target, arg), 0, response_context);
        return 1;
    }

    result = LIFE_SKILL_D->perform_activity(me, contract["skill_id"], contract,
        ([ "drop_on_full": 1 ]));
    if (!mapp(result) || !result["ok"]) {
        message = query_failure_message(result, target, arg);
        send_gather_result_event(me, message, 0,
            query_target_display_name(target, arg), result, response_context);
        return 1;
    }

    REWARD_FEEDBACK_D->sync_reward_client_state(me, result["applied"],
        response_context);
    message = build_success_message(result);
    send_gather_result_event(me, message, 1,
        query_target_display_name(target, arg), result, response_context);
    return 1;
}

int help(object me)
{
    msg_send(me, @HELP
命令：gather（采集）

对当前房间内带有 gather contract 的目标执行一次采集。

用法：
  gather <目标>

说明：
  - 目标必须在当前房间内
  - 目标需要提供 `query_gather_target` / `query_gather_contract`
    或 `gather_target` / `gather_contract` mapping
  - 结算走 LIFE_SKILL_D activity 分发 → gather runtime
  - 世界采集点由 resource_nodes 内容切片经房间实体补刷生成（gather spot 壳）

HELP
    );
    return 1;
}
