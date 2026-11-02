/**
 * @file    network.c
 * @brief   兼容网络消息壳
 * @author  XiaKeXing Development Team
 * @date    2026-03-03
 * @version 2.0.0
 *
 * @par Description:
 * 兼容网络消息壳，保留旧路径可加载性：
 * - 玩家消息发送 (send_message)
 * - 房间广播 (send_room)
 * - 消息格式化
 *
 * @par Note:
 * 当前文件不是命令层 canonical network 基类；稳定入口仍是 `/lib/framework/network`。
 */

#pragma strict_types

inherit "/lib/framework/network";
