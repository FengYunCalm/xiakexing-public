/**
 * @file    simul_efun_room_exits_xk.c
 * @brief   房间出口投影缓存与房间快照原语（自 simul_efun_room_meta_xk.c 拆分）
 */

#pragma strict_types

#include <system/room_meta_helpers.h>
#include <system/services.h>
#include <system/xk_protocol.h>

#define XK_PROTOCOL_CORE "/kernel/framework/xk_protocol_core"

inherit "/lib/std/utils/util_owner_projection";

// ---- 前向声明 ----
private object load_room_object(string room_path);
void xk_send_exits(object me, mapping exits);
varargs mapping xk_build_exits_payload(mapping exits, object player);
private mapping copy_exit_projection_payload(mapping payload);
private string query_exit_projection_cache_token(mapping exits, object player);
private string *without_exit_projection_cache_token(mixed order_value,
    string token);
private void forget_exit_projection_cache_entry(object player, string token);
private mapping query_cached_exit_projection(object player, string token);
private void remember_exit_projection(object player, string token, mapping payload,
    string *dependency_room_ids);
public mapping query_map_room_snapshot(string room_path, object known_room,
    mapping known_exits);
public int is_complete_map_room_snapshot(mapping snapshot);
private mapping map_room_snapshot_from_object(object room_ob, mapping known_exits);
private int map_room_snapshot_matches_content(mapping runtime_snapshot,
    mapping content_snapshot);
public string map_exits_signature(mapping exits);

#define MAP_ROOM_LOAD_FAILURE_CACHE_LIMIT 256
#define MAP_ROOM_LOAD_FAILURE_CACHE_TTL 300
#define EXIT_PROJECTION_CACHE_LIMIT 8
#define INDOOR_QUEST_SCAN_MAX_STEPS 20
#define INDOOR_QUEST_SCAN_MAX_ROOMS 80

nosave private mapping map_room_load_failure_cache = ([]);


void xk_send_exits(object me, mapping exits)
{
    if (!me || !interactive(me))
        return;

    call_other(XK_PROTOCOL_CORE, "xk_send", me, XK_EXIT,
        xk_build_exits_payload(exits, me));
}

