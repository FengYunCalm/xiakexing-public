/**
 * @file    bank_d.c
 * @brief   银行服务真源



 */

#pragma strict_types
#pragma save_types

#include <system/ansi.h>
#include <system/constants.h>
#include <system/globals.h>
#include <system/services.h>

inherit "/lib/framework/daemon";

// `bank_d.c` owns account balances and banking workflow. Currency formatting
// and on-body cash mutation are delegated back to `money_d.c` on purpose.
mapping _player_banks = ([]);
/* 每条资产腿的持久 receipt 与余额同一 save_object 提交；重放同一
 * operation_id 只返回原结果，不重新改变余额。 */
mapping _operation_receipts = ([]);
nosave int _legacy_operation_sequence;
nosave int _save_sequence;
nosave string _operation_namespace;
/* 资产账本加载门：0=尚未加载，1=已加载且可写，-1=加载失败。
 * 失败态必须保留原文件并拒绝所有写入，不能把空 mapping 当成新账本覆盖故障数据。 */
nosave int _bank_state_status;
nosave string _bank_state_error;
/* save_object 已落盘但 owner 终态收据待补时，保留原 snapshot 身份；
 * 下一次保存先补收据，不能把新业务状态覆盖成旧操作的假重试。 */
mapping _save_receipt_pending;
/* S15：银行跨腿结算意图与各腿收据由银行 owner 持久保存。业务
 * operation_id 与一次保存尝试的 identity 分离；未完成记录不得裁剪。
 * 旧存档没有此字段时只初始化空 mapping，不改写旧业务记录。 */
mapping _settlements;
nosave mapping _last_bank_save_phase;
nosave string _interest_intent_needs_publish;
nosave mapping _settlement_callouts;
nosave mapping _interest_callouts;

private int valid_operation_id(string operation_id);
private string build_restore_operation_id(string key, mapping snapshot);
private void schedule_bank_settlement_recovery(string operation_id,
    string actor_id);
private void schedule_bank_interest_recovery(string actor_id);
private void rebuild_bank_recovery_callouts();
mapping command_interest(object player);

private mapping build_command_result(int success, string message, mapping extra)
{
    mapping result;

    result = ([
        "success": success ? 1 : 0,
        "message": stringp(message) ? message : ""
    ]);

    if (mapp(extra))
        result += extra;

    return result;
}

private string get_bank_save_file()
{
    return DATA_DIR + "runtime/bank_d";
}

private mapping query_economy_bank_config()
{
    mapping config;

    config = CONFIG_D->query_full_config("economy");
    return mapp(config) && mapp(config["bank"]) ? config["bank"] : ([]);
}

private float query_interest_rate()
{
    mapping bank_config;
    mixed interest_rate;

    bank_config = query_economy_bank_config();
    interest_rate = bank_config["interest_rate"];
    if ((intp(interest_rate) || floatp(interest_rate)) && to_float(interest_rate) >= 0.0)
        return to_float(interest_rate);

    return 0.01;
}

private int query_interest_interval()
{
    mapping bank_config;
    int interval;

    bank_config = query_economy_bank_config();
    interval = bank_config["interest_interval"];
    if (intp(interval) && interval > 0)
        return interval;

    return SECONDS_PER_DAY;
}

private int query_min_balance_for_interest()
{
    mapping bank_config;
    int min_balance;

    bank_config = query_economy_bank_config();
    min_balance = bank_config["min_balance_for_interest"];
    if (intp(min_balance) && min_balance >= 0)
        return min_balance;

    return 0;
}

private int restore_bank_data()
{
    string file;
    mixed restored;
    int restore_result;

    _bank_state_status = 0;
    _bank_state_error = "";
    file = get_bank_save_file();
    if (file_size(file + ".o") == -1) {
        _player_banks = ([]);
        _bank_state_status = 1;
        return 1;
    }

    restored = file_size(file + ".o") > 0
        ? catch(restore_result = restore_object(file))
        : "bank state file is empty or not regular";
    if (restored || !restore_result || !mapp(_player_banks)) {
        _bank_state_status = -1;
        _bank_state_error = restored
            ? "bank state restore failed" : "bank state shape invalid";
        return 0;
    }

    if (!mapp(_operation_receipts))
        _operation_receipts = ([]);
    if (!mapp(_settlements))
        _settlements = ([]);
    _bank_state_status = 1;
    return 1;
}

private int bank_state_ready()
{
    return _bank_state_status == 1 && mapp(_player_banks);
}

private int retry_bank_save_receipt()
{
    mapping pending;
    mapping result;
    mapping snapshot;
    mixed err;
    int snapshot_id;

    pending = mapp(_save_receipt_pending) ? _save_receipt_pending : 0;
    if (!mapp(pending)) {
        err = catch(result = OWNER_PERSISTENCE_D->query_pending_publication(
            "owner/global/service/economy/bank", "bank_state",
            get_bank_save_file() + ".o"));
        if (err || !mapp(result)) {
            _last_bank_save_phase = ([ "outcome": "unknown", "persisted": -1,
                "save_result": 0, "error": "publication lookup failed" ]);
            return 0;
        }
        if (result["ok"] != 1) {
            _last_bank_save_phase = ([ "outcome": "unknown", "persisted": -1,
                "save_result": 0, "error": result["error_code"] || "publication lookup rejected" ]);
            return 0;
        }
        snapshot = result["snapshot"];
        if (!mapp(snapshot)) {
            _last_bank_save_phase = ([
                "outcome": "none", "persisted": 0, "save_result": 0,
            ]);
            return 1;
        }
        pending = ([ "snapshot_id": snapshot["id"],
            "retry_token": "snapshot-retry/" + snapshot["id"] ]);
        _save_receipt_pending = pending;
    }
    snapshot_id = intp(pending["snapshot_id"]) ? pending["snapshot_id"] : 0;
    if (snapshot_id <= 0) {
        _last_bank_save_phase = ([ "outcome": "unknown", "persisted": -1,
            "save_result": 0, "error": "bank receipt identity missing" ]);
        return 0;
    }
    err = catch(result = OWNER_PERSISTENCE_D->recover_snapshot_publication(
        snapshot_id));
    if (err || !mapp(result)) {
        _last_bank_save_phase = ([ "outcome": "unknown", "persisted": -1,
            "save_result": 0, "error": err ? sprintf("%O", err)
                : "publication recovery unavailable" ]);
        return 0;
    }
    if (result["outcome"] == "not_written") {
        mapping retired;
        mixed retire_error;

        /* The old candidate never reached the file. Retire the verified
         * receipt itself before allowing a new candidate on the same path;
         * otherwise the owner ledger keeps the obsolete snapshot pending and
         * the next publication is misclassified as unknown. */
        retire_error = catch(retired = OWNER_PERSISTENCE_D->retire_unwritten_snapshot(
            pending["snapshot_id"],
            "publication evidence proves bank candidate was not written"));
        if (retire_error || !mapp(retired) || retired["ok"] != 1) {
            _last_bank_save_phase = ([
                "outcome": "unknown", "persisted": -1, "save_result": 0,
                "error": retire_error ? sprintf("%O", retire_error)
                : (mapp(retired) ? retired["error_code"]
                : "bank not-written receipt could not be retired") ]);
            return 0;
        }
        _save_receipt_pending = 0;
        _last_bank_save_phase = ([
            "outcome": "not_written", "persisted": 0, "save_result": 0,
            "retry_token": pending["retry_token"] || "" ]);
        return 1;
    }
    if (result["ok"] != 1 || result["outcome"] != "committed") {
        _last_bank_save_phase = ([
            "outcome": result["outcome"] == "unknown"
                ? "unknown" : "persisted_receipt_pending",
            "persisted": result["outcome"] == "unknown" ? -1 : 1,
            "save_result": result["outcome"] == "unknown" ? 0 : 2,
            "retry_token": pending["retry_token"] || "",
            "error": result["error_code"] || "bank receipt pending" ]);
        return 0;
    }
    _save_receipt_pending = 0;
    _last_bank_save_phase = ([ "outcome": "committed", "persisted": 1,
        "save_result": 1 ]);
    return 1;
}

