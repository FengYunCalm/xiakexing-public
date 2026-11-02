/**
 * @file    crafting_runtime_d.c
 * @brief   通用 crafting recipe 执行真链
 *
 * @note
 * 这里只收口固定 recipe 制作，不处理 forge enhance、批量制作或 gather。
 * 各领域 facade 仍负责 recipe 真源、scope 语义与玩家文案。
 */

#pragma strict_types

#include <system/services.h>

inherit "/lib/framework/daemon";

void refund_material_plan(object player, mixed *plan);

private string query_recipe_name(mapping recipe, string fallback)
{
    string name;

    name = mapp(recipe) ? recipe["name"] : "";
    if (stringp(name) && name != "")
        return name;
    if (stringp(fallback) && fallback != "")
        return fallback;
    return "这件东西";
}

private string query_actor_scope(object actor)
{
    string scope;

    if (objectp(actor) && function_exists("query_quest_provider_id", actor)) {
        scope = actor->query_quest_provider_id();
        if (stringp(scope) && scope != "")
            return scope;
    }

    return objectp(actor) && stringp(actor->query("role")) ? actor->query("role") : "";
}

private int actor_matches_current_contract(object actor, string scope)
{
    if (!objectp(actor) || !stringp(scope) || scope == "")
        return 0;
    if (actor->query(scope))
        return 1;
    if (actor->query("role") == scope || actor->query("npc_type") == scope)
        return 1;
    if (scope == "pharmacist")
        return actor->query("npc_type") == "healer"
            || actor->query("role") == "healer"
            || actor->query("healer");
    return 0;
}

private int actor_matches_scope(object actor, string scope)
{
    if (!stringp(scope) || scope == "")
        return 1;
    if (query_actor_scope(actor) == scope)
        return 1;

    return actor_matches_current_contract(actor, scope);
}

private int query_material_count(object item)
{
    int count;

    if (!objectp(item))
        return 0;

    count = item->query("count") || item->query("amount") || 1;
    return count > 0 ? count : 1;
}

mapping collect_recipe_materials(object player, mixed *materials)
{
    mapping result;
    mapping reserved;
    mixed material;
    object item;
    string item_id;
    string slot_key;
    int required_count;
    int found_count;
    int consume_count;
    int available_count;

    result = ([
        "ok": 1,
        "error": "",
        "plan": ({ }),
        "missing": ({ }),
    ]);
    reserved = ([]);

    if (!objectp(player) || !arrayp(materials)) {
        result["ok"] = 0;
        result["error"] = "invalid_materials";
        return result;
    }

    foreach (material in materials) {
        found_count = 0;
        item_id = mapp(material) ? (material["item_id"] || material["template_id"] || "") : "";
        required_count = mapp(material) ? material["count"] : 0;
        if (!stringp(item_id) || item_id == "" || required_count <= 0) {
            result["ok"] = 0;
            result["error"] = "invalid_materials";
            result["missing"] += ({ ([
                "item_id": stringp(item_id) ? item_id : "",
                "need": required_count,
                "have": 0,
            ]) });
            return result;
        }

        foreach (item in all_inventory(player)) {
            if (!objectp(item) || item->query("template_id") != item_id)
                continue;

            slot_key = file_name(item);
            if (!stringp(slot_key) || slot_key == "")
                continue;

            available_count = query_material_count(item) - (reserved[slot_key] || 0);
            if (available_count <= 0)
                continue;

            consume_count = available_count;
            if (found_count + consume_count > required_count)
                consume_count = required_count - found_count;
            if (consume_count <= 0)
                continue;

            result["plan"] += ({ ([
                "item": item,
                "item_id": item_id,
                "count": consume_count,
                "path": base_name(item),
                "data": function_exists("query_data", item) ? item->query_data() : ([]),
            ]) });
            reserved[slot_key] = (reserved[slot_key] || 0) + consume_count;
            found_count += consume_count;
            if (found_count >= required_count)
                break;
        }

        if (found_count < required_count) {
            result["ok"] = 0;
            result["missing"] += ({ ([
                "item_id": item_id,
                "need": required_count,
                "have": found_count,
            ]) });
        }
    }

    return result;
}

