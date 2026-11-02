/**
 * @file    region_d.c
 * @brief   区域守护进程,管理区域注册表与区域元数据。
 */

#pragma strict_types
#pragma save_types

#include <system/globals.h>
#include <system/services.h>
#include <system/event_names.h>

inherit "/lib/framework/daemon";

nosave mapping _regions;
nosave int _region_generation;

private int coerce_int(mixed value, int default_value)
{
    int parsed;

    if (intp(value))
        return value;
    if (stringp(value) && sscanf(value, "%d", parsed) == 1)
        return parsed;
    return default_value;
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

private mapping build_default_regions()
{
    /* 配置文件是区域事实的唯一入口；缺失配置表示未就绪，不能回落到
     * 某个历史区域或 default scope。 */
    return ([ ]);
}

private mapping normalize_region(string region_id, mapping raw)
{
    mapping normalized;

    if (!stringp(region_id) || region_id == "" || !mapp(raw))
        return 0;

    normalized = copy(raw);
    normalized["id"] = region_id;
    if (!stringp(normalized["name"]) || normalized["name"] == "")
        normalized["name"] = region_id;
    if (!stringp(normalized["zone"]) || normalized["zone"] == "")
        normalized["zone"] = region_id;
    if (!stringp(normalized["root"]) || normalized["root"] == "")
        normalized["root"] = "/world/" + region_id + "/";
    if (!stringp(normalized["start_room"]) || normalized["start_room"] == "")
        normalized["start_room"] = stringp(normalized["entry_room"])
            && normalized["entry_room"] != "" ? normalized["entry_room"]
            : "";
    if (normalized["start_room"] == "")
        return 0;
    normalized["scope_key"] = "shared:" + region_id;
    normalized["personal_scope_prefix"] = "player:" + region_id + ":";
    normalized["region_generation"] = _region_generation;
    normalized["enabled"] = coerce_bool(normalized["enabled"], 1);
    normalized["priority"] = coerce_int(normalized["priority"], 0);
    return normalized;
}

private void apply_config_regions(mapping config)
{
    string section_name;
    mapping section;
    string region_id;

    if (!mapp(config))
        return;

    foreach (section_name, section in config) {
        if (!stringp(section_name) || !mapp(section) || strsrch(section_name, "region_") != 0)
            continue;

        region_id = section_name[7..];
        if (!stringp(region_id) || region_id == "")
            continue;

        _regions[region_id] = normalize_region(region_id, section);
    }
}

void reload_region_catalog()
{
    mapping config;

    _region_generation++;
    _regions = build_default_regions();
    config = CONFIG_D->query_full_config("world_regions");
    apply_config_regions(config);
}

void create()
{
    ::create();
    CONFIG_D->compat_register_config_callback("world_regions", "on_config_reloaded", this_object());
    reload_region_catalog();
}

void on_config_reloaded(string key, string config_name)
{
    if (config_name == "world_regions")
        reload_region_catalog();
}

void on_config_reload_rollback(string key, string config_name)
{
    if (config_name == "world_regions")
        reload_region_catalog();
}

int register_region(string region_id, mapping region_def)
{
    mapping normalized;

    _region_generation++;
    normalized = normalize_region(region_id, region_def);
    if (!mapp(normalized))
        return 0;

    _regions[region_id] = normalized;
    EVENT_D->emit(EVENT_WORLD_REGION_REGISTERED, ([
        "region_id": region_id,
        "root": normalized["root"],
        "timestamp": time()
    ]));
    return 1;
}

/* Synthetic fixture cleanup uses an exact region identity. Formal region
 * truth still comes from world_regions.cfg; this only removes an in-memory
 * test registration and never edits the canonical config. */
int unregister_region(string region_id)
{
    if (!stringp(region_id) || region_id == ""
        || !mapp(_regions[region_id]))
        return 1;
    _region_generation++;
    map_delete(_regions, region_id);
    return 1;
}

mapping query_region(string region_id)
{
    if (!stringp(region_id) || !mapp(_regions[region_id]))
        return 0;
    return copy(_regions[region_id]);
}

mapping query_all_regions()
{
    return copy(_regions || ([]));
}

int query_region_generation()
{
    return _region_generation;
}

string *query_all_region_ids()
{
    return keys(_regions || ([]));
}

string resolve_room_region(mixed room_or_path)
{
    string room_path;
    string zone;
    string region_id;
    mapping region;

    if (objectp(room_or_path)) {
        zone = room_or_path->query("zone");
        if (stringp(zone) && mapp(_regions[zone]))
            return zone;
        room_path = base_name(room_or_path);
    } else if (stringp(room_or_path)) {
        room_path = room_or_path;
    } else {
        return 0;
    }

    foreach (region_id, region in _regions) {
        if (!mapp(region) || !region["enabled"])
            continue;
        if (stringp(region["root"]) && strsrch(room_path, region["root"]) == 0)
            return region_id;
    }

    return 0;
}

mapping resolve_region_context(mixed room_or_path)
{
    mapping region;
    string region_id;

    region_id = resolve_room_region(room_or_path);
    region = stringp(region_id) ? query_region(region_id) : 0;
    if (!mapp(region) || !region["enabled"])
        return ([
            "ok": 0,
            "error_code": "REGION_UNRESOLVED",
            "room": objectp(room_or_path) ? file_name(room_or_path)
                : (stringp(room_or_path) ? room_or_path : ""),
        ]);
    return ([
        "ok": 1,
        "region_id": region_id,
        "scope_key": region["scope_key"],
        "personal_scope_prefix": region["personal_scope_prefix"],
        "region": region,
    ]);
}

mapping query_region_scope(string region_id)
{
    mapping region;

    region = query_region(region_id);
    if (!mapp(region) || !region["enabled"])
        return 0;
    return ([
        "region_id": region_id,
        "scope_key": region["scope_key"],
        "personal_scope_prefix": region["personal_scope_prefix"],
        "region_generation": region["region_generation"] || 0,
    ]);
}

mapping query_room_region(mixed room_or_path)
{
    string region_id;

    region_id = resolve_room_region(room_or_path);
    return region_id ? query_region(region_id) : 0;
}
