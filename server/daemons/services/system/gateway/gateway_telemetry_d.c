/**
 * @file    gateway_telemetry_d.c
 * @brief   网关性能遥测服务真源
 *
 * 从 command_dispatch_d.c 迁出的全部网关计时存储、探针与压测窗口重置：
 * - _gateway_active_probe 扁平标量探针（receive/dispatch/command_main/flush）
 * - 7 组固定基数 timing（family / trade_consume / dispatch_stage /
 *   movement / combat / progression / receive phase）
 * - 命令 family 分类单一数据表（新增热点命令只改表）
 *
 * 职责边界：
 * - 只做观测记录与 status 查询，不参与命令执行语义。
 * - 压测输入标记装置（mark/consume/is_loadtest_measurement_command）留在
 *   command_dispatch_d.c；本服务仅在 finish_gateway_receive_measurement
 *   处回调 consume 以跳过 setup 流量。
 * - status 键形是 ownercheck/healthcheck 消费契约，不得更改。
 */

#pragma strict_types
#pragma save_types

#include <system/globals.h>
#include <system/services.h>
#include <system/telemetry_schema.h>

#define UTIL_TIMING "/lib/std/utils/util_timing"

inherit "/lib/framework/daemon";

#define GATEWAY_ACTIVE_TAIL_25MS_US 25000

nosave mapping _gateway_active_probe = ([
    "gateway_active_receive_count": 0,
    "gateway_active_receive_total_us": 0,
    "gateway_active_receive_max_us": 0,
    "gateway_active_receive_tail_25ms_count": 0,
    "gateway_active_chat_receive_count": 0,
    "gateway_active_chat_receive_total_us": 0,
    "gateway_active_chat_receive_max_us": 0,
    "gateway_active_chat_receive_tail_25ms_count": 0,
    "gateway_active_dispatch_count": 0,
    "gateway_active_dispatch_total_us": 0,
    "gateway_active_dispatch_max_us": 0,
    "gateway_active_dispatch_tail_25ms_count": 0,
    "gateway_active_command_main_count": 0,
    "gateway_active_command_main_total_us": 0,
    "gateway_active_command_main_max_us": 0,
    "gateway_active_command_main_tail_25ms_count": 0,
    "gateway_active_command_main_tail_cpu_samples": 0,
    "gateway_active_command_main_tail_cpu_total_us": 0,
    "gateway_active_command_main_tail_cpu_max_us": 0,
    "gateway_active_command_main_tail_cpu_unavailable_count": 0,
    "gateway_active_command_main_tail_25ms_look_count": 0,
    "gateway_active_command_main_tail_25ms_inventory_count": 0,
    "gateway_active_command_main_tail_25ms_skills_count": 0,
    "gateway_active_command_main_tail_25ms_hp_count": 0,
    "gateway_active_command_main_tail_25ms_map_xk_count": 0,
    "gateway_active_command_main_tail_25ms_score_count": 0,
    "gateway_active_command_main_tail_25ms_other_count": 0,
    "gateway_active_flush_count": 0,
    "gateway_active_flush_total_us": 0,
    "gateway_active_flush_max_us": 0,
    "gateway_active_flush_tail_25ms_count": 0,
]);
nosave mapping _gateway_command_family_timing = ([]);
nosave mapping _gateway_trade_consume_command_timing = ([]);
nosave mapping _gateway_command_dispatch_stage_timing = ([]);
nosave mapping _gateway_movement_phase_timing = ([]);
nosave mapping _gateway_combat_phase_timing = ([]);
nosave mapping _gateway_progression_phase_timing = ([]);
nosave mapping _gateway_receive_phase_timing = ([]);

nosave string *_gateway_receive_phases = ({
    "session_lookup", "session_touch", "route_ensure", "input_decode",
    "process_input", "finish_measurement",
});

nosave string *_gateway_movement_phases = ({
    "framework_precheck", "framework_context", "framework_leave",
    "framework_move_object", "framework_transition", "framework_arrive",
    "framework_completion", "framework_cleanup", "framework_total",
    "completion_navigation", "navigation_meta_query", "navigation_payload_build",
    "navigation_batch_send", "exits_name_resolve", "exits_quest_marker",
    "exits_indoor_scan", "exits_payload_assembly", "completion_entities", "completion_world",
    "completion_quest_escort", "completion_quest_arrive", "completion_dungeon",
    "completion_followers", "completion_total",
});

