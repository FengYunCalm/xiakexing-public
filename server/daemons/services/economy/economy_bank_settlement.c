/**
 * @file    economy_bank_settlement.c
 * @brief   银行手续费、存取款与转账结算模块。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

mapping query_economy_bank_config();
mapping build_result(int success, string message, mapping extra);
float normalize_rate(mixed raw_rate, float fallback);
string issue_money_operation_id(string domain, string action,
    string actor_id, string subject_id);
int queue_compensation_with_operation(object who, string domain, string action,
    string actor_id, string subject_id, string operation_id, int amount);
mapping settle_bank_deposit_with_operation(object player, int amount,
    string operation_id);
mapping settle_bank_withdraw_with_operation(object player, int amount,
    string operation_id);
mapping settle_bank_transfer_with_operation(object player, string target_id,
    int amount, string operation_id);

float query_bank_transaction_fee_rate()
{
    mapping bank_config;

    bank_config = query_economy_bank_config();
    return normalize_rate(bank_config["transaction_fee"], 0.01);
}

int query_bank_deposit_fee(int amount)
{
    float fee_rate;

    if (!intp(amount) || amount <= 0)
        return 0;
    fee_rate = query_bank_transaction_fee_rate();
    return to_int(amount * fee_rate);
}

int query_bank_transfer_fee(int amount)
{
    float fee_rate;

    if (!intp(amount) || amount <= 0)
        return 0;
    fee_rate = query_bank_transaction_fee_rate();
    return to_int(amount * fee_rate);
}

mapping settle_bank_deposit(object player, int amount)
{
    return settle_bank_deposit_with_operation(player, amount, 0);
}

private mapping build_command_result(int success, string message, mapping extra)
{
    return build_result(success, message, extra);
}

private int bank_settlement_leg_committed(mapping result)
{
    if (!mapp(result) || result["ok"] != 1) return 0;
    return result["state"] == "committed" || result["duplicate"] == 1;
}

private int bank_settlement_leg_pending(mapping result)
{
    string state;

    if (!mapp(result)) return 1;
    if (result["pending_recovery"] == 1) return 1;
    state = result["state"];
    return state == "persisted_receipt_pending" || state == "unknown" ||
           state == "blocked_by_prior_receipt";
}

private mapping bank_settlement_progress(mapping record, int success, string message, mapping extra)
{
    mapping values;

    values = ([
        "operation_id": record["operation_id"],
        "state": record["phase"],
        "settlement": record,
        "pending_recovery": success ? 0 : 1,
    ]);
    if (mapp(extra)) values += extra;
    return build_command_result(success, message, values);
}

private mapping bank_settlement_leg(mapping result, string leg)
{
    mapping copy_result;

    copy_result = mapp(result) ? copy(result) : ([]);
    copy_result["leg"] = leg;
    return copy_result;
}

private mapping advance_bank_settlement(string operation_id, string expected_phase,
    string next_phase, mapping leg_result)
{
    mapping result;

    result = BANK_D->advance_bank_settlement(
        operation_id, expected_phase, next_phase, leg_result);
    if (!mapp(result)) {
        return ([
            "ok": 0,
            "state": "unknown",
            "pending_recovery": 1,
            "operation_id": operation_id,
        ]);
    }
    return result;
}

mapping resume_bank_settlement(object player, string operation_id)
{
    mapping record;
    mapping result;
    mapping step;
    string phase;
    string action;
    string actor_id;
    string owner_id;
    string cash_operation_id;
    string bank_operation_id;
    string refund_operation_id;
    int amount;
    int transfer_amount;
    int fee;
    int net;
    int loop;

    if (!objectp(player) || !stringp(operation_id) || operation_id == "") {
        return build_command_result(0, "银行操作参数无效。", (["state": "invalid_request"]));
    }

    record = BANK_D->query_bank_settlement(operation_id);
    if (!mapp(record)) {
        return build_command_result(0, "银行操作不存在。", ([
            "state": "not_found", "operation_id": operation_id,
        ]));
    }
    actor_id = PLAYER_STATE_D->query_player_id(player);
    if (record["actor_id"] != actor_id) {
        return build_command_result(0, "银行操作身份不匹配。", ([
            "state": "operation_conflict", "operation_id": operation_id,
        ]));
    }
    result = BANK_D->recover_bank_settlement_save_receipt();
    if (!mapp(result) || result["ok"] != 1)
        return bank_settlement_progress(record, 0, "银行操作等待原保存收据恢复。", result);

    action = record["action"];
    amount = record["frozen"]["amount"];
    transfer_amount = record["frozen"]["transfer_amount"] || amount;
    fee = record["frozen"]["fee"];
    net = record["frozen"]["net"];
    cash_operation_id = operation_id;
    bank_operation_id = operation_id + "/bank";
    refund_operation_id = operation_id + "/refund";
    owner_id = "BANK_SETTLEMENT/" + operation_id;

    for (loop = 0; loop < 16; loop++) {
        record = BANK_D->query_bank_settlement(operation_id);
        if (!mapp(record)) return build_command_result(0, "银行操作记录丢失。", ([
            "state": "unknown", "operation_id": operation_id,
        ]));
        phase = record["phase"];

        if (phase == "committed") {
            return bank_settlement_progress(record, 1, "银行操作已完成。", ([
                "amount": amount, "fee": fee, "net": net,
            ]));
        }
        if (phase == "rolled_back" || phase == "failed") {
            return bank_settlement_progress(record, 0, "银行操作未完成。", ([
                "amount": amount, "fee": fee, "net": net,
                "pending_recovery": 0,
            ]));
        }

        if (phase == "prepared") {
            step = advance_bank_settlement(operation_id, "prepared",
                action == "deposit" ? "cash_pending" : "bank_pending",
                (["leg": "intent"]));
            if (step["ok"] != 1) return bank_settlement_progress(record, 0, "银行操作等待保存确认。", step);
            continue;
        }

        if (action == "deposit" && phase == "cash_pending") {
            if (MONEY_D->pin_money_operation(player, cash_operation_id, owner_id) != 1) {
                return bank_settlement_progress(record, 0, "现金操作保护记录保存失败。", ([
                    "state": "persisted_receipt_pending",
                ]));
            }
            result = ECONOMY_SETTLEMENT_D->settle_money_operation(
                player, "bank", "deposit", "deduct", amount, cash_operation_id);
            if (bank_settlement_leg_committed(result)) {
                step = advance_bank_settlement(operation_id, "cash_pending", "cash_committed",
                    bank_settlement_leg(result, "cash"));
                if (step["ok"] != 1) return bank_settlement_progress(record, 0, "银行操作等待保存确认。", step);
                continue;
            }
            step = advance_bank_settlement(operation_id, "cash_pending", "cash_pending",
                bank_settlement_leg(result, "cash"));
            if (bank_settlement_leg_pending(result)) return bank_settlement_progress(record, 0, "现金扣款正在恢复。", ([
                "leg_result": result,
            ]));
            advance_bank_settlement(operation_id, "cash_pending", "failed", bank_settlement_leg(result, "cash"));
            record = BANK_D->query_bank_settlement(operation_id);
            return bank_settlement_progress(record, 0, "现金不足或现金扣款失败。", ([
                "leg_result": result, "pending_recovery": 0,
            ]));
        }

        if (action == "deposit" && phase == "cash_committed") {
            step = advance_bank_settlement(operation_id, "cash_committed", "bank_pending", (["leg": "bank_intent"]));
            if (step["ok"] != 1) return bank_settlement_progress(record, 0, "银行操作等待保存确认。", step);
            continue;
        }

        if ((action == "deposit" || action == "withdraw") && phase == "bank_pending") {
            if (action == "deposit") {
                result = BANK_D->credit_balance_with_operation(actor_id, net, bank_operation_id);
            } else {
                result = BANK_D->debit_balance_with_operation(actor_id, amount, bank_operation_id);
            }
            if (bank_settlement_leg_committed(result)) {
                step = advance_bank_settlement(operation_id, "bank_pending", "bank_committed",
                    bank_settlement_leg(result, "bank"));
                if (step["ok"] != 1) return bank_settlement_progress(record, 0, "银行操作等待保存确认。", step);
                continue;
            }
            if (bank_settlement_leg_pending(result)) {
                step = advance_bank_settlement(operation_id, "bank_pending", "bank_pending",
                    bank_settlement_leg(result, "bank"));
                return bank_settlement_progress(record, 0, "银行余额正在恢复。", (["leg_result": result]));
            }
            if (action == "withdraw") {
                advance_bank_settlement(operation_id, "bank_pending", "failed", bank_settlement_leg(result, "bank"));
                record = BANK_D->query_bank_settlement(operation_id);
                return bank_settlement_progress(record, 0, "银行余额不足或扣款失败。", ([
                    "leg_result": result, "pending_recovery": 0,
                ]));
            }
            result = ECONOMY_SETTLEMENT_D->settle_money_operation(player, "bank",
                "deposit_refund", "add", amount, refund_operation_id);
            if (bank_settlement_leg_committed(result)) {
                if (MONEY_D->unpin_money_operation(player, cash_operation_id, owner_id) != 1) {
                    step = advance_bank_settlement(operation_id, "bank_pending", "compensation_pending",
                        bank_settlement_leg(result, "compensation"));
                    record = BANK_D->query_bank_settlement(operation_id);
                    return bank_settlement_progress(record, 0, "银行存款补偿已完成，等待清理保护记录。", (["leg_result": result]));
                }
                step = advance_bank_settlement(operation_id, "bank_pending", "rolled_back",
                    bank_settlement_leg(result, "compensation"));
                record = BANK_D->query_bank_settlement(operation_id);
                return bank_settlement_progress(record, 0, "银行存款已回滚。", ([
                    "leg_result": result, "pending_recovery": step["ok"] == 1 ? 0 : 1,
                ]));
            }
            step = advance_bank_settlement(operation_id, "bank_pending", "compensation_pending",
                bank_settlement_leg(result, "compensation"));
            record = BANK_D->query_bank_settlement(operation_id);
            return bank_settlement_progress(record, 0, "银行存款补偿正在恢复。", ([
                "leg_result": result,
            ]));
        }

        if (action == "deposit" && phase == "bank_committed") {
            step = advance_bank_settlement(operation_id, "bank_committed", "finalizing", (["leg": "finalizing"]));
            if (step["ok"] != 1) return bank_settlement_progress(record, 0, "银行操作等待保存确认。", step);
            continue;
        }

        if (action == "withdraw" && phase == "bank_committed") {
            if (MONEY_D->pin_money_operation(player, cash_operation_id, owner_id) != 1) {
                return bank_settlement_progress(record, 0, "现金操作保护记录保存失败。", ([
                    "state": "persisted_receipt_pending",
                ]));
            }
            step = advance_bank_settlement(operation_id, "bank_committed", "cash_pending", (["leg": "cash_intent"]));
            if (step["ok"] != 1) return bank_settlement_progress(record, 0, "银行操作等待保存确认。", step);
            continue;
        }

        if (action == "withdraw" && phase == "cash_pending") {
            result = ECONOMY_SETTLEMENT_D->settle_money_operation(player, "bank",
                "withdraw", "add", amount, cash_operation_id);
            if (bank_settlement_leg_committed(result)) {
                step = advance_bank_settlement(operation_id, "cash_pending", "finalizing",
                    bank_settlement_leg(result, "cash"));
                if (step["ok"] != 1) return bank_settlement_progress(record, 0, "银行操作等待保存确认。", step);
                continue;
            }
            if (bank_settlement_leg_pending(result)) {
                step = advance_bank_settlement(operation_id, "cash_pending", "cash_pending",
                    bank_settlement_leg(result, "cash"));
                return bank_settlement_progress(record, 0, "取款现金正在恢复。", (["leg_result": result]));
            }
            result = BANK_D->credit_balance_with_operation(actor_id, amount, operation_id + "/rollback");
            if (bank_settlement_leg_committed(result)) {
                if (MONEY_D->unpin_money_operation(player, cash_operation_id, owner_id) != 1) {
                    step = advance_bank_settlement(operation_id, "cash_pending", "compensation_pending",
                        bank_settlement_leg(result, "compensation"));
                    record = BANK_D->query_bank_settlement(operation_id);
                    return bank_settlement_progress(record, 0, "取款已补偿，等待清理保护记录。", (["leg_result": result]));
                }
                step = advance_bank_settlement(operation_id, "cash_pending", "rolled_back",
                    bank_settlement_leg(result, "compensation"));
                record = BANK_D->query_bank_settlement(operation_id);
                return bank_settlement_progress(record, 0, "银行取款已回滚。", ([
                    "leg_result": result, "pending_recovery": step["ok"] == 1 ? 0 : 1,
                ]));
            }
            step = advance_bank_settlement(operation_id, "cash_pending", "compensation_pending",
                bank_settlement_leg(result, "compensation"));
            return bank_settlement_progress(record, 0, "银行取款补偿正在恢复。", step);
        }

        if (phase == "compensation_pending") {
            if (action == "deposit") {
                result = ECONOMY_SETTLEMENT_D->settle_money_operation(player, "bank",
                    "deposit_refund", "add", amount, refund_operation_id);
            } else {
                result = BANK_D->credit_balance_with_operation(actor_id, amount, operation_id + "/rollback");
            }
            if (bank_settlement_leg_committed(result)) {
                if ((action == "deposit" || action == "withdraw") &&
                    MONEY_D->unpin_money_operation(player, cash_operation_id, owner_id) != 1) {
                    step = advance_bank_settlement(operation_id, "compensation_pending", "compensation_pending",
                        bank_settlement_leg(result, "compensation"));
                    return bank_settlement_progress(record, 0, "补偿已完成，等待清理保护记录。", step);
                }
                step = advance_bank_settlement(operation_id, "compensation_pending", "rolled_back",
                    bank_settlement_leg(result, "compensation"));
                record = BANK_D->query_bank_settlement(operation_id);
                return bank_settlement_progress(record, 0, "银行操作已回滚。", ([
                    "leg_result": result, "pending_recovery": step["ok"] == 1 ? 0 : 1,
                ]));
            }
            step = advance_bank_settlement(operation_id, "compensation_pending", "compensation_pending",
                bank_settlement_leg(result, "compensation"));
            return bank_settlement_progress(record, 0, "补偿仍在恢复。", step);
        }

        if (phase == "finalizing") {
            if ((action == "deposit" || action == "withdraw") &&
                MONEY_D->unpin_money_operation(player, cash_operation_id, owner_id) != 1) {
                step = advance_bank_settlement(operation_id, "finalizing", "finalizing", (["leg": "cleanup", "state": "unknown"]));
                return bank_settlement_progress(record, 0, "银行操作等待清理保护记录。", step);
            }
            step = advance_bank_settlement(operation_id, "finalizing", "committed", (["leg": "cleanup", "state": "committed"]));
            if (step["ok"] != 1) return bank_settlement_progress(record, 0, "银行操作等待保存确认。", step);
            continue;
        }

        if (action == "transfer" && phase == "bank_pending") {
            result = BANK_D->debit_balance_with_operation(actor_id,
                amount, operation_id + "/debit");
            if (bank_settlement_leg_committed(result)) {
                step = advance_bank_settlement(operation_id, "bank_pending", "debit_committed",
                    bank_settlement_leg(result, "debit"));
                if (step["ok"] != 1) return bank_settlement_progress(record, 0, "银行转账等待保存确认。", step);
                continue;
            }
            if (bank_settlement_leg_pending(result)) {
                step = advance_bank_settlement(operation_id, "bank_pending", "bank_pending",
                    bank_settlement_leg(result, "debit"));
                return bank_settlement_progress(record, 0, "转账扣款正在恢复。", (["leg_result": result]));
            }
            advance_bank_settlement(operation_id, "bank_pending", "failed", bank_settlement_leg(result, "debit"));
            record = BANK_D->query_bank_settlement(operation_id);
            return bank_settlement_progress(record, 0, "转账扣款失败。", ([
                "leg_result": result, "pending_recovery": 0,
            ]));
        }

        if (action == "transfer" && phase == "debit_committed") {
            step = advance_bank_settlement(operation_id, "debit_committed", "credit_pending", (["leg": "credit_intent"]));
            if (step["ok"] != 1) return bank_settlement_progress(record, 0, "银行转账等待保存确认。", step);
            continue;
        }

        if (action == "transfer" && phase == "credit_pending") {
            result = BANK_D->credit_balance_with_operation(
                record["frozen"]["target_id"], transfer_amount, operation_id + "/credit");
            if (bank_settlement_leg_committed(result)) {
                step = advance_bank_settlement(operation_id, "credit_pending", "finalizing",
                    bank_settlement_leg(result, "credit"));
                if (step["ok"] != 1) return bank_settlement_progress(record, 0, "银行转账等待保存确认。", step);
                continue;
            }
            if (bank_settlement_leg_pending(result)) {
                step = advance_bank_settlement(operation_id, "credit_pending", "credit_pending",
                    bank_settlement_leg(result, "credit"));
                return bank_settlement_progress(record, 0, "转账入账正在恢复。", (["leg_result": result]));
            }
            result = BANK_D->credit_balance_with_operation(actor_id, amount, operation_id + "/rollback");
            if (bank_settlement_leg_committed(result)) {
                step = advance_bank_settlement(operation_id, "credit_pending", "rolled_back",
                    bank_settlement_leg(result, "compensation"));
                record = BANK_D->query_bank_settlement(operation_id);
                return bank_settlement_progress(record, 0, "银行转账已回滚。", ([
                    "leg_result": result, "pending_recovery": step["ok"] == 1 ? 0 : 1,
                ]));
            }
            step = advance_bank_settlement(operation_id, "credit_pending", "compensation_pending",
                bank_settlement_leg(result, "compensation"));
            return bank_settlement_progress(record, 0, "转账补偿正在恢复。", step);
        }

        return bank_settlement_progress(record, 0, "银行操作状态不受支持。", ([
            "state": "unknown", "pending_recovery": 1,
        ]));
    }

    return bank_settlement_progress(record, 0, "银行操作恢复次数超过上限。", ([
        "state": "unknown", "pending_recovery": 1,
    ]));
}

/* 显式 operation_id 和无 token 的相同意图都必须先检查冻结记录。
 * 这样现金或余额已经扣除后，重试不会再次经过当前余额预检，也不会
 * 因手续费配置变化而重建一笔不同的操作。 */