private int persist_bank_data(string operation_id)
{
    mapping detail;
    string file;
    string runtime_dir;

    if (!bank_state_ready() || !valid_operation_id(operation_id)) {
        _last_bank_save_phase = ([ "outcome": "not_written", "persisted": 0,
            "save_result": 0, "error": "bank save identity unavailable" ]);
        return 0;
    }
    if (!retry_bank_save_receipt())
        return 0;

    file = get_bank_save_file();
    runtime_dir = DATA_DIR + "runtime";
    if (!mapp(_player_banks))
        _player_banks = ([]);
    if (file_size(runtime_dir) == -1)
        mkdir(runtime_dir);
    _save_sequence++;
    detail = owner_save_object_phases(file,
        "owner/global/service/economy/bank", "bank_state", ([
            "operation_id": operation_id,
            "account_count": sizeof(_player_banks),
            "state_digest": hash("sha256", save_variable(_player_banks)),
        ]), sprintf("bank/save/%s/%d", _operation_namespace, _save_sequence));
    /* save_object 已落盘但 owner 终态收据待补时，不能回滚业务内存或重放现金腿；
     * 保存者保留原 snapshot，下一次保存先补收据。 */
    if (mapp(detail))
        _last_bank_save_phase = ([
            "outcome": detail["outcome"] || "unknown",
            "persisted": intp(detail["persisted"]) ? detail["persisted"] : -1,
            "save_result": detail["persisted"] == 1 ? 2 : 0,
            "snapshot_id": detail["snapshot_id"] || 0,
            "retry_token": detail["retry_token"] || "",
            "error": detail["stages"] && detail["stages"]["terminal"]
                ? detail["stages"]["terminal"]["error_code"] || "" : "",
        ]);
    if (mapp(detail) && detail["persisted"] == 1) {
        if (detail["ok"] == 1) {
            _save_receipt_pending = 0;
            _last_bank_save_phase["outcome"] = "committed";
            _last_bank_save_phase["save_result"] = 1;
            return 1;
        }
        _save_receipt_pending = ([
            "snapshot_id": detail["snapshot_id"],
            "retry_token": detail["retry_token"] || "",
        ]);
        return 2;
    }
    if (!mapp(detail))
        _last_bank_save_phase = ([ "outcome": "unknown", "persisted": -1,
            "save_result": 0, "error": "owner save result unavailable" ]);
    return 0;
}

private void save_bank_data(string operation_id)
{
    persist_bank_data(operation_id);
}

void create()
{
    ::create();
    _operation_namespace = sprintf("%016x%016x%016x", secure_random(MAX_INT),
        secure_random(MAX_INT), secure_random(MAX_INT));
    _settlement_callouts = ([]);
    _interest_callouts = ([]);
    restore_bank_data();
    if (!mapp(_settlements))
        _settlements = ([]);
    rebuild_bank_recovery_callouts();
}

private string get_player_key(mixed player_or_id)
{
    if (stringp(player_or_id))
        return player_or_id;
    if (objectp(player_or_id))
        return get_object_id(player_or_id);
    return "";
}

private mapping get_bank_data(mixed player_or_id)
{
    string key = get_player_key(player_or_id);

    if (!bank_state_ready() || key == "") return 0;
    if (!mapp(_player_banks[key])) {
        _player_banks[key] = ([
            "balance": 0,
            "interest_last": 0,
            "last_access": time()
        ]);
    }
    return _player_banks[key];
}

private int query_interest_payout(mapping bank)
{
    int balance;
    int interest;
    int min_balance;
    float interest_rate;

    if (!mapp(bank))
        return 0;
    balance = bank["balance"] || 0;
    if (balance <= 0)
        return 0;

    min_balance = query_min_balance_for_interest();
    if (balance < min_balance)
        return 0;

    interest_rate = query_interest_rate();
    /* L-97：零利率就是真实零收益，不再用最小值 1 把配置语义改写掉。 */
    interest = to_int(balance * interest_rate);
    return interest > 0 ? interest : 0;
}

/* L-97：冻结本次利息计算意图（本金/利率/周期/金额），重试必须复用同一意图，
 * 不能因为期间配置变化而算出不同金额、进而用新 operation_id 再发一次。 */
private mapping build_interest_intent(mapping bank)
{
    int balance;
    int interest;
    int last_time;

    if (!mapp(bank))
        return 0;
    balance = bank["balance"] || 0;
    interest = query_interest_payout(bank);
    last_time = intp(bank["interest_last"]) ? bank["interest_last"] : 0;
    if (interest <= 0 || balance <= 0)
        return 0;
    return ([
        "balance": balance,
        "interest_rate": query_interest_rate(),
        "interval": query_interest_interval(),
        "amount": interest,
        "period_start": last_time,
        "period_end": time(),
    ]);
}

private string build_interest_operation_id(string player_id, mapping intent)
{
    float rate_basis;

    if (!stringp(player_id) || player_id == "" || !mapp(intent))
        return "";
    rate_basis = floatp(intent["interest_rate"]) ? intent["interest_rate"]
        : to_float(intent["interest_rate"] || 0);
    return sprintf("bank/interest/%s/%d/%d/%d/%d", player_id,
        intp(intent["period_start"]) ? intent["period_start"] : 0,
        intp(intent["balance"]) ? intent["balance"] : 0,
        to_int(rate_basis * 1000000.0),
        intp(intent["amount"]) ? intent["amount"] : 0);
}

mapping query_account_snapshot(mixed player_or_id)
{
    mapping bank;

    if (!bank_state_ready())
        return 0;
    bank = get_bank_data(player_or_id);
    return mapp(bank) ? copy(bank) : 0;
}

mapping restore_account_snapshot(mixed player_or_id, mapping snapshot)
{
    string key;
    int save_code;
    int had_previous;
    mapping previous_snapshot;
    mapping phase;

    if (!bank_state_ready())
        return ([ "ok": 0, "persisted": 0, "outcome": "not_written",
            "save_result": 0, "error": "bank state unavailable" ]);
    key = get_player_key(player_or_id);
    if (key == "")
        return ([ "ok": 0, "persisted": 0, "outcome": "not_written",
            "save_result": 0, "error": "bank identity unavailable" ]);

    if (!mapp(_player_banks))
        _player_banks = ([]);
    had_previous = mapp(_player_banks[key]);
    previous_snapshot = had_previous ? copy(_player_banks[key]) : 0;
    if (!mapp(snapshot))
        map_delete(_player_banks, key);
    else
        _player_banks[key] = copy(snapshot);
    save_code = persist_bank_data(build_restore_operation_id(key, snapshot));
    phase = mapp(_last_bank_save_phase) ? copy(_last_bank_save_phase) : ([]);
    if (save_code == 0 && phase["outcome"] == "not_written") {
        if (had_previous)
            _player_banks[key] = previous_snapshot;
        else
            map_delete(_player_banks, key);
    }
    phase["ok"] = save_code == 1 ? 1 : 0;
    phase["save_result"] = save_code;
    phase["operation_id"] = build_restore_operation_id(key, snapshot);
    if (!phase["outcome"])
        phase["outcome"] = save_code == 1 ? "committed" : "unknown";
    if (!phase["persisted"] && save_code == 0
        && phase["outcome"] == "unknown")
        phase["persisted"] = -1;
    return phase;
}

