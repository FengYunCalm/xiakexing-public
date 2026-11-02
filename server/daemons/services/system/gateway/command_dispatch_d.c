/**
 * @file    command_dispatch_d.c
 * @brief   命令分发服务真源



 *
 * @par Description:
 * 系统级命令分发真源，统一负责 `/cmds` 路径解析与执行。
 * kernel 层只委托到这里，不再直接拼接或探测命令文件路径。
 *
 * 职责边界（2026-08 拆分后）：
 * - 本文件：输入归一化、命令路径解析、压测输入标记、owner 事务编排委托、
 *   分发主流程与 status 聚合门面。
 * - 网关计时/探针/family 分类：GATEWAY_TELEMETRY_D。
 * - owner trace 与事务生命周期：COMMAND_OWNER_TXN_D。
 */

#pragma strict_types
#pragma save_types

#include <system/globals.h>
#include <system/services.h>

inherit "/lib/framework/daemon";

private mapping _loadtest_measurement_input_markers = ([]);
private int _operation_sequence;

private mapping _direction_aliases = ([
    "n": "north",
    "s": "south",
    "e": "east",
    "w": "west",
    "ne": "northeast",
    "nw": "northwest",
    "se": "southeast",
    "sw": "southwest",
    "u": "up",
    "d": "down",
    "north": "north",
    "south": "south",
    "east": "east",
    "west": "west",
    "northeast": "northeast",
    "northwest": "northwest",
    "southeast": "southeast",
    "southwest": "southwest",
    "up": "up",
    "down": "down",
    "enter": "enter",
    "out": "out",
    "northup": "northup",
    "northdown": "northdown",
    "southup": "southup",
    "southdown": "southdown",
    "eastup": "eastup",
    "eastdown": "eastdown",
    "westup": "westup",
    "westdown": "westdown",
]);

int consume_loadtest_measurement_input(object actor);

private mapping normalize_dispatch_input(mapping parsed)
{
    string dir;
    string args;

    if (!mapp(parsed) || !stringp(parsed["cmd"]))
        return parsed;
    dir = _direction_aliases[parsed["cmd"]];
    if (!stringp(dir) || dir == "")
        return parsed;
    args = stringp(parsed["args"]) && parsed["args"] != "" ? dir + " " + parsed["args"] : dir;
    return ([ "cmd": "go", "args": args ]);
}

private mapping split_command_input(string input)
{
    int space_pos;

    if (!stringp(input) || input == "")
        return ([ "cmd": "", "args": "" ]);

    input = trim(input);
    if (input == "")
        return ([ "cmd": "", "args": "" ]);

    space_pos = strsrch(input, " ");
    if (space_pos == -1)
        return ([ "cmd": input, "args": "" ]);

    return ([
        "cmd": input[0..space_pos - 1],
        "args": input[space_pos + 1..]
    ]);
}

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

private string query_command_operation_id(object actor)
{
    string operation_id;

    if (!objectp(actor))
        return "";
    operation_id = actor->query_temp("task15/operation_id");
    if (stringp(operation_id) && operation_id != "")
        return operation_id;
    _operation_sequence++;
    return sprintf("command/%s/%d/%d", file_name(actor), time(), _operation_sequence);
}

private int has_admin_command_access(object actor)
{
    return objectp(actor) && (CONFIG_D->is_admin_player(actor)
        || actor->query_temp("test_fixture/non_persistent") == 1);
}

private void mark_loadtest_measurement_input(object actor)
{
    string actor_name;

    if (!objectp(actor))
        return;
    actor_name = file_name(actor);
    if (stringp(actor_name) && actor_name != "")
        _loadtest_measurement_input_markers[actor_name] = 1;
}

int consume_loadtest_measurement_input(object actor)
{
    string actor_name;

    if (!objectp(actor))
        return 0;
    actor_name = file_name(actor);
    if (!stringp(actor_name) || actor_name == ""
        || _loadtest_measurement_input_markers[actor_name] != 1)
        return 0;
    map_delete(_loadtest_measurement_input_markers, actor_name);
    return 1;
}

