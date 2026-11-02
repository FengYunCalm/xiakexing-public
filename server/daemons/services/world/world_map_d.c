/**
 * @file    world_map_d.c
 * @brief   世界地图守护进程,导出 world-map.json 供地图检查与独立地图工具消费。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>
#include <system/globals.h>

inherit "/lib/framework/daemon";

#define EXPORT_DIR DATA_DIR + "exports"
#define EXPORT_PATH EXPORT_DIR + "/world-map.json"
#define EXPORT_TMP_PATH EXPORT_PATH + ".tmp"
#define EXPORT_BACKUP_PATH EXPORT_PATH + ".bak"
#define EXPORT_CHUNK_LIMIT 32768
#define EXPORT_NODE_BATCH_LIMIT 300
#define EXPORT_EDGE_BATCH_LIMIT 600

nosave private string _last_export_error;
nosave private string _export_buffer;
nosave private mapping _export_state;
nosave private int _export_call_out_handle;
nosave private mapping _route_topology;
nosave private int _route_topology_generation;
private mapping *query_transition_specs();

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

private string query_region_id_from_room_path(string room_path)
{
    string region_id;

    if (!stringp(room_path) || room_path == "")
        return "";
    if (sscanf(room_path, "/world/%s/rooms/%*s", region_id) != 1)
        return "";
    return region_id;
}

private string *query_sorted_room_paths(mapping rooms)
{
    return mapp(rooms) ? sort_array(keys(rooms), 1) : ({ });
}

private string normalize_room_path(string room_path)
{
    if (!stringp(room_path) || room_path == "")
        return "";
    room_path = trim(room_path);
    if (strlen(room_path) > 2 && room_path[<2..] == ".c")
        return room_path[0..<3];
    return room_path;
}

private string *query_sorted_exit_dirs(mapping exits)
{
    return mapp(exits) ? sort_array(keys(exits), 1) : ({ });
}

private string query_quality_class(string room_path, mapping room, mapping room_meta)
{
    string layer;

    if (mapp(room_meta) && stringp(room_meta["quality_class"]) && room_meta["quality_class"] != "")
        return room_meta["quality_class"];
    layer = mapp(room_meta) ? room_meta["map_layer"] : 0;
    if (stringp(layer) && (layer == "router" || layer == "route" || layer == "structural"))
        return "structural";
    if (strsrch(room_path, "bulk_generated") >= 0 || strsrch(room_path, "generated") >= 0)
        return "review";
    if (strsrch(room_path, "_expansion_") >= 0 || strsrch(room_path, "deep_") >= 0)
        return "review";
    if (!mapp(room) || !stringp(room["long"]) || strlen(room["long"]) < 24)
        return "review";
    return "authored";
}

private string query_map_layer(string room_path, mapping room, mapping room_meta)
{
    mapping exits;

    if (mapp(room_meta) && stringp(room_meta["map_layer"]) && room_meta["map_layer"] != "")
        return room_meta["map_layer"];
    if (strsrch(room_path, "route_") >= 0 || strsrch(room_path, "_road") >= 0)
        return "route";
    if (strsrch(room_path, "hub") >= 0 || strsrch(room_path, "plaza") >= 0 || strsrch(room_path, "entry") >= 0)
        return "hub";
    exits = mapp(room) ? room["exits"] : 0;
    if (mapp(exits) && sizeof(exits) <= 1)
        return "structural";
    return "room";
}

private string *query_quality_flags(string room_path, mapping room, mapping room_index, mapping room_meta)
{
    string *flags;
    mapping exits;
    string dir;
    string target;

    flags = ({ });
    if (!mapp(room))
        return ({ "invalid_room" });
    if (!stringp(room["area"]) || room["area"] == "")
        flags += ({ "missing_area" });
    if (!stringp(room["title"]) || room["title"] == "")
        flags += ({ "missing_title" });
    if (!stringp(room["long"]) || room["long"] == "")
        flags += ({ "missing_long" });
    else if (strlen(room["long"]) < 24)
        flags += ({ "short_long" });

    exits = room["exits"];
    if (!mapp(exits) || sizeof(exits) == 0) {
        flags += ({ "no_exits" });
    } else {
        foreach (dir, target in exits) {
            if (!stringp(target) || target == "" || !mapp(room_index[target])) {
                flags += ({ "broken_exit" });
                break;
            }
        }
    }

    if (query_quality_class(room_path, room, room_meta) == "review")
        flags += ({ "needs_review" });
    return flags;
}

private string *query_region_package_paths()
{
    return coerce_string_array(CONFIG_D->query_config("content", "region_package_paths"));
}

private mapping query_region_package_config(string package_path)
{
    object package_ob;
    mixed config;
    mixed err;

    if (!stringp(package_path) || package_path == "")
        return ([]);
    err = catch(package_ob = load_object(package_path));
    if (err || !objectp(package_ob))
        return ([]);
    err = catch(config = package_ob->query_all_config());
    if (err || !mapp(config))
        return ([]);
    return copy(config);
}

private mapping query_room_index()
{
    mapping room_index;
    mapping package_config;
    mapping rooms;
    string package_path;

    room_index = ([]);
    foreach (package_path in query_region_package_paths()) {
        package_config = query_region_package_config(package_path);
        rooms = mapp(package_config["rooms"]) ? package_config["rooms"] : ([]);
        room_index += rooms;
    }
    return room_index;
}

/*
 * 路线缓存只投影可跨玩家共享的房间/出口事实。任务 marker、对象可见性、
 * projection 和协议序号都留在各自玩家路径，禁止进入这份共享拓扑。
 */
