/**
 * @file    reward_apply_d.c
 * @brief   奖励统一 apply 真源,落地 gold/potential/exp/item/title 奖励。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

#define REWARD_APPLICATION_SCHEMA "xkx_reward_application_v1"

private mixed deep_copy(mixed value)
{
    return restore_variable(save_variable(value));
}

private string query_reward_application_key(mapping options)
{
    string target_key;

    if (!mapp(options) || !options["idempotent"])
        return "";
    target_key = options["target_key"];
    return stringp(target_key) && target_key != "" ? target_key : "";
}

private mapping apply_reward_gold(object player, int gold,
    mapping options)
{
    string operation_id;
    string source;

    if (!objectp(player) || gold <= 0)
        return ([ "ok": 0, "state": "invalid_args" ]);
    source = mapp(options) && stringp(options["source"])
        ? options["source"] : "reward_apply";
    /* L-224/L-220：金币腿只有和整包同一个幂等开关时才复用 target_key。
     * 否则会出现“包不去重、金币腿去重”的半幂等语义。 */
    operation_id = mapp(options) && options["idempotent"]
        && stringp(options["target_key"]) && options["target_key"] != ""
            ? options["target_key"] + "/gold"
            : PLAYER_STATE_D->build_state_idempotency_key(player,
                "reward/gold", ([ "source": source, "gold": gold ]));
    return ECONOMY_SETTLEMENT_D->settle_money_operation(player,
        "reward", source, "add", gold, operation_id);
}

/* L-02：退还“奖励支出”里已经扣掉的钱，方向必须是 add。
 * 这条腿不能复用 apply_reward_gold_rollback（那是撤销 add 用的 deduct）。 */
/* 退款只有两种可报告的完成状态：已经落账，或已被持久补偿队列接纳。
 * 队列拒绝时不得伪装成 refunded/queued；调用者必须保留未完成责任。 */
private mapping settle_reward_refund(object player, string action,
    string operation_id, int gold)
{
    mapping refund;
    mixed queue_result;
    int queued;
    string actor_id;

    if (!objectp(player) || !stringp(action) || action == ""
        || !stringp(operation_id) || operation_id == "" || gold <= 0)
        return ([ "ok": 0, "compensation": "failed",
            "error_code": "REWARD_REFUND_INVALID_ARGS" ]);

    refund = ECONOMY_SETTLEMENT_D->settle_money_operation(player,
        "reward", action, "add", gold, operation_id);
    if (mapp(refund) && refund["ok"] == 1) {
        refund["compensation"] = "refunded";
        refund["refund_operation_id"] = operation_id;
        return refund;
    }

    actor_id = PLAYER_STATE_D->query_player_id(player);
    queue_result = ECONOMY_SETTLEMENT_D->queue_compensation_with_operation(player,
        "reward", action, actor_id, "reward_package", operation_id, gold);
    queued = intp(queue_result) ? queue_result
        : (mapp(queue_result) && (queue_result["ok"] || queue_result["queued"]));
    return ([
        "ok": 0,
        "compensation": queued ? "queued" : "failed",
        "queued": queued,
        "pending_recovery": queued ? 1 : 0,
        "compensation_required": queued ? 0 : 1,
        "retryable": 1,
        "refund_operation_id": operation_id,
        "error_code": queued
            ? (action == "spend_refund" ? "REWARD_SPEND_REFUND_PENDING"
                : "REWARD_REVERT_REFUND_PENDING")
            : (action == "spend_refund" ? "REWARD_SPEND_REFUND_QUEUE_FAILED"
                : "REWARD_REVERT_REFUND_QUEUE_FAILED"),
    ]);
}

private mapping apply_reward_gold_rollback(object player, mapping rewards)
{
    string operation_id;
    int gold;

    if (!objectp(player) || !mapp(rewards))
        return ([ "ok": 0, "state": "invalid_args" ]);
    gold = intp(rewards["gold"]) ? rewards["gold"] : 0;
    if (gold <= 0)
        return ([ "ok": 1, "skipped": 1 ]);
    operation_id = stringp(rewards["gold_operation_id"])
        ? rewards["gold_operation_id"] + "/rollback"
        : PLAYER_STATE_D->build_state_idempotency_key(player,
            "reward/gold_rollback", ([ "gold": gold ]));
    return ECONOMY_SETTLEMENT_D->settle_money_operation(player,
        "reward", "apply_rollback", "deduct", gold, operation_id);
}

private int commit_reward_player_state(object player, mapping changes,
    string operation)
{
    mapping response;

    if (!objectp(player) || !mapp(changes) || sizeof(changes) == 0)
        return 0;
    response = PLAYER_STATE_D->commit_state_fields(player, changes,
        PLAYER_STATE_D->build_state_idempotency_key(player,
            operation || "reward/state", changes));
    return mapp(response) && response["ok"];
}

