/**
 * @file    content_registry_d.c
 * @brief   内容定义注册与校验服务



 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

#define MAP_INDOOR_QUEST_TOPOLOGY_CACHE_LIMIT 1024
#define MAP_QUEST_TOPOLOGY_MAX_STEPS 64
#define MAP_QUEST_TOPOLOGY_MAX_ROOMS 512

nosave private mapping quest_provider_room_index = 0;
nosave private mapping map_indoor_quest_topology_cache = ([]);
nosave private mapping content_rooms_index = 0;
nosave private mapping content_descriptions_index = 0;
nosave private mapping loaded_content_rooms = ([]);
nosave private int content_generation = 1;
nosave private int content_degraded;
nosave private int content_validation_depth;
nosave private mapping last_reload_failure;
nosave private int reload_generation_before;
nosave private int reload_degraded_before;
nosave private mapping reload_failure_before;

void reset_content_query_indexes();
private mapping query_content_room_descriptions();
private mapping query_content_config();
private mapping build_validation_report(string mode);
private int refresh_loaded_content_rooms(int generation);
varargs mapping query_validation_report(string mode);
mapping query_test_event_validation(mapping event);
int query_content_generation();

void create()
{
    ::create();
    quest_provider_room_index = 0;
    map_indoor_quest_topology_cache = ([]);
    content_rooms_index = 0;
    content_descriptions_index = 0;
    loaded_content_rooms = ([]);
    content_validation_depth = 0;
    content_generation = time();
    if (content_generation <= 0)
        content_generation = 1;
    content_degraded = 0;
    last_reload_failure = ([ ]);
    CONFIG_D->compat_register_config_callback("content", "on_config_reloaded", this_object());
}

void on_config_reloaded(string key, string config_name)
{
    mapping validation;

    if (key != "content" && config_name != "content")
        return;

    reload_generation_before = content_generation;
    reload_degraded_before = content_degraded;
    reload_failure_before = mapp(last_reload_failure) ? copy(last_reload_failure) : ([ ]);
    validation = query_validation_report();
    if (!mapp(validation) || validation["ok"] != 1) {
        content_degraded = 1;
        last_reload_failure = ([
            "ok": 0,
            "error_code": "CONTENT_VALIDATION_FAILED",
            "source": "/daemons/services/world/content_registry_d.c",
            "previous_generation": query_content_generation(),
            "validation": validation,
            "preserve_previous_generation": 1,
        ]);
        log_file("system/content",
            sprintf("[%s] content candidate rejected; preserve_previous_generation=1: %O\n",
                ctime(time())[4..19], last_reload_failure));
        error("CONTENT_VALIDATION_FAILED");
        return;
    }

    content_degraded = 0;
    last_reload_failure = ([ ]);
    content_generation++;
    if (content_generation <= 0)
        content_generation = 1;
    reset_content_query_indexes();
    if (!refresh_loaded_content_rooms(content_generation)) {
        content_degraded = 1;
        last_reload_failure = ([
            "ok": 0,
            "error_code": "CONTENT_ROOM_REFRESH_FAILED",
            "source": "/daemons/services/world/content_registry_d.c",
            "generation": content_generation,
            "preserve_previous_generation": 1,
        ]);
        log_file("system/content", sprintf(
            "[%s] loaded content room refresh failed: %O\n",
            ctime(time())[4..19], last_reload_failure));
        error("CONTENT_ROOM_REFRESH_FAILED");
        return;
    }
}

void on_config_reload_rollback(string key, string config_name)
{
    if (key != "content" && config_name != "content")
        return;
    content_generation = reload_generation_before > 0
        ? reload_generation_before : content_generation;
    content_degraded = reload_degraded_before;
    last_reload_failure = mapp(reload_failure_before)
        ? copy(reload_failure_before) : ([ ]);
    reset_content_query_indexes();
    if (!refresh_loaded_content_rooms(content_generation)) {
        last_reload_failure = ([
            "ok": 0,
            "error_code": "CONTENT_ROOM_ROLLBACK_FAILED",
            "source": "/daemons/services/world/content_registry_d.c",
            "generation": content_generation,
        ]);
        log_file("system/content", sprintf(
            "[%s] loaded content room rollback failed: %O\n",
            ctime(time())[4..19], last_reload_failure));
        error("CONTENT_ROOM_ROLLBACK_FAILED");
    }
}

int query_content_generation()
{
    return content_generation > 0 ? content_generation : 1;
}

void register_loaded_content_room(object room)
{
    string room_path;

    if (!objectp(room))
        return;
    room_path = base_name(room);
    if (!stringp(room_path) || room_path == "")
        return;
    loaded_content_rooms[room_path] = room;
}

private int refresh_loaded_content_rooms(int generation)
{
    object room;
    string room_path;
    int ok;

    ok = 1;
    foreach (room_path, room in loaded_content_rooms) {
        if (!objectp(room)) {
            map_delete(loaded_content_rooms, room_path);
            continue;
        }
        if (!function_exists("refresh_content_room", room)) {
            ok = 0;
            log_file("system/content", sprintf(
                "[%s] loaded room lacks content refresh hook: %s\n",
                ctime(time())[4..19], room_path));
            continue;
        }
        if (!call_other(room, "refresh_content_room", generation)) {
            ok = 0;
            log_file("system/content", sprintf(
                "[%s] loaded room content refresh rejected: %s generation=%d\n",
                ctime(time())[4..19], room_path, generation));
        }
    }
    return ok;
}

int is_content_validation_active()
{
    return content_validation_depth > 0;
}

mapping query_content_health()
{
    mapping config;
    mapping validation;
    mixed *load_errors;
    mixed *ownership_errors;
    mixed *validation_errors;
    mixed *duplicate_ids;
    mixed *entity_list;
    mixed issue;
    string room_path;
    int entity_count;

    config = query_content_config();
    validation = query_validation_report();
    load_errors = arrayp(config["load_errors"]) ? config["load_errors"] : ({ });
    ownership_errors = arrayp(config["region_package_issues"])
        ? config["region_package_issues"] : ({ });
    validation_errors = arrayp(validation["violations"])
        ? validation["violations"] : ({ });
    duplicate_ids = ({ });
    foreach (issue in ownership_errors)
        if (mapp(issue) && stringp(issue["key"]) && issue["key"] != ""
            && member_array(issue["key"], duplicate_ids) < 0)
            duplicate_ids += ({ issue["key"] });
    entity_count = 0;
    if (mapp(config["room_entities"]))
        foreach (room_path, entity_list in config["room_entities"])
            if (arrayp(entity_list))
                entity_count += sizeof(entity_list);
    return ([
        "ok": sizeof(load_errors) == 0 && sizeof(ownership_errors) == 0
            && sizeof(validation_errors) == 0 ? 1 : 0,
        "generation": query_content_generation(),
        "package_count": arrayp(config["region_package_paths"])
            ? sizeof(config["region_package_paths"]) : 0,
        "room_count": mapp(config["rooms"]) ? sizeof(config["rooms"]) : 0,
        "entity_count": entity_count,
        "duplicate_ids": duplicate_ids,
        "load_errors": load_errors,
        "ownership_errors": ownership_errors,
        "validation_errors": validation_errors,
        "degraded": content_degraded,
        "last_reload_failure": last_reload_failure || ([ ]),
    ]);
}

private mixed query_content_section(string key)
{
    if (!stringp(key) || key == "")
        return 0;
    return CONFIG_D->query_config("content", key);
}

private mapping query_content_config()
{
    /* 与 content_config.c query_all_config 顶层键保持同步：新增 section 时
     * 必须两处同改，否则批量校验读恒空。 */
    return ([
        "region_package_paths": query_content_section("region_package_paths"),
        "areas": query_content_section("areas"),
        "rooms": query_content_section("rooms"),
        "room_entities": query_content_section("room_entities"),
        "room_descriptions": query_content_section("room_descriptions"),
        "npc_templates": query_content_section("npc_templates"),
        "item_templates": query_content_section("item_templates"),
        "quest_definitions": query_content_section("quest_definitions"),
        "combat_loot_tables": query_content_section("combat_loot_tables"),
        "skill_teachers": query_content_section("skill_teachers"),
        "resource_nodes": query_content_section("resource_nodes"),
        "shops": query_content_section("shops"),
        "reward_tables": query_content_section("reward_tables"),
        "reputation_sources": query_content_section("reputation_sources"),
        "exploration_chests": query_content_section("exploration_chests"),
        "world_events": query_content_section("world_events"),
        "map_metadata": query_content_section("map_metadata"),
        "room_behaviors": query_content_section("room_behaviors"),
        "allowed_action_groups": query_content_section("allowed_action_groups"),
        "region_package_issues": query_content_section("region_package_issues"),
        "load_errors": query_content_section("load_errors"),
        "validation_errors": query_content_section("validation_errors"),
    ]);
}

private mapping query_content_room_descriptions()
{
    mapping descriptions;

    if (!mapp(content_descriptions_index)) {
        descriptions = query_content_section("room_descriptions");
        content_descriptions_index = mapp(descriptions) ? descriptions : ([]);
    }
    return content_descriptions_index;
}