private mapping project_route_topology(mapping rooms)
{
    mapping topology;
    mapping room;
    mapping exits;
    mapping projected_exits;
    string room_path;
    string normalized_room;
    string dir;
    string target;

    topology = ([]);
    if (!mapp(rooms))
        return topology;

    foreach (room_path, room in rooms) {
        normalized_room = normalize_room_path(room_path);
        if (normalized_room == "")
            continue;
        projected_exits = ([]);
        exits = mapp(room) ? room["exits"] : 0;
        if (mapp(exits)) {
            foreach (dir in query_sorted_exit_dirs(exits)) {
                target = stringp(exits[dir])
                    ? normalize_room_path(exits[dir]) : "";
                if (target != "" && mapp(rooms[target])
                    && query_region_id_from_room_path(normalized_room)
                        == query_region_id_from_room_path(target))
                    projected_exits[dir] = target;
            }
        }
        topology[normalized_room] = projected_exits;
    }
    return topology;
}

private mapping query_route_topology()
{
    mapping rooms;
    int content_generation;

    content_generation = CONTENT_REGISTRY_D->query_content_generation();
    if (mapp(_route_topology)
        && _route_topology_generation == content_generation)
        return _route_topology;

    rooms = query_room_index();
    _route_topology = project_route_topology(rooms);
    _route_topology_generation = content_generation;
    return _route_topology;
}

private mapping normalize_map_metadata(mapping metadata)
{
    return mapp(metadata)
        ? ([
            "regions": mapp(metadata["regions"]) ? metadata["regions"] : ([]),
            "room_tags": mapp(metadata["room_tags"]) ? metadata["room_tags"] : ([]),
            "submaps": arrayp(metadata["submaps"]) ? metadata["submaps"] : ({ }),
            "transitions": arrayp(metadata["transitions"]) ? metadata["transitions"] : ({ }),
        ])
        : ([ "regions": ([]), "room_tags": ([]), "submaps": ({ }), "transitions": ({ }) ]);
}

private mapping build_room_node(string room_path, mapping room, mapping room_index, mapping room_tags)
{
    mapping room_meta;
    string *tags;
    string region_id;
    string layer;
    string quality_class;

    room_meta = mapp(room_tags) && mapp(room_tags[room_path]) ? room_tags[room_path] : ([]);
    tags = coerce_string_array(room_meta["tags"]);
    region_id = query_region_id_from_room_path(room_path);
    layer = query_map_layer(room_path, room, room_meta);
    quality_class = query_quality_class(room_path, room, room_meta);

    return ([
        "id": room_path,
        "title": stringp(room["title"]) ? room["title"] : "",
        "region": region_id,
        "area": stringp(room["area"]) ? room["area"] : "",
        "zone": stringp(room["zone"]) ? room["zone"] : "",
        "long": stringp(room["long"]) ? room["long"] : "",
        "exits": mapp(room["exits"]) ? copy(room["exits"]) : ([]),
        "layer": layer,
        "map_layer": layer,
        "node_type": layer,
        "quality_state": quality_class,
        "quality_class": quality_class,
        "quality_flags": query_quality_flags(room_path, room, room_index, room_meta),
        "tags": tags,
    ]);
}

private int query_link_count(mapping rooms)
{
    mapping room;
    mapping exits;
    string room_path;
    string dir;
    mixed target;
    int count;

    count = 0;
    if (!mapp(rooms))
        return 0;
    foreach (room_path, room in rooms) {
        exits = mapp(room) ? room["exits"] : 0;
        if (!mapp(exits))
            continue;
        foreach (dir, target in exits)
            count += 1;
    }
    return count;
}

private mapping *query_missing_anchor_reports(mapping regions, mapping rooms)
{
    mapping *reports;
    mapping region;
    mapping anchor;
    mixed *anchors;
    string *region_ids;
    string region_id;

    reports = ({ });
    region_ids = mapp(regions) ? sort_array(keys(regions), 1) : ({ });
    foreach (region_id in region_ids) {
        region = regions[region_id];
        if (!mapp(region))
            continue;
        if (!stringp(region["entry_room"]) || !mapp(rooms[region["entry_room"]]))
            reports += ({ ([ "region": region_id, "anchor": "entry_room", "room": stringp(region["entry_room"]) ? region["entry_room"] : "" ]) });
        anchors = arrayp(region["anchors"]) ? region["anchors"] : ({ });
        foreach (anchor in anchors) {
            if (mapp(anchor) && (!stringp(anchor["room"]) || !mapp(rooms[anchor["room"]])))
                reports += ({ ([ "region": region_id, "anchor": stringp(anchor["id"]) ? anchor["id"] : "", "room": stringp(anchor["room"]) ? anchor["room"] : "" ]) });
        }
    }
    return reports;
}

private mapping *collect_anchor_specs(mapping regions, mapping rooms)
{
    mapping *specs;
    string *region_ids;
    string region_id;
    mapping region;
    mapping anchor;
    mixed *anchors;

    specs = ({ });
    region_ids = mapp(regions) ? sort_array(keys(regions), 1) : ({ });
    foreach (region_id in region_ids) {
        region = regions[region_id];
        if (!mapp(region))
            continue;
        anchors = arrayp(region["anchors"]) ? region["anchors"] : ({ });
        foreach (anchor in anchors) {
            if (!mapp(anchor) || !stringp(anchor["room"]) || !mapp(rooms[anchor["room"]]))
                continue;
            specs += ({ ([
                "region_id": region_id,
                "region_name": stringp(region["name"]) ? region["name"] : region_id,
                "anchor": copy(anchor),
            ]) });
        }
    }
    return specs;
}

