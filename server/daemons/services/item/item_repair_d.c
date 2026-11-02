/**
 * @file    item_repair_d.c
 * @brief   物品修理守护进程,提供修理运行时快照。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

mapping query_repair_runtime_snapshot()
{
    return ([
        "service": "item_repair",
        "sink": "repair",
        "currency": "coin",
        "cost_source": "NUMERIC_RULE_D->query_equipment_durability_budget_report",
        "min_cost": 1,
        "stateless": 1,
    ]);
}

private int is_repairable_item(object item)
{
    return objectp(item)
        && function_exists("query_durability", item)
        && function_exists("query_max_durability", item)
        && function_exists("set_durability", item);
}

private string query_smith_name(object smith)
{
    string name;

    if (!objectp(smith))
        return "铁匠";

    name = smith->query_name() || smith->query("name") || "铁匠";
    return stringp(name) && name != "" ? name : "铁匠";
}

private string query_item_name(object item)
{
    string name;

    if (!objectp(item))
        return "这件装备";

    name = item->query_name() || item->query("name") || "这件装备";
    return stringp(name) && name != "" ? name : "这件装备";
}

private string query_item_id(object item)
{
    string item_id;

    if (!objectp(item))
        return "";

    item_id = item->query("template_id") || get_object_id(item);
    return stringp(item_id) ? item_id : "";
}

mapping query_repair_quote(object player, object item)
{
    mapping budget;
    int current;
    int max_durability;
    int missing;
    int cost;

    if (!objectp(player) || !objectp(item))
        return ([ "ok": 0, "message": "无法修理这件物品。\n" ]);

    if (!is_repairable_item(item))
        return ([
            "ok": 0,
            "item_id": query_item_id(item),
            "durability": objectp(item) ? item->query_durability() : 0,
            "max_durability": objectp(item) ? item->query_max_durability() : 0,
            "message": "这件物品无法修理。\n",
        ]);

    current = item->query_durability();
    max_durability = item->query_max_durability();
    if (max_durability <= 0)
        return ([
            "ok": 0,
            "item_id": query_item_id(item),
            "durability": current,
            "max_durability": max_durability,
            "message": "这件物品没有可修理的耐久。\n",
        ]);

    if (current >= max_durability)
        return ([
            "ok": 0,
            "item_id": query_item_id(item),
            "durability": current,
            "max_durability": max_durability,
            "message": "这件物品已经是完好状态。\n",
        ]);

    missing = max_durability - current;
    budget = NUMERIC_RULE_D->query_equipment_durability_budget_report(([
        "action_type": "repair",
        "repair_missing_durability": missing,
    ]));
    cost = mapp(budget) && !undefinedp(budget["repair_cost"]) && intp(budget["repair_cost"])
        ? budget["repair_cost"] : missing;
    if (cost < 1)
        cost = 1;
    return ([
        "ok": 1,
        "item_id": query_item_id(item),
        "cost": cost,
        "durability": current,
        "max_durability": max_durability,
        "budget": mapp(budget) ? budget : ([ ]),
    ]);
}

mapping repair_item(object player, object smith, object item)
{
    mapping quote;
    mapping money_result;
    string operation_id;
    int before_durability;
    int cost;
    int repaired_durability;

    quote = query_repair_quote(player, item);
    if (!mapp(quote) || !quote["ok"])
        return quote;
    if (mapp(quote["budget"]) && quote["budget"]["block_apply"])
        return ([
            "ok": 0,
            "item_id": query_item_id(item),
            "cost": quote["cost"] || 0,
            "durability": item->query_durability(),
            "max_durability": item->query_max_durability(),
            "budget": quote["budget"],
            "message": "这件物品的修理预算超限，暂时无法修理。\n",
        ]);

    cost = quote["cost"] || 0;
    operation_id = PLAYER_STATE_D->build_state_idempotency_key(player,
        "item/repair", ([
            "item_id": query_item_id(item),
            "cost": cost,
        ]));
    money_result = ECONOMY_SETTLEMENT_D->settle_money_operation(player,
        "repair", "item", "deduct", cost, operation_id);
    if (!mapp(money_result) || money_result["ok"] != 1) {
        return ([
            "ok": 0,
            "item_id": query_item_id(item),
            "cost": cost,
            "durability": item->query_durability(),
            "max_durability": item->query_max_durability(),
            "message": sprintf("修理需要 %d 文，你的钱不够。\n", cost),
        ]);
    }

    operation_id = stringp(money_result["operation_id"])
        ? money_result["operation_id"] : operation_id;
    before_durability = item->query_durability();
    item->set_durability(quote["max_durability"] || item->query_max_durability());
    repaired_durability = item->query_durability();
    if (repaired_durability <= before_durability) {
        /* 补偿失败进可恢复队列（不忽略返回值）。 */
        money_result = ECONOMY_SETTLEMENT_D->settle_money_operation(player,
            "repair", "cost_refund", "add", cost,
            operation_id + "/refund");
        if (!mapp(money_result) || money_result["ok"] != 1)
            ECONOMY_SETTLEMENT_D->queue_compensation_with_operation(player,
                "repair", "cost_refund",
                PLAYER_STATE_D->query_player_id(player), query_item_id(item),
                operation_id + "/refund", cost);
        return ([
            "ok": 0,
            "error": "no_repair_applied",
            "item_id": query_item_id(item),
            "cost": 0,
            "durability": repaired_durability,
            "max_durability": item->query_max_durability(),
            "message": "这次修理没有恢复耐久，费用已经退还。\n",
        ]);
    }

    if (ITEM_D->is_training_weapon(item))
        QUEST_D->record_action_progress(player,
            "training_weapon_repaired", item, "success");
    return ([
        "ok": 1,
        "item_id": query_item_id(item),
        "cost": cost,
        "durability": repaired_durability,
        "max_durability": item->query_max_durability(),
        "budget": quote["budget"] || ([ ]),
        "message": sprintf("%s替你修好了%s，花费 %d 文。\n",
            query_smith_name(smith), query_item_name(item), cost),
    ]);
}
