/**
 * @file    combat_loot_registry_d.c
 * @brief   战斗掉落配置注册表,从 loot 配置加载掉落表与注册状态。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

private nosave mapping _loot_tables = ([]);
private nosave mapping _registry_status = ([]);

private void mark_reload_failure(string error_message);

private mapping query_loot_config()
{
    mapping config;

    /* 唯一加载入口是 CONFIG_D（compat_register_config_callback 已注册
     * reload 通知，on_config_reloaded 只消费 CONFIG_D cache，不再直载对象）。 */
    config = CONFIG_D->query_full_config("combat_loot");
    if (!mapp(config)) {
        mark_reload_failure("config missing in CONFIG_D");
        return ([]);
    }
    return config;
}

private int validate_reward_amount(mapping loot_table, string fixed_key, string min_key,
    string range_key)
{
    int fixed_amount;
    int min_amount;
    int range_amount;

    if (!mapp(loot_table))
        return 0;

    fixed_amount = loot_table[fixed_key];
    if (intp(fixed_amount) && fixed_amount < 0)
        return 0;

    min_amount = loot_table[min_key];
    if (intp(min_amount) && min_amount < 0)
        return 0;

    range_amount = loot_table[range_key];
    if (intp(range_amount) && range_amount < 0)
        return 0;

    return 1;
}

private int validate_loot_item_entry(mixed item_entry)
{
    int chance;
    int count;
    string template_id;

    if (stringp(item_entry))
        template_id = item_entry;
    else if (mapp(item_entry))
        template_id = item_entry["template_id"] || item_entry["item"] || item_entry["id"] || "";
    else
        return 0;

    if (!stringp(template_id) || template_id == "")
        return 0;
    if (!ITEM_D->item_exists(template_id))
        return 0;

    if (mapp(item_entry)) {
        count = item_entry["count"];
        if (intp(count) && count < 0)
            return 0;

        chance = item_entry["chance"];
        if (intp(chance) && (chance < 0 || chance > 100))
            return 0;
    }

    return 1;
}

private int validate_loot_table(mapping loot_table)
{
    mixed *items;
    mixed item_entry;
    int has_rewards;

    if (!mapp(loot_table))
        return 0;
    if (!validate_reward_amount(loot_table, "exp", "exp_min", "exp_range"))
        return 0;
    if (!validate_reward_amount(loot_table, "coin", "coin_min", "coin_range"))
        return 0;
    if (stringp(loot_table["item_mode"]) && loot_table["item_mode"] != ""
        && member_array(loot_table["item_mode"], ({ "one", "all" })) == -1)
        return 0;

    has_rewards = 0;
    if ((loot_table["exp"] || 0) > 0 || (loot_table["exp_min"] || 0) > 0)
        has_rewards = 1;
    if ((loot_table["coin"] || 0) > 0 || (loot_table["coin_min"] || 0) > 0)
        has_rewards = 1;

    if (loot_table["item"]) {
        if (!validate_loot_item_entry(loot_table["item"]))
            return 0;
        has_rewards = 1;
    }

    items = loot_table["items"];
    if (arrayp(items)) {
        foreach (item_entry in items) {
            if (!validate_loot_item_entry(item_entry))
                return 0;
        }
        if (sizeof(items) > 0)
            has_rewards = 1;
    }

    return has_rewards;
}

private void mark_reload_failure(string error_message)
{
    if (!mapp(_registry_status))
        _registry_status = ([]);

    _registry_status["config_name"] = "combat_loot";
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
    _registry_status["config_name"] = "combat_loot";
    _registry_status["config_version"] = version + 1;
    _registry_status["last_attempt_at"] = time();
    _registry_status["last_reload_at"] = time();
    _registry_status["last_reload_ok"] = 1;
    _registry_status["last_error"] = "";
}

void reload_registry()
{
    mapping config;
    mapping configured_tables;
    mapping previous_tables;
    mapping validated_tables;
    string loot_table_id;
    mapping loot_table;

    config = query_loot_config();
    previous_tables = mapp(_loot_tables) ? copy(_loot_tables) : ([]);
    configured_tables = config["loot_tables"];
    if (!mapp(configured_tables)) {
        log_file("system/combat_loot", sprintf("[%s] reload_registry: missing loot table definitions\n",
            ctime(time())[4..19]));
        mark_reload_failure("missing loot table definitions");
        _loot_tables = previous_tables;
        return;
    }

    validated_tables = ([]);
    foreach (loot_table_id, loot_table in configured_tables) {
        if (!stringp(loot_table_id) || loot_table_id == "")
            continue;
        if (!validate_loot_table(loot_table)) {
            log_file("system/combat_loot", sprintf("[%s] invalid loot table: %s\n",
                ctime(time())[4..19], loot_table_id));
            continue;
        }
        validated_tables[loot_table_id] = copy(loot_table);
    }

    if (sizeof(configured_tables) > 0 && !sizeof(validated_tables)) {
        log_file("system/combat_loot", sprintf("[%s] reload_registry: all loot table definitions invalid\n",
            ctime(time())[4..19]));
        mark_reload_failure("all loot table definitions invalid");
        _loot_tables = previous_tables;
        return;
    }

    _loot_tables = validated_tables;
    mark_reload_success();
}

void on_config_reloaded(string key, string config_name)
{
    if (key != "combat_loot")
        return;

    reload_registry();
}

void on_config_reload_rollback(string key, string config_name)
{
    if (key != "combat_loot")
        return;

    reload_registry();
}

void create()
{
    ::create();
    log_file("system/combat_loot", sprintf("[%s] registry CREATE v4\n", ctime(time())[4..19]));
    if (!mapp(_loot_tables))
        _loot_tables = ([]);
    if (!mapp(_registry_status)) {
        _registry_status = ([
            "config_name": "combat_loot",
            "config_version": 0,
            "last_attempt_at": 0,
            "last_reload_at": 0,
            "last_reload_ok": 0,
            "last_error": "",
        ]);
    }

    reload_registry();
    CONFIG_D->compat_register_config_callback("combat_loot", "on_config_reloaded", this_object());
}

mapping query_loot_table(string loot_table_id)
{
    if (!stringp(loot_table_id) || loot_table_id == "")
        return 0;

    return mapp(_loot_tables[loot_table_id]) ? copy(_loot_tables[loot_table_id]) : 0;
}

int loot_table_exists(string loot_table_id)
{
    if (!stringp(loot_table_id) || loot_table_id == "")
        return 0;

    return mapp(_loot_tables[loot_table_id]);
}

mapping query_registry_snapshot()
{
    return ([
        "config_name": _registry_status["config_name"] || "combat_loot",
        "config_version": intp(_registry_status["config_version"]) ? _registry_status["config_version"] : 0,
        "last_attempt_at": intp(_registry_status["last_attempt_at"]) ? _registry_status["last_attempt_at"] : 0,
        "last_reload_at": intp(_registry_status["last_reload_at"]) ? _registry_status["last_reload_at"] : 0,
        "last_reload_ok": _registry_status["last_reload_ok"] ? 1 : 0,
        "last_error": stringp(_registry_status["last_error"]) ? _registry_status["last_error"] : "",
        "loaded_count": sizeof(_loot_tables || ([])),
        "loaded_types": sort_array(keys(_loot_tables || ([])), 1),
        "definitions": copy(_loot_tables || ([])),
    ]);
}