/* 相位清单单源：GATEWAY_COMBAT_PHASES 见 telemetry_schema.h（单分配快照） */
nosave string *_gateway_combat_phases = GATEWAY_COMBAT_PHASES;

nosave string *_gateway_progression_phases = ({
    "learn_teacher_lookup", "learn_levels", "learn_message_event",
    "learn_quest_progress", "learn_skill_list",
});

/* 命令 family 分类单一数据表。cmd_path → family；
 * "map xk" 的参数谓词是唯一特例（见 gateway_command_main_tail_family）。
 * 新增热点命令只需在此加一行。 */
nosave mapping _command_family_table = ([
    "/cmds/player/look": "look",
    "/cmds/player/i": "inventory",
    "/cmds/player/inventory": "inventory",
    "/cmds/player/skills": "skills",
    "/cmds/player/hp": "hp",
    "/cmds/player/map": "map_xk",
    "/cmds/player/score": "score",
    "/cmds/player/go": "movement",
    "/cmds/player/fight": "combat",
    "/cmds/player/flee": "combat",
    "/cmds/player/perform_cmd": "combat",
    "/cmds/player/buy": "trade_consume",
    "/cmds/player/eat": "trade_consume",
    "/cmds/player/learn_cmd": "progression",
    "/cmds/player/say": "chat",
    "/cmds/player/equip": "equipment",
    "/cmds/player/unequip": "equipment",
]);

/* family status 枚举顺序真源：与拆分前 dispatch 固定数组逐项一致。
 * mapping values() 顺序不保证等于插入序，禁止用表派生枚举顺序；
 * 新增 family 时本数组与 _command_family_table 同文件内同步加一行。 */
nosave string *_command_family_order = ({
    "look", "inventory", "skills", "hp", "map_xk", "score",
    "movement", "combat", "trade_consume", "progression", "chat",
    "equipment", "other"
});

void create()
{
    ::create();
}

int gateway_actor_probe_ready(object actor)
{
    string session_id;

    if (!objectp(actor) || !interactive(actor))
        return 0;

    /* Session binding already publishes the canonical id on the actor. Keep
     * fixed-cardinality timing on the command path object-local; the daemon
     * mapping remains the fallback truth source for non-hot routing callers. */
    session_id = actor->query_temp("gateway/session_id");
    return stringp(session_id) && session_id != "";
}

void record_gateway_probe_sample(string prefix, int elapsed_ns)
{
    string count_key;
    string total_key;
    string max_key;
    string tail_key;
    int elapsed_us;

    if (!stringp(prefix) || prefix == "" || !intp(elapsed_ns) || elapsed_ns < 0)
        return;

    /* The phase set is fixed. Literal keys avoid four dynamic strings for
     * every serial gateway command while preserving the public status schema. */
    switch (prefix) {
    case "gateway_active_receive":
        count_key = "gateway_active_receive_count";
        total_key = "gateway_active_receive_total_us";
        max_key = "gateway_active_receive_max_us";
        tail_key = "gateway_active_receive_tail_25ms_count";
        break;
    case "gateway_active_chat_receive":
        count_key = "gateway_active_chat_receive_count";
        total_key = "gateway_active_chat_receive_total_us";
        max_key = "gateway_active_chat_receive_max_us";
        tail_key = "gateway_active_chat_receive_tail_25ms_count";
        break;
    case "gateway_active_dispatch":
        count_key = "gateway_active_dispatch_count";
        total_key = "gateway_active_dispatch_total_us";
        max_key = "gateway_active_dispatch_max_us";
        tail_key = "gateway_active_dispatch_tail_25ms_count";
        break;
    case "gateway_active_command_main":
        count_key = "gateway_active_command_main_count";
        total_key = "gateway_active_command_main_total_us";
        max_key = "gateway_active_command_main_max_us";
        tail_key = "gateway_active_command_main_tail_25ms_count";
        break;
    case "gateway_active_flush":
        count_key = "gateway_active_flush_count";
        total_key = "gateway_active_flush_total_us";
        max_key = "gateway_active_flush_max_us";
        tail_key = "gateway_active_flush_tail_25ms_count";
        break;
    default:
        return;
    }
    elapsed_us = elapsed_ns / 1000;

    _gateway_active_probe[count_key] =
        (intp(_gateway_active_probe[count_key]) ? _gateway_active_probe[count_key] : 0) + 1;
    _gateway_active_probe[total_key] =
        (intp(_gateway_active_probe[total_key]) ? _gateway_active_probe[total_key] : 0) + elapsed_us;
    if (!intp(_gateway_active_probe[max_key]) || elapsed_us > _gateway_active_probe[max_key])
        _gateway_active_probe[max_key] = elapsed_us;
    if (elapsed_us >= GATEWAY_ACTIVE_TAIL_25MS_US)
        _gateway_active_probe[tail_key] =
            (intp(_gateway_active_probe[tail_key]) ? _gateway_active_probe[tail_key] : 0) + 1;
}