private int query_reward_state_generation(object player)
{
    mixed generation;

    if (!objectp(player))
        return -1;
    generation = PLAYER_STATE_D->query_state_value(player,
        "persistence/player_state_generation");
    return intp(generation) && generation >= 0 ? generation : 0;
}

private mapping query_reward_application_receipt(object player,
    mapping options)
{
    mapping ledger;
    mapping receipt;
    string target_key;

    if (!objectp(player))
        return 0;
    target_key = query_reward_application_key(options);
    if (target_key == "")
        return 0;
    ledger = player->query("reward/applications");
    if (!mapp(ledger))
        return 0;
    receipt = ledger[target_key];
    if (!mapp(receipt)
        || receipt["schema"] != REWARD_APPLICATION_SCHEMA
        || receipt["target_key"] != target_key
        || !mapp(receipt["applied_summary"])
        || !receipt["applied_summary"]["ok"])
        return 0;
    return deep_copy(receipt);
}

private mapping normalize_attachment(mixed attachment)
{
    string item_id;
    string item_path;
    int count;

    item_id = "";
    item_path = "";
    count = 1;

    if (stringp(attachment)) {
        item_id = attachment;
    } else if (mapp(attachment)) {
        item_id = attachment["template_id"] || attachment["item"] || attachment["id"] || "";
        item_path = attachment["item_path"] || "";
        if (intp(attachment["count"]) && attachment["count"] > 0)
            count = attachment["count"];
    }

    if (item_id == "" && item_path == "")
        return 0;

    return ([
        "item_id": item_id,
        "item_path": item_path,
        "count": count,
    ]);
}

private object create_reward_item(mapping spec)
{
    object item;
    string item_id;
    string item_path;
    int count;

    if (!mapp(spec))
        return 0;

    item_id = spec["item_id"];
    item_path = spec["item_path"];
    count = intp(spec["count"]) && spec["count"] > 0 ? spec["count"] : 1;

    if (stringp(item_id) && item_id != "" && ITEM_D->item_exists(item_id))
        return ITEM_D->create_item(item_id, count);

    if (!stringp(item_path) || item_path == "")
        return 0;

    item = new(item_path);
    if (objectp(item) && count > 1 && item->query("stackable")) {
        item->set("amount", count);
        item->set("count", count);
    }
    return item;
}

private void apply_reward_item_policy(object item, mapping options)
{
    mapping provenance;
    mixed usage_modes;

    if (!objectp(item) || !mapp(options))
        return;
    provenance = mapp(options["provenance"])
        ? copy(options["provenance"]) : ([]);
    provenance["settled_at"] = time();
    provenance["settlement_key"] = options["target_key"] || "";
    item->set("acquisition/provenance", provenance);
    item->set("acquisition/binding_policy",
        options["binding_policy"] || "character");
    usage_modes = arrayp(options["usage_modes"])
        ? copy(options["usage_modes"]) : ({ });
    item->set("acquisition/usage_modes", usage_modes);
}

private mapping *build_reward_item_summaries(mixed raw_gains,
    string location)
{
    mapping *summaries;
    mixed gain;
    object item;

    summaries = ({ });
    if (!arrayp(raw_gains))
        return summaries;
    foreach (gain in raw_gains) {
        if (!mapp(gain) || !objectp(gain["item"]))
            continue;
        item = gain["item"];
        summaries += ({ ([
            "template_id": item->query("template_id") || "",
            "item_path": base_name(item),
            "count": intp(gain["count"]) && gain["count"] > 0
                ? gain["count"] : 1,
            "location": location,
        ]) });
    }
    return summaries;
}

private mapping build_reward_application_summary(mapping result)
{
    return ([
        "ok": mapp(result) && result["ok"] ? 1 : 0,
        "gold": mapp(result) ? (result["gold"] || 0) : 0,
        "potential": mapp(result) ? (result["potential"] || 0) : 0,
        "exp": mapp(result) ? (result["exp"] || 0) : 0,
        "sect_contribution": mapp(result)
            ? (result["sect_contribution"] || 0) : 0,
        "reputation": mapp(result) ? copy(result["reputation"] || ([])) : ([]),
        "title": mapp(result) ? (result["title"] || "") : "",
        "inventory_items": build_reward_item_summaries(
            mapp(result) ? result["inventory_item_gains"] : 0, "inventory"),
        "room_items": build_reward_item_summaries(
            mapp(result) ? result["room_item_gains"] : 0, "room"),
    ]);
}

private mapping record_reward_application_receipt(object player,
    mapping options, mapping result)
{
    mapping ledger;
    mapping receipt;
    string target_key;

    if (!objectp(player) || !mapp(result) || !result["ok"])
        return 0;
    target_key = query_reward_application_key(options);
    if (target_key == "")
        return 0;
    ledger = player->query("reward/applications");
    if (!mapp(ledger))
        ledger = ([]);
    receipt = ledger[target_key];
    if (mapp(receipt))
        return deep_copy(receipt);
    receipt = ([
        "schema": REWARD_APPLICATION_SCHEMA,
        "target_key": target_key,
        "source": options["source"] || "reward_apply",
        "applied_summary": build_reward_application_summary(result),
        "applied_at": time(),
    ]);
    ledger[target_key] = receipt;
    if (!commit_reward_player_state(player,
        ([ "reward/applications": ledger ]), "reward/application"))
        return 0;
    return deep_copy(receipt);
}

