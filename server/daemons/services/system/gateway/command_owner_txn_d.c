/**
 * @file    command_owner_txn_d.c
 * @brief   命令 owner 事务边界编排服务
 *
 * 从 command_dispatch_d.c 迁出的 owner 侧命令编排：
 * - record_owner_command_trace：命令前 owner 归属标记与意图记录
 * - begin_/finish_owner_command_transaction：commit boundary 事务生命周期
 * - query_dispatch_owner_route：分发 owner 路由查询（ownercheck 消费）
 *
 * 职责边界：
 * - 不持有第二份真状态；owner 真源仍在 OWNER_RUNTIME_D / OWNER_COMMIT_D /
 *   OWNER_GAMEPLAY_D，本服务只做命令周期的编排门面。
 * - 压测输入标记装置（loadtest measurement markers）留在
 *   command_dispatch_d.c，与本服务无关。
 */

#pragma strict_types
#pragma save_types

#include <system/globals.h>
#include <system/services.h>

inherit "/lib/framework/daemon";

#define WORKER_DRIVER_ADAPTER "/daemons/services/system/multicore/worker_driver_adapter"
#define OWNER_TXN_TEST_FAILURE_KEY "task15/fault_injection/"

nosave int _operation_sequence;

private mapping failure_result(string stage, string reason, string operation_id,
    mixed error)
{
    return ([ "ok": 0, "required": 1,
        "failure_stage": stringp(stage) ? stage : "unknown",
        "reason": stringp(reason) ? reason : "owner transaction failure",
        "operation_id": stringp(operation_id) ? operation_id : "",
        "error": error,
        "retry_required": 1,
        "direct_cross_owner_write": 0,
    ]);
}

/* These hooks consume only an already armed in-memory marker. No command or
 * admin path arms it; the isolated test harness may seed the marker while
 * constructing a test actor. Production input therefore cannot reach it. */
private int arm_test_failure(mixed target, string stage)
{
    if (!stringp(stage) || member_array(stage,
        ({ "trace", "intent", "prepare", "finalize" })) == -1)
        return 0;
    if (objectp(target)) {
        target->set_temp(OWNER_TXN_TEST_FAILURE_KEY + stage, 1);
        return 1;
    }
    if (mapp(target)) {
        target["test_failure_stage"] = stage;
        return 1;
    }
    return 0;
}

private int consume_test_failure(mixed target, string stage)
{
    mixed armed;

    if (!stringp(stage))
        return 0;
    if (objectp(target)) {
        armed = target->query_temp(OWNER_TXN_TEST_FAILURE_KEY + stage);
        if (armed == 1) {
            target->delete_temp(OWNER_TXN_TEST_FAILURE_KEY + stage);
            return 1;
        }
        return 0;
    }
    if (mapp(target) && target["test_failure_stage"] == stage) {
        map_delete(target, "test_failure_stage");
        return 1;
    }
    return 0;
}

void create()
{
    ::create();
    _operation_sequence = 0;
}

mapping record_owner_command_trace(object actor, string verb, string args,
    string operation_id)
{
    object adapter;
    mapping intent_result;
    string player_id;
    string owner_id;
    mixed err;
    int set_owner_id_result;
    int record_owner_task_result;

    if (!objectp(actor) || !stringp(verb) || verb == "")
        return failure_result("trace", "actor and verb are required", operation_id, 0);
    if (!stringp(operation_id) || operation_id == "") {
        _operation_sequence++;
        operation_id = sprintf("command/%d/%d", time(), _operation_sequence);
    }
    if (consume_test_failure(actor, "trace"))
        return failure_result("trace", "fault injection reached", operation_id, 0);

    err = catch(adapter = load_object(WORKER_DRIVER_ADAPTER));
    if (err || !objectp(adapter))
        return failure_result("trace", "driver adapter unavailable", operation_id, err);

    player_id = get_object_id(actor);
    if (!stringp(player_id) || player_id == "")
        player_id = geteuid(actor) || file_name(actor);
    owner_id = "owner/player/" + player_id;
    err = catch(set_owner_id_result = adapter->set_owner_id(actor, owner_id));
    if (err || !set_owner_id_result)
        return failure_result("trace", "driver owner assignment failed", operation_id, err);
    err = catch(record_owner_task_result = adapter->record_owner_task(owner_id, "command", verb,
    adapter->query_owner_epoch(actor), "observed"));
    if (err || !record_owner_task_result)
        return failure_result("trace", "driver owner trace failed", operation_id, err);
    if (consume_test_failure(actor, "intent"))
        return failure_result("intent", "fault injection reached", operation_id, 0);
    if (file_size(OWNER_GAMEPLAY_D + ".c") <= 0)
        return failure_result("intent", "owner gameplay service unavailable", operation_id, 0);
    err = catch(intent_result = OWNER_GAMEPLAY_D->record_compact_dispatch_intent(actor, verb,
    stringp(args) ? args : ""));
    if (err || !mapp(intent_result) || intent_result["ok"] != 1) {
        log_file("system/owner_trace", sprintf("[%s] command trace failed: %O %O\n",
            ctime(time())[4..19], err, intent_result));
        return failure_result("intent", "command trace failed", operation_id,
            ([ "error": err, "intent": intent_result ]));
    }
    return ([
        "ok": 1,
        "owner_id": owner_id,
        "operation_id": operation_id,
        "intent": intent_result,
        "direct_cross_owner_write": 0,
    ]);
}

