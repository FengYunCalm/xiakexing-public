/**
 * @file    room_lifecycle.c
 * @brief   房间生命周期子模块



 *
 * @par Description:
 * 基础对象层子模块，供上层对象组合能力。
 * reset 留空是刻意的占位设计，不代表房间必须实现周期刷新。
 */

#pragma strict_types

void reset()
{
    /* 基类默认不注入补刷或重置策略，避免误改上层房间生命周期。 */
}
