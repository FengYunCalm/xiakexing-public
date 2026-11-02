/**
 * @file    owner_commit_d.c
 * @brief   跨 owner 提交边界协调器
 *
 * 协调器只负责事务状态机、幂等键、generation/CAS 和提交审计；实际领域
 * 状态由注册的 owner handler 在本地 prepare/apply/rollback。未接入真实
 * handler 的诊断 proposal 只能记录审计，不得被误认为业务 mutation。
 */
#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

#define WORKER_DRIVER_ADAPTER "/daemons/services/system/multicore/worker_driver_adapter"
#define OWNER_DOMAIN_HANDLER_D "/daemons/services/system/multicore/owner_domain_handler_d"
#define MAX_TERMINAL_TRANSACTION_HISTORY 8192

nosave int _next_txn_id;
nosave int _transactions_created_total;
nosave int _transactions_terminal_total;
nosave int _terminal_transaction_evictions;
nosave int _compact_command_transactions_created_total;
nosave int _compact_command_prepare_records_total;
nosave int _compact_command_apply_records_total;
nosave int _compact_command_rollback_records_total;
nosave mapping _transactions;
nosave mapping _idempotency_index;
nosave mapping _terminal_transaction_order;
nosave int _terminal_transaction_order_head;
nosave int _terminal_transaction_order_tail;
nosave mapping _domain_counts;
nosave mapping _domain_handlers;
nosave mapping _domain_prepare_log;
nosave mapping _domain_apply_log;
nosave mapping _domain_rollback_log;
nosave mapping _domain_apply_counts;
/* Owner generation registry is metadata only; each domain still owns the
 * actual value and must update this watermark after its local commit. */
nosave mapping _owner_generations;

int register_domain_handler(string domain, object handler, string prepare_fn, string apply_fn, string rollback_fn);
mapping abort_transaction(int txn_id, string reason);
int set_owner_generation(string owner_id, int generation);
int query_owner_generation(string owner_id);
private void register_default_domain_handlers();
private void register_real_domain_handlers();
private mapping freeze_payload(mapping payload);
private mapping with_boundary_default_generations(mapping payload, string *owners);

void create()
{
    string domain;

    ::create();
    _next_txn_id = 1;
    _transactions_created_total = 0;
    _transactions_terminal_total = 0;
    _terminal_transaction_evictions = 0;
    _compact_command_transactions_created_total = 0;
    _compact_command_prepare_records_total = 0;
    _compact_command_apply_records_total = 0;
    _compact_command_rollback_records_total = 0;
    _transactions = ([]);
    _idempotency_index = ([]);
    _terminal_transaction_order = ([]);
    _terminal_transaction_order_head = 1;
    _terminal_transaction_order_tail = 1;
    _domain_counts = ([
        "move": 0,
        "item": 0,
        "economy": 0,
        "combat": 0,
        "mail": 0,
        "reward": 0,
        "trade": 0,
        "world": 0,
        "persistence": 0,
        "player": 0,
        "room": 0,
        "session": 0,
        "team": 0,
        "guild": 0,
        "sect": 0,
        "quest": 0,
        "rank": 0,
        "crafting": 0,
        "life_skill": 0,
        "idle": 0,
        "thievery": 0,
    ]);
    _domain_handlers = ([]);
    _domain_prepare_log = ([]);
    _domain_apply_log = ([]);
    _domain_rollback_log = ([]);
    _domain_apply_counts = ([]);
    _owner_generations = ([]);
    foreach (domain in keys(_domain_counts))
        _domain_apply_counts[domain] = 0;
    register_default_domain_handlers();
    register_real_domain_handlers();
}

private object query_adapter()
{
    return find_object(WORKER_DRIVER_ADAPTER) || load_object(WORKER_DRIVER_ADAPTER);
}

private int is_valid_transaction_state(string state)
{
    return member_array(state, ({
        "created",
        "prepared",
        "committing",
        "recovery_pending",
        "committed",
        "aborting",
        "aborted",
        "finalized",
    })) != -1;
}

private int is_terminal_transaction_state(string state)
{
    return member_array(state, ({ "committed", "aborted", "finalized" })) != -1;
}

private int is_valid_transaction_transition(string current_state, string next_state)
{
    if (current_state == next_state)
        return 1;

    switch (current_state) {
        case "created":
            return member_array(next_state, ({ "prepared", "aborting" })) != -1;
        case "prepared":
            return member_array(next_state, ({ "committing", "aborting" })) != -1;
        case "committing":
            return member_array(next_state, ({ "recovery_pending", "committed", "aborting" })) != -1;
        case "recovery_pending":
            return next_state == "committed";
        case "aborting":
            return next_state == "aborted";
        case "committed":
        case "aborted":
            return next_state == "finalized";
    }
    return 0;
}

private mapping build_handler_transaction(mapping txn)
{
    if (!mapp(txn))
        return ([]);

    return ([
        "id": txn["id"],
        "operation": txn["operation"],
        "domain": txn["domain"],
        "idempotency_key": txn["idempotency_key"],
        "owners": arrayp(txn["owners"]) ? txn["owners"] + ({}) : ({}),
        "payload": mapp(txn["payload"]) ? freeze_payload(txn["payload"]) : ([]),
        "expected_generation": txn["expected_generation"],
        "expected_generations": mapp(txn["expected_generations"])
            ? copy(txn["expected_generations"]) : ([]),
        "schema_version": txn["schema_version"],
        "command_id": txn["command_id"],
        "causation_id": txn["causation_id"],
        "source_owner": txn["source_owner"],
        "target_owner": txn["target_owner"],
        "state": txn["state"],
        "compact_command": txn["compact_command"] ? 1 : 0,
        "created_at": txn["created_at"],
        "updated_at": txn["updated_at"],
        "prepare_required": txn["prepare_required"],
        "commit_boundary_only": txn["commit_boundary_only"],
        "rollback_supported": txn["rollback_supported"],
        "idempotent": txn["idempotent"],
        "direct_cross_owner_write": 0,
    ]);
}

private mapping build_compact_transaction_handle(mapping txn)
{
    if (!mapp(txn))
        return ([]);

    return ([
        "transaction_id": txn["id"],
        "state": txn["state"],
        "domain": txn["domain"],
        "compact_command": txn["compact_command"] ? 1 : 0,
        "prepare_required": txn["prepare_required"],
        "commit_boundary_only": txn["commit_boundary_only"],
        "rollback_supported": txn["rollback_supported"],
        "idempotent": txn["idempotent"],
        "recovery_required": txn["recovery_required"] ? 1 : 0,
        "pending_recovery": txn["pending_recovery"] ? 1 : 0,
        "direct_cross_owner_write": 0,
    ]);
}