private mapping query_content_rooms()
{
    mapping rooms;

    if (!mapp(content_rooms_index)) {
        rooms = query_content_section("rooms");
        content_rooms_index = mapp(rooms) ? rooms : ([]);
    }
    return content_rooms_index;
}

private string normalize_path(string path)
{
    if (!stringp(path) || path == "")
        return "";
    if (strlen(path) > 2 && path[<2..] == ".c")
        return path[0..<3];
    return path;
}

private string query_lpc_file(string path)
{
    path = normalize_path(path);
    if (path == "")
        return "";
    return path + ".c";
}

private string *coerce_string_array(mixed value)
{
    string *result;
    mixed entry;

    result = ({ });
    if (!arrayp(value))
        return result;
    foreach (entry in value) {
        if (stringp(entry) && entry != "")
            result += ({ entry });
    }
    return result;
}

private void add_violation(mapping report, string category, string message)
{
    mapping *violations;

    if (!mapp(report) || !stringp(message) || message == "")
        return;
    violations = arrayp(report["violations"]) ? report["violations"] : ({ });
    violations += ({ ([
        "category": stringp(category) && category != "" ? category : "general",
        "message": message,
        "source": "/daemons/services/world/content_registry_d.c",
        "error_code": "CONTENT_VALIDATION_FAILED",
    ]) });
    report["violations"] = violations;
}

/* 独立 warning 通道：不 gate 内容加载、不进 contentcheck 主列表，
 * 供人工核查（多师冲突等合法但需留意的配置状态）。 */
private void add_warning(mapping report, string category, string message)
{
    mapping *warnings;

    if (!mapp(report) || !stringp(message) || message == "")
        return;
    warnings = arrayp(report["warnings"]) ? report["warnings"] : ({ });
    warnings += ({ ([
        "category": stringp(category) && category != "" ? category : "general",
        "message": message,
    ]) });
    report["warnings"] = warnings;
}

private string describe_value(mixed value)
{
    return stringp(value) ? value : sprintf("%O", value);
}

private int is_nonempty_string(mixed value)
{
    return stringp(value) && value != "";
}

private string *append_unique_string(string *values, string value)
{
    if (!arrayp(values))
        values = ({ });
    value = normalize_path(value);
    if (value != "" && member_array(value, values) == -1)
        values += ({ value });
    return values;
}

private string *append_unique_strings(string *values, string *additions)
{
    string value;

    if (!arrayp(values))
        values = ({ });
    if (!arrayp(additions))
        return values;
    foreach (value in additions)
        values = append_unique_string(values, value);
    return values;
}

/* 房间级模板归属辅助：room_map[provider_id] = 数组追加去重 */
private mapping append_unique_mapping_string(mapping room_map, string provider_id,
    string template_id)
{
    if (!mapp(room_map))
        room_map = ([]);
    room_map[provider_id] = append_unique_string(
        arrayp(room_map[provider_id]) ? room_map[provider_id] : ({ }),
        template_id);
    return room_map;
}

private mapping copy_string_array_mapping(mapping source)
{
    mapping result;
    string key;
    string *values;

    result = ([]);
    if (!mapp(source))
        return result;

    foreach (key, values in source) {
        if (!stringp(key) || !arrayp(values))
            continue;
        result[key] = values + ({ });
    }
    return result;
}

mapping query_area(string area_id)
{
    mapping areas;
    mapping area;

    areas = query_content_section("areas");
    area = mapp(areas) && stringp(area_id) ? areas[area_id] : 0;
    return mapp(area) ? copy(area) : 0;
}

/* 房间长描述查询（单一 API：look 命令与协议正文共用）。
 * 无描述返回空串（非 content 房间/未配置区域）。 */
string query_room_description(string room_path)
{
    mapping descriptions;
    string desc;

    room_path = normalize_path(room_path);
    descriptions = query_content_room_descriptions();
    if (!mapp(descriptions) || !stringp(room_path))
        return "";
    desc = descriptions[room_path];
    return stringp(desc) ? desc : "";
}

mapping query_room(string room_path)
{
    mapping rooms;
    mapping room;

    room_path = normalize_path(room_path);
    rooms = query_content_rooms();
    room = mapp(rooms) && stringp(room_path) ? rooms[room_path] : 0;
    return mapp(room) ? copy(room) : 0;
}

mapping query_all_rooms()
{
    mapping rooms;

    rooms = query_content_rooms();
    return mapp(rooms) ? copy(rooms) : ([]);
}

mapping query_all_npc_templates()
{
    mapping templates;

    templates = query_content_section("npc_templates");
    return mapp(templates) ? copy(templates) : ([]);
}

mapping query_room_for_actor(object actor, string room_path)
{
    mapping room;

    room = query_room(room_path);
    if (!mapp(room))
        return 0;
    return CONTENT_MODE_POLICY_D->project_content(actor, "room", room);
}

mapping query_map_room_snapshot(string room_path)
{
    mapping rooms;
    mapping room;

    room_path = normalize_path(room_path);
    rooms = query_content_rooms();
    room = mapp(rooms) && room_path != "" ? rooms[room_path] : 0;
    if (!mapp(room))
        return 0;

    return ([
        "room_id": room_path,
        "title": stringp(room["title"]) ? room["title"] : "",
        "exits": mapp(room["exits"]) ? copy(room["exits"]) : 0,
        "region_id": stringp(room["region"]) ? room["region"] : "",
        "map_context_id": stringp(room["map_context_id"])
            ? room["map_context_id"] : "",
        "map_layer": stringp(room["map_layer"]) ? room["map_layer"] : "",
        "submap_id": stringp(room["submap_id"]) ? room["submap_id"] : "",
        "source": "content",
        "content_generation": query_content_generation(),
    ]);
}

mapping *query_room_entities(string room_path)
{
    mapping entities;
    mixed raw_entities;

    room_path = normalize_path(room_path);
    entities = query_content_section("room_entities");
    raw_entities = mapp(entities) ? entities[room_path] : 0;
    return arrayp(raw_entities) ? raw_entities + ({ }) : ({ });
}

mapping *query_room_entities_for_actor(object actor, string room_path)
{
    mapping *entities;
    mapping entity;
    mapping projected;
    mapping *result;
    string content_type;

    result = ({ });
    entities = query_room_entities(room_path);
    foreach (entity in entities) {
        if (!mapp(entity))
            continue;
        content_type = entity["content_type"] || entity["type"] || "npc";
        projected = CONTENT_MODE_POLICY_D->project_content(actor,
            content_type, entity);
        if (mapp(projected) && projected["visible"])
            result += ({ projected });
    }
    return result;
}

private string query_entity_provider_id(mapping entity, mapping npc_templates)
{
    mapping template;
    string provider_id;
    string template_id;

    if (!mapp(entity))
        return "";

    provider_id = stringp(entity["provider_id"]) ? entity["provider_id"] : "";
    if (provider_id != "")
        return provider_id;

    template_id = stringp(entity["template_id"]) ? entity["template_id"] : "";
    if (template_id == "")
        return "";
    template = mapp(npc_templates) ? npc_templates[template_id] : 0;
    return mapp(template) && stringp(template["provider_id"]) ? template["provider_id"] : "";
}

/* 室内子房投影根房的单一权威：含 __ 子房返回根房路径，否则返回空。
 * 地图格子、入口拓扑、日程在场谓词都以此规则把室内实体投影到根房标记。
 * 注意：spawn 条目必须携带 template_id（仅 provider_id 的条目会绕过
 * 日程过滤，见 npc_schedule_d::query_provider_present_in_room）。 */
public string query_indoor_root_room_path(string room_path)
{
    string *parts;

    if (!stringp(room_path) || strsrch(room_path, "__") == -1)
        return "";
    parts = explode(room_path, "__");
    return sizeof(parts) > 1 ? parts[0] : "";
}

/* L-37：provider 展示名取自当前内容身份（实体 name → 模板 name），
 * 供协议层读取，替代协议服务内嵌的旧 provider 姓名表。 */
private string query_entity_provider_name(mapping entity, mapping npc_templates)
{
    mapping template;
    string name;
    string template_id;

    if (!mapp(entity))
        return "";
    name = stringp(entity["name"]) ? entity["name"] : "";
    if (name != "")
        return name;
    template_id = stringp(entity["template_id"]) ? entity["template_id"] : "";
    if (template_id == "")
        return "";
    template = mapp(npc_templates) ? npc_templates[template_id] : 0;
    return mapp(template) && stringp(template["name"]) ? template["name"] : "";
}

