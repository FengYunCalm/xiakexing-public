/**
 * @file    medical_d.c
 * @brief   医馆治疗服务真源
 *
 * @note
 * 当前第一刀只承接药师场景下的完整治疗：报价、扣费、HP/MP 恢复与状态同步。
 * 不在这一刀引入中毒、伤势分类、疗程或疾病系统，避免把现有公开 `heal`
 * 入口扩成另一套复杂状态机。
 */

#pragma strict_types

#include <system/services.h>

inherit "/lib/framework/daemon";

#define HEAL_COST_DIVISOR 40

private mapping query_resource_snapshot(object player)
{
    int hp;
    int max_hp;
    int mp;
    int max_mp;

    if (!objectp(player)) {
        return ([
            "hp": 0,
            "max_hp": 100,
            "mp": 0,
            "max_mp": 100,
        ]);
    }

    hp = function_exists("query_hp", player)
        ? player->query_hp() : (player->query("combat/hp") || 0);
    max_hp = function_exists("query_max_hp", player)
        ? player->query_max_hp() : (player->query("combat/max_hp") || 100);
    mp = function_exists("query_mp", player)
        ? player->query_mp() : (player->query("combat/mp") || 0);
    max_mp = function_exists("query_max_mp", player)
        ? player->query_max_mp() : (player->query("combat/max_mp") || 100);
    return ([
        "hp": hp,
        "max_hp": max_hp,
        "mp": mp,
        "max_mp": max_mp,
    ]);
}

private string query_healer_name(object healer)
{
    string name;

    if (!objectp(healer))
        return "大夫";

    name = healer->query_name() || healer->query("name") || "大夫";
    return stringp(name) && name != "" ? name : "大夫";
}

private int healer_has_current_contract(object healer)
{
    if (!objectp(healer))
        return 0;

    return healer->query("npc_type") == "healer"
        || healer->query("role") == "healer"
        || healer->query("healer");
}

private string query_healer_scope(object healer)
{
    string scope;

    if (objectp(healer) && function_exists("query_quest_provider_id", healer)) {
        scope = healer->query_quest_provider_id();
        if (stringp(scope) && scope != "")
            return scope;
    }

    return healer_has_current_contract(healer) ? "pharmacist" : "";
}

private int healer_matches_scope(object healer, string scope)
{
    if (!stringp(scope) || scope == "")
        return 1;
    if (query_healer_scope(healer) == scope)
        return 1;

    return scope == "pharmacist" && healer_has_current_contract(healer);
}

private int query_heal_cost(int hp_missing, int mp_missing)
{
    int cost;

    /* Keep a full treatment affordable from the first earned city income. */
    cost = (hp_missing + mp_missing + HEAL_COST_DIVISOR - 1)
        / HEAL_COST_DIVISOR;
    return cost > 0 ? cost : 1;
}

mapping query_medical_runtime_snapshot()
{
    return ([
        "service": "medical",
        "sink": "heal",
        "currency": "coin",
        "cost_formula": "ceil((hp_missing + mp_missing) / 40)",
        "min_cost": 1,
    ]);
}

mapping query_heal_quote(object player, object healer)
{
    mapping resources;
    int hp_missing;
    int mp_missing;
    int cost;

    if (!objectp(player)) {
        return ([
            "ok": 0,
            "error": "invalid_player",
            "message": "无法为这个目标疗伤。\n",
        ]);
    }

    if (!healer_matches_scope(healer, "pharmacist")) {
        return ([
            "ok": 0,
            "error": "invalid_healer_scope",
            "message": "这里只能找大夫疗伤。\n",
        ]);
    }

    resources = query_resource_snapshot(player);
    hp_missing = resources["max_hp"] - resources["hp"];
    mp_missing = resources["max_mp"] - resources["mp"];
    if (hp_missing <= 0)
        hp_missing = 0;
    if (mp_missing <= 0)
        mp_missing = 0;

    if (hp_missing <= 0 && mp_missing <= 0) {
        return ([
            "ok": 0,
            "error": "no_need_heal",
            "hp_missing": hp_missing,
            "mp_missing": mp_missing,
            "message": "你当前气血与内力都很充沛，无需治疗。\n",
        ]);
    }

    cost = query_heal_cost(hp_missing, mp_missing);
    return ([
        "ok": 1,
        "cost": cost,
        "hp_missing": hp_missing,
        "mp_missing": mp_missing,
        "healer_scope": query_healer_scope(healer),
        "healer_name": query_healer_name(healer),
    ]);
}