string gateway_command_main_tail_family(string cmd_path, string args)
{
    string family;

    family = _command_family_table[cmd_path];
    if (!stringp(family))
        return "other";
    if (family == "map_xk" && lower_case(trim(args)) != "xk")
        return "other";
    return family;
}

/* 与 command_dispatch_d 的同名私有助手同实现：仅去首尾空格。 */
private string trim(string str)
{
    int start;
    int end;

    if (!stringp(str))
        return "";

    start = 0;
    end = strlen(str) - 1;

    while (start <= end && str[start] == ' ')
        start++;
    while (end >= start && str[end] == ' ')
        end--;

    if (start > end)
        return "";

    return str[start..end];
}

private void record_gateway_command_main_tail_family(string family)
{
    string key;

    if (member_array(family, ({ "look", "inventory", "skills", "hp",
            "map_xk", "score" })) >= 0)
        key = "gateway_active_command_main_tail_25ms_" + family + "_count";
    else
        key = "gateway_active_command_main_tail_25ms_other_count";
    _gateway_active_probe[key] =
        (intp(_gateway_active_probe[key]) ? _gateway_active_probe[key] : 0) + 1;
}

void record_gateway_command_main_tail_cpu_sample(string family, int elapsed_ns,
    int cpu_elapsed_ns)
{
    int elapsed_us;
    int cpu_elapsed_us;

    if (!intp(elapsed_ns) || elapsed_ns < 0)
        return;

    elapsed_us = elapsed_ns / 1000;
    if (elapsed_us < GATEWAY_ACTIVE_TAIL_25MS_US)
        return;

    record_gateway_command_main_tail_family(family);

    if (!intp(cpu_elapsed_ns) || cpu_elapsed_ns < 0) {
        _gateway_active_probe["gateway_active_command_main_tail_cpu_unavailable_count"] =
            (intp(_gateway_active_probe["gateway_active_command_main_tail_cpu_unavailable_count"])
                ? _gateway_active_probe["gateway_active_command_main_tail_cpu_unavailable_count"] : 0) + 1;
        return;
    }

    cpu_elapsed_us = cpu_elapsed_ns / 1000;
    _gateway_active_probe["gateway_active_command_main_tail_cpu_samples"] =
        (intp(_gateway_active_probe["gateway_active_command_main_tail_cpu_samples"])
            ? _gateway_active_probe["gateway_active_command_main_tail_cpu_samples"] : 0) + 1;
    _gateway_active_probe["gateway_active_command_main_tail_cpu_total_us"] =
        (intp(_gateway_active_probe["gateway_active_command_main_tail_cpu_total_us"])
            ? _gateway_active_probe["gateway_active_command_main_tail_cpu_total_us"] : 0) + cpu_elapsed_us;
    if (!intp(_gateway_active_probe["gateway_active_command_main_tail_cpu_max_us"])
        || cpu_elapsed_us > _gateway_active_probe["gateway_active_command_main_tail_cpu_max_us"])
        _gateway_active_probe["gateway_active_command_main_tail_cpu_max_us"] = cpu_elapsed_us;
}

void record_gateway_command_family_timing(string family, int wall_ns, int cpu_ns)
{
    UTIL_TIMING->accumulate_wall_cpu(_gateway_command_family_timing, family,
        wall_ns / 1000, cpu_ns < 0 ? -1 : cpu_ns / 1000);
}

void record_gateway_trade_consume_command_timing(string command, int wall_ns,
    int cpu_ns)
{
    if (member_array(command, ({ "buy", "eat" })) == -1
        || !intp(wall_ns) || wall_ns < 0)
        return;
    UTIL_TIMING->accumulate_wall_cpu(_gateway_trade_consume_command_timing,
        command, wall_ns / 1000,
        (!intp(cpu_ns) || cpu_ns < 0) ? -1 : cpu_ns / 1000);
}

void record_gateway_command_dispatch_stage(string stage, int elapsed_ns)
{
    if (member_array(stage, ({ "owner_trace", "transaction_prepare", "command_main",
            "actor_flush", "transaction_finalize", "dispatch_total" })) == -1
        || !intp(elapsed_ns) || elapsed_ns < 0)
        return;
    UTIL_TIMING->accumulate_samples(_gateway_command_dispatch_stage_timing,
        stage, elapsed_ns / 1000);
}

