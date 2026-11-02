/**
 * @file    npc_behavior_d.c
 * @brief   NPC 行为守护进程,管理 NPC 行为档案。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

nosave mapping _profiles;

private int coerce_flag(mixed value, int default_value)
{
    string text;

    if (intp(value))
        return value != 0;
    if (!stringp(value) || value == "")
        return default_value;

    text = lower_case(value);
    if (member_array(text, ({ "1", "true", "yes", "on" })) != -1)
        return 1;
    if (member_array(text, ({ "0", "false", "no", "off" })) != -1)
        return 0;
    return default_value;
}

private int is_flag_key(string key)
{
    if (!stringp(key) || key == "")
        return 0;
    return strsrch(key, "can_") == 0 || strsrch(key, "supports_") == 0;
}

private mapping build_default_profiles()
{
    return ([
        "vendor_basic": ([
            "archetype": "vendor",
            "can_trade": 1,
            "can_idle_chat": 1,
            "supports_list_hook": 1,
            "supports_quick_cmds": 1
        ]),
        "quest_giver_basic": ([
            "archetype": "quest_giver",
            "can_issue_quest": 1,
            "can_explain_quest": 1,
            "can_idle_chat": 1,
            "supports_quest_hook": 1
        ]),
        "guide_basic": ([
            "archetype": "guide",
            "can_give_guidance": 1,
            "can_idle_chat": 1,
            "supports_talk_hook": 1
        ]),
        "guard_basic": ([
            "archetype": "guard",
            "can_guard": 1,
            "can_warn": 1,
            "can_idle_chat": 1,
            "supports_talk_hook": 1
        ]),
        "sparring_basic": ([
            "archetype": "sparring",
            "can_idle_chat": 1,
            "supports_talk_hook": 1,
            "can_attack": 1
        ]),
        "ambient_basic": ([
            "archetype": "ambient",
            "can_idle_chat": 1,
            "can_wander": 0
        ]),
        "wild_enemy_basic": ([
            "archetype": "wild_enemy",
            "can_attack": 1,
            "can_respawn": 1,
            "can_idle_chat": 0
        ])
    ]);
}

private void apply_config_profiles(mapping config)
{
    string section_name;
    mapping section;
    string profile_id;
    mapping profile;

    if (!mapp(config))
        return;

    foreach (section_name, section in config) {
        if (!stringp(section_name) || !mapp(section) || strsrch(section_name, "profile_") != 0)
            continue;

        profile_id = section_name[8..];
        if (!stringp(profile_id) || profile_id == "")
            continue;

        profile = copy(section);
        foreach (string key in keys(profile)) {
            // 只有布尔语义的键才强制成 0/1：原实现把所有键（含 archetype 文本）
            // 都送进 coerce_flag，还把原文当 int 默认值，结果非布尔键被静默写成 0。
            if (is_flag_key(key))
                profile[key] = coerce_flag(profile[key], 0);
        }
        _profiles[profile_id] = profile;
    }
}

void reload_behavior_profiles()
{
    mapping config;

    _profiles = build_default_profiles();
    config = CONFIG_D->query_full_config("npc_behaviors");
    apply_config_profiles(config);
}

void create()
{
    ::create();
    CONFIG_D->compat_register_config_callback("npc_behaviors", "on_config_reloaded", this_object());
    reload_behavior_profiles();
}

void on_config_reloaded(string key, string config_name)
{
    if (config_name == "npc_behaviors")
        reload_behavior_profiles();
}

void on_config_reload_rollback(string key, string config_name)
{
    if (config_name == "npc_behaviors")
        reload_behavior_profiles();
}

int register_behavior_profile(string profile_id, mapping profile)
{
    if (!stringp(profile_id) || profile_id == "" || !mapp(profile))
        return 0;
    _profiles[profile_id] = copy(profile);
    return 1;
}

mapping query_behavior_profile(string profile_id)
{
    if (!stringp(profile_id) || !mapp(_profiles[profile_id]))
        return 0;
    return copy(_profiles[profile_id]);
}

string *query_all_behavior_profiles()
{
    return keys(_profiles || ([]));
}

int behavior_flag_enabled(string profile_id, string key)
{
    mapping profile;

    profile = query_behavior_profile(profile_id);
    if (!mapp(profile) || !stringp(key) || key == "")
        return 0;
    return profile[key] ? 1 : 0;
}

mapping query_effective_behavior(object npc)
{
    mapping profile;
    mapping overrides;
    string profile_id;

    if (!objectp(npc))
        return ([]);

    profile_id = stringp(npc->query_behavior_profile()) ? npc->query_behavior_profile() : "";
    profile = query_behavior_profile(profile_id);
    if (!mapp(profile))
        profile = ([]);

    overrides = npc->query("behavior_overrides");
    if (mapp(overrides))
        profile += overrides;

    if (stringp(npc->query_npc_archetype()) && npc->query_npc_archetype() != "")
        profile["archetype"] = npc->query_npc_archetype();

    return profile;
}
