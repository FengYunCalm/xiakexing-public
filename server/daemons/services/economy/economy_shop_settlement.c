/**
 * @file    economy_shop_settlement.c
 * @brief   商店价格与买卖结算模块。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

float normalize_rate(mixed raw_rate, float fallback);
int apply_price_rate(int base_price, float rate);
mapping query_economy_shop_config();
mapping build_result(int success, string message, mapping extra);
mapping build_money_operation_spec_with_id(string domain, string action,
    string actor_id, string subject_id, string operation_id);
string issue_money_operation_id(string domain, string action,
    string actor_id, string subject_id);
int queue_compensation_with_operation(object who, string domain, string action,
    string actor_id, string subject_id, string operation_id, int amount);
mapping settle_shop_buy_with_operation(object player, string goods_id,
    string display_name, int price, string operation_id);
mapping settle_shop_sell_with_operation(object player, object item,
    string display_name, int price, string operation_id);

private mapping save_shop_player_phase(object player)
{
    mapping phase;
    mixed err;

    if (!objectp(player) || !function_exists("save_phases", player))
        return ([ "outcome": "not_written", "persisted": 0,
            "save_result": 0, "error": "player_save_phases_unavailable" ]);
    err = catch(phase = player->save_phases());
    if (err || !mapp(phase))
        return ([ "outcome": "unknown", "persisted": -1,
            "save_result": 0,
            "error": err ? sprintf("%O", err) : "invalid_save_phase" ]);
    return phase;
}

private int item_has_shop_operation(object item, string operation_id,
    string field)
{
    mapping receipts;

    if (!objectp(item) || !stringp(operation_id) || operation_id == "")
        return 0;
    receipts = item->query(field);
    return mapp(receipts) && !undefinedp(receipts[operation_id]);
}

private void mark_item_shop_operation(object item, string operation_id,
    string field)
{
    mapping receipts;

    if (!objectp(item) || !stringp(operation_id) || operation_id == "")
        return;
    receipts = item->query(field);
    receipts = mapp(receipts) ? copy(receipts) : ([]);
    receipts[operation_id] = time();
    item->set(field, receipts);
}

private void clear_item_shop_operation(object item, string operation_id,
    string field)
{
    mapping receipts;

    if (!objectp(item) || !stringp(operation_id) || operation_id == "")
        return;
    receipts = item->query(field);
    if (!mapp(receipts) || undefinedp(receipts[operation_id]))
        return;
    receipts = copy(receipts);
    map_delete(receipts, operation_id);
    item->set(field, receipts);
}

private int restore_shop_sell_item(object player, mapping attachment,
    string operation_id)
{
    object restored;
    int count;

    if (!objectp(player) || !mapp(attachment)
        || !stringp(attachment["template_id"])
        || attachment["template_id"] == "")
        return 0;
    count = intp(attachment["count"]) && attachment["count"] > 0
        ? attachment["count"] : 1;
    restored = ITEM_D->create_item(attachment["template_id"], count);
    if (!objectp(restored) || !restored->move(player)) {
        if (objectp(restored))
            destruct(restored);
        return 0;
    }
    ITEM_D->bind_item_owner(restored, player, "shop_sell_restore");
    mark_item_shop_operation(restored, operation_id,
        "economy/shop_sell_operations");
    return 1;
}

private mapping query_shop_sell_save_receipt(object player)
{
    mixed err;
    mapping phase;

    if (!objectp(player) || !function_exists("recover_save_receipt", player))
        return ([ "outcome": "committed", "persisted": 1, "save_result": 1 ]);
    err = catch(phase = player->recover_save_receipt());
    if (err || !mapp(phase))
        return ([ "outcome": "unknown", "persisted": -1, "save_result": 0,
            "error": err ? sprintf("%O", err) : "receipt recovery unavailable" ]);
    return phase;
}

private object find_shop_operation_item_in(object container,
    string operation_id, string field)
{
    object item;
    object nested;

    if (!objectp(container) || !stringp(operation_id) || operation_id == "")
        return 0;
    foreach (item in all_inventory(container)) {
        if (item_has_shop_operation(item, operation_id, field))
            return item;
        nested = find_shop_operation_item_in(item, operation_id, field);
        if (objectp(nested))
            return nested;
    }
    return 0;
}

private object find_shop_operation_item(object player, string operation_id,
    string field)
{
    if (!objectp(player) || !stringp(operation_id) || operation_id == "")
        return 0;
    return find_shop_operation_item_in(player, operation_id, field);
}

float query_shop_buy_rate()
{
    mapping shop_config;

    shop_config = query_economy_shop_config();
    return normalize_rate(shop_config["buy_rate"], 0.5);
}

float query_shop_buy_rate_for_shop(string shop_id)
{
    mapping shop_config;
    mixed rate;

    if (stringp(shop_id) && shop_id != "") {
        shop_config = ITEM_D->query_shop(shop_id);
        if (mapp(shop_config)) {
            rate = shop_config["buy_rate"];
            if (intp(rate) || floatp(rate))
                return normalize_rate(rate, query_shop_buy_rate());
        }
    }

    return query_shop_buy_rate();
}

float query_shop_sell_rate()
{
    mapping shop_config;

    shop_config = query_economy_shop_config();
    return normalize_rate(shop_config["sell_rate"], 1.0);
}

float query_shop_sell_rate_for_shop(string shop_id)
{
    mapping shop_config;
    mixed rate;

    if (stringp(shop_id) && shop_id != "") {
        shop_config = ITEM_D->query_shop(shop_id);
        if (mapp(shop_config)) {
            rate = shop_config["sell_rate"];
            if (intp(rate) || floatp(rate))
                return normalize_rate(rate, query_shop_sell_rate());
        }
    }

    return query_shop_sell_rate();
}

float query_shop_price_fluctuation()
{
    mapping shop_config;
    mixed raw;

    shop_config = query_economy_shop_config();
    raw = shop_config["price_fluctuation"];
    if ((intp(raw) || floatp(raw)) && to_float(raw) >= 0.0)
        return to_float(raw);

    return 0.0;
}

int query_shop_buy_price(string shop_id, string goods_id, int base_price)
{
    if ((!intp(base_price) || base_price <= 0) && stringp(goods_id) && goods_id != "")
        base_price = ITEM_D->get_item_value(goods_id);

    return apply_price_rate(base_price, query_shop_sell_rate_for_shop(shop_id));
}

/* 善恶玩法化：商店报价叠加道德调整（调用层统一入口） */
int apply_moral_price_adjustment(object player, int price)
{
    int moral;

    if (!objectp(player) || price <= 0)
        return price;
    moral = intp(player->query("moral")) ? player->query("moral") : 0;
    if (moral > 50)
        return price * 95 / 100;
    if (moral < -50)
        return price * 105 / 100;
    return price;
}

