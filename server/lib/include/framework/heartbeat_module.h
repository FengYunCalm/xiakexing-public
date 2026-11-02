/**
 * @file    heartbeat_module.h
 * @brief   心跳框架共享常量头文件
 *
 * @note
 * 统一 living 侧 batched heartbeat 的 profile / batch 命名，
 * 避免 player、npc 和运维视图继续散落硬编码字符串。
 */

#pragma strict_types

#ifndef __HEARTBEAT_MODULE_H__
#define __HEARTBEAT_MODULE_H__

#define HEARTBEAT_PROFILE_DEFAULT "default"
#define HEARTBEAT_PROFILE_PLAYER "player"
#define HEARTBEAT_PROFILE_NPC "npc"

#define HEARTBEAT_BATCH_DEFAULT "default"
#define HEARTBEAT_BATCH_PLAYER "living.player"
#define HEARTBEAT_BATCH_NPC "living.npc"

#endif /* __HEARTBEAT_MODULE_H__ */