varargs mapping xk_build_exits_payload(mapping exits, object player)
{
    string *dirs;
    string *room_paths;
    mixed *indoor_candidate_groups;
    mixed *indoor_candidates;
    mixed *topology_candidates;
    mixed *direct_room_snapshots;
    mixed *exits_buffer;
    mixed *exits_payload;
    string entry_room_path;
    mapping indoor_topology_by_exit;
    mapping indoor_topology_expected;
    mapping indoor_topology_ready;
    mapping marker_topology;
    mapping cached_projection;
    mapping payload;
    mapping projection_dependencies;
    mapping quest_ids_by_provider;
    mapping room_provider_ids;
    string cache_token;
    int index;
    int exit_count;
    int indoor_candidate_count;
    int room_path_count;
    int total_started_at;
    int total_cpu_started_at;
    int name_wall_ns;
    int name_cpu_ns;
    int marker_wall_ns;
    int marker_cpu_ns;
    int indoor_wall_ns;
    int indoor_cpu_ns;
    int name_cpu_available;
    int marker_cpu_available;
    int indoor_cpu_available;
    int projection_cacheable;
    int total_wall_ns;
    int total_cpu_ns;
    int total_cpu_finished_at;

    if (!mapp(exits) || sizeof(exits) == 0)
        return ([ "exits": ({ }) ]);

    cache_token = query_exit_projection_cache_token(exits, player);
    if (cache_token != "") {
        cached_projection = query_cached_exit_projection(player, cache_token);
        if (mapp(cached_projection))
            return cached_projection;
    }

    total_started_at = perf_counter_ns();
    total_cpu_started_at = GATEWAY_TELEMETRY_D->query_gateway_thread_cpu_time_ns();
    name_cpu_available = 1;
    marker_cpu_available = 1;
    indoor_cpu_available = 1;
    dirs = keys(exits);
    room_paths = allocate(sizeof(dirs));
    indoor_candidate_groups = allocate(sizeof(dirs));
    direct_room_snapshots = allocate(sizeof(dirs));
    indoor_topology_expected = ([]);
    indoor_topology_ready = ([]);
    projection_dependencies = ([]);
    projection_cacheable = cache_token != "";
    entry_room_path = objectp(player) && objectp(environment(player))
        ? base_name(environment(player)) : "";
    room_path_count = 0;
    for (index = 0; index < sizeof(dirs); index++) {
        string canonical_dir;
        string dir;
        string room_path;
        mapping nested_room_snapshot;
        mapping room_snapshot;
        mapping target_exits;
        mixed *candidate_buffer;
        int candidate_count;
        int indoor_topology_complete;
        int phase_cpu_finished_at;
        int phase_cpu_started_at;
        int phase_started_at;

        dir = dirs[index];
        canonical_dir = ROOM_MAPGRID_XK_HELPER->normalize_grid_direction(dir);
        room_path = exits[dir];
        if (!stringp(room_path) || room_path == "")
            continue;
        room_paths[room_path_count++] = room_path;
        phase_started_at = perf_counter_ns();
        phase_cpu_started_at = GATEWAY_TELEMETRY_D->query_gateway_thread_cpu_time_ns();
        room_snapshot = query_map_room_snapshot(room_path, 0, 0);
        phase_cpu_finished_at = GATEWAY_TELEMETRY_D->query_gateway_thread_cpu_time_ns();
        name_wall_ns += perf_counter_ns() - phase_started_at;
        if (phase_cpu_started_at >= 0 && phase_cpu_finished_at >= phase_cpu_started_at)
            name_cpu_ns += phase_cpu_finished_at - phase_cpu_started_at;
        else
            name_cpu_available = 0;
        direct_room_snapshots[index] = room_snapshot;
        if (mapp(room_snapshot) && room_snapshot["source"] == "content")
            projection_dependencies[room_path] = 1;
        else
            projection_cacheable = 0;

        if (!objectp(player) || entry_room_path == "")
            continue;
        if (ROOM_MARKER_XK_HELPER->should_scan_indoor_entry_exit(entry_room_path, room_path, canonical_dir)) {
            if (mapp(room_snapshot) && room_snapshot["source"] == "content") {
                indoor_candidate_groups[index] = ({ ([
                    "exit_index": index,
                    "entry_room_id": entry_room_path,
                    "target_room_id": room_path,
                ]) });
                indoor_topology_expected[index] = 1;
                indoor_topology_ready[index] = 1;
                indoor_candidate_count++;
            }
            continue;
        }
        if (!ROOM_MAPGRID_XK_HELPER->is_grid_direction(canonical_dir))
            continue;

        if (!mapp(room_snapshot) || room_snapshot["source"] != "content")
            continue;
        target_exits = mapp(room_snapshot["exits"])
            ? room_snapshot["exits"] : ([]);
        candidate_buffer = allocate(sizeof(target_exits));
        candidate_count = 0;
        indoor_topology_complete = 1;
        foreach (string nested_dir, mixed nested_ref in target_exits) {
            string nested_room_path;

            if (!stringp(nested_ref))
                continue;
            nested_room_path = nested_ref;
            if (!ROOM_MARKER_XK_HELPER->should_scan_indoor_entry_exit(room_path, nested_room_path,
                ROOM_MAPGRID_XK_HELPER->normalize_grid_direction(nested_dir))) {
                continue;
            }
            nested_room_snapshot = query_map_room_snapshot(nested_room_path, 0, 0);
            if (!mapp(nested_room_snapshot)
                || nested_room_snapshot["source"] != "content") {
                indoor_topology_complete = 0;
                projection_cacheable = 0;
                continue;
            }
            projection_dependencies[nested_room_path] = 1;
            candidate_buffer[candidate_count++] = ([
                "exit_index": index,
                "entry_room_id": room_path,
                "target_room_id": nested_room_path,
            ]);
        }
        if (!indoor_topology_complete)
            continue;
        indoor_candidate_groups[index] = candidate_count > 0
            ? candidate_buffer[0..candidate_count - 1] : ({ });
        indoor_topology_expected[index] = candidate_count;
        indoor_topology_ready[index] = 1;
        indoor_candidate_count += candidate_count;
    }
    room_paths = room_path_count > 0
        ? room_paths[0..room_path_count - 1] : ({ });
    indoor_candidates = allocate(indoor_candidate_count);
    indoor_candidate_count = 0;
    for (index = 0; index < sizeof(indoor_candidate_groups); index++) {
        mixed *candidate_group;

        candidate_group = arrayp(indoor_candidate_groups[index])
            ? indoor_candidate_groups[index] : ({ });
        foreach (mapping candidate in candidate_group)
            indoor_candidates[indoor_candidate_count++] = candidate;
    }
    marker_topology = objectp(player) && room_path_count > 0
        ? CONTENT_REGISTRY_D->query_map_quest_marker_topology(
            room_paths, indoor_candidates,
            INDOOR_QUEST_SCAN_MAX_STEPS, INDOOR_QUEST_SCAN_MAX_ROOMS)
        : 0;
    room_provider_ids = mapp(marker_topology)
        && mapp(marker_topology["room_provider_ids"])
        ? marker_topology["room_provider_ids"] : ([]);
    topology_candidates = mapp(marker_topology)
        && arrayp(marker_topology["indoor_entry_candidates"])
        ? marker_topology["indoor_entry_candidates"] : ({ });
    indoor_topology_by_exit = ([]);
    foreach (mapping candidate in topology_candidates) {
        mixed *candidate_group;
        int exit_index;

        if (!mapp(candidate) || !intp(candidate["exit_index"]))
            continue;
        exit_index = candidate["exit_index"];
        candidate_group = arrayp(indoor_topology_by_exit[exit_index])
            ? indoor_topology_by_exit[exit_index] : ({ });
        indoor_topology_by_exit[exit_index] = candidate_group + ({ candidate });
    }
    foreach (mixed exit_index in keys(indoor_topology_ready)) {
        mixed *candidate_group;

        candidate_group = arrayp(indoor_topology_by_exit[exit_index])
            ? indoor_topology_by_exit[exit_index] : ({ });
        if (sizeof(candidate_group) != indoor_topology_expected[exit_index]) {
            map_delete(indoor_topology_ready, exit_index);
            projection_cacheable = 0;
        }
    }
    exits_buffer = allocate(sizeof(dirs));
    quest_ids_by_provider = objectp(player)
        ? QUEST_D->query_map_marker_quest_ids_by_provider(player) : ([]);
    exit_count = 0;
    for (index = 0; index < sizeof(dirs); index++) {
        string dir;
        string canonical_dir;
        string room_path;
        string room_name;
        string *available_quest_ids;
        mapping indoor_marker;
        mapping exit_payload;
        mapping room_snapshot;
        int phase_started_at;
        int phase_cpu_started_at;
        int phase_cpu_finished_at;

        dir = dirs[index];
        canonical_dir = ROOM_MAPGRID_XK_HELPER->normalize_grid_direction(dir);
        room_path = exits[dir];
        phase_started_at = perf_counter_ns();
        phase_cpu_started_at = GATEWAY_TELEMETRY_D->query_gateway_thread_cpu_time_ns();
        room_snapshot = direct_room_snapshots[index];
        room_name = mapp(room_snapshot) && stringp(room_snapshot["title"])
            && room_snapshot["title"] != ""
                ? room_snapshot["title"] : ROOM_TITLE_XK_HELPER->resolve_room_display_name(room_path);
        phase_cpu_finished_at = GATEWAY_TELEMETRY_D->query_gateway_thread_cpu_time_ns();
        name_wall_ns += perf_counter_ns() - phase_started_at;
        if (phase_cpu_started_at >= 0 && phase_cpu_finished_at >= phase_cpu_started_at)
            name_cpu_ns += phase_cpu_finished_at - phase_cpu_started_at;
        else
            name_cpu_available = 0;

        exit_payload = ([
            "direction": dir,
            "name": room_name,
        ]);
        if (stringp(room_path) && room_path != "") {
            exit_payload["target_room_id"] = room_path;
            phase_started_at = perf_counter_ns();
            phase_cpu_started_at = GATEWAY_TELEMETRY_D->query_gateway_thread_cpu_time_ns();
            available_quest_ids = ROOM_MARKER_XK_HELPER->query_room_marker_quest_ids(player, room_path,
                quest_ids_by_provider, room_provider_ids);
            phase_cpu_finished_at = GATEWAY_TELEMETRY_D->query_gateway_thread_cpu_time_ns();
            marker_wall_ns += perf_counter_ns() - phase_started_at;
            if (phase_cpu_started_at >= 0 && phase_cpu_finished_at >= phase_cpu_started_at)
                marker_cpu_ns += phase_cpu_finished_at - phase_cpu_started_at;
            else
                marker_cpu_available = 0;
            if (sizeof(available_quest_ids) == 0 && entry_room_path != ""
                && ROOM_MARKER_XK_HELPER->should_scan_indoor_entry_exit(entry_room_path, room_path, canonical_dir)) {
                /* 非网格出口改索引直查——
                 * 按建筑根投影键取「玩家可用任务 provider」∩「楼内 provider」，
                 * 零遍历；旧实现逐房 BFS 扫整栋建筑，浪费性能且深链路漏标。 */
                phase_started_at = perf_counter_ns();
                phase_cpu_started_at = GATEWAY_TELEMETRY_D->query_gateway_thread_cpu_time_ns();
                available_quest_ids = ROOM_MARKER_XK_HELPER->query_indoor_exit_quest_ids_by_index(
                    room_path, quest_ids_by_provider);
                phase_cpu_finished_at = GATEWAY_TELEMETRY_D->query_gateway_thread_cpu_time_ns();
                indoor_wall_ns += perf_counter_ns() - phase_started_at;
                if (phase_cpu_started_at >= 0 && phase_cpu_finished_at >= phase_cpu_started_at)
                    indoor_cpu_ns += phase_cpu_finished_at - phase_cpu_started_at;
                else
                    indoor_cpu_available = 0;
            }
            if (sizeof(available_quest_ids) == 0 && ROOM_MAPGRID_XK_HELPER->is_grid_direction(canonical_dir)) {
                phase_started_at = perf_counter_ns();
                phase_cpu_started_at = GATEWAY_TELEMETRY_D->query_gateway_thread_cpu_time_ns();
                if (indoor_topology_ready[index]) {
                    indoor_marker = ROOM_MARKER_XK_HELPER->query_exit_indoor_quest_marker_from_topology(
                        indoor_topology_by_exit[index], quest_ids_by_provider);
                } else {
                    /* 兜底路径：仅当 daemon 拓扑索引无此房间候选（非 content
                     * 源的运行时生成房）时逐出口遍历；content 房走上方拓扑主路径。 */
                    projection_cacheable = 0;
                    indoor_marker = ROOM_MARKER_XK_HELPER->query_room_indoor_entry_quest_marker(player,
                        room_path, quest_ids_by_provider);
                }
                phase_cpu_finished_at = GATEWAY_TELEMETRY_D->query_gateway_thread_cpu_time_ns();
                indoor_wall_ns += perf_counter_ns() - phase_started_at;
                if (phase_cpu_started_at >= 0 && phase_cpu_finished_at >= phase_cpu_started_at)
                    indoor_cpu_ns += phase_cpu_finished_at - phase_cpu_started_at;
                else
                    indoor_cpu_available = 0;
                available_quest_ids = arrayp(indoor_marker["quest_ids"])
                    ? indoor_marker["quest_ids"] : ({ });
            }
            if (sizeof(available_quest_ids) > 0) {
                exit_payload["quest_available"] = 1;
                exit_payload["available_quest_ids"] = available_quest_ids;
            }
        }
        exits_buffer[exit_count++] = exit_payload;
    }

    exits_payload = exit_count > 0 ? exits_buffer[0..exit_count - 1] : ({ });

    total_wall_ns = perf_counter_ns() - total_started_at;
    total_cpu_finished_at = GATEWAY_TELEMETRY_D->query_gateway_thread_cpu_time_ns();
    total_cpu_ns = total_cpu_started_at >= 0
        && total_cpu_finished_at >= total_cpu_started_at
        ? total_cpu_finished_at - total_cpu_started_at : -1;
    GATEWAY_TELEMETRY_D->record_gateway_movement_phase_timing(player,
        "exits_name_resolve", name_wall_ns, name_cpu_available ? name_cpu_ns : -1);
    GATEWAY_TELEMETRY_D->record_gateway_movement_phase_timing(player,
        "exits_quest_marker", marker_wall_ns, marker_cpu_available ? marker_cpu_ns : -1);
    GATEWAY_TELEMETRY_D->record_gateway_movement_phase_timing(player,
        "exits_indoor_scan", indoor_wall_ns, indoor_cpu_available ? indoor_cpu_ns : -1);
    GATEWAY_TELEMETRY_D->record_gateway_movement_phase_timing(player,
        "exits_payload_assembly",
        total_wall_ns - name_wall_ns - marker_wall_ns - indoor_wall_ns,
        total_cpu_ns >= 0 && name_cpu_available && marker_cpu_available
            && indoor_cpu_available
            ? total_cpu_ns - name_cpu_ns - marker_cpu_ns - indoor_cpu_ns : -1);

    payload = ([ "exits": exits_payload ]);
    if (cache_token != "" && projection_cacheable) {
        remember_exit_projection(player, cache_token, payload,
            keys(projection_dependencies));
    } else if (cache_token != "" && objectp(player)) {
        forget_exit_projection_cache_entry(player, cache_token);
    }
    return payload;
}