private void restore_material_snapshot(object player, mapping snapshot)
{
    object item;
    object refund_item;
    mapping placement;

    if (!objectp(player) || !mapp(snapshot))
        return;

    item = snapshot["item"];
    if (objectp(item) && function_exists("set_data", item) && mapp(snapshot["data"])) {
        item->set_data(snapshot["data"]);
        return;
    }

    if (!stringp(snapshot["path"]) || snapshot["path"] == "")
        return;

    refund_item = new(snapshot["path"]);
    if (!objectp(refund_item))
        return;

    if (function_exists("set_data", refund_item) && mapp(snapshot["data"]))
        refund_item->set_data(snapshot["data"]);

    placement = ITEM_INVENTORY_D->add_item_to_inventory(player, refund_item);
    if (mapp(placement) && placement["ok"])
        return;
    if (objectp(environment(player)) && refund_item->move(environment(player)))
        return;

    destruct(refund_item);
}

int consume_material_plan(object player, mixed *plan)
{
    mixed entry;
    mixed *consumed;
    object item;
    int current_count;
    int consume_count;
    int remain_count;

    if (!objectp(player) || !arrayp(plan))
        return 0;

    consumed = ({ });

    foreach (entry in plan) {
        item = mapp(entry) ? entry["item"] : 0;
        if (!objectp(item)) {
            refund_material_plan(player, consumed);
            return 0;
        }

        consume_count = entry["count"] || 0;
        current_count = query_material_count(item);
        if (consume_count <= 0 || current_count < consume_count) {
            refund_material_plan(player, consumed);
            return 0;
        }

        remain_count = current_count - consume_count;
        if (remain_count > 0) {
            item->set("count", remain_count);
            item->set("amount", remain_count);
            consumed += ({ entry });
            continue;
        }

        destruct(item);
        consumed += ({ entry });
    }

    return 1;
}

void refund_material_plan(object player, mixed *plan)
{
    mixed entry;

    if (!objectp(player) || !arrayp(plan))
        return;

    foreach (entry in plan)
        restore_material_snapshot(player, entry);
}

