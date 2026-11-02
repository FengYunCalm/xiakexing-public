/**
 * @file    simul_efun_room_mapgrid_xk.c
 * @brief   地图网格 payload / read-model / ack 缓存辅助（自 simul_efun_room_meta_xk.c 拆分）
 */

#pragma strict_types

#include <system/room_meta_helpers.h>
#include <system/services.h>
#include <system/xk_protocol.h>

#define XK_PROTOCOL_CORE "/kernel/framework/xk_protocol_core"

inherit "/lib/std/utils/util_owner_projection";

// ---- 前向声明 ----
public string normalize_grid_direction(string dir);
private int has_exit_direction(mapping exits, string canonical_dir);
private string grid_cell_label(mapping exits, string canonical_dir, string visible_label);
private string *build_compact_map_rows(mapping exits);
public int is_grid_direction(string canonical_dir);
private int *grid_direction_delta(string canonical_dir);
private string cell_key(int x, int y);
private string sanitize_map_label(string label);
public string *build_special_exit_legend(mapping exits);
varargs int xk_send_map_grid(object me, string title, string zone,
    mapping exits, int force_snapshot, mapping response_context);
private void record_map_room_data(mapping rooms_by_cell, mapping quest_markers,
    mapping quest_marker_details,
    object player, object room, int x, int y);
private void record_map_room_geometry(mapping geometry_state, mapping room_snapshot, int x, int y);
private string query_map_cell_for_room(mapping rooms_by_cell, string room_path);
private void record_global_quest_provider_markers(mapping rooms_by_cell,
    mapping quest_markers, mapping quest_marker_details, object player,
    mapping quest_ids_by_provider, mapping map_marker_provider_ids);
private void record_player_room_markers(mapping rooms_by_cell,
    mapping quest_markers, mapping quest_marker_details, object player,
    mapping quest_ids_by_provider, mapping room_provider_ids);
private void record_player_indoor_entry_markers(mixed *indoor_entry_candidates,
    mapping quest_markers, mapping quest_marker_details, object player,
    mapping quest_ids_by_provider);
private int record_map_cell_projection(mapping geometry_state,
    mapping room_snapshot, int x, int y, string *path);
private int enqueue_map_room(mixed *queue, int tail, mapping visited,
    mapping room_snapshot, int x, int y, string *path);
private string map_geometry_cache_key(object center_room, mapping center_exits);
private int is_cached_map_geometry_valid(mapping geometry);
public mapping copy_string_array_mapping(mapping source);
public mixed *copy_indoor_entry_candidates(mixed *candidates);
private mapping copy_map_geometry(mapping geometry);
private mixed *copy_mapping_array(mixed *values);
public mapping copy_map_grid_payload(mapping payload);
private void cache_map_geometry(string cache_key, mapping geometry);
private mapping build_room_radius_geometry(object center_room, mapping center_exits);
public mapping query_room_radius_geometry(object center_room, mapping center_exits);
private mapping build_player_quest_marker_data(mapping radius_geometry, object player);
private mapping build_room_radius_data(object center_room, mapping center_exits, object player);
varargs mapping xk_build_map_grid_payload(string title, string zone, mapping exits, object center_room, object player);

#define MAP_GRID_RADIUS 20
#define MAP_GRID_DIAMETER (MAP_GRID_RADIUS * 2 + 1)
#define MAP_GRID_GEOMETRY_CACHE_LIMIT 128
#define MAP_GRID_ACK_TTL 5
#define MAP_ROOM_LOAD_FAILURE_CACHE_LIMIT 256
#define MAP_ROOM_LOAD_FAILURE_CACHE_TTL 300
#define INDOOR_QUEST_SCAN_MAX_STEPS 20
#define INDOOR_QUEST_SCAN_MAX_ROOMS 80
#define EXIT_PROJECTION_CACHE_LIMIT 8

nosave private mapping map_radius_geometry_cache = ([]);


public string normalize_grid_direction(string dir)
{
    string normalized;

    if (!stringp(dir) || dir == "")
        return "";

    normalized = lower_case(dir);
    switch (normalized) {
        case "n":
            return "north";
        case "s":
            return "south";
        case "e":
            return "east";
        case "w":
            return "west";
        case "ne":
            return "northeast";
        case "nw":
            return "northwest";
        case "se":
            return "southeast";
        case "sw":
            return "southwest";
        case "u":
            return "up";
        case "d":
            return "down";
        default:
            return normalized;
    }
}

private int has_exit_direction(mapping exits, string canonical_dir)
{
    string *dirs;

    if (!mapp(exits) || !stringp(canonical_dir) || canonical_dir == "")
        return 0;

    dirs = keys(exits);
    foreach (string dir in dirs) {
        if (normalize_grid_direction(dir) == canonical_dir)
            return 1;
    }

    return 0;
}