private mapping resume_existing_bank_operation(object player, string operation_id,
    string action, string target_id, int amount)
{
    mapping record;
    mapping frozen;
    string actor_id;
    string pending_id;
    int frozen_amount;

    if (!objectp(player)) return 0;
    actor_id = PLAYER_STATE_D->query_player_id(player);
    if (!stringp(operation_id) || operation_id == "") {
        pending_id = BANK_D->query_pending_bank_settlement(
            actor_id, action, target_id, amount, 0, 0);
        if (!stringp(pending_id) || pending_id == "") return 0;
        operation_id = pending_id;
    }
    record = BANK_D->query_bank_settlement(operation_id);
    if (!mapp(record)) return 0;
    frozen = record["frozen"];
    frozen_amount = action == "transfer"
        ? (mapp(frozen) ? frozen["transfer_amount"] : 0)
        : (mapp(frozen) ? frozen["amount"] : 0);
    if (record["actor_id"] != actor_id || record["action"] != action
        || !mapp(frozen) || frozen["target_id"] != target_id
        || frozen_amount != amount)
        return build_command_result(0, "银行操作身份或冻结参数不匹配。", ([
            "state": "operation_conflict", "operation_id": operation_id,
        ]));
    return resume_bank_settlement(player, operation_id);
}

mapping resume_pending_bank_settlement(object player, string action)
{
    string actor_id;
    string operation_id;
    string target_id;

    if (!objectp(player) || !stringp(action) || action == "")
        return build_command_result(0, "银行操作参数无效。", (["state": "invalid_request"]));
    actor_id = PLAYER_STATE_D->query_player_id(player);
    target_id = action == "transfer" ? "" : actor_id;
    operation_id = BANK_D->query_pending_bank_settlement(
        actor_id, action, target_id, 0, 0, 0);
    if (!stringp(operation_id) || operation_id == "")
        return build_command_result(0, "没有可恢复的银行操作。", (["state": "not_found"]));
    return resume_bank_settlement(player, operation_id);
}