private mapping copy_exit_projection_payload(mapping payload)
{
    mapping exit_payload;
    mixed *result;
    mixed *source;
    int index;

    source = mapp(payload) && arrayp(payload["exits"])
        ? payload["exits"] : ({ });
    result = allocate(sizeof(source));
    for (index = 0; index < sizeof(source); index++) {
        if (!mapp(source[index])) {
            result[index] = source[index];
            continue;
        }
        exit_payload = copy(source[index]);
        if (arrayp(exit_payload["available_quest_ids"]))
            exit_payload["available_quest_ids"] =
                exit_payload["available_quest_ids"] + ({ });
        result[index] = exit_payload;
    }
    return ([ "exits": result ]);
}

private string query_exit_projection_cache_token(mapping exits, object player)
{
    mapping room_snapshot;
    object room;
    string room_path;

    if (!mapp(exits) || sizeof(exits) == 0 || !objectp(player))
        return "";
    room = environment(player);
    if (!objectp(room))
        return "";
    room_path = base_name(room);
    room_snapshot = query_map_room_snapshot(room_path, room, exits);
    if (!mapp(room_snapshot) || room_snapshot["source"] != "content")
        return "";

    return implode(({
        sprintf("%d", CONTENT_REGISTRY_D->query_content_generation()),
        sprintf("%d", QUEST_D->query_quest_definition_generation()),
        sprintf("%d", QUEST_D->query_player_map_projection_generation(player)),
        sprintf("%d", TIME_D->query_hour()),
        base_name(room),
        map_exits_signature(exits),
    }), "#");
}

