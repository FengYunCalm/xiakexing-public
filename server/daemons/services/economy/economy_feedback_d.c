/**
 * @file    economy_feedback_d.c
 * @brief   经济反馈守护进程,格式化货币数值等经济展示文案。
 */

#pragma strict_types
#pragma save_types

#include <system/ansi.h>
#include <system/services.h>

inherit "/lib/framework/daemon";

private string format_money_value(int amount)
{
    return MONEY_D->format_money(amount);
}

string format_shop_trade_notice(string action, string item_name, int price)
{
    string name;
    string price_label;

    name = stringp(item_name) && item_name != "" ? item_name : "物品";
    price_label = format_money_value(price);
    if (action == "sell")
        return HIG + "你卖出了" + name + "，获得" + price_label + "。" + NOR + "\n";

    return HIG + "你购买了" + name + "，花费" + price_label + "。" + NOR + "\n";
}

string format_bulk_sell_notice(int count)
{
    if (count <= 0)
        return "没有可出售的物品。\n";
    return HIG + "你卖出了" + sprintf("%d", count) + "件物品。" + NOR + "\n";
}

varargs void emit_trade_failure_result(object player, string action, object item,
    string message, mapping response_context)
{
    mapping extra_payload;
    mapping tx;
    string item_name;

    if (!objectp(player))
        return;

    item_name = objectp(item) ? (item->query_name() || item->query("name") || "物品") : "物品";
    extra_payload = CLIENT_PROJECTION_D->build_item_client_payload(item, ([]));
    tx = CLIENT_SYNC_D->tx_add_trade_result(0, action, item_name, 0, 0,
        extra_payload);
    tx = CLIENT_SYNC_D->tx_with_response_context(tx, response_context);
    CLIENT_SYNC_D->submit_transaction(player, tx);
    msg_send(player, stringp(message) && message != "" ? message : "交易失败。\n",
        response_context);
}

varargs void sync_shop_trade_state(object player, mapping response_context)
{
    if (!objectp(player))
        return;

    CLIENT_SYNC_D->request_status_inventory(player, response_context);
}

varargs int emit_shop_settlement_result(object player, mapping result,
    mapping response_context)
{
    string action;
    string item_name;
    mapping extra_payload;
    mapping tx;
    int price;
    int success;

    if (!objectp(player) || !mapp(result))
        return 0;

    action = stringp(result["action"]) ? result["action"] : "";
    item_name = stringp(result["item"]) ? result["item"] : "物品";
    price = intp(result["price"]) ? result["price"] : 0;
    success = result["success"] || 0;
    extra_payload = CLIENT_PROJECTION_D->build_item_client_payload(0, result);
    if (stringp(result["price_label"]))
        extra_payload["price_label"] = result["price_label"];
    if (intp(result["balance"]))
        extra_payload["balance"] = result["balance"];
    if (stringp(result["balance_label"]))
        extra_payload["balance_label"] = result["balance_label"];

    tx = CLIENT_SYNC_D->tx_add_trade_result(0, action, item_name, price,
        success, extra_payload);
    if (!success) {
        tx = CLIENT_SYNC_D->tx_with_response_context(tx, response_context);
        CLIENT_SYNC_D->submit_transaction(player, tx);
        msg_send(player, stringp(result["message"]) ? result["message"] : "交易失败。\n",
            response_context);
        return 1;
    }

    tx = CLIENT_SYNC_D->tx_plan_status_inventory(tx);
    tx = CLIENT_SYNC_D->tx_with_response_context(tx, response_context);
    CLIENT_SYNC_D->submit_transaction(player, tx);
    msg_send(player, format_shop_trade_notice(action, item_name, price),
        response_context);
    return 1;
}

string format_bank_command_message(mapping result)
{
    string action;
    string target_id;
    int amount;
    int fee;
    int net;
    int balance;

    if (!mapp(result))
        return "银行操作失败。\n";

    if (!(result["success"] || 0))
        return stringp(result["message"]) ? result["message"] : "银行操作失败。\n";

    action = result["action"];
    amount = intp(result["amount"]) ? result["amount"] : 0;
    fee = intp(result["fee"]) ? result["fee"] : 0;
    net = intp(result["net"]) ? result["net"] : amount;
    balance = intp(result["balance"]) ? result["balance"] : 0;
    target_id = stringp(result["target_id"]) ? result["target_id"] : "";

    switch (action) {
        case "deposit":
            return HIG + sprintf("存款成功！已存入%s，手续费%s，入账%s。当前存款：%s。",
                format_money_value(amount), format_money_value(fee), format_money_value(net), format_money_value(balance)) + NOR + "\n";
        case "withdraw":
            return HIG + sprintf("取款成功！已取出%s。当前存款：%s。",
                format_money_value(amount), format_money_value(balance)) + NOR + "\n";
        case "transfer":
            return HIG + sprintf("转账成功！已向%s转账%s，手续费%s。当前存款：%s。",
                target_id != "" ? target_id : "对方", format_money_value(amount), format_money_value(fee), format_money_value(balance)) + NOR + "\n";
        default:
            return stringp(result["message"]) ? result["message"] : "银行操作成功。\n";
    }
}

void sync_bank_command_state(object player, object bankd, mapping result,
    mapping response_context)
{
    if (!objectp(player) || !mapp(result) || !(result["success"] || 0))
        return;

    if (result["refresh_status"])
        CLIENT_SYNC_D->request_status(player, response_context);

    if (objectp(bankd) && result["refresh_balance"] && function_exists("emit_bank_balance_ui", bankd))
        bankd->emit_bank_balance_ui(player, response_context);
}

varargs int emit_bank_settlement_result(object player, object bankd,
    mapping result, mapping response_context)
{
    if (!objectp(player) || !mapp(result))
        return 0;

    sync_bank_command_state(player, bankd, result, response_context);
    msg_send(player, format_bank_command_message(result), response_context);
    return 1;
}
