/**
 * @file    kill.c
 * @brief   击杀命令别名壳



 *
 * @par Description:
 * `kill` 当前只是 `attack` 的 alias shell，保留独立文件仅为兼容旧命令路径与帮助入口。
 *
 * @par Usage:
 *   kill <目标>    - 攻击并尝试杀死目标
 *
 * @par Examples:
 *   kill bandit    - 攻击强盗并试图杀死他
 *   kill npc       - 攻击指定NPC
 *
 * @par Note:
 *   - 此命令继承自 attack.c，提供相同的攻击功能
 *   - 使用 "fight" 命令可以切换到切磋模式（不会致死）
 *   - 在城内杀人可能会被守卫追捕
 */

#pragma strict_types

#include <framework/message_facade.h>

inherit "/cmds/player/attack";

/**
 * @brief 显示命令帮助
 * @param me 玩家对象
 * @return 1
 */
int help(object me)
{
    msg_send(me, @HELP
命令：kill（击杀）

攻击目标。这是 attack 命令的别名入口。

用法：
  kill <目标名称>    - 发起攻击（等同于 attack）

示例：
  kill bandit       - 攻击强盗
  kill wolf         - 攻击野狼

注意：
  - 这是 attack 命令的快捷方式
  - 使用 "fight" 命令可切换到非致命的切磋模式
  - 在城镇内杀人可能会被守卫追捕

相关命令：
  attack   - 攻击（与kill相同）
  fight    - 切磋（非致命）
  flee     - 逃跑

HELP
    );
    return 1;
}