private string *without_exit_projection_cache_token(mixed order_value,
    string token)
{
    mixed existing_token;
    string *filtered;
    int count;

    if (!arrayp(order_value))
        return ({ });
    filtered = allocate(sizeof(order_value));
    count = 0;
    foreach (existing_token in order_value) {
        if (!stringp(existing_token) || existing_token == ""
            || existing_token == token)
            continue;
        filtered[count++] = existing_token;
    }
    return count > 0 ? filtered[0..count - 1] : ({ });
}

private void forget_exit_projection_cache_entry(object player, string token)
{
    mapping cache;
    mapping entries;
    string *order;

    if (!objectp(player) || !stringp(token) || token == "")
        return;
    cache = player->query_temp("xk/exit_projection_cache");
    if (!mapp(cache) || !mapp(cache["entries"]) || !arrayp(cache["order"])) {
        player->delete_temp("xk/exit_projection_cache");
        return;
    }
    entries = copy(cache["entries"]);
    map_delete(entries, token);
    order = without_exit_projection_cache_token(cache["order"], token);
    if (sizeof(entries) == 0) {
        player->delete_temp("xk/exit_projection_cache");
        return;
    }
    player->set_temp("xk/exit_projection_cache", ([
        "entries": entries,
        "order": order,
    ]));
}

