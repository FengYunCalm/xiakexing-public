/**
 * @file    fish.c
 * @brief   钓鱼命令 - 公共 fish 动词入口。
 *
 * @par Description:
 * fish 的公共入口只负责房间钓点目标解析、fishing contract 提取与 life_skill
 * activity 分发。结算真链在 LIFE_SKILL_D / fishing runtime，命令层不新增第二套规则。
 */

#pragma strict_types

#include <system/services.h>
#include <framework/message_facade.h>

inherit "/lib/framework/command";
inherit "/lib/std/command_utils";

int help(object me);
int main(object me, string arg);
private object resolve_fishing_spot(object me, string target_name);
private mapping query_fishing_contract(object me, object target);
private string query_target_display_name(object target, string fallback);
private string query_invalid_contract_message(object target, string fallback);
private string query_failure_message(mapping result, object target, string fallback_name);
private string build_success_message(mapping result);

private object resolve_fishing_spot(object me, string target_name)
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

private mapping query_fishing_contract(object me, object target)
{
    mapping contract;
    mixed err;

    if (!objectp(target))
        return 0;

    if (function_exists("query_fishing_contract", target)) {
        err = catch(contract = target->query_fishing_contract(me));
        if (!err && mapp(contract))
            return contract;
    }

    if (function_exists("query_fishing_target", target)) {
        err = catch(contract = target->query_fishing_target(me));
        if (!err && mapp(contract))
            return contract;
    }

    contract = target->query("fishing_target");
    if (mapp(contract))
        return contract;

    contract = target->query("fishing_contract");
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

private string query_invalid_contract_message(object target, string fallback)
{
    return "这里的「" + query_target_display_name(target, fallback) + "」没有可用的钓鱼契约。\n";
}

private string query_failure_message(mapping result, object target, string fallback_name)
{
    string error_code;
    string target_name;

    if (!mapp(result))
        return "钓鱼失败。\n";

    error_code = result["error"];
    target_name = query_target_display_name(target,
        stringp(result["target_name"]) && result["target_name"] != ""
            ? result["target_name"]
            : fallback_name);

    switch (error_code) {
    case "missing_activity_runtime":
    case "no_runtime_for_activity":
        return "钓鱼功能当前不可用。\n";

    case "invalid_fishing_skill":
        return "这里不能这么钓鱼。\n";

    case "insufficient_skill_level":
        return "你的「钓鱼」等级不足，无法在此下竿。\n";

    case "action_interval":
        return "你收竿收得太急，稍候再试。\n";

    case "action_cooldown":
        return "鱼线还没稳下来，暂时不能再下竿。\n";

    case "insufficient_resource":
        return "你有些疲乏，暂时抛不动竿。\n";

    case "invalid_fishing_contract":
        return "这里的「" + target_name + "」没有可用的鱼情。\n";

    case "no_catch_item":
        return "这一竿下去，水花都没有。\n";

    case "reward_apply_failed":
        return "钓鱼结算未完成，鱼获没有到手。\n";

    default:
        return "钓鱼失败。\n";
    }
}

private string build_success_message(mapping result)
{
    string *lines;
    string target_name;
    string catch_name;
    string line;

    lines = ({ });
    target_name = stringp(result["target_name"]) && result["target_name"] != ""
        ? result["target_name"]
        : "钓点";

    if (stringp(result["catch_item"]) && result["catch_item"] != "") {
        mapping item_template;

        item_template = ITEM_D->query_item_template(result["catch_item"]);
        catch_name = mapp(item_template) && stringp(item_template["name"])
            ? item_template["name"] : result["catch_item"];
        lines += ({ "你在「" + target_name + "」钓到了" + catch_name + "。" });
    } else {
        lines += ({ "你在「" + target_name + "」钓了一会儿。" });
    }

    foreach (line in REWARD_FEEDBACK_D->build_reward_gain_lines(result["applied"])) {
        if (stringp(line) && line != "")
            lines += ({ line });
    }

    if (intp(result["exp_awarded"]) && result["exp_awarded"] > 0)
        lines += ({ "钓鱼经验 +" + result["exp_awarded"] });

    if (result["levelled_up"])
        lines += ({ "你的「钓鱼」提升到了 " + result["level_after"] + " 级。" });

    return implode(lines, "\n") + "\n";
}

private void send_fish_result_event(object me, string message, int ok,
    string target_name, mapping result, mapping response_context)
{
    mapping payload;

    payload = ([
        "kind": "fishing_result",
        "ok": ok ? 1 : 0,
        "target": stringp(target_name) ? target_name : "",
    ]);
    if (mapp(result)) {
        payload["skill_id"] = result["skill_id"] || "";
        payload["target_id"] = result["target_id"] || "";
        payload["target_name"] = result["target_name"] || target_name || "";
        payload["catch_item"] = result["catch_item"] || "";
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
        msg_send(me, "用法：fish <钓点>\n", response_context);
        return 1;
    }

    target = resolve_fishing_spot(me, arg);
    if (!objectp(target)) {
        send_fish_result_event(me, "这里没有「" + arg + "」可以钓鱼。\n", 0, arg, 0,
            response_context);
        return 1;
    }

    contract = query_fishing_contract(me, target);
    if (!mapp(contract)) {
        send_fish_result_event(me, query_invalid_contract_message(target, arg), 0,
            query_target_display_name(target, arg), 0, response_context);
        return 1;
    }

    result = LIFE_SKILL_D->perform_activity(me, contract["skill_id"], contract,
        ([ "drop_on_full": 1 ]));
    if (!mapp(result) || !result["ok"]) {
        message = query_failure_message(result, target, arg);
        send_fish_result_event(me, message, 0,
            query_target_display_name(target, arg), result, response_context);
        return 1;
    }

    REWARD_FEEDBACK_D->sync_reward_client_state(me, result["applied"],
        response_context);
    message = build_success_message(result);
    send_fish_result_event(me, message, 1,
        query_target_display_name(target, arg), result, response_context);
    return 1;
}

int help(object me)
{
    msg_send(me, @HELP
命令：fish（钓鱼）

对当前房间内的钓点下一次竿。

用法：
  fish <钓点>

说明：
  - 钓点必须在当前房间内
  - 钓点需要提供 `query_fishing_contract` / `query_fishing_target`
    或 `fishing_target` / `fishing_contract` mapping
  - 结算走 LIFE_SKILL_D activity 分发 → fishing runtime

HELP
    );
    return 1;
}