private string grid_cell_label(mapping exits, string canonical_dir, string visible_label)
{
    if (!has_exit_direction(exits, canonical_dir))
        return "·";

    return visible_label;
}

private string *build_compact_map_rows(mapping exits)
{
    return ({
        sprintf("%s %s %s",
            grid_cell_label(exits, "northwest", "西北"),
            grid_cell_label(exits, "north", "北"),
            grid_cell_label(exits, "northeast", "东北")),
        sprintf("%s 你 %s",
            grid_cell_label(exits, "west", "西"),
            grid_cell_label(exits, "east", "东")),
        sprintf("%s %s %s",
            grid_cell_label(exits, "southwest", "西南"),
            grid_cell_label(exits, "south", "南"),
            grid_cell_label(exits, "southeast", "东南")),
    });
}

public int is_grid_direction(string canonical_dir)
{
    return member_array(canonical_dir, ({
        "northwest", "north", "northeast",
        "west", "east",
        "southwest", "south", "southeast",
    })) != -1;
}

private int *grid_direction_delta(string canonical_dir)
{
    switch (canonical_dir) {
        case "north":
            return ({ 0, -1 });
        case "south":
            return ({ 0, 1 });
        case "east":
            return ({ 1, 0 });
        case "west":
            return ({ -1, 0 });
        case "northeast":
            return ({ 1, -1 });
        case "northwest":
            return ({ -1, -1 });
        case "southeast":
            return ({ 1, 1 });
        case "southwest":
            return ({ -1, 1 });
        default:
            return ({ 0, 0 });
    }
}

private string cell_key(int x, int y)
{
    return sprintf("%d,%d", x, y);
}

private string sanitize_map_label(string label)
{
    if (!stringp(label) || label == "")
        return "";

    label = replace_string(label, "\r", " ");
    label = replace_string(label, "\n", " ");
    label = replace_string(label, "\t", " ");
    return label;
}

public string *build_special_exit_legend(mapping exits)
{
    string *dirs;
    string *legend_buffer;
    string *legend;
    int legend_count;

    if (!mapp(exits) || sizeof(exits) == 0)
        return ({ });

    dirs = keys(exits);
    legend_buffer = allocate(sizeof(dirs));
    legend_count = 0;
    foreach (string dir in dirs) {
        string canonical_dir;
        string room_name;

        canonical_dir = normalize_grid_direction(dir);
        if (is_grid_direction(canonical_dir))
            continue;

        room_name = ROOM_TITLE_XK_HELPER->resolve_room_display_name(exits[dir]);
        legend_buffer[legend_count++] = sprintf("%s: %s", dir, room_name);
    }

    legend = legend_count > 0 ? legend_buffer[0..legend_count - 1] : ({ });
    return legend;
}

