/**
 * @file    economy_death_settlement.c
 * @brief   战斗死亡货币结算模块。
 *
 * 死亡扣钱是幂等、可审计、可回滚的 settlement operation：
 * - 计算规则（余额 10%，保留至少 1 文）只在本模块；
 * - 按 event_id 记账，重复死亡事件不重复扣款；
 * - 扣款失败返回可回滚 receipt（调用方按失败路径处理）。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

/* 幂等记账 FIFO 上限：超出后删除最旧条目，防 daemon 存活期无界增长。 */
#define DEATH_PENALTY_LEDGER_MAX 4096

nosave mapping _death_penalty_ledger;
nosave string * _death_penalty_order;
nosave mapping _last_persist_phase;

private int normalize_penalty_rate(mixed raw_rate, int fallback)
{
    int rate;

    rate = intp(raw_rate) && raw_rate > 0 ? raw_rate : fallback;
    if (rate > 100)
        rate = 100;
    return rate;
}

/**
 * 计算死亡扣款金额：余额 * rate%，保留至少 keep_min 文。
 */
int calculate_death_penalty(int balance, int rate, int keep_min)
{
    int penalty;

    if (balance <= 0)
        return 0;
    rate = normalize_penalty_rate(rate, 10);
    penalty = balance * rate / 100;
    if (penalty <= 0)
        return 0;
    if (keep_min > 0 && penalty >= balance - keep_min)
        return balance - keep_min > 0 ? balance - keep_min : 0;
    return penalty;
}

/**
 * 结算死亡扣款（幂等）。
 * options: ([ "event_id": string（战斗事件唯一 id，幂等键）,
 *             "rate": int（默认 10 = 10%），
 *             "keep_min": int（保留至少 N 文，默认 1） ])
 * 返回：([ "ok": 1, "deducted": N, "balance": M ]) 或 ([ "ok": 0, "reason": ... ])
 */
private void record_penalty_outcome(string event_id, string outcome, string last_error);
private int record_death_penalty_pending(object player, string event_id,
    int amount, int balance_before, mapping receipt);
private mapping prepare_death_penalty_intent(object player, string event_id,
    int amount, int balance_before);
private mapping clear_death_penalty_intent(object player, string event_id);

private int persist_player_data(object player)
{
    mapping phase;
    mixed err;

    if (!objectp(player) || !function_exists("save_phases", player)) {
        _last_persist_phase = ([ "outcome": "not_written",
            "persisted": 0, "save_result": 0,
            "error": "player save phases unavailable" ]);
        return 0;
    }
    err = catch(phase = player->save_phases());
    _last_persist_phase = err || !mapp(phase)
        ? ([ "outcome": "unknown", "persisted": -1, "save_result": 0,
            "error": err ? sprintf("%O", err) : "invalid_save_phase" ])
        : phase;
    return !err && mapp(phase) && phase["outcome"] == "committed";
}

/* 死亡罚金 pending 是玩家持久状态：写入必须经玩家状态 owner 提交。
 * 提交失败返回 0，调用方按“未记账”处理，不得伪报 pending 已记录。 */
private int commit_death_pending(object player, mapping pending)
{
    mapping response;

    if (!objectp(player) || !mapp(pending))
        return 0;
    response = PLAYER_STATE_D->commit_state_fields(player,
        ([ "economy/death_penalty_pending": pending ]),
        PLAYER_STATE_D->build_state_idempotency_key(player,
            "economy/death_penalty_pending", pending));
    return mapp(response) && response["ok"];
}

/* 在资金腿前冻结死亡意图。意图与资金 receipt 共用玩家 JSON，
 * 因此写后收据未完成时仍能在重载后找到原 event_id，而不会依赖
 * daemon 的 nosave ledger。 */
