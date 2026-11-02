#pragma strict_types

#include <system/services.h>
#include <system/xk_protocol.h>
#include <framework/message_facade.h>

inherit "/lib/framework/command";

int main(object me, string arg);
int resolve_perform_action(object me, string skill_id, string action_id);
private mapping build_perform_skill_event_payload(string skill_id, string action_id, string skill_name, mapping behavior_result, mapping result);
int help(object me);
private void record_combat_phase(object me, string phase, int wall_started_at,
    int cpu_started_at);

private void record_combat_phase(object me, string phase, int wall_started_at,
    int cpu_started_at)
{
    int cpu_finished_at;

    cpu_finished_at = thread_cpu_time_ns();
    GATEWAY_TELEMETRY_D->record_gateway_combat_phase_timing(
        me, phase, perf_counter_ns() - wall_started_at,
        cpu_started_at >= 0 && cpu_finished_at >= cpu_started_at
            ? cpu_finished_at - cpu_started_at : -1);
}

private string format_perform_failure(mapping result)
{
    string error;

    if (!mapp(result))
        return "你现在还施展不出这门招式。\n";

    error = result["error"];
    switch (error) {
        case "missing_unlock_rule":
            return "这门招式还没有正式的解锁规则。\n";
        case "skill_level_too_low":
            if (result["required_skill_level"])
                return sprintf("你的这门武学火候还不够，需要等级%d才能施展这招。\n", result["required_skill_level"]);
            return "你的这门武学火候还不够，暂时施展不出这招。\n";
        case "breakthrough_entry_required":
            return sprintf("你尚未选择对应突破词条，暂时不能施展这招。需要词条：%s\n",
                result["required_breakthrough_entry"] || "未知");
        case "skill_not_activated":
            return "你现在使用的武学中并没有这种招式。\n";
        case "weapon_type_mismatch":
            return sprintf("你当前装备的武器不能施展这门招式，需要使用与%s匹配的武器。\n",
                result["required_weapon_name"] || result["required_weapon_type"] || "对应");
        case "missing_required_status":
            return sprintf("此招需要%s状态「%s」。\n",
                result["status_owner"] == "target" ? "目标持有" : "自身持有",
                result["status_id"] || "前置");
        case "sect_not_allowed":
            return "你的门派不允许施展这门招式。\n";
        case "missing_perform_behavior":
            return "这门招式当前还没有具体的施展方式。\n";
        case "action_cooldown":
            return sprintf("这门招式还需%d秒才能再次施展。\n", result["cooldown_remaining"] || 0);
        case "action_interval":
            return sprintf("你出手太急，还需%d秒才能再施展这门招式。\n", result["interval_remaining"] || 0);
        case "insufficient_resource":
            return "你的气力不足，暂时施展不出这门招式。\n";
        case "missing_combat_target":
            return "你尚未与目标交手，无法施展这门招式。\n";
        default:
            return "你现在还施展不出这门招式。\n";
    }
}

private string query_perform_failure_reason(mapping result)
{
    if (mapp(result) && stringp(result["error"]) && result["error"] != "")
        return result["error"];
    return "perform_failed";
}

private void send_perform_error(object me, string skill_id, string action_id, mapping result)
{
    string message;
    string request;

    message = format_perform_failure(result);
    request = skill_id + (stringp(action_id) && action_id != "" ? " " + action_id : "");
    if (is_xk_client(me) && !me->query_temp("client/plain_text_only")) {
        if (!xk_send_command_error(me, message, XK_ERR_SKILL_NOT_FOUND, "perform",
                query_perform_failure_reason(result), request,
                CLIENT_SYNC_D->capture_command_response_context(me)))
            msg_send_legacy(me, message);
        return;
    }
    msg_error(me, message, "perform", query_perform_failure_reason(result), request,
        XK_ERR_SKILL_NOT_FOUND);
}

private mapping build_perform_skill_event_payload(string skill_id, string action_id, string skill_name, mapping behavior_result, mapping result)
{
    return ([
        "event": "perform",
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
        "message": behavior_result["resolve_damage_now"] ? ""
            : stringp(behavior_result["message"]) && behavior_result["message"] != ""
                ? behavior_result["message"]
                : sprintf("你提气运劲，使出「%s」。", skill_name),
        "result_label": "施展招式",
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

    return resolve_perform_action(me, skill_id, action_id);
}

int resolve_perform_action(object me, string skill_id, string action_id)
{
    mapping definition;
    mapping behavior_result;
    mapping result;
    string skill_name;
    int phase_started_at;
    int phase_cpu_started_at;

    if (!objectp(me) || !stringp(skill_id) || skill_id == "")
        return 0;

    phase_started_at = perf_counter_ns();
    phase_cpu_started_at = thread_cpu_time_ns();
    result = SKILL_UNLOCK_D->ensure_unlocked(me, "perform", skill_id);
    record_combat_phase(me, "perform_unlock", phase_started_at, phase_cpu_started_at);
    if (!mapp(result) || !result["ok"]) {
        send_perform_error(me, skill_id, action_id, result);
        return 1;
    }

    phase_started_at = perf_counter_ns();
    phase_cpu_started_at = thread_cpu_time_ns();
    behavior_result = SKILL_BEHAVIOR_D->perform_action(me, skill_id, 0, action_id);
    record_combat_phase(me, "perform_behavior", phase_started_at, phase_cpu_started_at);
    if (!mapp(behavior_result) || !behavior_result["ok"]) {
        send_perform_error(me, skill_id, action_id, behavior_result);
        return 1;
    }

    definition = SKILL_D->get_skill_definition(skill_id);
    skill_name = mapp(definition) && stringp(definition["name"]) && definition["name"] != ""
        ? definition["name"]
        : "招式";
    /* Combat-target performs report their outcome in the resolved combat packet. */
    if (result["just_unlocked"])
        msg_send(me, sprintf("你领悟了「%s」的基础招式。\n", skill_name));
    if (behavior_result["resolve_damage_now"]) {
        phase_started_at = perf_counter_ns();
        phase_cpu_started_at = thread_cpu_time_ns();
        COMBAT_D->begin_action_packet_batch();
        COMBAT_D->combat_packet(me, XK_CBSK,
            build_perform_skill_event_payload(skill_id, action_id, skill_name, behavior_result, result));
        SKILL_BEHAVIOR_D->resolve_pending_perform_action(me, behavior_result["target"]);
        COMBAT_D->flush_action_packet_batch();
        record_combat_phase(me, "perform_resolve", phase_started_at, phase_cpu_started_at);
        phase_started_at = perf_counter_ns();
        phase_cpu_started_at = thread_cpu_time_ns();
        xk_send_status_light(me);
        record_combat_phase(me, "perform_status", phase_started_at, phase_cpu_started_at);
        return 1;
    }
    SKILL_D->xk_send_skill_event(me, ([
        "event": "perform",
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
            : sprintf("你提气运劲，使出「%s」。", skill_name),
        "result_label": "施展招式",
    ]));
    phase_started_at = perf_counter_ns();
    phase_cpu_started_at = thread_cpu_time_ns();
    xk_send_status_light(me);
    COMBAT_D->send_combat_runtime_snapshot(me);
    record_combat_phase(me, "perform_status", phase_started_at, phase_cpu_started_at);
    return 1;
}

int help(object me)
{
    msg_send(me, "用法：perform <skill_id> [action_id]\n");
    return 1;
}
