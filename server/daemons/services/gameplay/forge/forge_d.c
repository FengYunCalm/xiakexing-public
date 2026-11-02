/**
 * @file    forge_d.c
 * @brief   锻造服务真源骨架
 *
 * @note
 * 固定 recipe 制作已收口到 `CRAFTING_RUNTIME_D`；forge 仍保留自身配方真源、
 * 铁匠 scope 语义以及强化真链。
 */

#pragma strict_types

#include <system/services.h>

inherit "/lib/framework/daemon";

private string query_smith_scope(object smith)
{
    string scope;

    if (objectp(smith) && function_exists("query_quest_provider_id", smith)) {
        scope = smith->query_quest_provider_id();
        if (stringp(scope) && scope != "")
            return scope;
    }

    return objectp(smith) && stringp(smith->query("role")) ? smith->query("role") : "";
}

private int smith_matches_scope(object smith, string scope)
{
    if (!stringp(scope) || scope == "")
        return 1;
    if (query_smith_scope(smith) == scope)
        return 1;

    return objectp(smith)
        && (smith->query(scope)
            || smith->query("role") == scope
            || smith->query("npc_type") == scope);
}

private mapping query_forge_craft_options()
{
    return ([
        "action": "forge",
        "usage_message": "用法：forge craft <recipe_id>\n",
        "unknown_recipe_message": "铁匠这里没有这个固定配方。\n",
        "invalid_scope_error": "invalid_smith_scope",
        "invalid_scope_message": "这里只有张铁匠能做这份活。\n",
        "invalid_materials_message": "这个打造配方的材料定义有问题，暂时不能使用。\n",
        "missing_materials_message": "你的材料还不齐。\n",
        "consume_missing_materials_message": "你的材料不够，没法继续打造。\n",
        "money_action_verb": "打造",
        "default_recipe_name": "这件东西",
        "create_item_failed_message": "铁匠开炉失败，这件东西没打出来。\n",
        "move_failed_message": "你的背包放不下成品，打造被中止了。\n",
        "success_actor_name": "张铁匠",
        "success_action_verb": "打好了",
    ]);
}

mapping craft(object player, object smith, string recipe_id)
{
    return CRAFTING_RUNTIME_D->execute_recipe_craft(player,
        smith,
        recipe_id,
        FORGE_RECIPE_REGISTRY_D->query_recipe(recipe_id),
        query_forge_craft_options());
}

mapping enhance(object player, object smith, object item)
{
    mapping rule;
    mapping materials;
    mapping money_result;
    string item_type;
    string sub_type;
    string operation_id;
    int cost;
    int enhance_level;

    if (!objectp(player) || !objectp(item)) {
        return ([
            "ok": 0,
            "error": "invalid_item",
            "item": objectp(item) ? item : 0,
            "message": "你要强化哪件装备？\n",
        ]);
    }

    if (environment(item) != player) {
        return ([
            "ok": 0,
            "error": "item_not_carried",
            "item": item,
            "message": "你只能强化自己当前带在身上的装备。\n",
        ]);
    }

    item_type = item->query("item_type") || "misc";
    if (item_type != "weapon" && item_type != "armor") {
        return ([
            "ok": 0,
            "error": "invalid_item_type",
            "item": item,
            "message": "只有武器和防具才能强化。\n",
        ]);
    }

    rule = FORGE_RECIPE_REGISTRY_D->query_enhance_rule(item_type);
    if (!mapp(rule)) {
        return ([
            "ok": 0,
            "error": "missing_enhance_rule",
            "item": item,
            "message": "这类装备暂时还不能强化。\n",
        ]);
    }

    if (!smith_matches_scope(smith, rule["scope"])) {
        return ([
            "ok": 0,
            "error": "invalid_smith_scope",
            "item": item,
            "message": "这里只有张铁匠能做这份活。\n",
        ]);
    }

    enhance_level = item->query("enhance_level") || 0;
    if (enhance_level >= rule["max_level"]) {
        return ([
            "ok": 0,
            "error": "max_enhance_level",
            "item": item,
            "enhance_level": enhance_level,
            "message": "这件装备已经不能再强化了。\n",
        ]);
    }

    materials = CRAFTING_RUNTIME_D->collect_recipe_materials(player,
        rule["materials"]);
    if (!mapp(materials) || !materials["ok"]) {
        if (mapp(materials) && materials["error"] == "invalid_materials") {
            return ([
                "ok": 0,
                "error": "invalid_materials",
                "item": item,
                "missing": materials["missing"],
                "message": "这个强化配方的材料定义有问题，暂时不能使用。\n",
            ]);
        }

        return ([
            "ok": 0,
            "error": "missing_materials",
            "item": item,
            "missing": mapp(materials) ? materials["missing"] : ({ }),
            "message": "你的强化材料还不齐。\n",
        ]);
    }

    cost = rule["cost"] || 0;
    if (cost > 0 && !MONEY_D->can_afford(player, cost)) {
        return ([
            "ok": 0,
            "error": "insufficient_money",
            "item": item,
            "cost": cost,
            "message": sprintf("强化%s需要 %d 文，你的钱不够。\n", item->query_name() || item->query("name") || "这件装备", cost),
        ]);
    }

    if (!CRAFTING_RUNTIME_D->consume_material_plan(player,
        materials["plan"])) {
        return ([
            "ok": 0,
            "error": "missing_materials",
            "item": item,
            "message": "你的强化材料不够，没法继续。\n",
        ]);
    }

    if (cost > 0) {
        operation_id = PLAYER_STATE_D->build_state_idempotency_key(player,
            "forge/enhance", ([
                "item_id": item->query("instance_id")
                    || item->query("template_id") || "item",
                "enhance_level": enhance_level,
                "cost": cost,
                "materials": materials["plan"],
            ]));
        money_result = ECONOMY_SETTLEMENT_D->settle_money_operation(player,
            "forge", "enhance", "deduct", cost, operation_id);
    }
    if (cost > 0 && (!mapp(money_result) || money_result["ok"] != 1)) {
        CRAFTING_RUNTIME_D->refund_material_plan(player, materials["plan"]);
        return ([
            "ok": 0,
            "error": "insufficient_money",
            "item": item,
            "cost": cost,
            "message": sprintf("强化%s需要 %d 文，你的钱不够。\n", item->query_name() || item->query("name") || "这件装备", cost),
        ]);
    }

    item->set("enhance_level", enhance_level + 1);
    sub_type = item->query("sub_type") || item->query("armor_type") || "";

    return ([
        "ok": 1,
        "action": "enhance",
        "item": item,
        "item_id": item->query("template_id") || "",
        "type": item_type,
        "sub_type": sub_type,
        "quality": item->query("quality") || 1,
        "required_skill_cap": item->query("required_skill_cap") || 0,
        "durability": function_exists("query_durability", item) ? item->query_durability() : 0,
        "max_durability": function_exists("query_max_durability", item) ? item->query_max_durability() : 0,
        "can_equip": 1,
        "can_use": 0,
        "equipped": function_exists("query_equipped", item) ? item->query_equipped() : 0,
        "cost": cost,
        "enhance_level": enhance_level + 1,
        "message": sprintf("张铁匠替你把%s强化到了 +%d。\n", item->query_name() || item->query("name") || "这件装备", enhance_level + 1),
    ]);
}