varargs int xk_send_map_grid(object me, string title, string zone,
    mapping exits, int force_snapshot, mapping response_context)
{
    mapping owner_snapshot;
    mapping payload;
    mapping read_model;
    object room;
    mapping snapshot;
    string cache_key;
    string input_token;
    string worker_state;
    int artifact_result;
    int fallback_started_at;

    if (!me || !interactive(me))
        return 0;

    room = environment(me);
    cache_key = ROOM_MAPREAD_XK_HELPER->map_grid_ack_cache_key(title, zone, room, exits, me);
    worker_state = ROOM_MAPREAD_XK_HELPER->query_recent_map_grid_ack_worker_state(me, cache_key);
    if (!force_snapshot && worker_state != "") {
        call_other(XK_PROTOCOL_CORE, "xk_send", me, XK_SYST, ([
            "content": "地图已是最新。",
            "level": "info",
            "reliability": "important",
            "priority": "normal",
            "read_side_worker": 0,
            "read_side_worker_state": worker_state,
        ]), response_context);
        return 1;
    }

    input_token = ROOM_MAPREAD_XK_HELPER->xk_query_map_grid_read_model_token(me, title, zone, exits, room);
    read_model = input_token != ""
        ? me->query_owner_read_model("map_grid", input_token, 0) : 0;
    if (mapp(read_model) && read_model["state"] == "ready"
        && mapp(read_model["view"]) && mapp(read_model["preencoded"])) {
        worker_state = ROOM_MAPREAD_XK_HELPER->normalize_map_worker_state(
            read_model["view"]["read_side_worker_state"]);
        artifact_result = call_other(XK_PROTOCOL_CORE,
            "xk_send_preencoded_artifact", me, XK_MAPS,
            read_model["preencoded"], response_context);
        /* L-36：0=未填充可安全回退；1/-1（已填充待对账）都已投递，不得重发。 */
        if (intp(artifact_result) && artifact_result != 0) {
            ROOM_MAPREAD_XK_HELPER->remember_recent_map_grid_sent(me, cache_key, worker_state);
            return 1;
        }
    }

    worker_state = "main_thread";
    fallback_started_at = perf_counter_ns();
    snapshot = ROOM_MAPREAD_XK_HELPER->xk_query_map_grid_read_model_snapshot(me, title, zone, exits, room);
    if (mapp(snapshot)) {
        snapshot["read_side_worker"] = 0;
        snapshot["read_side_worker_state"] = worker_state;
        payload = project_map_grid_payload_snapshot(snapshot);
        owner_snapshot = copy(snapshot);
        owner_snapshot["projection_kind"] = "map_grid";
        owner_snapshot["read_side_worker"] = 1;
        owner_snapshot["read_side_worker_state"] = "ready_cached";
        me->submit_owner_read_model("map_grid", owner_snapshot,
            snapshot["input_token"]);
    } else {
        payload = ROOM_MAPREAD_XK_HELPER->attach_map_worker_state(
            xk_build_map_grid_payload(title, zone, exits, room, me),
            worker_state);
    }
    me->record_owner_protocol_sync_fallback("map_grid",
        mapp(snapshot) ? "read_model_not_ready" : "non_content_geometry");
    me->record_owner_protocol_projection_timing("sync_fallback", ([
        "projection_kind": "map_grid",
    ]), perf_counter_ns() - fallback_started_at);
    if (call_other(XK_PROTOCOL_CORE, "xk_send", me, XK_MAPS, payload,
        response_context)) {
        ROOM_MAPREAD_XK_HELPER->remember_recent_map_grid_sent(me, cache_key, worker_state);
        return 1;
    }
    return 0;
}

private void record_map_room_data(mapping rooms_by_cell, mapping quest_markers,
    mapping quest_marker_details,
    object player, object room, int x, int y)
{
    string key;
    string room_path;
    string *available_quest_ids;
    mapping exits;

    if (!mapp(rooms_by_cell) || !mapp(quest_markers) || !mapp(quest_marker_details) || !objectp(room))
        return;
    if (x < -MAP_GRID_RADIUS || x > MAP_GRID_RADIUS
        || y < -MAP_GRID_RADIUS || y > MAP_GRID_RADIUS)
        return;

    key = cell_key(x, y);
    room_path = base_name(room);
    rooms_by_cell[key] = room_path;
    available_quest_ids = ROOM_MARKER_XK_HELPER->query_room_marker_quest_ids(player, room_path);
    ROOM_MARKER_XK_HELPER->record_quest_marker(quest_markers, quest_marker_details, key, room_path, room_path,
        "", available_quest_ids, "任务地点");

    exits = room->query("exits");
    if (!mapp(exits) || sizeof(exits) == 0)
        return;

    foreach (string dir, mixed target_room_ref in exits) {
        string canonical_dir;
        string target_room_path;
        mapping target_marker;
        string *target_quest_ids;
        string marker_target_room_path;

        if (!stringp(target_room_ref))
            continue;
        target_room_path = target_room_ref;
        canonical_dir = normalize_grid_direction(dir);
        if (!stringp(target_room_path) || target_room_path == "")
            continue;
        if (!ROOM_MARKER_XK_HELPER->should_scan_indoor_entry_exit(room_path, target_room_path, canonical_dir))
            continue;
        target_marker = ROOM_MARKER_XK_HELPER->query_indoor_entry_quest_marker(player, room_path, target_room_path);
        target_quest_ids = arrayp(target_marker["quest_ids"]) ? target_marker["quest_ids"] : ({ });
        marker_target_room_path = stringp(target_marker["target_room_id"])
            ? target_marker["target_room_id"] : target_room_path;
        ROOM_MARKER_XK_HELPER->record_quest_marker(quest_markers, quest_marker_details, key, room_path, marker_target_room_path,
            dir, target_quest_ids, "任务入口");
    }
}