private mapping build_idempotent_reward_result(mapping receipt)
{
    mapping summary;

    summary = mapp(receipt) && mapp(receipt["applied_summary"])
        ? deep_copy(receipt["applied_summary"]) : ([]);
    return ([
        "ok": 1,
        "idempotent": 1,
        "already_applied": 1,
        "persisted": 1,
        "gold": summary["gold"] || 0,
        "potential": summary["potential"] || 0,
        "exp": summary["exp"] || 0,
        "sect_contribution": summary["sect_contribution"] || 0,
        "reputation": copy(summary["reputation"] || ([])),
        "inventory_items": ({ }),
        "inventory_item_gains": ({ }),
        "room_items": ({ }),
        "room_item_gains": ({ }),
        "title": summary["title"] || "",
        "budget": ([ ]),
        "diminishing": ([ ]),
        "applied_summary": summary,
        "application_receipt": deep_copy(receipt),
    ]);
}

private void rollback_reward_items(object *items)
{
    object item;

    if (!arrayp(items))
        return;

    foreach (item in items) {
        if (objectp(item))
            destruct(item);
    }
}

private int query_reward_item_stack_amount(object item)
{
    int amount;

    if (!objectp(item))
        return 0;

    amount = item->query("amount");
    if (!intp(amount) || amount <= 0)
        amount = item->query("count");
    return amount > 0 ? amount : 1;
}

private void set_reward_item_stack_amount(object item, int amount)
{
    if (!objectp(item))
        return;
    if (amount <= 0) {
        destruct(item);
        return;
    }
    item->set("amount", amount);
    item->set("count", amount);
}

private void rollback_merged_reward_items(mapping *merged_items)
{
    mapping entry;
    object item;
    int count;
    int amount;

    if (!arrayp(merged_items))
        return;

    foreach (entry in merged_items) {
        if (!mapp(entry))
            continue;
        item = entry["item"];
        count = entry["count"];
        if (!objectp(item) || !intp(count) || count <= 0)
            continue;
        amount = query_reward_item_stack_amount(item) - count;
        set_reward_item_stack_amount(item, amount);
    }
}

private void rollback_applied_items(mapping result, mapping item_result)
{
    if (mapp(item_result)) {
        rollback_merged_reward_items(item_result["merged_items"]);
        rollback_reward_items((item_result["rollback_items"] || ({ }))
            + (item_result["room_items"] || ({ })));
    }
    if (!mapp(result))
        return;
    result["inventory_items"] = ({ });
    result["inventory_item_gains"] = ({ });
    result["room_items"] = ({ });
    result["room_item_gains"] = ({ });
}

private mapping apply_attachment_rewards(object player, mixed *attachments,
    int drop_on_full, mapping options)
{
    mapping result;
    mapping placement;
    mixed attachment;
    mapping spec;
    object item;
    object room;

    result = ([
        "ok": 1,
        "inventory_items": ({ }),
        "inventory_item_gains": ({ }),
        "rollback_items": ({ }),
        "merged_items": ({ }),
        "room_items": ({ }),
        "room_item_gains": ({ }),
    ]);

    if (!objectp(player) || !arrayp(attachments) || sizeof(attachments) == 0)
        return result;

    foreach (attachment in attachments) {
        spec = normalize_attachment(attachment);
        if (!mapp(spec)) {
            rollback_merged_reward_items(result["merged_items"]);
            rollback_reward_items(result["rollback_items"] + result["room_items"]);
            result["ok"] = 0;
            return result;
        }

        item = create_reward_item(spec);
        if (!objectp(item)) {
            rollback_merged_reward_items(result["merged_items"]);
            rollback_reward_items(result["rollback_items"] + result["room_items"]);
            result["ok"] = 0;
            return result;
        }
        apply_reward_item_policy(item, options);

        placement = ITEM_INVENTORY_D->add_item_to_inventory(player, item);
        if (mapp(placement) && placement["ok"] && objectp(placement["item"])) {
            result["inventory_items"] += ({ placement["item"] });
            result["inventory_item_gains"] += ({ ([
                "item": placement["item"],
                "count": intp(placement["count"]) && placement["count"] > 0 ? placement["count"] : query_reward_item_stack_amount(placement["item"]),
            ]) });
            if (!placement["merged"])
                result["rollback_items"] += ({ placement["item"] });
            else
                result["merged_items"] += ({ ([ "item": placement["item"], "count": placement["count"] ]) });
            continue;
        }

        room = environment(player);
        if (drop_on_full && objectp(room) && item->move(room)) {
            result["room_items"] += ({ item });
            result["room_item_gains"] += ({ ([
                "item": item,
                "count": query_reward_item_stack_amount(item),
            ]) });
            continue;
        }

        destruct(item);
        rollback_merged_reward_items(result["merged_items"]);
        rollback_reward_items(result["rollback_items"] + result["room_items"]);
        result["ok"] = 0;
        return result;
    }

    return result;
}