mapping query_bank_runtime_snapshot()
{
    mapping bank_config;
    mixed fee_rate;

    bank_config = query_economy_bank_config();
    fee_rate = bank_config["transaction_fee"];
    return ([
        "config_name": "economy",
        "loaded_config_ok": mapp(bank_config) ? 1 : 0,
        "bank_state_loaded": bank_state_ready() ? 1 : 0,
        "bank_state_error": _bank_state_error || "",
        "transaction_fee_rate": (intp(fee_rate) || floatp(fee_rate)) ? to_float(fee_rate) : 0.01,
        "interest_rate": query_interest_rate(),
        "interest_interval": query_interest_interval(),
        "min_balance_for_interest": query_min_balance_for_interest(),
        "account_count": sizeof(keys(_player_banks || ([]))),
    ]);
}

int query_balance(mixed player_or_id)
{
    mapping bank;

    if (!bank_state_ready())
        return 0;
    bank = get_bank_data(player_or_id);
    return mapp(bank) ? (bank["balance"] || 0) : 0;
}

string format_money(int amount)
{
    return call_other(MONEY_D, "format_money", amount);
}

int parse_money(string amount_str)
{
    int amount;
    string unit;

    if (!stringp(amount_str) || amount_str == "") return 0;

    amount = MONEY_D->parse_currency_amount(amount_str);
    if (amount > 0)
        return amount;

    if (sscanf(amount_str, "%d %s", amount, unit) == 2) {
        switch (unit) {
            case "gold":
            case "黄金":
                return amount * 10000;
            case "silver":
            case "银两":
            case "银子":
                return amount * 100;
            default:
                return amount;
        }
    }
    if (sscanf(amount_str, "%d", amount) == 1) {
        return amount;
    }
    return 0;
}

int process_deposit(object player, int amount)
{
    mapping result;

    if (!objectp(player) || amount <= 0 || query_balance(player) < 0)
        return 0;
    result = call_other(ECONOMY_SETTLEMENT_D, "settle_bank_deposit",
        player, amount);
    return mapp(result) && result["success"] == 1;
}

int process_withdraw(object player, int amount)
{
    mapping result;

    if (!objectp(player) || amount <= 0 || query_balance(player) < amount)
        return 0;
    result = call_other(ECONOMY_SETTLEMENT_D, "settle_bank_withdraw",
        player, amount);
    return mapp(result) && result["success"] == 1;
}

int process_transfer(object player, string target_id, int amount)
{
    mapping result;

    if (!objectp(player) || !stringp(target_id) || target_id == ""
        || amount <= 0)
        return 0;
    result = call_other(ECONOMY_SETTLEMENT_D, "settle_bank_transfer",
        player, target_id, amount);
    return mapp(result) && result["success"] == 1;
}

private int valid_operation_id(string operation_id)
{
    return stringp(operation_id) && operation_id != ""
        && strlen(operation_id) <= 256
        && strsrch(operation_id, "\n") == -1
        && strsrch(operation_id, "\r") == -1;
}

private int bank_settlement_write_allowed()
{
    object caller;
    string path;

    caller = previous_object();
    if (!objectp(caller))
        return 0;
    if (caller == this_object())
        return 1;
    path = base_name(caller);
    return strsrch(path, "/daemons/services/economy/") == 0
        || strsrch(path, "/daemons/services/system/player_lifecycle_d") == 0;
}

private int bank_settlement_frozen_matches(mapping record, mapping frozen)
{
    mapping old_frozen;
    string *fields;
    string field;

    if (!mapp(record) || !mapp(frozen) || !mapp(record["frozen"]))
        return 0;
    old_frozen = record["frozen"];
    fields = ({ "actor_id", "action", "amount", "transfer_amount",
        "fee", "net", "target_id" });
    foreach (field in fields) {
        if (!undefinedp(frozen[field]) && old_frozen[field] != frozen[field])
            return 0;
    }
    return 1;
}

private int bank_settlement_transition_allowed(string action,
    string current_phase, string next_phase)
{
    if (!stringp(action) || !stringp(current_phase) || !stringp(next_phase))
        return 0;
    if (current_phase == next_phase)
        return 1;
    if (next_phase == "failed" || next_phase == "rolled_back"
        || next_phase == "compensation_pending")
        return current_phase != "committed" && current_phase != "rolled_back";
    if (current_phase == "compensation_pending" && next_phase == "finalizing")
        return 1;
    if (current_phase == "finalizing" && next_phase == "rolled_back")
        return 1;
    if (action == "deposit")
        return (current_phase == "prepared" && next_phase == "cash_pending")
            || (current_phase == "cash_pending" && next_phase == "cash_committed")
            || (current_phase == "cash_committed" && next_phase == "bank_pending")
            || (current_phase == "bank_pending" && next_phase == "bank_committed")
            || (current_phase == "bank_committed" && next_phase == "finalizing")
            || (current_phase == "finalizing" && next_phase == "committed");
    if (action == "withdraw")
        return (current_phase == "prepared" && next_phase == "bank_pending")
            || (current_phase == "bank_pending" && next_phase == "bank_committed")
            || (current_phase == "bank_committed" && next_phase == "cash_pending")
            || (current_phase == "cash_pending" && next_phase == "finalizing")
            || (current_phase == "finalizing" && next_phase == "committed");
    if (action == "transfer")
        return (current_phase == "prepared" && next_phase == "bank_pending")
            || (current_phase == "bank_pending" && next_phase == "debit_committed")
            || (current_phase == "debit_committed" && next_phase == "credit_pending")
            || (current_phase == "credit_pending" && next_phase == "finalizing")
            || (current_phase == "finalizing" && next_phase == "committed");
    return 0;
}

private mapping bank_settlement_save_result(string operation_id, int save_code)
{
    mapping phase;
    mapping result;

    phase = mapp(_last_bank_save_phase) ? copy(_last_bank_save_phase) : ([]);
    result = ([
        "ok": save_code == 1 ? 1 : 0,
        "operation_id": operation_id,
        "persisted": intp(phase["persisted"]) ? phase["persisted"] : 0,
        "save_result": save_code,
        "outcome": phase["outcome"] || (save_code == 1
            ? "committed" : "unknown"),
    ]);
    if (phase["retry_token"])
        result["retry_token"] = phase["retry_token"];
    if (phase["error"])
        result["error"] = phase["error"];
    return result;
}

/* 银行结算 intent 由经济 owner 冻结一次；同 ID 的后续调用只能复用
 * 相同业务参数，不能把费率、余额或目标重新读成另一笔交易。 */
