#pragma strict_types

#include <system/services.h>

inherit "/lib/framework/command";
inherit "/lib/std/command_utils";

int main(object me, string arg);
int help(object me);

private string trade_result_message(mapping result, string fallback)
{
    if (mapp(result) && stringp(result["message"]) && result["message"] != "")
        return result["message"];
    return fallback;
}

private int send_trade_result_feedback(object me, mapping result, string fallback,
    string action, mapping response_context)
{
    string message;
    message = trade_result_message(result, fallback);
    if (mapp(result) && result["success"])
        return msg_system(me, message, "trade_feedback", response_context);
    return msg_error(me, message, "trade",
        stringp(action) ? action : "trade_failed", "", 0,
        response_context);
}

private int send_trade_status(object me, mapping response_context)
{
    return msg_dialog(me, "交易", TRADE_D->render_trade_session(me),
        "trade/status", "trade_status", response_context);
}

private int do_request(object me, string rest, mapping response_context)
{
    mapping result;
    object target;

    if (!stringp(rest) || rest == "") {
        msg_error(me, "用法：trade request <玩家>\n", "trade", "missing_target", "request", 0,
            response_context);
        return 1;
    }

    target = find_player_in_room(me, rest);
    if (!objectp(target))
        target = call_other("/lib/std/utils/util_player",
            "find_living_in_room", environment(me), rest, me);
    if (!objectp(target)
        || (!userp(target) && !target->query("trade_proxy"))) {
        msg_error(me, "这里没有这个玩家或测试交易替身。\n", "trade",
            "target_not_found", rest, 0, response_context);
        return 1;
    }

    result = TRADE_D->request_trade(me, target);
    if (!mapp(result) || !result["success"]) {
        send_trade_result_feedback(me, result, "交易请求失败。\n", "request_failed",
            response_context);
        return 1;
    }

    return 1;
}

private int do_accept(object me, mapping response_context)
{
    mapping result;

    result = TRADE_D->accept_trade(me);
    if (!mapp(result) || !result["success"])
        send_trade_result_feedback(me, result, "接受交易失败。\n", "accept_failed",
            response_context);
    return 1;
}

private int do_reject(object me, mapping response_context)
{
    mapping result;

    result = TRADE_D->reject_trade(me);
    if (!mapp(result) || !result["success"])
        send_trade_result_feedback(me, result, "拒绝交易失败。\n", "reject_failed",
            response_context);
    return 1;
}

private int do_add(object me, string rest, mapping response_context)
{
    mapping result;

    if (!stringp(rest) || rest == "") {
        msg_error(me, "用法：trade add <物品>\n", "trade", "missing_item", "add", 0,
            response_context);
        return 1;
    }

    result = TRADE_D->add_item_offer(me, rest);
    send_trade_result_feedback(me, result, "报价失败。\n", "add_failed", response_context);
    return 1;
}

private int do_remove(object me, string rest, mapping response_context)
{
    mapping result;

    if (!stringp(rest) || rest == "") {
        msg_error(me, "用法：trade remove <物品>\n", "trade", "missing_item", "remove", 0,
            response_context);
        return 1;
    }

    result = TRADE_D->remove_item_offer(me, rest);
    send_trade_result_feedback(me, result, "移除失败。\n", "remove_failed", response_context);
    return 1;
}

private int do_money(object me, string rest, mapping response_context)
{
    int amount;
    mapping result;

    if (!stringp(rest) || sscanf(rest, "%d", amount) != 1 || amount < 0) {
        msg_error(me, "用法：trade money <金额>\n", "trade", "invalid_money", rest, 0,
            response_context);
        return 1;
    }

    result = TRADE_D->set_money_offer(me, amount);
    send_trade_result_feedback(me, result, "报价失败。\n", "money_failed", response_context);
    return 1;
}

private int do_unmoney(object me, mapping response_context)
{
    mapping result;

    result = TRADE_D->clear_money_offer(me);
    send_trade_result_feedback(me, result, "清空现金报价失败。\n", "unmoney_failed", response_context);
    return 1;
}

private int do_confirm(object me, mapping response_context)
{
    mapping result;

    result = TRADE_D->confirm_trade(me);
    if (mapp(result) && result["success"])
        return 1;

    send_trade_result_feedback(me, result, "确认交易失败。\n", "confirm_failed", response_context);
    return 1;
}

private int do_cancel(object me, mapping response_context)
{
    mapping result;

    result = TRADE_D->cancel_trade(me);
    if (!mapp(result) || !result["success"])
        send_trade_result_feedback(me, result, "取消交易失败。\n", "cancel_failed", response_context);
    return 1;
}

int main(object me, string arg)
{
    mapping parsed;
    mapping response_context;

    if (!objectp(me))
        return 0;

    response_context = CLIENT_SYNC_D->capture_command_response_context(me);
    if (!stringp(arg) || arg == "") {
        send_trade_status(me, response_context);
        return 1;
    }

    parsed = parse_subcommand_args(arg);
    if (!parsed["valid"])
        return help(me);

    switch (parsed["action"]) {
        case "request":
            return do_request(me, parsed["rest"], response_context);
        case "accept":
            return do_accept(me, response_context);
        case "reject":
            return do_reject(me, response_context);
        case "add":
            return do_add(me, parsed["rest"], response_context);
        case "remove":
            return do_remove(me, parsed["rest"], response_context);
        case "money":
            return do_money(me, parsed["rest"], response_context);
        case "unmoney":
            return do_unmoney(me, response_context);
        case "status":
            send_trade_status(me, response_context);
            return 1;
        case "confirm":
            return do_confirm(me, response_context);
        case "cancel":
            return do_cancel(me, response_context);
        default:
            return help(me);
    }
}

int help(object me)
{
    msg_send(me, @HELP
命令：trade（玩家交易）

用法：
  trade
  trade request <玩家>
  trade accept
  trade reject
  trade add <物品>
  trade remove <物品>
  trade money <金额>
  trade unmoney
  trade status
  trade confirm
  trade cancel

说明：
  - 交易双方必须在同一地点
  - 任一方修改报价后，双方确认状态都会重置
  - 交易税费按当前 economy 配置结算
HELP
    );
    return 1;
}