private mapping build_quest_provider_room_index()
{
    mapping entities_by_room;
    mapping npc_templates;
    mapping provider_rooms;
    mapping room_providers;
    mapping room_projected_providers;
    mapping room_provider_templates;
    mapping provider_names;
    mixed raw_entities;
    mapping entity;
    mapping room_templates;
    string room_path;
    string normalized_room;
    string projected_room;
    string provider_id;
    string provider_name;
    string template_id;

    entities_by_room = query_content_section("room_entities");
    npc_templates = query_content_section("npc_templates");
    provider_rooms = ([]);
    room_providers = ([]);
    room_projected_providers = ([]);
    room_provider_templates = ([]);
    provider_names = ([]);
    if (!mapp(entities_by_room)) {
        return ([
            "provider_rooms": provider_rooms,
            "room_providers": room_providers,
            "room_projected_providers": room_projected_providers,
            "room_provider_templates": room_provider_templates,
            "provider_names": provider_names,
        ]);
    }

    foreach (room_path, raw_entities in entities_by_room) {
        normalized_room = normalize_path(room_path);
        if (normalized_room == "" || !arrayp(raw_entities))
            continue;
        room_templates = mapp(room_provider_templates[normalized_room])
            ? room_provider_templates[normalized_room] : ([]);
        foreach (entity in raw_entities) {
            provider_id = query_entity_provider_id(entity, npc_templates);
            if (provider_id == "")
                continue;
            provider_name = query_entity_provider_name(entity, npc_templates);
            if (provider_name != "" && !stringp(provider_names[provider_id]))
                provider_names[provider_id] = provider_name;
            /* 房间级模板归属：只有本房间实体的模板参与在场判定，
             * 避免跨房间模板并集造成假阳性 */
            template_id = stringp(entity["template_id"]) ? entity["template_id"] : "";
            if (template_id != "") {
                room_templates[provider_id] = append_unique_string(
                    room_templates[provider_id] || ({ }), template_id);
                if (strsrch(normalized_room, "__") != -1) {
                    /* 室内子房实体同时归属根房键：地图根房格子显示投影标记 */
                    projected_room = query_indoor_root_room_path(normalized_room);
                    if (projected_room != "") {
                        room_provider_templates[projected_room] =
                            append_unique_mapping_string(
                                room_provider_templates[projected_room] || ([]),
                                provider_id, template_id);
                    }
                }
            }
            provider_rooms[provider_id] = append_unique_string(provider_rooms[provider_id] || ({ }),
                normalized_room);
            room_providers[normalized_room] = append_unique_string(room_providers[normalized_room] || ({ }),
                provider_id);
            if (strsrch(normalized_room, "__") != -1) {
                projected_room = query_indoor_root_room_path(normalized_room);
                if (projected_room != "") {
                    room_projected_providers[projected_room] =
                        append_unique_string(room_projected_providers[projected_room] || ({ }),
                            provider_id);
                }
            }
        }
        room_provider_templates[normalized_room] = room_templates;
    }

    return ([
        "provider_rooms": provider_rooms,
        "room_providers": room_providers,
        "room_projected_providers": room_projected_providers,
        "room_provider_templates": room_provider_templates,
        "provider_names": provider_names,
    ]);
}

private mapping query_quest_provider_room_index()
{
    if (!mapp(quest_provider_room_index))
        quest_provider_room_index = build_quest_provider_room_index();
    return quest_provider_room_index;
}

/* L-37：provider 展示名的内容真源读取入口。内容缺失返回空串
 * （明确缺失，不回退旧 NPC 姓名表）。 */
string query_quest_provider_display_name(string provider_id)
{
    mapping index;
    mapping provider_names;

    if (!stringp(provider_id) || provider_id == "")
        return "";
    index = query_quest_provider_room_index();
    provider_names = mapp(index) && mapp(index["provider_names"])
        ? index["provider_names"] : ([]);
    return stringp(provider_names[provider_id]) ? provider_names[provider_id] : "";
}

/* 静态查询：provider 在本房间（含投影根房键）的模板归属。
 * 在场判定（含日程）由 NPC_SCHEDULE_D->query_provider_present_in_room
 * 负责，本 daemon 保持纯静态。 */
string *query_room_provider_templates(string room_path, string provider_id)
{
    mapping index;
    mapping room_map;

    room_path = normalize_path(room_path);
    if (room_path == "" || !stringp(provider_id) || provider_id == "")
        return ({ });
    index = query_quest_provider_room_index();
    room_map = mapp(index) && mapp(index["room_provider_templates"])
        ? index["room_provider_templates"][room_path] : 0;
    return mapp(room_map) && arrayp(room_map[provider_id])
        ? room_map[provider_id] + ({ }) : ({ });
}

void reset_content_query_indexes()
{
    quest_provider_room_index = 0;
    map_indoor_quest_topology_cache = ([]);
    content_rooms_index = 0;
    content_descriptions_index = 0;
}

mapping query_quest_provider_rooms()
{
    mapping index;
    mapping provider_rooms;

    index = query_quest_provider_room_index();
    provider_rooms = mapp(index["provider_rooms"]) ? index["provider_rooms"] : ([]);
    return copy_string_array_mapping(provider_rooms);
}

string *query_room_quest_provider_ids(string room_path)
{
    mapping index;
    mapping room_providers;
    string *providers;

    room_path = normalize_path(room_path);
    if (room_path == "")
        return ({ });

    index = query_quest_provider_room_index();
    room_providers = mapp(index["room_providers"]) ? index["room_providers"] : ([]);
    providers = arrayp(room_providers[room_path]) ? room_providers[room_path] : ({ });
    return providers + ({ });
}

string *query_map_marker_quest_provider_ids(string room_path)
{
    mapping index;
    mapping projected_providers;
    string *providers;
    string *projected;

    room_path = normalize_path(room_path);
    if (room_path == "")
        return ({ });

    index = query_quest_provider_room_index();
    projected_providers = mapp(index["room_projected_providers"])
        ? index["room_projected_providers"] : ([]);
    providers = query_room_quest_provider_ids(room_path);
    projected = arrayp(projected_providers[room_path]) ? projected_providers[room_path] : ({ });
    return append_unique_strings(providers, projected);
}


private mixed *copy_indoor_room_candidates(mixed *candidates)
{
    mixed *result;
    mapping candidate;
    int index;

    if (!arrayp(candidates))
        return ({ });

    result = allocate(sizeof(candidates));
    for (index = 0; index < sizeof(candidates); index++) {
        candidate = candidates[index];
        if (!mapp(candidate)) {
            result[index] = candidate;
            continue;
        }
        result[index] = copy(candidate);
        result[index]["provider_ids"] = arrayp(candidate["provider_ids"])
            ? candidate["provider_ids"] + ({ }) : ({ });
    }
    return result;
}

private mapping copy_indoor_quest_topology(mapping topology)
{
    if (!mapp(topology))
        return ([]);
    return ([
        "entry_room_id": topology["entry_room_id"],
        "start_room_id": topology["start_room_id"],
        "room_candidates": copy_indoor_room_candidates(topology["room_candidates"]),
    ]);
}

private string map_indoor_quest_topology_cache_key(string entry_room_id,
    string start_room_id, int max_steps, int max_rooms)
{
    if (!stringp(entry_room_id) || entry_room_id == ""
        || !stringp(start_room_id) || start_room_id == "")
        return "";
    return sprintf("%s#%s#%d#%d", entry_room_id, start_room_id,
        max_steps, max_rooms);
}

private mapping build_map_indoor_quest_topology(mapping rooms,
    mapping room_providers, string entry_room_id, string start_room_id,
    int max_steps, int max_rooms)
{
    string *room_queue;
    int *step_queue;
    mixed *candidate_buffer;
    mapping visited;
    mapping room;
    mapping exits;
    string room_path;
    string next_room_id;
    string *provider_ids;
    string dir;
    int head;
    int tail;
    int step_count;
    int candidate_count;

    if (!mapp(rooms) || !mapp(room_providers) || max_steps < 0 || max_rooms <= 0)
        return ([]);
    if (!mapp(rooms[start_room_id]))
        return ([]);

    room_queue = allocate(max_rooms);
    step_queue = allocate(max_rooms);
    candidate_buffer = allocate(max_rooms);
    visited = ([ start_room_id: 1 ]);
    room_queue[0] = start_room_id;
    step_queue[0] = 0;
    tail = 1;

    for (head = 0; head < tail; head++) {
        room_path = room_queue[head];
        step_count = step_queue[head];
        provider_ids = arrayp(room_providers[room_path])
            ? room_providers[room_path] : ({ });
        if (sizeof(provider_ids) > 0) {
            candidate_buffer[candidate_count++] = ([
                "room_id": room_path,
                "provider_ids": provider_ids + ({ }),
            ]);
        }

        if (step_count >= max_steps || tail >= max_rooms)
            continue;
        room = rooms[room_path];
        exits = mapp(room) && mapp(room["exits"]) ? room["exits"] : ([]);
        foreach (dir, next_room_id in exits) {
            next_room_id = normalize_path(next_room_id);
            if (next_room_id == "" || next_room_id == entry_room_id)
                continue;
            if (lower_case(dir) == "out")
                continue;
            if (strsrch(next_room_id, "__") != -1
                && query_indoor_root_room_path(next_room_id) != entry_room_id)
                continue;
            if (visited[next_room_id] || !mapp(rooms[next_room_id]))
                continue;

            visited[next_room_id] = 1;
            room_queue[tail] = next_room_id;
            step_queue[tail] = step_count + 1;
            tail++;
            if (tail >= max_rooms)
                break;
        }
    }

    return ([
        "entry_room_id": entry_room_id,
        "start_room_id": start_room_id,
        "room_candidates": candidate_count > 0
            ? candidate_buffer[0..candidate_count - 1] : ({ }),
    ]);
}

