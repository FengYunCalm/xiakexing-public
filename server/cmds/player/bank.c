/**
 * @file    bank.c
 * @brief   银行命令 - 银行业务文本门面



 *
 * @par Description:
 * 银行业务文本门面，负责参数分发、场景校验、结果反馈与 XK 银行界面入口。
 * 存款、取款、转账与余额真逻辑统一归 `BANK_D` 持有。
 */

#pragma strict_types

#include <system/ansi.h>
#include <game/command.h>
#include <system/daemon.h>

inherit "/lib/framework/network";
inherit "/lib/std/command_utils";

/* 银行守护进程真源 */
#define BANK_D    "/daemons/services/economy/bank_d"

int help(object me);
int main(object me, string arg);

private nosave object _bank_service;

private object find_banker(object me);
private object load_bank_service();
private object require_banker(object me, mapping response_context);
private mapping build_bank_action(string label, string cmd, string kind, string role);
private mapping build_bank_action_group();
private int emit_bank_dialog_ui(object me, object banker, object bankd,
    mapping response_context);
private int emit_bank_result_dialog_ui(object me, object banker, object bankd,
    mapping result, mapping response_context);
private int emit_command_result(object me, object bankd, mapping result,
    mapping response_context);
private int emit_bank_overview_ui(object me, object banker, object bankd,
    mapping response_context);
private int handle_bank_action(object me, string action, string params,
    mapping response_context);
private void emit_bank_menu_ui(object me, object banker, object bankd,
    mapping response_context);
private void emit_bank_balance_ui(object me, object bankd, mapping response_context);

private object check_bank_available(object me, mapping response_context);

private int do_deposit(object me, string amount_str, mapping response_context);
private int do_withdraw(object me, string amount_str, mapping response_context);
private int do_check(object me, mapping response_context);
private int do_transfer(object me, string args, mapping response_context);
private int do_interest(object me, mapping response_context);

private int emit_command_result(object me, object bankd, mapping result,
    mapping response_context)
{
    object banker;

    if (is_xk_client(me)) {
        banker = find_banker(me);
        if (objectp(banker))
            return emit_bank_result_dialog_ui(me, banker, bankd, result,
                response_context);
    }

    return ECONOMY_FEEDBACK_D->emit_bank_settlement_result(me, bankd, result,
        response_context);
}

private string format_bank_result_message(object bankd, mapping result)
{
    string action;
    string target_id;
    int amount;
    int fee;
    int net;
    int balance;

    if (!mapp(result))
        return "银行操作失败。";
    if (!(result["success"] || 0))
        return stringp(result["message"]) && result["message"] != "" ? result["message"] : "银行操作失败。";

    action = result["action"] || "";
    amount = intp(result["amount"]) ? result["amount"] : 0;
    fee = intp(result["fee"]) ? result["fee"] : 0;
    net = intp(result["net"]) ? result["net"] : amount;
    balance = intp(result["balance"]) ? result["balance"] : 0;
    target_id = result["target_id"] || "";

    switch (action) {
        case "deposit":
            return sprintf("存款成功！已存入%s，手续费%s，入账%s。当前存款：%s。",
                bankd->format_money(amount), bankd->format_money(fee), bankd->format_money(net), bankd->format_money(balance));
        case "withdraw":
            return sprintf("取款成功！已取出%s。当前存款：%s。",
                bankd->format_money(amount), bankd->format_money(balance));
        case "transfer":
            return sprintf("转账成功！已向%s转账%s，手续费%s。当前存款：%s。",
                target_id != "" ? target_id : "对方", bankd->format_money(amount), bankd->format_money(fee), bankd->format_money(balance));
        default:
            return stringp(result["message"]) && result["message"] != "" ? result["message"] : "银行操作成功。";
    }
}

private void emit_bank_menu_ui(object me, object banker, object bankd,
    mapping response_context)
{
    if (!objectp(me) || !objectp(banker) || !objectp(bankd))
        return;

    if (function_exists("emit_bank_menu_ui", bankd))
        bankd->emit_bank_menu_ui(me, banker, response_context);
}

private void emit_bank_balance_ui(object me, object bankd,
    mapping response_context)
{
    if (!objectp(me) || !objectp(bankd))
        return;

    if (function_exists("emit_bank_balance_ui", bankd))
        bankd->emit_bank_balance_ui(me, response_context);
}