mapping prepare_bank_settlement(object player, string operation_id,
    string action, mapping frozen)
{
    mapping record;
    mapping candidate;
    int save_code;
    string actor_id;

    if (!bank_settlement_write_allowed())
        return ([ "ok": 0, "state": "invalid_caller" ]);
    if (!bank_state_ready())
        return ([ "ok": 0, "state": "bank_state_unavailable" ]);
    if (!objectp(player) || !valid_operation_id(operation_id)
        || member_array(action, ({ "deposit", "withdraw", "transfer" })) < 0
        || !mapp(frozen))
        return ([ "ok": 0, "state": "invalid_args" ]);
    actor_id = get_player_key(player);
    if (actor_id == "" || frozen["actor_id"] != actor_id
        || frozen["action"] != action || !intp(frozen["amount"])
        || frozen["amount"] <= 0 || !intp(frozen["fee"])
        || frozen["fee"] < 0 || !intp(frozen["net"])
        || frozen["net"] < 0)
        return ([ "ok": 0, "state": "invalid_frozen_intent" ]);
    if (!mapp(_settlements))
        _settlements = ([]);
    record = _settlements[operation_id];
    if (mapp(record)) {
        if (record["action"] != action
            || !bank_settlement_frozen_matches(record, frozen))
            return ([ "ok": 0, "state": "operation_conflict",
                "operation_id": operation_id ]);
        /* 准备意图的原保存收据未闭合前，禁止任何现金/银行业务腿。
         * 重试必须先补原收据；明确 not_written 后才允许为同一冻结意图
         * 创建新的保存尝试，不能把重复 prepare 直接当作已提交。 */
        if (!retry_bank_save_receipt())
            return bank_settlement_save_result(operation_id, 0)
                + ([ "state": mapp(_last_bank_save_phase)
                    ? _last_bank_save_phase["outcome"] : "unknown" ]);
        if (mapp(_last_bank_save_phase)
            && _last_bank_save_phase["outcome"] == "not_written") {
            save_code = persist_bank_data(operation_id
                + "/settlement/prepare-retry");
            if (save_code != 1)
                return bank_settlement_save_result(operation_id, save_code)
                    + ([ "state": mapp(_settlements[operation_id])
                        ? _settlements[operation_id]["phase"]
                        : "not_written" ]);
        }
        if (member_array(record["phase"],
            ({ "committed", "rolled_back", "failed" })) < 0)
            schedule_bank_settlement_recovery(operation_id, actor_id);
        return ([ "ok": 1, "duplicate": 1, "operation_id": operation_id,
            "state": record["phase"], "settlement": copy(record) ]);
    }
    candidate = ([
        "operation_id": operation_id,
        "actor_id": actor_id,
        "action": action,
        "frozen": copy(frozen),
        "phase": "prepared",
        "legs": ([]),
        "created_at": time(),
        "updated_at": time(),
    ]);
    _settlements[operation_id] = candidate;
    save_code = persist_bank_data(operation_id + "/settlement/prepare");
    if (save_code != 1) {
        if (mapp(_last_bank_save_phase)
            && _last_bank_save_phase["outcome"] == "not_written")
            map_delete(_settlements, operation_id);
        return bank_settlement_save_result(operation_id, save_code)
            + ([ "state": mapp(_settlements[operation_id])
                ? _settlements[operation_id]["phase"] : "not_written" ]);
    }
    schedule_bank_settlement_recovery(operation_id, actor_id);
    return ([ "ok": 1, "duplicate": 0, "operation_id": operation_id,
        "state": "prepared", "persisted": 1, "save_result": 1,
        "outcome": "committed", "settlement": copy(candidate) ]);
}

mapping query_bank_settlement(string operation_id)
{
    mapping record;

    if (!valid_operation_id(operation_id) || !mapp(_settlements))
        return 0;
    record = _settlements[operation_id];
    return mapp(record) ? copy(record) : 0;
}

/* 命令没有重试 token 时，只复用仍未终结的相同冻结意图；已完成意图
 * 不会吞掉用户后续发起的同金额新交易。 */
string query_pending_bank_settlement(string actor_id, string action,
    string target_id, int amount, int fee, int net)
{
    string operation_id;
    string *operation_ids;
    mapping record;
    mapping frozen;
    int i;
    int latest;
    int created_at;

    if (!bank_state_ready() || !mapp(_settlements) || actor_id == "")
        return "";
    operation_ids = keys(_settlements);
    operation_id = "";
    latest = 0;
    for (i = 0; i < sizeof(operation_ids); i++) {
        record = _settlements[operation_ids[i]];
        if (!mapp(record) || record["actor_id"] != actor_id
            || record["action"] != action
            || member_array(record["phase"], ({ "committed", "rolled_back", "failed" })) >= 0)
            continue;
        frozen = record["frozen"];
        if (!mapp(frozen) || (target_id != "" && frozen["target_id"] != target_id))
            continue;
        /* fee/net 属于原始冻结意图。重试不能因为配置已经变化而
         * 丢失原操作；transfer 的 amount 是总扣款，net 是转账本金。 */
        if (action == "transfer") {
            if (amount > 0 && frozen["net"] != amount)
                continue;
        } else if (amount > 0 && frozen["amount"] != amount) {
            continue;
        }
        created_at = intp(record["created_at"]) ? record["created_at"] : 0;
        if (created_at >= latest) {
            latest = created_at;
            operation_id = operation_ids[i];
        }
    }
    return operation_id;
}

mapping recover_bank_settlement_save_receipt()
{
    if (!bank_settlement_write_allowed())
        return ([ "ok": 0, "state": "invalid_caller" ]);
    if (retry_bank_save_receipt())
        return ([ "ok": 1, "state": "committed", "outcome": "committed" ]);
    return bank_settlement_save_result("bank_receipt", 0);
}

/* 只允许按声明的 phase 单向前进。保存写前失败恢复旧 intent；写后待收据
 * 与 unknown 保留新 phase，等待同一保存收据恢复，绝不让调用者提前执行下一腿。 */
mapping advance_bank_settlement(string operation_id, string expected_phase,
    string next_phase, mapping leg_result)
{
    mapping record;
    mapping before;
    mapping phase_result;
    mapping legs;
    string action;
    string leg;
    int save_code;

    if (!bank_settlement_write_allowed() || !valid_operation_id(operation_id)
        || !mapp(_settlements))
        return ([ "ok": 0, "state": "invalid_args" ]);
    record = _settlements[operation_id];
    if (!mapp(record))
        return ([ "ok": 0, "state": "settlement_missing",
            "operation_id": operation_id ]);
    action = record["action"];
    if (record["phase"] != expected_phase)
        return ([ "ok": 0, "state": "phase_conflict",
            "operation_id": operation_id, "phase": record["phase"] ]);
    if (!bank_settlement_transition_allowed(action, expected_phase, next_phase))
        return ([ "ok": 0, "state": "invalid_transition",
            "operation_id": operation_id, "phase": expected_phase ]);
    before = copy(record);
    record["phase"] = next_phase;
    record["updated_at"] = time();
    if (mapp(leg_result)) {
        if (stringp(leg_result["terminal_phase"])
            && leg_result["terminal_phase"] != "")
            record["terminal_phase"] = leg_result["terminal_phase"];
        leg = stringp(leg_result["leg"]) ? leg_result["leg"] : "";
        if (leg == "cash" || leg == "bank" || leg == "bank_debit"
            || leg == "bank_credit" || leg == "debit" || leg == "credit"
            || leg == "intent" || leg == "bank_intent" || leg == "cash_intent"
            || leg == "finalizing" || leg == "compensation" || leg == "cleanup") {
            legs = mapp(record["legs"]) ? record["legs"] : ([]);
            legs[leg] = copy(leg_result);
            record["legs"] = legs;
        }
    }
    _settlements[operation_id] = record;
    save_code = persist_bank_data(operation_id + "/settlement/" + next_phase);
    if (save_code == 1)
        return ([ "ok": 1, "operation_id": operation_id,
            "state": next_phase, "phase": next_phase,
            "persisted": 1, "save_result": 1, "outcome": "committed",
            "settlement": copy(record) ]);
    phase_result = bank_settlement_save_result(operation_id, save_code);
    if (phase_result["outcome"] == "not_written")
        _settlements[operation_id] = before;
    phase_result["state"] = phase_result["outcome"];
    phase_result["phase"] = _settlements[operation_id]["phase"];
    phase_result["settlement"] = copy(_settlements[operation_id]);
    if (phase_result["outcome"] != "committed")
        schedule_bank_settlement_recovery(operation_id,
            _settlements[operation_id]["actor_id"]);
    return phase_result;
}