/* NPC 好感只读玩家 × NPC 的结构化快照；阈值/比例仍是待设计配置，
 * 本接口暂保留既有报价兼容值，不再读取 NPC 全局字段。 */
int apply_favor_price_adjustment(object player, object vendor, int price)
{
    if (!objectp(player) || !objectp(vendor) || price <= 0)
        return price;
    /* S20：好感折扣阈值与比例尚未完成产品设计；框架只读取 owner
     * 快照并保留调整项，不凭旧默认值产生折扣。 */
    REPUTATION_D->query_npc_favor(player, vendor);
    return price;
}

/* 称谓折扣（六扇门世界响应）：总捕头/掌刑使（阶位 ≥ 5）在店铺 95 折。
 * S20/L-190：与善恶、好感按调用顺序叠加；具体比例后续由产品数值
 * 设计统一收口。 */
int apply_rank_price_adjustment(object player, int price)
{
    int rank;

    if (!objectp(player) || price <= 0)
        return price;
    if ((player->query("family/id") || "") != "liushanmen")
        return price;
    rank = intp(player->query("family/rank")) ? player->query("family/rank") : 0;
    if (rank >= 5)
        return price * 95 / 100;
    return price;
}

int query_shop_sell_price(string shop_id, object item, int base_price)
{
    string template_id;

    if ((!intp(base_price) || base_price <= 0) && objectp(item))
        base_price = item->query("value") || 0;
    if ((!intp(base_price) || base_price <= 0) && objectp(item)) {
        template_id = item->query("template_id") || "";
        if (template_id != "")
            base_price = ITEM_D->get_item_value(template_id);
    }

    return apply_price_rate(base_price, query_shop_buy_rate_for_shop(shop_id));
}

