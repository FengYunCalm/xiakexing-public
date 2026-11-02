/**
 * @file    dungeon_registry_d.c
 * @brief   副本定义注册表真源
 *
 * @note
 * 配置中的副本声明事实由这里统一缓存和校验。
 * DUNGEON_D 只消费这里输出的定义，不再自己持有模板真相。
 */

#pragma strict_types
#pragma save_types

#include <game_constants.h>
#include <system/constants.h>
#include <system/services.h>

inherit "/lib/framework/daemon";

nosave mapping _dungeon_defs;
nosave mapping _registry_status;

private mapping query_dungeon_config()
{
    mapping config;

    config = CONFIG_D->query_full_config("dungeon");
    return mapp(config) ? config : ([]);
}

private int validate_reward_entry(mapping reward)
{
    if (!mapp(reward))
        return 0;
    if (!stringp(reward["template_id"]) || reward["template_id"] == "")
        return 0;
    if (!intp(reward["count"]) || reward["count"] <= 0)
        return 0;

    return 1;
}

private int validate_dungeon_definition(mapping def)
{
    mapping templates;
    mixed reward;

    if (!mapp(def))
        return 0;
    if (!stringp(def["name"]) || def["name"] == "")
        return 0;
    if (!stringp(def["state_scope"]) || def["state_scope"] == "")
        return 0;
    if (!stringp(def["instance_scope"]) || def["instance_scope"] == "")
        return 0;
    if (!intp(def["max_players"]) || def["max_players"] <= 0
        || def["max_players"] > MAX_TEAM_SIZE)
        return 0;
    if (!stringp(def["public_entry_room"]) || def["public_entry_room"] == "")
        return 0;
    if (!stringp(def["instance_entry_room"]) || def["instance_entry_room"] == "")
        return 0;
    if (!stringp(def["entry_room"]) || def["entry_room"] == "")
        return 0;
    if (!stringp(def["exit_room"]) || def["exit_room"] == "")
        return 0;
    if (!stringp(def["treasury_room"]) || def["treasury_room"] == "")
        return 0;
    if (!intp(def["guild_contribution_reward"]) || def["guild_contribution_reward"] < 0)
        return 0;
    if (!intp(def["duration"]) || def["duration"] <= 0)
        return 0;
    if (!undefinedp(def["cooldown_enabled"]) && !intp(def["cooldown_enabled"]))
        return 0;
    if (!undefinedp(def["cooldown_seconds"])
        && (!intp(def["cooldown_seconds"]) || def["cooldown_seconds"] < 0))
        return 0;

    templates = def["instance_templates"];
    if (!mapp(templates) || !sizeof(templates))
        return 0;
    if (member_array(def["instance_entry_room"], values(templates)) == -1)
        return 0;
    if (member_array(def["treasury_room"], values(templates)) == -1)
        return 0;

    if (!arrayp(def["treasury_rewards"]))
        return 0;
    foreach (reward in def["treasury_rewards"]) {
        if (!validate_reward_entry(reward))
            return 0;
    }

    return 1;
}

private void mark_reload_failure(string error_message)
{
    if (!mapp(_registry_status))
        _registry_status = ([]);

    _registry_status["config_name"] = "dungeon";
    _registry_status["last_attempt_at"] = time();
    _registry_status["last_reload_ok"] = 0;
    _registry_status["last_error"] = stringp(error_message) ? error_message : "unknown";
}

private void mark_reload_success(mapping validated_defs)
{
    int version;

    if (!mapp(_registry_status))
        _registry_status = ([]);

    version = intp(_registry_status["config_version"]) ? _registry_status["config_version"] : 0;
    _registry_status["config_name"] = "dungeon";
    _registry_status["config_version"] = version + 1;
    _registry_status["last_attempt_at"] = time();
    _registry_status["last_reload_at"] = time();
    _registry_status["last_reload_ok"] = 1;
    _registry_status["last_error"] = "";
}