varargs mapping heal_player(object player, object healer,
    mapping response_context)
{
    mapping before;
    mapping after;
    mapping quote;
    mapping money_result;
    string operation_id;
    int hp_healed;
    int mp_healed;
    int cost;

    quote = query_heal_quote(player, healer);
    if (!mapp(quote) || !quote["ok"])
        return quote;

    cost = quote["cost"] || 0;
    operation_id = PLAYER_STATE_D->build_state_idempotency_key(player,
        "medical/heal", ([
            "healer": objectp(healer) ? get_object_id(healer) : "",
            "cost": cost,
            "hp_missing": quote["hp_missing"] || 0,
            "mp_missing": quote["mp_missing"] || 0,
        ]));
    if (cost > 0) {
        money_result = ECONOMY_SETTLEMENT_D->settle_money_operation(player,
            "medical", "heal", "deduct", cost, operation_id);
    }
    if (cost > 0 && (!mapp(money_result) || money_result["ok"] != 1)) {
        return ([
            "ok": 0,
            "error": "insufficient_money",
            "cost": cost,
            "hp_missing": quote["hp_missing"] || 0,
            "mp_missing": quote["mp_missing"] || 0,
            "message": sprintf("疗伤需要 %d 文，你的钱不够。\n", cost),
        ]);
    }

    if (mapp(money_result) && stringp(money_result["operation_id"]))
        operation_id = money_result["operation_id"];
    before = query_resource_snapshot(player);
    hp_healed = 0;
    mp_healed = 0;
    if ((quote["hp_missing"] || 0) > 0)
        player->receive_healing("hp", quote["hp_missing"] || 0);
    if ((quote["mp_missing"] || 0) > 0)
        player->receive_healing("mp", quote["mp_missing"] || 0);

    after = query_resource_snapshot(player);
    hp_healed = (after["hp"] || 0) - (before["hp"] || 0);
    mp_healed = (after["mp"] || 0) - (before["mp"] || 0);
    if (hp_healed <= 0 && mp_healed <= 0) {
        /* 补偿失败进可恢复队列（不忽略返回值）。 */
        if (cost > 0) {
            money_result = ECONOMY_SETTLEMENT_D->settle_money_operation(player,
                "medical", "cost_refund", "add", cost,
                operation_id + "/refund");
            if (!mapp(money_result) || money_result["ok"] != 1)
                ECONOMY_SETTLEMENT_D->queue_compensation_with_operation(player,
                    "medical", "cost_refund",
                    PLAYER_STATE_D->query_player_id(player), "heal",
                    operation_id + "/refund", cost);
        }
        return ([
            "ok": 0,
            "error": "no_healing_applied",
            "cost": 0,
            "hp_healed": 0,
            "mp_healed": 0,
            "message": "这次治疗没有恢复任何状态，诊金已经退还。\n",
        ]);
    }

    QUEST_D->record_action_progress(player, "medical_heal_success", healer,
        "success");
    send_xk_status(player, response_context);
    return ([
        "ok": 1,
        "cost": cost,
        "hp_healed": hp_healed,
        "mp_healed": mp_healed,
        "message": sprintf("%s替你调理了一番，花费 %d 文。\n",
            query_healer_name(healer), cost),
    ]);
}

varargs void sync_heal_result(object player, mapping result,
    mapping response_context)
{
    mapping extra_payload;
    mapping tx;

    if (!objectp(player) || !mapp(result))
        return;

    extra_payload = ([
        "message": result["message"] || "疗伤失败。\n",
        "hp_healed": result["hp_healed"] || 0,
        "mp_healed": result["mp_healed"] || 0,
        "hp_missing": result["hp_missing"] || 0,
        "mp_missing": result["mp_missing"] || 0,
        "error": result["error"] || "",
    ]);
    tx = CLIENT_SYNC_D->tx_add_trade_result(0, "heal", "疗伤",
        result["cost"] || 0, result["ok"], extra_payload);
    if (result["ok"])
        tx = CLIENT_SYNC_D->tx_plan_status(tx);
    tx = CLIENT_SYNC_D->tx_with_response_context(tx, response_context);
    CLIENT_SYNC_D->submit_transaction(player, tx);
}
