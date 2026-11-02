/**
 * @file    util_direction.c
 * @brief   方向工具 - Direction Utilities
 */

#pragma strict_types
#pragma save_types

string normalize_direction(string dir)
{
    mapping dir_map;

    if (!stringp(dir) || dir == "")
        return 0;

    /*
     * 标准化入口只接受用户输入别名，返回统一出口键名。
     * 调用方仍然负责确认该方向在当前房间是否真的可走。
     */
    dir = lower_case(dir);
    dir_map = ([
        "n" : "north", "north" : "north",
        "s" : "south", "south" : "south",
        "e" : "east", "east" : "east",
        "w" : "west", "west" : "west",
        "ne" : "northeast", "northeast" : "northeast",
        "nw" : "northwest", "northwest" : "northwest",
        "se" : "southeast", "southeast" : "southeast",
        "sw" : "southwest", "southwest" : "southwest",
        "u" : "up", "up" : "up",
        "d" : "down", "down" : "down",
        "nu" : "northup", "northup" : "northup",
        "nd" : "northdown", "northdown" : "northdown",
        "su" : "southup", "southup" : "southup",
        "sd" : "southdown", "southdown" : "southdown",
        "eu" : "eastup", "eastup" : "eastup",
        "ed" : "eastdown", "eastdown" : "eastdown",
        "wu" : "westup", "westup" : "westup",
        "wd" : "westdown", "westdown" : "westdown",
        "enter" : "enter",
        "out" : "out"
    ]);

    return dir_map[dir];
}

string direction_name(string dir)
{
    mapping names;

    /*
     * 未登记方向保持原样返回，方便兼容自定义出口而不在工具层擅自改写。
     */
    names = ([
        "north" : "北边",
        "south" : "南边",
        "east" : "东边",
        "west" : "西边",
        "northeast" : "东北",
        "northwest" : "西北",
        "southeast" : "东南",
        "southwest" : "西南",
        "up" : "上面",
        "down" : "下面",
        "northup" : "北上",
        "northdown" : "北下",
        "southup" : "南上",
        "southdown" : "南下",
        "eastup" : "东上",
        "eastdown" : "东下",
        "westup" : "西上",
        "westdown" : "西下",
        "enter" : "里面",
        "out" : "外面"
    ]);

    return names[dir] || dir;
}
