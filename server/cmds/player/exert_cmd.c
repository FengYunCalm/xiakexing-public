#pragma strict_types

#include <system/services.h>
#include <system/xk_protocol.h>
#include <framework/message_facade.h>

inherit "/lib/framework/command";

int main(object me, string arg);
int resolve_exert_action(object me, string skill_id, string action_id);
private mapping build_exert_skill_event_payload(string skill_id, string action_id, string skill_name, mapping behavior_result, mapping result);
int help(object me);

private string format_exert_failure(mapping result)
{
    string error;

    if (!mapp(result))
        return "你现在还运不起这门内功。\n";

    error = result["error"];
    switch (error) {
        case "missing_unlock_rule":
            return "这门内功当前还没有正式的运功规则。\n";
        case "skill_level_too_low":
            if (result["required_skill_level"])
                return sprintf("你的内功火候还不够，需要等级%d才能运使这一路内功。\n", result["required_skill_level"]);
            return "你的内功火候还不够，暂时运不起这门内功。\n";
        case "breakthrough_entry_required":
            return sprintf("你尚未选择对应突破词条，暂时不能运使这一路内功。需要词条：%s\n",
                result["required_breakthrough_entry"] || "未知");
        case "skill_not_activated":
            return "你现在运使的内功中并没有这一路运功法门。\n";
        case "missing_required_status":
            return sprintf("此诀需要%s状态「%s」。\n",
                result["status_owner"] == "target" ? "目标持有" : "自身持有",
                result["status_id"] || "前置");
        case "sect_not_allowed":
            return "你的门派不允许运使这门内功。\n";
        case "missing_exert_behavior":
            return "这门内功当前还没有具体的运功方式。\n";
        case "missing_combat_target":
            return "你需要先与目标交手，才能运使这一路内功。\n";
        case "action_cooldown":
            return sprintf("这门内功还需%d秒才能再次运使。\n", result["cooldown_remaining"] || 0);
        case "action_interval":
            return sprintf("你调息太急，还需%d秒才能再运使这门内功。\n", result["interval_remaining"] || 0);
        case "insufficient_resource":
            return "你的气力不足，暂时运不起这门内功。\n";
        default:
            return "你现在还运不起这门内功。\n";
    }
}

private string query_exert_failure_reason(mapping result)
{
    if (mapp(result) && stringp(result["error"]) && result["error"] != "")
        return result["error"];
    return "exert_failed";
}

private void send_exert_error(object me, string skill_id, string action_id, mapping result)
{
    string message;
    string request;

    message = format_exert_failure(result);
    request = skill_id + (stringp(action_id) && action_id != "" ? " " + action_id : "");
    if (is_xk_client(me) && !me->query_temp("client/plain_text_only")) {
        if (!xk_send_command_error(me, message, XK_ERR_SKILL_NOT_FOUND, "exert",
                query_exert_failure_reason(result), request,
                CLIENT_SYNC_D->capture_command_response_context(me)))
            msg_send_legacy(me, message);
        return;
    }
    msg_error(me, message, "exert", query_exert_failure_reason(result), request,
        XK_ERR_SKILL_NOT_FOUND);
}

private mapping build_exert_skill_event_payload(string skill_id, string action_id, string skill_name, mapping behavior_result, mapping result)
{
    return ([
        "event": "exert",
        "skill_id": skill_id,
        "skill_name": skill_name,
        "action_id": behavior_result["action_id"] || action_id,
        "action_name": behavior_result["action_name"] || "",
        "busy": behavior_result["busy"] || 0,
        "cooldown": behavior_result["cooldown"] || 0,
        "cooldown_remaining": behavior_result["cooldown_remaining"] || 0,
        "interval_remaining": behavior_result["interval_remaining"] || 0,
        "resource_costs": copy(behavior_result["resource_costs"] || ([ ])),
        "fatigue_bp": behavior_result["fatigue_bp"] || 0,
        "fatigue_factor_bp": behavior_result["fatigue_factor_bp"] || 10000,
        "target_policy": behavior_result["target_policy"] || "",
        "pvp_factor_bp": behavior_result["pvp_factor_bp"] || 10000,
        "pve_factor_bp": behavior_result["pve_factor_bp"] || 10000,
        "effects": copy(behavior_result["effects"] || ([ ])),
        "just_unlocked": result["just_unlocked"] ? 1 : 0,
        "message": stringp(behavior_result["message"]) && behavior_result["message"] != ""
            ? behavior_result["message"]
            : sprintf("你凝神调息，运起「%s」。", skill_name),
        "result_label": "运功",
    ]);
}

