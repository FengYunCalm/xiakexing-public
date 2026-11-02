/**
 * @file    dungeon_instance_d.c
 * @brief   副本定义到通用实例房间服务的兼容适配器。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

nosave mapping _dungeon_contexts;

private mapping resolve_instance_templates(mapping dungeon)
{
    mapping definition;

    if (!mapp(dungeon) || !stringp(dungeon["type"]))
        return 0;
    definition = DUNGEON_D->query_dungeon_definition(dungeon["type"]);
    if (!mapp(definition) || !mapp(definition["instance_templates"]))
        return 0;
    return definition;
}

int create_instance_rooms(string dungeon_id, mapping dungeon)
{
    mapping context;
    mapping definition;
    object room;
    string template_path;

    if (!stringp(dungeon_id) || dungeon_id == "" || !mapp(dungeon))
        return 0;
    definition = resolve_instance_templates(dungeon);
    if (!mapp(definition))
        return 0;
    context = ([
        "kind": "dungeon",
        "dungeon_id": dungeon_id,
        "dungeon_type": dungeon["type"] || "",
        "instance_id": dungeon["instance_id"] || "dungeon:" + dungeon_id,
        "owner_id": dungeon["owner_id"] || "",
        "state_scope": dungeon["state_scope"] || "",
        "instance_scope": dungeon["instance_scope"] || "",
    ]);
    foreach (template_path in values(definition["instance_templates"])) {
        room = INSTANCE_ROOM_D->resolve_instance_room(
            context["instance_id"], template_path, context);
        if (!objectp(room)) {
            INSTANCE_ROOM_D->release_instance(context["instance_id"]);
            return 0;
        }
    }
    if (!INSTANCE_ROOM_D->rewrite_instance_exits(context["instance_id"])) {
        INSTANCE_ROOM_D->release_instance(context["instance_id"]);
        return 0;
    }
    context["entry_template"] = definition["instance_entry_room"] || "";
    _dungeon_contexts[dungeon_id] = context;
    return 1;
}

object query_instance_room(string dungeon_id, string template_path)
{
    mapping context;

    context = _dungeon_contexts[dungeon_id];
    if (!mapp(context))
        return 0;
    return INSTANCE_ROOM_D->query_instance_room(context["instance_id"], template_path);
}

object query_instance_entry_room(string dungeon_id, mapping dungeon)
{
    mapping context;

    if (!create_instance_rooms(dungeon_id, dungeon))
        return 0;
    context = _dungeon_contexts[dungeon_id];
    if (!mapp(context) || !stringp(context["entry_template"])
        || context["entry_template"] == "")
        return 0;
    return INSTANCE_ROOM_D->resolve_instance_room(context["instance_id"],
        context["entry_template"], context);
}

int destroy_instance_rooms(string dungeon_id)
{
    mapping context;

    context = _dungeon_contexts[dungeon_id];
    if (!mapp(context))
        return 1;
    if (!INSTANCE_ROOM_D->release_instance(context["instance_id"]))
        return 0;
    map_delete(_dungeon_contexts, dungeon_id);
    return 1;
}

void create()
{
    ::create();
    _dungeon_contexts = ([]);
}