private mapping freeze_payload(mapping payload)
{
    mixed frozen;
    mixed err;

    if (!mapp(payload))
        return ([]);
    err = catch(frozen = restore_variable(save_variable(payload)));
    return !err && mapp(frozen) ? frozen : 0;
}

private string canonical_value(mixed value)
{
    mixed *array_values;
    string *map_keys;
    string *parts;
    string key;
    int index;

    if (mapp(value)) {
        map_keys = sort_array(keys(value), 1);
        parts = ({ });
        foreach (key in map_keys)
            parts += ({ sprintf("%O:%s", key, canonical_value(value[key])) });
        return "M{" + implode(parts, ",") + "}";
    }
    if (arrayp(value)) {
        array_values = value;
        parts = ({ });
        for (index = 0; index < sizeof(array_values); index++)
            parts += ({ canonical_value(array_values[index]) });
        return "A[" + implode(parts, ",") + "]";
    }
    return sprintf("%O", value);
}

private string canonical_payload(mapping payload)
{
    mapping frozen;

    frozen = freeze_payload(payload);
    return mapp(frozen) ? canonical_value(frozen) : "<invalid_payload>";
}

private string proposal_fingerprint(string operation, string domain, string *owners,
    mapping payload)
{
    return sprintf("%s|%s|%O|%s", operation, stringp(domain) ? domain : "",
        arrayp(owners) ? owners : ({}), canonical_payload(payload));
}

private int proposal_matches_transaction(mapping txn, string operation, string domain,
    string *owners, mapping payload)
{
    string fingerprint;

    if (!mapp(txn) || txn["operation"] != operation)
        return 0;
    fingerprint = proposal_fingerprint(operation, domain, owners, payload);
    if (stringp(txn["proposal_fingerprint"]))
        return txn["proposal_fingerprint"] == fingerprint;
    if (stringp(domain) && domain != "" && txn["domain"] != domain)
        return 0;
    if (sprintf("%O", arrayp(txn["owners"]) ? txn["owners"] : ({})) !=
        sprintf("%O", arrayp(owners) ? owners : ({})))
        return 0;
    return canonical_payload(txn["payload"]) == canonical_payload(payload);
}

private mapping idempotency_conflict(string idempotency_key)
{
    return ([
        "ok": 0,
        "state": "rejected",
        "error_code": "IDEMPOTENCY_CONFLICT",
        "idempotency_key": idempotency_key,
    ]);
}

private mapping validate_owner_set(string *owners)
{
    string owner;

    if (!arrayp(owners) || sizeof(owners) == 0)
        return ([ "ok": 0, "error_code": "OWNER_SET_REQUIRED" ]);
    foreach (owner in owners) {
        if (!stringp(owner) || owner == "")
            return ([ "ok": 0, "error_code": "OWNER_ID_INVALID" ]);
    }
    return ([ "ok": 1 ]);
}

/* payload 未显式声明某 owner 的 expected_generations 时，默认取协调器
 * 当前水位（compact 边界事务的期望值定义上就是“当前已提交水位”；
 * 域事务的真实 CAS 由各自 handler 内的域真源保证——MONEY_D 账本幂等、
 * 位置 CAS、PLAYER_STATE_D 生成代次）。水位未知时物化为 0，保证
 * prepare 可比。调用方显式声明的 expected 值仍做严格校验。 */
private mapping with_boundary_default_generations(mapping payload, string *owners)
{
    mapping expected;
    string owner;
    int watermark;

    if (!mapp(payload) || !arrayp(owners))
        return payload;
    expected = mapp(payload["expected_generations"])
        ? payload["expected_generations"] : ([]);
    foreach (owner in owners) {
        if (!stringp(owner) || owner == "" || !undefinedp(expected[owner]))
            continue;
        watermark = query_owner_generation(owner);
        if (watermark < 0) {
            set_owner_generation(owner, 0);
            watermark = 0;
        }
        expected[owner] = watermark;
    }
    if (sizeof(expected) == 0)
        return payload;
    payload["expected_generations"] = expected;
    return payload;
}

private mapping query_expected_generations(mapping payload, string *owners)
{
    mapping expected;
    string owner;
    mixed value;
    int scalar_expected;

    expected = ([]);
    if (!mapp(payload))
        return expected;
    if (intp(payload["expected_generation"])) {
        scalar_expected = payload["expected_generation"];
        if (arrayp(owners) && sizeof(owners) > 0)
            expected[owners[0]] = scalar_expected;
    }
    if (mapp(payload["expected_generations"])) {
        foreach (owner, value in payload["expected_generations"]) {
            if (stringp(owner) && intp(value))
                expected[owner] = value;
        }
    }
    return expected;
}

private mapping validate_expected_generations(mapping payload, string *owners)
{
    string owner;
    mixed value;

    if (!mapp(payload))
        return ([ "ok": 1 ]);
    if (!undefinedp(payload["expected_generation"])
        && !intp(payload["expected_generation"]))
        return ([ "ok": 0, "error_code": "OWNER_GENERATION_INVALID" ]);
    if (!undefinedp(payload["expected_generations"])) {
        if (!mapp(payload["expected_generations"]))
            return ([ "ok": 0, "error_code": "OWNER_GENERATIONS_INVALID" ]);
        foreach (owner, value in payload["expected_generations"]) {
            if (!stringp(owner) || owner == "" || !intp(value))
                return ([ "ok": 0, "error_code": "OWNER_GENERATIONS_INVALID",
                    "owner_id": owner ]);
        }
    }
    return ([ "ok": 1 ]);
}

private mapping check_expected_generations(mapping txn)
{
    mapping expected;
    string owner;
    int actual;

    expected = mapp(txn) && mapp(txn["expected_generations"])
        ? txn["expected_generations"] : ([]);
    foreach (owner in keys(expected)) {
        if (!intp(_owner_generations[owner]))
            return ([ "ok": 0, "error_code": "OWNER_GENERATION_UNKNOWN",
                "owner_id": owner, "expected_generation": expected[owner] ]);
        actual = _owner_generations[owner];
        if (actual != expected[owner])
            return ([ "ok": 0, "error_code": "OWNER_GENERATION_STALE",
                "owner_id": owner, "expected_generation": expected[owner],
                "actual_generation": actual, "stale": 1 ]);
    }
    return ([ "ok": 1 ]);
}