private void schedule_bank_settlement_recovery(string operation_id,
    string actor_id)
{
    int handle;

    if (!mapp(_settlement_callouts) || !valid_operation_id(operation_id)
        || !stringp(actor_id) || actor_id == "")
        return;
    if (_settlement_callouts[operation_id])
        return;
    handle = call_out("recover_bank_settlement_timer", 1,
        operation_id, actor_id);
    if (handle >= 0)
        _settlement_callouts[operation_id] = handle;
}

void recover_bank_settlement_timer(string operation_id, string actor_id)
{
    object player;
    mapping result;
    mixed err;

    if (mapp(_settlement_callouts))
        map_delete(_settlement_callouts, operation_id);
    player = find_player_online(actor_id);
    if (!objectp(player))
        return;
    err = catch(result = ECONOMY_SETTLEMENT_D->resume_bank_settlement(
        player, operation_id));
    if (!err && mapp(result) && result["pending_recovery"])
        return;
}

private void schedule_bank_interest_recovery(string actor_id)
{
    int handle;

    if (!mapp(_interest_callouts) || !stringp(actor_id) || actor_id == ""
        || _interest_callouts[actor_id])
        return;
    handle = call_out("recover_bank_interest_timer", 1, actor_id);
    if (handle >= 0)
        _interest_callouts[actor_id] = handle;
}

void recover_bank_interest_timer(string actor_id)
{
    object player;
    mapping result;
    mixed err;

    if (mapp(_interest_callouts))
        map_delete(_interest_callouts, actor_id);
    player = find_player_online(actor_id);
    if (!objectp(player))
        return;
    err = catch(result = command_interest(player));
    if (err || !mapp(result) || result["success"] != 1) return;
}

private void rebuild_bank_recovery_callouts()
{
    string operation_id;
    string actor_id;
    string player_id;
    mapping record;
    mapping bank;
    string *ids;
    int limit;

    if (!bank_state_ready()) return;
    ids = keys(_settlements);
    limit = 0;
    foreach (operation_id in ids) {
        if (++limit > 64) break;
        record = _settlements[operation_id];
        if (!mapp(record) || member_array(record["phase"],
            ({ "committed", "rolled_back", "failed" })) >= 0)
            continue;
        actor_id = record["actor_id"];
        schedule_bank_settlement_recovery(operation_id, actor_id);
    }
    ids = keys(_player_banks);
    limit = 0;
    foreach (player_id in ids) {
        if (++limit > 64) break;
        bank = _player_banks[player_id];
        if (mapp(bank) && mapp(bank["interest_pending"]))
            schedule_bank_interest_recovery(player_id);
    }
}

mapping recover_player_bank_settlements(object player)
{
    mapping record;
    mapping result;
    mapping interest_result;
    mapping summary;
    mapping bank;
    string operation_id;
    string actor_id;
    string *ids;
    mixed err;
    int limit;

    summary = ([ "ok": 1, "recovered": 0, "pending": 0, "unknown": 0 ]);
    if (!objectp(player) || !bank_state_ready() || !mapp(_settlements))
        return summary + ([ "ok": 0, "error": "bank state unavailable" ]);

    /* 登录恢复也负责银行 owner 自身的孤立保存收据。不能只扫描
     * _settlements/interest_pending，否则终态写后失败会没有业务标记可供
     * 调度，下一次玩家登录也无法闭合原收据。 */
    if (!retry_bank_save_receipt()) {
        summary["ok"] = 0;
        if (mapp(_last_bank_save_phase)
            && _last_bank_save_phase["outcome"] == "persisted_receipt_pending")
            summary["pending"]++;
        else
            summary["unknown"]++;
    }
    actor_id = get_player_key(player);
    ids = keys(_settlements);
    limit = 0;
    foreach (operation_id in ids) {
        if (++limit > 32)
            break;
        record = _settlements[operation_id];
        if (!mapp(record) || record["actor_id"] != actor_id
            || member_array(record["phase"],
                ({ "committed", "rolled_back", "failed" })) >= 0)
            continue;
        err = catch(result = ECONOMY_SETTLEMENT_D->resume_bank_settlement(
            player, operation_id));
        if (err || !mapp(result) || result["success"] != 1) {
            if (mapp(result) && (result["pending_recovery"]
                || result["outcome"] == "persisted_receipt_pending"))
                summary["pending"]++;
            else
                summary["unknown"]++;
            summary["ok"] = 0;
            continue;
        }
        summary["recovered"]++;
    }
    bank = get_bank_data(actor_id);
    if (mapp(bank) && mapp(bank["interest_pending"])) {
        err = catch(interest_result = command_interest(player));
        bank = get_bank_data(actor_id);
        if (err || !mapp(bank) || mapp(bank["interest_pending"])) {
            summary[err ? "unknown" : "pending"]++;
            summary["ok"] = 0;
        } else {
            summary["recovered"]++;
        }
    }
    return summary;
}

private string build_restore_operation_id(string key, mapping snapshot)
{
    int balance;
    int interest_last;
    int last_access;

    if (!mapp(snapshot))
        return "restore/" + key + "/deleted";
    balance = intp(snapshot["balance"]) ? snapshot["balance"] : 0;
    interest_last = intp(snapshot["interest_last"])
        ? snapshot["interest_last"] : 0;
    last_access = intp(snapshot["last_access"])
        ? snapshot["last_access"] : 0;
    return sprintf("restore/%s/%d/%d/%d", key, balance,
        interest_last, last_access);
}