private mapping query_map_indoor_quest_topology(mapping rooms,
    mapping room_providers, string entry_room_id, string start_room_id,
    int max_steps, int max_rooms)
{
    mapping topology;
    string cache_key;
    string *cache_keys;

    cache_key = map_indoor_quest_topology_cache_key(entry_room_id,
        start_room_id, max_steps, max_rooms);
    if (cache_key == "")
        return ([]);
    if (!mapp(map_indoor_quest_topology_cache))
        map_indoor_quest_topology_cache = ([]);
    if (mapp(map_indoor_quest_topology_cache[cache_key]))
        return copy_indoor_quest_topology(map_indoor_quest_topology_cache[cache_key]);

    topology = build_map_indoor_quest_topology(rooms, room_providers,
        entry_room_id, start_room_id, max_steps, max_rooms);
    if (!mapp(topology))
        return ([]);

    if (sizeof(map_indoor_quest_topology_cache)
        >= MAP_INDOOR_QUEST_TOPOLOGY_CACHE_LIMIT) {
        cache_keys = sort_array(keys(map_indoor_quest_topology_cache), 1);
        if (sizeof(cache_keys) > 0)
            map_delete(map_indoor_quest_topology_cache, cache_keys[0]);
    }
    map_indoor_quest_topology_cache[cache_key] = copy_indoor_quest_topology(topology);
    return copy_indoor_quest_topology(topology);
}

mapping query_map_quest_marker_topology(string *room_paths,
    mixed *indoor_candidates, int max_steps, int max_rooms)
{
    mapping rooms;
    mapping index;
    mapping room_providers;
    mapping projected_providers;
    mapping direct_by_room;
    mapping map_by_room;
    mixed *topology_candidates;
    mapping candidate;
    mapping topology;
    mapping result_candidate;
    string room_path;
    string entry_room_id;
    string start_room_id;
    string *direct;
    string *projected;
    int candidate_count;

    direct_by_room = ([]);
    map_by_room = ([]);
    if (!arrayp(room_paths) || !arrayp(indoor_candidates)
        || max_steps < 0 || max_rooms <= 0) {
        return ([
            "room_provider_ids": direct_by_room,
            "map_marker_provider_ids": map_by_room,
            "indoor_entry_candidates": ({ }),
        ]);
    }
    if (max_steps > MAP_QUEST_TOPOLOGY_MAX_STEPS)
        max_steps = MAP_QUEST_TOPOLOGY_MAX_STEPS;
    if (max_rooms > MAP_QUEST_TOPOLOGY_MAX_ROOMS)
        max_rooms = MAP_QUEST_TOPOLOGY_MAX_ROOMS;

    rooms = query_content_rooms();
    index = query_quest_provider_room_index();
    room_providers = mapp(index["room_providers"])
        ? index["room_providers"] : ([]);
    projected_providers = mapp(index["room_projected_providers"])
        ? index["room_projected_providers"] : ([]);

    foreach (room_path in room_paths) {
        room_path = normalize_path(room_path);
        if (room_path == "")
            continue;
        direct = arrayp(room_providers[room_path])
            ? room_providers[room_path] + ({ }) : ({ });
        projected = arrayp(projected_providers[room_path])
            ? projected_providers[room_path] : ({ });
        direct_by_room[room_path] = direct;
        map_by_room[room_path] = append_unique_strings(direct + ({ }), projected);
    }

    topology_candidates = allocate(sizeof(indoor_candidates));
    foreach (candidate in indoor_candidates) {
        if (!mapp(candidate))
            continue;
        entry_room_id = normalize_path(candidate["entry_room_id"]);
        start_room_id = normalize_path(candidate["target_room_id"]);
        topology = query_map_indoor_quest_topology(rooms, room_providers,
            entry_room_id, start_room_id, max_steps, max_rooms);
        if (!mapp(topology))
            continue;

        result_candidate = copy(candidate);
        result_candidate["room_candidates"] =
            copy_indoor_room_candidates(topology["room_candidates"]);
        topology_candidates[candidate_count++] = result_candidate;
    }

    return ([
        "room_provider_ids": direct_by_room,
        "map_marker_provider_ids": map_by_room,
        "indoor_entry_candidates": candidate_count > 0
            ? topology_candidates[0..candidate_count - 1] : ({ }),
    ]);
}

mapping query_entity_rooms(string entity_id)
{
    mapping entities_by_room;
    mapping npc_templates;
    mapping result;
    mixed raw_entities;
    mapping entity;
    mapping template;
    mapping nodes;
    mapping node;
    mapping events;
    mapping event;
    string room_path;
    string template_id;
    string *ids;

    result = ([]);
    if (!stringp(entity_id) || entity_id == "")
        return result;

    entities_by_room = query_content_section("room_entities");
    npc_templates = query_content_section("npc_templates");

    /* 采集点/事件实体不在 room_entities 索引内——按 reward_item /
     * present_id 反查 resource_nodes 与 world_events，任务追踪（COLLECT 步
     * 纯 item_id、LOOK 步指向事件实体）由此可解析目标房间。 */
    nodes = query_content_section("resource_nodes");
    if (mapp(nodes)) {
        foreach (string node_id, node in nodes) {
            if (!mapp(node))
                continue;
            if (node["reward_item"] == entity_id
                && stringp(node["room"]) && node["room"] != "")
                result[node["room"]] = 1;
        }
    }
    events = query_content_section("world_events");
    if (mapp(events)) {
        foreach (string event_id, event in events) {
            if (!mapp(event))
                continue;
            if (event["present_id"] == entity_id
                && stringp(event["room"]) && event["room"] != "")
                result[event["room"]] = 1;
        }
    }

    if (!mapp(entities_by_room))
        return result;

    foreach (room_path, raw_entities in entities_by_room) {
        if (!arrayp(raw_entities))
            continue;
        foreach (entity in raw_entities) {
            if (!mapp(entity))
                continue;
            template_id = stringp(entity["template_id"]) ? entity["template_id"] : "";
            if (entity["present_id"] == entity_id || entity["id"] == entity_id
                || template_id == entity_id
                || query_entity_provider_id(entity, npc_templates) == entity_id) {
                result[room_path] = 1;
                continue;
            }
            template = template_id != "" && mapp(npc_templates) ? npc_templates[template_id] : 0;
            ids = mapp(template) ? coerce_string_array(template["ids"]) : ({ });
            if (member_array(entity_id, ids) != -1)
                result[room_path] = 1;
        }
    }
    return copy(result);
}

/* 任务击杀目标声明集合：room_entities 中所有 quest_kill_target → 1。
 * 击杀进度由战斗结算读取 NPC 实例的该字段推进，任务校验据此判断 kill
 * 步骤的 target 是否有实体承载；room_entities 未配置时返回 0（跳过校验）。 */
mapping query_quest_kill_targets()
{
    mapping entities_by_room;
    mapping targets;
    mixed *entities;
    mapping entity;
    string room_path;

    entities_by_room = query_content_section("room_entities");
    if (!mapp(entities_by_room) || sizeof(entities_by_room) == 0)
        return 0;
    targets = ([]);
    foreach (room_path, entities in entities_by_room) {
        if (!arrayp(entities))
            continue;
        foreach (entity in entities) {
            if (mapp(entity) && stringp(entity["quest_kill_target"])
                && entity["quest_kill_target"] != "")
                targets[entity["quest_kill_target"]] = 1;
        }
    }
    return targets;
}

mapping query_room_behaviors(string room_path)
{
    mapping behaviors;
    mapping result;
    mapping behavior;
    string behavior_id;

    room_path = normalize_path(room_path);
    behaviors = query_content_section("room_behaviors");
    result = ([]);
    if (!mapp(behaviors) || room_path == "")
        return result;

    foreach (behavior_id, behavior in behaviors) {
        if (!mapp(behavior))
            continue;
        if (normalize_path(behavior["room"]) != room_path)
            continue;
        result[behavior_id] = copy(behavior);
    }
    return result;
}

mapping query_npc_template(string template_id)
{
    mapping templates;
    mapping template;

    templates = query_content_section("npc_templates");
    template = mapp(templates) && stringp(template_id) ? templates[template_id] : 0;
    return mapp(template) ? copy(template) : 0;
}

mapping query_npc_template_for_actor(object actor, string template_id)
{
    mapping template;

    template = query_npc_template(template_id);
    if (!mapp(template))
        return 0;
    return CONTENT_MODE_POLICY_D->project_content(actor, "npc", template);
}

mapping query_item_template(string template_id)
{
    mapping templates;
    mapping template;

    templates = query_content_section("item_templates");
    template = mapp(templates) && stringp(template_id) ? templates[template_id] : 0;
    return mapp(template) ? copy(template) : 0;
}

