/**
 * @file    attack.c
 * @brief   攻击命令 - attack 公共入口



 *
 * @par Description:
 * `attack` 的公共动词入口，负责参数转发、目标 hook 分发与开战提示。
 * `kill` 当前由独立 alias shell 继承本文件实现；标准攻击准备与战斗启动统一归 `COMBAT_D` 持有。
 */

#pragma strict_types

#include <system/ansi.h>
#include <game/command.h>
#include <system/services.h>
#include <framework/message_facade.h>

inherit "/lib/framework/command";
inherit "/lib/std/utils/util_player";

int help(object me);
int main(object me, string arg);
private string target_display_name(object target);
private int dispatch_target_attack_hook(object me, object target, string arg);

private string target_display_name(object target)
{
    return get_object_display_name(target, "对方");
}

private int dispatch_target_attack_hook(object me, object target, string arg)
{
    mixed err;
    mixed hook_result;

    if (!objectp(target) || !function_exists("do_attack_cmd", target))
        return 0;

    err = catch(hook_result = target->do_attack_cmd(arg));
    if (err) {
        msg_send(me, HIR + target_display_name(target) + "当前无法响应攻击指令，请稍后再试。" NOR + "\n");
        return 1;
    }

    if (!hook_result)
        return 0;

    return 1;
}



/**
 * 启动战斗
 * @param me 玩家对象
 * @param target 目标对象
 */
private int start_combat(object me, object target)
{
    if (!COMBAT_D->start_fight(me, target, 1)) {
        msg_send(me, "战斗状态同步中，请稍后再试。\n");
        return 0;
    }

    return 1;
}

int main(object me, string arg)
{
    object target;

    if (!me) return 0;

    if (!arg || arg == "") {
        msg_send(me, "攻击谁？\n");
        return 1;
    }

    /* 统一战斗检查与目标解析下沉至 COMBAT_D */
    target = COMBAT_D->prepare_attack(me, arg);
    if (!objectp(target))
        return 1;

    /* 目标 hook 有机会接管特殊攻击流程，否则统一回到 COMBAT_D 真源。 */
    if (dispatch_target_attack_hook(me, target, arg)) {
        return 1;
    }

    /* 启动战斗 */
    start_combat(me, target);

    return 1;
}

int help(object me)
{
    msg_send(me, @HELP
命令：attack（攻击）

攻击指定目标。`kill` 当前走独立别名壳，但实际会复用这里的攻击实现。

 用法：
  attack <目标名>  - 攻击指定目标

 示例：
  attack guard      - 攻击守卫
  attack 老虎       - 攻击老虎

 注意：
  - 安全区禁止战斗
  - 攻击玩家会增加PK值
  - kill 由独立命令文件提供兼容入口，但当前仍复用 attack 的攻击实现
  - 战斗中可使用 flee 或 go <方向> 逃跑

相关命令：
  flee    - 逃跑
  go      - 移动（战斗中可按方向逃跑）
  hp      - 查看状态

HELP
    );
    return 1;
}