private void record_map_room_geometry(mapping geometry_state, mapping room_snapshot, int x, int y)
{
    string key;
    string room_path;
    mapping exits;

    if (!mapp(geometry_state) || !ROOM_EXITS_XK_HELPER->is_complete_map_room_snapshot(room_snapshot))
        return;
    if (x < -MAP_GRID_RADIUS || x > MAP_GRID_RADIUS
        || y < -MAP_GRID_RADIUS || y > MAP_GRID_RADIUS)
        return;

    key = cell_key(x, y);
    room_path = room_snapshot["room_id"];
    geometry_state["rooms_by_cell"][key] = room_path;

    exits = room_snapshot["exits"];
    if (!mapp(exits) || sizeof(exits) == 0)
        return;

    foreach (string dir, mixed target_room_ref in exits) {
        string canonical_dir;
        string target_room_path;
        mixed *candidates;

        if (!stringp(target_room_ref))
            continue;
        target_room_path = target_room_ref;
        canonical_dir = normalize_grid_direction(dir);
        if (!ROOM_MARKER_XK_HELPER->should_scan_indoor_entry_exit(room_path, target_room_path, canonical_dir))
            continue;

        candidates = arrayp(geometry_state["indoor_entry_candidates"])
            ? geometry_state["indoor_entry_candidates"] : ({ });
        candidates += ({ ([
            "cell": key,
            "entry_room_id": room_path,
            "target_room_id": target_room_path,
            "entry_direction": dir,
        ]) });
        geometry_state["indoor_entry_candidates"] = candidates;
    }
}

private string query_map_cell_for_room(mapping rooms_by_cell, string room_path)
{
    string key;
    string projected_room_path;

    if (!mapp(rooms_by_cell) || !stringp(room_path) || room_path == "")
        return "";

    foreach (key in keys(rooms_by_cell)) {
        if (rooms_by_cell[key] == room_path)
            return key;
    }

    projected_room_path = CONTENT_REGISTRY_D->query_indoor_root_room_path(room_path);
    if (projected_room_path == "")
        return "";

    foreach (key in keys(rooms_by_cell)) {
        if (rooms_by_cell[key] == projected_room_path)
            return key;
    }
    return "";
}

private void record_global_quest_provider_markers(mapping rooms_by_cell,
    mapping quest_markers, mapping quest_marker_details, object player,
    mapping quest_ids_by_provider, mapping map_marker_provider_ids)
{
    string key;
    string room_path;
    string *provider_ids;
    string provider_id;
    string *marker_quest_ids;
    string entry_room_id;

    if (!mapp(rooms_by_cell) || !mapp(quest_markers) || !mapp(quest_marker_details)
        || !objectp(player))
        return;

    foreach (key, room_path in rooms_by_cell) {
        if (!stringp(key) || key == "" || !stringp(room_path) || room_path == "")
            continue;
        provider_ids = mapp(map_marker_provider_ids)
            ? map_marker_provider_ids[room_path]
            : CONTENT_REGISTRY_D->query_map_marker_quest_provider_ids(room_path);
        if (!arrayp(provider_ids) || sizeof(provider_ids) == 0)
            continue;
        entry_room_id = room_path;
        foreach (provider_id in provider_ids) {
            if (!stringp(provider_id) || provider_id == "")
                continue;
            if (!NPC_SCHEDULE_D->query_provider_present_in_room(
                provider_id, room_path))
                continue;
            marker_quest_ids = ROOM_MARKER_XK_HELPER->query_provider_marker_quest_ids(player, provider_id,
                quest_ids_by_provider);
            if (sizeof(marker_quest_ids) == 0)
                continue;
            ROOM_MARKER_XK_HELPER->record_quest_marker(quest_markers, quest_marker_details, key, entry_room_id, room_path,
                "", marker_quest_ids, "任务发布点");
        }
    }
}

private void record_player_room_markers(mapping rooms_by_cell,
    mapping quest_markers, mapping quest_marker_details, object player,
    mapping quest_ids_by_provider, mapping room_provider_ids)
{
    string key;
    string room_path;
    string *available_quest_ids;

    if (!mapp(rooms_by_cell) || !mapp(quest_markers) || !mapp(quest_marker_details)
        || !objectp(player))
        return;

    foreach (key, room_path in rooms_by_cell) {
        if (!stringp(room_path) || room_path == "")
            continue;
        available_quest_ids = ROOM_MARKER_XK_HELPER->query_room_marker_quest_ids(player, room_path,
            quest_ids_by_provider, room_provider_ids);
        ROOM_MARKER_XK_HELPER->record_quest_marker(quest_markers, quest_marker_details, key, room_path,
            room_path, "", available_quest_ids, "任务地点");
    }
}