private mapping build_transaction_result(mapping txn, int duplicate)
{
    mapping result;

    if (!mapp(txn))
        return ([ "ok": 0, "reason": "transaction is required" ]);
    result = ([ "ok": 1, "duplicate": duplicate ? 1 : 0 ]);
    if (txn["compact_command"])
        return result + build_compact_transaction_handle(txn);
    result["transaction"] = copy(txn);
    return result;
}

private mapping build_compact_terminal_transaction(mapping txn)
{
    return ([
        "id": txn["id"],
        "operation": txn["operation"],
        "domain": txn["domain"],
        "idempotency_key": txn["idempotency_key"],
        "proposal_fingerprint": txn["proposal_fingerprint"],
        "state": txn["state"],
        "reason": stringp(txn["reason"]) ? txn["reason"] : "",
        "created_at": txn["created_at"],
        "updated_at": txn["updated_at"],
        "terminal_recorded": 1,
        "compact_command": 1,
        "prepare_succeeded": txn["prepare_succeeded"] ? 1 : 0,
        "apply_succeeded": txn["apply_succeeded"] ? 1 : 0,
        "rollback_succeeded": txn["rollback_succeeded"] ? 1 : 0,
        "recovery_required": txn["recovery_required"] ? 1 : 0,
        "pending_recovery": txn["pending_recovery"] ? 1 : 0,
        "payload_released": 1,
        "handler_results_compacted": 1,
        "prepare_required": 1,
        "commit_boundary_only": 1,
        "rollback_supported": 1,
        "idempotent": 1,
        "direct_cross_owner_write": 0,
    ]);
}

private int is_recovery_result(mapping result)
{
    return mapp(result) && (result["pending_recovery"]
        || result["recovery_required"]);
}

private void delete_domain_commit_record(mapping store, string domain, string idempotency_key)
{
    if (!mapp(store) || !stringp(domain) || !mapp(store[domain]) ||
        !stringp(idempotency_key) || idempotency_key == "")
        return;

    map_delete(store[domain], idempotency_key);
    if (sizeof(store[domain]) == 0)
        map_delete(store, domain);
}

private void prune_terminal_transaction_history()
{
    int evicted_txn_id;
    mapping evicted;
    string domain;
    string idempotency_key;

    while (_terminal_transaction_order_tail - _terminal_transaction_order_head >
        MAX_TERMINAL_TRANSACTION_HISTORY) {
        evicted_txn_id = _terminal_transaction_order[_terminal_transaction_order_head];
        map_delete(_terminal_transaction_order, _terminal_transaction_order_head);
        _terminal_transaction_order_head++;

        evicted = _transactions[evicted_txn_id];
        if (!mapp(evicted))
            continue;
        domain = evicted["domain"];
        idempotency_key = evicted["idempotency_key"];
        if (stringp(idempotency_key) && idempotency_key != "" &&
            intp(_idempotency_index[idempotency_key]) &&
            _idempotency_index[idempotency_key] == evicted_txn_id)
            map_delete(_idempotency_index, idempotency_key);
        delete_domain_commit_record(_domain_prepare_log, domain, idempotency_key);
        delete_domain_commit_record(_domain_apply_log, domain, idempotency_key);
        delete_domain_commit_record(_domain_rollback_log, domain, idempotency_key);
        map_delete(_transactions, evicted_txn_id);
        _terminal_transaction_evictions++;
    }
}

private void retain_terminal_transaction(int txn_id)
{
    if (mapp(_transactions[txn_id]) && _transactions[txn_id]["compact_command"])
        _transactions[txn_id] = build_compact_terminal_transaction(_transactions[txn_id]);
    _terminal_transaction_order[_terminal_transaction_order_tail++] = txn_id;
    _transactions_terminal_total++;
    prune_terminal_transaction_history();
}

private int count_active_transactions()
{
    int active;
    int txn_id;

    foreach (txn_id in keys(_transactions)) {
        if (mapp(_transactions[txn_id]) && !_transactions[txn_id]["terminal_recorded"])
            active++;
    }
    return active;
}

private int count_domain_commit_records(mapping store)
{
    int count;
    string domain;

    if (!mapp(store))
        return 0;
    foreach (domain in keys(store)) {
        if (mapp(store[domain]))
            count += sizeof(store[domain]);
    }
    return count;
}

private int is_valid_transaction_domain(string domain)
{
    return member_array(domain, ({
        "move", "item", "economy", "combat", "mail", "reward", "trade", "world", "persistence",
        "player", "room", "session", "team", "guild", "sect", "quest", "rank", "crafting", "life_skill",
        "idle", "thievery",
    })) != -1;
}

private void register_default_domain_handlers()
{
    /* No business domain may fall back to the coordinator's audit-only
     * proposal logger. Real handlers are registered below or remain missing. */
}

private void register_real_domain_handlers()
{
    object player_state;
    object domain_handler;
    string domain;
    mixed err;

    /* Loading the handler after the coordinator has initialized its default
     * table avoids a recursive create path while still making the player
     * owner the sole writer for real player-state proposals. */
    err = catch(player_state = find_object(PLAYER_STATE_D)
        || load_object(PLAYER_STATE_D));
    if (!err && objectp(player_state)
        && function_exists("prepare_owner_proposal", player_state)
        && function_exists("apply_owner_proposal", player_state)
        && function_exists("rollback_owner_proposal", player_state))
        register_domain_handler("player", player_state,
            "prepare_owner_proposal", "apply_owner_proposal",
            "rollback_owner_proposal");

    /* 其余域统一注册真实 handler 宿主：命令边界/探针事务 skipped，
     * 携带域 mutation schema 的事务由宿主委托各域真源真实执行，
     * 无 schema 的域显式 fail-closed，不再落到纯提交日志骨架。 */
    err = catch(domain_handler = find_object(OWNER_DOMAIN_HANDLER_D)
        || load_object(OWNER_DOMAIN_HANDLER_D));
    if (err || !objectp(domain_handler)
        || !function_exists("prepare_domain_proposal", domain_handler)
        || !function_exists("apply_domain_proposal", domain_handler)
        || !function_exists("rollback_domain_proposal", domain_handler))
        return;
    foreach (domain in keys(_domain_counts)) {
        if (domain == "player")
            continue;
        register_domain_handler(domain, domain_handler,
            "prepare_domain_proposal", "apply_domain_proposal",
            "rollback_domain_proposal");
    }
}

