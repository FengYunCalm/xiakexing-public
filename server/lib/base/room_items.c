/**
 * @file    room_items.c
 * @brief   房间物品子模块



 *
 * @par Description:
 * 基础对象层子模块，供上层对象组合能力。
 */

#pragma strict_types

mixed query(string path);
int set(string path, mixed value);

void add_item(string id, string desc)
{
    mapping items;

    items = query("items");
    if (!mapp(items)) items = ([]);
    items[id] = desc;
    set("items", items);
}

string query_item_desc(string id)
{
    mapping items;

    items = query("items");
    if (!mapp(items)) return 0;
    return items[id];
}