private mapping prepare_death_penalty_intent(object player, string event_id,
    int amount, int balance_before)
{
    mapping pending;
    mapping previous;
    mapping entry;
    mapping phase;

    if (!objectp(player) || !stringp(event_id) || event_id == ""
        || amount <= 0)
        return ([ "outcome": "not_written", "persisted": 0,
            "save_result": 0, "error": "invalid death intent" ]);
    pending = player->query("economy/death_penalty_pending");
    pending = mapp(pending) ? copy(pending) : ([]);
    entry = pending[event_id];
    if (mapp(entry)) {
        if (entry["amount"] != amount
            || entry["operation_id"] != event_id)
            return ([ "outcome": "unknown", "persisted": -1,
                "save_result": 0, "error": "death intent parameter conflict" ]);
        if (entry["state"] == "resolved")
            return ([ "outcome": "blocked_by_prior_receipt", "persisted": 0,
                "save_result": 0, "error": "death intent is resolved" ]);
        /* Existing intent may still be backed by an unresolved player JSON
         * receipt.  Close/recover that receipt before the money leg; do not
         * treat the in-memory entry as proof that the intent is durable. */
        if (!persist_player_data(player)) {
            phase = mapp(_last_persist_phase) ? _last_persist_phase
                : ([ "outcome": "unknown", "persisted": -1,
                    "save_result": 0 ]);
            if (phase["outcome"] == "not_written")
                phase["outcome"] = "unknown";
            phase["error"] = "death intent receipt requires recovery";
            return phase;
        }
        phase = mapp(_last_persist_phase) ? _last_persist_phase
            : ([ "outcome": "committed", "persisted": 1,
                "save_result": 1 ]);
        return phase + ([ "existing": 1 ]);
    }
    previous = copy(pending);
    pending[event_id] = ([
        "event_id": event_id,
        "amount": amount,
        "balance_before": balance_before,
        "operation_id": event_id,
        "state": "intent",
        "attempts": 0,
        "created_at": time(),
        "updated_at": time(),
    ]);
    if (!commit_death_pending(player, pending))
        return ([ "outcome": "unknown", "persisted": -1,
            "save_result": 0, "error": "death intent commit failed" ]);
    if (persist_player_data(player))
        return _last_persist_phase;
    phase = mapp(_last_persist_phase) ? _last_persist_phase
        : ([ "outcome": "unknown", "persisted": -1, "save_result": 0 ]);
    if (phase["outcome"] == "not_written") {
        if (!commit_death_pending(player, previous))
            log_file("economy", sprintf(
                "%s death intent rollback failed for %s\n",
                ctime(time()), PLAYER_STATE_D->query_player_id(player)));
    }
    return phase;
}

/* 资金腿完成后清除意图。只有明确 not_written 才恢复“仍待处理”的
 * 内存候选；已写待收据/unknown 保留新候选，由玩家 receipt owner 接管。 */
private mapping clear_death_penalty_intent(object player, string event_id)
{
    mapping pending;
    mapping previous;
    mapping phase;

    pending = objectp(player) ? player->query("economy/death_penalty_pending") : 0;
    if (!mapp(pending) || undefinedp(pending[event_id]))
        return ([ "outcome": "committed", "persisted": 1,
            "save_result": 1, "already_clear": 1 ]);
    previous = copy(pending);
    pending = copy(pending);
    map_delete(pending, event_id);
    if (!commit_death_pending(player, pending))
        return ([ "outcome": "unknown", "persisted": -1,
            "save_result": 0, "error": "death intent clear commit failed" ]);
    if (persist_player_data(player))
        return _last_persist_phase;
    phase = mapp(_last_persist_phase) ? _last_persist_phase
        : ([ "outcome": "unknown", "persisted": -1, "save_result": 0 ]);
    if (phase["outcome"] == "not_written") {
        if (!commit_death_pending(player, previous))
            log_file("economy", sprintf(
                "%s death intent clear rollback failed for %s\n",
                ctime(time()), PLAYER_STATE_D->query_player_id(player)));
    }
    return phase;
}