private mixed *collect_reward_attachments(mapping rewards)
{
    mixed *attachments;
    string item_id;
    int item_count;

    attachments = arrayp(rewards["attachments"]) ? copy(rewards["attachments"]) : ({ });

    item_id = rewards["item"];
    item_count = intp(rewards["item_count"]) && rewards["item_count"] > 0 ? rewards["item_count"] : 1;
    if (stringp(item_id) && item_id != "")
        attachments += ({ ([ "template_id": item_id, "count": item_count ]) });

    return attachments;
}

int can_spend_reward_package(object player, mapping rewards)
{
    int gold;
    int potential;
    int exp;

    if (!objectp(player) || !mapp(rewards))
        return 0;

    gold = intp(rewards["gold"]) ? rewards["gold"] : 0;
    potential = intp(rewards["potential"]) ? rewards["potential"] : 0;
    exp = intp(rewards["exp"]) ? rewards["exp"] : 0;

    if (gold > 0 && !MONEY_D->can_afford(player, gold))
        return 0;
    if (potential > 0 && !PROGRESSION_D->can_spend_potential(player, potential))
        return 0;
    if (exp > 0 && !PROGRESSION_D->can_spend_combat_exp(player, exp))
        return 0;

    return 1;
}

/* L-03：失败结果必须保留操作身份与可重试阶段，不能只返回普通 0。
 * int 入口保持兼容（调用方仍旧只看成功/失败），阶段信息走 detail 入口。 */
mapping spend_reward_package_detail(object player, mapping rewards)
{
    mapping money_result;
    mapping compensation_result;
    string operation_id;
    int gold;
    int potential;
    int exp;
    int potential_rollback_ok;

    if (!can_spend_reward_package(player, rewards))
        return 0;

    gold = intp(rewards["gold"]) ? rewards["gold"] : 0;
    potential = intp(rewards["potential"]) ? rewards["potential"] : 0;
    exp = intp(rewards["exp"]) ? rewards["exp"] : 0;

    operation_id = stringp(rewards["operation_id"])
        && rewards["operation_id"] != ""
        ? rewards["operation_id"]
        : PLAYER_STATE_D->build_state_idempotency_key(player,
            "reward/spend_gold", ([ "gold": gold, "potential": potential,
                "exp": exp ]));
    if (gold > 0) {
        money_result = ECONOMY_SETTLEMENT_D->settle_money_operation(player,
            "reward", "spend", "deduct", gold, operation_id);
        if (!mapp(money_result) || money_result["ok"] != 1)
            return ([
                "ok": 0,
                "stage": "spend_gold",
                "operation_id": operation_id,
                "compensation": "none",
                "error_code": mapp(money_result) && stringp(money_result["code"])
                    ? money_result["code"] : "REWARD_SPEND_GOLD_FAILED",
            ]);
    }

    if (potential > 0 && !PROGRESSION_D->spend_potential(player, potential)) {
        compensation_result = gold > 0
            ? settle_reward_refund(player, "spend_refund",
                operation_id + "/refund", gold)
            : ([ "ok": 1, "compensation": "none" ]);
        return ([
            "ok": 0,
            "stage": "spend_potential",
            "operation_id": operation_id,
            "refund_operation_id": gold > 0 ? operation_id + "/refund" : "",
            "compensation": compensation_result["compensation"] || "failed",
            "pending_recovery": compensation_result["pending_recovery"] || 0,
            "compensation_required": compensation_result["compensation_required"] || 0,
            "retryable": 1,
            "error_code": compensation_result["error_code"]
                || "REWARD_SPEND_POTENTIAL_FAILED",
        ]);
    }

    if (exp > 0 && !PROGRESSION_D->spend_combat_exp(player, exp)) {
        potential_rollback_ok = 1;
        if (potential > 0
            && !PROGRESSION_D->add_potential(player, potential))
            potential_rollback_ok = 0;
        compensation_result = gold > 0
            ? settle_reward_refund(player, "spend_refund",
                operation_id + "/refund", gold)
            : ([ "ok": 1, "compensation": "none" ]);
        return ([
            "ok": 0,
            "stage": "spend_exp",
            "operation_id": operation_id,
            "refund_operation_id": gold > 0 ? operation_id + "/refund" : "",
            "compensation": potential_rollback_ok
                ? (compensation_result["compensation"] || "failed") : "failed",
            "potential_rollback_ok": potential_rollback_ok,
            "pending_recovery": compensation_result["pending_recovery"] || 0,
            "compensation_required": (!potential_rollback_ok
                || compensation_result["compensation"] == "failed") ? 1 : 0,
            "retryable": 1,
            "error_code": !potential_rollback_ok
                ? "REWARD_SPEND_POTENTIAL_REFUND_FAILED"
                : (compensation_result["error_code"]
                    || "REWARD_SPEND_EXP_FAILED"),
        ]);
    }

    return ([
        "ok": 1,
        "stage": "committed",
        "operation_id": operation_id,
        "compensation": "none",
    ]);
}