private void record_player_indoor_entry_markers(mixed *indoor_entry_candidates,
    mapping quest_markers, mapping quest_marker_details, object player,
    mapping quest_ids_by_provider)
{
    mapping candidate;
    mapping target_marker;
    string *target_quest_ids;
    string marker_target_room_path;

    if (!arrayp(indoor_entry_candidates) || !mapp(quest_markers)
        || !mapp(quest_marker_details) || !objectp(player))
        return;

    foreach (candidate in indoor_entry_candidates) {
        if (!mapp(candidate))
            continue;
        if (arrayp(candidate["room_candidates"])) {
            target_marker = ROOM_MARKER_XK_HELPER->query_indoor_entry_quest_marker_from_topology(
                candidate, quest_ids_by_provider);
        } else {
            target_marker = ROOM_MARKER_XK_HELPER->query_indoor_entry_quest_marker(player,
                candidate["entry_room_id"], candidate["target_room_id"],
                quest_ids_by_provider);
        }
        target_quest_ids = arrayp(target_marker["quest_ids"]) ? target_marker["quest_ids"] : ({ });
        marker_target_room_path = stringp(target_marker["target_room_id"])
            ? target_marker["target_room_id"] : candidate["target_room_id"];
        ROOM_MARKER_XK_HELPER->record_quest_marker(quest_markers, quest_marker_details, candidate["cell"],
            candidate["entry_room_id"], marker_target_room_path,
            candidate["entry_direction"], target_quest_ids, "任务入口");
    }
}

private int record_map_cell_projection(mapping geometry_state,
    mapping room_snapshot, int x, int y, string *path)
{
    mapping labels_by_cell;
    mapping paths_by_cell;
    mapping rooms_by_cell;
    string key;
    string label;
    string room_path;

    if (!mapp(geometry_state)
        || !ROOM_EXITS_XK_HELPER->is_complete_map_room_snapshot(room_snapshot))
        return 0;
    if (x < -MAP_GRID_RADIUS || x > MAP_GRID_RADIUS
        || y < -MAP_GRID_RADIUS || y > MAP_GRID_RADIUS)
        return 0;

    labels_by_cell = geometry_state["labels_by_cell"];
    paths_by_cell = geometry_state["paths_by_cell"];
    rooms_by_cell = geometry_state["rooms_by_cell"];
    if (!mapp(labels_by_cell) || !mapp(paths_by_cell) || !mapp(rooms_by_cell))
        return 0;

    key = cell_key(x, y);
    if (stringp(rooms_by_cell[key]) && rooms_by_cell[key] != "")
        return 0;

    room_path = room_snapshot["room_id"];
    label = x == 0 && y == 0 ? "你" : sanitize_map_label(room_snapshot["title"]);
    if (!stringp(room_path) || room_path == "" || !stringp(label) || label == "")
        return 0;

    /* The three values are one projection choice. A collision keeps the first
     * BFS candidate and rejects the whole later candidate, so a label can
     * never point at a different room or path. */
    rooms_by_cell[key] = room_path;
    labels_by_cell[key] = label;
    if (arrayp(path) && sizeof(path) > 0)
        paths_by_cell[key] = path + ({ });
    return 1;
}

private int enqueue_map_room(mixed *queue, int tail, mapping visited,
    mapping room_snapshot, int x, int y, string *path)
{
    string room_key;

    if (!arrayp(queue))
        return tail;
    if (!ROOM_EXITS_XK_HELPER->is_complete_map_room_snapshot(room_snapshot))
        return tail;
    if (x < -MAP_GRID_RADIUS || x > MAP_GRID_RADIUS
        || y < -MAP_GRID_RADIUS || y > MAP_GRID_RADIUS)
        return tail;
    if (tail < 0 || tail >= sizeof(queue))
        return tail;

    room_key = room_snapshot["room_id"];
    if (visited[room_key])
        return tail;

    visited[room_key] = 1;
    queue[tail] = ([
        "room_snapshot": room_snapshot,
        "x": x,
        "y": y,
        "path": arrayp(path) ? path : ({ }),
    ]);
    return tail + 1;
}

private string map_geometry_cache_key(object center_room, mapping center_exits)
{
    if (!objectp(center_room))
        return "";
    return sprintf("%s#%d#%s",
        base_name(center_room),
        CONTENT_REGISTRY_D->query_content_generation(),
        ROOM_EXITS_XK_HELPER->map_exits_signature(center_exits));
}

private int is_cached_map_geometry_valid(mapping geometry)
{
    string *room_paths;
    mapping snapshot;

    if (!mapp(geometry) || !mapp(geometry["rooms"]))
        return 0;
    room_paths = values(geometry["rooms"]);
    foreach (string room_path in room_paths) {
        snapshot = ROOM_EXITS_XK_HELPER->query_map_room_snapshot(room_path, 0, 0);
        if (!ROOM_EXITS_XK_HELPER->is_complete_map_room_snapshot(snapshot)
            || snapshot["source"] != "content")
            return 0;
    }
    return 1;
}

