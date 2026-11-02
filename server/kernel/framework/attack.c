/**
 * @file    attack.c
 * @brief   攻击框架
 * @author  XiaKeXing Development Team
 * @date    2026-03-20
 * @version 1.0.1
 *
 * @par Description:
 * 提供敌人列表、攻击目标、仇恨值和统一战斗入口。
 */

#pragma strict_types
#pragma save_types

#include <system/globals.h>
#include <system/ansi.h>

mixed query_temp(string path);
int set_temp(string path, mixed value);

/**
 * @brief 添加敌人到敌人列表
 * @param target 敌人对象
 * @return 1成功, 0失败
 */
int add_enemy(object target)
{
    object *enemies;

    if (!objectp(target))
        return 0;

    enemies = query_temp("enemies") || ({});

    if (member_array(target, enemies) != -1)
        return 0;

    enemies += ({ target });
    set_temp("enemies", enemies);

    return 1;
}

/**
 * @brief 从敌人列表移除敌人
 * @param target 敌人对象
 * @return 1成功, 0失败
 */
int remove_enemy(object target)
{
    object *enemies;
    int idx;

    if (!objectp(target))
        return 0;

    enemies = query_temp("enemies") || ({});

    idx = member_array(target, enemies);
    if (idx == -1)
        return 0;

    enemies = enemies[0..idx-1] + enemies[idx+1..];
    set_temp("enemies", enemies);

    return 1;
}

/**
 * @brief 获取所有敌人
 * @return 敌人对象数组
 */
object *query_enemies()
{
    return query_temp("enemies") || ({});
}

/**
 * @brief 检查是否为敌人
 * @param target 目标对象
 * @return 1是敌人, 0不是
 */
int is_enemy(object target)
{
    object *enemies = query_temp("enemies") || ({});
    return (member_array(target, enemies) != -1);
}

/**
 * @brief 标记攻击意图
 * @param target 目标对象
 */
void set_fight(object target)
{
    if (!objectp(target))
        return;

    set_temp("fight_ob", target);
    add_enemy(target);
}

/**
 * @brief 获取当前攻击目标
 * @return 目标对象
 */
object query_fight_object()
{
    return query_temp("fight_ob");
}

/**
 * @brief 增加仇恨值
 * @param target 敌人对象
 * @param amount 仇恨值
 */
void add_hate(object target, int amount)
{
    mapping hate_list;
    string key;

    if (!objectp(target) || amount <= 0)
        return;

    hate_list = query_temp("hate_list") || ([]);

    key = file_name(target);
    if (!hate_list[key])
        hate_list[key] = 0;

    hate_list[key] += amount;
    set_temp("hate_list", hate_list);
}

/**
 * @brief 获取仇恨值
 * @param target 敌人对象
 * @return 仇恨值
 */
int query_hate(object target)
{
    mapping hate_list;
    string key;

    hate_list = query_temp("hate_list") || ([]);
    key = file_name(target);
    return hate_list[key] || 0;
}

/**
 * @brief 获取仇恨最高的目标
 * @return 仇恨最高的敌人对象
 */
object query_most_hated()
{
    mapping hate_list;
    object *enemies;
    object most_hated;
    int max_hate;

    hate_list = query_temp("hate_list") || ([]);
    enemies = query_enemies();

    if (sizeof(enemies) == 0)
        return 0;

    most_hated = enemies[0];
    max_hate = 0;

    foreach(object enemy in enemies) {
        int hate = query_hate(enemy);
        if (hate > max_hate) {
            max_hate = hate;
            most_hated = enemy;
        }
    }

    return most_hated;
}
/**
 * @brief 开始切磋战斗（不致死）
 * @param target 目标对象
 */
void fight_ob(object target)
{
    if (!objectp(target))
        return;

    COMBAT_D->start_fight(this_object(), target, 0);
}

/**
 * @brief 开始生死战斗（可致死）
 * @param target 目标对象
 */
void kill_ob(object target)
{
    if (!objectp(target))
        return;

    add_hate(target, 100);
    COMBAT_D->start_fight(this_object(), target, 1);
}

/**
 * @brief 检查是否在战斗中
 * @return 1在战斗中, 0不在
 */
int is_fighting()
{
    // 检查本地状态
    if (objectp(query_temp("fight_ob")))
        return 1;

    // 委托给战斗守护进程
    return COMBAT_D->is_in_combat(this_object());
}