mapping settle_bank_deposit_with_operation(object player, int amount,
    string operation_id)
{
    mapping record;
    mapping frozen;
    mapping existing;
    string player_id;
    int fee;
    int net;

    if (!objectp(player) || amount <= 0)
        return build_result(0, "请输入有效的金额。\n", 0);
    player_id = PLAYER_STATE_D->query_player_id(player);
    existing = resume_existing_bank_operation(player, operation_id,
        "deposit", player_id, amount);
    if (mapp(existing)) return existing;
    fee = query_bank_deposit_fee(amount);
    net = amount - fee;
    if (net <= 0)
        return build_result(0, "当前手续费配置使这笔存款无法入账。\n", 0);
    if (!MONEY_D->can_afford(player, amount))
        return build_result(0, "你身上没有那么多钱。\n", 0);
    if (!stringp(operation_id) || operation_id == "")
        operation_id = issue_money_operation_id("bank", "deposit", player_id, "deposit");
    frozen = ([
        "actor_id": player_id,
        "action": "deposit",
        "target_id": player_id,
        "amount": amount,
        "fee": fee,
        "net": net,
    ]);
    record = BANK_D->prepare_bank_settlement(player, operation_id, "deposit", frozen);
    if (!mapp(record) || record["ok"] != 1)
        return build_result(0, "存款意图保存失败，请稍后重试。\n", ([
            "state": mapp(record) ? record["state"] : "unknown",
            "operation_id": operation_id,
            "pending_recovery": mapp(record) && record["pending_recovery"] ? 1 : 0,
        ]));
    return resume_bank_settlement(player, operation_id);
}

