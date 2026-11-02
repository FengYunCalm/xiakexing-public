/**
 * @file    flee.c
 * @brief   逃跑命令 - 战斗中逃跑



 *
 * @par Description:
 * 战斗中逃跑，尝试脱离战斗状态。
 *
 * @par Usage:
 *   flee              - 尝试逃跑
 *   flee <方向>       - 向指定方向逃跑
 */

#pragma strict_types

#include <system/ansi.h>
#include <game/command.h>
#include <framework/message_facade.h>

inherit "/lib/framework/command";
inherit "/lib/std/command_utils";

int help(object me);
int main(object me, string arg);

/**
 * @brief 逃跑命令主函数
 * @param me 玩家对象
 * @param arg 命令参数
 * @return 1
 */
int main(object me, string arg)
{
    string direction;

    if (!objectp(me))
        return 0;

    direction = arg;
    if (stringp(arg) && arg != "")
        direction = normalize_direction(arg) || arg;

    return COMBAT_D->attempt_flee(me, direction);
}

int help(object me)
{
    msg_send(me, @HELP
命令：flee（逃跑）

在战斗中尝试逃跑，脱离战斗状态。

用法：
  flee              - 尝试逃跑
  flee <方向>       - 向指定方向逃跑

示例：
  flee              - 尝试逃跑
  flee north        - 向北逃跑
  flee out          - 逃出房间

注意：
  - 成功率受敏捷影响
  - busy状态会影响逃跑成功率
  - 逃跑失败会消耗时间

相关命令：
  attack  - 攻击
  fight   - 切磋
  go      - 移动（战斗中等价于向该方向逃跑）

HELP
    );
    return 1;
}
