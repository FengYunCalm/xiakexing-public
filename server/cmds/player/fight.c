/**
 * @file    fight.c
 * @brief   切磋命令 - 公共 fight 动词入口



 *
 * @par Description:
 * fight 的公共动词入口，负责目标解析、切磋前置校验与 `do_fight` hook 分发。
 * 标准切磋启动与状态推进统一归 `COMBAT_D` 持有。
 *
 * @par Usage:
 *   fight <目标>     - 与目标切磋
 */

#pragma strict_types

#include <system/ansi.h>
#include <game/command.h>
#include <system/services.h>
inherit "/lib/framework/command";

int help(object me);
int main(object me, string arg);
private int dispatch_target_fight_hook(object me, object target, string arg);
private void record_combat_phase(object me, string phase, int wall_started_at,
    int cpu_started_at);

private void record_combat_phase(object me, string phase, int wall_started_at,
    int cpu_started_at)
{
    int cpu_finished_at;

    cpu_finished_at = thread_cpu_time_ns();
    GATEWAY_TELEMETRY_D->record_gateway_combat_phase_timing(
        me, phase, perf_counter_ns() - wall_started_at,
        cpu_started_at >= 0 && cpu_finished_at >= cpu_started_at
            ? cpu_finished_at - cpu_started_at : -1);
}

private int dispatch_target_fight_hook(object me, object target, string arg)
{
    mixed err;
    mixed hook_result;

    /* fight 负责公共入口，对象只通过 do_fight 在统一校验后插入定制切磋逻辑。 */
    if (!objectp(target) || !function_exists("do_fight", target))
        return 0;

    err = catch(hook_result = target->do_fight(arg));
    if (err) {
        msg_send(me, HIR + "对方当前无法响应切磋请求，请稍后再试。" NOR + "\n");
        return 1;
    }

    if (!hook_result)
        return 0;

    return 1;
}

int main(object me, string arg)
{
    object target;
    int phase_started_at;
    int phase_cpu_started_at;

    if (!me) {
        return 0;
    }

    if (!arg || arg == "")
    {
        msg_send(me, "你想和谁切磋？\n");
        return 1;
    }

    phase_started_at = perf_counter_ns();
    phase_cpu_started_at = thread_cpu_time_ns();
    target = COMBAT_D->prepare_fight(me, arg);
    record_combat_phase(me, "fight_prepare", phase_started_at, phase_cpu_started_at);
    if (!objectp(target))
        return 1;

    /* 先给目标 hook 处理，未接管时再由 COMBAT_D 启动标准切磋。 */
    phase_started_at = perf_counter_ns();
    phase_cpu_started_at = thread_cpu_time_ns();
    if (dispatch_target_fight_hook(me, target, arg)) {
        record_combat_phase(me, "fight_hook", phase_started_at, phase_cpu_started_at);
        return 1;
    }
    record_combat_phase(me, "fight_hook", phase_started_at, phase_cpu_started_at);

    phase_started_at = perf_counter_ns();
    phase_cpu_started_at = thread_cpu_time_ns();
    if (!COMBAT_D->start_fight(me, target, 0)) {
        record_combat_phase(me, "fight_start", phase_started_at, phase_cpu_started_at);
        msg_send(me, "切磋启动失败。\n");
        return 1;
    }
    record_combat_phase(me, "fight_start", phase_started_at, phase_cpu_started_at);

    return 1;
}

int help(object me)
{
    msg_send(me, @HELP
命令：fight（切磋）

与NPC或其他玩家进行友好切磋。

用法：
  fight <目标>     - 与目标开始切磋

示例：
  fight guard      - 与守卫切磋
  fight 令狐冲    - 与玩家令狐冲切磋

注意：
  - 切磋不会致死，会在击倒前结束
  - 只能在允许战斗的区域切磋

相关命令：
  attack  - 攻击
  flee    - 逃跑
  go      - 移动（战斗中可按方向逃跑）
HELP
    );
    return 1;
}