mapping query_shop_buy_price_breakdown(string shop_id, object player,
    object vendor, string goods_id, int base_price)
{
    mapping *adjustments;
    int current;
    int next;

    if ((!intp(base_price) || base_price <= 0)
        && stringp(goods_id) && goods_id != "")
        base_price = ITEM_D->get_item_value(goods_id);
    current = query_shop_buy_price(shop_id, goods_id, base_price);
    adjustments = ({ });

    next = apply_moral_price_adjustment(player, current);
    adjustments += ({ ([ "source": "moral", "before": current,
        "after": next, "delta": next - current ]) });
    current = next;

    next = apply_favor_price_adjustment(player, vendor, current);
    adjustments += ({ ([ "source": "favor", "before": current,
        "after": next, "delta": next - current ]) });
    current = next;

    next = apply_rank_price_adjustment(player, current);
    adjustments += ({ ([ "source": "rank", "before": current,
        "after": next, "delta": next - current ]) });
    current = next;

    return ([
        "ok": 1,
        "base_price": base_price,
        "shop_price": query_shop_buy_price(shop_id, goods_id, base_price),
        "adjustments": adjustments,
        "final_price": current,
        "stacking": "multiplicative_in_declared_order",
        "rounding": "floor_after_each_adjustment",
    ]);
}

mapping settle_shop_buy(object player, string goods_id, string display_name, int price)
{
    return settle_shop_buy_with_operation(player, goods_id, display_name, price,
        0);
}

