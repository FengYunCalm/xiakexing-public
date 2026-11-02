#pragma strict_types

#include <system/services.h>
#include <system/xk_protocol.h>
#include <framework/message_facade.h>

inherit "/lib/framework/command";
inherit "/lib/std/command_utils";

int main(object me, string arg);
int help(object me);

private string format_learn_cost_kind(string kind)
{
    switch (kind) {
        case "potential":
            return "潜能";
        case "combat_exp":
            return "经验";
        case "sect_contribution":
            return "门派贡献";
        case "guild_contribution":
            return "帮派贡献";
        default:
            return kind;
    }
}

private string format_learn_cost_summary(mapping result)
{
    mapping cost_spec;
    string kind;
    int amount;

    cost_spec = mapp(result) ? (result["cost_spec"] || ([])) : ([]);
    kind = mapp(cost_spec) && stringp(cost_spec["kind"]) ? cost_spec["kind"] : "none";
    amount = mapp(cost_spec) && intp(cost_spec["amount"]) ? cost_spec["amount"] : 0;
    if (amount <= 0 || kind == "" || kind == "none")
        return "";
    return sprintf("，消耗%s%d", format_learn_cost_kind(kind), amount);
}

private string format_learn_insufficient_cost(mapping result, string fallback_kind)
{
    mapping cost_spec;
    string kind;
    int amount;
    int current;

    cost_spec = mapp(result) ? (result["cost_spec"] || ([])) : ([]);
    kind = mapp(cost_spec) && stringp(cost_spec["kind"]) ? cost_spec["kind"] : fallback_kind;
    amount = mapp(cost_spec) && intp(cost_spec["amount"]) ? cost_spec["amount"] : 0;
    current = mapp(result) && intp(result["cost_current"]) ? result["cost_current"] : 0;
    if (amount > 0)
        return sprintf("你的%s不足，本次学习需要%d，你现在有%d。\n", format_learn_cost_kind(kind), amount, current);
    return sprintf("你的%s不足，暂时付不起这门技能的修习消耗。\n", format_learn_cost_kind(kind));
}

private string format_learn_stop_hint(mapping result)
{
    mapping cost_spec;
    string error;
    int amount;
    int current;

    if (!mapp(result) || !mapp(result["cost_spec"]))
        return "";
    error = result["cost_spec"]["stop_error"] || "";
    if (error == "")
        return "";
    cost_spec = result["cost_spec"]["stop_cost_spec"] || ([]);
    amount = mapp(cost_spec) && intp(cost_spec["amount"]) ? cost_spec["amount"] : 0;
    current = result["cost_spec"]["stop_cost_current"] || 0;
    switch (error) {
        case "insufficient_potential":
        case "insufficient_combat_exp":
        case "insufficient_sect_contribution":
        case "insufficient_guild_contribution":
            if (amount > 0)
                return sprintf("下一次学习需要%s%d，你现在有%d。", format_learn_cost_kind(cost_spec["kind"] || "none"), amount, current);
            return "剩余等级暂时无法继续学习。";
        default:
            return "剩余等级暂时无法继续学习。";
    }
}

private string format_learn_failure(mapping result)
{
    string error;

    if (!mapp(result))
        return "你现在还学不会这门技能。\n";

    error = result["error"];
    switch (error) {
        case "missing_skill_source":
            return "目前没有合适的来源可以教授这门技能。\n";
        case "sect_not_allowed":
            return "你的门派不符合这位导师的授业要求。\n";
        case "organization_missing":
            return "你当前不在可授业的组织体系里。\n";
        case "organization_not_allowed":
            return "你的门派不符合这位导师的授业要求。\n";
        case "rank_too_low":
            return "你的门派身份还不够，暂时学不了这门技能。\n";
        case "contribution_too_low":
            return "你的门派贡献不足，暂时学不了这门技能。\n";
        case "insufficient_potential":
            return format_learn_insufficient_cost(result, "potential");
        case "insufficient_combat_exp":
            return format_learn_insufficient_cost(result, "combat_exp");
        case "insufficient_sect_contribution":
            return format_learn_insufficient_cost(result, "sect_contribution");
        case "insufficient_guild_contribution":
            return format_learn_insufficient_cost(result, "guild_contribution");
        case "already_learned":
            return "这门技能你已经掌握，无需重复求教。\n";
        case "teacher_limit_reached":
            return "这门技能已经学到这位导师能教的上限。\n";
        case "skill_cap_reached":
            return "你的基础修为还没达到要求，暂时学不了这门技能。\n";
        case "missing_required_skills":
            if (stringp(result["requirement_hint"]) && result["requirement_hint"] != "")
                return "你的前置技能还没达到要求：" + result["requirement_hint"] + "。\n";
            return "你的前置技能还没达到要求。\n";
        case "missing_required_quests":
            if (stringp(result["requirement_hint"]) && result["requirement_hint"] != "")
                return result["requirement_hint"] + "。\n";
            return "你还没有完成这位导师要求的支线任务。\n";
        case "missing_required_chests":
            if (stringp(result["requirement_hint"]) && result["requirement_hint"] != "")
                return result["requirement_hint"] + "。\n";
            return "你还没有开启这位导师要求的探索宝箱。\n";
        case "learn_skill_failed":
            return "这门技能学习失败，暂时无法写入你的技能列表。\n";
        case "spend_failed":
            return "修习消耗结算失败，请稍后再试。\n";
        default:
            return "你现在还学不会这门技能。\n";
    }
}

