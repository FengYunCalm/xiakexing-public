/**
 * @file    content_room.c
 * @brief   配置化房间共享基类
 */

#pragma strict_types

#include <system/services.h>

inherit "/lib/base/room";

private int content_validation_active()
{
    int active;
    mixed err;

    err = catch(active = CONTENT_REGISTRY_D->is_content_validation_active());
    return !err && active;
}

private mapping query_content_room_def()
{
    mapping def;

    def = CONTENT_REGISTRY_D->query_room(base_name(this_object()));
    return mapp(def) ? def : ([ ]);
}

private mapping query_content_room_area(mapping def)
{
    mapping area;

    if (!mapp(def) || !stringp(def["area"]) || def["area"] == "")
        return 0;

    area = CONTENT_REGISTRY_D->query_area(def["area"]);
    return mapp(area) ? area : 0;
}

private string query_content_room_title(mapping def, mapping area)
{
    if (stringp(def["title"]) && def["title"] != "")
        return def["title"];
    if (mapp(area) && stringp(area["name"]) && area["name"] != "")
        return area["name"];
    return "无名之地";
}

private string query_content_room_zone(mapping def, mapping area)
{
    if (stringp(def["zone"]) && def["zone"] != "")
        return def["zone"];
    if (mapp(area) && stringp(area["id"]) && area["id"] != "")
        return area["id"];
    return "world";
}

private mapping query_content_room_snapshot()
{
    mixed owned_fields;
    mixed owned_exits;

    owned_fields = query_temp("content_room/owned_fields");
    owned_exits = query_temp("content_room/owned_exits");
    return ([
        "short": query("short"),
        "zone": query("zone"),
        "exits": mapp(query("exits")) ? copy(query("exits")) : ([]),
        "outdoors": query("outdoors"),
        "no_fight": query("no_fight"),
        "owned_fields": mapp(owned_fields) ? copy(owned_fields) : 0,
        "owned_exits": mapp(owned_exits) ? copy(owned_exits) : 0,
        "initialized": query_temp("content_room/initialized"),
    ]);
}

private int restore_content_room_snapshot(mapping snapshot)
{
    int ok;

    if (!mapp(snapshot))
        return 0;
    ok = 1;
    ok = set("short", snapshot["short"]) && ok;
    ok = set("zone", snapshot["zone"]) && ok;
    ok = set("exits", snapshot["exits"]) && ok;
    ok = set("outdoors", snapshot["outdoors"]) && ok;
    ok = set("no_fight", snapshot["no_fight"]) && ok;
    if (mapp(snapshot["owned_fields"]))
        set_temp("content_room/owned_fields", copy(snapshot["owned_fields"]));
    else
        delete_temp("content_room/owned_fields");
    if (mapp(snapshot["owned_exits"]))
        set_temp("content_room/owned_exits", copy(snapshot["owned_exits"]));
    else
        delete_temp("content_room/owned_exits");
    if (snapshot["initialized"] == 1)
        set_temp("content_room/initialized", 1);
    else
        delete_temp("content_room/initialized");
    return ok;
}

private mapping query_content_room_exits(mapping def, int initialized)
{
    mapping current;
    mapping desired;
    mapping owned;
    mapping merged;
    string direction;
    mixed destination;
    int current_has;
    int owned_has;

    current = query("exits");
    if (!mapp(current))
        current = ([]);
    desired = mapp(def["exits"]) ? copy(def["exits"]) : ([]);
    if (!initialized)
        return desired;

    owned = query_temp("content_room/owned_exits");
    merged = ([]);
    foreach (direction, destination in current) {
        owned_has = mapp(owned)
            && member_array(direction, keys(owned)) >= 0;
        if (!owned_has || owned[direction] != destination)
            merged[direction] = destination;
    }
    foreach (direction, destination in desired) {
        current_has = member_array(direction, keys(current)) >= 0;
        owned_has = mapp(owned)
            && member_array(direction, keys(owned)) >= 0;
        if (!current_has || (owned_has && current[direction] == owned[direction]))
            merged[direction] = destination;
    }
    return merged;
}

private mixed query_content_room_field(string field, mixed desired,
    int initialized)
{
    mapping owned;
    mixed current;

    if (!initialized)
        return desired;
    owned = query_temp("content_room/owned_fields");
    current = query(field);
    if (!mapp(owned) || member_array(field, keys(owned)) < 0)
        return current;
    return current == owned[field] ? desired : current;
}

int refresh_content_room(int generation)
{
    mapping area;
    mapping before;
    mapping def;
    mapping desired_fields;
    mapping owned_fields;
    mapping exits;
    int allow_fight;
    int initialized;
    int ok;
    int safe;

    if (!intp(generation) || generation <= 0)
        return 0;

    def = query_content_room_def();
    area = query_content_room_area(def);
    safe = intp(def["safe"]) ? def["safe"] : 0;
    allow_fight = intp(def["allow_fight"]) ? def["allow_fight"] : 0;
    desired_fields = ([
        "short": query_content_room_title(def, area),
        "zone": query_content_room_zone(def, area),
        "outdoors": intp(def["outdoors"]) ? def["outdoors"] : 0,
        "no_fight": safe == 1 && allow_fight != 1 ? 1 : 0,
    ]);
    initialized = query_temp("content_room/initialized") == 1;
    exits = query_content_room_exits(def, initialized);
    before = query_content_room_snapshot();
    ok = 1;
    ok = set("short", query_content_room_field("short",
        desired_fields["short"], initialized)) && ok;
    ok = set("zone", query_content_room_field("zone",
        desired_fields["zone"], initialized)) && ok;
    ok = set("exits", exits) && ok;
    ok = set("outdoors", query_content_room_field("outdoors",
        desired_fields["outdoors"], initialized)) && ok;
    ok = set("no_fight", query_content_room_field("no_fight",
        desired_fields["no_fight"], initialized)) && ok;
    if (!ok) {
        restore_content_room_snapshot(before);
        return 0;
    }

    owned_fields = ([
        "short": desired_fields["short"],
        "zone": desired_fields["zone"],
        "outdoors": desired_fields["outdoors"],
        "no_fight": desired_fields["no_fight"],
    ]);
    set_temp("content_room/owned_fields", owned_fields);
    set_temp("content_room/owned_exits", copy(mapp(def["exits"])
        ? def["exits"] : ([])));
    set_temp("content_room/generation", generation);
    set_temp("content_room/initialized", 1);
    return 1;
}

/* 房间长描述单一 API——只从 CONTENT_REGISTRY_D 的 room_descriptions
 * 真源读取（契约：基类不消费 dbase long）。look 命令与协议房间正文
 * 共用本函数；无描述返回空串。 */
string query_long()
{
    string desc;

    desc = CONTENT_REGISTRY_D->query_room_description(base_name(this_object()));
    return stringp(desc) ? desc : "";
}

void setup_content_room()
{
    CONTENT_REGISTRY_D->register_loaded_content_room(this_object());
    if (!refresh_content_room(CONTENT_REGISTRY_D->query_content_generation()))
        return;
    if (!content_validation_active())
        ensure_room_entities();
}

void refresh_room_entities()
{
    if (!content_validation_active())
        ensure_room_entities();
}

void init()
{
    ::init();
    if (!content_validation_active())
        ensure_room_entities();
}

/* 离场检查必须是纯预检；房间行为的 leave mutation 由
 * WORLD_TRAVEL_D 在 placement 成功后显式提交。 */
int valid_leave(object me, string dir)
{
    return ::valid_leave(me, dir);
}

void reset()
{
    ::reset();
    if (!content_validation_active())
        ensure_room_entities();
}