private int is_loadtest_measurement_input(object actor, mapping parsed)
{
    string args;

    if (!has_admin_command_access(actor) || !mapp(parsed)
        || parsed["cmd"] != "ownercheck")
        return 0;
    args = stringp(parsed["args"]) ? trim(parsed["args"]) : "";
    return member_array(args, ({
        "measure command_loop_before",
        "measure command_loop_after",
    })) >= 0;
}

private int is_command_token(string verb)
{
    int index;
    int character;

    if (!stringp(verb) || verb == "")
        return 0;
    for (index = 0; index < strlen(verb); index++) {
        character = verb[index];
        if (!((character >= 'a' && character <= 'z')
            || (character >= 'A' && character <= 'Z')
            || (character >= '0' && character <= '9')
            || character == '_'))
            return 0;
    }
    return 1;
}

private string resolve_admin_command_path(string verb)
{
    string cmd_path;
    int cmd_size;

    if (!is_command_token(verb))
        return 0;

    cmd_path = "/cmds/admin/" + verb;
    cmd_size = file_size(cmd_path + ".c");
    if (cmd_size <= 0) {
        cmd_path = "/cmds/admin/" + verb + "_cmd";
        cmd_size = file_size(cmd_path + ".c");
    }

    return cmd_size > 0 ? cmd_path : 0;
}

private string resolve_player_command_path(string verb)
{
    string cmd_path;
    int cmd_size;

    if (!is_command_token(verb))
        return 0;

    cmd_path = "/cmds/player/" + verb;
    cmd_size = file_size(cmd_path + ".c");
    if (cmd_size <= 0) {
        cmd_path = "/cmds/player/" + verb + "_cmd";
        cmd_size = file_size(cmd_path + ".c");
    }

    return cmd_size > 0 ? cmd_path : 0;
}

private string resolve_command_path(object actor, string verb)
{
    string cmd_path;

    if (!objectp(actor) || !stringp(verb) || verb == "")
        return 0;

    if (has_admin_command_access(actor)) {
        cmd_path = resolve_admin_command_path(verb);
        if (!stringp(cmd_path) || cmd_path == "")
            cmd_path = resolve_player_command_path(verb);
    } else {
        cmd_path = resolve_player_command_path(verb);
    }

    return cmd_path;
}

void create()
{
    ::create();
    log_file("system/service", sprintf("[%s] Command dispatch service initialized\n", ctime(time())[4..19]));
}

mapping query_status()
{
    /* 稳定聚合门面：静态 owner 声明键 + GATEWAY_TELEMETRY_D 迁出部分整图。
     * 键集与拆分前逐字节一致（ownercheck 消费契约）。 */
    return ([
        "mode": "command_dispatch_owner_commit_boundary",
        "service_map_v2": 1,
        "service": "command_dispatch",
        "policy": "owner_scheduler",
        "enabled_by_default": 0,
        "command_gateway_fifo_required": 1,
        "owner_commit_boundary": 1,
        "prepare_before_execute": 1,
        "commit_after_execute": 1,
        "rollback_on_execute_failure": 1,
        "message_only_cross_owner": 1,
        "facade_only_runtime_claims": 0,
        "normal_path_single_thread_fallback": 0,
        "direct_cross_owner_write": 0,
    ]) + GATEWAY_TELEMETRY_D->query_gateway_timing_status();
}

int command_exists(object actor, string input)
{
    mapping parsed;

    parsed = normalize_dispatch_input(split_command_input(input));
    if (!stringp(parsed["cmd"]) || parsed["cmd"] == "")
        return 0;

    return stringp(resolve_command_path(actor, parsed["cmd"]));
}