int spend_reward_package(object player, mapping rewards)
{
    mapping detail;

    detail = spend_reward_package_detail(player, rewards);
    return mapp(detail) && detail["ok"] == 1;
}

mapping revert_reward_package(object player, mapping rewards)
{
    int gold;
    int potential;
    int exp;
    int sect_contribution;
    mapping contribution_result;
    mapping compensation_result;
    mapping result;
    string operation_id;

    result = ([
        "ok": 1,
        "compensation": "none",
        "pending_recovery": 0,
        "compensation_required": 0,
        "legs": ([]),
    ]);
    if (!objectp(player) || !mapp(rewards))
        return ([ "ok": 0, "error_code": "REWARD_REVERT_INVALID_ARGS" ]);

    gold = intp(rewards["gold"]) ? rewards["gold"] : 0;
    potential = intp(rewards["potential"]) ? rewards["potential"] : 0;
    exp = intp(rewards["exp"]) ? rewards["exp"] : 0;
    sect_contribution = intp(rewards["sect_contribution"])
        ? rewards["sect_contribution"] : 0;
    operation_id = stringp(rewards["operation_id"])
        && rewards["operation_id"] != ""
        ? rewards["operation_id"]
        : (stringp(rewards["gold_operation_id"])
            && rewards["gold_operation_id"] != ""
            ? rewards["gold_operation_id"]
            : PLAYER_STATE_D->build_state_idempotency_key(player,
                "reward/revert", ([ "gold": gold, "potential": potential,
                    "exp": exp, "sect_contribution": sect_contribution ])));
    result["operation_id"] = operation_id;

    if (gold > 0) {
        compensation_result = settle_reward_refund(player, "revert_refund",
            operation_id + "/refund", gold);
        result["legs"]["gold"] = compensation_result;
        if (compensation_result["compensation"] == "queued") {
            result["ok"] = 0;
            result["pending_recovery"] = 1;
            result["compensation"] = "queued";
        } else if (compensation_result["compensation"] == "failed") {
            result["ok"] = 0;
            result["compensation_required"] = 1;
            result["compensation"] = "failed";
            result["error_code"] = compensation_result["error_code"];
        } else {
            result["compensation"] = "refunded";
        }
    }
    if (potential > 0) {
        result["legs"]["potential"] = ([ "amount": potential ]);
        if (!PROGRESSION_D->add_potential(player, potential)) {
            result["ok"] = 0;
            result["pending_recovery"] = 1;
            result["compensation_required"] = 1;
            result["error_code"] = "REWARD_REVERT_POTENTIAL_FAILED";
        } else {
            result["legs"]["potential"]["ok"] = 1;
        }
    }
    if (exp > 0) {
        result["legs"]["exp"] = ([ "amount": exp ]);
        if (!PROGRESSION_D->adjust_combat_exp(player, exp)) {
            result["ok"] = 0;
            result["pending_recovery"] = 1;
            result["compensation_required"] = 1;
            result["error_code"] = "REWARD_REVERT_EXP_FAILED";
        } else {
            result["legs"]["exp"]["ok"] = 1;
        }
    }
    if (sect_contribution > 0) {
        contribution_result = SECT_D->apply_contribution_event(
            player,
            -sect_contribution,
            "reward_revert",
            "reward_package_revert"
        );
        result["legs"]["sect_contribution"] = ([
            "amount": sect_contribution,
            "ok": mapp(contribution_result) && contribution_result["ok"] ? 1 : 0,
        ]);
        if (!mapp(contribution_result) || !contribution_result["ok"]) {
            result["ok"] = 0;
            result["pending_recovery"] = 1;
            result["compensation_required"] = 1;
            result["error_code"] = "REWARD_REVERT_CONTRIBUTION_FAILED";
        }
    }

    return result;
}

private int rollback_applied_reward_package(object player, mapping rewards)
{
    mapping reputation;
    mapping contribution_result;
    string scope;
    int delta;
    int exp;
    int gold;
    int ok;
    int potential;
    int sect_contribution;
    mapping money_result;

    if (!objectp(player) || !mapp(rewards))
        return 0;
    ok = 1;
    gold = intp(rewards["gold"]) ? rewards["gold"] : 0;
    potential = intp(rewards["potential"]) ? rewards["potential"] : 0;
    exp = intp(rewards["exp"]) ? rewards["exp"] : 0;
    sect_contribution = intp(rewards["sect_contribution"])
        ? rewards["sect_contribution"] : 0;
    reputation = mapp(rewards["reputation"])
        ? rewards["reputation"] : ([]);

    foreach (scope, delta in reputation) {
        if (stringp(scope) && scope != "" && intp(delta) && delta != 0
            && !REPUTATION_D->add_reputation_scope(player, scope, -delta))
            ok = 0;
    }
    if (sect_contribution > 0) {
        contribution_result = SECT_D->apply_contribution_event(player,
            -sect_contribution, "reward_rollback", "reward_apply_rollback");
        if (!mapp(contribution_result) || !contribution_result["ok"])
            ok = 0;
    }
    if (exp > 0 && !PROGRESSION_D->adjust_combat_exp(player, -exp))
        ok = 0;
    if (potential > 0 && !PROGRESSION_D->spend_potential(player, potential))
        ok = 0;
    if (gold > 0) {
        money_result = apply_reward_gold_rollback(player, rewards);
        if (!mapp(money_result) || money_result["ok"] != 1)
            ok = 0;
    }
    return ok;
}