mapping settle_shop_buy_with_operation(object player, string goods_id,
    string display_name, int price, string operation_id)
{
    object item;
    mapping placement;
    mapping result;
    mapping money_result;
    mapping money_spec;
    mapping receipt;
    mapping refund_receipt;
    mapping receipt_phase;
    mapping save_phase;
    string player_id;
    object delivered;
    int compensation_queued;
    int balance;

    if (!objectp(player) || !stringp(goods_id) || goods_id == "" || price <= 0)
        return build_result(0, "购买失败。\n", ([
            "action": "buy",
            "item": stringp(display_name) && display_name != "" ? display_name : goods_id,
            "price": price,
            "price_label": MONEY_D->format_money(price),
            "balance": MONEY_D->query_balance(player),
            "balance_label": MONEY_D->format_money(MONEY_D->query_balance(player)),
        ]));

    display_name = stringp(display_name) && display_name != "" ? display_name : goods_id;
    player_id = PLAYER_STATE_D->query_player_id(player);
    if (!stringp(operation_id) || operation_id == "")
        operation_id = issue_money_operation_id("shop", "buy", player_id,
            goods_id);
    /* 资金 receipt 已存在时，不能把一次已扣款的购买当成新购买。
     * 只有带同一 operation 标记的已交付物品才能安全返回 duplicate；
     * 无物品证据时 fail-closed，避免“扣一次钱、发两件物品”或免费补发。 */
    {
        receipt = MONEY_D->query_money_operation_receipt(player, operation_id);
        if (mapp(receipt)) {
            if (receipt["amount"] != price || receipt["direction"] != "deduct"
                || receipt["domain"] != "shop"
                || receipt["action"] != "buy")
                return build_result(0, "购买操作参数冲突，请使用新的请求编号。\n", ([
                    "action": "buy",
                    "item": display_name,
                    "price": price,
                    "operation_id": operation_id,
                    "operation_conflict": 1,
                ]));
            /* 现金收据存在不等于本次购买的物品快照已经完成。
             * 先补原玩家 JSON 收据；待收据或未知状态不得把内存中的
             * 交付物品当成持久凭证返回 duplicate。not_written 只允许
             * 在确认物品凭证后走一次原 operation 的退款。 */
            receipt_phase = query_shop_sell_save_receipt(player);
            if (!mapp(receipt_phase)
                || receipt_phase["outcome"] != "committed") {
                if (mapp(receipt_phase)
                    && receipt_phase["outcome"] == "not_written") {
                    refund_receipt = MONEY_D->query_money_operation_receipt(
                        player, operation_id + "/refund");
                    delivered = find_shop_operation_item(player, operation_id,
                        "economy/shop_buy_operations");
                    if (!mapp(refund_receipt) && objectp(delivered)) {
                        destruct(delivered);
                        money_result = MONEY_D->apply_money_operation(player, ([
                            "operation_id": operation_id + "/refund",
                            "direction": "add",
                            "amount": price,
                            "domain": "shop",
                            "action": "buy_refund",
                        ]));
                        if (mapp(money_result) && money_result["ok"] == 1)
                            return build_result(0,
                                "购买未能保存，款项已退回，请重新购买。\n", ([
                                "action": "buy",
                                "item": display_name,
                                "price": price,
                                "price_label": MONEY_D->format_money(price),
                                "balance": MONEY_D->query_balance(player),
                                "balance_label": MONEY_D->format_money(
                                    MONEY_D->query_balance(player)),
                                "operation_id": operation_id,
                                "recovery_required": 1,
                            ]));
                        if (!mapp(money_result)
                            || !money_result["pending_recovery"])
                            compensation_queued = queue_compensation_with_operation(
                                player, "shop", "buy_refund", player_id,
                                goods_id, operation_id + "/refund", price);
                    }
                }
                return build_result(0,
                    "购买结果正在恢复，请使用相同请求编号重试。\n", ([
                    "action": "buy",
                    "item": display_name,
                    "price": price,
                    "price_label": MONEY_D->format_money(price),
                    "balance": MONEY_D->query_balance(player),
                    "balance_label": MONEY_D->format_money(
                        MONEY_D->query_balance(player)),
                    "operation_id": operation_id,
                    "pending_recovery": 1,
                    "compensation_queued": compensation_queued,
                ]));
            }
            delivered = find_shop_operation_item(player, operation_id,
                "economy/shop_buy_operations");
            if (objectp(delivered)) {
                result = build_result(1, "", ([
                    "action": "buy",
                    "item": display_name,
                    "price": price,
                    "price_label": MONEY_D->format_money(price),
                    "balance": MONEY_D->query_balance(player),
                    "balance_label": MONEY_D->format_money(
                        MONEY_D->query_balance(player)),
                    "refresh_status": 1,
                    "refresh_inventory": 1,
                    "duplicate": 1,
                ]));
                result += CLIENT_PROJECTION_D->build_item_client_payload_from_template_id(
                    goods_id, delivered);
                return result;
            }
            refund_receipt = MONEY_D->query_money_operation_receipt(player,
                operation_id + "/refund");
            if (mapp(refund_receipt))
                return build_result(0,
                    "这笔购买已退款，请使用新的购买请求重试。\n", ([
                        "action": "buy",
                        "item": display_name,
                        "price": price,
                        "price_label": MONEY_D->format_money(price),
                        "balance": MONEY_D->query_balance(player),
                        "balance_label": MONEY_D->format_money(
                            MONEY_D->query_balance(player)),
                        "operation_id": operation_id,
                        "recovery_required": 1,
                    ]));
            return build_result(0,
                "这笔购买已扣款但交付凭证缺失，暂不能重复发放。\n", ([
                    "action": "buy",
                    "item": display_name,
                    "price": price,
                    "price_label": MONEY_D->format_money(price),
                    "balance": MONEY_D->query_balance(player),
                    "balance_label": MONEY_D->format_money(
                        MONEY_D->query_balance(player)),
                    "operation_id": operation_id,
                    "pending_recovery": 1,
                ]));
        }
    }
    money_spec = build_money_operation_spec_with_id("shop", "buy",
        player_id, goods_id, operation_id);
    money_spec["direction"] = "deduct";
    money_spec["amount"] = price;
    money_result = MONEY_D->apply_money_operation(player, money_spec);
    if (!mapp(money_result) || money_result["ok"] != 1)
        return build_result(0,
            mapp(money_result) && money_result["pending_recovery"]
                ? "购买操作正在恢复，请使用相同请求编号重试。\n"
                : mapp(money_result) && money_result["state"] == "deduct_failed"
                    ? "你的铜钱不足！\n" : "购买失败，请稍后重试。\n", ([
            "action": "buy",
            "item": display_name,
            "price": price,
            "price_label": MONEY_D->format_money(price),
            "balance": MONEY_D->query_balance(player),
            "balance_label": MONEY_D->format_money(MONEY_D->query_balance(player)),
            "operation_id": operation_id,
            "pending_recovery": mapp(money_result)
                && money_result["pending_recovery"] ? 1 : 0,
        ]));

    item = ITEM_D->create_item(goods_id, 1);
    if (!objectp(item)) {
        /* 补偿失败进可恢复队列（不忽略返回值）。 */
        money_result = MONEY_D->apply_money_operation(player, ([
            "operation_id": operation_id + "/refund",
            "direction": "add",
            "amount": price,
            "domain": "shop",
            "action": "buy_refund",
        ]));
        if (!mapp(money_result) || money_result["ok"] != 1)
            compensation_queued = queue_compensation_with_operation(player,
                "shop", "buy_refund", player_id, goods_id,
                operation_id + "/refund", price);
        result = build_result(0, "购买失败，无法将物品放入你的背包。\n", ([
            "action": "buy",
            "item": display_name,
            "price": price,
            "price_label": MONEY_D->format_money(price),
            "balance": MONEY_D->query_balance(player),
            "balance_label": MONEY_D->format_money(MONEY_D->query_balance(player)),
            "operation_id": operation_id,
            "pending_recovery": compensation_queued
                || (mapp(money_result) && money_result["pending_recovery"])
                ? 1 : 0,
            "compensation_queued": compensation_queued,
        ]));
        result += CLIENT_PROJECTION_D->build_item_client_payload_from_template_id(goods_id, item);
        return result;
    }

    mark_item_shop_operation(item, operation_id,
        "economy/shop_buy_operations");
    placement = ITEM_INVENTORY_D->add_item_to_inventory(player, item);
    if (!mapp(placement) || !placement["ok"]) {
        if (objectp(item) && !environment(item))
            destruct(item);
        /* 补偿失败进可恢复队列（不忽略返回值）。 */
        money_result = MONEY_D->apply_money_operation(player, ([
            "operation_id": operation_id + "/refund",
            "direction": "add",
            "amount": price,
            "domain": "shop",
            "action": "buy_refund",
        ]));
        if (!mapp(money_result) || money_result["ok"] != 1)
            compensation_queued = queue_compensation_with_operation(player,
                "shop", "buy_refund", player_id, goods_id,
                operation_id + "/refund", price);
        result = build_result(0, "购买失败，无法将物品放入你的背包。\n", ([
            "action": "buy",
            "item": display_name,
            "price": price,
            "price_label": MONEY_D->format_money(price),
            "balance": MONEY_D->query_balance(player),
            "balance_label": MONEY_D->format_money(MONEY_D->query_balance(player)),
            "operation_id": operation_id,
            "pending_recovery": compensation_queued
                || (mapp(money_result) && money_result["pending_recovery"])
                ? 1 : 0,
            "compensation_queued": compensation_queued,
        ]));
        result += CLIENT_PROJECTION_D->build_item_client_payload_from_template_id(goods_id, item);
        return result;
    }
    item = placement["item"];
    /* 堆叠合并会销毁刚创建的临时对象；把同一 operation 标记
     * 写到最终库存对象，重试时用物品凭证证明已交付。 */
    mark_item_shop_operation(item, operation_id,
        "economy/shop_buy_operations");

    /* 购买获得物品纳入 collect 事件（与拾取/采集/钓鱼同模式）：
     * 任务 COLLECT 步（如 bj_baiyun_herb「买一味半夏」）可经购买推进；
     * 无匹配任务时 record_item_acquired 为 no-op。 */
    QUEST_D->record_item_acquired(player, goods_id, 1);

    /* The successful debit is synchronous; placement cannot change currency. */
    balance = MONEY_D->query_balance(player);
    result = build_result(1, "", ([
        "action": "buy",
        "item": display_name,
        "price": price,
        "price_label": MONEY_D->format_money(price),
        "balance": balance,
        "balance_label": MONEY_D->format_money(balance),
        "refresh_status": 1,
        "refresh_inventory": 1,
    ]));
    result += CLIENT_PROJECTION_D->build_item_client_payload_from_template_id(goods_id, item);
    save_phase = save_shop_player_phase(player);
    if (!mapp(save_phase) || save_phase["outcome"] != "committed") {
            /* 已扣款但最终库存保存未完成时，只有明确 not_written
             * 才能安全退款；其余状态保留标记，等待同一 operation 恢复。 */
            if (save_phase["outcome"] == "not_written") {
                /* 先移除内存中的已交付物品，再持久化退款候选。
                 * 否则退款腿的保存快照可能同时保留物品，形成“物品与退款
                 * 都落盘”的双重价值交付。 */
                if (objectp(item) && environment(item))
                    destruct(item);
                money_result = MONEY_D->apply_money_operation(player, ([
                    "operation_id": operation_id + "/refund",
                    "direction": "add",
                    "amount": price,
                    "domain": "shop",
                    "action": "buy_refund",
                ]));
                if (mapp(money_result) && money_result["ok"] == 1) {
                    if (function_exists("schedule_save", player))
                        player->schedule_save();
                    return build_result(0,
                        "购买未能保存，款项已退回，请重新购买。\n", ([
                            "action": "buy",
                            "item": display_name,
                            "price": price,
                            "price_label": MONEY_D->format_money(price),
                            "balance": balance,
                            "balance_label": MONEY_D->format_money(balance),
                            "operation_id": operation_id,
                            "recovery_required": 1,
                        ]));
                }
                queue_compensation_with_operation(player, "shop", "buy_refund",
                    player_id, goods_id, operation_id + "/refund", price);
            }
            return build_result(0,
                "购买结果正在恢复，请使用相同请求编号重试。\n", ([
                    "action": "buy",
                    "item": display_name,
                    "price": price,
                    "price_label": MONEY_D->format_money(price),
                    "balance": balance,
                    "balance_label": MONEY_D->format_money(balance),
                    "operation_id": operation_id,
                    "pending_recovery": 1,
                ]));
        }
    return result;
}