mapping query_item_template_for_actor(object actor, string template_id)
{
    mapping template;

    template = query_item_template(template_id);
    if (!mapp(template))
        return 0;
    return CONTENT_MODE_POLICY_D->project_content(actor, "item", template);
}

mapping query_exploration_chest(string chest_id)
{
    mapping chests;
    mapping chest;

    chests = query_content_section("exploration_chests");
    chest = mapp(chests) && stringp(chest_id) ? chests[chest_id] : 0;
    return mapp(chest) ? copy(chest) : 0;
}

mapping query_exploration_chests()
{
    mapping chests;

    chests = query_content_section("exploration_chests");
    return mapp(chests) ? copy(chests) : ([ ]);
}

mapping query_listen_points()
{
    mapping points;

    points = query_content_section("listen_points");
    return mapp(points) ? copy(points) : ([ ]);
}

mapping query_world_events()
{
    mapping events;

    events = query_content_section("world_events");
    return mapp(events) ? copy(events) : ([ ]);
}

mapping query_quest_definitions()
{
    mapping definitions;

    definitions = query_content_section("quest_definitions");
    return mapp(definitions) ? copy(definitions) : ([]);
}

mapping query_quest_definitions_for_actor(object actor)
{
    mapping definitions;
    mapping result;
    mapping definition;
    string quest_id;

    result = ([]);
    definitions = query_quest_definitions();
    foreach (quest_id, definition in definitions) {
        if (mapp(definition)
            && CONTENT_MODE_POLICY_D->is_content_visible(actor, "quest", definition))
            result[quest_id] = definition;
    }
    return result;
}

string *query_allowed_action_groups()
{
    return coerce_string_array(query_content_section("allowed_action_groups"));
}

string *query_region_package_paths()
{
    return coerce_string_array(query_content_section("region_package_paths"));
}

private int path_file_exists(string path)
{
    string file;

    file = query_lpc_file(path);
    return file != "" && file_size(file) >= 0;
}

private int path_object_loadable(string path)
{
    path = normalize_path(path);
    if (path == "")
        return 0;
    return catch(load_object(path)) ? 0 : 1;
}

private void validate_single_room(mapping report, mapping areas, mapping rooms, string room_path, mapping room)
{
    mapping exits;
    string dir;
    string target;

    if (!mapp(room)) {
        add_violation(report, "rooms", "invalid room definition: " + room_path);
        return;
    }
    if (!path_file_exists(room_path))
        add_violation(report, "rooms", "room file missing: " + room_path);
    else if (!path_object_loadable(room_path))
        add_violation(report, "rooms", "room load failed: " + room_path);
    if (!mapp(areas) || !mapp(areas[room["area"] || ""]))
        add_violation(report, "rooms", "room area missing: " + room_path);
    exits = room["exits"];
    if (!mapp(exits))
        return;
    foreach (dir, target in exits) {
        if (!stringp(target) || target == "" || !mapp(rooms[normalize_path(target)]))
            add_violation(report, "links", "broken exit " + room_path + " -> " + dir);
    }
}

private void validate_areas(mapping report, mapping config)
{
    mapping areas;
    mapping area;
    string *area_ids;
    string area_id;

    areas = config["areas"];
    if (!mapp(areas) || !sizeof(areas)) {
        add_violation(report, "areas", "missing areas section");
        return;
    }
    area_ids = keys(areas);
    foreach (area_id in area_ids) {
        area = areas[area_id];
        if (!mapp(area)) {
            add_violation(report, "areas", "invalid area: " + area_id);
            continue;
        }
        if (!stringp(area["entry_room"]) || area["entry_room"] == "")
            add_violation(report, "areas", "area missing entry room: " + area_id);
    }
}

private void validate_region_packages(mapping report, mapping config)
{
    string package_path;
    string *package_paths;
    string issue;
    string *issues;

    package_paths = coerce_string_array(config["region_package_paths"]);
    if (sizeof(package_paths) == 0) {
        add_violation(report, "regions", "missing region packages section");
        return;
    }

    foreach (package_path in package_paths) {
        if (!path_file_exists(package_path))
            add_violation(report, "regions", "region package file missing: " + package_path);
        else if (!path_object_loadable(package_path))
            add_violation(report, "regions", "region package load failed: " + package_path);
    }

    /* 未知顶层键（region_package_issues 由 content_config merge 时收集）
     * 报为 violation，让拼错键名的配置错误启动可见而非静默丢弃。 */
    issues = coerce_string_array(config["region_package_issues"]);
    foreach (issue in issues)
        add_violation(report, "regions", issue);
}

private void validate_rooms(mapping report, mapping config, int full_mode)
{
    mapping areas;
    mapping rooms;
    mapping room;
    string room_path;
    string start_room_path;
    int room_count;

    rooms = config["rooms"];
    areas = config["areas"];
    report["declared_rooms"] = mapp(rooms) ? sizeof(rooms) : 0;
    if (!mapp(rooms) || !sizeof(rooms)) {
        add_violation(report, "rooms", "missing rooms section");
        return;
    }
    room_count = sizeof(rooms);
    if (full_mode) {
        foreach (room_path in keys(rooms)) {
            report["checked_rooms"] = report["checked_rooms"] + 1;
            validate_single_room(report, areas, rooms, room_path, rooms[room_path]);
        }
        return;
    }

    start_room_path = CONFIG_D->query_start_room();
    report["skipped_rooms"] = room_count - 1;
    report["skipped"] = report["skipped"] + room_count - 1;
    room_path = start_room_path;
    room = stringp(room_path) ? rooms[room_path] : 0;
    if (!mapp(room)) {
        add_violation(report, "rooms", "start room missing: " + (room_path || ""));
        return;
    }
    report["checked_rooms"] = 1;
    validate_single_room(report, areas, rooms, room_path, room);
}

private void validate_entities(mapping report, mapping config, int full_mode)
{
    mapping rooms;
    mapping entities_by_room;
    mapping templates;
    mapping item_templates;
    mapping entity;
    mapping template;
    string room_path;
    string start_room_path;
    mixed *entities;
    int i;
    int room_entity_count;

    rooms = config["rooms"];
    entities_by_room = config["room_entities"];
    templates = config["npc_templates"];
    item_templates = config["item_templates"];
    report["declared_entity_rooms"] = mapp(entities_by_room)
        ? sizeof(entities_by_room) : 0;
    if (!mapp(entities_by_room))
        return;
    room_entity_count = sizeof(entities_by_room);
    start_room_path = CONFIG_D->query_start_room();

    if (!full_mode) {
        room_path = normalize_path(start_room_path);
        if (undefinedp(entities_by_room[room_path])) {
            report["skipped_entity_rooms"] = room_entity_count;
            report["skipped"] = report["skipped"] + room_entity_count;
            return;
        }
        report["checked_entity_rooms"] = 1;
        entities = entities_by_room[room_path];
        report["skipped_entity_rooms"] = room_entity_count - 1;
        report["skipped"] = report["skipped"] + room_entity_count - 1;
        if (!arrayp(entities)) {
            add_violation(report, "entities", "room entities invalid: " + room_path);
            return;
        }
        if (!mapp(rooms[room_path]))
            add_violation(report, "entities", "entity room missing: " + room_path);
        for (i = 0; i < sizeof(entities); i++) {
            entity = entities[i];
            if (!mapp(entity)) {
                add_violation(report, "entities", "invalid room entity in: " + room_path);
                continue;
            }
            if (!path_file_exists(entity["path"] || ""))
                add_violation(report, "entities", "entity file missing: " + (entity["path"] || ""));
            else if (!path_object_loadable(entity["path"] || ""))
                add_violation(report, "entities", "entity load failed: " + (entity["path"] || ""));
            template = templates[entity["template_id"] || ""];
            if (entity["type"] == "npc" && !mapp(template))
                add_violation(report, "npcs", "npc template missing: " + (entity["template_id"] || ""));
            if (entity["type"] == "item" && !mapp(item_templates[entity["template_id"] || ""]))
                add_violation(report, "items", "item template missing: " + (entity["template_id"] || ""));
        }
        return;
    }

    foreach (room_path in keys(entities_by_room)) {
        report["checked_entity_rooms"] = report["checked_entity_rooms"] + 1;
        entities = entities_by_room[room_path];
        if (!mapp(rooms[normalize_path(room_path)]))
            add_violation(report, "entities", "entity room missing: " + room_path);
        if (!arrayp(entities)) {
            add_violation(report, "entities", "room entities invalid: " + room_path);
            continue;
        }
        for (i = 0; i < sizeof(entities); i++) {
            entity = entities[i];
            if (!mapp(entity)) {
                add_violation(report, "entities", "invalid room entity in: " + room_path);
                continue;
            }
            if (!path_file_exists(entity["path"] || ""))
                add_violation(report, "entities", "entity file missing: " + (entity["path"] || ""));
            else if (!path_object_loadable(entity["path"] || ""))
                add_violation(report, "entities", "entity load failed: " + (entity["path"] || ""));
            template = templates[entity["template_id"] || ""];
            if (entity["type"] == "npc" && !mapp(template))
                add_violation(report, "npcs", "npc template missing: " + (entity["template_id"] || ""));
            if (entity["type"] == "item" && !mapp(item_templates[entity["template_id"] || ""]))
                add_violation(report, "items", "item template missing: " + (entity["template_id"] || ""));
        }
    }
}