mapping query_gateway_trade_consume_command_timing_status()
{
    mapping result;
    mapping timing;
    string command;
    string prefix;

    result = ([]);
    foreach (command in ({ "buy", "eat" })) {
        timing = mapp(_gateway_trade_consume_command_timing[command])
            ? _gateway_trade_consume_command_timing[command] : ([]);
        prefix = "gateway_trade_consume_command_" + command + "_";
        foreach (string field in GATEWAY_TIMING_FIELD_SUFFIXES)
            result[prefix + field] = timing[field] || 0;
    }
    return result;
}

mapping query_gateway_command_dispatch_stage_timing_status()
{
    mapping result;
    mapping timing;
    string stage;
    string prefix;

    result = ([]);
    foreach (stage in ({ "owner_trace", "transaction_prepare", "command_main",
            "actor_flush", "transaction_finalize", "dispatch_total" })) {
        timing = mapp(_gateway_command_dispatch_stage_timing[stage])
            ? _gateway_command_dispatch_stage_timing[stage] : ([]);
        prefix = "gateway_command_dispatch_" + stage + "_";
        foreach (string field in ({ "samples", "total_us", "max_us" }))
            result[prefix + field] = timing[field] || 0;
    }
    return result;
}

mapping query_gateway_command_family_timing_status()
{
    mapping result;
    mapping timing;
    string family;
    string prefix;

    result = ([]);
    foreach (family in _command_family_order) {
        timing = mapp(_gateway_command_family_timing[family])
            ? _gateway_command_family_timing[family] : ([]);
        prefix = "gateway_command_family_" + family + "_";
        foreach (string field in GATEWAY_TIMING_FIELD_SUFFIXES)
            result[prefix + field] = timing[field] || 0;
    }
    return result;
}

void record_gateway_movement_phase_timing(object actor, string phase, int wall_ns,
    int cpu_ns)
{
    if (!gateway_actor_probe_ready(actor)
        || member_array(phase, _gateway_movement_phases) == -1
        || !intp(wall_ns) || wall_ns < 0)
        return;

    UTIL_TIMING->accumulate_wall_cpu(_gateway_movement_phase_timing, phase,
        wall_ns / 1000, (!intp(cpu_ns) || cpu_ns < 0) ? -1 : cpu_ns / 1000);
}

int query_gateway_thread_cpu_time_ns()
{
    return thread_cpu_time_ns();
}

mapping query_gateway_movement_phase_timing_status()
{
    mapping result;
    mapping timing;
    string phase;
    string prefix;

    result = ([]);
    foreach (phase in _gateway_movement_phases) {
        timing = mapp(_gateway_movement_phase_timing[phase])
            ? _gateway_movement_phase_timing[phase] : ([]);
        prefix = "gateway_movement_phase_" + phase + "_";
        foreach (string field in GATEWAY_TIMING_FIELD_SUFFIXES)
            result[prefix + field] = timing[field] || 0;
    }
    return result;
}

void record_gateway_combat_phase_timing(object actor, string phase, int wall_ns,
    int cpu_ns)
{
    if (!gateway_actor_probe_ready(actor)
        || member_array(phase, _gateway_combat_phases) == -1
        || !intp(wall_ns) || wall_ns < 0)
        return;

    UTIL_TIMING->accumulate_wall_cpu(_gateway_combat_phase_timing, phase,
        wall_ns / 1000, (!intp(cpu_ns) || cpu_ns < 0) ? -1 : cpu_ns / 1000);
}

mapping query_gateway_combat_phase_timing_status()
{
    mapping result;
    mapping timing;
    string phase;
    string prefix;

    result = ([]);
    foreach (phase in _gateway_combat_phases) {
        timing = mapp(_gateway_combat_phase_timing[phase])
            ? _gateway_combat_phase_timing[phase] : ([]);
        prefix = "gateway_combat_phase_" + phase + "_";
        foreach (string field in GATEWAY_TIMING_FIELD_SUFFIXES)
            result[prefix + field] = timing[field] || 0;
    }
    return result;
}

void record_gateway_progression_phase_timing(object actor, string phase,
    int wall_ns, int cpu_ns)
{
    if (!gateway_actor_probe_ready(actor)
        || member_array(phase, _gateway_progression_phases) == -1
        || !intp(wall_ns) || wall_ns < 0)
        return;

    UTIL_TIMING->accumulate_wall_cpu(_gateway_progression_phase_timing, phase,
        wall_ns / 1000, (!intp(cpu_ns) || cpu_ns < 0) ? -1 : cpu_ns / 1000);
}