mapping begin_owner_command_transaction(object actor, mapping parsed, mapping owner_trace)
{
    mapping intent_result;
    mapping intent;
    mapping created;
    mapping prepared;
    mapping payload;
    string idempotency_key;
    string operation_id;
    mixed err;

    operation_id = mapp(owner_trace) ? owner_trace["operation_id"] : "";
    if (!mapp(owner_trace) || owner_trace["ok"] != 1 || !mapp(owner_trace["intent"]))
        return failure_result(mapp(owner_trace) ? owner_trace["failure_stage"] : "trace",
            mapp(owner_trace) && owner_trace["reason"]
                ? owner_trace["reason"] : "owner intent unavailable",
            operation_id, mapp(owner_trace) ? owner_trace["error"] : 0);
    intent_result = owner_trace["intent"];
    if (!mapp(intent_result) || intent_result["ok"] != 1 || !mapp(intent_result["intent"]))
        return failure_result("intent", "command has no owner gameplay contract",
            operation_id, intent_result);
    intent = intent_result["intent"];
    if (!stringp(intent["registered_task_method"])
        || intent["registered_task_method"] == "")
        return failure_result("intent", "owner task is not registered",
            operation_id, intent);
    if (intent["requires_commit_boundary"] != 1)
        return ([ "ok": 1, "commit_required": 0,
            "intent_id": intent["id"], "operation_id": operation_id ]);
    if (!stringp(operation_id) || operation_id == "") {
        _operation_sequence++;
        operation_id = sprintf("command/%d/%d", time(), _operation_sequence);
    }
    if (consume_test_failure(actor, "prepare"))
        return failure_result("prepare", "fault injection reached", operation_id, 0);

    idempotency_key = "command/" + operation_id;
    payload = ([
        "verb": parsed["cmd"],
        "args": parsed["args"],
        "actor": file_name(actor),
        "owner_id": owner_trace["owner_id"],
        "intent_id": intent["id"],
        "operation_id": operation_id,
        "execution_mode": intent["execution_mode"],
    ]);
    err = catch(created = OWNER_GAMEPLAY_D->create_compact_gameplay_transaction(intent["domain"],
        idempotency_key, intent["owners"], payload));
    if (err || !mapp(created) || created["ok"] != 1 || !intp(created["transaction_id"]))
        return failure_result("prepare", "owner gameplay transaction create failed",
            operation_id, ([ "error": err, "result": created ]));

    err = catch(prepared = OWNER_COMMIT_D->prepare_transaction(created["transaction_id"]));
    if (err || !mapp(prepared) || prepared["ok"] != 1) {
        catch(OWNER_COMMIT_D->abort_transaction(created["transaction_id"],
            "owner command transaction prepare failed"));
        return failure_result("prepare", "owner command transaction prepare failed",
            operation_id, ([ "error": err, "result": prepared,
                "transaction_id": created["transaction_id"] ]));
    }

    return ([
        "ok": 1,
        "required": 1,
        "transaction_id": created["transaction_id"],
        "operation_id": operation_id,
        "direct_cross_owner_write": 0,
    ]);
}

