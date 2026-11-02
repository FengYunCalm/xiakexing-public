/**
 * @file    combat_flee.c
 * @brief   战斗逃跑决策子服务



 */

#pragma strict_types
#pragma save_types

#include <system/ansi.h>
#include <system/services.h>

private int query_flee_actor_dexterity(object actor)
{
    mixed value;

    if (!objectp(actor) || !function_exists("query", actor))
        return 10;
    value = actor->query("attributes/dexterity");
    if (!intp(value) || value <= 0)
        value = actor->query("dex");
    return intp(value) && value > 0 ? value : 10;
}

private mapping query_flee_enemy_profile(object who)
{
    mixed *enemies;
    mixed candidate;
    object enemy;
    int enemy_count;
    int max_dexterity;
    int dexterity;

    enemy_count = 0;
    max_dexterity = 0;
    if (objectp(who) && function_exists("query_enemies", who)) {
        enemies = who->query_enemies();
        if (arrayp(enemies))
            foreach (candidate in enemies) {
                if (!objectp(candidate))
                    continue;
                enemy = candidate;
                enemy_count++;
                dexterity = query_flee_actor_dexterity(enemy);
                if (dexterity > max_dexterity)
                    max_dexterity = dexterity;
            }
    }

    /* 敌人列表尚未同步时回退当前 fight_ob，避免把单人战斗当作无敌人。 */
    if (enemy_count == 0 && objectp(who)
        && function_exists("query_fight_object", who)) {
        enemy = who->query_fight_object();
        if (objectp(enemy)) {
            enemy_count = 1;
            max_dexterity = query_flee_actor_dexterity(enemy);
        }
    }

    if (max_dexterity <= 0)
        max_dexterity = query_flee_actor_dexterity(who);
    return ([
        "enemy_count": enemy_count,
        "max_dexterity": max_dexterity,
    ]);
}

private int query_flee_against_training_dummy(object who)
{
    mixed *enemies;
    mixed candidate;
    object enemy;

    if (!objectp(who))
        return 0;
    if (function_exists("query_enemies", who)) {
        enemies = who->query_enemies();
        if (arrayp(enemies))
            foreach (candidate in enemies) {
                if (objectp(candidate) && candidate->query("training_dummy"))
                    return 1;
            }
    }
    if (function_exists("query_fight_object", who)) {
        enemy = who->query_fight_object();
        if (objectp(enemy) && enemy->query("training_dummy"))
            return 1;
    }
    return 0;
}

private mapping query_flee_budget(object who, string direction)
{
    mapping enemy_profile;
    mapping budget;

    enemy_profile = query_flee_enemy_profile(who);
    budget = NUMERIC_RULE_D->query_combat_flee_budget_report(who, ([
        "directed": stringp(direction) && direction != "" ? 1 : 0,
        "enemy_count": enemy_profile["enemy_count"] || 0,
        "enemy_dexterity": enemy_profile["max_dexterity"] || 10,
    ]));
    if (!mapp(budget))
        budget = ([ ]);
    if (query_flee_against_training_dummy(who))
        budget["chance_bp"] = 10000;
    return budget;
}

private mapping failed_flee_result(mapping budget)
{
    return ([
        "status": "failed",
        "message": HIR "你逃跑失败了！" NOR "\n",
        "chance_bp": mapp(budget) ? (budget["chance_bp"] || 0) : 0,
        "failure_busy_seconds": mapp(budget) ? (budget["failure_busy_seconds"] || 2) : 2,
        "budget": mapp(budget) ? budget : ([ ]),
    ]);
}

/* L-139：定向与无定向逃跑共用同一 blocked 出口判定。blocked 表示该出口
 * 不可通过；当前运行树中该字段只由逃跑路径消费，移动侧合同未在本切片
 * 扩展（无内容使用该字段）。 */
private int flee_exit_blocked(object env, string direction)
{
    return objectp(env) && stringp(direction) && direction != ""
        && env->query("exits/" + direction + "/blocked") ? 1 : 0;
}

mapping resolve_flee_attempt(object who, object env, mapping exits, string direction)
{
    mapping budget;
    string *exit_dirs;
    string flee_dir;
    string flee_dir_name;

    if (!objectp(who))
        return ([ "status": "invalid", "message": "" ]);

    if (!objectp(env))
        return ([ "status": "invalid", "message": "你不知道在哪里。\n" ]);

    if (!mapp(exits) || sizeof(keys(exits)) == 0)
        return ([ "status": "invalid", "message": "这里没有出口，你无处可逃！\n" ]);

    if (stringp(direction) && direction != "") {
        if (undefinedp(exits[direction]))
            return ([ "status": "invalid", "message": "这个方向没有出口。\n" ]);

        if (flee_exit_blocked(env, direction))
            return ([ "status": "invalid", "message": HIR "这个出口被堵住了，你无法从这里逃跑。" NOR "\n" ]);

        budget = query_flee_budget(who, direction);
        if (!mapp(budget))
            budget = ([ "chance_bp": 5000 ]);
        /* 定向逃跑也走统一概率；direction 只决定目标出口，不再提供必成
         * 分支。具体概率曲线和失败代价仍由 combat_flee 配置拥有。 */
        if (random(10000) >= (budget["chance_bp"] || 5000))
            return failed_flee_result(budget);

        flee_dir_name = call_other("/lib/std/utils/util_direction", "direction_name", direction);
        return ([
            "status": "success",
            "message": HIC "你向" + (stringp(flee_dir_name) ? flee_dir_name : direction) + "逃跑..." NOR "\n",
            "direction": direction,
            "destination": exits[direction],
            "budget": mapp(budget) ? budget : ([ ]),
        ]);
    }

    /* L-139：无定向逃跑必须过滤 blocked 出口，不得选中被封锁的方向。 */
    exit_dirs = ({ });
    foreach (string exit_dir, mixed exit_dest in exits) {
        if (flee_exit_blocked(env, exit_dir))
            continue;
        exit_dirs += ({ exit_dir });
    }
    if (sizeof(exit_dirs) == 0)
        return ([ "status": "invalid",
            "message": HIR "所有出口都被堵住了，你无处可逃！" NOR "\n" ]);
    flee_dir = exit_dirs[random(sizeof(exit_dirs))];
    flee_dir_name = call_other("/lib/std/utils/util_direction", "direction_name", flee_dir);

    budget = query_flee_budget(who, "");
    if (!mapp(budget))
        budget = ([ "chance_bp": 5000 ]);
    if (random(10000) >= (budget["chance_bp"] || 5000))
        return failed_flee_result(budget);

    return ([
        "status": "success",
        "message": HIG "你成功逃跑了！" NOR "\n",
        "direction": flee_dir,
        "destination": exits[flee_dir],
        "budget": mapp(budget) ? budget : ([ ]),
    ]);
}