private void validate_item_templates(mapping report, mapping config)
{
    mapping templates;
    mapping template;
    string template_id;

    templates = config["item_templates"];
    if (!mapp(templates))
        return;
    foreach (template_id in keys(templates)) {
        template = templates[template_id];
        if (!mapp(template)) {
            add_violation(report, "items", "invalid item template: " + template_id);
            continue;
        }
        if (!stringp(template["name"]) || template["name"] == "")
            add_violation(report, "items", "item template missing name: " + template_id);
        if (!stringp(template["type"]) || template["type"] == "")
            add_violation(report, "items", "item template missing type: " + template_id);
        if (stringp(template["path"]) && template["path"] != "" && !path_file_exists(template["path"]))
            add_violation(report, "items", "item template file missing: " + template_id);
    }
}

private void validate_combat_loot_tables(mapping report, mapping config)
{
    mapping tables;
    mapping table;
    mixed *items;
    mixed item_entry;
    string table_id;
    string item_id;

    tables = config["combat_loot_tables"];
    if (!mapp(tables))
        return;
    foreach (table_id, table in tables) {
        if (!mapp(table)) {
            add_violation(report, "combat_loot", "invalid loot table: " + table_id);
            continue;
        }
        items = arrayp(table["items"]) ? table["items"] : ({ });
        foreach (item_entry in items) {
            if (stringp(item_entry))
                item_id = item_entry;
            else if (mapp(item_entry))
                item_id = item_entry["template_id"] || item_entry["item"] || item_entry["id"] || "";
            else
                item_id = "";
            if (item_id != "" && !ITEM_D->item_exists(item_id))
                add_violation(report, "combat_loot", "loot item template missing: " + item_id);
        }
    }
}

private string query_reward_item_id(mixed item_entry)
{
    if (stringp(item_entry))
        return item_entry;
    if (mapp(item_entry))
        return item_entry["template_id"] || item_entry["item_id"] || item_entry["item"] || item_entry["id"] || "";
    return "";
}

private void validate_reward_mapping(mapping report, string category, string owner_id, mapping rewards)
{
    mixed *attachments;
    mixed item_entry;
    string item_id;

    if (!mapp(rewards))
        return;
    if (arrayp(rewards["items"]))
        add_violation(report, category, owner_id + " uses rewards.items; use attachments");
    item_id = rewards["item"];
    if (stringp(item_id) && item_id != "" && !ITEM_D->item_exists(item_id))
        add_violation(report, category, owner_id + " reward item missing: " + item_id);
    attachments = arrayp(rewards["attachments"]) ? rewards["attachments"] : ({ });
    foreach (item_entry in attachments) {
        item_id = query_reward_item_id(item_entry);
        if (!stringp(item_id) || item_id == "")
            add_violation(report, category, owner_id + " invalid reward attachment");
        else if (!ITEM_D->item_exists(item_id))
            add_violation(report, category, owner_id + " reward attachment missing: " + item_id);
    }
}

private void validate_resource_nodes(mapping report, mapping config)
{
    mapping nodes;
    mapping node;
    string node_id;

    nodes = config["resource_nodes"];
    if (!mapp(nodes))
        return;
    foreach (node_id, node in nodes) {
        if (!mapp(node)) {
            add_violation(report, "resource_nodes", "invalid resource node: " + node_id);
            continue;
        }
        if (!stringp(node["skill_id"]) || node["skill_id"] == "")
            add_violation(report, "resource_nodes", "resource node missing skill_id: " + node_id);
        else if (!mapp(LIFE_SKILL_D->query_skill_definition(node["skill_id"])))
            add_violation(report, "resource_nodes", "resource node skill missing: " + node["skill_id"]);
        if (!stringp(node["reward_item"]) || node["reward_item"] == "")
            add_violation(report, "resource_nodes", "resource node missing reward_item: " + node_id);
        else if (!ITEM_D->item_exists(node["reward_item"]))
            add_violation(report, "resource_nodes", "resource node reward item missing: " + node["reward_item"]);
    }
}

private void validate_shops(mapping report, mapping config)
{
    mapping shops;
    mapping shop;
    mixed *items;
    mixed item_id;
    string shop_id;

    shops = config["shops"];
    if (!mapp(shops))
        return;
    foreach (shop_id, shop in shops) {
        if (!mapp(shop)) {
            add_violation(report, "shops", "invalid shop: " + shop_id);
            continue;
        }
        items = arrayp(shop["default_items"]) ? shop["default_items"] : ({ });
        foreach (item_id in items) {
            if (!stringp(item_id) || item_id == "" || !ITEM_D->item_exists(item_id))
                add_violation(report, "shops", "shop item missing: " + shop_id + " -> " + describe_value(item_id));
        }
    }
}

private void validate_reward_tables(mapping report, mapping config)
{
    mapping tables;
    mapping rewards;
    string table_id;

    tables = config["reward_tables"];
    if (!mapp(tables))
        return;
    foreach (table_id, rewards in tables) {
        if (!mapp(rewards))
            add_violation(report, "reward_tables", "invalid reward table: " + table_id);
        else
            validate_reward_mapping(report, "reward_tables", table_id, rewards);
    }
}

private void validate_reputation_sources(mapping report, mapping config)
{
    mapping sources;
    mapping source;
    string source_id;

    sources = config["reputation_sources"];
    if (!mapp(sources))
        return;
    foreach (source_id, source in sources) {
        if (!mapp(source))
            add_violation(report, "reputation_sources", "invalid reputation source: " + source_id);
        else if (!stringp(source["scope"]) || source["scope"] == "")
            add_violation(report, "reputation_sources", "reputation source missing scope: " + source_id);
    }
}

private void validate_exploration_chests(mapping report, mapping config)
{
    mapping chests;
    mapping chest;
    mapping rooms;
    mapping reward_tables;
    string chest_id;
    string room_path;
    string template_id;
    string reward_table;

    chests = config["exploration_chests"];
    rooms = config["rooms"];
    reward_tables = config["reward_tables"];
    if (!mapp(chests))
        return;
    foreach (chest_id, chest in chests) {
        if (!mapp(chest)) {
            add_violation(report, "exploration_chests", "invalid chest: " + chest_id);
            continue;
        }
        room_path = normalize_path(chest["room"] || "");
        if (room_path == "" || !mapp(rooms[room_path]))
            add_violation(report, "exploration_chests", "chest room missing: " + chest_id);
        template_id = chest["template_id"] || "";
        if (template_id == "" || !ITEM_D->item_exists(template_id))
            add_violation(report, "exploration_chests", "chest item template missing: " + chest_id);
        reward_table = chest["reward_table"] || "";
        if (reward_table != "" && !mapp(reward_tables[reward_table]))
            add_violation(report, "exploration_chests", "chest reward table missing: " + chest_id);
        if (mapp(chest["rewards"]))
            validate_reward_mapping(report, "exploration_chests", chest_id, chest["rewards"]);
    }
}

private void validate_quest_definitions(mapping report, mapping config)
{
    mapping definitions;
    mapping validation;
    string message;

    definitions = config["quest_definitions"];
    if (!mapp(definitions) || sizeof(definitions) == 0)
        return;
    validation = call_other("/daemons/services/gameplay/quest/quest_validate",
        "validate_all_quest_definitions", definitions);
    if (!mapp(validation)) {
        add_violation(report, "quests", "quest validation failed");
        return;
    }
    foreach (message in validation["errors"] || ({ }))
        add_violation(report, "quests", message);
}

/* 日程交叉校验辅助：room_entities 中该房间是否有实体引用该模板。 */
private int room_entity_covers_template(mapping room_entities, string room_path,
    string template_id)
{
    mapping *entities;
    mapping entity;
    int i;

    if (!mapp(room_entities))
        return 0;
    entities = room_entities[room_path];
    if (!arrayp(entities))
        return 0;
    for (i = 0; i < sizeof(entities); i++) {
        entity = entities[i];
        if (mapp(entity) && (entity["template_id"] || "") == template_id)
            return 1;
    }
    return 0;
}