private mapping persist_reward_result(object player, mapping result,
    mapping item_result, mapping before_data, int expected_generation)
{
    mixed err;
    mapping restore;
    mapping save_phase;
    int rollback_ok;
    int current_generation;

    if (!objectp(player) || !mapp(result) || !result["ok"]
        || !mapp(before_data) || expected_generation < 0)
        return ([ "ok": 0, "error": "reward_persist_failed",
            "pending_recovery": 1, "compensation_required": 1 ]);

    if (consume_test_fault("reward_save")) {
        /* 故障注入位于真实玩家 JSON 保存之前；不能先写盘再伪造
         * not_written，避免把已持久奖励反向撤销。 */
        err = 0;
        save_phase = ([ "outcome": "not_written", "persisted": 0,
            "save_result": 0, "error": "fault injected" ]);
    } else if (!function_exists("save_phases", player)) {
        save_phase = ([ "outcome": "not_written", "persisted": 0,
            "save_result": 0, "error": "player save phases unavailable" ]);
    } else {
        err = catch(save_phase = player->save_phases());
    }
    if (!err && mapp(save_phase)
        && save_phase["outcome"] == "committed") {
        result["persisted"] = 1;
        result["save_result"] = save_phase["save_result"];
        return result;
    }
    if (err || !mapp(save_phase)
        || (save_phase["outcome"] != "not_written"
            && save_phase["outcome"] != "persisted_receipt_pending"
            && save_phase["outcome"] != "unknown")) {
        result["ok"] = 0;
        result["persisted"] = -1;
        result["save_result"] = 0;
        result["state"] = "unknown";
        result["error"] = err ? "reward_persist_state_unknown"
            : "reward_persist_phase_unknown";
        result["pending_recovery"] = 1;
        result["recovery_required"] = 1;
        result["compensation_required"] = 0;
        return result;
    }
    if (save_phase["outcome"] == "persisted_receipt_pending"
        || save_phase["outcome"] == "unknown") {
        result["ok"] = 0;
        result["persisted"] = save_phase["persisted"];
        result["save_result"] = save_phase["save_result"];
        result["state"] = save_phase["outcome"];
        result["error"] = "reward_persist_receipt_pending";
        result["pending_recovery"] = 1;
        result["recovery_required"] = 1;
        /* 业务腿已经进入真实 JSON 候选；收据待补不是未发放，禁止
         * 上层按 compensation_required 反向撤销或再次发奖。 */
        result["compensation_required"] = 0;
        return result;
    }

    /* Item instances are runtime-owned and must be detached separately. The
     * player data rollback is a generation-checked field batch; never replace
     * the complete mapping after another owner has advanced the state. */
    rollback_applied_items(result, item_result);
    rollback_ok = rollback_applied_reward_package(player, result);
    current_generation = query_reward_state_generation(player);
    restore = PLAYER_STATE_D->restore_state_snapshot_if_generation(player,
        before_data, current_generation, "reward/save_failure");
    rollback_ok = rollback_ok && mapp(restore) && restore["ok"] == 1;
    if (rollback_ok) {
        if (!function_exists("save_phases", player)) {
            rollback_ok = 0;
        } else {
            err = catch(save_phase = player->save_phases());
            rollback_ok = !err && mapp(save_phase)
                && save_phase["outcome"] == "committed";
        }
    }
    result["ok"] = 0;
    result["error"] = "reward_persist_failed";
    result["pending_recovery"] = 1;
    result["rollback_ok"] = rollback_ok;
    result["state_restore"] = restore;
    if (!rollback_ok)
        result["compensation_required"] = 1;
    return result;
}

private mapping apply_reputation_rewards(object player, mixed raw_rewards)
{
    mapping applied;
    mapping rewards;
    string scope;
    int delta;

    applied = ([]);
    if (undefinedp(raw_rewards))
        return ([ "ok": 1, "applied": applied ]);
    if (!mapp(raw_rewards))
        return ([ "ok": 0, "applied": applied ]);
    rewards = raw_rewards;
    foreach (scope, delta in rewards) {
        if (!stringp(scope) || scope == "" || !intp(delta) || delta == 0
            || !REPUTATION_D->add_reputation_scope(player, scope, delta)) {
            rollback_applied_reward_package(player, ([ "reputation": applied ]));
            return ([ "ok": 0, "applied": ([]) ]);
        }
        applied[scope] = delta;
    }
    return ([ "ok": 1, "applied": applied ]);
}