mapping settle_death_penalty(object player, mapping options)
{
    mapping result;
    string event_id;
    int balance;
    int rate;
    int keep_min;
    int penalty;
    mapping receipt;
    mapping intent_phase;
    mapping clear_phase;

    result = ([ "ok": 0, "deducted": 0, "balance": 0 ]);
    if (!objectp(player) || !userp(player))
        return result;
    if (!mapp(_death_penalty_ledger)) {
        _death_penalty_ledger = ([]);
        _death_penalty_order = ({ });
    }

    event_id = mapp(options) ? (stringp(options["event_id"]) ? options["event_id"] : "") : "";
    if (event_id == "")
        return result + ([ "reason": "missing event_id" ]);

    /* 幂等：只有已应用、已确认重复或零扣款才是终态；失败/unknown
     * 必须继续按 pending/人工核对流程处理，不能伪报成功。 */
    if (mapp(_death_penalty_ledger[event_id])) {
        if (_death_penalty_ledger[event_id]["outcome"] == "applied"
            || _death_penalty_ledger[event_id]["outcome"] == "duplicate"
            || _death_penalty_ledger[event_id]["outcome"] == "zero")
            return result + ([ "ok": 1, "deducted": 0,
                "balance": MONEY_D->query_balance(player), "idempotent": 1 ]);
        return result + ([ "reason": _death_penalty_ledger[event_id]["last_error"]
                || "death penalty requires recovery",
            "state": _death_penalty_ledger[event_id]["outcome"] ]);
    }

    /* FIFO 上限：超限删最旧，防无界增长（LPC_CODE_STANDARD 天花板声明） */
    if (sizeof(_death_penalty_ledger) >= DEATH_PENALTY_LEDGER_MAX
        && sizeof(_death_penalty_order) > 0) {
        map_delete(_death_penalty_ledger, _death_penalty_order[0]);
        _death_penalty_order = _death_penalty_order[1..];
    }

    balance = MONEY_D->query_balance(player);
    rate = mapp(options) ? (intp(options["rate"]) ? options["rate"] : 10) : 10;
    keep_min = mapp(options) ? (intp(options["keep_min"]) ? options["keep_min"] : 1) : 1;
    penalty = calculate_death_penalty(balance, rate, keep_min);
    if (penalty <= 0) {
        record_penalty_outcome(event_id, "zero", "");
        return result + ([ "ok": 1, "deducted": 0, "balance": balance ]);
    }

    intent_phase = prepare_death_penalty_intent(player, event_id, penalty,
        balance);
    if (!mapp(intent_phase) || intent_phase["outcome"] != "committed") {
        record_penalty_outcome(event_id,
            mapp(intent_phase) && intent_phase["outcome"]
                ? intent_phase["outcome"] : "unknown",
            mapp(intent_phase) && intent_phase["error"]
                ? intent_phase["error"] : "death intent is pending");
        return result + ([
            "reason": mapp(intent_phase) && intent_phase["error"]
                ? intent_phase["error"] : "death intent requires recovery",
            "state": mapp(intent_phase) && intent_phase["outcome"]
                ? intent_phase["outcome"] : "unknown",
            "pending_recovery": 1,
            "persisted": mapp(intent_phase)
                ? intent_phase["persisted"] : -1,
        ]);
    }

    /* 幂等判据唯一化——持久层（MONEY_D->apply_money_operation）返回结构化
     * receipt，按 applied/duplicate 如实透传，不反查猜语义；内存 ledger 仅
     * 作 fast-path 去重。unknown（prepared 崩溃窗口）不虚报已结算，记录到
     * 玩家持久 pending 供运维核对/恢复。 */
    receipt = MONEY_D->apply_money_operation(player, ([
        "operation_id": event_id,
        "direction": "deduct",
        "amount": penalty,
        "domain": "death",
        "action": "penalty",
    ]));
    if (!mapp(receipt) || !receipt["ok"]) {
        /* 失败/unknown 记录到玩家持久 pending（event-id mapping，
         * 不覆盖写），daemon ledger 同步记 outcome 供健康检查。 */
        /* persisted_receipt_pending/unknown 已经把原 intent 与资金候选
         * 绑定到同一保存尝试；此时不能再用第二次 save 改写候选。
         * 明确未写的失败才更新 intent 状态并持久化待重试记录。 */
        if (mapp(receipt)
            && (receipt["pending_recovery"]
                || receipt["state"] == "persisted_receipt_pending"
                || receipt["state"] == "blocked_by_prior_receipt"
                || receipt["state"] == "unknown")) {
            record_penalty_outcome(event_id, receipt["state"],
                receipt["last_error"] || "death penalty requires recovery");
            return result + ([
                "reason": receipt["last_error"] || "death penalty requires recovery",
                "state": receipt["state"],
                "pending_recorded": 1,
                "pending_recovery": 1,
                "persisted": receipt["persisted"],
            ]);
        }
        record_penalty_outcome(event_id,
            mapp(receipt) && receipt["state"] == "unknown"
                ? "unknown" : "failed",
            mapp(receipt) ? receipt["last_error"] : "deduct failed");
        return result + ([ "reason": receipt && receipt["last_error"] ? receipt["last_error"] : "deduct failed",
            "state": mapp(receipt) && receipt["state"]
                ? receipt["state"] : "deduct_failed",
            "pending_recorded": record_death_penalty_pending(player,
                event_id, penalty, balance, receipt) ]);
    }
    clear_phase = clear_death_penalty_intent(player, event_id);
    if (!mapp(clear_phase) || clear_phase["outcome"] != "committed") {
        record_penalty_outcome(event_id,
            mapp(clear_phase) && clear_phase["outcome"]
                ? clear_phase["outcome"] : "unknown",
            mapp(clear_phase) && clear_phase["error"]
                ? clear_phase["error"] : "death intent cleanup pending");
        return result + ([
            "reason": mapp(clear_phase) && clear_phase["error"]
                ? clear_phase["error"] : "death intent cleanup requires recovery",
            "state": mapp(clear_phase) && clear_phase["outcome"]
                ? clear_phase["outcome"] : "unknown",
            "deducted": receipt["duplicate"] ? 0 : penalty,
            "balance": MONEY_D->query_balance(player),
            "pending_recovery": 1,
            "persisted": mapp(clear_phase)
                ? clear_phase["persisted"] : -1,
        ]);
    }
    record_penalty_outcome(event_id,
        receipt["duplicate"] ? "duplicate" : "applied", "");
    if (receipt["duplicate"]) {
        return result + ([ "ok": 1, "deducted": 0, "balance": balance,
            "idempotent": 1, "state": receipt["state"] ]);
    }
    return result + ([ "ok": 1, "deducted": penalty,
        "balance": MONEY_D->query_balance(player), "state": "committed" ]);
}