void reload_registry()
{
    mapping config;
    mapping configured_defs;
    mapping previous_defs;
    mapping validated_defs;
    string dungeon_type;
    mapping def;

    config = query_dungeon_config();
    previous_defs = mapp(_dungeon_defs) ? copy(_dungeon_defs) : ([]);
    configured_defs = config["dungeons"];
    if (!mapp(configured_defs)) {
        log_file("system/dungeon", sprintf("[%s] reload_registry: missing dungeon definitions\n", ctime(time())[4..19]));
        mark_reload_failure("missing dungeon definitions");
        _dungeon_defs = previous_defs;
        return;
    }

    validated_defs = ([]);
    foreach (dungeon_type, def in configured_defs) {
        if (!stringp(dungeon_type) || dungeon_type == "")
            continue;
        if (!validate_dungeon_definition(def))
            continue;
        validated_defs[dungeon_type] = copy(def);
    }

    if (sizeof(configured_defs) > 0 && !sizeof(validated_defs)) {
        log_file("system/dungeon", sprintf("[%s] reload_registry: all dungeon definitions invalid\n", ctime(time())[4..19]));
        mark_reload_failure("all dungeon definitions invalid");
        _dungeon_defs = previous_defs;
        return;
    }

    _dungeon_defs = validated_defs;
    mark_reload_success(validated_defs);
}

void on_config_reloaded(string key, string config_name)
{
    if (key != "dungeon" && config_name != "dungeon")
        return;

    reload_registry();
}

void on_config_reload_rollback(string key, string config_name)
{
    if (key != "dungeon" && config_name != "dungeon")
        return;

    reload_registry();
}

void create()
{
    ::create();

    if (!mapp(_dungeon_defs))
        _dungeon_defs = ([]);
    if (!mapp(_registry_status))
        _registry_status = ([
            "config_name": "dungeon",
            "config_version": 0,
            "last_attempt_at": 0,
            "last_reload_at": 0,
            "last_reload_ok": 0,
            "last_error": "",
        ]);

    reload_registry();
    CONFIG_D->compat_register_config_callback("dungeon", "on_config_reloaded", this_object());
}

mapping query_dungeon_definition(string dungeon_type)
{
    if (!stringp(dungeon_type) || dungeon_type == "")
        return 0;

    return mapp(_dungeon_defs[dungeon_type]) ? copy(_dungeon_defs[dungeon_type]) : 0;
}

int query_default_cooldown_enabled()
{
    mapping config;
    mapping defaults;

    config = query_dungeon_config();
    defaults = config["defaults"];
    if (mapp(defaults) && !undefinedp(defaults["cooldown_enabled"])
        && intp(defaults["cooldown_enabled"]))
        return defaults["cooldown_enabled"] ? 1 : 0;
    return 1;
}

int query_default_cooldown_seconds()
{
    mapping config;
    mapping defaults;

    config = query_dungeon_config();
    defaults = config["defaults"];
    if (mapp(defaults) && !undefinedp(defaults["cooldown_seconds"])
        && intp(defaults["cooldown_seconds"])
        && defaults["cooldown_seconds"] >= 0)
        return defaults["cooldown_seconds"];
    return 600;
}

int query_dungeon_cooldown_enabled(string dungeon_type)
{
    mapping definition;

    definition = query_dungeon_definition(dungeon_type);
    if (mapp(definition) && !undefinedp(definition["cooldown_enabled"])
        && intp(definition["cooldown_enabled"]))
        return definition["cooldown_enabled"] ? 1 : 0;
    return query_default_cooldown_enabled();
}

int query_dungeon_cooldown_seconds(string dungeon_type)
{
    mapping definition;

    definition = query_dungeon_definition(dungeon_type);
    if (mapp(definition) && !undefinedp(definition["cooldown_seconds"])
        && intp(definition["cooldown_seconds"])
        && definition["cooldown_seconds"] >= 0)
        return definition["cooldown_seconds"];
    return query_default_cooldown_seconds();
}

string *query_all_dungeon_types()
{
    return keys(_dungeon_defs || ([]));
}

mapping query_registry_snapshot()
{
    return ([
        "config_name": _registry_status["config_name"] || "dungeon",
        "config_version": intp(_registry_status["config_version"]) ? _registry_status["config_version"] : 0,
        "last_attempt_at": intp(_registry_status["last_attempt_at"]) ? _registry_status["last_attempt_at"] : 0,
        "last_reload_at": intp(_registry_status["last_reload_at"]) ? _registry_status["last_reload_at"] : 0,
        "last_reload_ok": _registry_status["last_reload_ok"] ? 1 : 0,
        "last_error": stringp(_registry_status["last_error"]) ? _registry_status["last_error"] : "",
        "loaded_count": sizeof(_dungeon_defs || ([])),
        "loaded_types": sort_array(keys(_dungeon_defs || ([])), 1),
        "definitions": copy(_dungeon_defs || ([])),
    ]);
}
