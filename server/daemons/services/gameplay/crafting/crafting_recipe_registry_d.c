/**
 * @file    crafting_recipe_registry_d.c
 * @brief   通用 crafting 配方与强化规则注册表真源
 *
 * @note
 * 持有跨 scope 的固定 recipe / enhance rule 配置真源。
 * `forge`、后续炼药或裁缝 facade 只按各自 scope 取数，不再各自维护第二份表。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

nosave mapping _recipes;
nosave mapping _enhance_rules;
nosave mapping _registry_status;

private mapping query_crafting_config()
{
    mapping config;

    config = CONFIG_D->query_full_config("crafting");
    return mapp(config) ? config : ([]);
}

private int validate_recipe(mapping recipe)
{
    mixed material;
    mixed *materials;
    string item_id;

    if (!mapp(recipe))
        return 0;
    if (!stringp(recipe["scope"]) || recipe["scope"] == "")
        return 0;
    if (!stringp(recipe["output_item_id"]) || recipe["output_item_id"] == "")
        return 0;
    if (intp(recipe["cost"]) && recipe["cost"] < 0)
        return 0;
    if (!ITEM_D->item_exists(recipe["output_item_id"]))
        return 0;

    materials = recipe["materials"];
    if (!arrayp(materials) || sizeof(materials) == 0)
        return 0;

    foreach (material in materials) {
        item_id = mapp(material) ? (material["item_id"] || material["template_id"] || "") : "";
        if (!stringp(item_id) || item_id == "")
            return 0;
        if (!mapp(material) || material["count"] <= 0)
            return 0;
        if (!ITEM_D->item_exists(item_id))
            return 0;
    }

    return 1;
}

private int validate_enhance_rule(mapping rule)
{
    mixed material;
    mixed *materials;
    string item_id;

    if (!mapp(rule))
        return 0;
    if (!stringp(rule["scope"]) || rule["scope"] == "")
        return 0;
    if (intp(rule["cost"]) && rule["cost"] < 0)
        return 0;
    if (!intp(rule["max_level"]) || rule["max_level"] <= 0)
        return 0;

    materials = rule["materials"];
    if (!arrayp(materials) || sizeof(materials) == 0)
        return 0;

    foreach (material in materials) {
        item_id = mapp(material) ? (material["item_id"] || material["template_id"] || "") : "";
        if (!stringp(item_id) || item_id == "")
            return 0;
        if (!mapp(material) || material["count"] <= 0)
            return 0;
        if (!ITEM_D->item_exists(item_id))
            return 0;
    }

    return 1;
}

private void mark_reload_failure(string error_message)
{
    if (!mapp(_registry_status))
        _registry_status = ([]);

    _registry_status["config_name"] = "crafting";
    _registry_status["last_attempt_at"] = time();
    _registry_status["last_reload_ok"] = 0;
    _registry_status["last_error"] = stringp(error_message) ? error_message : "unknown";
}

private void mark_reload_success()
{
    int version;

    if (!mapp(_registry_status))
        _registry_status = ([]);

    version = intp(_registry_status["config_version"]) ? _registry_status["config_version"] : 0;
    _registry_status["config_name"] = "crafting";
    _registry_status["config_version"] = version + 1;
    _registry_status["last_attempt_at"] = time();
    _registry_status["last_reload_at"] = time();
    _registry_status["last_reload_ok"] = 1;
    _registry_status["last_error"] = "";
}

void reload_registry()
{
    mapping config;
    mapping configured_recipes;
    mapping configured_rules;
    mapping previous_rules;
    mapping previous_recipes;
    mapping validated_rules;
    mapping validated_recipes;
    mapping rule;
    mapping recipe;
    string item_type;
    string recipe_id;

    config = query_crafting_config();
    previous_recipes = mapp(_recipes) ? copy(_recipes) : ([]);
    previous_rules = mapp(_enhance_rules) ? copy(_enhance_rules) : ([]);

    configured_recipes = config["recipes"];
    configured_rules = config["enhance_rules"];
    if (!mapp(configured_recipes) || !mapp(configured_rules)) {
        mark_reload_failure("missing crafting definitions");
        _recipes = previous_recipes;
        _enhance_rules = previous_rules;
        return;
    }

    validated_recipes = ([]);
    foreach (recipe_id, recipe in configured_recipes) {
        if (!stringp(recipe_id) || recipe_id == "")
            continue;
        if (!validate_recipe(recipe))
            continue;
        validated_recipes[recipe_id] = copy(recipe);
    }

    validated_rules = ([]);
    foreach (item_type, rule in configured_rules) {
        if (!stringp(item_type) || item_type == "")
            continue;
        if (!validate_enhance_rule(rule))
            continue;
        validated_rules[item_type] = copy(rule);
    }

    if ((sizeof(configured_recipes) > 0 && !sizeof(validated_recipes))
        || (sizeof(configured_rules) > 0 && !sizeof(validated_rules))) {
        mark_reload_failure("all crafting definitions invalid");
        _recipes = previous_recipes;
        _enhance_rules = previous_rules;
        return;
    }

    _recipes = validated_recipes;
    _enhance_rules = validated_rules;
    mark_reload_success();
}

void on_config_reloaded(string key, string config_name)
{
    if (key != "crafting" && config_name != "crafting")
        return;

    reload_registry();
}

void on_config_reload_rollback(string key, string config_name)
{
    if (key != "crafting" && config_name != "crafting")
        return;

    reload_registry();
}

void create()
{
    ::create();

    if (!mapp(_recipes))
        _recipes = ([]);
    if (!mapp(_enhance_rules))
        _enhance_rules = ([]);
    if (!mapp(_registry_status)) {
        _registry_status = ([
            "config_name": "crafting",
            "config_version": 0,
            "last_attempt_at": 0,
            "last_reload_at": 0,
            "last_reload_ok": 0,
            "last_error": "",
        ]);
    }

    reload_registry();
    CONFIG_D->compat_register_config_callback("crafting", "on_config_reloaded", this_object());
}

mapping query_recipe(string scope, string recipe_id)
{
    mapping recipe;

    if (!stringp(recipe_id) || recipe_id == "")
        return 0;

    recipe = _recipes[recipe_id];
    if (!mapp(recipe))
        return 0;
    if (stringp(scope) && scope != "" && recipe["scope"] != scope)
        return 0;
    if (!validate_recipe(recipe))
        return 0;

    return copy(recipe);
}

mapping query_enhance_rule(string scope, string item_type)
{
    mapping rule;

    if (!stringp(item_type) || item_type == "")
        return 0;

    rule = _enhance_rules[item_type];
    if (!mapp(rule))
        return 0;
    if (stringp(scope) && scope != "" && rule["scope"] != scope)
        return 0;
    if (!validate_enhance_rule(rule))
        return 0;

    return copy(rule);
}

mapping query_registry_snapshot()
{
    return ([
        "config_name": _registry_status["config_name"] || "crafting",
        "config_version": intp(_registry_status["config_version"]) ? _registry_status["config_version"] : 0,
        "last_attempt_at": intp(_registry_status["last_attempt_at"]) ? _registry_status["last_attempt_at"] : 0,
        "last_reload_at": intp(_registry_status["last_reload_at"]) ? _registry_status["last_reload_at"] : 0,
        "last_reload_ok": _registry_status["last_reload_ok"] ? 1 : 0,
        "last_error": stringp(_registry_status["last_error"]) ? _registry_status["last_error"] : "",
        "recipe_count": sizeof(_recipes || ([])),
        "rule_count": sizeof(_enhance_rules || ([])),
        "recipe_ids": sort_array(keys(_recipes || ([])), 1),
        "rule_ids": sort_array(keys(_enhance_rules || ([])), 1),
        "recipes": copy(_recipes || ([])),
        "enhance_rules": copy(_enhance_rules || ([])),
    ]);
}