private mapping build_bank_action(string label, string cmd, string kind, string role)
{
    return ([
        "label": label,
        "cmd": cmd,
        "kind": kind,
        "surface": "root",
        "role": role,
        "target_group": "root",
    ]);
}

private mapping build_bank_action_group()
{
    return ([
        "id": "root",
        "title": "银行业务",
        "surface": "root",
        "actions": ({
            build_bank_action("存钱", "ui://bank/deposit", "bank_deposit", "primary"),
            build_bank_action("取钱", "ui://bank/withdraw", "bank_withdraw", "primary"),
            build_bank_action("查账", "bank check", "bank_check", "secondary"),
            build_bank_action("领息", "bank interest", "bank_interest", "secondary"),
        }),
    ]);
}

private int emit_bank_dialog_ui(object me, object banker, object bankd,
    mapping response_context)
{
    int balance;
    int pocket;
    string banker_name;

    if (!objectp(me) || !objectp(banker) || !objectp(bankd))
        return 0;

    balance = bankd->query_balance(me);
    pocket = MONEY_D->query_balance(me);
    banker_name = get_object_display_name(banker, "钱庄伙计");

    xk_send_dialog(me, "银行",
        sprintf("%s把账册递给你。\n存款：%s\n现金：%s\n",
            banker_name,
            bankd->format_money(balance),
            MONEY_D->format_money(pocket)),
        ([
            "source": "bank",
            "dialog_id": "bank",
            "replace_mode": "replace",
            "action_groups": ({ build_bank_action_group() }),
        ]), response_context);
    return 1;
}

private int emit_bank_result_dialog_ui(object me, object banker, object bankd,
    mapping result, mapping response_context)
{
    int balance;
    int pocket;
    string result_line;

    if (!objectp(me) || !objectp(banker) || !objectp(bankd))
        return 0;

    if (mapp(result) && (result["success"] || 0))
        CLIENT_SYNC_D->request_status_inventory(me, response_context);

    balance = bankd->query_balance(me);
    pocket = MONEY_D->query_balance(me);
    result_line = format_bank_result_message(bankd, result);

    xk_send_dialog(me, "银行",
        sprintf("%s\n\n存款：%s\n现金：%s\n",
            result_line,
            bankd->format_money(balance),
            MONEY_D->format_money(pocket)),
        ([
            "source": "bank",
            "dialog_id": "bank",
            "replace_mode": "replace",
            "action_groups": ({ build_bank_action_group() }),
        ]), response_context);
    return 1;
}


/**
 * @brief 查找银行家
 */
private object find_banker(object me)
{
    object room, ob;
    object *inv;

    if (!me) return 0;

    room = environment(me);
    if (!room) return 0;

    inv = all_inventory(room);

    foreach (ob in inv)
    {
        if (ob->query("npc_type") == "banker" ||
            ob->query("banker") ||
            function_exists("is_banker", ob))
        {
            return ob;
        }
    }

    if (room->query("is_bank"))
        return room;

    return 0;
}

/**
 * @brief 获取银行守护进程
 */
private object load_bank_service()
{
    object bankd;

    if (objectp(_bank_service))
        return _bank_service;

    bankd = find_object(BANK_D);
    if (!objectp(bankd))
        bankd = load_object(BANK_D);

    if (objectp(bankd))
        _bank_service = bankd;

    return bankd;
}

private object require_banker(object me, mapping response_context)
{
    object banker;

    banker = find_banker(me);
    if (!banker) {
        msg_send(me, "这里不是钱庄，没有银行服务。\n", response_context);
        return 0;
    }

    return banker;
}

/**
 * @brief 检查银行系统是否可用
 * @return 银行守护进程对象，不可用返回0并提示错误
 */
private object check_bank_available(object me, mapping response_context)
{
    object bankd;

    bankd = load_bank_service();

    if (!bankd) {
        msg_send(me, "银行系统暂时不可用。\n", response_context);
        return 0;
    }

    return bankd;
}

/**
 * @brief 显示银行菜单
 */
private int emit_bank_overview_ui(object me, object banker, object bankd,
    mapping response_context)
{
    int balance = bankd->query_balance(me);
    int pocket = MONEY_D->query_balance(me);
    string banker_name;

    banker_name = get_object_display_name(banker, "钱庄伙计");

    if (is_xk_client(me))
        return emit_bank_dialog_ui(me, banker, bankd, response_context);

    msg_send(me,
        sprintf("%s把账册递给你。\n存款：%s\n现金：%s\n可用操作：bank cun <金额>、bank qu <金额>、bank check、bank transfer <玩家> <金额>、bank interest\n",
            banker_name,
            bankd->format_money(balance),
            MONEY_D->format_money(pocket)), response_context);

    return 1;
}