private string query_learn_failure_reason(mapping result)
{
    if (mapp(result) && stringp(result["error"]) && result["error"] != "")
        return result["error"];
    return "learn_failed";
}

private void emit_learn_message_event(object me, string text, mapping payload,
    mapping response_context)
{
    mapping event_payload;

    if (!objectp(me) || !stringp(text) || text == "")
        return;

    event_payload = mapp(payload) ? copy(payload) : ([]);
    if (!stringp(event_payload["kind"]) || event_payload["kind"] == "")
        event_payload["kind"] = "skill_learn";
    msg_event(me, text, ([
        "channel": event_payload["kind"] == "skill_learn_error" ? "error" : "system",
        "intent": "notify",
        "priority": event_payload["kind"] == "skill_learn_error" ? "high" : "normal",
        "reliability": event_payload["kind"] == "skill_learn_error" ? "critical" : "important",
        "display_mode": "instant",
        "response_context": response_context,
        "payload": event_payload,
    ]));
}

private void send_learn_error(object me, string message, string reason,
    string request, int code, mapping response_context)
{
    xk_send_command_error(me, message, code, "learn", reason, request,
        response_context);
    emit_learn_message_event(me, message, ([
        "kind": "skill_learn_error",
        "command": "learn",
        "reason": reason,
        "request": request,
        "code": code,
    ]), response_context);
}

private object resolve_teacher(object me, string teacher_name)
{
    object env;

    env = objectp(me) ? environment(me) : 0;
    if (!objectp(env) || !stringp(teacher_name) || teacher_name == "")
        return 0;
    return find_living_in_room(env, teacher_name, me);
}

private void record_learn_phase(object me, string phase, int wall_started_at,
    int cpu_started_at)
{
    int cpu_finished_at;
    int cpu_elapsed_ns;

    cpu_finished_at = GATEWAY_TELEMETRY_D->query_gateway_thread_cpu_time_ns();
    cpu_elapsed_ns = cpu_started_at >= 0 && cpu_finished_at >= 0
        ? cpu_finished_at - cpu_started_at
        : -1;
    GATEWAY_TELEMETRY_D->record_gateway_progression_phase_timing(
        me, phase, perf_counter_ns() - wall_started_at, cpu_elapsed_ns);
}

private string query_teacher_display_name(object teacher, string fallback)
{
    string display_name;

    if (objectp(teacher)) {
        display_name = teacher->query_name();
        if (!stringp(display_name) || display_name == "")
            display_name = teacher->query("name");
    }
    if (!stringp(display_name) || display_name == "")
        display_name = fallback;
    if (!stringp(display_name) || display_name == "")
        display_name = "导师";
    return display_name;
}

private int show_teacher_skills(object me, string teacher_name,
    mapping response_context)
{
    object teacher;

    teacher = resolve_teacher(me, teacher_name);
    if (!objectp(teacher)) {
        send_learn_error(me, "这里没有这位导师。\n", "teacher_not_found",
            teacher_name, XK_ERR_NOT_FOUND, response_context);
        return 1;
    }
    if (function_exists("present_skill_learning_panel", teacher))
        return teacher->present_skill_learning_panel(me);
    msg_send(me, "此人当前没有可以传授给你的技能。\n", response_context);
    return 1;
}

private mixed parse_level_count(string text)
{
    int count;

    if (!stringp(text) || text == "")
        return 1;
    text = lower_case(text);
    if (text == "all" || text == "全部")
        return "all";
    if (sscanf(text, "%d", count) == 1 && count > 0)
        return count;
    return 1;
}

private int looks_like_level_count(string text)
{
    int count;

    if (!stringp(text) || text == "")
        return 0;
    text = lower_case(text);
    return text == "all" || text == "全部" || sscanf(text, "%d", count) == 1;
}