private mapping query_anchor_coord(mapping anchor, int min_x, int max_x, int min_y, int max_y)
{
    int grid_x;
    int grid_y;
    int width;
    int height;

    if (!mapp(anchor) || !intp(anchor["x"]) || !intp(anchor["y"]))
        return 0;
    grid_x = anchor["x"];
    grid_y = anchor["y"];
    width = max_x - min_x;
    height = max_y - min_y;

    return ([
        "x": width > 0 ? 180 + ((grid_x - min_x) * 620) / width : 500,
        "y": height > 0 ? 140 + ((max_y - grid_y) * 380) / height : 330,
    ]);
}

private string query_anchor_node_type(mapping anchor, mapping room_node)
{
    if (mapp(anchor) && stringp(anchor["kind"]) && anchor["kind"] != "")
        return anchor["kind"];
    if (mapp(room_node) && stringp(room_node["map_layer"]) && room_node["map_layer"] != "")
        return room_node["map_layer"];
    return "hub";
}

private string query_edge_type(mapping from_anchor, mapping to_anchor, string direction)
{
    string from_kind;
    string to_kind;

    from_kind = mapp(from_anchor) ? from_anchor["kind"] : "";
    to_kind = mapp(to_anchor) ? to_anchor["kind"] : "";
    if (from_kind == "waterway" || to_kind == "waterway")
        return "waterway";
    if (from_kind == "route" || to_kind == "route")
        return "route";
    if (direction == "up" || direction == "down" || direction == "enter" || direction == "out")
        return "transition";
    return "walk";
}

private mapping *build_overworld_nodes(mapping regions, mapping rooms, mapping room_index, mapping room_tags)
{
    mapping *nodes;
    mapping *specs;
    mapping spec;
    mapping anchor;
    mapping room_node;
    int min_x;
    int max_x;
    int min_y;
    int max_y;
    int has_grid;

    nodes = ({ });
    specs = collect_anchor_specs(regions, rooms);
    min_x = 0;
    max_x = 0;
    min_y = 0;
    max_y = 0;
    has_grid = 0;

    foreach (spec in specs) {
        anchor = spec["anchor"];
        if (!mapp(anchor) || !intp(anchor["x"]) || !intp(anchor["y"]))
            continue;
        if (!has_grid) {
            min_x = max_x = anchor["x"];
            min_y = max_y = anchor["y"];
            has_grid = 1;
            continue;
        }
        if (anchor["x"] < min_x)
            min_x = anchor["x"];
        if (anchor["x"] > max_x)
            max_x = anchor["x"];
        if (anchor["y"] < min_y)
            min_y = anchor["y"];
        if (anchor["y"] > max_y)
            max_y = anchor["y"];
    }

    foreach (spec in specs) {
        anchor = spec["anchor"];
        room_node = build_room_node(anchor["room"], rooms[anchor["room"]], room_index, room_tags);
        room_node["region"] = spec["region_id"];
        room_node["area"] = spec["region_name"];
        room_node["zone"] = stringp(anchor["id"]) && anchor["id"] != "" ? anchor["id"] : room_node["zone"];
        room_node["layer"] = "overworld";
        room_node["node_type"] = query_anchor_node_type(anchor, room_node);
        if (has_grid)
            room_node["coord"] = query_anchor_coord(anchor, min_x, max_x, min_y, max_y);
        nodes += ({ room_node });
    }
    return nodes;
}

private mapping *build_overworld_edges(mapping regions, mapping rooms)
{
    mapping *edges;
    mapping *specs;
    mapping anchor_rooms;
    mapping anchor_by_room;
    mapping spec;
    mapping anchor;
    mapping exits;
    string room_path;
    string dir;
    string target;

    edges = ({ });
    specs = collect_anchor_specs(regions, rooms);
    anchor_rooms = ([ ]);
    anchor_by_room = ([ ]);

    foreach (spec in specs) {
        anchor = spec["anchor"];
        anchor_rooms[anchor["room"]] = 1;
        anchor_by_room[anchor["room"]] = anchor;
    }

    foreach (spec in specs) {
        anchor = spec["anchor"];
        room_path = anchor["room"];
        exits = rooms[room_path]["exits"];
        if (!mapp(exits))
            continue;
        foreach (dir, target in exits) {
            if (!stringp(target) || !anchor_rooms[target])
                continue;
            edges += ({ ([
                "from": room_path,
                "to": target,
                "direction": dir,
                "edge_type": query_edge_type(anchor, anchor_by_room[target], dir),
            ]) });
        }
    }
    return edges;
}

private mapping *build_room_edges(string *room_list, mapping rooms, mapping room_index, int local_only)
{
    mapping *edges;
    mapping room_set;
    string room_path;
    mapping exits;
    string dir;
    string target;

    edges = ({ });
    room_set = ([ ]);
    foreach (room_path in room_list)
        room_set[room_path] = 1;

    foreach (room_path in room_list) {
        exits = mapp(rooms[room_path]) ? rooms[room_path]["exits"] : 0;
        if (!mapp(exits))
            continue;
        foreach (dir, target in exits) {
            if (!stringp(target))
                continue;
            if (local_only && !room_set[target])
                continue;
            edges += ({ ([
                "from": room_path,
                "to": target,
                "direction": dir,
                "edge_type": "walk",
                "valid": mapp(room_index[target]) ? 1 : 0,
            ]) });
        }
    }
    return edges;
}