private mapping apply_balance_operation(mixed player_or_id, int amount,
    string direction, string operation_id)
{
    mapping bank;
    mapping receipt;
    string key;
    int previous_balance;
    int previous_access;
    int save_code;

    if (!bank_state_ready() || amount <= 0
        || (direction != "add" && direction != "deduct"))
        return ([ "ok": 0, "state": "invalid_args" ]);
    key = get_player_key(player_or_id);
    if (key == "")
        return ([ "ok": 0, "state": "invalid_player" ]);
    if (!valid_operation_id(operation_id))
        return ([ "ok": 0, "state": "invalid_operation_id" ]);

    receipt = _operation_receipts[operation_id];
    if (mapp(receipt)) {
        if (receipt["player_id"] != key
            || receipt["direction"] != direction
            || receipt["amount"] != amount)
            return ([ "ok": 0, "state": "operation_conflict" ]);
        if (!retry_bank_save_receipt())
            return ([ "ok": 0, "applied": 1, "duplicate": 0,
                "operation_id": operation_id,
                "balance": query_balance(key),
                "persisted": mapp(_last_bank_save_phase)
                    ? _last_bank_save_phase["persisted"] : -1,
                "state": mapp(_last_bank_save_phase)
                    ? _last_bank_save_phase["outcome"] : "unknown",
                "pending_recovery": 1,
                "retry_token": mapp(_last_bank_save_phase)
                    ? _last_bank_save_phase["retry_token"] || "" : "" ]);
        return ([
            "ok": 1,
            "applied": 0,
            "duplicate": 1,
            "operation_id": operation_id,
            "balance": receipt["balance"] || 0,
            "persisted": 1,
            "save_result": 1,
            "state": "committed",
        ]);
    }

    /* A preceding publication belongs to A, not to the unstarted B.
     * Resolve it before changing B's balance or inserting B's receipt. */
    if (!retry_bank_save_receipt())
        return ([ "ok": 0, "applied": 0, "persisted": 0,
            "save_result": 0, "operation_id": operation_id,
            "state": "blocked_by_prior_receipt", "pending_recovery": 1,
            "prior_receipt": copy(_last_bank_save_phase) ]);
    bank = get_bank_data(key);
    if (!mapp(bank))
        return ([ "ok": 0, "state": "account_unavailable" ]);
    previous_balance = bank["balance"] || 0;
    if (direction == "deduct" && previous_balance < amount)
        return ([ "ok": 0, "state": "insufficient_funds",
            "balance": previous_balance ]);
    previous_access = bank["last_access"] || 0;
    bank["balance"] = direction == "add"
        ? previous_balance + amount : previous_balance - amount;
    bank["last_access"] = time();
    _operation_receipts[operation_id] = ([
        "operation_id": operation_id,
        "player_id": key,
        "direction": direction,
        "amount": amount,
        "balance": bank["balance"],
        "applied_at": time(),
    ]);
    save_code = persist_bank_data(operation_id + "/bank");
    if (save_code == 1)
        return ([
            "ok": 1,
            "applied": 1,
            "duplicate": 0,
            "operation_id": operation_id,
            "balance": bank["balance"],
            "persisted": 1,
            "save_result": 1,
            "state": "committed",
        ]);
    if (save_code == 2 || (mapp(_last_bank_save_phase)
        && (_last_bank_save_phase["outcome"] == "persisted_receipt_pending"
            || _last_bank_save_phase["outcome"] == "unknown")))
        return ([
            "ok": 0,
            "applied": 1,
            "duplicate": 0,
            "operation_id": operation_id,
            "balance": bank["balance"],
            "persisted": save_code == 2 ? 1
                : _last_bank_save_phase["persisted"],
            "save_result": save_code == 2 ? 2
                : _last_bank_save_phase["save_result"],
            "state": save_code == 2 ? "persisted_receipt_pending"
                : _last_bank_save_phase["outcome"],
            "pending_recovery": 1,
            "retry_token": _last_bank_save_phase["retry_token"] || "",
        ]);

    bank["balance"] = previous_balance;
    bank["last_access"] = previous_access;
    map_delete(_operation_receipts, operation_id);
    return ([ "ok": 0, "applied": 0, "persisted": 0,
        "save_result": mapp(_last_bank_save_phase)
            ? _last_bank_save_phase["save_result"] : 0,
        "state": "not_written" ]);
}

mapping credit_balance_with_operation(mixed player_or_id, int amount,
    string operation_id)
{
    return apply_balance_operation(player_or_id, amount, "add", operation_id);
}

mapping debit_balance_with_operation(mixed player_or_id, int amount,
    string operation_id)
{
    return apply_balance_operation(player_or_id, amount, "deduct", operation_id);
}

int credit_balance(mixed player_or_id, int amount)
{
    string key;

    key = get_player_key(player_or_id);
    if (key == "")
        return 0;
    _legacy_operation_sequence++;
    return apply_balance_operation(player_or_id, amount, "add",
        sprintf("bank/legacy/add/%s/%s/%d", key, _operation_namespace,
            _legacy_operation_sequence))["ok"];
}

/* L-118：退款/回补入账必须区分真实保存阶段。只有 not_written 才撤销
 * 本次尚未落盘的候选；已写待收据和 unknown 保留余额与 pending 标记，
 * 重试只清理原标记，不再次入账。 */