/* 失败/unknown 死亡罚金持久 pending（event-id mapping，不覆盖写）。
 * 每项含 amount/balance_before/operation_id/state/attempts/timestamps；
 * 连续失败递增 attempts，不丢前一事件。 */
private int record_death_penalty_pending(object player, string event_id,
    int amount, int balance_before, mapping receipt)
{
    mapping pending;
    mapping previous_pending;
    mapping entry;

    if (!objectp(player) || !stringp(event_id) || event_id == "")
        return 0;
    pending = player->query("economy/death_penalty_pending");
    if (!mapp(pending))
        pending = ([]);
    previous_pending = copy(pending);
    if (mapp(pending[event_id]))
        previous_pending[event_id] = copy(pending[event_id]);
    entry = pending[event_id];
    if (!mapp(entry)) {
        entry = ([
            "event_id": event_id,
            "amount": amount,
            "balance_before": balance_before,
            "operation_id": event_id,
            "state": "pending",
            "attempts": 0,
            "created_at": time(),
            "updated_at": time(),
        ]);
    }
    entry["attempts"] = (entry["attempts"] || 0) + 1;
    entry["state"] = mapp(receipt) && receipt["state"]
        ? receipt["state"] : "deduct_failed";
    entry["last_error"] = mapp(receipt) && receipt["last_error"]
        ? receipt["last_error"] : "deduct failed";
    entry["updated_at"] = time();
    pending[event_id] = entry;
    if (!commit_death_pending(player, pending)) {
        log_file("economy", sprintf("%s death penalty pending commit failed for %s\n",
            ctime(time()), PLAYER_STATE_D->query_player_id(player)));
        return 0;
    }
    if (!persist_player_data(player)) {
        /* pending 记录本身也是业务状态：只有明确 not_written 才回到
         * 原快照。写后待收据或 unknown 保留冻结候选，禁止清除或重放。
         */
        if (mapp(_last_persist_phase)
            && _last_persist_phase["outcome"] == "not_written") {
            if (!commit_death_pending(player, previous_pending))
                log_file("economy", sprintf(
                    "%s death penalty pending rollback failed for %s\n",
                    ctime(time()), PLAYER_STATE_D->query_player_id(player)));
        }
        log_file("economy", sprintf("%s death penalty pending save failed for %s: %O\n",
            ctime(time()), PLAYER_STATE_D->query_player_id(player),
            _last_persist_phase));
        return 0;
    }
    return 1;
}