int register_domain_handler(string domain, object handler, string prepare_fn, string apply_fn, string rollback_fn)
{
    if (!is_valid_transaction_domain(domain) || !objectp(handler) ||
        !stringp(apply_fn) || apply_fn == ""
        || !function_exists(apply_fn, handler)
        || (stringp(prepare_fn) && prepare_fn != ""
            && !function_exists(prepare_fn, handler))
        || (stringp(rollback_fn) && rollback_fn != ""
            && !function_exists(rollback_fn, handler)))
        return 0;

    _domain_handlers[domain] = ([
        "domain": domain,
        "handler": handler,
        "prepare_fn": stringp(prepare_fn) && prepare_fn != "" ? prepare_fn : "",
        "apply_fn": apply_fn,
        "rollback_fn": stringp(rollback_fn) && rollback_fn != "" ? rollback_fn : "",
        "registered_at": time(),
    ]);
    return 1;
}

private mixed query_domain_handler(string domain)
{
    if (!is_valid_transaction_domain(domain) || !mapp(_domain_handlers[domain]) ||
        !objectp(_domain_handlers[domain]["handler"]))
        return 0;
    return _domain_handlers[domain];
}

private mapping call_domain_handler(mapping txn, string phase)
{
    mapping handler;
    mapping handler_txn;
    string fn;
    mixed result;
    mixed err;

    if (!mapp(txn) || !stringp(txn["domain"]))
        return ([ "ok": 0, "reason": "transaction domain is required" ]);

    handler = query_domain_handler(txn["domain"]);
    if (!mapp(handler))
        return ([ "ok": 0, "reason": "domain handler missing", "domain": txn["domain"] ]);

    switch (phase) {
        case "prepare":
            fn = handler["prepare_fn"];
            break;
        case "apply":
            fn = handler["apply_fn"];
            break;
        case "rollback":
            fn = handler["rollback_fn"];
            break;
        default:
            return ([ "ok": 0, "reason": "invalid handler phase" ]);
    }

    if (!stringp(fn) || fn == "")
        return ([ "ok": 0, "reason": "domain handler method missing",
            "phase": phase, "domain": txn["domain"] ]);

    /* The built-in handler is read-only and can consume the canonical active
     * transaction. External handlers receive one isolated canonical view,
     * never the nested prepare/apply results accumulated by the coordinator. */
    handler_txn = handler["handler"] == this_object()
        ? txn : build_handler_transaction(txn);
    if (!mapp(handler_txn) || !mapp(handler_txn["payload"]))
        return ([ "ok": 0, "reason": "handler transaction payload invalid",
            "phase": phase, "domain": txn["domain"] ]);
    err = catch(result = call_other(handler["handler"], fn, handler_txn));
    if (err)
        return ([ "ok": 0, "reason": "domain handler failed", "phase": phase,
                  "domain": txn["domain"], "error": err ]);
    if (!mapp(result))
        return ([ "ok": 0, "reason": "domain handler returned non-mapping",
                  "phase": phase, "domain": txn["domain"] ]);
    return result + ([ "phase": phase, "domain": txn["domain"] ]);
}

private mapping build_commit_record(mapping txn, string state)
{
    string source_owner;
    string target_owner;

    source_owner = mapp(txn) && arrayp(txn["owners"]) && sizeof(txn["owners"]) > 0
        && stringp(txn["owners"][0]) ? txn["owners"][0] : "";
    target_owner = mapp(txn) && arrayp(txn["owners"]) && sizeof(txn["owners"]) > 1
        && stringp(txn["owners"][1]) ? txn["owners"][1] : source_owner;
    return ([
        "transaction_id": mapp(txn) ? txn["id"] : 0,
        "operation": mapp(txn) ? txn["operation"] : "",
        "domain": mapp(txn) ? txn["domain"] : "",
        "idempotency_key": mapp(txn) ? txn["idempotency_key"] : "",
        "source_owner": source_owner,
        "target_owner": target_owner,
        "owner_count": mapp(txn) && arrayp(txn["owners"]) ? sizeof(txn["owners"]) : 0,
        "payload_present": mapp(txn) && mapp(txn["payload"]) ? 1 : 0,
        "state": state,
        "recorded_at": time(),
        "commit_log_applied": state == "applied" ? 1 : 0,
        "single_writer": 1,
        "idempotent": 1,
        "direct_cross_owner_write": 0,
    ]);
}

private mapping store_commit_record(mapping txn, string state)
{
    mapping record;
    mapping store;
    string domain;
    string idempotency_key;

    if (!mapp(txn))
        return ([ "ok": 0, "reason": "transaction is required" ]);
    domain = txn["domain"];
    if (!is_valid_transaction_domain(domain))
        return ([ "ok": 0, "reason": "invalid transaction domain" ]);
    idempotency_key = txn["idempotency_key"];
    if (!stringp(idempotency_key) || idempotency_key == "")
        return ([ "ok": 0, "reason": "idempotency_key is required" ]);

    switch (state) {
        case "prepared":
            store = _domain_prepare_log;
            break;
        case "applied":
            store = _domain_apply_log;
            break;
        case "rolled_back":
            store = _domain_rollback_log;
            break;
        default:
            return ([ "ok": 0, "reason": "invalid commit record state" ]);
    }

    if (txn["compact_command"])
        return ([
            "ok": 1,
            "compact_log": 1,
            "transaction_id": txn["id"],
            "state": state,
        ]);

    if (!mapp(store[domain]))
        store[domain] = ([]);
    record = build_commit_record(txn, state);
    store[domain][idempotency_key] = record;
    if (state == "applied") {
        if (!intp(_domain_apply_counts[domain]))
            _domain_apply_counts[domain] = 0;
        _domain_apply_counts[domain] = _domain_apply_counts[domain] + 1;
    }
    return ([ "ok": 1, "record": copy(record) ]);
}

mapping prepare_commit_proposal(mapping txn)
{
    mapping result;
    mapping stored;

    stored = store_commit_record(txn, "prepared");
    if (!mapp(stored) || stored["ok"] != 1)
        return stored;
    result = ([
        "ok": 1,
        "prepared": 1,
        "commit_proposal": 1,
        "domain": mapp(txn) ? txn["domain"] : "",
        "idempotency_key": mapp(txn) ? txn["idempotency_key"] : "",
        "commit_log_prepared": 1,
        "direct_cross_owner_write": 0,
    ]);
    if (mapp(txn) && txn["compact_command"])
        result["compact_log"] = 1;
    else
        result["record"] = stored["record"];
    return result;
}

