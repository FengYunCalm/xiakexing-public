/**
 * @file    room_identity.c
 * @brief   房间标识子模块



 *
 * @par Description:
 * 基础对象层子模块，供上层对象组合能力。
 */

#pragma strict_types

mixed query(string path);
int set(string path, mixed value);

void set_room_id(string id)
{
    set("room_id", id);
}

string query_room_id()
{
    return query("room_id") || file_name(this_object());
}
