/**
 * @file    idle_dazuo.c
 * @brief   打坐挂机结算器：离线时长 × 单位收益。
 *
 * 单位收益（每小时）：内力 +9% 上限、精力 +6% 上限、潜能 +30。
 * 打坐点：药铺后院、水神庙（安全区）。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

private int query_max_hp(object player)
{
    return intp(player->query("combat/max_hp")) ? player->query("combat/max_hp") : 100;
}

private int query_max_mp(object player)
{
    return intp(player->query("combat/max_mp")) ? player->query("combat/max_mp") : 100;
}

private int query_max_ep(object player)
{
    return intp(player->query("combat/max_ep")) ? player->query("combat/max_ep") : 100;
}

mapping settle_idle(object player, int hours, mapping session)
{
    int hp_gain;
    int mp_gain;
    int ep_gain;
    int potential_gain;
    int elapsed_seconds;
    int level_gain;
    string skill_id;
    mapping context;
    mapping applied;
    int max_hp;
    int max_mp;
    int max_ep;
    string *messages;
    string message;

    /* 恢复分量判 0 放行、进度分量独立算（30-59 分钟也有半小时块）。 */
    if (!objectp(player))
        return ([ "ok": 0, "error": "invalid_arguments" ]);

    max_hp = query_max_hp(player);
    max_mp = query_max_mp(player);
    max_ep = query_max_ep(player);

    /* 每小时回 9% 上限内力、6% 上限精力（打坐本质是恢复，不是暴涨上限） */
    mp_gain = max_mp * 9 / 100 * hours;
    ep_gain = max_ep * 6 / 100 * hours;
    /* 顺带少量潜能（纳入每日收益池，见 reward diminishing） */
    potential_gain = 30 * hours;

    /* 恢复写入失败就到此为止：不能一边拿不到内力恢复、一边继续发潜能与技能进度。 */
    if (mp_gain > 0 && !player->heal_mp(mp_gain))
        return ([ "ok": 0, "error": "resource_commit_failed" ]);
    if (ep_gain > 0 && function_exists("heal_ep", player)
        && !player->heal_ep(ep_gain))
        return ([ "ok": 0, "error": "resource_commit_failed" ]);
    /* 打坐也回少量气血，保证重连即状态良好 */
    hp_gain = max_hp * 3 / 100 * hours;
    if (hp_gain > 0 && !player->heal_hp(hp_gain))
        return ([ "ok": 0, "error": "resource_commit_failed" ]);

    if (potential_gain > 0
        && !PROGRESSION_D->add_potential(player, potential_gain))
        return ([ "ok": 0, "error": "potential_commit_failed" ]);

    /* 给潜能的同时给内功进度——每挂机半小时 1 级，向下取整。
     * skill_id 来自 dazuo <skill_id> 写入的 session.context；无参旧路径为空则跳过。 */
    context = mapp(session) && mapp(session["context"]) ? session["context"] : ([]);
    skill_id = stringp(context["skill_id"]) ? context["skill_id"] : "";
    elapsed_seconds = mapp(session) && intp(session["elapsed_seconds"])
        ? session["elapsed_seconds"] : 0;
    level_gain = elapsed_seconds / 1800;
    if (skill_id != "" && level_gain > 0
        && !SKILL_D->improve_skill(player, skill_id, level_gain * 10))
        return ([ "ok": 0, "error": "skill_progress_commit_failed" ]);

    /* 浮动句：三选一，口语化，数字进句尾；参悟有进境时追加一句。 */
    messages = ({
        sprintf("你挂机修行了 %d 小时。内力 +%d、精力 +%d、气血 +%d，还攒下 %d 点潜能。",
            hours, mp_gain, ep_gain, hp_gain, potential_gain),
        sprintf("这一坐就是 %d 小时，起来时腿麻得厉害，气倒是足了不少：内力 +%d、精力 +%d、气血 +%d，潜能 +%d。",
            hours, mp_gain, ep_gain, hp_gain, potential_gain),
        sprintf("%d 小时过去，灰尘落了你一身。内力 +%d、精力 +%d、气血 +%d，潜能 +%d。",
            hours, mp_gain, ep_gain, hp_gain, potential_gain),
    });
    message = messages[random(sizeof(messages))];
    if (skill_id != "" && level_gain > 0)
        message += sprintf("\n参悟「%s」，你的内功修为又深了几分。", skill_id);

    applied = ([
        "mp": mp_gain,
        "ep": ep_gain,
        "hp": hp_gain,
        "potential": potential_gain,
    ]);
    if (skill_id != "" && level_gain > 0)
        applied["level_gain"] = level_gain;

    return ([
        "ok": 1,
        "message": message,
        "applied": applied,
    ]);
}