mapping settle_bank_withdraw(object player, int amount)
{
    return settle_bank_withdraw_with_operation(player, amount, 0);
}

mapping settle_bank_withdraw_with_operation(object player, int amount,
    string operation_id)
{
    mapping record;
    mapping frozen;
    mapping existing;
    string player_id;

    if (!objectp(player) || amount <= 0)
        return build_result(0, "请输入有效的金额。\n", 0);
    player_id = PLAYER_STATE_D->query_player_id(player);
    existing = resume_existing_bank_operation(player, operation_id,
        "withdraw", player_id, amount);
    if (mapp(existing)) return existing;
    if (BANK_D->query_balance(player) < amount)
        return build_result(0, "你的存款不足。\n", 0);
    if (!stringp(operation_id) || operation_id == "")
        operation_id = issue_money_operation_id("bank", "withdraw", player_id, "withdraw");
    frozen = ([
        "actor_id": player_id,
        "action": "withdraw",
        "target_id": player_id,
        "amount": amount,
        "fee": 0,
        "net": amount,
    ]);
    record = BANK_D->prepare_bank_settlement(player, operation_id, "withdraw", frozen);
    if (!mapp(record) || record["ok"] != 1)
        return build_result(0, "取款意图保存失败，请稍后重试。\n", ([
            "state": mapp(record) ? record["state"] : "unknown",
            "operation_id": operation_id,
            "pending_recovery": mapp(record) && record["pending_recovery"] ? 1 : 0,
        ]));
    return resume_bank_settlement(player, operation_id);
}