private mapping *build_submaps(mapping rooms, mapping room_index, mapping room_tags, mixed *submap_specs)
{
    mapping *compiled;
    mapping submap;
    string *room_list;
    string *valid_rooms;
    string room_path;
    mapping *nodes;
    mapping room_node;
    string anchor_id;
    mixed *submaps;

    compiled = ({ });
    submaps = arrayp(submap_specs) ? submap_specs : ({ });
    foreach (submap in submaps) {
        if (!mapp(submap) || !stringp(submap["id"]) || submap["id"] == "")
            continue;
        room_list = coerce_string_array(submap["rooms"]);
        valid_rooms = ({ });
        foreach (room_path in room_list) {
            if (mapp(rooms[room_path]))
                valid_rooms += ({ room_path });
        }
        if (sizeof(valid_rooms) == 0)
            continue;

        nodes = ({ });
        foreach (room_path in valid_rooms) {
            room_node = build_room_node(room_path, rooms[room_path], room_index, room_tags);
            room_node["submap_id"] = submap["id"];
            room_node["layer"] = "submap";
            nodes += ({ room_node });
        }

        anchor_id = stringp(submap["anchor"]) && submap["anchor"] != ""
            ? submap["anchor"]
            : valid_rooms[0];
        compiled += ({ ([
            "id": submap["id"],
            "title": stringp(submap["title"]) && submap["title"] != "" ? submap["title"] : submap["id"],
            "region": stringp(submap["region"]) && submap["region"] != "" ? submap["region"] : query_region_id_from_room_path(anchor_id),
            "anchor": anchor_id,
            "nodes": nodes,
            "edges": build_room_edges(valid_rooms, rooms, room_index, 1),
        ]) });
    }
    return compiled;
}

private mapping build_quality_report_for_config(mapping metadata, mapping rooms)
{
    mapping regions;
    mapping room_tags;
    mapping counters;
    mapping room;
    mapping room_meta;
    mapping *missing_anchors;
    string room_path;
    string quality_class;
    int room_count;
    int has_map_metadata;

    metadata = normalize_map_metadata(metadata);
    regions = metadata["regions"];
    room_tags = metadata["room_tags"];
    counters = ([ "authored": 0, "structural": 0, "review": 0 ]);
    room_count = 0;
    has_map_metadata = sizeof(regions) > 0 || sizeof(metadata["submaps"]) > 0 || sizeof(room_tags) > 0;

    if (mapp(rooms)) {
        foreach (room_path, room in rooms) {
            room_meta = mapp(room_tags) && mapp(room_tags[room_path]) ? room_tags[room_path] : ([]);
            quality_class = query_quality_class(room_path, room, room_meta);
            if (!intp(counters[quality_class]))
                counters[quality_class] = 0;
            counters[quality_class] += 1;
            room_count += 1;
        }
    }

    missing_anchors = has_map_metadata ? query_missing_anchor_reports(regions, rooms) : ({ });
    return ([
        "ok": !has_map_metadata || (room_count > 0 && sizeof(missing_anchors) == 0),
        "region_count": sizeof(regions),
        "room_count": room_count,
        "link_count": query_link_count(rooms),
        "quality": counters,
        "missing_anchors": missing_anchors,
    ]);
}

private mapping merge_quality_report(mapping acc, mapping report)
{
    mapping quality;
    mapping item;
    mapping *missing_anchors;
    string key;
    mixed quality_value;

    if (!mapp(acc))
        acc = ([
            "ok": 1,
            "region_count": 0,
            "room_count": 0,
            "link_count": 0,
            "quality": ([ "authored": 0, "structural": 0, "review": 0 ]),
            "missing_anchors": ({ }),
        ]);
    if (!mapp(report))
        return acc;

    acc["ok"] = acc["ok"] && report["ok"];
    acc["region_count"] += intp(report["region_count"]) ? report["region_count"] : 0;
    acc["room_count"] += intp(report["room_count"]) ? report["room_count"] : 0;
    acc["link_count"] += intp(report["link_count"]) ? report["link_count"] : 0;

    quality = mapp(acc["quality"]) ? acc["quality"] : ([ ]);
    item = mapp(report["quality"]) ? report["quality"] : ([ ]);
    foreach (key, quality_value in item) {
        if (!intp(quality[key]))
            quality[key] = 0;
        quality[key] += intp(quality_value) ? quality_value : 0;
    }
    acc["quality"] = quality;

    missing_anchors = arrayp(acc["missing_anchors"]) ? acc["missing_anchors"] : ({ });
    missing_anchors += arrayp(report["missing_anchors"]) ? report["missing_anchors"] : ({ });
    acc["missing_anchors"] = missing_anchors;
    if (sizeof(missing_anchors) > 0)
        acc["ok"] = 0;
    return acc;
}

mapping query_quality_report()
{
    mapping report;
    mapping package_config;
    string package_path;

    report = ([
        "ok": 1,
        "region_count": 0,
        "room_count": 0,
        "link_count": 0,
        "quality": ([ "authored": 0, "structural": 0, "review": 0 ]),
        "missing_anchors": ({ }),
    ]);

    foreach (package_path in query_region_package_paths()) {
        package_config = query_region_package_config(package_path);
        report = merge_quality_report(
            report,
            build_quality_report_for_config(
                package_config["map_metadata"],
                mapp(package_config["rooms"]) ? package_config["rooms"] : ([])
            )
        );
    }
    if (!intp(report["ok"]))
        report["ok"] = 0;
    return report;
}