private mapping query_cached_exit_projection(object player, string token)
{
    mapping cache;
    mapping entries;
    mapping entry;
    mapping room_snapshot;
    string *order;
    string room_path;

    if (!objectp(player) || !stringp(token) || token == "")
        return 0;
    cache = player->query_temp("xk/exit_projection_cache");
    if (!mapp(cache))
        return 0;
    if (!mapp(cache["entries"]) || !arrayp(cache["order"])) {
        player->delete_temp("xk/exit_projection_cache");
        return 0;
    }
    entries = cache["entries"];
    order = cache["order"];
    entry = entries[token];
    if (!mapp(entry))
        return 0;
    if (!mapp(entry["payload"]) || !arrayp(entry["dependency_room_ids"])) {
        forget_exit_projection_cache_entry(player, token);
        return 0;
    }
    foreach (room_path in entry["dependency_room_ids"]) {
        room_snapshot = query_map_room_snapshot(room_path, 0, 0);
        if (!mapp(room_snapshot) || room_snapshot["source"] != "content") {
            forget_exit_projection_cache_entry(player, token);
            return 0;
        }
    }
    order = without_exit_projection_cache_token(order, token) + ({ token });
    player->set_temp("xk/exit_projection_cache", ([
        "entries": entries,
        "order": order,
    ]));
    return copy_exit_projection_payload(entry["payload"]);
}

