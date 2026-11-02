/**
 * @file    skill_unlock_registry_d.c
 * @brief   技能解锁规则注册表,从 skill_catalog 配置加载解锁条件。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

nosave mapping _unlock_rules;

private mapping query_unlock_config()
{
    mapping config;

    config = CONFIG_D->query_full_config("skill_catalog");
    return mapp(config) ? config : ([]);
}

private void load_unlock_config()
{
    mapping config;

    config = query_unlock_config();
    _unlock_rules = mapp(config["unlock_rules"]) ? copy(config["unlock_rules"]) : ([]);
}

void on_config_reloaded(string key, string config_name)
{
    if (key != "skill_catalog" && config_name != "skill_catalog")
        return;

    load_unlock_config();
}

void on_config_reload_rollback(string key, string config_name)
{
    if (key != "skill_catalog" && config_name != "skill_catalog")
        return;

    load_unlock_config();
}

void create()
{
    ::create();
    if (!mapp(_unlock_rules))
        _unlock_rules = ([]);
    load_unlock_config();
    CONFIG_D->compat_register_config_callback("skill_catalog", "on_config_reloaded", this_object());
}

mapping query_unlock_rule(string unlock_type, string skill_id)
{
    mapping by_type;

    if (!stringp(unlock_type) || unlock_type == "" || !stringp(skill_id) || skill_id == "")
        return 0;

    by_type = _unlock_rules[unlock_type];
    return mapp(by_type) && mapp(by_type[skill_id]) ? copy(by_type[skill_id]) : 0;
}