mapping query_world_map()
{
    mapping package_config;
    mapping metadata;
    mapping room_index;
    mapping rooms;
    mapping room_tags;
    string *package_paths;
    string package_path;
    string room_path;
    mapping room;
    mapping *nodes;
    mapping *edges;
    mapping *overworld_nodes;
    mapping *overworld_edges;
    mapping *submaps;
    mapping *transitions;

    package_paths = query_region_package_paths();
    room_index = ([]);
    nodes = ({ });
    edges = ({ });
    overworld_nodes = ({ });
    overworld_edges = ({ });
    submaps = ({ });
    transitions = query_transition_specs();

    foreach (package_path in package_paths) {
        package_config = query_region_package_config(package_path);
        rooms = mapp(package_config["rooms"]) ? package_config["rooms"] : ([]);
        foreach (room_path, room in rooms)
            room_index[room_path] = room;
    }

    foreach (package_path in package_paths) {
        package_config = query_region_package_config(package_path);
        metadata = normalize_map_metadata(package_config["map_metadata"]);
        rooms = mapp(package_config["rooms"]) ? package_config["rooms"] : ([]);
        room_tags = metadata["room_tags"];
        foreach (room_path, room in rooms) {
            if (!mapp(room))
                continue;
            nodes += ({ build_room_node(room_path, room, room_index, room_tags) });
        }
        edges += build_room_edges(query_sorted_room_paths(rooms), rooms, room_index, 0);
        overworld_nodes += build_overworld_nodes(metadata["regions"], rooms, room_index, room_tags);
        overworld_edges += build_overworld_edges(metadata["regions"], rooms);
        submaps += build_submaps(rooms, room_index, room_tags, metadata["submaps"]);
    }

    return ([
        "schema": "xkx-world-map-v2",
        "generated_at": ctime(time()),
        "source": "server/daemons/services/world/world_map_d.c",
        "nodes": nodes,
        "edges": edges,
        "overworld": ([
            "nodes": overworld_nodes,
            "edges": overworld_edges,
        ]),
        "submaps": submaps,
        "transitions": transitions,
        "report": query_quality_report(),
    ]);
}

mapping query_room_node(string room_path)
{
    mapping package_config;
    mapping metadata;
    mapping rooms;
    mapping room_index;
    mapping room_tags;
    string *package_paths;
    string package_path;

    if (!stringp(room_path) || room_path == "")
        return ([]);
    if (strlen(room_path) > 2 && room_path[<2..] == ".c")
        room_path = room_path[0..<3];

    room_index = ([]);
    package_paths = query_region_package_paths();
    foreach (package_path in package_paths) {
        package_config = query_region_package_config(package_path);
        rooms = mapp(package_config["rooms"]) ? package_config["rooms"] : ([]);
        room_index += rooms;
    }

    foreach (package_path in package_paths) {
        package_config = query_region_package_config(package_path);
        rooms = mapp(package_config["rooms"]) ? package_config["rooms"] : ([]);
        if (!mapp(rooms[room_path]))
            continue;
        metadata = normalize_map_metadata(package_config["map_metadata"]);
        room_tags = metadata["room_tags"];
        return build_room_node(room_path, rooms[room_path], room_index, room_tags);
    }
    return ([]);
}

private mapping *query_transition_specs()
{
    mapping *result;
    mapping package_config;
    mapping metadata;
    mapping transition;
    mapping snapshot;
    mixed transitions;
    string *package_paths;
    string package_path;
    string region_id;
    string source_room;
    string target_room;
    string target_region;

    result = ({ });
    package_paths = query_region_package_paths();
    foreach (package_path in package_paths) {
        if (sscanf(package_path, "/config/content/%s/package", region_id) != 1)
            continue;
        package_config = query_region_package_config(package_path);
        metadata = normalize_map_metadata(package_config["map_metadata"]);
        transitions = metadata["transitions"];
        if (!arrayp(transitions))
            continue;
        foreach (transition in transitions) {
            if (!mapp(transition))
                continue;
            source_room = transition["source_room"];
            target_room = transition["target_room"];
            target_region = transition["target_region"];
            if (!stringp(source_room) || source_room == ""
                || !stringp(target_room) || target_room == ""
                || !stringp(target_region) || target_region == ""
                || (stringp(transition["trigger"])
                    && transition["trigger"] != "automatic"))
                continue;
            snapshot = copy(transition);
            snapshot["source_region"] = region_id;
            snapshot["source_path"] = strsrch(source_room, "/world/") == 0
                ? source_room : "/world/" + region_id + "/rooms/" + source_room;
            snapshot["target_path"] = strsrch(target_room, "/world/") == 0
                ? target_room : "/world/" + target_region + "/rooms/" + target_room;
            snapshot["edge_type"] = "TRANSFER";
            result += ({ snapshot });
        }
    }
    return result;
}

mapping query_automatic_transition_for_room(string room_path)
{
    mapping *transitions;
    mapping transition;
    string normalized_path;

    normalized_path = normalize_room_path(room_path);
    if (normalized_path == "")
        return 0;
    transitions = query_transition_specs();
    foreach (transition in transitions) {
        if (mapp(transition)
            && normalize_room_path(transition["source_path"]) == normalized_path)
            return copy(transition);
    }
    return 0;
}