mapping apply_reward_package(object player, mapping rewards, mapping options)
{
    mapping budget;
    mapping diminishing;
    mapping effective_rewards;
    mapping receipt;
    mapping result;
    mapping reputation_result;
    mapping gold_result;
    mixed *attachments;
    mapping item_result;
    int gold;
    int potential;
    int exp;
    int sect_contribution;
    int drop_on_full;
    string reward_title;
    string reward_source;
    string contribution_source;
    string contribution_reason;
    mapping contribution_result;
    mapping before_data;
    mapping restore;
    int marker_rollback;
    int expected_generation;
    int persist_required;
    int marker_rollback_ok;
    mapping marker_save_phase;
    int marker_save_error;
    int marker_saved;

    result = ([
        "ok": 0,
        "gold": 0,
        "potential": 0,
        "exp": 0,
        "sect_contribution": 0,
        "reputation": ([ ]),
        "inventory_items": ({ }),
        "inventory_item_gains": ({ }),
        "room_items": ({ }),
        "room_item_gains": ({ }),
        "title": "",
        "budget": ([ ]),
        "diminishing": ([ ]),
    ]);

    if (!objectp(player) || !mapp(rewards))
        return result;

    persist_required = mapp(options) && (options["persist"]
        || (options["idempotent"] && stringp(options["target_key"])
            && options["target_key"] != "")) ? 1 : 0;
    if (persist_required) {
        if (!function_exists("query_data", player))
            return ([ "ok": 0, "error": "reward_persist_unavailable" ]);
        before_data = deep_copy(player->query_data());
        if (!mapp(before_data))
            return ([ "ok": 0, "error": "reward_snapshot_failed" ]);
    }

    receipt = query_reward_application_receipt(player, options);
    if (mapp(receipt))
        return build_idempotent_reward_result(receipt);

    drop_on_full = mapp(options) && options["drop_on_full"] ? 1 : 0;
    reward_source = mapp(options) ? (options["source"] || "reward_apply") : "reward_apply";
    budget = NUMERIC_RULE_D->query_reward_package_budget_report(rewards, options);
    result["budget"] = mapp(budget) ? budget : ([ ]);
    if (mapp(budget) && budget["block_apply"])
        return result;

    diminishing = NUMERIC_RULE_D->query_reward_diminishing_result(player, rewards, options);
    effective_rewards = mapp(diminishing) && diminishing["ok"] && mapp(diminishing["rewards"])
        ? diminishing["rewards"]
        : rewards;
    attachments = collect_reward_attachments(effective_rewards);
    item_result = apply_attachment_rewards(player, attachments, drop_on_full, options);
    if (!mapp(item_result) || !item_result["ok"])
        return result;

    result["inventory_items"] = item_result["inventory_items"];
    result["inventory_item_gains"] = item_result["inventory_item_gains"];
    result["room_items"] = item_result["room_items"];
    result["room_item_gains"] = item_result["room_item_gains"];

    gold = intp(effective_rewards["gold"]) ? effective_rewards["gold"] : 0;
    potential = intp(effective_rewards["potential"]) ? effective_rewards["potential"] : 0;
    exp = intp(effective_rewards["exp"]) ? effective_rewards["exp"] : 0;
    sect_contribution = intp(effective_rewards["sect_contribution"]) ? effective_rewards["sect_contribution"] : 0;
    contribution_source = reward_source;
    contribution_reason = mapp(options)
        ? (options["reason"] || options["source"] || "reward_package")
        : "reward_package";

    if (gold > 0)
        gold = ECONOMY_SETTLEMENT_D->apply_reward_gold_multiplier(reward_source, gold);

    if (gold > 0) {
        gold_result = apply_reward_gold(player, gold, options);
        if (!mapp(gold_result) || gold_result["ok"] != 1) {
            rollback_applied_items(result, item_result);
            return result;
        }
        result["gold"] = gold;
        result["gold_operation_id"] = gold_result["operation_id"] || "";
    }

    if (potential > 0) {
        if (!PROGRESSION_D->add_potential(player, potential)) {
            rollback_applied_reward_package(player, ([
                "gold": result["gold"],
                "gold_operation_id": result["gold_operation_id"],
            ]));
            rollback_applied_items(result, item_result);
            result["gold"] = 0;
            return result;
        }
        result["potential"] = potential;
    }

    if (exp > 0) {
        if (!PROGRESSION_D->adjust_combat_exp(player, exp)) {
            rollback_applied_reward_package(player, ([
                "gold": result["gold"],
                "gold_operation_id": result["gold_operation_id"],
                "potential": result["potential"],
            ]));
            rollback_applied_items(result, item_result);
            result["gold"] = 0;
            result["potential"] = 0;
            return result;
        }
        result["exp"] = exp;
    }

    if (sect_contribution > 0 && SECT_D->query_player_sect_id(player) != "") {
        /* 无门派玩家跳过门派贡献单项，不阻塞其余奖励发放（新手未入派时
         * 仍可正常交付任务；已入派玩家正常累计贡献） */
        contribution_result = SECT_D->apply_contribution_event(
            player,
            sect_contribution,
            contribution_source,
            contribution_reason
        );
        if (!mapp(contribution_result) || !contribution_result["ok"]) {
            rollback_applied_reward_package(player, ([
                "gold": result["gold"],
                "potential": result["potential"],
                "exp": result["exp"],
            ]));
            rollback_applied_items(result, item_result);
            result["gold"] = 0;
            result["potential"] = 0;
            result["exp"] = 0;
            result["title"] = "";
            return result;
        }
        result["sect_contribution"] = sect_contribution;
    }

    reputation_result = apply_reputation_rewards(player,
        effective_rewards["reputation"]);
    if (!mapp(reputation_result) || !reputation_result["ok"]) {
        rollback_applied_reward_package(player, ([
            "gold": result["gold"],
            "potential": result["potential"],
            "exp": result["exp"],
            "sect_contribution": result["sect_contribution"],
        ]));
        rollback_applied_items(result, item_result);
        result["gold"] = 0;
        result["potential"] = 0;
        result["exp"] = 0;
        result["sect_contribution"] = 0;
        return result;
    }
    result["reputation"] = reputation_result["applied"];

    reward_title = effective_rewards["title"];
    if (stringp(reward_title) && reward_title != "") {
        if (!commit_reward_player_state(player, ([ "title": reward_title ]),
            "reward/title")) {
            rollback_applied_reward_package(player, result);
            rollback_applied_items(result, item_result);
            result["ok"] = 0;
            result["error"] = "title_persist_failed";
            return result;
        }
        if (function_exists("invalidate_xk_score_projection", player))
            player->invalidate_xk_score_projection();
        result["title"] = reward_title;
    }

    result["diminishing"] = mapp(diminishing) ? diminishing : ([ ]);
    if (mapp(diminishing) && mapp(diminishing["event"])
        && diminishing["event"]["enabled"] && diminishing["event"]["tracked"]
        && !NUMERIC_RULE_D->record_reward_diminishing_event(player, diminishing["event"])) {
        /* 递减状态是持久规则的一部分；记录失败时不能把已按递减前提交付
         * 的奖励报告成成功。优先回滚整份快照，非持久调用走现有补偿路径。 */
        rollback_applied_items(result, item_result);
        if (persist_required) {
            restore = PLAYER_STATE_D->restore_state_snapshot_if_generation(
                player, before_data, query_reward_state_generation(player),
                "reward/diminishing_state_failure");
            result["rollback_ok"] = mapp(restore) && restore["ok"] == 1;
        } else {
            result["rollback_ok"] = rollback_applied_reward_package(player, result);
        }
        result["ok"] = 0;
        result["error"] = "reward_diminishing_state_persist_failed";
        result["pending_recovery"] = 1;
        return result;
    }
    result["ok"] = 1;
    receipt = record_reward_application_receipt(player, options, result);
    if (mapp(receipt)) {
        result["application_receipt"] = deep_copy(receipt);
        result["applied_summary"] = deep_copy(
            receipt["applied_summary"] || ([]));
    } else if (persist_required && query_reward_application_key(options) != "") {
        /* One-time rewards are not successful until the claim marker is
         * durable. Undo the delivered package and persist the restored
         * snapshot; otherwise a retry could grant the same reward twice. */
        rollback_applied_items(result, item_result);
        marker_rollback = rollback_applied_reward_package(player, result);
        marker_rollback_ok = marker_rollback;
        restore = PLAYER_STATE_D->restore_state_snapshot_if_generation(player,
            before_data, query_reward_state_generation(player),
            "reward/claim_marker_failure");
        marker_rollback_ok = marker_rollback_ok
            && mapp(restore) && restore["ok"] == 1;
        if (marker_rollback_ok) {
            if (!function_exists("save_phases", player)) {
                marker_save_error = 0;
                marker_rollback_ok = 0;
            } else {
                marker_save_error = catch(
                    marker_save_phase = player->save_phases());
                marker_rollback_ok = !marker_save_error
                    && mapp(marker_save_phase)
                    && marker_save_phase["outcome"] == "committed";
            }
        }
        result["ok"] = 0;
        result["error"] = "reward_claim_marker_persist_failed";
        result["pending_recovery"] = 1;
        result["rollback_ok"] = marker_rollback_ok;
        result["state_restore"] = restore;
        if (!marker_rollback_ok)
            result["compensation_required"] = 1;
        return result;
    }
    if (persist_required) {
        expected_generation = query_reward_state_generation(player);
        return persist_reward_result(player, result, item_result, before_data,
            expected_generation);
    }
    return result;
}
