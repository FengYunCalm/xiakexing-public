/**
 * @file    simul_efun_room_mapread_xk.c
 * @brief   地图网格读侧 read-model token/snapshot 与 ack 去重缓存
 *          （自 simul_efun_room_meta_xk.c 拆分）
 */

#pragma strict_types

#include <system/room_meta_helpers.h>
#include <system/services.h>
#include <system/xk_protocol.h>

#define MAP_GRID_RADIUS 20
#define MAP_GRID_DIAMETER (MAP_GRID_RADIUS * 2 + 1)

inherit "/lib/std/utils/util_owner_projection";

// ---- 前向声明 ----
varargs string xk_query_map_grid_read_model_token(object player, string title,
    string zone, mapping exits, object center_room);
varargs mapping xk_query_map_grid_read_model_snapshot(object player, string title,
    string zone, mapping exits, object center_room);
public string map_grid_ack_cache_key(string title, string zone, object center_room,
    mapping exits, object player);
public string query_recent_map_grid_ack_worker_state(object player, string cache_key);
public void remember_recent_map_grid_sent(object player, string cache_key,
    string worker_state);
public int clear_recent_map_grid_sent(object player);
public mapping handle_player_logout(object player);
public string player_map_cache_id(object player);
public string normalize_map_worker_state(mixed worker_state);
public mapping attach_map_worker_state(mapping payload, string worker_state);


#define MAP_GRID_ACK_TTL 5
#define MAP_GRID_ACK_CACHE_LIMIT 256
#define MAP_GRID_ACK_PRUNE_BUDGET 16

nosave private mapping map_grid_last_sent_by_player = ([]);

varargs string xk_query_map_grid_read_model_token(object player, string title,
    string zone, mapping exits, object center_room)
{
    if (!objectp(player))
        return "";
    if (!objectp(center_room))
        center_room = environment(player);
    if (!objectp(center_room))
        return "";
    if (!mapp(exits))
        exits = center_room->query("exits");
    if (!mapp(exits) || sizeof(exits) == 0)
        return "";
    if (!stringp(title) || title == "")
        title = center_room->query("short") || "未知地点";
    if (!stringp(zone))
        zone = center_room->query("zone") || "";
    return map_grid_ack_cache_key(title, zone, center_room, exits, player);
}

varargs mapping xk_query_map_grid_read_model_snapshot(object player, string title,
    string zone, mapping exits, object center_room)
{
    mapping center_map_snapshot;
    mapping radius_geometry;
    mapping snapshot;
    string *legend;

    if (!objectp(player))
        return 0;
    if (!objectp(center_room))
        center_room = environment(player);
    if (!objectp(center_room))
        return 0;
    if (!mapp(exits))
        exits = center_room->query("exits");
    if (!mapp(exits) || sizeof(exits) == 0)
        return 0;
    if (!stringp(title) || title == "")
        title = center_room->query("short") || "未知地点";
    if (!stringp(zone))
        zone = center_room->query("zone") || "";

    radius_geometry = ROOM_MAPGRID_XK_HELPER->query_room_radius_geometry(center_room, exits);
    center_map_snapshot = CONTENT_REGISTRY_D->query_map_room_snapshot(
        base_name(center_room));
    if (!radius_geometry["cacheable"])
        return 0;
    if (!mapp(radius_geometry["room_provider_ids"])
        || !mapp(radius_geometry["map_marker_provider_ids"])
        || !arrayp(radius_geometry["indoor_entry_candidates"]))
        return 0;

    snapshot = ([
        "input_token": xk_query_map_grid_read_model_token(player, title, zone,
            exits, center_room),
        "rows": radius_geometry["rows"] + ({ }),
        "paths": copy(radius_geometry["paths"]),
        "rooms": copy(radius_geometry["rooms"]),
        "room_provider_ids": ROOM_MAPGRID_XK_HELPER->copy_string_array_mapping(
            radius_geometry["room_provider_ids"]),
        "map_marker_provider_ids": ROOM_MAPGRID_XK_HELPER->copy_string_array_mapping(
            radius_geometry["map_marker_provider_ids"]),
        "indoor_entry_candidates": ROOM_MAPGRID_XK_HELPER->copy_indoor_entry_candidates(
            radius_geometry["indoor_entry_candidates"]),
        "quest_ids_by_provider": ROOM_MAPGRID_XK_HELPER->copy_string_array_mapping(
            QUEST_D->query_map_marker_quest_ids_by_provider(player)),
        "width": MAP_GRID_DIAMETER,
        "height": MAP_GRID_DIAMETER,
    ]);
    if (mapp(center_map_snapshot)) {
        snapshot["region_id"] = center_map_snapshot["region_id"] || "";
        snapshot["map_context_id"] = center_map_snapshot["map_context_id"] || "";
        snapshot["map_layer"] = center_map_snapshot["map_layer"] || "";
        snapshot["submap_id"] = center_map_snapshot["submap_id"] || "";
    }
    if (title != "")
        snapshot["title"] = title;
    if (zone != "")
        snapshot["zone"] = zone;
    legend = ROOM_MAPGRID_XK_HELPER->build_special_exit_legend(exits);
    if (sizeof(legend) > 0)
        snapshot["legend"] = legend + ({ });
    return snapshot;
}