private void remember_exit_projection(object player, string token, mapping payload,
    string *dependency_room_ids)
{
    mapping cache;
    mapping entries;
    string *entry_tokens;
    string *order;
    string evicted_token;

    if (!objectp(player) || !stringp(token) || token == "" || !mapp(payload)
        || !arrayp(dependency_room_ids))
        return;
    cache = player->query_temp("xk/exit_projection_cache");
    entries = mapp(cache) && mapp(cache["entries"])
        ? copy(cache["entries"]) : ([]);
    order = mapp(cache) && arrayp(cache["order"])
        ? without_exit_projection_cache_token(cache["order"], token)
        : ({ });
    map_delete(entries, token);
    while (sizeof(entries) >= EXIT_PROJECTION_CACHE_LIMIT) {
        if (sizeof(order) > 0) {
            evicted_token = order[0];
            order = sizeof(order) > 1 ? order[1..] : ({ });
        } else {
            entry_tokens = keys(entries);
            if (sizeof(entry_tokens) == 0)
                break;
            evicted_token = entry_tokens[0];
        }
        map_delete(entries, evicted_token);
    }
    entries[token] = ([
        "payload": copy_exit_projection_payload(payload),
        "dependency_room_ids": dependency_room_ids + ({ }),
    ]);
    order += ({ token });
    player->set_temp("xk/exit_projection_cache", ([
        "entries": entries,
        "order": order,
    ]));
}