mapping apply_commit_proposal(mapping txn)
{
    mapping result;
    mapping stored;

    stored = store_commit_record(txn, "applied");
    if (!mapp(stored) || stored["ok"] != 1)
        return stored;
    result = ([
        "ok": 1,
        "applied": 1,
        "commit_proposal": 1,
        "domain": mapp(txn) ? txn["domain"] : "",
        "idempotency_key": mapp(txn) ? txn["idempotency_key"] : "",
        "commit_log_applied": 1,
        "direct_cross_owner_write": 0,
    ]);
    if (mapp(txn) && txn["compact_command"])
        result["compact_log"] = 1;
    else
        result["record"] = stored["record"];
    return result;
}

mapping rollback_commit_proposal(mapping txn)
{
    mapping result;
    mapping stored;

    stored = store_commit_record(txn, "rolled_back");
    if (!mapp(stored) || stored["ok"] != 1)
        return stored;
    result = ([
        "ok": 1,
        "rolled_back": 1,
        "commit_proposal": 1,
        "domain": mapp(txn) ? txn["domain"] : "",
        "idempotency_key": mapp(txn) ? txn["idempotency_key"] : "",
        "commit_log_rolled_back": 1,
        "direct_cross_owner_write": 0,
    ]);
    if (mapp(txn) && txn["compact_command"])
        result["compact_log"] = 1;
    else
        result["record"] = stored["record"];
    return result;
}

mapping create_transaction(string operation, string idempotency_key, string *owners, mapping payload)
{
    int txn_id;
    mapping txn;
    mapping owner_validation;
    mapping generation_validation;

    if (!stringp(operation) || operation == "")
        return ([ "ok": 0, "reason": "operation is required" ]);
    if (!stringp(idempotency_key) || idempotency_key == "")
        return ([ "ok": 0, "reason": "idempotency_key is required" ]);
    owner_validation = validate_owner_set(owners);
    if (!mapp(owner_validation) || owner_validation["ok"] != 1)
        return owner_validation;
    generation_validation = validate_expected_generations(payload, owners);
    if (!mapp(generation_validation) || generation_validation["ok"] != 1)
        return generation_validation;
    if (!freeze_payload(payload))
        return ([ "ok": 0, "reason": "payload is not serializable",
            "error_code": "OWNER_PAYLOAD_NOT_SERIALIZABLE" ]);
    if (intp(_idempotency_index[idempotency_key])) {
        txn_id = _idempotency_index[idempotency_key];
        if (mapp(_transactions[txn_id])) {
            if (!proposal_matches_transaction(_transactions[txn_id], operation, "", owners,
            payload))
                return idempotency_conflict(idempotency_key);
            return build_transaction_result(_transactions[txn_id], 1);
        }
        map_delete(_idempotency_index, idempotency_key);
    }

    txn_id = _next_txn_id++;
    txn = ([
        "id": txn_id,
        "operation": operation,
        "idempotency_key": idempotency_key,
        "owners": arrayp(owners) ? owners : ({}),
        "payload": mapp(payload) ? copy(payload) : ([]),
        "proposal_fingerprint": proposal_fingerprint(operation, "", owners, payload),
        "state": "created",
        "created_at": time(),
        "updated_at": time(),
        "commit_after_client_sync": 0,
    ]);
    _transactions[txn_id] = txn;
    _idempotency_index[idempotency_key] = txn_id;
    _transactions_created_total++;
    return ([ "ok": 1, "transaction": copy(txn) ]);
}

mapping create_domain_transaction(string domain, string idempotency_key, string *owners, mapping payload)
{
    mapping result;
    mapping txn;
    mapping owner_validation;
    mapping generation_validation;
    int txn_id;

    if (!is_valid_transaction_domain(domain))
        return ([ "ok": 0, "reason": "invalid transaction domain" ]);
    if (!stringp(idempotency_key) || idempotency_key == "")
        return ([ "ok": 0, "reason": "idempotency_key is required" ]);
    owner_validation = validate_owner_set(owners);
    if (!mapp(owner_validation) || owner_validation["ok"] != 1)
        return owner_validation;
    /* 未显式声明 expected_generations 的 owner 缺省取协调器当前水位：
     * 域 handler 内部由各自真源做真实 CAS（MONEY_D 账本幂等 / 位置 CAS /
     * PLAYER_STATE_D 生成代次），协调器水位在此作为单调观测基准。player
     * 域的真实提交总是显式声明并做严格 CAS，不受缺省影响。 */
    payload = with_boundary_default_generations(payload, owners);
    generation_validation = validate_expected_generations(payload, owners);
    if (!mapp(generation_validation) || generation_validation["ok"] != 1)
        return generation_validation;
    if (!freeze_payload(payload))
        return ([ "ok": 0, "reason": "payload is not serializable",
            "error_code": "OWNER_PAYLOAD_NOT_SERIALIZABLE" ]);
    if (intp(_idempotency_index[idempotency_key]) &&
        mapp(_transactions[_idempotency_index[idempotency_key]])) {
        txn = _transactions[_idempotency_index[idempotency_key]];
        if (!proposal_matches_transaction(txn, domain + "_transaction", domain, owners, payload))
            return idempotency_conflict(idempotency_key);
        return build_transaction_result(txn, 1);
    }

    result = create_transaction(domain + "_transaction", idempotency_key, owners, payload);
    if (!mapp(result) || result["ok"] != 1 || !mapp(result["transaction"]))
        return result;

    txn = result["transaction"];
    txn_id = txn["id"];
    txn["domain"] = domain;
    txn["expected_generation"] = mapp(payload) ? payload["expected_generation"] : 0;
    txn["expected_generations"] = query_expected_generations(payload, owners);
    txn["schema_version"] = mapp(payload) && intp(payload["schema_version"])
        ? payload["schema_version"] : 1;
    txn["command_id"] = mapp(payload) && stringp(payload["command_id"])
        ? payload["command_id"] : idempotency_key;
    txn["causation_id"] = mapp(payload) && stringp(payload["causation_id"])
        ? payload["causation_id"] : domain + "_transaction";
    txn["source_owner"] = owners[0];
    txn["target_owner"] = sizeof(owners) > 1 ? owners[1] : owners[0];
    txn["proposal_fingerprint"] = proposal_fingerprint(domain + "_transaction", domain,
        owners, payload);
    txn["prepare_required"] = 1;
    txn["commit_boundary_only"] = 1;
    txn["rollback_supported"] = 1;
    txn["idempotent"] = 1;
    txn["direct_cross_owner_write"] = 0;
    _transactions[txn_id] = txn;
    if (!result["duplicate"])
        _domain_counts[domain] = _domain_counts[domain] + 1;

    result["transaction"] = copy(txn);
    return result;
}