private void validate_npc_templates(mapping report, mapping config)
{
    mapping templates;
    mapping template;
    mapping room_entities;
    mapping schedule;
    string *template_ids;
    string *room_paths;
    string template_id;
    string room_path;
    string default_room;
    mixed schedule_value;

    templates = config["npc_templates"];
    if (!mapp(templates) || !sizeof(templates))
        return;  /* 空世界：旧 NPC 内容已删（Task 09），空 section 是合法状态 */
    room_entities = config["room_entities"];
    template_ids = keys(templates);
    foreach (template_id in template_ids) {
        template = templates[template_id];
        if (!mapp(template)) {
            add_violation(report, "npcs", "invalid npc template: " + template_id);
            continue;
        }
        if (!path_file_exists(template["path"] || ""))
            add_violation(report, "npcs", "npc template file missing: " + template_id);
        else if (!path_object_loadable(template["path"] || ""))
            add_violation(report, "npcs", "npc template load failed: " + template_id);
        /* 日程交叉校验：带 schedule/schedule_default 的模板，其全部
         * 目标房间必须被 room_entities 覆盖（否则该时辰 NPC 无处
         * 补刷，日程移动后房间空置）。 */
        schedule_value = template["schedule"];
        default_room = template["schedule_default"] || "";
        if (!mapp(schedule_value) && default_room == "")
            continue;
        room_paths = ({ });
        if (mapp(schedule_value)) {
            foreach (room_path in values(schedule_value)) {
                if (stringp(room_path) && room_path != ""
                    && member_array(room_path, room_paths) == -1)
                    room_paths += ({ room_path });
            }
        }
        if (default_room != "" && member_array(default_room, room_paths) == -1)
            room_paths += ({ default_room });
        foreach (room_path in room_paths) {
            if (!room_entity_covers_template(room_entities, room_path, template_id))
                add_violation(report, "npcs",
                    "npc schedule room not covered by room_entities: "
                    + template_id + " -> " + room_path);
        }
    }
}

/* 技能→重置导师全集反查：遍历活树教师配置，返回 teach_skills 含
 * skill_id 的全部 provider_id；换代后教师配置更新即自动跟随，禁止再写
 * 硬编码技能 id 表（旧代 switch 曾致突破重置对全部真实技能落空）。
 * 返回全集而非单值：多师场景（试炼/新区/门派扩招）下任一在场即可，
 * 单师场景集合恰含一个元素，语义不变。 */
public string *query_skill_teacher_providers(string skill_id)
{
    mapping teachers;
    string *providers;
    string provider_id;

    if (!stringp(skill_id) || skill_id == "")
        return ({ });
    teachers = query_content_section("skill_teachers");
    if (!mapp(teachers))
        return ({ });
    providers = ({ });
    foreach (provider_id, mixed raw_teacher in teachers) {
        mapping teacher;
        mixed *teach_skills;

        teacher = mapp(raw_teacher) ? raw_teacher : 0;
        if (!teacher)
            continue;
        teach_skills = teacher["teach_skills"];
        if (arrayp(teach_skills) && member_array(skill_id, teach_skills) != -1)
            providers += ({ provider_id });
    }
    return providers;
}

private void validate_skill_teachers(mapping report, mapping config)
{
    mapping chests;
    mapping templates;
    mapping teachers;
    mapping teacher;
    mapping quest_defs;
    string *teacher_ids;
    string chest_id;
    string quest_id;
    string teacher_id;
    string skill_id;
    mixed required_chests;
    mixed required_quests;

    chests = config["exploration_chests"];
    quest_defs = config["quest_definitions"];
    templates = config["npc_templates"];
    teachers = config["skill_teachers"];
    if (!mapp(teachers))
        return;
    teacher_ids = keys(teachers);
    /* 跨 provider 冲突核查（warning 级）：同一 teach_skill 出现在 >1 个
     * 教师即记 warning。多师是合法状态（运行时按全集匹配，任一在场
     * 即可，不依赖迭代序），仅当配置漂移（复制粘贴教师块）时供人工
     * 核查；不进 violations 主列表，避免合法扩招被误报为违规。 */
    {
        mapping skill_owner_count;
        string *skill_ids;

        skill_owner_count = ([]);
        foreach (teacher_id in teacher_ids) {
            teacher = teachers[teacher_id];
            if (!mapp(teacher))
                continue;
            foreach (skill_id in coerce_string_array(teacher["teach_skills"])) {
                if (!mapp(skill_owner_count[skill_id]))
                    skill_owner_count[skill_id] = ([ ]);
                skill_owner_count[skill_id][teacher_id] = 1;
            }
        }
        skill_ids = keys(skill_owner_count);
        foreach (skill_id in skill_ids) {
            if (sizeof(skill_owner_count[skill_id]) > 1)
                add_warning(report, "skills",
                    "skill taught by multiple teachers: " + skill_id);
        }
    }
    foreach (teacher_id in teacher_ids) {
        teacher = teachers[teacher_id];
        if (!mapp(teacher)) {
            add_violation(report, "skills", "invalid skill teacher: " + teacher_id);
            continue;
        }
        if (!mapp(templates) || !mapp(templates[teacher["teacher_template_id"] || ""]))
            add_violation(report, "skills", "teacher template missing: " + teacher_id);
        foreach (skill_id in coerce_string_array(teacher["teach_skills"])) {
            if (!SKILL_D->skill_exists(skill_id))
                add_violation(report, "skills", "skill missing: " + skill_id);
        }
        required_quests = teacher["required_quests"];
        if (stringp(required_quests) && required_quests != "")
            required_quests = ({ required_quests });
        foreach (quest_id in coerce_string_array(required_quests)) {
            if (!mapp(quest_defs) || !mapp(quest_defs[quest_id]))
                add_violation(report, "skills", "teacher quest gate missing: " + teacher_id + " -> " + quest_id);
        }
        required_chests = teacher["required_chests"];
        if (stringp(required_chests) && required_chests != "")
            required_chests = ({ required_chests });
        foreach (chest_id in coerce_string_array(required_chests)) {
            if (!mapp(chests) || !mapp(chests[chest_id]))
                add_violation(report, "skills", "teacher chest gate missing: " + teacher_id + " -> " + chest_id);
        }
    }
}

private int validate_event_candidate(mapping report, string event_id,
    mapping event, mapping config)
{
    mapping rooms;
    object event_ob;
    object room_ob;
    string room_path;
    string event_path;
    mixed hours;
    mixed hour;
    mixed err;
    int valid_hours;
    int ok;

    ok = 1;
    rooms = config["rooms"];
    if (stringp(event["id"]) && event["id"] != event_id) {
        add_violation(report, "world_events", "event id mismatch: " + event_id);
        ok = 0;
    }
    if (!stringp(event["name"]) || event["name"] == "") {
        add_violation(report, "world_events", "event name missing: " + event_id);
        ok = 0;
    }
    room_path = event["room"];
    if (!stringp(room_path) || room_path == "") {
        add_violation(report, "world_events", "event room missing: " + event_id);
        ok = 0;
    } else {
        if (!mapp(rooms) || !mapp(rooms[room_path])) {
            add_violation(report, "world_events", "event room missing from rooms: " + event_id);
            ok = 0;
        }
        err = catch(room_ob = WORLD_TRAVEL_D->resolve_public_room(room_path, ([
            "reason": "content_event_validation",
            "event_id": event_id,
        ])));
        if (err || !objectp(room_ob)) {
            add_violation(report, "world_events",
                "event room is not resolvable through WORLD_TRAVEL_D: " + event_id);
            ok = 0;
        }
    }
    event_path = event["path"];
    if (!stringp(event_path) || event_path == "") {
        add_violation(report, "world_events", "event path missing: " + event_id);
        ok = 0;
    } else {
        err = catch(event_ob = load_object(event_path));
        if (err || !objectp(event_ob)) {
            add_violation(report, "world_events", "event path is not loadable: " + event_id);
            ok = 0;
        }
    }
    if (!stringp(event["present_id"]) || event["present_id"] == "") {
        add_violation(report, "world_events", "event present_id missing: " + event_id);
        ok = 0;
    }
    if (!stringp(event["state_scope"]) || event["state_scope"] == ""
        || !stringp(event["instance_scope"]) || event["instance_scope"] == "") {
        add_violation(report, "world_events",
            "event requires explicit state_scope and instance_scope: " + event_id);
        ok = 0;
    }
    hours = event["hours"];
    valid_hours = 1;
    if (arrayp(hours)) {
        if (sizeof(hours) == 0)
            valid_hours = 0;
        foreach (hour in hours)
            if (!intp(hour) || hour < 0
                || hour >= TIME_D->query_hours_per_day())
                valid_hours = 0;
    } else if (!intp(hours) || hours < 0
        || hours >= TIME_D->query_hours_per_day())
        valid_hours = 0;
    if (!valid_hours) {
        add_violation(report, "world_events", "event hours invalid: " + event_id);
        ok = 0;
    }
    return ok;
}

private void validate_world_events(mapping report, mapping config)
{
    mapping events;
    mapping event;
    string event_id;

    events = config["world_events"];
    if (!mapp(events))
        return;
    foreach (event_id, event in events) {
        if (!stringp(event_id) || event_id == "" || !mapp(event)) {
            add_violation(report, "world_events", "invalid event definition: " + event_id);
            continue;
        }
        validate_event_candidate(report, event_id, event, config);
    }
}

