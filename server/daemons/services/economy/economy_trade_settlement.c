/**
 * @file    economy_trade_settlement.c
 * @brief   玩家交易税费与双向结算模块。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

float normalize_rate(mixed raw_rate, float fallback);
mapping query_economy_trading_config();
mapping build_result(int success, string message, mapping extra);
int queue_compensation_with_operation(object who, string domain, string action,
    string actor_id, string subject_id, string operation_id, int amount);

private mapping apply_trade_money(object player, string operation_id,
    string direction, int amount, string action)
{
    return MONEY_D->apply_money_operation(player, ([
        "operation_id": operation_id,
        "direction": direction,
        "amount": amount,
        "domain": "trade",
        "action": action,
    ]));
}

private string trade_money_operation_id(mapping session, object player,
    string leg)
{
    return sprintf("trade/%s/%s/%s", session["id"] || "session",
        get_object_id(player) || "player", leg);
}

private string trade_item_operation_id(mapping session, object player,
    int index, string leg)
{
    return sprintf("trade/%s/%s/item/%s/%d", session["id"] || "session",
        get_object_id(player) || "player", leg, index);
}

private int trade_money_receipt_matches(object player, string operation_id,
    int amount, string direction, string action)
{
    mapping receipt;

    if (!objectp(player) || !stringp(operation_id) || operation_id == "")
        return 0;
    receipt = MONEY_D->query_money_operation_receipt(player, operation_id);
    if (!mapp(receipt))
        return 0;
    return receipt["amount"] == amount && receipt["direction"] == direction
        && receipt["domain"] == "trade" && receipt["action"] == action;
}

private int trade_item_transfer_done(object item, string operation_id,
    object target)
{
    return objectp(item) && objectp(target)
        && environment(item) == target
        && ITEM_INVENTORY_D->query_item_transfer_operation(item,
            operation_id, target);
}

private int trade_money_receipt_conflict(object player, string operation_id,
    int amount, string direction, string action)
{
    mapping receipt;

    if (!objectp(player) || !stringp(operation_id) || operation_id == "")
        return 0;
    receipt = MONEY_D->query_money_operation_receipt(player, operation_id);
    if (!mapp(receipt))
        return 0;
    return !trade_money_receipt_matches(player, operation_id, amount,
        direction, action);
}

float query_trade_tax_rate()
{
    mapping trading_config;

    trading_config = query_economy_trading_config();
    return normalize_rate(trading_config["tax_rate"], 0.0);
}

int query_trade_tax(int amount)
{
    if (!intp(amount) || amount <= 0)
        return 0;

    return to_int(amount * query_trade_tax_rate());
}

int query_trade_timeout()
{
    mapping trading_config;

    trading_config = query_economy_trading_config();
    return intp(trading_config["trade_timeout"]) ? trading_config["trade_timeout"] : 60;
}

int query_trade_min_skill_cap()
{
    mapping trading_config;

    trading_config = query_economy_trading_config();
    return intp(trading_config["min_trade_skill_cap"]) ? trading_config["min_trade_skill_cap"] : 0;
}

mapping query_trade_runtime_snapshot()
{
    return ([
        "enabled": query_economy_trading_config()["enabled"] ? 1 : 0,
        "tax_rate": query_trade_tax_rate(),
        "trade_timeout": query_trade_timeout(),
        "scope": "same_room",
        "min_trade_skill_cap": query_trade_min_skill_cap(),
    ]);
}

private int actor_meets_trade_gate(object who)
{
    if (!objectp(who))
        return 0;

    return NUMERIC_RULE_D->player_meets_skill_cap_gate(who, query_trade_min_skill_cap());
}

private int can_trade_same_room(object left, object right)
{
    object left_room;
    object right_room;

    if (!objectp(left) || !objectp(right))
        return 0;
    left_room = environment(left);
    right_room = environment(right);
    return objectp(left_room) && left_room == right_room;
}

private mapping save_trade_player(object player)
{
    mapping phase;
    mixed error;

    if (!objectp(player) || !function_exists("save_phases", player))
        return ([ "ok": 0, "outcome": "not_written", "persisted": 0,
            "save_result": 0, "error": "player_save_phases_unavailable" ]);
    error = catch(phase = player->save_phases());
    if (error || !mapp(phase))
        return ([ "ok": 0, "outcome": "unknown", "persisted": -1,
            "save_result": 0,
            "error": error ? sprintf("%O", error) : "invalid_save_phase" ]);
    return phase;
}

private mapping pending_trade_result(mapping session, mapping phase,
    string message)
{
    return build_result(0, message, ([
        "action": "trade",
        "session_id": mapp(session) ? session["id"] : "",
        "pending_recovery": 1,
        "save_phase": phase,
    ]));
}

private mapping rollback_trade_settlement(mapping state)
{
    mapping money_result;
    mapping save_phase;
    mapping result;
    int index;
    int rollback_failed;
    int compensation_queued;
    mapping transfer;
    mixed *transfers;

    if (!mapp(state))
        return ([ "success": 0, "message": "交易失败，请稍后再试。\n" ]);

    if (state["left_received"] > 0) {
        money_result = apply_trade_money(state["left_player"],
            state["left_received_operation"] + "/rollback", "deduct",
            state["left_received"], "receive_rollback");
        if (!mapp(money_result) || money_result["ok"] != 1)
            rollback_failed = 1;
    }
    if (state["right_received"] > 0) {
        money_result = apply_trade_money(state["right_player"],
            state["right_received_operation"] + "/rollback", "deduct",
            state["right_received"], "receive_rollback");
        if (!mapp(money_result) || money_result["ok"] != 1)
            rollback_failed = 1;
    }

    transfers = arrayp(state["right_transfers"]) ? state["right_transfers"] : ({ });
    for (index = sizeof(transfers) - 1; index >= 0; index--) {
        transfer = transfers[index];
        if (!ITEM_INVENTORY_D->rollback_player_item_transfer(transfer))
            rollback_failed = 1;
    }

    transfers = arrayp(state["left_transfers"]) ? state["left_transfers"] : ({ });
    for (index = sizeof(transfers) - 1; index >= 0; index--) {
        transfer = transfers[index];
        if (!ITEM_INVENTORY_D->rollback_player_item_transfer(transfer))
            rollback_failed = 1;
    }

    if (state["left_spent"] > 0) {
        /* 回滚失败进可恢复队列（不忽略返回值）。 */
        money_result = apply_trade_money(state["left_player"],
            state["left_spent_operation"] + "/refund", "add",
            state["left_spent"], "rollback_refund");
        if (!mapp(money_result) || money_result["ok"] != 1) {
            rollback_failed = 1;
            compensation_queued |= queue_compensation_with_operation(
                state["left_player"], "trade", "rollback_refund",
                get_object_id(state["left_player"]), "trade",
                state["left_spent_operation"] + "/refund",
                state["left_spent"]);
        }
    }
    if (state["right_spent"] > 0) {
        money_result = apply_trade_money(state["right_player"],
            state["right_spent_operation"] + "/refund", "add",
            state["right_spent"], "rollback_refund");
        if (!mapp(money_result) || money_result["ok"] != 1) {
            rollback_failed = 1;
            compensation_queued |= queue_compensation_with_operation(
                state["right_player"], "trade", "rollback_refund",
                get_object_id(state["right_player"]), "trade",
                state["right_spent_operation"] + "/refund",
                state["right_spent"]);
        }
    }

    if (arrayp(state["left_transfers"]) && sizeof(state["left_transfers"])) {
        save_phase = save_trade_player(state["left_player"]);
        if (!mapp(save_phase) || save_phase["outcome"] != "committed")
            rollback_failed = 1;
    }
    if (arrayp(state["right_transfers"]) && sizeof(state["right_transfers"])) {
        save_phase = save_trade_player(state["right_player"]);
        if (!mapp(save_phase) || save_phase["outcome"] != "committed")
            rollback_failed = 1;
    }
    result = ([ "success": 0,
        "message": state["error"] || "交易失败，请稍后再试。\n" ]);
    if (rollback_failed || compensation_queued) {
        result["pending_recovery"] = 1;
        result["compensation_queued"] = compensation_queued;
    }
    return result;
}