public mapping copy_string_array_mapping(mapping source)
{
    mapping result;
    string key;
    string *values;

    result = ([]);
    if (!mapp(source))
        return result;
    foreach (key, values in source) {
        if (stringp(key) && arrayp(values))
            result[key] = values + ({ });
    }
    return result;
}

public mixed *copy_indoor_entry_candidates(mixed *candidates)
{
    mixed *result;
    mixed *room_candidates;
    mixed *room_result;
    mapping candidate;
    mapping room_candidate;
    int index;
    int room_index;

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
        room_candidates = arrayp(candidate["room_candidates"])
            ? candidate["room_candidates"] : ({ });
        room_result = allocate(sizeof(room_candidates));
        for (room_index = 0; room_index < sizeof(room_candidates); room_index++) {
            room_candidate = room_candidates[room_index];
            if (!mapp(room_candidate)) {
                room_result[room_index] = room_candidate;
                continue;
            }
            room_result[room_index] = copy(room_candidate);
            room_result[room_index]["provider_ids"] =
                arrayp(room_candidate["provider_ids"])
                    ? room_candidate["provider_ids"] + ({ }) : ({ });
        }
        result[index]["room_candidates"] = room_result;
    }
    return result;
}

private mapping copy_map_geometry(mapping geometry)
{
    if (!mapp(geometry))
        return ([]);

    return ([
        "rows": arrayp(geometry["rows"]) ? geometry["rows"] + ({ }) : ({ }),
        "paths": mapp(geometry["paths"]) ? copy(geometry["paths"]) : ([ ]),
        "rooms": mapp(geometry["rooms"]) ? copy(geometry["rooms"]) : ([ ]),
        "width": intp(geometry["width"]) ? geometry["width"] : MAP_GRID_DIAMETER,
        "height": intp(geometry["height"]) ? geometry["height"] : MAP_GRID_DIAMETER,
        "room_provider_ids": mapp(geometry["room_provider_ids"])
            ? copy_string_array_mapping(geometry["room_provider_ids"]) : 0,
        "map_marker_provider_ids": mapp(geometry["map_marker_provider_ids"])
            ? copy_string_array_mapping(geometry["map_marker_provider_ids"]) : 0,
        "indoor_entry_candidates": copy_indoor_entry_candidates(
            geometry["indoor_entry_candidates"]),
        "cacheable": geometry["cacheable"] ? 1 : 0,
    ]);
}

private mixed *copy_mapping_array(mixed *values)
{
    mixed *result;
    int index;

    if (!arrayp(values))
        return ({ });

    result = allocate(sizeof(values));
    for (index = 0; index < sizeof(values); index++)
        result[index] = mapp(values[index]) ? copy(values[index]) : values[index];
    return result;
}

public mapping copy_map_grid_payload(mapping payload)
{
    mapping result;

    if (!mapp(payload))
        return ([ ]);

    result = ([
        "rows": arrayp(payload["rows"]) ? payload["rows"] + ({ }) : ({ }),
        "paths": mapp(payload["paths"]) ? copy(payload["paths"]) : ([ ]),
        "rooms": mapp(payload["rooms"]) ? copy(payload["rooms"]) : ([ ]),
        "quest_markers": arrayp(payload["quest_markers"]) ? payload["quest_markers"] + ({ }) : ({ }),
        "quest_marker_details": copy_mapping_array(payload["quest_marker_details"]),
        "width": payload["width"],
        "height": payload["height"],
    ]);
    if (stringp(payload["title"]) && payload["title"] != "")
        result["title"] = payload["title"];
    if (stringp(payload["zone"]) && payload["zone"] != "")
        result["zone"] = payload["zone"];
    if (arrayp(payload["legend"]) && sizeof(payload["legend"]) > 0)
        result["legend"] = payload["legend"] + ({ });
    if (stringp(payload["region_id"]) && payload["region_id"] != "")
        result["region_id"] = payload["region_id"];
    if (stringp(payload["map_context_id"]) && payload["map_context_id"] != "")
        result["map_context_id"] = payload["map_context_id"];
    if (stringp(payload["map_layer"]) && payload["map_layer"] != "")
        result["map_layer"] = payload["map_layer"];
    if (stringp(payload["submap_id"]) && payload["submap_id"] != "")
        result["submap_id"] = payload["submap_id"];
    return result;
}

private void cache_map_geometry(string cache_key, mapping geometry)
{
    string *keys;

    if (!stringp(cache_key) || cache_key == "" || !mapp(geometry))
        return;
    if (!mapp(map_radius_geometry_cache))
        map_radius_geometry_cache = ([]);

    if (sizeof(map_radius_geometry_cache) >= MAP_GRID_GEOMETRY_CACHE_LIMIT) {
        keys = keys(map_radius_geometry_cache);
        if (sizeof(keys) > 0)
            map_delete(map_radius_geometry_cache, keys[0]);
    }
    map_radius_geometry_cache[cache_key] = copy_map_geometry(geometry);
}