mapping settle_bank_transfer(object player, string target_id, int amount)
{
    return settle_bank_transfer_with_operation(player, target_id, amount, 0);
}

mapping settle_bank_transfer_with_operation(object player, string target_id,
    int amount, string requested_operation_id)
{
    mapping record;
    mapping frozen;
    string from_id;
    string operation_id;
    int fee;
    int total_cost;
    mapping existing;

    if (!objectp(player) || !stringp(target_id) || amount <= 0)
        return build_result(0, "用法：bank transfer <玩家ID> <金额>\n", 0);
    target_id = trim(target_id);
    if (target_id == "")
        return build_result(0, "用法：bank transfer <玩家ID> <金额>\n", 0);
    from_id = PLAYER_STATE_D->query_player_id(player);
    if (target_id == from_id)
        return build_result(0, "不能给自己转账。\n", 0);
    existing = resume_existing_bank_operation(player, requested_operation_id,
        "transfer", target_id, amount);
    if (mapp(existing)) return existing;
    if (!PLAYER_D->account_exists(target_id))
        return build_result(0, sprintf("找不到收款账号 %s。\n", target_id), 0);

    fee = query_bank_transfer_fee(amount);
    total_cost = amount + fee;
    if (BANK_D->query_balance(player) < total_cost)
        return build_result(0, "你的存款不足。\n", 0);
    operation_id = stringp(requested_operation_id) && requested_operation_id != ""
        ? requested_operation_id
        : issue_money_operation_id("bank", "transfer", from_id, target_id);
    frozen = ([
        "actor_id": from_id,
        "action": "transfer",
        "target_id": target_id,
        "amount": total_cost,
        "transfer_amount": amount,
        "fee": fee,
        "net": amount,
    ]);
    record = BANK_D->prepare_bank_settlement(player, operation_id, "transfer", frozen);
    if (!mapp(record) || record["ok"] != 1)
        return build_result(0, "转账意图保存失败，请稍后重试。\n", ([
            "state": mapp(record) ? record["state"] : "unknown",
            "operation_id": operation_id,
            "pending_recovery": mapp(record) && record["pending_recovery"] ? 1 : 0,
        ]));
    return resume_bank_settlement(player, operation_id);
}
