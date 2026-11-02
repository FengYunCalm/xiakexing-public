/**
 * @file    room_exits.c
 * @brief   房间出口子模块



 *
 * @par Description:
 * 基础对象层子模块，供上层对象组合能力。
 */

#pragma strict_types

mixed query(string path);
int set(string path, mixed value);

void set_exits(mapping ex)
{
    set("exits", ex);
}

mapping query_exits()
{
    return query("exits") || ([]);
}

string query_exit(string dir)
{
    mapping ex = query_exits();
    return ex[dir];
}

void add_exit(string dir, string dest)
{
    mapping exits;

    exits = query_exits();
    if (!mapp(exits)) exits = ([]);
    exits[dir] = dest;
    set("exits", exits);
}

void remove_exit(string dir)
{
    mapping exits;

    exits = query_exits();
    if (mapp(exits)) {
        map_delete(exits, dir);
        set("exits", exits);
    }
}

void set_door(string dir, mapping door)
{
    mapping doors;

    doors = query("doors");
    if (!mapp(doors)) doors = ([]);
    doors[dir] = door;
    set("doors", doors);
}

mapping query_door(string dir)
{
    mapping doors;

    doors = query("doors");
    if (!mapp(doors)) return 0;
    return doors[dir];
}

int is_door_closed(string dir)
{
    mapping door = query_door(dir);
    if (!mapp(door)) return 0;
    return door["closed"] || 0;
}

int is_door_locked(string dir)
{
    mapping door = query_door(dir);
    if (!mapp(door)) return 0;
    return door["locked"] || 0;
}