private mapping build_room_radius_geometry(object center_room, mapping center_exits)
{
    mixed *queue;
    mapping center_snapshot;
    mapping visited;
    mapping labels_by_cell;
    mapping paths_by_cell;
    mapping geometry_state;
    mapping marker_topology;
    string *room_paths;
    string *rows;
    int index;
    int tail;

    if (!objectp(center_room)) {
        return ([
            "rows": build_compact_map_rows(center_exits),
            "paths": ([ ]),
            "rooms": ([ ]),
            "quest_markers": ({ }),
            "quest_marker_details": ({ }),
            "width": 3,
            "height": 3,
            "cacheable": 0,
        ]);
    }

    center_snapshot = ROOM_EXITS_XK_HELPER->query_map_room_snapshot(base_name(center_room), center_room, center_exits);
    if (!ROOM_EXITS_XK_HELPER->is_complete_map_room_snapshot(center_snapshot)) {
        return ([
            "rows": build_compact_map_rows(center_exits),
            "paths": ([ ]),
            "rooms": ([ ]),
            "quest_markers": ({ }),
            "quest_marker_details": ({ }),
            "width": 3,
            "height": 3,
            "cacheable": 0,
        ]);
    }

    queue = allocate(MAP_GRID_DIAMETER * MAP_GRID_DIAMETER);
    visited = ([ ]);
    labels_by_cell = ([ ]);
    paths_by_cell = ([ ]);
    geometry_state = ([
        "rooms_by_cell": ([ ]),
        "labels_by_cell": labels_by_cell,
        "paths_by_cell": paths_by_cell,
        "indoor_entry_candidates": ({ }),
        "cacheable": 1,
    ]);
    tail = enqueue_map_room(queue, 0, visited, center_snapshot, 0, 0, ({ }));

    for (index = 0; index < tail; index++) {
        mapping item;
        mapping room_snapshot;
        mapping exits;
        string *dirs;
        string *path;
        int x;
        int y;

        item = queue[index];
        room_snapshot = item["room_snapshot"];
        if (!ROOM_EXITS_XK_HELPER->is_complete_map_room_snapshot(room_snapshot))
            continue;
        x = item["x"];
        y = item["y"];
        path = arrayp(item["path"]) ? item["path"] : ({ });
        if (room_snapshot["source"] != "content"
            && ((x != 0 || y != 0) || !room_snapshot["content_backed"]))
            geometry_state["cacheable"] = 0;
        if (!record_map_cell_projection(geometry_state, room_snapshot, x, y, path))
            continue;
        record_map_room_geometry(geometry_state, room_snapshot, x, y);

        exits = room_snapshot["exits"];
        if (!mapp(exits) || sizeof(exits) == 0)
            continue;

        dirs = keys(exits);
        foreach (string dir in dirs) {
            string canonical_dir;
            int *delta;
            mapping next_snapshot;
            int next_x;
            int next_y;
            string *next_path;

            canonical_dir = normalize_grid_direction(dir);
            if (!is_grid_direction(canonical_dir))
                continue;

            delta = grid_direction_delta(canonical_dir);
            next_x = x + delta[0];
            next_y = y + delta[1];
            next_snapshot = ROOM_EXITS_XK_HELPER->query_map_room_snapshot(exits[dir], 0, 0);
            if (!ROOM_EXITS_XK_HELPER->is_complete_map_room_snapshot(next_snapshot))
                continue;

            next_path = path + ({ canonical_dir });
            tail = enqueue_map_room(queue, tail, visited, next_snapshot,
                next_x, next_y, next_path);
        }
    }

    rows = allocate(MAP_GRID_DIAMETER);
    for (int row_index = 0; row_index < MAP_GRID_DIAMETER; row_index++) {
        string *cells;
        int row;

        row = row_index - MAP_GRID_RADIUS;
        cells = allocate(MAP_GRID_DIAMETER);
        for (int col_index = 0; col_index < MAP_GRID_DIAMETER; col_index++) {
            int col;

            col = col_index - MAP_GRID_RADIUS;
            cells[col_index] = labels_by_cell[cell_key(col, row)] || "·";
        }
        rows[row_index] = implode(cells, " ");
    }

    if (geometry_state["cacheable"]) {
        room_paths = values(geometry_state["rooms_by_cell"]);
        marker_topology = CONTENT_REGISTRY_D->query_map_quest_marker_topology(
            room_paths, geometry_state["indoor_entry_candidates"],
            INDOOR_QUEST_SCAN_MAX_STEPS, INDOOR_QUEST_SCAN_MAX_ROOMS);
    }

    return ([
        "rows": rows,
        "paths": paths_by_cell,
        "rooms": geometry_state["rooms_by_cell"],
        "width": MAP_GRID_DIAMETER,
        "height": MAP_GRID_DIAMETER,
        "room_provider_ids": mapp(marker_topology)
            ? marker_topology["room_provider_ids"] : 0,
        "map_marker_provider_ids": mapp(marker_topology)
            ? marker_topology["map_marker_provider_ids"] : 0,
        "indoor_entry_candidates": mapp(marker_topology)
            ? marker_topology["indoor_entry_candidates"]
            : geometry_state["indoor_entry_candidates"],
        "cacheable": geometry_state["cacheable"],
    ]);
}

