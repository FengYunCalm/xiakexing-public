/**
 * @file    world_context_d.c
 * @brief   合并实例房间与角色作用域。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

private string query_player_id(object player)
{
    string player_id;

    if (!objectp(player))
        return "unknown";
    player_id = get_object_id(player);
    return stringp(player_id) && player_id != "" ? player_id : "unknown";
}

private string query_region_id(mixed room_or_path)
{
    mapping context;
    string template_path;

    template_path = INSTANCE_ROOM_D->query_template_room_path(room_or_path);
    context = REGION_D->resolve_region_context(template_path);
    return mapp(context) && context["ok"] && stringp(context["region_id"])
        ? context["region_id"] : "";
}

string query_player_world_scope_key(object player, mixed room_or_path)
{
    string region_id;

    region_id = query_region_id(room_or_path);
    return region_id == "" ? "" : "shared:" + region_id;
}

string query_player_personal_scope_key(object player, mixed room_or_path)
{
    string player_id;
    string region_id;

    region_id = query_region_id(room_or_path);
    player_id = query_player_id(player);
    if (region_id == "" || player_id == "unknown")
        return "";
    return "player:" + player_id + ":" + region_id;
}

mapping query_player_world_context(object player, mixed room_or_path)
{
    mapping region;
    mapping map_room;
    string runtime_room;
    string template_room;

    template_room = INSTANCE_ROOM_D->query_template_room_path(room_or_path);
    runtime_room = objectp(room_or_path) ? file_name(room_or_path)
        : (stringp(room_or_path) ? room_or_path : "");
    region = REGION_D->query_room_region(template_room);
    if (!mapp(region))
        return ([
            "ok": 0,
            "error_code": "REGION_UNRESOLVED",
            "scope_key": "",
            "personal_scope_key": "",
            "template_room": template_room,
            "runtime_room": runtime_room,
            "region": ([]),
            "map_context": ([ ]),
            "room": template_room,
        ]);
    map_room = CONTENT_REGISTRY_D->query_map_room_snapshot(template_room);
    return ([
        "ok": 1,
        "error_code": "",
        "scope_key": query_player_world_scope_key(player, room_or_path),
        "personal_scope_key": query_player_personal_scope_key(player, room_or_path),
        "template_room": template_room,
        "runtime_room": runtime_room,
        "region": region,
        "map_context": mapp(map_room) ? ([
            "region_id": map_room["region_id"] || region["id"] || "",
            "map_context_id": map_room["map_context_id"] || "",
            "map_layer": map_room["map_layer"] || "",
            "submap_id": map_room["submap_id"] || "",
            "content_generation": map_room["content_generation"] || 0,
        ]) : ([ ]),
        "room": template_room,
    ]);
}