mapping create_compact_domain_transaction(string domain, string idempotency_key,
    string *owners, mapping payload)
{
    int txn_id;
    mapping txn;
    mapping owner_validation;
    mapping generation_validation;

    if (!is_valid_transaction_domain(domain))
        return ([ "ok": 0, "reason": "invalid transaction domain" ]);
    if (!stringp(idempotency_key) || idempotency_key == "")
        return ([ "ok": 0, "reason": "idempotency_key is required" ]);
    owner_validation = validate_owner_set(owners);
    if (!mapp(owner_validation) || owner_validation["ok"] != 1)
        return owner_validation;
    payload = with_boundary_default_generations(payload, owners);
    generation_validation = validate_expected_generations(payload, owners);
    if (!mapp(generation_validation) || generation_validation["ok"] != 1)
        return generation_validation;
    if (!freeze_payload(payload))
        return ([ "ok": 0, "reason": "payload is not serializable",
            "error_code": "OWNER_PAYLOAD_NOT_SERIALIZABLE" ]);
    if (!stringp(idempotency_key) || idempotency_key == "")
        return ([ "ok": 0, "reason": "idempotency_key is required" ]);
    if (intp(_idempotency_index[idempotency_key])) {
        txn_id = _idempotency_index[idempotency_key];
        if (mapp(_transactions[txn_id])) {
            if (!proposal_matches_transaction(_transactions[txn_id], domain + "_transaction",
                domain, owners, payload))
                return idempotency_conflict(idempotency_key);
            return build_transaction_result(_transactions[txn_id], 1);
        }
        map_delete(_idempotency_index, idempotency_key);
    }

    txn_id = _next_txn_id++;
    txn = ([
        "id": txn_id,
        "operation": domain + "_transaction",
        "domain": domain,
        "idempotency_key": idempotency_key,
        "owners": arrayp(owners) ? owners + ({}) : ({}),
        "payload": mapp(payload) ? freeze_payload(payload) : ([]),
        "expected_generation": mapp(payload) ? payload["expected_generation"] : 0,
        "expected_generations": query_expected_generations(payload, owners),
        "schema_version": mapp(payload) && intp(payload["schema_version"])
            ? payload["schema_version"] : 1,
        "command_id": mapp(payload) && stringp(payload["command_id"])
            ? payload["command_id"] : idempotency_key,
        "causation_id": mapp(payload) && stringp(payload["causation_id"])
            ? payload["causation_id"] : domain + "_transaction",
        "source_owner": owners[0],
        "target_owner": sizeof(owners) > 1 ? owners[1] : owners[0],
        "proposal_fingerprint": proposal_fingerprint(domain + "_transaction", domain,
            owners, payload),
        "state": "created",
        "created_at": time(),
        "updated_at": time(),
        "commit_after_client_sync": 0,
        "prepare_required": 1,
        "commit_boundary_only": 1,
        "rollback_supported": 1,
        "idempotent": 1,
        "direct_cross_owner_write": 0,
        "compact_command": 1,
    ]);
    _transactions[txn_id] = txn;
    _idempotency_index[idempotency_key] = txn_id;
    _transactions_created_total++;
    _compact_command_transactions_created_total++;
    _domain_counts[domain] = _domain_counts[domain] + 1;
    return ([ "ok": 1, "duplicate": 0 ]) + build_compact_transaction_handle(txn);
}

mapping transition_transaction(int txn_id, string state, string reason)
{
    int newly_terminal;
    object adapter;
    mapping txn;
    string source_owner;
    string target_owner;

    if (!mapp(_transactions[txn_id]))
        return ([ "ok": 0, "reason": "unknown transaction" ]);
    if (!is_valid_transaction_state(state))
        return ([ "ok": 0, "reason": "invalid transaction state" ]);

    txn = _transactions[txn_id];
    if (txn["state"] == state)
        return build_transaction_result(txn, 1);
    if (!is_valid_transaction_transition(txn["state"], state))
        return ([
            "ok": 0,
            "reason": "invalid transaction transition",
            "transaction_id": txn_id,
            "current_state": txn["state"],
            "requested_state": state,
        ]);
    txn["state"] = state;
    txn["reason"] = stringp(reason) ? reason : "";
    txn["updated_at"] = time();
    newly_terminal = !txn["terminal_recorded"] && is_terminal_transaction_state(state);
    if (newly_terminal)
        txn["terminal_recorded"] = 1;
    _transactions[txn_id] = txn;

    if (arrayp(txn["owners"]) && sizeof(txn["owners"]) > 0) {
        source_owner = stringp(txn["owners"][0]) ? txn["owners"][0] : "owner/unknown";
        target_owner = sizeof(txn["owners"]) > 1 && stringp(txn["owners"][1]) ? txn["owners"][1] : source_owner;
        adapter = query_adapter();
        if (objectp(adapter))
            catch(adapter->observe_owner_commit(source_owner, target_owner, txn["operation"], txn_id, state));
    }
    if (newly_terminal)
        retain_terminal_transaction(txn_id);
    return build_transaction_result(_transactions[txn_id], 0);
}

mapping prepare_transaction(int txn_id)
{
    mapping aborted;
    mapping txn;
    mapping prepared;
    mapping generation_check;
    mapping transitioned;

    if (!mapp(_transactions[txn_id]))
        return ([ "ok": 0, "reason": "unknown transaction" ]);
    txn = _transactions[txn_id];
    if (txn["state"] == "prepared")
        return build_transaction_result(txn, 1);
    if (txn["state"] != "created")
        return ([ "ok": 0, "reason": "transaction state does not allow prepare",
                  "transaction_id": txn_id, "state": txn["state"] ]);
    generation_check = check_expected_generations(txn);
    if (!mapp(generation_check) || generation_check["ok"] != 1) {
        txn["prepare_result"] = generation_check;
        _transactions[txn_id] = txn;
        aborted = abort_transaction(txn_id, "owner generation stale");
        return ([ "ok": 0, "reason": "owner generation check failed",
            "error_code": mapp(generation_check) ? generation_check["error_code"] : "OWNER_GENERATION_CHECK_FAILED",
            "generation": generation_check,
            "transaction": mapp(aborted) && mapp(aborted["transaction"])
                ? aborted["transaction"] : copy(txn) ]);
    }
    if (stringp(txn["domain"])) {
        prepared = call_domain_handler(txn, "prepare");
        if (!mapp(prepared) || prepared["ok"] != 1) {
            aborted = abort_transaction(txn_id, "prepare handler rejected");
            if (txn["compact_command"])
                return ([ "ok": 0, "reason": "prepare handler rejected",
                          "handler_reason": mapp(prepared) && stringp(prepared["reason"])
                              ? prepared["reason"] : "" ])
                    + build_compact_transaction_handle(_transactions[txn_id]);
            return ([ "ok": 0, "reason": "prepare handler rejected", "handler_result": prepared,
                      "transaction": mapp(aborted) && mapp(aborted["transaction"])
                          ? aborted["transaction"] : copy(txn) ]);
        }
        if (txn["compact_command"]) {
            txn["prepare_succeeded"] = 1;
        } else {
            txn["prepare_result"] = prepared;
        }
        _transactions[txn_id] = txn;
    }
    transitioned = transition_transaction(txn_id, "prepared", "prepare accepted");
    if (txn["compact_command"] && mapp(transitioned) && transitioned["ok"] == 1)
        _compact_command_prepare_records_total++;
    return transitioned;
}