int main(object me, string arg)
{
    mapping parsed;
    mapping result;
    object teacher;
    string skill_id;
    string teacher_name;
    string teacher_display_name;
    string count_text;
    string rest;
    string *parts;
    mapping response_context;
    int from_index;
    int phase_cpu_started_at;
    int phase_wall_started_at;
    mixed level_count;

    if (!objectp(me))
        return 0;

    response_context = CLIENT_SYNC_D->capture_command_response_context(me);
    if (!stringp(arg) || arg == "")
        return help(me);

    if (sscanf(arg, "list %s", teacher_name) == 1)
        return show_teacher_skills(me, teacher_name, response_context);

    from_index = strsrch(arg, " from ");
    if (from_index >= 0) {
        skill_id = arg[0..from_index - 1];
        rest = arg[from_index + 6..];
        parts = explode(rest, " ");
        if (sizeof(parts) > 1 && looks_like_level_count(parts[<1])) {
            count_text = parts[<1];
            teacher_name = implode(parts[0..<2], " ");
            level_count = parse_level_count(count_text);
        } else {
            teacher_name = rest;
            level_count = 1;
        }
    } else {
        parsed = parse_command_args(arg, "%s from %s");
        if (!parsed["valid"])
            return help(me);
        skill_id = parsed["args"][0];
        teacher_name = parsed["args"][1];
        level_count = 1;
    }

    phase_wall_started_at = perf_counter_ns();
    phase_cpu_started_at = GATEWAY_TELEMETRY_D->query_gateway_thread_cpu_time_ns();
    teacher = resolve_teacher(me, teacher_name);
    record_learn_phase(me, "learn_teacher_lookup", phase_wall_started_at,
        phase_cpu_started_at);
    if (!objectp(teacher)) {
        send_learn_error(me, "这里没有这位导师。\n", "teacher_not_found",
            teacher_name, XK_ERR_NOT_FOUND, response_context);
        return 1;
    }

    phase_wall_started_at = perf_counter_ns();
    phase_cpu_started_at = GATEWAY_TELEMETRY_D->query_gateway_thread_cpu_time_ns();
    result = SKILL_SOURCE_D->learn_from_teacher_levels(me, teacher, skill_id, level_count);
    record_learn_phase(me, "learn_levels", phase_wall_started_at,
        phase_cpu_started_at);
    if (mapp(result) && result["ok"]) {
        string cost_summary;
        string stop_hint;
        string message;

        phase_wall_started_at = perf_counter_ns();
        phase_cpu_started_at = GATEWAY_TELEMETRY_D->query_gateway_thread_cpu_time_ns();
        cost_summary = format_learn_cost_summary(result);
        stop_hint = format_learn_stop_hint(result);
        teacher_display_name = query_teacher_display_name(teacher, teacher_name);
        message = sprintf("你向%s学习了%s%d级，当前%d级%s。%s%s\n",
            teacher_display_name,
            result["display_name"] || skill_id,
            result["levels"] || 1,
            result["level"] || 1,
            cost_summary,
            stop_hint == "" ? "" : " ",
            stop_hint);
        emit_learn_message_event(me, message, ([
            "skill_id": skill_id,
            "skill_name": result["display_name"] || skill_id,
            "teacher": teacher_display_name,
            "levels": result["levels"] || 1,
            "level": result["level"] || 1,
            "cost_summary": cost_summary,
        ]), response_context);
        record_learn_phase(me, "learn_message_event", phase_wall_started_at,
            phase_cpu_started_at);

        phase_wall_started_at = perf_counter_ns();
        phase_cpu_started_at = GATEWAY_TELEMETRY_D->query_gateway_thread_cpu_time_ns();
        QUEST_D->record_action_progress(me, "learn_" + skill_id, teacher, "");
        record_learn_phase(me, "learn_quest_progress", phase_wall_started_at,
            phase_cpu_started_at);

        phase_wall_started_at = perf_counter_ns();
        phase_cpu_started_at = GATEWAY_TELEMETRY_D->query_gateway_thread_cpu_time_ns();
        call_other("/kernel/simul_efun", "xk_send_skill_list", me);
        record_learn_phase(me, "learn_skill_list", phase_wall_started_at,
            phase_cpu_started_at);
        return 1;
    }

    if (mapp(result) && result["error"] == "already_learned") {
        /* L-203：重复学习不改变任何状态，不得上报任务进度——否则按动作
         * 次数累计的任务目标可原地刷满（命令层把重复请求当成功事件）。
         * 只有真实学会（上面 ok 分支）才发 learn_<skill> 事件。 */
        phase_wall_started_at = perf_counter_ns();
        phase_cpu_started_at = GATEWAY_TELEMETRY_D->query_gateway_thread_cpu_time_ns();
        call_other("/kernel/simul_efun", "xk_send_skill_list", me);
        record_learn_phase(me, "learn_skill_list", phase_wall_started_at,
            phase_cpu_started_at);
    }

    send_learn_error(me, format_learn_failure(result), query_learn_failure_reason(result),
        skill_id + " from " + teacher_name, XK_ERR_SKILL_NOT_FOUND, response_context);
    return 1;
}

int help(object me)
{
    msg_send(me, "用法：learn list <导师> | learn <skill_id> from <导师> [级数|all]\n");
    return 1;
}