mapping settle_player_trade(mapping session)
{
    mapping left_offer;
    mapping right_offer;
    mapping state;
    mapping transfer;
    mapping money_result;
    mapping save_phase;
    mixed item;
    mixed *left_items;
    mixed *right_items;
    int left_money;
    int left_tax;
    int right_money;
    int right_tax;
    object left;
    object right;
    int index;

    left = mapp(session) ? session["left_player"] : 0;
    right = mapp(session) ? session["right_player"] : 0;
    if (!objectp(left) || !objectp(right))
        return build_result(0, "交易对象已不在线。\n", 0);
    if (environment(left) != environment(right) || !can_trade_same_room(left, right))
        return build_result(0, "双方不在同一房间，无法完成交易。\n", 0);
    if (!actor_meets_trade_gate(left) || !actor_meets_trade_gate(right))
        return build_result(0, "任一方战斗经验不足，无法交易。\n", 0);

    left_offer = mapp(session["offers"]) && mapp(session["offers"][session["left_id"]])
        ? session["offers"][session["left_id"]]
        : ([]);
    right_offer = mapp(session["offers"]) && mapp(session["offers"][session["right_id"]])
        ? session["offers"][session["right_id"]]
        : ([]);
    left_items = arrayp(left_offer["items"]) ? left_offer["items"] : ({ });
    right_items = arrayp(right_offer["items"]) ? right_offer["items"] : ({ });
    left_money = intp(left_offer["money"]) ? left_offer["money"] : 0;
    right_money = intp(right_offer["money"]) ? right_offer["money"] : 0;
    left_tax = query_trade_tax(left_money);
    right_tax = query_trade_tax(right_money);

    for (index = 0; index < sizeof(left_items); index++) {
        item = left_items[index];
        if (!objectp(item)
            || (environment(item) != left
                && !trade_item_transfer_done(item,
                    trade_item_operation_id(session, left, index, "left"), right)))
            return build_result(0, "你提供的物品已无法交易。\n", ([ "error": "item_not_owned" ]));
        if (!trade_item_transfer_done(item,
                trade_item_operation_id(session, left, index, "left"), right)
            && ((function_exists("query_equipped", item) && item->query_equipped())
                || item->query("no_give") || item->query("no_drop")))
            return build_result(0, "你提供的物品已无法交易。\n", ([ "error": "item_locked" ]));
    }
    for (index = 0; index < sizeof(right_items); index++) {
        item = right_items[index];
        if (!objectp(item)
            || (environment(item) != right
                && !trade_item_transfer_done(item,
                    trade_item_operation_id(session, right, index, "right"), left)))
            return build_result(0, "对方提供的物品已无法交易。\n", ([ "error": "item_not_owned" ]));
        if (!trade_item_transfer_done(item,
                trade_item_operation_id(session, right, index, "right"), left)
            && ((function_exists("query_equipped", item) && item->query_equipped())
                || item->query("no_give") || item->query("no_drop")))
            return build_result(0, "对方提供的物品已无法交易。\n", ([ "error": "item_locked" ]));
    }
    if (left_money + left_tax > 0) {
        if (trade_money_receipt_conflict(left,
                trade_money_operation_id(session, left, "debit"),
                left_money + left_tax, "deduct", "debit"))
            return build_result(0, "交易操作参数冲突，请使用原始报价重试。\n", ([
                "error": "operation_conflict" ]));
        if (!trade_money_receipt_matches(left,
                trade_money_operation_id(session, left, "debit"),
                left_money + left_tax, "deduct", "debit")
            && !MONEY_D->can_afford(left, left_money + left_tax))
            return build_result(0, "你身上现金不足，无法完成交易。\n", 0);
    }
    if (right_money + right_tax > 0) {
        if (trade_money_receipt_conflict(right,
                trade_money_operation_id(session, right, "debit"),
                right_money + right_tax, "deduct", "debit"))
            return build_result(0, "交易操作参数冲突，请使用原始报价重试。\n", ([
                "error": "operation_conflict" ]));
        if (!trade_money_receipt_matches(right,
                trade_money_operation_id(session, right, "debit"),
                right_money + right_tax, "deduct", "debit")
            && !MONEY_D->can_afford(right, right_money + right_tax))
            return build_result(0, "对方现金不足，无法完成交易。\n", 0);
    }

    state = ([
        "left_player": left,
        "right_player": right,
        "left_spent": 0,
        "right_spent": 0,
        "left_received": 0,
        "right_received": 0,
        "left_transfers": ({ }),
        "right_transfers": ({ }),
        "left_spent_operation": trade_money_operation_id(session, left,
            "debit"),
        "right_spent_operation": trade_money_operation_id(session, right,
            "debit"),
        "left_received_operation": trade_money_operation_id(session, left,
            "credit"),
        "right_received_operation": trade_money_operation_id(session, right,
            "credit"),
        "error": "交易提交失败。\n",
    ]);

    if (left_money + left_tax > 0) {
        money_result = apply_trade_money(left, state["left_spent_operation"],
            "deduct", left_money + left_tax, "debit");
        if (!mapp(money_result) || money_result["ok"] != 1) {
            if (mapp(money_result) && money_result["pending_recovery"])
                return pending_trade_result(session, money_result,
                    "交易资金正在恢复，请使用相同请求重试。\n");
            return build_result(0, "你身上现金不足，无法完成交易。\n", 0);
        }
        state["left_spent"] = left_money + left_tax;
    }
    if (right_money + right_tax > 0) {
        money_result = apply_trade_money(right,
            state["right_spent_operation"], "deduct",
            right_money + right_tax, "debit");
        if (!mapp(money_result) || money_result["ok"] != 1) {
            if (mapp(money_result) && money_result["pending_recovery"])
                return pending_trade_result(session, money_result,
                    "交易资金正在恢复，请使用相同请求重试。\n");
            state["error"] = "对方现金不足，无法完成交易。\n";
            return rollback_trade_settlement(state);
        }
        state["right_spent"] = right_money + right_tax;
    }

    for (index = 0; index < sizeof(left_items); index++) {
        item = left_items[index];
        transfer = ITEM_INVENTORY_D->transfer_item_between_players_with_operation(
            left, right, item,
            trade_item_operation_id(session, left, index, "left"));
        if (!mapp(transfer) || !transfer["ok"]) {
            state["error"] = "你提供的物品无法交付给对方。\n";
            return rollback_trade_settlement(state);
        }
        state["left_transfers"] += ({ transfer });
    }
    save_phase = save_trade_player(right);
    if (save_phase["outcome"] != "committed") {
        state["error"] = "交易物品正在保存，请使用相同请求编号重试。\n";
        return pending_trade_result(session, save_phase, state["error"]);
    }
    for (index = 0; index < sizeof(right_items); index++) {
        item = right_items[index];
        transfer = ITEM_INVENTORY_D->transfer_item_between_players_with_operation(
            right, left, item,
            trade_item_operation_id(session, right, index, "right"));
        if (!mapp(transfer) || !transfer["ok"]) {
            state["error"] = "对方提供的物品无法交付给你。\n";
            return rollback_trade_settlement(state);
        }
        state["right_transfers"] += ({ transfer });
    }
    save_phase = save_trade_player(left);
    if (save_phase["outcome"] != "committed") {
        state["error"] = "交易物品正在保存，请使用相同请求编号重试。\n";
        return pending_trade_result(session, save_phase, state["error"]);
    }

    if (right_money > 0) {
        money_result = apply_trade_money(left, state["left_received_operation"],
            "add", right_money, "credit");
        if (!mapp(money_result) || money_result["ok"] != 1) {
            if (mapp(money_result) && money_result["pending_recovery"])
                return pending_trade_result(session, money_result,
                    "交易资金正在恢复，请使用相同请求重试。\n");
            state["error"] = "无法向你发放对方提供的现金。\n";
            return rollback_trade_settlement(state);
        }
        state["left_received"] = right_money;
    }
    if (left_money > 0) {
        money_result = apply_trade_money(right,
            state["right_received_operation"], "add", left_money, "credit");
        if (!mapp(money_result) || money_result["ok"] != 1) {
            if (mapp(money_result) && money_result["pending_recovery"])
                return pending_trade_result(session, money_result,
                    "交易资金正在恢复，请使用相同请求重试。\n");
            state["error"] = "无法向对方发放你提供的现金。\n";
            return rollback_trade_settlement(state);
        }
        state["right_received"] = left_money;
    }

    save_phase = save_trade_player(left);
    if (save_phase["outcome"] != "committed")
        return pending_trade_result(session, save_phase,
            "交易结果正在保存，请使用相同请求编号重试。\n");
    save_phase = save_trade_player(right);
    if (save_phase["outcome"] != "committed")
        return pending_trade_result(session, save_phase,
            "交易结果正在保存，请使用相同请求编号重试。\n");

    return build_result(1, "", ([
        "action": "trade",
        "left_id": session["left_id"] || "",
        "right_id": session["right_id"] || "",
        "left_money": left_money,
        "right_money": right_money,
        "left_tax": left_tax,
        "right_tax": right_tax,
        "left_items": sizeof(left_items),
        "right_items": sizeof(right_items),
        "refresh_status": 1,
        "refresh_inventory": 1,
    ]));
}