mapping commit_transaction(int txn_id)
{
    mapping txn;
    mapping applied;
    mapping compensated;
    mapping transitioned;
    int recovery_retry;

    if (!mapp(_transactions[txn_id]))
        return ([ "ok": 0, "reason": "unknown transaction" ]);
    txn = _transactions[txn_id];
    if (txn["state"] == "committed")
        return build_transaction_result(txn, 1);
    recovery_retry = txn["state"] == "recovery_pending" ? 1 : 0;
    if (!recovery_retry && txn["state"] != "prepared")
        return ([ "ok": 0, "reason": "transaction state does not allow commit",
                  "transaction_id": txn_id, "state": txn["state"] ]);
    if (!recovery_retry) {
        transition_transaction(txn_id, "committing", "commit started");
        txn = _transactions[txn_id];
    }
    if (stringp(txn["domain"])) {
        applied = call_domain_handler(txn, "apply");
        if (!mapp(applied) || applied["ok"] != 1
            || is_recovery_result(applied)) {
            /* unknown/pending 的业务结果禁止进入 generic rollback：原 domain
             * 可能已经写入，只能由同一 operation 的恢复 owner 接管。 */
            if (recovery_retry || is_recovery_result(applied)) {
                txn["recovery_required"] = 1;
                txn["pending_recovery"] = 1;
                txn["recovery_result"] = applied;
                if (txn["compact_command"])
                    txn["apply_succeeded"] = 0;
                else
                    txn["apply_result"] = applied;
                _transactions[txn_id] = txn;
                if (!recovery_retry)
                    transition_transaction(txn_id, "recovery_pending",
                        "apply result requires domain recovery");
                if (txn["compact_command"])
                    return ([ "ok": 0,
                              "reason": "domain recovery is pending",
                              "recovery_required": 1,
                              "pending_recovery": 1,
                              "handler_result": applied ])
                        + build_compact_transaction_handle(_transactions[txn_id]);
                return ([ "ok": 0,
                          "reason": "domain recovery is pending",
                          "recovery_required": 1,
                          "pending_recovery": 1,
                          "handler_result": applied,
                          "transaction": copy(_transactions[txn_id]) ]);
            }
            if (txn["compact_command"])
                txn["apply_succeeded"] = 0;
            else
                txn["apply_result"] = applied;
            compensated = call_domain_handler(txn, "rollback");
            txn["compensation_result"] = compensated;
            txn["compensation_succeeded"] = mapp(compensated) && compensated["ok"] == 1 ? 1 : 0;
            _transactions[txn_id] = txn;
            transition_transaction(txn_id, "aborting", "apply handler rejected");
            transition_transaction(txn_id, "aborted", "apply handler rejected");
            if (txn["compact_command"])
                return ([ "ok": 0, "reason": "apply handler rejected",
                          "handler_reason": mapp(applied) && stringp(applied["reason"])
                              ? applied["reason"] : "",
                          "compensation_required": 1,
                          "compensation": compensated,
                          "compensation_succeeded": txn["compensation_succeeded"] ])
                    + build_compact_transaction_handle(_transactions[txn_id]);
            return ([ "ok": 0, "reason": "apply handler rejected", "handler_result": applied,
                      "compensation_required": 1, "compensation": compensated,
                      "compensation_succeeded": txn["compensation_succeeded"],
                      "transaction": copy(_transactions[txn_id]) ]);
        }
        if (txn["compact_command"])
            txn["apply_succeeded"] = 1;
        else
            txn["apply_result"] = applied;
        txn["recovery_required"] = applied["recovery_required"] ? 1 : 0;
        txn["pending_recovery"] = applied["pending_recovery"] ? 1 : 0;
        _transactions[txn_id] = txn;
    }
    transitioned = transition_transaction(txn_id, "committed", "commit completed");
    if (txn["compact_command"] && mapp(transitioned) && transitioned["ok"] == 1)
        _compact_command_apply_records_total++;
    return transitioned;
}

mapping abort_transaction(int txn_id, string reason)
{
    mapping txn;

    if (!mapp(_transactions[txn_id]))
        return ([ "ok": 0, "reason": "unknown transaction" ]);
    txn = _transactions[txn_id];
    if (txn["state"] == "aborted")
        return build_transaction_result(txn, 1);
    if (member_array(txn["state"], ({ "created", "prepared", "committing", "aborting" })) == -1)
        return ([ "ok": 0, "reason": "transaction state does not allow abort",
                  "transaction_id": txn_id, "state": txn["state"] ]);
    if (txn["state"] != "aborting")
        transition_transaction(txn_id, "aborting", reason);
    return transition_transaction(txn_id, "aborted", reason);
}

mapping rollback_transaction(int txn_id, string reason)
{
    mapping aborted;
    mapping txn;
    mapping rolled_back;

    if (!mapp(_transactions[txn_id]))
        return ([ "ok": 0, "reason": "unknown transaction" ]);
    txn = _transactions[txn_id];
    if (txn["state"] == "aborted")
        return build_transaction_result(txn, 1);
    if (member_array(txn["state"], ({ "created", "prepared" })) == -1)
        return ([ "ok": 0, "reason": "transaction state does not allow rollback",
                  "transaction_id": txn_id, "state": txn["state"] ]);
    if (stringp(txn["domain"])) {
        rolled_back = call_domain_handler(txn, "rollback");
        if (txn["compact_command"]) {
            txn["rollback_succeeded"] = mapp(rolled_back) && rolled_back["ok"] == 1 ? 1 : 0;
        } else {
            txn["rollback_result"] = rolled_back;
        }
        _transactions[txn_id] = txn;
        if (!mapp(rolled_back) || rolled_back["ok"] != 1) {
            aborted = abort_transaction(txn_id, "rollback handler rejected");
            if (txn["compact_command"])
                return ([ "ok": 0, "reason": "rollback handler rejected",
                          "handler_reason": mapp(rolled_back) && stringp(rolled_back["reason"])
                              ? rolled_back["reason"] : "" ])
                    + build_compact_transaction_handle(_transactions[txn_id]);
            return ([ "ok": 0, "reason": "rollback handler rejected", "handler_result": rolled_back,
                      "transaction": mapp(aborted) && mapp(aborted["transaction"])
                          ? aborted["transaction"] : copy(txn) ]);
        }
    }
    aborted = abort_transaction(txn_id, stringp(reason) && reason != "" ? reason : "rollback requested");
    if (txn["compact_command"] && mapp(aborted) && aborted["ok"] == 1)
        _compact_command_rollback_records_total++;
    return aborted;
}

