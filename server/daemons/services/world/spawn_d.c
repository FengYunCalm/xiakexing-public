/**
 * @file    spawn_d.c
 * @brief   刷怪守护进程,管理刷怪档案与实体补刷。
 */

#pragma strict_types
#pragma save_types

#include <system/globals.h>
#include <system/event_names.h>

inherit "/lib/framework/daemon";

nosave mapping _spawn_profiles;

private string *coerce_string_array(mixed value)
{
    string text;
    string *parts;

    if (arrayp(value))
        return filter(value, (: stringp($1) && $1 != "" :));
    if (!stringp(value) || value == "")
        return ({ });

    text = trim(replace_string(value, "，", ","));
    if (strlen(text) >= 2 && text[0] == '[' && text[<1] == ']')
        text = trim(text[1..<2]);
    if (text == "")
        return ({ });

    parts = explode(text, ",");
    parts = map(parts, (: trim($1) :));
    return filter(parts, (: stringp($1) && $1 != "" :));
}

private int coerce_bool(mixed value, int default_value)
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

private mapping build_default_profiles()
{
    return ([ ]);
}

private void apply_config_profiles(mapping config)
{
    string section_name;
    mapping section;
    string room_path;
    mapping profile;

    if (!mapp(config))
        return;

    foreach (section_name, section in config) {
        if (!stringp(section_name) || !mapp(section) || strsrch(section_name, "spawn_") != 0)
            continue;

        room_path = section["room"];
        if (!stringp(room_path) || room_path == "")
            continue;

        profile = copy(section);
        profile["id"] = section_name;
        profile["room"] = room_path;
        profile["tags"] = coerce_string_array(profile["tags"]);
        profile["enabled"] = coerce_bool(profile["enabled"], 1);
        _spawn_profiles[room_path] = profile;
    }
}

void reload_spawn_profiles()
{
    mapping config;

    _spawn_profiles = build_default_profiles();
    config = CONFIG_D->query_full_config("world_spawn");
    apply_config_profiles(config);
}

void create()
{
    ::create();
    CONFIG_D->compat_register_config_callback("world_spawn", "on_config_reloaded", this_object());
    reload_spawn_profiles();
}

void on_config_reloaded(string key, string config_name)
{
    if (config_name == "world_spawn")
        reload_spawn_profiles();
}

void on_config_reload_rollback(string key, string config_name)
{
    if (config_name == "world_spawn")
        reload_spawn_profiles();
}

mapping query_room_spawn_profile(mixed room_or_path)
{
    string room_path;

    if (objectp(room_or_path))
        room_path = base_name(room_or_path);
    else if (stringp(room_or_path))
        room_path = room_or_path;
    else
        return 0;

    if (!mapp(_spawn_profiles[room_path]))
        return 0;
    return copy(_spawn_profiles[room_path]);
}

mapping query_all_spawn_profiles()
{
    return copy(_spawn_profiles || ([]));
}

int emit_room_spawn_request(mixed room_or_path, string reason)
{
    mapping profile;

    profile = query_room_spawn_profile(room_or_path);
    if (!mapp(profile) || !profile["enabled"])
        return 0;

    EVENT_D->emit(EVENT_WORLD_SPAWN_REQUESTED, ([
        "room": profile["room"],
        "profile_id": profile["id"],
        "tags": profile["tags"],
        "reason": stringp(reason) ? reason : "manual",
        "timestamp": time()
    ]));
    return 1;
}

int emit_room_spawn_complete(mixed room_or_path, string reason)
{
    mapping profile;

    profile = query_room_spawn_profile(room_or_path);
    if (!mapp(profile) || !profile["enabled"])
        return 0;

    EVENT_D->emit(EVENT_WORLD_SPAWN_COMPLETED, ([
        "room": profile["room"],
        "profile_id": profile["id"],
        "reason": stringp(reason) ? reason : "manual",
        "timestamp": time()
    ]));
    return 1;
}