mapping query_route_plan(string start_room, string target_room, int max_steps)
{
    mapping topology;
    mapping transitions_by_source;
    mapping visited;
    mapping parent_by_room;
    mapping step_by_room;
    mapping exits;
    mapping transition;
    mapping step;
    string *queue;
    string *commands;
    string *rooms;
    mixed *steps;
    mapping *transitions;
    string current;
    string next_room;
    string dir;
    int head;
    int guard;
    int transfer_count;

    start_room = normalize_room_path(start_room);
    target_room = normalize_room_path(target_room);
    max_steps = max_steps > 0 ? max_steps : 64;
    topology = query_route_topology();
    transitions = query_transition_specs();
    transitions_by_source = ([]);
    foreach (transition in transitions) {
        current = normalize_room_path(transition["source_path"]);
        next_room = normalize_room_path(transition["target_path"]);
        if (current == "" || next_room == "")
            continue;
        if (!arrayp(transitions_by_source[current]))
            transitions_by_source[current] = ({ });
        transitions_by_source[current] += ({ transition });
    }

    if (start_room == "" || target_room == "")
        return ([ "ok": 0, "error": "missing_room", "commands": ({ }), "steps": ({ }) ]);
    if (!mapp(topology[start_room]))
        return ([ "ok": 0, "error": "unknown_start", "commands": ({ }), "steps": ({ }) ]);
    if (!mapp(topology[target_room]))
        return ([ "ok": 0, "error": "unknown_target", "commands": ({ }), "steps": ({ }) ]);
    if (start_room == target_room)
        return ([ "ok": 1, "error": "", "commands": ({ }), "steps": ({ }),
            "rooms": ({ start_room }), "topology_generation": _route_topology_generation ]);

    visited = ([ start_room: 1 ]);
    parent_by_room = ([]);
    step_by_room = ([]);
    queue = ({ start_room });
    head = 0;
    while (head < sizeof(queue)) {
        current = queue[head++];
        if (current == target_room)
            break;
        exits = topology[current];
        if (mapp(exits)) {
            foreach (dir in query_sorted_exit_dirs(exits)) {
                next_room = normalize_room_path(exits[dir]);
                if (next_room == "" || visited[next_room] || !mapp(topology[next_room]))
                    continue;
                visited[next_room] = 1;
                parent_by_room[next_room] = current;
                step_by_room[next_room] = ([
                    "kind": "walk",
                    "edge_type": "WALK",
                    "from": current,
                    "to": next_room,
                    "direction": dir,
                ]);
                queue += ({ next_room });
            }
        }
        foreach (transition in (arrayp(transitions_by_source[current])
            ? transitions_by_source[current] : ({ }))) {
            next_room = normalize_room_path(transition["target_path"]);
            if (next_room == "" || visited[next_room] || !mapp(topology[next_room]))
                continue;
            visited[next_room] = 1;
            parent_by_room[next_room] = current;
            step_by_room[next_room] = ([
                "kind": "transfer",
                "edge_type": "TRANSFER",
                "from": current,
                "to": next_room,
                "transition": copy(transition),
            ]);
            queue += ({ next_room });
        }
    }
    if (!visited[target_room])
        return ([ "ok": 0, "error": "unreachable", "commands": ({ }), "steps": ({ }),
            "topology_generation": _route_topology_generation ]);

    commands = ({ });
    rooms = ({ target_room });
    steps = ({ });
    transfer_count = 0;
    current = target_room;
    guard = 0;
    while (current != start_room && guard++ < max_steps + 1) {
        step = step_by_room[current];
        if (!mapp(step) || !stringp(parent_by_room[current]))
            return ([ "ok": 0, "error": "broken_route", "commands": ({ }), "steps": ({ }) ]);
        steps = ({ step }) + steps;
        current = parent_by_room[current];
        rooms = ({ current }) + rooms;
    }
    if (current != start_room || sizeof(steps) > max_steps)
        return ([ "ok": 0, "error": "too_far", "commands": ({ }), "steps": ({ }),
            "topology_generation": _route_topology_generation ]);
    foreach (step in steps) {
        if (step["kind"] == "walk")
            commands += ({ step["direction"] });
        else if (step["kind"] == "transfer")
            transfer_count++;
    }
    return ([
        "ok": 1,
        "error": "",
        "commands": commands,
        "steps": steps,
        "rooms": rooms,
        "topology_generation": _route_topology_generation,
        "transfer_count": transfer_count,
    ]);
}

mapping query_route_commands(string start_room, string target_room, int max_steps)
{
    return query_route_plan(start_room, target_room, max_steps);
}

private void ensure_export_dirs()
{
    if (file_size(EXPORT_DIR) != -2)
        mkdir(EXPORT_DIR);
}

private int flush_export_buffer()
{
    int write_mode;

    if (!stringp(_export_buffer) || _export_buffer == "")
        return 1;
    write_mode = 1;
    if (file_size(EXPORT_TMP_PATH) >= 0)
        write_mode = 0;
    if (!write_file(EXPORT_TMP_PATH, _export_buffer, write_mode)) {
        _last_export_error = "write_file failed";
        return 0;
    }
    _export_buffer = "";
    return 1;
}

private int append_export_text(string text)
{
    if (!stringp(text))
        return 0;

    if (!stringp(_export_buffer))
        _export_buffer = "";
    _export_buffer += text;
    if (strlen(_export_buffer) < EXPORT_CHUNK_LIMIT)
        return 1;
    return flush_export_buffer();
}

private int append_export_json(mixed value)
{
    string payload;
    mixed err;

    err = catch(payload = json_encode(value));
    if (err || !stringp(payload)) {
        _last_export_error = sprintf("json_encode failed: %O", err);
        return 0;
    }
    return append_export_text(payload);
}

private int append_export_json_array(mixed *items)
{
    int index;

    if (!arrayp(items))
        items = ({ });

    if (!append_export_text("["))
        return 0;
    for (index = 0; index < sizeof(items); index++) {
        if (index > 0 && !append_export_text(","))
            return 0;
        if (!append_export_json(items[index]))
            return 0;
    }
    return append_export_text("]");
}

private int append_room_nodes_batch(mapping state, int limit)
{
    mapping package_config;
    mapping metadata;
    mapping rooms;
    mapping room_tags;
    mapping room_index;
    string *package_paths;
    string *room_paths;
    string package_path;
    string room_path;
    int package_index;
    int room_index_pos;
    int written;

    package_paths = state["package_paths"];
    room_index = state["room_index"];
    package_index = state["package_index"];
    room_index_pos = state["room_index_pos"];
    written = 0;

    while (package_index < sizeof(package_paths) && written < limit) {
        package_path = package_paths[package_index];
        package_config = query_region_package_config(package_path);
        metadata = normalize_map_metadata(package_config["map_metadata"]);
        rooms = mapp(package_config["rooms"]) ? package_config["rooms"] : ([]);
        room_tags = metadata["room_tags"];
        room_paths = query_sorted_room_paths(rooms);

        while (room_index_pos < sizeof(room_paths) && written < limit) {
            room_path = room_paths[room_index_pos];
            if (mapp(rooms[room_path])) {
                if (state["node_count"] > 0 && !append_export_text(","))
                    return 0;
                if (!append_export_json(build_room_node(room_path, rooms[room_path], room_index, room_tags)))
                    return 0;
                state["node_count"] += 1;
                written += 1;
            }
            room_index_pos += 1;
        }

        if (room_index_pos >= sizeof(room_paths)) {
            package_index += 1;
            room_index_pos = 0;
        }
    }

    state["package_index"] = package_index;
    state["room_index_pos"] = room_index_pos;
    if (package_index >= sizeof(package_paths))
        state["stage"] = "edges_start";
    return 1;
}

