/* telemetry_schema.h — 网关遥测测量 schema 单一真源（测量专场落地）
 *
 * 背景：战斗相位清单与计时字段后缀此前在 gateway_telemetry_d.c（记录/查询侧）
 * 与 cmds/admin/ownercheck.c（发射侧）各持一份字面量拷贝。现收编为单源：
 * 覆盖 GATEWAY_* 计时族 combat/movement/progression/trade_consume/
 * command_family 的记录、查询与发射循环。
 * 范围外：① kernel/login/login_session.c 的 login_timing_* 使用混合集
 * （wall×3＋thread_cpu×4，非本宏可表达的单一后缀集）；② receive_phase 为
 * wall-only 短变体（samples/total_us/max_us，无 cpu 三元组）且发射侧硬编码
 * 全键清单，未并入本宏域。测试一律以发射输出为锚，禁内联清单。
 *
 * 修改纪律：增删相位或字段必须同一提交内同步消费方行为验证
 * （ownercheck measure 前后差分 + loadtest 契约套件）。
 */

#ifndef __TELEMETRY_SCHEMA_H__

/* 战斗相位 19 项：fight 主流程 / perform 解锁执行链 / round 心跳 / flee 链 */
#define GATEWAY_COMBAT_PHASES ({ \
    "fight_prepare", "fight_hook", "fight_start", \
    "perform_unlock", "perform_behavior", "perform_resolve", "perform_status", \
    "perform_definition_access", "perform_target_modifiers", "perform_budget", \
    "perform_effects", "perform_state_build", \
    "round_due_lag", "round_total", \
    "flee_precheck", "flee_resolve", "flee_move", "flee_finalize", "flee_refresh", \
})

/* 计时字段后缀 7 项：wall 双维度 + cpu 三元组 + cpu 不可用哨兵 */
#define GATEWAY_TIMING_FIELD_SUFFIXES ({ \
    "wall_samples", "wall_total_us", "wall_max_us", \
    "cpu_samples", "cpu_total_us", "cpu_max_us", "cpu_unavailable", \
})

#define __TELEMETRY_SCHEMA_H__
#endif
