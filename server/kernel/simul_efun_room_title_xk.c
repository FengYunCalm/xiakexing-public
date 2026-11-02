/**
 * @file    simul_efun_room_title_xk.c
 * @brief   房间标题 payload 构建辅助（自 simul_efun_room_meta_xk.c 拆分）
 */

#pragma strict_types

#include <system/room_meta_helpers.h>
#include <system/services.h>
#include <system/xk_protocol.h>

#define XK_PROTOCOL_CORE "/kernel/framework/xk_protocol_core"
#define ROOM_DISPLAY_NAME_CACHE_LIMIT 128

inherit "/lib/std/utils/util_owner_projection";

// ---- 前向声明 ----
void xk_send_title(object me, string title, string zone);
mapping xk_build_title_payload(string title, string zone);
mapping xk_build_room_title_payload(string title, string zone, string room_id);
string resolve_room_display_name(string room_path);


private mapping room_display_name_cache = ([]);


private void remember_room_display_name(string cache_key, string room_name)
{
    string *cache_keys;

    if (!stringp(cache_key) || cache_key == ""
        || !stringp(room_name) || room_name == "")
        return;
    if (!mapp(room_display_name_cache))
        room_display_name_cache = ([]);
    if (!stringp(room_display_name_cache[cache_key])
        && sizeof(room_display_name_cache) >= ROOM_DISPLAY_NAME_CACHE_LIMIT) {
        cache_keys = keys(room_display_name_cache);
        if (sizeof(cache_keys) > 0)
            map_delete(room_display_name_cache, cache_keys[0]);
    }
    room_display_name_cache[cache_key] = room_name;
}


void xk_send_title(object me, string title, string zone)
{
    object room;

    if (!me || !interactive(me) || !stringp(title) || title == "")
        return;

    room = environment(me);
    call_other(XK_PROTOCOL_CORE, "xk_send", me, XK_TITL,
        xk_build_room_title_payload(title, zone,
            objectp(room) ? base_name(room) : ""));
}

mapping xk_build_title_payload(string title, string zone)
{
    mapping payload;

    payload = ([ "title": stringp(title) && title != "" ? title : "未知地点" ]);
    if (stringp(zone) && zone != "")
        payload["zone"] = zone;

    return payload;
}

mapping xk_build_room_title_payload(string title, string zone, string room_id)
{
    mapping payload;

    payload = xk_build_title_payload(title, zone);
    if (stringp(room_id) && room_id != "")
        payload["room_id"] = room_id;
    return payload;
}

/* 实例身份权威标记（room_view 的 build_room_meta_view 补发）：
 * dungeon 实例房间带 instance_kind="dungeon" + dungeon_type；
 * 共享地图 gate 房间带 dungeon_gate。客户端据此区分共享地图与副本实例，
 * 不再靠标题/zone 启发式。instance_kind/dungeon_gate 无条件写键：
 * 空串是权威负值（离开副本后必须能清除客户端状态），不能省略。 */
mapping xk_build_room_title_payload_ex(string title, string zone, string room_id,
    string instance_kind, string dungeon_type, string dungeon_gate)
{
    mapping payload;

    payload = xk_build_room_title_payload(title, zone, room_id);
    payload["instance_kind"] = stringp(instance_kind) ? instance_kind : "";
    if (stringp(dungeon_type) && dungeon_type != "")
        payload["dungeon_type"] = dungeon_type;
    payload["dungeon_gate"] = stringp(dungeon_gate) ? dungeon_gate : "";
    return payload;
}

string resolve_room_display_name(string room_path)
{
    mapping room_snapshot;
    string cache_key;
    string room_name;
    string *parts;

    if (!stringp(room_path) || room_path == "")
        return "未知地点";

    cache_key = sprintf("%d#%s", CONTENT_REGISTRY_D->query_content_generation(), room_path);
    if (mapp(room_display_name_cache) && stringp(room_display_name_cache[cache_key])
        && room_display_name_cache[cache_key] != "") {
        return room_display_name_cache[cache_key];
    }

    room_snapshot = ROOM_EXITS_XK_HELPER->query_map_room_snapshot(room_path, 0, 0);
    room_name = mapp(room_snapshot) ? room_snapshot["title"] : 0;
    if (stringp(room_name) && room_name != "") {
        remember_room_display_name(cache_key, room_name);
        return room_name;
    }

    parts = explode(room_path, "/");
    if (sizeof(parts) > 0)
        return replace_string(parts[<1], "_", " ");

    return room_path;
}