private int append_room_edges_batch(mapping state, int limit)
{
    mapping package_config;
    mapping rooms;
    mapping room;
    mapping exits;
    mapping room_index;
    string *package_paths;
    string *room_paths;
    string *exit_dirs;
    string package_path;
    string room_path;
    string dir;
    string target;
    int package_index;
    int room_index_pos;
    int exit_index_pos;
    int written;

    package_paths = state["package_paths"];
    room_index = state["room_index"];
    package_index = state["package_index"];
    room_index_pos = state["room_index_pos"];
    exit_index_pos = state["exit_index_pos"];
    written = 0;

    while (package_index < sizeof(package_paths) && written < limit) {
        package_path = package_paths[package_index];
        package_config = query_region_package_config(package_path);
        rooms = mapp(package_config["rooms"]) ? package_config["rooms"] : ([]);
        room_paths = query_sorted_room_paths(rooms);

        while (room_index_pos < sizeof(room_paths) && written < limit) {
            room_path = room_paths[room_index_pos];
            room = rooms[room_path];
            exits = mapp(room) ? room["exits"] : 0;
            exit_dirs = query_sorted_exit_dirs(exits);
            while (exit_index_pos < sizeof(exit_dirs) && written < limit) {
                dir = exit_dirs[exit_index_pos];
                target = exits[dir];
                if (state["edge_count"] > 0 && !append_export_text(","))
                    return 0;
                if (!append_export_json(([
                    "from": room_path,
                    "to": stringp(target) ? target : "",
                    "direction": dir,
                    "edge_type": "walk",
                    "valid": stringp(target) && mapp(room_index[target]) ? 1 : 0,
                ])))
                    return 0;
                state["edge_count"] += 1;
                written += 1;
                exit_index_pos += 1;
            }
            if (exit_index_pos >= sizeof(exit_dirs)) {
                room_index_pos += 1;
                exit_index_pos = 0;
            }
        }

        if (room_index_pos >= sizeof(room_paths)) {
            package_index += 1;
            room_index_pos = 0;
            exit_index_pos = 0;
        }
    }

    state["package_index"] = package_index;
    state["room_index_pos"] = room_index_pos;
    state["exit_index_pos"] = exit_index_pos;
    if (package_index >= sizeof(package_paths))
        state["stage"] = "overworld_start";
    return 1;
}

private int append_overworld_nodes_for_package(mapping package_config, mapping room_index, mapping state)
{
    mapping metadata;
    mapping rooms;
    mapping *nodes;
    int index;

    metadata = normalize_map_metadata(package_config["map_metadata"]);
    rooms = mapp(package_config["rooms"]) ? package_config["rooms"] : ([]);
    nodes = build_overworld_nodes(metadata["regions"], rooms, room_index, metadata["room_tags"]);
    for (index = 0; index < sizeof(nodes); index++) {
        if (state["overworld_node_count"] > 0 && !append_export_text(","))
            return 0;
        if (!append_export_json(nodes[index]))
            return 0;
        state["overworld_node_count"] += 1;
    }
    return 1;
}

private int append_overworld_edges_for_package(mapping package_config, mapping state)
{
    mapping metadata;
    mapping rooms;
    mapping *edges;
    int index;

    metadata = normalize_map_metadata(package_config["map_metadata"]);
    rooms = mapp(package_config["rooms"]) ? package_config["rooms"] : ([]);
    edges = build_overworld_edges(metadata["regions"], rooms);
    for (index = 0; index < sizeof(edges); index++) {
        if (state["overworld_edge_count"] > 0 && !append_export_text(","))
            return 0;
        if (!append_export_json(edges[index]))
            return 0;
        state["overworld_edge_count"] += 1;
    }
    return 1;
}

private int append_submaps(mapping package_config, mapping room_index, mapping state)
{
    mapping metadata;
    mapping rooms;
    mapping *submaps;
    int index;

    metadata = normalize_map_metadata(package_config["map_metadata"]);
    rooms = mapp(package_config["rooms"]) ? package_config["rooms"] : ([]);
    submaps = build_submaps(rooms, rooms, metadata["room_tags"], metadata["submaps"]);
    for (index = 0; index < sizeof(submaps); index++) {
        if (state["submap_count"] > 0 && !append_export_text(","))
            return 0;
        if (!append_export_json(submaps[index]))
            return 0;
        state["submap_count"] += 1;
    }
    return 1;
}

private void reset_export_state()
{
    if (_export_call_out_handle) {
        remove_call_out(_export_call_out_handle);
        _export_call_out_handle = 0;
    }
    _export_state = ([]);
    _export_buffer = "";
}

private void schedule_export_step()
{
    if (_export_call_out_handle)
        remove_call_out(_export_call_out_handle);
    _export_call_out_handle = call_out("process_export_step", 0);
}