mapping execute_recipe_craft(object player, object actor, string recipe_id, mapping recipe, mapping options)
{
    mapping materials;
    mapping placement;
    mapping money_result;
    object result_item;
    string action;
    string default_recipe_name;
    string invalid_scope_error;
    string money_action_verb;
    string recipe_name;
    string success_action_verb;
    string success_actor_name;
    string operation_id;
    int cost;

    if (!mapp(options))
        options = ([]);

    if (!objectp(player) || !stringp(recipe_id) || recipe_id == "") {
        return ([
            "ok": 0,
            "error": "invalid_recipe_id",
            "recipe_id": stringp(recipe_id) ? recipe_id : "",
            "message": options["usage_message"] || "用法：craft <recipe_id>\n",
        ]);
    }

    if (!mapp(recipe)) {
        return ([
            "ok": 0,
            "error": "unknown_recipe",
            "recipe_id": recipe_id,
            "message": options["unknown_recipe_message"] || "这里没有这个固定配方。\n",
        ]);
    }

    invalid_scope_error = options["invalid_scope_error"] || "invalid_scope";
    if (!actor_matches_scope(actor, recipe["scope"])) {
        return ([
            "ok": 0,
            "error": invalid_scope_error,
            "recipe_id": recipe_id,
            "message": options["invalid_scope_message"] || "这里做不了这份活。\n",
        ]);
    }

    materials = collect_recipe_materials(player, recipe["materials"]);
    if (!mapp(materials) || !materials["ok"]) {
        if (mapp(materials) && materials["error"] == "invalid_materials") {
            return ([
                "ok": 0,
                "error": "invalid_materials",
                "recipe_id": recipe_id,
                "missing": materials["missing"],
                "message": options["invalid_materials_message"] || "这个配方的材料定义有问题，暂时不能使用。\n",
            ]);
        }

        return ([
            "ok": 0,
            "error": "missing_materials",
            "recipe_id": recipe_id,
            "missing": mapp(materials) ? materials["missing"] : ({ }),
            "message": options["missing_materials_message"] || "你的材料还不齐。\n",
        ]);
    }

    cost = recipe["cost"] || 0;
    default_recipe_name = options["default_recipe_name"] || "这件东西";
    recipe_name = query_recipe_name(recipe, default_recipe_name);
    money_action_verb = options["money_action_verb"] || "制作";
    if (cost > 0 && !MONEY_D->can_afford(player, cost)) {
        return ([
            "ok": 0,
            "error": "insufficient_money",
            "recipe_id": recipe_id,
            "cost": cost,
            "message": sprintf("%s%s需要 %d 文，你的钱不够。\n", money_action_verb, recipe_name, cost),
        ]);
    }

    if (!consume_material_plan(player, materials["plan"])) {
        return ([
            "ok": 0,
            "error": "missing_materials",
            "recipe_id": recipe_id,
            "message": options["consume_missing_materials_message"] || options["missing_materials_message"] || "你的材料不够，没法继续。\n",
        ]);
    }

    if (cost > 0) {
        operation_id = PLAYER_STATE_D->build_state_idempotency_key(player,
            "crafting/craft", ([
                "recipe_id": recipe_id,
                "cost": cost,
                "materials": materials["plan"],
            ]));
        money_result = ECONOMY_SETTLEMENT_D->settle_money_operation(player,
            "crafting", "craft", "deduct", cost, operation_id);
    }
    if (cost > 0 && (!mapp(money_result) || money_result["ok"] != 1)) {
        refund_material_plan(player, materials["plan"]);
        return ([
            "ok": 0,
            "error": "insufficient_money",
            "recipe_id": recipe_id,
            "cost": cost,
            "message": sprintf("%s%s需要 %d 文，你的钱不够。\n", money_action_verb, recipe_name, cost),
        ]);
    }

    if (mapp(money_result) && stringp(money_result["operation_id"]))
        operation_id = money_result["operation_id"];

    /* 锻造产出可带词条（与掉落一致：RARE/EPIC 装备 roll 词条，商店货无词条） */
    result_item = ITEM_D->create_item_with_affixes(recipe["output_item_id"], 1);
    if (!objectp(result_item)) {
        /* 补偿失败进可恢复队列（不忽略返回值）。 */
        if (cost > 0) {
            money_result = ECONOMY_SETTLEMENT_D->settle_money_operation(player,
                "crafting", "cost_refund", "add", cost,
                operation_id + "/refund");
            if (!mapp(money_result) || money_result["ok"] != 1)
                ECONOMY_SETTLEMENT_D->queue_compensation_with_operation(player,
                    "crafting", "cost_refund",
                    PLAYER_STATE_D->query_player_id(player), recipe_id,
                    operation_id + "/refund", cost);
        }
        refund_material_plan(player, materials["plan"]);
        return ([
            "ok": 0,
            "error": "create_item_failed",
            "recipe_id": recipe_id,
            "message": options["create_item_failed_message"] || "这件东西没能做出来。\n",
        ]);
    }

    placement = ITEM_INVENTORY_D->add_item_to_inventory(player, result_item);
    if (!mapp(placement) || !placement["ok"]) {
        /* 补偿失败进可恢复队列（不忽略返回值）。 */
        if (cost > 0) {
            money_result = ECONOMY_SETTLEMENT_D->settle_money_operation(player,
                "crafting", "cost_refund", "add", cost,
                operation_id + "/refund");
            if (!mapp(money_result) || money_result["ok"] != 1)
                ECONOMY_SETTLEMENT_D->queue_compensation_with_operation(player,
                    "crafting", "cost_refund",
                    PLAYER_STATE_D->query_player_id(player), recipe_id,
                    operation_id + "/refund", cost);
        }
        refund_material_plan(player, materials["plan"]);
        if (objectp(result_item) && !environment(result_item))
            destruct(result_item);
        return ([
            "ok": 0,
            "error": "move_failed",
            "recipe_id": recipe_id,
            "message": options["move_failed_message"] || "你的背包放不下成品。\n",
        ]);
    }
    result_item = placement["item"];
    QUEST_D->record_action_progress(player, "craft_" + recipe_id,
        result_item, "success");

    action = options["action"] || "forge";
    success_actor_name = options["success_actor_name"] || "工匠";
    success_action_verb = options["success_action_verb"] || "做好了";
    return ([
        "ok": 1,
        "action": action,
        "recipe_id": recipe_id,
        "cost": cost,
        "item": result_item,
        "item_id": result_item->query("template_id") || recipe["output_item_id"] || "",
        "message": sprintf("%s替你%s%s，收了 %d 文。\n", success_actor_name, success_action_verb, recipe_name, cost),
    ]);
}
