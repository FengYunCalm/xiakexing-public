/**
 * @file    room_flags.c
 * @brief   房间标记子模块



 *
 * @par Description:
 * 基础对象层子模块，供上层对象组合能力。
 */

#pragma strict_types

mixed query(string path);
int set(string path, mixed value);

void set_no_fight(int flag)
{
    set("no_fight", flag);
}

int query_no_fight()
{
    return query("no_fight") || 0;
}

void set_no_magic(int flag)
{
    set("no_magic", flag);
}

int query_no_magic()
{
    return query("no_magic") || 0;
}

void set_indoor(int flag)
{
    set("indoor", flag);
}

int query_indoor()
{
    return query("indoor") || 0;
}