mapping credit_balance_recoverable(mixed player_or_id, int amount,
    string operation_id)
{
    mapping bank;
    mapping pending;
    mapping phase;
    mapping receipt;
    int now;
    int previous_balance;
    int previous_access;
    int save_code;
    string state;

    if (amount <= 0 || !valid_operation_id(operation_id))
        return ([ "ok": 0, "state": "invalid_args", "applied": 0 ]);
    bank = get_bank_data(player_or_id);
    if (!mapp(bank))
        return ([ "ok": 0, "state": "account_unavailable", "applied": 0 ]);

    pending = bank["pending_recovery"];
    receipt = _operation_receipts[operation_id];
    if (mapp(receipt) && !mapp(pending)) {
        if (receipt["player_id"] != get_player_key(player_or_id)
            || receipt["direction"] != "add"
            || receipt["amount"] != amount)
            return ([ "ok": 0, "state": "operation_conflict", "applied": 0 ]);
        if (!retry_bank_save_receipt())
            return ([ "ok": 0, "state": _last_bank_save_phase["outcome"] || "unknown",
                "applied": 1, "duplicate": 1, "persisted": _last_bank_save_phase["persisted"],
                "save_result": _last_bank_save_phase["save_result"],
                "pending_recovery": 1, "operation_id": operation_id ]);
        return ([ "ok": 1, "state": "committed", "applied": 0,
            "duplicate": 1, "persisted": 1, "save_result": 1,
            "operation_id": operation_id, "balance": bank["balance"] ]);
    }

    if (mapp(pending)) {
        if (pending["operation_id"] != operation_id
            || pending["amount"] != amount)
            return ([ "ok": 0, "state": "operation_conflict", "applied": 0 ]);
        _operation_receipts[operation_id] = ([
            "operation_id": operation_id,
            "player_id": get_player_key(player_or_id),
            "direction": "add",
            "amount": amount,
            "balance": bank["balance"],
            "applied_at": pending["recorded_at"] || time(),
        ]);
        map_delete(bank, "pending_recovery");
        save_code = persist_bank_data(operation_id + "/recovery-clear");
        if (save_code == 1)
            return ([ "ok": 1, "state": "committed", "applied": 0,
                "persisted": 1, "save_result": 1,
                "operation_id": operation_id ]);
        bank["pending_recovery"] = pending;
        phase = mapp(_last_bank_save_phase) ? _last_bank_save_phase : ([]);
        state = phase["outcome"] || "unknown";
        return ([ "ok": 0, "state": state, "applied": 1,
            "persisted": intp(phase["persisted"]) ? phase["persisted"] : -1,
            "save_result": intp(phase["save_result"]) ? phase["save_result"] : 0,
            "pending_recovery": 1, "operation_id": operation_id ]);
    }

    now = time();
    previous_balance = bank["balance"] || 0;
    previous_access = bank["last_access"] || 0;
    bank["balance"] = previous_balance + amount;
    bank["last_access"] = now;
    pending = ([
        "operation_id": operation_id,
        "amount": amount,
        "recorded_at": now,
    ]);
    bank["pending_recovery"] = pending;
    _operation_receipts[operation_id] = ([
        "operation_id": operation_id,
        "player_id": get_player_key(player_or_id),
        "direction": "add",
        "amount": amount,
        "balance": bank["balance"],
        "applied_at": now,
    ]);
    save_code = persist_bank_data(operation_id + "/recovery-pending");
    phase = mapp(_last_bank_save_phase) ? _last_bank_save_phase : ([]);
    state = phase["outcome"] || (save_code == 1 ? "committed" : "unknown");
    if (save_code != 1 && state == "not_written") {
        bank["balance"] = previous_balance;
        bank["last_access"] = previous_access;
        map_delete(bank, "pending_recovery");
        map_delete(_operation_receipts, operation_id);
        return ([ "ok": 0, "state": "not_written", "applied": 0,
            "persisted": 0, "save_result": phase["save_result"] || 0,
            "operation_id": operation_id ]);
    }
    if (save_code != 1) {
        log_file("economy", sprintf(
            "%s bank recoverable credit pending persistence for %O amount %d operation %s\n",
            ctime(now), player_or_id, amount, operation_id));
        return ([ "ok": 0, "state": state, "applied": 1,
            "persisted": intp(phase["persisted"]) ? phase["persisted"] : -1,
            "save_result": intp(phase["save_result"]) ? phase["save_result"] : 0,
            "pending_recovery": 1, "operation_id": operation_id ]);
    }

    map_delete(bank, "pending_recovery");
    save_code = persist_bank_data(operation_id + "/recovery-clear");
    if (save_code == 1)
        return ([ "ok": 1, "state": "committed", "applied": 1,
            "persisted": 1, "save_result": 1,
            "operation_id": operation_id ]);
    bank["pending_recovery"] = pending;
    phase = mapp(_last_bank_save_phase) ? _last_bank_save_phase : ([]);
    state = phase["outcome"] || "unknown";
    return ([ "ok": 0, "state": state, "applied": 1,
        "persisted": intp(phase["persisted"]) ? phase["persisted"] : -1,
        "save_result": intp(phase["save_result"]) ? phase["save_result"] : 0,
        "pending_recovery": 1, "operation_id": operation_id ]);
}

mapping query_pending_bank_recovery(mixed player_or_id)
{
    mapping bank;

    bank = get_bank_data(player_or_id);
    return mapp(bank) && mapp(bank["pending_recovery"])
        ? copy(bank["pending_recovery"]) : 0;
}

int debit_balance(mixed player_or_id, int amount)
{
    string key;

    key = get_player_key(player_or_id);
    if (key == "")
        return 0;
    _legacy_operation_sequence++;
    return apply_balance_operation(player_or_id, amount, "deduct",
        sprintf("bank/legacy/deduct/%s/%s/%d", key, _operation_namespace,
            _legacy_operation_sequence))["ok"];
}

mapping command_deposit(object player, string amount_str)
{
    int amount;
    int pocket;
    mapping pending;

    if (!objectp(player))
        return 0;

    pocket = call_other(MONEY_D, "query_balance", player);
    if (amount_str == "all") {
        pending = call_other(ECONOMY_SETTLEMENT_D,
            "resume_pending_bank_settlement", player, "deposit");
        if (mapp(pending) && pending["state"] != "not_found")
            return pending;
        if (pocket <= 0)
            return build_command_result(0, "你身上没有钱可以存。\n", 0);
        amount = pocket;
    } else {
        amount = parse_money(amount_str);
        if (amount <= 0)
            return build_command_result(0, "请输入有效的金额。\n", 0);
    }

    return call_other(ECONOMY_SETTLEMENT_D, "settle_bank_deposit", player, amount);
}

mapping command_withdraw(object player, string amount_str)
{
    int amount;
    int balance;
    mapping pending;

    if (!objectp(player))
        return 0;

    balance = query_balance(player);
    if (amount_str == "all") {
        pending = call_other(ECONOMY_SETTLEMENT_D,
            "resume_pending_bank_settlement", player, "withdraw");
        if (mapp(pending) && pending["state"] != "not_found")
            return pending;
        if (balance <= 0)
            return build_command_result(0, "你在银行没有存款。\n", 0);
        amount = balance;
    } else {
        amount = parse_money(amount_str);
        if (amount <= 0)
            return build_command_result(0, "请输入有效的金额。\n", 0);
    }

    return call_other(ECONOMY_SETTLEMENT_D, "settle_bank_withdraw", player, amount);
}

mapping command_transfer(object player, string args)
{
    string target_id;
    string amount_str;
    int amount;
    int space_pos;

    if (!objectp(player))
        return 0;

    if (!stringp(args) || args == "")
        return build_command_result(0, "用法：bank transfer <玩家ID> <金额>\n", 0);

    space_pos = strsrch(args, " ");
    if (space_pos <= 0 || space_pos >= strlen(args) - 1)
        return build_command_result(0, "用法：bank transfer <玩家ID> <金额>\n", 0);

    target_id = args[0..space_pos - 1];
    amount_str = args[space_pos + 1..];
    amount = parse_money(amount_str);
    if (amount <= 0)
        return build_command_result(0, "转账金额必须大于0。\n", 0);
    return call_other(ECONOMY_SETTLEMENT_D, "settle_bank_transfer", player, target_id, amount);
}