int set_owner_generation(string owner_id, int generation)
{
    if (!stringp(owner_id) || owner_id == "" || !intp(generation)
        || generation < 0)
        return 0;
    if (intp(_owner_generations[owner_id])
        && _owner_generations[owner_id] > generation)
        return 0;
    _owner_generations[owner_id] = generation;
    return 1;
}

int query_owner_generation(string owner_id)
{
    return stringp(owner_id) && intp(_owner_generations[owner_id])
        ? _owner_generations[owner_id] : -1;
}

mapping query_owner_generations()
{
    return copy(_owner_generations || ([]));
}

mapping query_domain_counts()
{
    return copy(_domain_counts);
}

mapping finalize_transaction(int txn_id)
{
    return transition_transaction(txn_id, "finalized", "finalized");
}

mapping query_transaction(int txn_id)
{
    if (!mapp(_transactions[txn_id]))
        return 0;
    return copy(_transactions[txn_id]);
}

mapping query_status()
{
    int active_transactions;
    int retained_terminal_transactions;
    int transaction_conservation_ok;
    int default_handler_domain_count;
    int mutation_schema_domain_count;
    mapping registered;
    mapping schema_contract;
    object handler_object;
    mixed err;
    string domain;
    string *missing;
    string *mutation_schema_missing;
    string *non_mutating_domains;
    string *real_handlers;

    active_transactions = count_active_transactions();
    retained_terminal_transactions = sizeof(_transactions) - active_transactions;
    transaction_conservation_ok =
        _transactions_created_total == _transactions_terminal_total + active_transactions &&
        _transactions_terminal_total == _terminal_transaction_evictions + retained_terminal_transactions &&
        sizeof(_transactions) == sizeof(_idempotency_index) &&
        retained_terminal_transactions ==
            _terminal_transaction_order_tail - _terminal_transaction_order_head;
    missing = ({});
    real_handlers = ({ });
    foreach (domain in keys(_domain_counts)) {
        if (domain == "room")
            continue;
        if (!mapp(query_domain_handler(domain)))
            missing += ({ domain });
        else if (query_domain_handler(domain)["handler"] != this_object())
            real_handlers += ({ domain });
        else
            default_handler_domain_count++;
    }

    mutation_schema_missing = ({ });
    non_mutating_domains = ({ });
    foreach (domain in keys(_domain_counts)) {
        if (domain == "room") {
            non_mutating_domains += ({ domain });
            continue;
        }
        registered = query_domain_handler(domain);
        if (!mapp(registered) || registered["handler"] == this_object()) {
            mutation_schema_missing += ({ domain });
            continue;
        }
        if (domain == "player") {
            mutation_schema_domain_count++;
            continue;
        }
        handler_object = registered["handler"];
        schema_contract = 0;
        err = catch(schema_contract = handler_object->query_mutation_schema_status());
        if (!err && mapp(schema_contract)
            && arrayp(schema_contract["supported_domains"])
            && member_array(domain, schema_contract["supported_domains"]) != -1)
            mutation_schema_domain_count++;
        else
            mutation_schema_missing += ({ domain });
    }

    return ([
        "transactions": sizeof(_transactions),
        "idempotency_keys": sizeof(_idempotency_index),
        "transactions_created_total": _transactions_created_total,
        "compact_command_transactions_created_total": _compact_command_transactions_created_total,
        "compact_command_prepare_records_total": _compact_command_prepare_records_total,
        "compact_command_apply_records_total": _compact_command_apply_records_total,
        "compact_command_rollback_records_total": _compact_command_rollback_records_total,
        "transactions_terminal_total": _transactions_terminal_total,
        "transactions_active": active_transactions,
        "terminal_transactions_retained": retained_terminal_transactions,
        "terminal_transaction_evictions": _terminal_transaction_evictions,
        "transaction_conservation_ok": transaction_conservation_ok,
        "max_terminal_transaction_history": MAX_TERMINAL_TRANSACTION_HISTORY,
        "domains": copy(_domain_counts),
        "mode": "owner_commit_state_machine",
        "registered_domain_handlers": sizeof(_domain_handlers),
        "handler_missing_domains": missing,
        "handler_missing_domain_count": sizeof(missing),
        "domain_prepare_logs": sizeof(_domain_prepare_log),
        "domain_apply_logs": sizeof(_domain_apply_log),
        "domain_rollback_logs": sizeof(_domain_rollback_log),
        "domain_prepare_log_entries": count_domain_commit_records(_domain_prepare_log),
        "domain_apply_log_entries": count_domain_commit_records(_domain_apply_log),
        "domain_rollback_log_entries": count_domain_commit_records(_domain_rollback_log),
        "domain_apply_counts": copy(_domain_apply_counts),
        "default_handler_commit_log_ready": 0,
        "real_apply_handler_ready": sizeof(missing) == 0
            && sizeof(real_handlers) > 0,
        "real_domain_handler_count": sizeof(real_handlers),
        "real_domain_handlers": real_handlers,
        "default_handler_domain_count": default_handler_domain_count,
        "all_domains_real_handler_ready": sizeof(missing) == 0,
        "mutation_schema_domain_count": mutation_schema_domain_count,
        "mutation_schema_missing_domains": mutation_schema_missing,
        "non_mutating_domains": non_mutating_domains,
        "all_domain_mutation_schema_ready": sizeof(mutation_schema_missing) == 0,
        "prepare_required": 1,
        "rollback_supported": 1,
        "idempotent": 1,
        "commit_boundary_only": 1,
        "facade_only_runtime_claims": 0,
        "direct_cross_owner_write": 0,
    ]);
}