public mapping query_room_radius_geometry(object center_room, mapping center_exits)
{
    string cache_key;
    mapping geometry;

    cache_key = map_geometry_cache_key(center_room, center_exits);
    if (cache_key != "" && mapp(map_radius_geometry_cache)
        && mapp(map_radius_geometry_cache[cache_key])) {
        if (is_cached_map_geometry_valid(map_radius_geometry_cache[cache_key]))
            return copy_map_geometry(map_radius_geometry_cache[cache_key]);
        map_delete(map_radius_geometry_cache, cache_key);
    }

    geometry = build_room_radius_geometry(center_room, center_exits);
    if (geometry["cacheable"])
        cache_map_geometry(cache_key, geometry);
    return copy_map_geometry(geometry);
}

private mapping build_player_quest_marker_data(mapping radius_geometry, object player)
{
    mapping rooms_by_cell;
    mapping quest_markers;
    mapping quest_marker_details;
    mapping quest_ids_by_provider;
    mapping room_provider_ids;
    mapping map_marker_provider_ids;

    rooms_by_cell = mapp(radius_geometry["rooms"]) ? radius_geometry["rooms"] : ([ ]);
    quest_markers = ([ ]);
    quest_marker_details = ([ ]);
    quest_ids_by_provider = QUEST_D->query_map_marker_quest_ids_by_provider(player);
    room_provider_ids = radius_geometry["room_provider_ids"];
    map_marker_provider_ids = radius_geometry["map_marker_provider_ids"];

    record_player_room_markers(rooms_by_cell, quest_markers, quest_marker_details,
        player, quest_ids_by_provider, room_provider_ids);
    record_player_indoor_entry_markers(radius_geometry["indoor_entry_candidates"],
        quest_markers, quest_marker_details, player, quest_ids_by_provider);
    record_global_quest_provider_markers(rooms_by_cell, quest_markers,
        quest_marker_details, player, quest_ids_by_provider,
        map_marker_provider_ids);

    return ([
        "quest_markers": keys(quest_markers),
        "quest_marker_details": ROOM_MARKER_XK_HELPER->quest_marker_details_array(quest_marker_details),
    ]);
}

private mapping build_room_radius_data(object center_room, mapping center_exits, object player)
{
    mapping radius_geometry;
    mapping marker_data;

    radius_geometry = query_room_radius_geometry(center_room, center_exits);
    marker_data = build_player_quest_marker_data(radius_geometry, player);

    return ([
        "rows": radius_geometry["rows"],
        "paths": radius_geometry["paths"],
        "rooms": radius_geometry["rooms"],
        "width": intp(radius_geometry["width"]) ? radius_geometry["width"] : 3,
        "height": intp(radius_geometry["height"]) ? radius_geometry["height"] : 3,
        "quest_markers": marker_data["quest_markers"],
        "quest_marker_details": marker_data["quest_marker_details"],
    ]);
}

varargs mapping xk_build_map_grid_payload(string title, string zone, mapping exits, object center_room, object player)
{
    mapping payload;
    mapping radius_data;
    string *legend;

    radius_data = build_room_radius_data(center_room, exits, player);
    payload = ([
        "rows": radius_data["rows"],
        "paths": radius_data["paths"],
        "rooms": radius_data["rooms"],
        "quest_markers": radius_data["quest_markers"],
        "quest_marker_details": radius_data["quest_marker_details"],
        "width": radius_data["width"],
        "height": radius_data["height"],
    ]);
    if (stringp(title) && title != "")
        payload["title"] = title;
    if (stringp(zone) && zone != "")
        payload["zone"] = zone;

    legend = build_special_exit_legend(exits);
    if (sizeof(legend) > 0)
        payload["legend"] = legend;
    return payload;
}