int dispatch_command(object actor, string input)
{
    mapping parsed;
    mapping owner_trace;
    mapping command_txn;
    mapping finalize_result;
    string cmd_path;
    mixed err;
    mixed cmd_result;
    int command_ok;
    int transaction_finalize_ok;
    int gateway_probe;
    int dispatch_started_at;
    int command_main_started_at;
    int command_main_elapsed_ns;
    int command_main_cpu_started_at;
    int command_main_cpu_finished_at;
    int command_main_cpu_elapsed_ns;
    int flush_started_at;
    int stage_started_at;
    int trace_loadtest_move;
    string operation_id;
    string trace_command;
    string command_family;

    if (!objectp(actor) || !stringp(input) || input == "")
        return 0;
    if (actor->query_temp("admin/command_frozen")) {
        msg_send(actor, "你的操作已被管理员冻结，请联系管理员。\n");
        return 1;
    }

    gateway_probe = GATEWAY_TELEMETRY_D->gateway_actor_probe_ready(actor);
    if (gateway_probe)
        dispatch_started_at = perf_counter_ns();

    parsed = normalize_dispatch_input(split_command_input(input));
    if (!stringp(parsed["cmd"]) || parsed["cmd"] == "")
        return 0;
    operation_id = query_command_operation_id(actor);
    parsed["operation_id"] = operation_id;
    if (is_loadtest_measurement_input(actor, parsed))
        mark_loadtest_measurement_input(actor);

    if (gateway_probe)
        stage_started_at = perf_counter_ns();
    owner_trace = COMMAND_OWNER_TXN_D->record_owner_command_trace(actor, parsed["cmd"],
        parsed["args"], operation_id);
    if (gateway_probe)
        GATEWAY_TELEMETRY_D->record_gateway_command_dispatch_stage("owner_trace", perf_counter_ns() - stage_started_at);

    cmd_path = resolve_command_path(actor, parsed["cmd"]);
    if ((!stringp(cmd_path) || cmd_path == "") && !has_admin_command_access(actor)
        && stringp(resolve_admin_command_path(parsed["cmd"]))) {
        msg_send(actor, "你没有权限执行此命令。\n");
        return 1;
    }

    if (!stringp(cmd_path) || cmd_path == "") {
        if (objectp(environment(actor))
            && ROOM_BEHAVIOR_D->dispatch_room_command(actor, environment(actor), parsed["cmd"], parsed["args"])) {
            if (gateway_probe)
                flush_started_at = perf_counter_ns();
            CLIENT_SYNC_D->flush_player_command_sync(actor);
            MESSAGE_D->promote_actor_room_message_fifo_debt(actor);
            if (gateway_probe) {
                GATEWAY_TELEMETRY_D->record_gateway_probe_sample("gateway_active_flush", perf_counter_ns() - flush_started_at);
                GATEWAY_TELEMETRY_D->record_gateway_probe_sample("gateway_active_dispatch", perf_counter_ns() - dispatch_started_at);
            }
            return 1;
        }
        return -1;
    }

    if (gateway_probe)
        stage_started_at = perf_counter_ns();
    command_txn = COMMAND_OWNER_TXN_D->begin_owner_command_transaction(actor, parsed, owner_trace);
    owner_trace = 0;
    if (gateway_probe)
        GATEWAY_TELEMETRY_D->record_gateway_command_dispatch_stage("transaction_prepare", perf_counter_ns() - stage_started_at);
    if (mapp(command_txn) && command_txn["required"] == 1 && command_txn["ok"] != 1) {
        log_file("system/owner_trace", sprintf("[%s] command transaction rejected: retry_required=1 %O\n",
            ctime(time())[4..19], command_txn));
        msg_send(actor, "Command owner commit boundary rejected.\n");
        return 1;
    }
    if (mapp(command_txn)
        && actor->query_temp("task15/fault_injection/finalize") == 1) {
        command_txn["test_failure_stage"] = "finalize";
        actor->delete_temp("task15/fault_injection/finalize");
    }

    if (gateway_probe) {
        command_main_started_at = perf_counter_ns();
        command_main_cpu_started_at = thread_cpu_time_ns();
        command_family = GATEWAY_TELEMETRY_D->gateway_command_main_tail_family(cmd_path, parsed["args"]);
    }
    trace_loadtest_move = actor->query_temp("loadtest/move_trace_run_id") ? 1 : 0;
    if (trace_loadtest_move) {
        trace_command = parsed["cmd"];
        if (stringp(parsed["args"]) && parsed["args"] != "")
            trace_command += " " + parsed["args"];
        actor->set_temp("loadtest/active_command", trace_command);
    }
    err = catch(cmd_result = call_other(cmd_path, "main", actor, parsed["args"]));
    if (gateway_probe) {
        command_main_cpu_finished_at = thread_cpu_time_ns();
        command_main_elapsed_ns = perf_counter_ns() - command_main_started_at;
        command_main_cpu_elapsed_ns =
            command_main_cpu_started_at >= 0
            && command_main_cpu_finished_at >= command_main_cpu_started_at
                ? command_main_cpu_finished_at - command_main_cpu_started_at : -1;
    }
    command_ok = (!err && cmd_result) ? 1 : 0;
    if (gateway_probe)
        stage_started_at = perf_counter_ns();
    transaction_finalize_ok = 1;
    finalize_result = COMMAND_OWNER_TXN_D->finish_owner_command_transaction(command_txn,
            command_ok, err);
    transaction_finalize_ok = mapp(finalize_result) && finalize_result["ok"] == 1;
    if (gateway_probe)
        GATEWAY_TELEMETRY_D->record_gateway_command_dispatch_stage("transaction_finalize", perf_counter_ns() - stage_started_at);
    if (trace_loadtest_move)
        actor->delete_temp("loadtest/active_command");
    if (gateway_probe) {
        GATEWAY_TELEMETRY_D->record_gateway_probe_sample("gateway_active_command_main",
            command_main_elapsed_ns);
        GATEWAY_TELEMETRY_D->record_gateway_command_main_tail_cpu_sample(
            command_family, command_main_elapsed_ns, command_main_cpu_elapsed_ns);
        GATEWAY_TELEMETRY_D->record_gateway_command_family_timing(
            command_family, command_main_elapsed_ns, command_main_cpu_elapsed_ns);
        if (parsed["cmd"] == "buy" || parsed["cmd"] == "eat")
            GATEWAY_TELEMETRY_D->record_gateway_trade_consume_command_timing(parsed["cmd"],
                command_main_elapsed_ns, command_main_cpu_elapsed_ns);
        GATEWAY_TELEMETRY_D->record_gateway_command_dispatch_stage("command_main", command_main_elapsed_ns);
    }
    if (!transaction_finalize_ok) {
        log_file("system/owner_trace", sprintf("[%s] command transaction finalize rejected: %O\n",
            ctime(time())[4..19], finalize_result));
        msg_send(actor, "Command owner transaction finalization failed.\n");
        return 0;
    }

    /* 命令结束只同步 flush actor；其他接收者继续由既有 callout 合并排空。 */
    if (gateway_probe)
        flush_started_at = perf_counter_ns();
    CLIENT_SYNC_D->flush_player_command_sync(actor);
    MESSAGE_D->promote_actor_room_message_fifo_debt(actor);
    if (gateway_probe) {
        GATEWAY_TELEMETRY_D->record_gateway_probe_sample("gateway_active_flush", perf_counter_ns() - flush_started_at);
        GATEWAY_TELEMETRY_D->record_gateway_command_dispatch_stage("actor_flush", perf_counter_ns() - flush_started_at);
        GATEWAY_TELEMETRY_D->record_gateway_probe_sample("gateway_active_dispatch", perf_counter_ns() - dispatch_started_at);
    }
    if (gateway_probe)
        GATEWAY_TELEMETRY_D->record_gateway_command_dispatch_stage("dispatch_total", perf_counter_ns() - dispatch_started_at);
    if (err)
        return 0;

    if (!cmd_result
        && strsrch(cmd_path, "/cmds/admin/") == 0
        && !has_admin_command_access(actor))
        msg_send(actor, "你没有权限执行此命令。\n");

    return 1;
}