/* 恢复单条死亡罚金 pending（同 operation id 幂等重试）。
 * unknown 状态不可盲扣——需 admin reconcile（余额证据核对）。 */
private mapping recover_one_death_pending(object player, mapping pending,
    string event_id, mapping entry)
{
    mapping receipt;

    if (!mapp(entry) || !intp(entry["amount"]) || entry["amount"] <= 0)
        return ([ "ok": 0, "error": "invalid pending entry",
            "event_id": event_id ]);
    /* resolved 为终态：admin 人工核对后保留条目作审计，
     * 登录自动重试不得撤销 resolve 决定。 */
    if (entry["state"] == "resolved")
        return ([ "ok": 1, "skipped": 1, "reason": "resolved",
            "event_id": event_id ]);
    if (entry["state"] == "unknown") {
        return ([ "ok": 0, "error": "unknown state requires admin reconcile",
            "event_id": event_id, "amount": entry["amount"],
            "balance_before": entry["balance_before"] ]);
    }
    receipt = MONEY_D->apply_money_operation(player, ([
        "operation_id": entry["operation_id"],
        "direction": "deduct",
        "amount": entry["amount"],
        /* 同一 operation_id 重试必须复用原始 action；改变 action 会被
         * MONEY_D 的 receipt 指纹判定为参数冲突。 */
        "domain": "death",
        "action": "penalty",
    ]));
    if (mapp(receipt) && receipt["ok"]) {
        map_delete(pending, event_id);
        if (!commit_death_pending(player, pending)) {
            pending[event_id] = entry;
            log_file("economy", sprintf("%s death penalty recovery commit failed for %s\n",
                ctime(time()), PLAYER_STATE_D->query_player_id(player)));
            return ([ "ok": 0, "recovered": 0,
                "error": "pending commit failed", "event_id": event_id ]);
        }
        if (!persist_player_data(player)) {
            /* 只有 not_written 才能恢复 pending。写后收据待补或 unknown
             * 仍保留“已消费”的内存候选，避免把已落盘的清理重新变成
             * 一次可重放的扣款；原保存 owner 负责补原收据。 */
            if (mapp(_last_persist_phase)
                && _last_persist_phase["outcome"] == "not_written") {
                pending[event_id] = entry;
                if (!commit_death_pending(player, pending))
                    log_file("economy", sprintf(
                        "%s death penalty recovery rollback commit failed for %s\n",
                        ctime(time()), PLAYER_STATE_D->query_player_id(player)));
            }
            log_file("economy", sprintf("%s death penalty recovery save failed for %s: %O\n",
                ctime(time()), PLAYER_STATE_D->query_player_id(player),
                _last_persist_phase));
            return ([ "ok": 0, "recovered": 0,
                "state": mapp(_last_persist_phase)
                    ? _last_persist_phase["outcome"] : "unknown",
                "persisted": mapp(_last_persist_phase)
                    ? _last_persist_phase["persisted"] : -1,
                "pending_recovery": 1,
                "error": "pending save failed", "event_id": event_id ]);
        }
        return ([ "ok": 1, "recovered": 1,
            "deducted": receipt["applied"] ? entry["amount"] : 0,
            "duplicate": receipt["duplicate"] ? 1 : 0,
            "state": receipt["state"], "event_id": event_id ]);
    }
    /* 资金腿已落盘但原收据尚未闭合时，pending 意图已经绑定在同一
     * 玩家候选中；禁止第二次 save 覆盖候选，也不能把待恢复状态伪装成
     * 普通 deduct_failed。unknown 同样保留原意图，交给 receipt owner。 */
    if (mapp(receipt)
        && (receipt["pending_recovery"]
            || receipt["state"] == "persisted_receipt_pending"
            || receipt["state"] == "blocked_by_prior_receipt"
            || receipt["state"] == "unknown"))
        return ([ "ok": 0,
            "state": receipt["state"] || "unknown",
            "persisted": receipt["persisted"],
            "pending_recovery": 1,
            "error": receipt["last_error"] || "death penalty receipt requires recovery",
            "event_id": event_id ]);
    entry["attempts"] = (entry["attempts"] || 0) + 1;
    entry["state"] = "retry_failed";
    entry["last_error"] = mapp(receipt) && receipt["last_error"]
        ? receipt["last_error"] : "deduct failed";
    entry["updated_at"] = time();
    pending[event_id] = entry;
    if (!commit_death_pending(player, pending))
        log_file("economy", sprintf(
            "%s death penalty retry state commit failed for %s\n",
            ctime(time()), PLAYER_STATE_D->query_player_id(player)));
    else if (!persist_player_data(player))
        log_file("economy", sprintf("%s death penalty retry state save failed for %s\n",
            ctime(time()), PLAYER_STATE_D->query_player_id(player)));
    return ([ "ok": 0, "error": entry["last_error"], "event_id": event_id ]);
}

