/**
 * @file    protocol.h
 * @brief   XK 协议公共声明子集



 *
 * @par Description:
 * XK 协议相关公共声明子集。
 * 当前头文件只保留业务层仍直接依赖的对象列表/状态/快捷操作/技能相关入口；
 * room/shop/trade/chat 家族的完整 helper 清单以 `kernel/simul_efun.c` 与 `kernel/simul_efun_*` 真源为准。
 * 这些接口面向已完成登录并具备 XK 输出能力的目标对象；调用方负责确保
 * 目标对象有效、连接仍在、以及传入数据已经过业务层裁剪和脱敏。
 *
 * @par Usage:
 * #include <game/protocol.h>
 */

#pragma strict_types

#ifndef __GAME_PROTOCOL_H__
#define __GAME_PROTOCOL_H__


/**
 * @brief 发送物品列表
 * @param me 目标玩家
 * @param items 物品对象数组
 * @note items 与房间显示保持同一可见性口径，调用方应先完成过滤。
 */
void xk_send_objects(object me, object *items);

/**
 * @brief 发送玩家状态
 * @param me 目标玩家
 * @note 语义上发送完整状态快照，适合在状态同步点调用，而不是把它当作细粒度字段补丁接口。
 */
varargs void xk_send_status(object me, mapping response_context);

/**
 * @brief 发送快捷命令
 * @param me 目标玩家
 * @param cmds 命令数组
 * @param priority 优先级
 * @note cmds 的 payload 语义由客户端快捷栏协议定义，通常按完整槽位数据处理。
 */
varargs void xk_send_quick_cmds(object me, mixed cmds, int priority,
    mapping response_context);

/**
 * @brief 发送技能列表
 * @param me 目标玩家
 * @note 发送当前可展示技能集合快照，不保证包含隐藏或未解锁技能。
 */
void xk_send_skill_list(object me);

#endif // __GAME_PROTOCOL_H__
