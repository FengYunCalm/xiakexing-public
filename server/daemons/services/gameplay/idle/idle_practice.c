/**
 * @file    idle_practice.c
 * @brief   练功挂机结算器：潜能 + 外功进度双通道。
 *
 * 给潜能（30/小时，与打坐同规格）的同时给外功进度——
 * 一门武学每挂机半小时给 1 级，不满半小时不算，向下取整。
 * 旧「40 次/小时批量 practice_skill 消耗型」模型废弃（经济学反转，方案 §2.1 记档）；
 * 发起时的单次 practice_skill 校验保留在 practice_cmd.c，本文件不再调用它。
 * 进度粒度用 session.elapsed_seconds（idle_runtime_d 单点钳上限后下发），
 * 恢复分量维持整小时语义；等级发放走 SKILL_D->improve_skill 唯一权威（cap 内置）。
 * 练功点：武馆练功房（文昌街口）。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

#define IDLE_PRACTICE_POTENTIAL_PER_HOUR 30

mapping settle_idle(object player, int hours, mapping session)
{
    mapping applied;
    mapping context;
    string skill_id;
    string message;
    int elapsed_seconds;
    int level_gain;

    /* ：恢复分量判 0 放行、进度分量独立计算——hours<=0 不再整体早退，
     * 否则 30-59 分钟会话拿不到半小时块的第一级。 */
    if (!objectp(player))
        return ([ "ok": 0, "error": "invalid_arguments" ]);

    context = mapp(session) && mapp(session["context"])
        ? session["context"] : ([]);
    skill_id = stringp(context["skill_id"]) ? context["skill_id"] : "";

    if (skill_id == "")
        return ([ "ok": 0, "error": "missing_skill" ]);

    elapsed_seconds = intp(session["elapsed_seconds"]) ? session["elapsed_seconds"] : 0;
    level_gain = elapsed_seconds / 1800;

    if (hours <= 0 && level_gain <= 0)
        return ([ "ok": 0, "error": "practice_unavailable",
            "message": "练功时间太短，还没什么收获。" ]);

    applied = ([ "skill_id": skill_id ]);
    if (level_gain > 0) {
        if (!SKILL_D->improve_skill(player, skill_id, level_gain * 10))
            return ([ "ok": 0, "error": "skill_progress_commit_failed" ]);
        applied["level_gain"] = level_gain;
    }
    if (hours > 0
        && !PROGRESSION_D->add_potential(player,
            IDLE_PRACTICE_POTENTIAL_PER_HOUR * hours))
        return ([ "ok": 0, "error": "potential_commit_failed" ]);
    applied["potential"] = IDLE_PRACTICE_POTENTIAL_PER_HOUR * (hours > 0 ? hours : 0);

    /* 浮动句：口语化报时长与进度 */
    if (level_gain > 0)
        message = sprintf("你沉下心练了 %d 小时的「%s」，武艺精进不止一筹。",
            hours > 0 ? hours : 1, skill_id);
    else
        message = sprintf("你练了一会儿「%s」，时间还短，火候未到。", skill_id);

    return ([
        "ok": 1,
        "message": message,
        "applied": applied,
    ]);
}
