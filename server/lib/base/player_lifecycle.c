/**
 * @file    player_lifecycle.c
 * @brief   玩家生命周期子模块



 *
 * @par Description:
 * 基础对象层子模块，供上层对象组合能力。
 * 这里只声明玩家初始化协作点，具体顺序和实现仍由宿主玩家基类掌握。
 */

#pragma strict_types

string init_identity();
/* init_attributes 依赖身份结果，调用方应先拿到稳定 my_id 再继续后续初始化。 */
void init_attributes(string my_id);
/* 战斗与技能初始化拆开声明，方便宿主按兼容需要控制先后和覆盖点。 */
void init_combat_stats();
void init_skills();