private int finish_export_file()
{
    rm(EXPORT_BACKUP_PATH);
    if (file_size(EXPORT_PATH) >= 0)
        rename(EXPORT_PATH, EXPORT_BACKUP_PATH);
    if (rename(EXPORT_TMP_PATH, EXPORT_PATH) != 0) {
        if (file_size(EXPORT_BACKUP_PATH) >= 0)
            rename(EXPORT_BACKUP_PATH, EXPORT_PATH);
        _last_export_error = "rename temp export failed";
        return 0;
    }
    rm(EXPORT_BACKUP_PATH);
    return file_size(EXPORT_PATH) > 0;
}

void process_export_step()
{
    mapping state;
    mapping package_config;
    string *package_paths;
    string package_path;
    mixed err;

    _export_call_out_handle = 0;
    state = _export_state;
    if (!mapp(state) || state["status"] != "running")
        return;

    err = catch {
        package_paths = state["package_paths"];
        switch (state["stage"]) {
        case "nodes":
            if (!append_room_nodes_batch(state, EXPORT_NODE_BATCH_LIMIT))
                error(_last_export_error);
            break;
        case "edges_start":
            append_export_text("],\"edges\":[");
            state["stage"] = "edges";
            state["package_index"] = 0;
            state["room_index_pos"] = 0;
            state["exit_index_pos"] = 0;
            break;
        case "edges":
            if (!append_room_edges_batch(state, EXPORT_EDGE_BATCH_LIMIT))
                error(_last_export_error);
            break;
        case "overworld_start":
            append_export_text("],\"overworld\":{\"nodes\":[");
            foreach (package_path in package_paths) {
                package_config = query_region_package_config(package_path);
                if (!append_overworld_nodes_for_package(package_config, state["room_index"], state))
                    error(_last_export_error);
            }
            append_export_text("],\"edges\":[");
            foreach (package_path in package_paths) {
                package_config = query_region_package_config(package_path);
                if (!append_overworld_edges_for_package(package_config, state))
                    error(_last_export_error);
            }
            append_export_text("]},\"submaps\":[");
            foreach (package_path in package_paths) {
                package_config = query_region_package_config(package_path);
                if (!append_submaps(package_config, state["room_index"], state))
                    error(_last_export_error);
            }
            append_export_text("],\"report\":");
            append_export_json(state["report"]);
            append_export_text("}\n");
            if (!flush_export_buffer())
                error(_last_export_error);
            if (!finish_export_file())
                error(_last_export_error);
            state["status"] = "done";
            state["finished_at"] = time();
            break;
        }
    };

    if (err) {
        _last_export_error = sprintf("export_world_map failed: %O", err);
        state["status"] = "failed";
        state["last_error"] = _last_export_error;
        rm(EXPORT_TMP_PATH);
        return;
    }

    if (state["status"] == "running")
        schedule_export_step();
}

string query_last_export_error()
{
    return stringp(_last_export_error) ? _last_export_error : "";
}

mapping query_export_status()
{
    mapping state;

    state = mapp(_export_state) ? _export_state : ([]);
    return ([
        "status": stringp(state["status"]) ? state["status"] : "idle",
        "stage": stringp(state["stage"]) ? state["stage"] : "",
        "node_count": intp(state["node_count"]) ? state["node_count"] : 0,
        "edge_count": intp(state["edge_count"]) ? state["edge_count"] : 0,
        "overworld_node_count": intp(state["overworld_node_count"]) ? state["overworld_node_count"] : 0,
        "overworld_edge_count": intp(state["overworld_edge_count"]) ? state["overworld_edge_count"] : 0,
        "submap_count": intp(state["submap_count"]) ? state["submap_count"] : 0,
        "file_size": file_size(EXPORT_PATH),
        "last_error": stringp(state["last_error"]) ? state["last_error"] : _last_export_error,
    ]);
}

int export_world_map()
{
    mapping state;
    mapping package_config;
    mapping room_index;
    string *package_paths;
    string package_path;
    string room_path;
    mapping rooms;
    mapping room;
    mapping report;
    mixed err;

    _last_export_error = "";
    _export_buffer = "";
    ensure_export_dirs();
    rm(EXPORT_TMP_PATH);

    err = catch(package_paths = query_region_package_paths());
    if (err || !arrayp(package_paths)) {
        _last_export_error = sprintf("query_region_package_paths failed: %O", err);
        return 0;
    }

    err = catch(report = query_quality_report());
    if (err || !mapp(report)) {
        _last_export_error = sprintf("query_quality_report failed: %O", err);
        return 0;
    }

    room_index = ([]);
    foreach (package_path in package_paths) {
        package_config = query_region_package_config(package_path);
        rooms = mapp(package_config["rooms"]) ? package_config["rooms"] : ([]);
        foreach (room_path, room in rooms)
            room_index[room_path] = room;
    }

    reset_export_state();
    state = ([
        "status": "running",
        "stage": "nodes",
        "package_paths": package_paths,
        "room_index": room_index,
        "report": report,
        "package_index": 0,
        "room_index_pos": 0,
        "exit_index_pos": 0,
        "node_count": 0,
        "edge_count": 0,
        "overworld_node_count": 0,
        "overworld_edge_count": 0,
        "submap_count": 0,
        "started_at": time(),
        "last_error": "",
    ]);
    _export_state = state;

    err = catch {
        append_export_text("{\"schema\":");
        append_export_json("xkx-world-map-v2");
        append_export_text(",\"generated_at\":");
        append_export_json(ctime(time()));
        append_export_text(",\"source\":");
        append_export_json("server/daemons/services/world/world_map_d.c");
        append_export_text(",\"nodes\":[");
        if (!flush_export_buffer())
            error(_last_export_error);
    };
    if (err) {
        _last_export_error = sprintf("export_world_map failed: %O", err);
        rm(EXPORT_TMP_PATH);
        reset_export_state();
        return 0;
    }

    schedule_export_step();
    return 1;
}
