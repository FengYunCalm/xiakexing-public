/**
 * @file    event_emitter.c
 * @brief   事件发射子模块



 *
 * @par Description:
 * 事件系统内部子模块，供 event_d 或相关门面组合使用。
 * `core/event/*` 仅保留 compatibility wrapper 职责，emit 链路直接回到 /daemons/core/event_d。
 */

#pragma strict_types
#pragma save_types

void emit(string event, mixed data)
{
    "/daemons/core/event_d"->emit(event, data);
}