/**
 * @brief 处理银行操作
 */
private int handle_bank_action(object me, string action, string params,
    mapping response_context)
{
    switch (action)
    {
        case "cun":
        case "deposit":
            return do_deposit(me, params, response_context);

        case "qu":
        case "withdraw":
            return do_withdraw(me, params, response_context);

        case "check":
        case "query":
            return do_check(me, response_context);

        case "transfer":
            return do_transfer(me, params, response_context);

        case "interest":
            return do_interest(me, response_context);

        default:
            msg_send(me, "用法：bank <cun|qu|check|transfer|interest> [参数]\n",
                response_context);
            return 1;
    }
}

int main(object me, string arg)
{
    object banker, bankd;
    string action, params;
    mapping response_context;

    // Guard Clauses
    if (!me) return 0;

    response_context = CLIENT_SYNC_D->capture_command_response_context(me);

    // 获取银行守护进程
    bankd = check_bank_available(me, response_context);
    if (!bankd)
        return 1;

    if (!arg || arg == "") {
        banker = require_banker(me, response_context);
        if (!banker)
            return 1;
        return emit_bank_overview_ui(me, banker, bankd, response_context);
    }

    // 解析参数
    {
        mapping parsed = parse_subcommand_args(arg);
        if (!parsed["valid"]) {
            msg_send(me, parsed["error"], response_context);
            return 1;
        }

        action = parsed["action"];
        params = parsed["rest"];
    }

    banker = require_banker(me, response_context);
    if (!banker)
        return 1;

    // 处理各子命令
    return handle_bank_action(me, action, params, response_context);
}

/**
 * @brief 存款
 */
int do_deposit(object me, string amount_str, mapping response_context)
{
    object bankd = check_bank_available(me, response_context);
    mapping result;

    if (!me) return 0;
    if (!bankd) return 1;

    result = bankd->command_deposit(me, amount_str);
    return emit_command_result(me, bankd, result, response_context);
}

/**
 * @brief 取款
 */
int do_withdraw(object me, string amount_str, mapping response_context)
{
    object bankd = check_bank_available(me, response_context);
    mapping result;

    if (!me) return 0;
    if (!bankd) return 1;

    result = bankd->command_withdraw(me, amount_str);
    return emit_command_result(me, bankd, result, response_context);
}

/**
 * @brief 查询余额
 */
int do_check(object me, mapping response_context)
{
    object bankd = check_bank_available(me, response_context);
    object banker;
    int balance, pocket;

    if (!me) return 0;
    if (!bankd) return 1;

    banker = find_banker(me);
    if (is_xk_client(me) && objectp(banker))
        return emit_bank_dialog_ui(me, banker, bankd, response_context);

    balance = bankd->query_balance(me);
    pocket = MONEY_D->query_balance(me);

    // 发送XK协议
    emit_bank_balance_ui(me, bankd, response_context);

    msg_send(me, sprintf("存款：%s\n现金：%s\n总资产：%s\n",
        bankd->format_money(balance),
        MONEY_D->format_money(pocket),
        bankd->format_money(balance + pocket)), response_context);
    return 1;
}

/**
 * @brief 转账
 */
int do_transfer(object me, string args, mapping response_context)
{
    object bankd = check_bank_available(me, response_context);
    mapping result;

    if (!me) return 0;
    if (!bankd) return 1;

    result = bankd->command_transfer(me, args);
    return emit_command_result(me, bankd, result, response_context);
}

int do_interest(object me, mapping response_context)
{
    object bankd = check_bank_available(me, response_context);
    mapping result;

    if (!me) return 0;
    if (!bankd) return 1;

    result = bankd->command_interest(me);
    return emit_command_result(me, bankd, result, response_context);
}

int help(object me)
{
    msg_send(me,
        "命令：bank\n"
        "用法：bank、bank cun <金额>、bank qu <金额>、bank check、bank transfer <玩家> <金额>、bank interest\n"
        "金额格式：1000、10 silver、1 gold、all\n"
        "说明：只能在钱庄或银行伙计面前使用，利息和手续费按当前规则结算。\n");
    return 1;
}