int main(object me, string arg)
{
    string action_id;
    string skill_id;

    if (!objectp(me))
        return 0;
    if (!stringp(arg) || arg == "")
        return help(me);

    if (sscanf(arg, "%s %s", skill_id, action_id) != 2) {
        skill_id = arg;
        action_id = "";
    }

    return resolve_exert_action(me, skill_id, action_id);
}

int resolve_exert_action(object me, string skill_id, string action_id)
{
    mapping definition;
    mapping behavior_result;
    mapping result;
    string skill_name;

    if (!objectp(me) || !stringp(skill_id) || skill_id == "")
        return 0;

    result = SKILL_UNLOCK_D->ensure_unlocked(me, "exert", skill_id);
    if (!mapp(result) || !result["ok"]) {
        send_exert_error(me, skill_id, action_id, result);
        return 1;
    }

    behavior_result = SKILL_BEHAVIOR_D->exert_function(me, skill_id, 0, action_id);
    if (!mapp(behavior_result) || !behavior_result["ok"]) {
        send_exert_error(me, skill_id, action_id, behavior_result);
        return 1;
    }

    definition = SKILL_D->get_skill_definition(skill_id);
    skill_name = mapp(definition) && stringp(definition["name"]) && definition["name"] != ""
        ? definition["name"]
        : "内功";
    if (result["just_unlocked"])
        msg_send(me, sprintf("你领悟了「%s」的基础运功法门。\n", skill_name));
    if (behavior_result["resolve_damage_now"]) {
        COMBAT_D->begin_action_packet_batch();
        COMBAT_D->combat_packet(me, XK_CBSK,
            build_exert_skill_event_payload(skill_id, action_id, skill_name, behavior_result, result));
        SKILL_BEHAVIOR_D->resolve_pending_perform_action(me, behavior_result["target"]);
        COMBAT_D->flush_action_packet_batch();
        xk_send_status_light(me);
        return 1;
    }
    SKILL_D->xk_send_skill_event(me, ([
        "event": "exert",
        "skill_id": skill_id,
        "skill_name": skill_name,
        "action_id": behavior_result["action_id"] || action_id,
        "action_name": behavior_result["action_name"] || "",
        "busy": behavior_result["busy"] || 0,
        "cooldown": behavior_result["cooldown"] || 0,
        "cooldown_remaining": behavior_result["cooldown_remaining"] || 0,
        "interval_remaining": behavior_result["interval_remaining"] || 0,
        "resource_costs": copy(behavior_result["resource_costs"] || ([ ])),
        "fatigue_bp": behavior_result["fatigue_bp"] || 0,
        "fatigue_factor_bp": behavior_result["fatigue_factor_bp"] || 10000,
        "target_policy": behavior_result["target_policy"] || "",
        "pvp_factor_bp": behavior_result["pvp_factor_bp"] || 10000,
        "pve_factor_bp": behavior_result["pve_factor_bp"] || 10000,
        "effects": copy(behavior_result["effects"] || ([ ])),
        "status_effects": behavior_result["status_effects"] || ({ }),
        "just_unlocked": result["just_unlocked"] ? 1 : 0,
        "message": stringp(behavior_result["message"]) && behavior_result["message"] != ""
            ? behavior_result["message"]
            : sprintf("你凝神调息，运起「%s」。", skill_name),
        "result_label": "运功",
    ]));
    xk_send_status_light(me);
    COMBAT_D->send_combat_runtime_snapshot(me);
    return 1;
}

int help(object me)
{
    msg_send(me, "用法：exert <skill_id> [action_id]\n");
    return 1;
}