mapping settle_shop_sell(object player, object item, string display_name, int price)
{
    return settle_shop_sell_with_operation(player, item, display_name, price,
        0);
}

mapping settle_shop_sell_with_operation(object player, object item,
    string display_name, int price, string operation_id)
{
    mapping result;
    mapping money_result;
    mapping receipt;
    mapping save_phase;
    mapping sell_attachment;
    mapping receipt_phase;
    object marked_item;
    string player_id;
    string pending_key;

    if (!objectp(player) || !objectp(item) || price <= 0)
        return build_result(0, "出售失败。\n", 0);

    display_name = stringp(display_name) && display_name != "" ? display_name : "物品";
    player_id = PLAYER_STATE_D->query_player_id(player);
    if (!stringp(operation_id) || operation_id == "")
        operation_id = issue_money_operation_id("shop", "sell", player_id,
            item->query("instance_id") || item->query("template_id") || "item");
    pending_key = "economy/shop_sell_pending/" + operation_id;
    sell_attachment = ITEM_INVENTORY_D->build_item_attachment_from_object(item);
    if (!mapp(sell_attachment)
        || !stringp(sell_attachment["template_id"])
        || sell_attachment["template_id"] == "")
        return build_result(0, "这件物品缺少可恢复的物品凭证，暂不能出售。\n", ([
            "action": "sell",
            "item": display_name,
            "operation_id": operation_id,
        ]));
    {
        receipt = MONEY_D->query_money_operation_receipt(player, operation_id);
        if (mapp(receipt)) {
            if (receipt["amount"] != price || receipt["direction"] != "add"
                || receipt["domain"] != "shop"
                || receipt["action"] != "sell")
                return build_result(0, "出售操作参数冲突，请使用新的请求编号。\n", ([
                    "action": "sell",
                    "item": display_name,
                    "price": price,
                    "operation_id": operation_id,
                    "operation_conflict": 1,
                ]));
            marked_item = find_shop_operation_item(player, operation_id,
                "economy/shop_sell_operations");
            if (objectp(marked_item)) {
                sell_attachment = ITEM_INVENTORY_D->build_item_attachment_from_object(
                    marked_item);
                if (!mapp(sell_attachment)
                    || !stringp(sell_attachment["template_id"])
                    || sell_attachment["template_id"] == "")
                    return build_result(0,
                        "这笔出售的物品凭证无法恢复，请稍后重试。\n", ([
                        "action": "sell",
                        "item": display_name,
                        "operation_id": operation_id,
                        "pending_recovery": 1,
                    ]));
                player->set_temp(pending_key, sell_attachment);
                destruct(marked_item);
                save_phase = save_shop_player_phase(player);
                if (mapp(save_phase)
                    && save_phase["outcome"] == "committed") {
                    player->delete_temp(pending_key);
                    return build_result(1, "", ([
                        "action": "sell",
                        "item": display_name,
                        "price": price,
                        "price_label": MONEY_D->format_money(price),
                        "balance": MONEY_D->query_balance(player),
                        "balance_label": MONEY_D->format_money(
                            MONEY_D->query_balance(player)),
                        "refresh_status": 1,
                        "refresh_inventory": 1,
                        "duplicate": 1,
                    ]));
                }
                if (mapp(save_phase)
                    && save_phase["outcome"] == "not_written"
                    && restore_shop_sell_item(player, sell_attachment,
                        operation_id))
                    return build_result(0,
                        "出售未能保存，请使用相同请求重试。\n", ([
                        "action": "sell",
                        "item": display_name,
                        "price": price,
                        "operation_id": operation_id,
                        "pending_recovery": 1,
                    ]));
                return build_result(0,
                    "这笔出售正在恢复，请使用相同请求重试。\n", ([
                    "action": "sell",
                    "item": display_name,
                    "price": price,
                    "operation_id": operation_id,
                    "pending_recovery": 1,
                ]));
            }
            receipt_phase = query_shop_sell_save_receipt(player);
            if (receipt_phase["outcome"] == "not_written") {
                sell_attachment = player->query_temp(pending_key);
                if (mapp(sell_attachment)
                    && restore_shop_sell_item(player, sell_attachment,
                        operation_id))
                    return build_result(0,
                        "出售未能保存，请使用相同请求重试。\n", ([
                        "action": "sell",
                        "item": display_name,
                        "price": price,
                        "operation_id": operation_id,
                        "pending_recovery": 1,
                    ]));
            }
            if (receipt_phase["outcome"] == "persisted_receipt_pending"
                || receipt_phase["outcome"] == "unknown")
                return build_result(0,
                    "这笔出售正在恢复，请使用相同请求重试。\n", ([
                    "action": "sell",
                    "item": display_name,
                    "price": price,
                    "operation_id": operation_id,
                    "pending_recovery": 1,
                ]));
            if (!objectp(item) || environment(item) != player)
                return build_result(1, "", ([
                    "action": "sell",
                    "item": display_name,
                    "price": price,
                    "price_label": MONEY_D->format_money(price),
                    "balance": MONEY_D->query_balance(player),
                    "balance_label": MONEY_D->format_money(
                        MONEY_D->query_balance(player)),
                    "refresh_status": 1,
                    "refresh_inventory": 1,
                    "duplicate": 1,
                ]));
            return build_result(0,
                "这笔出售已入账但物品凭证缺失，暂不能再次结算。\n", ([
                    "action": "sell",
                    "item": display_name,
                    "price": price,
                    "price_label": MONEY_D->format_money(price),
                    "balance": MONEY_D->query_balance(player),
                    "balance_label": MONEY_D->format_money(
                        MONEY_D->query_balance(player)),
                    "operation_id": operation_id,
                    "pending_recovery": 1,
                ]));
        }
    }
    mark_item_shop_operation(item, operation_id,
        "economy/shop_sell_operations");
    money_result = MONEY_D->apply_money_operation(player, ([
        "operation_id": operation_id,
        "direction": "add",
        "amount": price,
        "domain": "shop",
        "action": "sell",
    ]));
    if (!mapp(money_result) || money_result["ok"] != 1) {
        if (!mapp(money_result)
            || !money_result["pending_recovery"])
            clear_item_shop_operation(item, operation_id,
                "economy/shop_sell_operations");
        return build_result(0,
            mapp(money_result) && money_result["pending_recovery"]
                ? "出售操作正在恢复，请使用相同请求编号重试。\n"
                : "出售失败，请稍后再试。\n", ([
                "action": "sell",
                "item": display_name,
                "price": price,
                "price_label": MONEY_D->format_money(price),
                "balance": MONEY_D->query_balance(player),
                "balance_label": MONEY_D->format_money(
                    MONEY_D->query_balance(player)),
                "operation_id": operation_id,
                "pending_recovery": mapp(money_result)
                    && money_result["pending_recovery"] ? 1 : 0,
            ]));
    }

    result = build_result(1, "", ([
        "action": "sell",
        "item": display_name,
        "price": price,
        "price_label": MONEY_D->format_money(price),
        "balance": MONEY_D->query_balance(player),
        "balance_label": MONEY_D->format_money(MONEY_D->query_balance(player)),
        "refresh_status": 1,
        "refresh_inventory": 1,
    ]));
    result += CLIENT_PROJECTION_D->build_item_client_payload_from_template_id(item->query("template_id"), item);

    player->set_temp(pending_key, sell_attachment);
    if (environment(item) == player)
        destruct(item);

    save_phase = save_shop_player_phase(player);
    if (mapp(save_phase)
        && save_phase["outcome"] == "committed") {
        player->delete_temp(pending_key);
        return result;
    }
    if (mapp(save_phase)
        && save_phase["outcome"] == "not_written"
        && restore_shop_sell_item(player, sell_attachment, operation_id)) {
            player->delete_temp(pending_key);
            return build_result(0,
                "出售未能保存，请使用相同请求重试。\n", ([
                    "action": "sell",
                    "item": display_name,
                    "price": price,
                    "price_label": MONEY_D->format_money(price),
                    "balance": MONEY_D->query_balance(player),
                    "balance_label": MONEY_D->format_money(
                        MONEY_D->query_balance(player)),
                    "operation_id": operation_id,
                    "pending_recovery": 1,
                ]));
        }
    return build_result(0,
        "出售物品正在保存，请使用相同请求重试。\n", ([
            "action": "sell",
            "item": display_name,
            "price": price,
            "price_label": MONEY_D->format_money(price),
            "balance": MONEY_D->query_balance(player),
            "balance_label": MONEY_D->format_money(
                MONEY_D->query_balance(player)),
            "operation_id": operation_id,
            "pending_recovery": 1,
        ]));
}
