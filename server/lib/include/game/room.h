/**
 * @file    room.h
 * @brief   房间系统头文件 - Room System Header



 *
 * @par Description:
 * 房间系统相关的宏定义和函数声明。
 * 这里的常量主要描述通用房间标记语义，具体存储键和房间对象内部实现
 * 仍以房间基类/真源服务为准。
 */

#pragma strict_types

#ifndef __GAME_ROOM_H__
#define __GAME_ROOM_H__

#include <system/globals.h>


#define ROOM_OUTDOOR        1
#define ROOM_INDOOR         0
#define ROOM_NO_FIGHT       1
#define ROOM_NO_SLEEP       1


#define DOOR_CLOSED         0
#define DOOR_OPENED         1
#define DOOR_LOCKED         2
#define DOOR_SMASHED        3


#define ROOM_TYPE_NORMAL    "normal"
#define ROOM_TYPE_SHOP      "shop"
#define ROOM_TYPE_PUB       "pub"
#define ROOM_TYPE_GUILD     "guild"
#define ROOM_TYPE_DUNGEON   "dungeon"


// 注意：这里不再声明 `find_room/query_exits/query_room_name/valid_leave/valid_enter`
// 这类看似通用的 free-function 契约。
// 当前仓库里这些能力分别属于：
// - 房间对象实例方法（如 room->query("exits") / room->valid_leave(...)）
// - 具体 helper / 真源服务
// 继续在头文件里给出统一函数声明只会制造“到处都能直接调用”的假象。

#endif // __GAME_ROOM_H__
