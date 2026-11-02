/**
 * @file    economy_guild_settlement.c
 * @brief   帮派货币结算模块。
 *
 * guild 贡献/成员账本仍 guild-local；实际扣款/退款/俸禄货币操作归本模块：
 * - 捐献：settle 扣款 + refund 回滚（contribution 记账失败时）；
 * - 俸禄：settle 发放（发放日期守卫在 guild 账本，economy 层不做第二守卫）。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

/**
 * 捐献扣款。返回 ([ "ok": 1, "deducted": N ]) 或 ([ "ok": 0, "reason": ... ])。
 * 调用方（guild）contribution 记账失败时必须调 refund_guild_donation 回滚。
 */
mapping settle_guild_donation(object player, int amount, string operation_id)
{
    mapping receipt;

    if (!objectp(player) || amount <= 0)
        return ([ "ok": 0, "reason": "invalid amount" ]);
    if (!stringp(operation_id) || operation_id == "")
        return ([ "ok": 0, "reason": "operation_id required" ]);
    receipt = MONEY_D->apply_money_operation(player, ([
        "operation_id": operation_id,
        "direction": "deduct",
        "amount": amount,
        "domain": "guild",
        "action": "donation",
    ]));
    if (!mapp(receipt) || !receipt["ok"])
        return ([ "ok": 0, "reason": receipt && receipt["last_error"]
            ? receipt["last_error"] : "donation failed",
            "state": receipt && receipt["state"] ? receipt["state"] : "deduct_failed" ]);
    return ([ "ok": 1, "deducted": amount,
        "duplicate": receipt["duplicate"] ? 1 : 0,
        "state": receipt["state"] ]);
}

/** 捐献回滚（contribution 记账失败时退款）。
 * 经 MONEY_D->apply_money_operation 统一提交（无 prepared 窗口，
 * 写 committed receipt 并同步 player save）；同 id 重放返回 duplicate
 * （不再加钱）；调用方只有 committed/duplicate 才可删除挂账。
 * 缺参 fail-closed（不静默降级）。 */
mapping refund_guild_donation(object player, int amount, string operation_id)
{
    mapping receipt;

    if (!objectp(player) || amount <= 0)
        return ([ "ok": 0, "reason": "invalid amount" ]);
    if (!stringp(operation_id) || operation_id == "")
        return ([ "ok": 0, "reason": "operation_id required" ]);
    receipt = MONEY_D->apply_money_operation(player, ([
        "operation_id": operation_id,
        "direction": "add",
        "amount": amount,
        "domain": "guild",
        "action": "refund",
    ]));
    if (!mapp(receipt) || !receipt["ok"])
        return ([ "ok": 0, "reason": receipt && receipt["last_error"]
            ? receipt["last_error"] : "refund failed",
            "state": receipt && receipt["state"] ? receipt["state"] : "add_failed" ]);
    return ([ "ok": 1, "refunded": amount,
        "duplicate": receipt["duplicate"] ? 1 : 0,
        "state": receipt["state"] ]);
}

/**
 * 俸禄发放。返回 ([ "ok": 1, "salary": N ]) 或 ([ "ok": 0, "reason": ... ])。
 * 每日领取守卫在 guild 账本（member["salary_last"]）。
 */
/* 俸禄幂等发放——operation_id 必填（guild/player/claim seq 确定 id），
 * 经 apply_money_operation 统一提交；崩溃窗口重放返回 duplicate
 * 不重复加款。缺参 fail-closed。 */
mapping settle_guild_salary(object player, int amount, string operation_id)
{
    mapping receipt;

    if (!objectp(player) || amount <= 0)
        return ([ "ok": 0, "reason": "invalid amount" ]);
    if (!stringp(operation_id) || operation_id == "")
        return ([ "ok": 0, "reason": "operation_id required" ]);
    receipt = MONEY_D->apply_money_operation(player, ([
        "operation_id": operation_id,
        "direction": "add",
        "amount": amount,
        "domain": "guild",
        "action": "salary",
    ]));
    if (!mapp(receipt) || !receipt["ok"])
        return ([ "ok": 0, "reason": receipt && receipt["last_error"]
            ? receipt["last_error"] : "salary failed",
            "state": receipt && receipt["state"] ? receipt["state"] : "add_failed" ]);
    return ([ "ok": 1, "salary": amount,
        "duplicate": receipt["duplicate"] ? 1 : 0 ]);
}