mapping finish_owner_command_transaction(mapping command_txn, int command_ok, mixed command_error)
{
    mapping finalized;
    mapping transaction_record;
    mapping wal_result;
    string outcome;
    mixed err;
    string operation_id;
    string wal_operation_id;

    operation_id = mapp(command_txn) ? command_txn["operation_id"] : "";
    if (!mapp(command_txn) || (command_txn["required"] != 1 && command_txn["commit_required"] != 1))
        return ([ "ok": 1, "state": "not_required", "compensation_required": 0,
            "operation_id": operation_id ]);
    if (!intp(command_txn["transaction_id"]))
        return failure_result("finalize", "transaction id is missing", operation_id,
            "TRANSACTION_ID_MISSING") + ([
                "state": "invalid_transaction",
                "compensation_required": 1,
                "error_code": "TRANSACTION_ID_MISSING",
            ]);
    if (consume_test_failure(command_txn, "finalize")) {
        catch(OWNER_COMMIT_D->abort_transaction(command_txn["transaction_id"],
            "owner command transaction finalize fault injection"));
        return failure_result("finalize", "fault injection reached", operation_id, 0) + ([
            "state": "finalize_failed",
            "compensation_required": 1,
            "persisted": 0,
            "error_code": "OWNER_TRANSACTION_FINALIZE_FAILED",
        ]);
    }

    if (command_ok)
        err = catch(finalized = OWNER_COMMIT_D->commit_transaction(command_txn["transaction_id"]));
    else
        err = catch(finalized = OWNER_COMMIT_D->rollback_transaction(command_txn["transaction_id"],
            command_error ? sprintf("%O", command_error) : "command returned false"));
    if (err || !mapp(finalized) || finalized["ok"] != 1) {
        catch(OWNER_COMMIT_D->abort_transaction(command_txn["transaction_id"],
            "owner command transaction finalize failed"));
        log_file("system/owner_trace", sprintf("[%s] command transaction finalize failed: txn=%d err=%O result=%O\n",
            ctime(time())[4..19], command_txn["transaction_id"], err, finalized));
        return ([
            "ok": 0,
            "state": "finalize_failed",
            "compensation_required": 1,
            "persisted": 0,
            "error_code": "OWNER_TRANSACTION_FINALIZE_FAILED",
            "error": err,
            "result": finalized,
        ]);
    }

    outcome = command_ok ? "committed" : "rolled_back";
    transaction_record = OWNER_COMMIT_D->query_transaction(command_txn["transaction_id"]);
    /* Transaction ids are process-local; the WAL identity must survive a
     * driver restart without colliding with an older terminal record. */
    if (!mapp(transaction_record)
        || !stringp(transaction_record["idempotency_key"])
        || transaction_record["idempotency_key"] == "") {
        log_file("system/owner_trace", sprintf(
            "[%s] command transaction WAL identity missing: txn=%d record=%O\n",
            ctime(time())[4..19], command_txn["transaction_id"], transaction_record));
        return ([
            "ok": 0,
            "state": "persist_failed",
            "compensation_required": 0,
            "retry_required": 0,
            "persisted": 0,
            "error_code": "OWNER_TRANSACTION_WAL_IDENTITY_MISSING",
            "result": finalized,
        ]);
    }
    wal_operation_id = sprintf(
        "owner-command/%s/%s", transaction_record["idempotency_key"], outcome);
    wal_result = OWNER_PERSISTENCE_D->persist_wal_state(
        wal_operation_id, "owner_commit", ([
            "transaction_id": command_txn["transaction_id"],
            "outcome": outcome,
            "terminal_state": mapp(transaction_record) ? transaction_record["state"] : "",
            "updated_at": mapp(transaction_record) ? transaction_record["updated_at"] : time(),
        ]));
    if (!mapp(wal_result) || wal_result["ok"] != 1 || wal_result["persisted"] != 1) {
        log_file("system/owner_trace",
        sprintf("[%s] command transaction WAL persistence failed: txn=%d result=%O\n",
            ctime(time())[4..19], command_txn["transaction_id"], wal_result));
        return ([
            "ok": 0,
            "state": "persist_failed",
            "compensation_required": 0,
            "retry_required": 1,
            "persisted": 0,
            "error_code": "OWNER_TRANSACTION_WAL_PERSIST_FAILED",
            "retry_token": mapp(wal_result) ? wal_result["retry_token"] : "",
            "result": finalized,
            "wal": wal_result,
        ]);
    }
    return ([
        "ok": 1,
        "state": outcome,
        "compensation_required": 0,
        "persisted": 1,
        "error_code": "",
        "wal": wal_result,
    ]);
}

mapping query_dispatch_owner_route(object actor, string verb)
{
    mapping route;
    string player_id;
    string route_key;
    mixed err;

    if (!objectp(actor))
        return ([ "ok": 0, "reason": "actor is required" ]);

    player_id = get_object_id(actor);
    if (!stringp(player_id) || player_id == "")
        player_id = geteuid(actor) || file_name(actor);
    route_key = player_id + ":" + (stringp(verb) && verb != "" ? verb : "command");
    err = catch(route = OWNER_RUNTIME_D->query_service_owner_route("command_dispatch", route_key));
    if (err || !mapp(route) || route["ok"] != 1)
        return ([
            "ok": 0,
            "service": "command_dispatch",
            "key": route_key,
            "player_id": player_id,
            "failure_reason": err ? sprintf("owner route query failed: %O", err)
                : (mapp(route) ? route["failure_reason"] : "owner route unavailable"),
            "readiness": mapp(route) ? route["readiness"] : ([ "ok": 0 ]),
            "dependencies": mapp(route) ? route["dependencies"] : ([ ]),
            "direct_cross_owner_write": 0,
        ]);

    route["player_id"] = player_id;
    route["player_owner_id"] = "owner/player/" + player_id;
    route["command"] = stringp(verb) ? verb : "";
    route["command_gateway_fifo_required"] = 1;
    route["direct_cross_owner_write"] = 0;
    return route;
}
