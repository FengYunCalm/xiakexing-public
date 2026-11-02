/**
 * @file    room_directions.c
 * @brief   房间方向子模块



 *
 * @par Description:
 * 基础对象层子模块，供上层对象组合能力。
 */

#pragma strict_types

private mapping _exit_direction_names = ([
    "north": "北", "south": "南", "east": "东", "west": "西",
    "northeast": "东北", "northwest": "西北",
    "southeast": "东南", "southwest": "西南",
    "up": "上", "down": "下",
    "enter": "里", "out": "外"
]);

string get_exit_direction_name(string dir)
{
    return _exit_direction_names[dir] || dir;
}