public mapping query_map_room_snapshot(string room_path, object known_room,
    mapping known_exits)
{
    mapping runtime_snapshot;
    mapping static_snapshot;
    object room_ob;

    if (!stringp(room_path) || room_path == "")
        return 0;

    room_ob = known_room;
    if (!objectp(room_ob))
        room_ob = find_object(room_path);
    static_snapshot = CONTENT_REGISTRY_D->query_map_room_snapshot(room_path);
    if (objectp(room_ob)) {
        runtime_snapshot = map_room_snapshot_from_object(room_ob, known_exits);
        if (mapp(runtime_snapshot)) {
            runtime_snapshot["content_backed"] = is_complete_map_room_snapshot(static_snapshot);
            if (map_room_snapshot_matches_content(runtime_snapshot, static_snapshot))
                return static_snapshot;
        }
        return runtime_snapshot;
    }
    if (is_complete_map_room_snapshot(static_snapshot))
        return static_snapshot;

    room_ob = load_room_object(room_path);
    return objectp(room_ob) ? map_room_snapshot_from_object(room_ob, 0) : 0;
}

public int is_complete_map_room_snapshot(mapping snapshot)
{
    return mapp(snapshot)
        && stringp(snapshot["room_id"]) && snapshot["room_id"] != ""
        && stringp(snapshot["title"]) && snapshot["title"] != ""
        && mapp(snapshot["exits"]);
}

private mapping map_room_snapshot_from_object(object room_ob, mapping known_exits)
{
    mapping exits;
    string room_path;
    string room_name;
    string *parts;

    if (!objectp(room_ob))
        return 0;

    room_path = base_name(room_ob);
    room_name = function_exists("query_short", room_ob) ? room_ob->query_short() : 0;
    if (!stringp(room_name) || room_name == "")
        room_name = room_ob->query("short");
    if (!stringp(room_name) || room_name == "") {
        parts = explode(room_path, "/");
        room_name = sizeof(parts) > 0 ? replace_string(parts[<1], "_", " ") : room_path;
    }

    exits = mapp(known_exits) ? copy(known_exits) : room_ob->query("exits");
    return ([
        "room_id": room_path,
        "title": room_name,
        "exits": mapp(exits) ? copy(exits) : ([ ]),
        "source": "runtime",
        "content_backed": 0,
    ]);
}

private int map_room_snapshot_matches_content(mapping runtime_snapshot,
    mapping content_snapshot)
{
    if (!is_complete_map_room_snapshot(runtime_snapshot)
        || !is_complete_map_room_snapshot(content_snapshot))
        return 0;
    if (runtime_snapshot["room_id"] != content_snapshot["room_id"])
        return 0;
    if (runtime_snapshot["title"] != content_snapshot["title"])
        return 0;
    return map_exits_signature(runtime_snapshot["exits"])
        == map_exits_signature(content_snapshot["exits"]);
}

public string map_exits_signature(mapping exits)
{
    string *dirs;
    string *parts;
    string dir;

    if (!mapp(exits) || sizeof(exits) == 0)
        return "";

    dirs = keys(exits);
    parts = ({ });
    foreach (dir in dirs)
        parts += ({ sprintf("%s=%O", dir, exits[dir]) });
    return implode(parts, "|");
}

private object load_room_object(string room_path)
{
    object room_ob;
    mixed error;
    string *keys;

    if (!stringp(room_path) || room_path == "")
        return 0;

    if (!mapp(map_room_load_failure_cache))
        map_room_load_failure_cache = ([]);
    if (intp(map_room_load_failure_cache[room_path])) {
        if (map_room_load_failure_cache[room_path] >= time())
            return 0;
        map_delete(map_room_load_failure_cache, room_path);
    }

    room_ob = find_object(room_path);
    if (!objectp(room_ob)) {
        error = catch(room_ob = load_object(room_path));
        if (error || !objectp(room_ob)) {
            if (sizeof(map_room_load_failure_cache) >= MAP_ROOM_LOAD_FAILURE_CACHE_LIMIT) {
                keys = keys(map_room_load_failure_cache);
                if (sizeof(keys) > 0)
                    map_delete(map_room_load_failure_cache, keys[0]);
            }
            map_room_load_failure_cache[room_path] = time() + MAP_ROOM_LOAD_FAILURE_CACHE_TTL;
            return 0;
        }
    }

    return objectp(room_ob) ? room_ob : 0;
}