/* 恢复死亡罚金 pending。event_id 为空时恢复全部；
 * 返回 ([ ok, recovered, pending, results ])。 */
mapping recover_death_penalty(object player, string event_id)
{
    mapping pending;
    mapping entry;
    mapping summary;
    mapping one;
    mapping *results;
    string *ids;
    string id;
    int recovered;
    int all_ok;

    if (!objectp(player))
        return ([ "ok": 0, "error": "invalid player" ]);
    pending = player->query("economy/death_penalty_pending");
    if (!mapp(pending) || sizeof(pending) == 0)
        return ([ "ok": 1, "recovered": 0, "pending": 0,
            "results": ({ }) ]);

    summary = ([ "ok": 1, "recovered": 0, "pending": sizeof(pending) ]);
    if (stringp(event_id) && event_id != "") {
        entry = pending[event_id];
        if (!mapp(entry))
            return summary + ([ "ok": 0, "error": "no such pending event" ]);
        return recover_one_death_pending(player, pending, event_id, entry);
    }
    results = ({ });
    all_ok = 1;
    ids = keys(pending);
    foreach (id in ids) {
        entry = pending[id];
        if (!mapp(entry)) {
            all_ok = 0;
            results += ({ ([ "ok": 0, "error": "invalid pending entry",
                "event_id": id ]) });
            continue;
        }
        one = recover_one_death_pending(player, pending, id, entry);
        results += ({ one });
        if (!mapp(one) || !one["ok"])
            all_ok = 0;
        if (mapp(one) && one["ok"] && one["recovered"])
            recovered++;
        pending = player->query("economy/death_penalty_pending");
        if (!mapp(pending))
            break;
    }
    return ([ "ok": all_ok, "recovered": recovered,
        "pending": mapp(pending) ? sizeof(pending) : 0,
        "results": results ]);
}

/* owner 内 resolve——人工核对后标记条目（记录操作者），
 * cmd 层只编排不直接改写玩家持久 key。条目不存在返回错误。 */
mapping resolve_death_pending(object player, string event_id, string operator)
{
    mapping pending;
    mapping entry;
    mapping entry_snapshot;

    if (!objectp(player) || !stringp(event_id) || event_id == "")
        return ([ "ok": 0, "error": "invalid args" ]);
    pending = player->query("economy/death_penalty_pending");
    if (!mapp(pending) || !mapp(pending[event_id]))
        return ([ "ok": 0, "error": "no such pending event" ]);
    entry = pending[event_id];
    entry_snapshot = copy(entry);
    entry["resolved_by"] = stringp(operator) && operator != ""
        ? operator : "?";
    entry["resolved_at"] = time();
    entry["state"] = "resolved";
    pending[event_id] = entry;
    if (!commit_death_pending(player, pending))
        return ([ "ok": 0, "error": "pending commit failed",
            "event_id": event_id ]);
    if (!persist_player_data(player)) {
        /* resolved 标记也是持久候选：写后待收据/unknown 时保留新状态，
         * 仅明确 not_written 才恢复人工核对前的条目。 */
        if (mapp(_last_persist_phase)
            && _last_persist_phase["outcome"] == "not_written") {
            pending[event_id] = entry_snapshot;
            if (!commit_death_pending(player, pending))
                log_file("economy", sprintf(
                    "%s death penalty resolve rollback commit failed for %s\n",
                    ctime(time()), PLAYER_STATE_D->query_player_id(player)));
        }
        return ([ "ok": 0,
            "state": mapp(_last_persist_phase)
                ? _last_persist_phase["outcome"] : "unknown",
            "persisted": mapp(_last_persist_phase)
                ? _last_persist_phase["persisted"] : -1,
            "pending_recovery": 1,
            "error": "pending save failed", "event_id": event_id ]);
    }
    return ([ "ok": 1, "state": "resolved", "event_id": event_id ]);
}