mapping command_interest(object player)
{
    mapping money_result;
    string player_id;
    string operation_id;
    mapping bank;
    mapping intent;
    int interest;
    int last_time;
    int previous_access;
    int now;
    mapping frozen_intent;
    mapping phase;
    int save_code;

    if (!objectp(player))
        return 0;

    player_id = get_player_key(player);
    bank = get_bank_data(player_id);
    if (!stringp(player_id) || !mapp(bank))
        return build_command_result(0, "当前没有可领取利息的账户。\n", 0);

    /* 银行 owner 的上一张保存收据必须先闭合。尤其是冻结利息意图
     * 已写但终态收据未完成时，不能先发现金再补意图收据。 */
    if (!retry_bank_save_receipt()) {
        phase = mapp(_last_bank_save_phase) ? copy(_last_bank_save_phase) : ([]);
        return build_command_result(0, "银行保存收据仍在恢复，请稍后重试。\n", ([
            "pending_recovery": 1,
            "persisted": intp(phase["persisted"]) ? phase["persisted"] : -1,
            "save_result": intp(phase["save_result"]) ? phase["save_result"] : 0,
            "operation_id": mapp(bank["interest_pending"])
                ? bank["interest_pending"]["operation_id"] || "" : "",
        ]));
    }

    last_time = bank["interest_last"] || 0;
    /* 上一轮已经冻结过意图（发钱成功但周期保存失败）：原样复用，不重算。 */
    intent = mapp(bank["interest_pending"]) ? copy(bank["interest_pending"]) : 0;
    if (mapp(intent)) {
        interest = intp(intent["amount"]) ? intent["amount"] : 0;
    } else {
        if (last_time && time() - last_time < query_interest_interval())
            return build_command_result(0, "当前周期银行利息已经领取过了。\n", 0);
        intent = build_interest_intent(bank);
        interest = mapp(intent) ? intent["amount"] : 0;
        if (interest <= 0) {
            /* 真实零收益也要推进并持久化周期，否则每次调用都会重复
             * 报告同一周期而永远没有可重试的边界。 */
            now = time();
            bank["interest_last"] = now;
            operation_id = sprintf("interest/zero/%s/%d", player_id, last_time);
            save_code = persist_bank_data(operation_id + "/zero");
            if (save_code != 1) {
                phase = mapp(_last_bank_save_phase)
                    ? _last_bank_save_phase : ([]);
                if (phase["outcome"] == "not_written")
                    bank["interest_last"] = last_time;
                return build_command_result(0,
                    "银行利息周期保存失败，请稍后重试。\n", ([
                        "pending_recovery": 1,
                        "persisted": intp(phase["persisted"])
                            ? phase["persisted"] : -1,
                        "save_result": save_code,
                    ]));
            }
            return build_command_result(0, "本期银行利息为 0 文。\n", 0);
        }
        operation_id = build_interest_operation_id(player_id, intent);
        if (operation_id == "")
            return build_command_result(0, "银行利息意图无效，请稍后重试。\n", 0);
        intent["operation_id"] = operation_id;
        bank["interest_pending"] = copy(intent);
        save_code = persist_bank_data(operation_id + "/intent");
        if (save_code != 1) {
            /* 写前失败、写后待收据以及进程内重试都必须保留冻结意图；
             * 不在此处删除它再让下一次调用按新本金/利率重算。 */
            if (mapp(_last_bank_save_phase)
                && _last_bank_save_phase["outcome"] == "not_written")
                _interest_intent_needs_publish = operation_id;
            else if (save_code == 2
                || (mapp(_last_bank_save_phase)
                    && _last_bank_save_phase["persisted"] == 1))
                _interest_intent_needs_publish = "";
            return build_command_result(0, "银行利息意图保存失败，请稍后重试。\n", ([
                "pending_recovery": 1,
                "persisted": mapp(_last_bank_save_phase)
                    ? _last_bank_save_phase["persisted"] : -1,
                "save_result": save_code,
                "operation_id": operation_id,
            ]));
        }
        _interest_intent_needs_publish = "";
    }
    if (mapp(bank["interest_pending"])
        && _interest_intent_needs_publish == bank["interest_pending"]["operation_id"]) {
        save_code = persist_bank_data(
            bank["interest_pending"]["operation_id"] + "/intent-retry");
        if (save_code != 1) {
            if (save_code == 2 || (mapp(_last_bank_save_phase)
                && _last_bank_save_phase["persisted"] == 1))
                _interest_intent_needs_publish = "";
            return build_command_result(0, "银行利息意图保存失败，请稍后重试。\n", ([
                "pending_recovery": 1,
                "persisted": mapp(_last_bank_save_phase)
                    ? _last_bank_save_phase["persisted"] : -1,
                "save_result": save_code,
                "operation_id": bank["interest_pending"]["operation_id"],
            ]));
        }
        _interest_intent_needs_publish = "";
    }
    if (interest <= 0)
        return build_command_result(0, "当前没有可领取的银行利息。\n", 0);

    if (stringp(intent["operation_id"]) && intent["operation_id"] != "")
        operation_id = intent["operation_id"];
    else
        operation_id = build_interest_operation_id(player_id, intent);
    if (operation_id == "")
        return build_command_result(0, "银行利息意图无效，请稍后重试。\n", 0);
    frozen_intent = copy(intent);
    money_result = ECONOMY_SETTLEMENT_D->settle_money_operation(player,
        "bank", "interest", "add", interest, operation_id);
    if (!mapp(money_result) || money_result["ok"] != 1)
        return build_command_result(0, "银行利息发放失败。\n", 0);

    previous_access = bank["last_access"] || 0;
    bank["interest_last"] = intp(intent["period_end"])
        ? intent["period_end"] : time();
    bank["last_access"] = time();
    map_delete(bank, "interest_pending");
    save_code = persist_bank_data(operation_id + "/settled");
    if (save_code != 1) {
        phase = mapp(_last_bank_save_phase) ? _last_bank_save_phase : ([]);
        /* 已写入业务文件但终态收据未完成时，内存必须保留磁盘候选：
         * 下一次调用只补原收据，不能把已发放利息恢复成旧周期。 */
        if (save_code == 2 || phase["outcome"] == "persisted_receipt_pending"
            || phase["outcome"] == "unknown")
            return build_command_result(0, "银行利息记录保存失败，稍后将自动重试。\n", ([
                "pending_recovery": 1,
                "persisted": intp(phase["persisted"]) ? phase["persisted"] : -1,
                "save_result": save_code,
                "operation_id": operation_id,
            ]));
        bank["interest_last"] = last_time;
        bank["last_access"] = previous_access;
        bank["interest_pending"] = frozen_intent;
        return build_command_result(0, "银行利息记录保存失败，稍后将自动重试。\n", ([
            "pending_recovery": 1,
            "persisted": 0,
            "save_result": save_code,
            "operation_id": operation_id,
        ]));
    }
    return build_command_result(1, sprintf("成功领取本期银行利息：%d 文。\n", interest), ([ "interest": interest ]));
}

varargs void emit_bank_menu_ui(object me, object banker, mapping response_context)
{
    int balance;
    int pocket;
    string banker_name;

    if (!objectp(me)) return;

    balance = query_balance(me);
    pocket = call_other(MONEY_D, "query_balance", me);
    banker_name = objectp(banker) ? (banker->name() || "钱庄伙计") : "钱庄伙计";

    send_xk_system(me,
        sprintf("【%s】你的存款：%s，身上现金：%s",
            banker_name,
            format_money(balance),
            format_money(pocket)
        ),
        "info", response_context);

    send_xk_quick_cmds(me,
        ({
            (["label": "查账", "cmd": "bank check"]),
            (["label": "领取利息", "cmd": "bank interest"]),
            (["label": "查看状态", "cmd": "score"]),
        }),
        1, response_context);
    send_xk_secondary_cmds(me,
        ({
            (["label": "查看背包", "cmd": "inventory"]),
            (["label": "保存角色", "cmd": "save"]),
            (["label": "查看时间", "cmd": "time"]),
        }),
        "银行辅助",
        1, response_context);
    send_xk_dynamic_buttons(me,
        ({
            (["label": "存100文", "cmd": "bank cun 100"]),
            (["label": "存500文", "cmd": "bank cun 500"]),
            (["label": "取100文", "cmd": "bank qu 100"]),
            (["label": "取500文", "cmd": "bank qu 500"]),
        }),
        "上下文按钮",
        1, response_context);
}

varargs void emit_bank_balance_ui(object me, mapping response_context)
{
    int balance;
    int pocket;

    if (!objectp(me)) return;

    balance = query_balance(me);
    pocket = call_other(MONEY_D, "query_balance", me);

    send_xk_system(me,
        sprintf("银行存款：%s，身上现金：%s，总资产：%s",
            format_money(balance),
            format_money(pocket),
            format_money(balance + pocket)
        ),
        "info", response_context);
}