public string map_grid_ack_cache_key(string title, string zone, object center_room,
    mapping exits, object player)
{
    mapping projection;

    if (!objectp(center_room))
        return "";

    projection = objectp(player)
        ? LOGIN_PROJECTION_D->query_projection_identity(player) : ([]);

    return implode(({
        base_name(center_room),
        sprintf("%d", CONTENT_REGISTRY_D->query_content_generation()),
        sprintf("%d", QUEST_D->query_quest_definition_generation()),
        sprintf("%d", QUEST_D->query_player_map_projection_generation(player)),
        projection["projection_id"] || "",
        sprintf("%d", projection["projection_revision"] || 0),
        stringp(title) ? title : "",
        stringp(zone) ? zone : "",
        ROOM_EXITS_XK_HELPER->map_exits_signature(exits),
    }), "#");
}

private void prune_map_grid_ack_cache(int now)
{
    mapping entry;
    string player_id;
    int inspected;

    if (!mapp(map_grid_last_sent_by_player))
        return;
    foreach (player_id in keys(map_grid_last_sent_by_player)) {
        if (inspected++ >= MAP_GRID_ACK_PRUNE_BUDGET)
            break;
        entry = map_grid_last_sent_by_player[player_id];
        if (!mapp(entry) || !intp(entry["worker_state_at"])
            || now - entry["worker_state_at"] > MAP_GRID_ACK_TTL)
            map_delete(map_grid_last_sent_by_player, player_id);
    }
}

public string query_recent_map_grid_ack_worker_state(object player, string cache_key)
{
    mapping entry;
    string player_id;
    int now;

    if (!objectp(player) || !stringp(cache_key) || cache_key == "")
        return "";

    player_id = player_map_cache_id(player);
    if (player_id == "")
        return "";
    if (!mapp(map_grid_last_sent_by_player))
        return "";

    entry = map_grid_last_sent_by_player[player_id];
    if (!mapp(entry) || entry["cache_key"] != cache_key)
        return "";
    if (!stringp(entry["worker_state"]) || entry["worker_state"] == "")
        return "";

    now = time();
    if (!intp(entry["worker_state_at"])
        || now - entry["worker_state_at"] > MAP_GRID_ACK_TTL) {
        map_delete(map_grid_last_sent_by_player, player_id);
        return "";
    }

    entry["sent_at"] = now;
    map_grid_last_sent_by_player[player_id] = entry;
    return normalize_map_worker_state(entry["worker_state"]);
}

public void remember_recent_map_grid_sent(object player, string cache_key,
    string worker_state)
{
    string player_id;
    string oldest_id;
    string candidate_id;
    mapping entry;
    int now;
    int oldest_at;
    int candidate_at;

    oldest_id = "";
    if (!objectp(player) || !stringp(cache_key) || cache_key == "")
        return;

    player_id = player_map_cache_id(player);
    if (player_id == "")
        return;
    if (!mapp(map_grid_last_sent_by_player))
        map_grid_last_sent_by_player = ([]);

    now = time();
    prune_map_grid_ack_cache(now);
    if (!mapp(map_grid_last_sent_by_player[player_id])
        && sizeof(keys(map_grid_last_sent_by_player))
            >= MAP_GRID_ACK_CACHE_LIMIT) {
        oldest_at = now + 1;
        foreach (candidate_id in keys(map_grid_last_sent_by_player)) {
            entry = map_grid_last_sent_by_player[candidate_id];
            candidate_at = mapp(entry) && intp(entry["sent_at"])
                ? entry["sent_at"] : 0;
            if (candidate_at < oldest_at) {
                oldest_at = candidate_at;
                oldest_id = candidate_id;
            }
        }
        if (oldest_id != "")
            map_delete(map_grid_last_sent_by_player, oldest_id);
    }
    map_grid_last_sent_by_player[player_id] = ([
        "cache_key": cache_key,
        "sent_at": now,
        "worker_state": normalize_map_worker_state(worker_state),
        "worker_state_at": now,
    ]);
}

public int clear_recent_map_grid_sent(object player)
{
    string player_id;

    if (!objectp(player))
        return 0;
    player_id = player_map_cache_id(player);
    if (player_id == "")
        return 0;
    if (mapp(map_grid_last_sent_by_player))
        map_delete(map_grid_last_sent_by_player, player_id);
    return 1;
}

public mapping handle_player_logout(object player)
{
    if (!objectp(player))
        return ([ "ok": 0, "error": "player unavailable" ]);
    clear_recent_map_grid_sent(player);
    return ([ "ok": 1, "state": "map_ack_cache_released" ]);
}

public string player_map_cache_id(object player)
{
    string player_id;

    if (!objectp(player))
        return "";

    player_id = call_other("/kernel/simul_efun", "get_object_id", player);
    if (!stringp(player_id) || player_id == "")
        player_id = file_name(player);
    return player_id;
}

public string normalize_map_worker_state(mixed worker_state)
{
    return stringp(worker_state) && worker_state != ""
        ? worker_state
        : "main_thread";
}

public mapping attach_map_worker_state(mapping payload, string worker_state)
{
    return ROOM_MAPGRID_XK_HELPER->copy_map_grid_payload(payload) + ([
        "read_side_worker": 0,
        "read_side_worker_state": normalize_map_worker_state(worker_state),
    ]);
}