private void validate_active_content_definition(mapping report, string section,
    string definition_id, mapping definition)
{
    string state_scope;
    string instance_scope;

    if (!mapp(definition)) {
        add_violation(report, "content_sections",
            section + " contains invalid definition: " + definition_id);
        return;
    }
    state_scope = definition["state_scope"] || "";
    instance_scope = definition["instance_scope"] || "";
    /* scope 值合法性统一消费 CONTENT_MODE_POLICY_D 查询接口，
     * 不复制字面量（避免与 policy 常量双真源漂移）。 */
    if (member_array(state_scope,
        CONTENT_MODE_POLICY_D->query_allowed_state_scopes()) == -1)
        add_violation(report, "content_sections",
            section + " uses invalid or missing state_scope: " + definition_id);
    if (member_array(instance_scope,
        CONTENT_MODE_POLICY_D->query_allowed_instance_scopes()) == -1)
        add_violation(report, "content_sections",
            section + " uses invalid or missing instance_scope: " + definition_id);
}

private void validate_active_content_section(mapping report, mapping config,
    string section)
{
    mapping definition;
    mapping definitions;
    string definition_id;

    definitions = config[section];
    if (!mapp(definitions))
        return;
    foreach (definition_id, definition in definitions) {
        if (!stringp(definition_id) || definition_id == "")
            continue;
        validate_active_content_definition(report, section, definition_id,
            definition);
    }
}

private void validate_active_room_entities(mapping report, mapping config)
{
    mapping entity;
    mapping entities_by_room;
    mapping *entities;
    string entity_id;
    string room_path;

    entities_by_room = config["room_entities"];
    if (!mapp(entities_by_room))
        return;
    foreach (room_path, entities in entities_by_room) {
        if (!arrayp(entities))
            continue;
        foreach (entity in entities) {
            if (!mapp(entity))
                continue;
            entity_id = entity["id"] || entity["entity_id"]
                || entity["template_id"] || entity["path"] || "unknown";
            validate_active_content_definition(report, "room_entities",
                room_path + " -> " + entity_id, entity);
        }
    }
}

private void validate_active_content_sections(mapping report, mapping config)
{
    string section;
    string *sections;

    sections = ({
        "areas", "rooms", "npc_templates", "item_templates",
        "quest_definitions", "combat_loot_tables", "skill_teachers",
        "resource_nodes", "shops", "reward_tables", "reputation_sources",
        "exploration_chests", "world_events",
    });
    foreach (section in sections)
        validate_active_content_section(report, config, section);
    validate_active_room_entities(report, config);
    report["active_content_sections"] = ([
        "validated_sections": sections + ({ "room_entities" }),
    ]);
}

/* Test-harness-only validation entry point. It validates an isolated candidate
 * against the current room/object resolvers without publishing or incrementing
 * the live content generation. No production command exposes this function. */
mapping query_test_event_validation(mapping event)
{
    mapping config;
    mapping report;
    mapping candidate_events;
    string event_id;

    report = ([
        "ok": 1,
        "violations": ({ }),
        "validation_errors": ({ }),
        "previous_generation": query_content_generation(),
        "preserve_previous_generation": 1,
    ]);
    if (!mapp(event)) {
        add_violation(report, "world_events", "invalid event candidate");
        report["ok"] = 0;
        return report;
    }
    event_id = stringp(event["id"]) && event["id"] != ""
        ? event["id"] : "__test_event_candidate__";
    candidate_events = ([ event_id: event ]);
    config = query_content_config();
    config = config + ([ "world_events": candidate_events ]);
    validate_world_events(report, config);
    report["ok"] = sizeof(report["violations"] || ({ })) == 0 ? 1 : 0;
    return report;
}

private mapping build_validation_report(string mode)
{
    mapping config;
    mapping report;
    mixed issue;
    int full_mode;
    int invalid_mode;

    full_mode = 1;
    invalid_mode = 0;
    if (stringp(mode) && mode == "smoke")
        full_mode = 0;
    else if (stringp(mode) && mode != "" && mode != "full") {
        full_mode = 1;
        invalid_mode = 1;
    }

    config = query_content_config();
    report = ([
        "ok": 1,
        "mode": full_mode ? "full" : "smoke",
        "declared_rooms": 0,
        "checked_rooms": 0,
        "declared_entity_rooms": 0,
        "checked_entity_rooms": 0,
        "skipped_rooms": 0,
        "skipped_entity_rooms": 0,
        "skipped": 0,
        "area_count": 0,
        "region_package_count": 0,
        "room_count": 0,
        "npc_template_count": 0,
        "item_template_count": 0,
        "quest_definition_count": 0,
        "combat_loot_table_count": 0,
        "resource_node_count": 0,
        "shop_count": 0,
        "reward_table_count": 0,
        "reputation_source_count": 0,
        "exploration_chest_count": 0,
        "room_entity_count": 0,
        "validation_errors": ({ }),
        "violations": ({ }),
    ]);
    foreach (issue in (arrayp(config["load_errors"]) ? config["load_errors"] : ({ }))) {
        report["validation_errors"] += ({ issue });
        add_violation(report, "content_load", sprintf("%O", issue));
    }
    foreach (issue in (arrayp(config["region_package_issues"])
        ? config["region_package_issues"] : ({ }))) {
        report["validation_errors"] += ({ issue });
        add_violation(report, "content_ownership", sprintf("%O", issue));
    }
    foreach (issue in (arrayp(config["validation_errors"])
        ? config["validation_errors"] : ({ }))) {
        report["validation_errors"] += ({ issue });
        add_violation(report, "content_validation", sprintf("%O", issue));
    }
    if (invalid_mode)
        add_violation(report, "mode", "unknown content validation mode: " + mode);
    report["area_count"] = mapp(config["areas"]) ? sizeof(config["areas"]) : 0;
    report["region_package_count"] = sizeof(coerce_string_array(config["region_package_paths"]));
    report["room_count"] = mapp(config["rooms"]) ? sizeof(config["rooms"]) : 0;
    report["npc_template_count"] = mapp(config["npc_templates"]) ? sizeof(config["npc_templates"]) : 0;
    report["item_template_count"] = mapp(config["item_templates"]) ? sizeof(config["item_templates"]) : 0;
    report["quest_definition_count"] = mapp(config["quest_definitions"]) ? sizeof(config["quest_definitions"]) : 0;
    report["combat_loot_table_count"] = mapp(config["combat_loot_tables"]) ? sizeof(config["combat_loot_tables"]) : 0;
    report["resource_node_count"] = mapp(config["resource_nodes"]) ? sizeof(config["resource_nodes"]) : 0;
    report["shop_count"] = mapp(config["shops"]) ? sizeof(config["shops"]) : 0;
    report["reward_table_count"] = mapp(config["reward_tables"]) ? sizeof(config["reward_tables"]) : 0;
    report["reputation_source_count"] = mapp(config["reputation_sources"]) ? sizeof(config["reputation_sources"]) : 0;
    report["exploration_chest_count"] = mapp(config["exploration_chests"]) ? sizeof(config["exploration_chests"]): 0;
    report["room_entity_count"] = mapp(config["room_entities"]) ? sizeof(config["room_entities"]) : 0;

    if (report["region_package_count"] == 0)
        add_violation(report, "readiness",
            "no registered region packages; world is awaiting map design");

    validate_region_packages(report, config);
    validate_areas(report, config);
    validate_rooms(report, config, full_mode);
    validate_entities(report, config, full_mode);
    validate_npc_templates(report, config);
    validate_item_templates(report, config);
    validate_combat_loot_tables(report, config);
    validate_quest_definitions(report, config);
    validate_skill_teachers(report, config);
    validate_resource_nodes(report, config);
    validate_shops(report, config);
    validate_reward_tables(report, config);
    validate_reputation_sources(report, config);
    validate_exploration_chests(report, config);
    validate_world_events(report, config);
    validate_active_content_sections(report, config);

    if (full_mode && report["checked_rooms"] != report["declared_rooms"])
        add_violation(report, "coverage", sprintf(
            "full room coverage mismatch: checked=%d declared=%d",
            report["checked_rooms"], report["declared_rooms"]));
    if (full_mode && report["checked_entity_rooms"] != report["declared_entity_rooms"])
        add_violation(report, "coverage", sprintf(
            "full entity-room coverage mismatch: checked=%d declared=%d",
            report["checked_entity_rooms"], report["declared_entity_rooms"]));
    if (full_mode && report["skipped"] != 0)
        add_violation(report, "coverage", sprintf(
            "full validation skipped=%d", report["skipped"]));

    report["ok"] = sizeof(report["violations"] || ({ })) == 0 ? 1 : 0;
    return report;
}

varargs mapping query_validation_report(string mode)
{
    mapping report;
    mixed err;

    content_validation_depth++;
    err = catch(report = build_validation_report(mode));
    content_validation_depth--;
    if (err || !mapp(report)) {
        return ([
            "ok": 0,
            "mode": stringp(mode) && mode != "" ? mode : "full",
            "validation_errors": ({ sprintf("content validation failed: %O", err) }),
            "violations": ({ ([
                "category": "runtime",
                "message": sprintf("content validation failed: %O", err),
                "source": "/daemons/services/world/content_registry_d.c",
                "error_code": "CONTENT_VALIDATION_FAILED",
            ]) }),
        ]);
    }
    return report;
}
