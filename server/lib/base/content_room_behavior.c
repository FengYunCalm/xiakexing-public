#pragma strict_types

#include <system/services.h>

private string query_room_behavior_file(string room_path)
{
    string *parts;

    if (!stringp(room_path) || room_path == "")
        return "";
    parts = explode(room_path, "/");
    return sizeof(parts) > 0 ? parts[<1] : "";
}

mapping query_room_behaviors_for_room(string room_path)
{
    mapping behaviors;
    mapping room_behaviors;
    mapping room_behavior;

    room_path = stringp(room_path) ? room_path : "";
    room_behaviors = CONTENT_REGISTRY_D->query_room_behaviors(room_path);
    if (!mapp(room_behaviors))
        return ([]);
    behaviors = ([ ]);
    foreach (room_behavior in values(room_behaviors)) {
        if (!mapp(room_behavior))
            continue;
        behaviors[room_behavior["id"] || query_room_behavior_file(room_path)] = copy(room_behavior);
    }
    return behaviors;
}

private object query_room_behavior_d()
{
    return load_object("/daemons/services/world/room_behavior_d");
}

int dispatch_room_behavior_command(object player, object room, string verb, string arg)
{
    object room_behavior_d;

    if (!objectp(player) || !objectp(room) || !stringp(verb) || verb == "")
        return 0;
    room_behavior_d = query_room_behavior_d();
    if (!objectp(room_behavior_d))
        return 0;
    return room_behavior_d->dispatch_room_command(player, room, verb, arg);
}

int dispatch_room_behavior_leave(object player, object room, string dir)
{
    object room_behavior_d;

    if (!objectp(player) || !objectp(room) || !stringp(dir) || dir == "")
        return 0;
    room_behavior_d = query_room_behavior_d();
    if (!objectp(room_behavior_d))
        return 0;
    return room_behavior_d->dispatch_room_leave(player, room, dir);
}
