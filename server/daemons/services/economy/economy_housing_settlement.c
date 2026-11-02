/**
 * @file    economy_housing_settlement.c
 * @brief   住房租金与税费结算模块。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

mapping query_economy_housing_config();
mapping build_result(int success, string message, mapping extra);
string issue_money_operation_id(string domain, string action,
    string actor_id, string subject_id);
mapping settle_housing_rent_with_operation(object player, mapping property_def,
    string operation_id);
mapping settle_housing_tax_with_operation(object player, mapping property_state,
    mapping property_def, string operation_id);

int query_housing_enabled()
{
    mapping housing_config;

    housing_config = query_economy_housing_config();
    return housing_config["enabled"] ? 1 : 0;
}

int query_housing_default_rent_days()
{
    mapping housing_config;

    housing_config = query_economy_housing_config();
    return intp(housing_config["default_rent_days"]) && housing_config["default_rent_days"] > 0
        ? housing_config["default_rent_days"]
        : 7;
}

int query_housing_default_tax_days()
{
    mapping housing_config;

    housing_config = query_economy_housing_config();
    return intp(housing_config["default_tax_days"]) && housing_config["default_tax_days"] > 0
        ? housing_config["default_tax_days"]
        : 1;
}

int query_housing_tax_grace_days()
{
    mapping housing_config;

    housing_config = query_economy_housing_config();
    return intp(housing_config["tax_grace_days"]) && housing_config["tax_grace_days"] > 0
        ? housing_config["tax_grace_days"]
        : 3;
}

private int query_housing_grade_amount(mapping property_def, string suffix, int fallback)
{
    mapping housing_config;
    string grade;
    string key;

    housing_config = query_economy_housing_config();
    grade = mapp(property_def) && stringp(property_def["grade"]) ? lower_case(property_def["grade"]) : "starter";
    key = grade + "_" + suffix;
    if (intp(housing_config[key]) && housing_config[key] >= 0)
        return housing_config[key];

    return fallback;
}

int query_housing_rent(mapping property_def)
{
    if (mapp(property_def) && member_array("rent", keys(property_def)) != -1
        && intp(property_def["rent"]) && property_def["rent"] >= 0)
        return property_def["rent"];

    return query_housing_grade_amount(property_def, "rent", 1000);
}

int query_housing_tax(mapping property_def)
{
    if (mapp(property_def) && member_array("tax", keys(property_def)) != -1
        && intp(property_def["tax"]) && property_def["tax"] >= 0)
        return property_def["tax"];

    return query_housing_grade_amount(property_def, "tax", 50);
}

mapping query_housing_runtime_snapshot()
{
    return ([
        "enabled": query_housing_enabled(),
        "default_rent_days": query_housing_default_rent_days(),
        "default_tax_days": query_housing_default_tax_days(),
        "tax_grace_days": query_housing_tax_grace_days(),
        "starter_rent": query_housing_grade_amount(([ "grade": "starter" ]), "rent", 1000),
        "starter_tax": query_housing_grade_amount(([ "grade": "starter" ]), "tax", 50),
        "standard_rent": query_housing_grade_amount(([ "grade": "standard" ]), "rent", 2000),
        "standard_tax": query_housing_grade_amount(([ "grade": "standard" ]), "tax", 100),
    ]);
}

mapping settle_housing_rent(object player, mapping property_def)
{
    return settle_housing_rent_with_operation(player, property_def, 0);
}

mapping settle_housing_rent_with_operation(object player, mapping property_def,
    string operation_id)
{
    mapping money_result;
    string player_id;
    string property_id;
    int amount;

    if (!query_housing_enabled())
        return build_result(0, "房屋系统当前未启用。\n", 0);
    if (!objectp(player) || !mapp(property_def))
        return build_result(0, "无法完成房租结算。\n", 0);

    amount = query_housing_rent(property_def);
    if (amount < 0)
        amount = 0;
    if (amount > 0 && (!stringp(operation_id) || operation_id == "")
        && !MONEY_D->can_afford(player, amount))
        return build_result(0, "你身上的现金不足以支付房租。\n", 0);
    player_id = PLAYER_STATE_D->query_player_id(player);
    property_id = property_def["id"] || "property";
    if (amount > 0) {
        if (!stringp(operation_id) || operation_id == "")
            operation_id = issue_money_operation_id("housing", "rent",
                player_id, property_id);
        money_result = ECONOMY_SETTLEMENT_D->settle_money_operation(
            player, "housing", "rent", "deduct", amount, operation_id);
        if (!mapp(money_result) || money_result["ok"] != 1)
            return build_result(0, "房租扣款失败。\n", ([
                "money_result": money_result,
                "pending_recovery": mapp(money_result)
                    && money_result["pending_recovery"] ? 1 : 0,
            ]));
    }

    return build_result(1, "", ([
        "action": "housing_rent",
        "property_id": property_def["id"] || "",
        "amount": amount,
        "refresh_status": 1,
        "refresh_inventory": 1,
        "money_result": money_result,
        "pending_recovery": mapp(money_result)
            && money_result["pending_recovery"] ? 1 : 0,
    ]));
}

mapping settle_housing_tax(object player, mapping property_state, mapping property_def)
{
    return settle_housing_tax_with_operation(player, property_state, property_def,
        0);
}

mapping settle_housing_tax_with_operation(object player, mapping property_state,
    mapping property_def, string operation_id)
{
    mapping money_result;
    string player_id;
    string property_id;
    int amount;

    if (!query_housing_enabled())
        return build_result(0, "房屋系统当前未启用。\n", 0);
    if (!objectp(player) || !mapp(property_def) || !mapp(property_state))
        return build_result(0, "无法完成房税结算。\n", 0);

    amount = query_housing_tax(property_def);
    if (amount < 0)
        amount = 0;
    if (amount > 0 && (!stringp(operation_id) || operation_id == "")
        && !MONEY_D->can_afford(player, amount))
        return build_result(0, "你身上的现金不足以支付房税。\n", 0);
    player_id = PLAYER_STATE_D->query_player_id(player);
    property_id = property_def["id"] || property_state["property_id"] || "property";
    if (amount > 0) {
        if (!stringp(operation_id) || operation_id == "")
            operation_id = issue_money_operation_id("housing", "tax",
                player_id, property_id);
        money_result = ECONOMY_SETTLEMENT_D->settle_money_operation(
            player, "housing", "tax", "deduct", amount, operation_id);
        if (!mapp(money_result) || money_result["ok"] != 1)
            return build_result(0, "房税扣款失败。\n", ([
                "money_result": money_result,
                "pending_recovery": mapp(money_result)
                    && money_result["pending_recovery"] ? 1 : 0,
            ]));
    }

    return build_result(1, "", ([
        "action": "housing_tax",
        "property_id": property_def["id"] || property_state["property_id"] || "",
        "amount": amount,
        "refresh_status": 1,
    ]));
}