mapping query_gateway_progression_phase_timing_status()
{
    mapping result;
    mapping timing;
    string phase;
    string prefix;

    result = ([]);
    foreach (phase in _gateway_progression_phases) {
        timing = mapp(_gateway_progression_phase_timing[phase])
            ? _gateway_progression_phase_timing[phase] : ([]);
        prefix = "gateway_progression_phase_" + phase + "_";
        foreach (string field in GATEWAY_TIMING_FIELD_SUFFIXES)
            result[prefix + field] = timing[field] || 0;
    }
    return result;
}

private void record_gateway_receive_phase_timing(string phase, int elapsed_ns)
{
    if (member_array(phase, _gateway_receive_phases) == -1
        || !intp(elapsed_ns) || elapsed_ns < 0)
        return;
    UTIL_TIMING->accumulate_samples(_gateway_receive_phase_timing, phase,
        elapsed_ns / 1000);
}

mapping query_gateway_receive_phase_timing_status()
{
    mapping result;
    mapping timing;
    string phase;
    string prefix;

    result = ([]);
    foreach (phase in _gateway_receive_phases) {
        timing = mapp(_gateway_receive_phase_timing[phase])
            ? _gateway_receive_phase_timing[phase] : ([]);
        prefix = "gateway_receive_phase_" + phase + "_";
        result[prefix + "samples"] = timing["samples"] || 0;
        result[prefix + "total_us"] = timing["total_us"] || 0;
        result[prefix + "max_us"] = timing["max_us"] || 0;
    }
    return result;
}

void finish_gateway_receive_measurement(object actor, string session_id,
    string input, int elapsed_ns, int session_lookup_elapsed_ns,
    int session_touch_elapsed_ns, int route_ensure_elapsed_ns,
    int input_decode_elapsed_ns, int process_input_elapsed_ns)
{
    int finish_started_at;

    if (!objectp(actor) || !stringp(session_id) || session_id == ""
        || COMMAND_DISPATCH_D->consume_loadtest_measurement_input(actor))
        return;

    finish_started_at = perf_counter_ns();
    record_gateway_receive_phase_timing(
        "session_lookup", session_lookup_elapsed_ns);
    record_gateway_receive_phase_timing(
        "session_touch", session_touch_elapsed_ns);
    record_gateway_receive_phase_timing(
        "route_ensure", route_ensure_elapsed_ns);
    record_gateway_receive_phase_timing(
        "input_decode", input_decode_elapsed_ns);
    record_gateway_receive_phase_timing(
        "process_input", process_input_elapsed_ns);
    record_gateway_probe_sample("gateway_active_receive", elapsed_ns);
    if (stringp(input) && strsrch(trim(input), "say ") == 0)
        record_gateway_probe_sample("gateway_active_chat_receive", elapsed_ns);
    record_gateway_receive_phase_timing(
        "finish_measurement", perf_counter_ns() - finish_started_at);
}

/*
 * The synchronized loadtest command loop is a measurement window. Reset only
 * fixed-cardinality observability aggregates so setup traffic cannot become a
 * command-loop high-water mark.
 */
void reset_gateway_measurement_window()
{
    string key;

    foreach (key in keys(_gateway_active_probe))
        _gateway_active_probe[key] = 0;
    _gateway_command_family_timing = ([]);
    _gateway_trade_consume_command_timing = ([]);
    _gateway_command_dispatch_stage_timing = ([]);
    _gateway_movement_phase_timing = ([]);
    _gateway_combat_phase_timing = ([]);
    _gateway_progression_phase_timing = ([]);
    _gateway_receive_phase_timing = ([]);
}

/* 迁出部分的整体聚合面：_gateway_active_probe + 全部 7 组 timing status。
 * command_dispatch_d::query_status() 展开本函数以维持原 status 全图。 */
mapping query_gateway_timing_status()
{
    return copy(_gateway_active_probe)
        + query_gateway_command_family_timing_status()
        + query_gateway_trade_consume_command_timing_status()
        + query_gateway_command_dispatch_stage_timing_status()
        + query_gateway_movement_phase_timing_status()
        + query_gateway_combat_phase_timing_status()
        + query_gateway_progression_phase_timing_status()
        + query_gateway_receive_phase_timing_status();
}