/** 审计快照：死亡扣款记账条目数。 */
/* 内存 ledger 记录结算 outcome（结构值，非时间戳 int）。
 * applied/duplicate/zero 为终态；failed/unknown 记录 last_error 供
 * 健康检查（玩家持久 pending 承载恢复，daemon ledger 只作审计）。 */
private void record_penalty_outcome(string event_id, string outcome, string last_error)
{
    if (!stringp(event_id) || event_id == "")
        return;
    if (!mapp(_death_penalty_ledger))
        _death_penalty_ledger = ([]);
    if (mapp(_death_penalty_ledger[event_id])) {
        _death_penalty_ledger[event_id]["completed_at"] = time();
        _death_penalty_ledger[event_id]["outcome"] = outcome;
        _death_penalty_ledger[event_id]["last_error"] =
            stringp(last_error) ? last_error : "";
        return;
    }
    _death_penalty_ledger[event_id] = ([
        "completed_at": time(),
        "outcome": outcome,
        "last_error": stringp(last_error) ? last_error : "",
    ]);
    _death_penalty_order += ({ event_id });
    if (sizeof(_death_penalty_order) > DEATH_PENALTY_LEDGER_MAX) {
        map_delete(_death_penalty_ledger, _death_penalty_order[0]);
        _death_penalty_order = _death_penalty_order[1..];
    }
}

mapping query_death_penalty_snapshot()
{
    mapping entry;
    mapping snapshot;
    int oldest;
    int now;
    string id;
    string *ids;
    string last_error;

    /* outcome 计数与最旧账龄从真实结构派生（ledger 值为
     * ([ completed_at, outcome, last_error ])）。pending = failed+unknown
     * 计数（玩家持久 pending 的 daemon 侧审计视图）。 */
    snapshot = ([
        "ledger_entries": mapp(_death_penalty_ledger) ? sizeof(_death_penalty_ledger) : 0,
        "applied": 0,
        "duplicate": 0,
        "zero": 0,
        "pending": 0,
        "unknown": 0,
        "oldest_age_seconds": 0,
        "last_error": "",
    ]);
    if (!mapp(_death_penalty_ledger) || sizeof(_death_penalty_ledger) == 0)
        return snapshot;
    now = time();
    ids = keys(_death_penalty_ledger);
    oldest = now;
    last_error = "";
    foreach (id in ids) {
        entry = _death_penalty_ledger[id];
        if (mapp(entry) && intp(entry["completed_at"])) {
            if (entry["completed_at"] < oldest)
                oldest = entry["completed_at"];
            if (entry["outcome"] == "applied")
                snapshot["applied"]++;
            else if (entry["outcome"] == "duplicate")
                snapshot["duplicate"]++;
            else if (entry["outcome"] == "zero")
                snapshot["zero"]++;
            else if (entry["outcome"] == "unknown")
                snapshot["unknown"]++;
            else if (entry["outcome"] == "failed")
                snapshot["pending"]++;
            if (stringp(entry["last_error"]) && entry["last_error"] != "")
                last_error = entry["last_error"];
        }
    }
    snapshot["oldest_age_seconds"] = now - oldest;
    snapshot["last_error"] = last_error;
    return snapshot;
}

void create()
{
    /* 死亡罚金 pending 恢复经 player_lifecycle manifest 装配
     * （handler=ECONOMY_SETTLEMENT_D recover_death_penalty），本模块
     * 不再 create 时注册。无父类 create，无需链式调用。 */
}
