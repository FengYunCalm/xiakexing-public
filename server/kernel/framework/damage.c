/**
 * @file    damage.c
 * @brief   伤害处理框架
 * @author  XiaKeXing Development Team
 * @date    2026-03-20
 * @version 1.0.1
 *
 * @par Description:
 * 提供伤害计算、伤害减免、受伤回调和死亡处理基础能力。
 */

#pragma strict_types
#pragma save_types

#include <system/globals.h>
#include <system/ansi.h>
#include <framework/message_facade.h>

mixed query(string path);
int set(string path, mixed value);
int delete(string path);
mixed query_temp(string path);
int set_temp(string path, mixed value);
int delete_temp(string path);
void on_damage(int damage, int type, object attacker);
void die(object killer);
void on_die(object killer);
void on_living_visibility_changed(object room);

#define DAMAGE_PHYSICAL    1   // 物理伤害
#define DAMAGE_MAGIC       2   // 法术伤害
#define DAMAGE_TRUE        3   // 真实伤害

/**
 * @brief 计算基础伤害
 * @param attacker 攻击者
 * @param defender 防御者
 * @return 基础伤害值
 */
int calculate_damage(object attacker, object defender)
{
    int attack, defense, damage;

    if (!objectp(attacker) || !objectp(defender))
        return 0;

    attack = NUMERIC_RULE_D->query_player_combat_attribute(attacker, "attack");
    defense = NUMERIC_RULE_D->query_player_combat_attribute(defender, "defense");

    damage = attack - defense / 2;

    if (damage < 1)
        damage = 1;

    return damage;
}

/**
 * @brief 应用伤害减免
 * @param damage 原始伤害
 * @param type 伤害类型
 * @param defender 防御者
 * @return 减免后的伤害
 */
int apply_damage_reduction(int damage, int type, object defender)
{
    int reduction = 0;

    if (type == DAMAGE_PHYSICAL) {
        int armor = defender->query("combat/armor") || 0;
        reduction = armor / 2;
    } else if (type == DAMAGE_MAGIC) {
        int mr = defender->query("combat/magic_resist") || 0;
        reduction = mr / 2;
    }

    damage -= reduction;
    if (damage < 1)
        damage = 1;

    return damage;
}

private int receive_combat_damage(int damage, int type, object attacker)
{
    int hp, max_hp;

    if (damage <= 0)
        return 0;

    damage = apply_damage_reduction(damage, type, this_object());

    if (objectp(attacker))
        set_temp("last_damage_from", attacker);

    hp = query("combat/hp") || 0;
    max_hp = query("combat/max_hp") || 100;

    hp -= damage;

    if (hp < 0)
        hp = 0;

    set("combat/hp", hp);
    if (function_exists("schedule_owner_hp_read_model_refresh", this_object()))
        call_other(this_object(), "schedule_owner_hp_read_model_refresh");
    if (function_exists("invalidate_xk_score_projection", this_object()))
        call_other(this_object(), "invalidate_xk_score_projection");

    on_damage(damage, type, attacker);

    if (hp == 0) {
        die(attacker);
    }

    return damage;
}

varargs int receive_damage(mixed type, int amount, object attacker)
{
    int hp;
    int max_hp;
    int damage;
    string hp_key;
    string max_key;

    if (intp(type)) {
        return receive_combat_damage(type, amount, attacker);
    }

    if (!stringp(type) || amount <= 0)
        return 0;

    if (type == "qi" || type == "hp") {
        hp_key = "combat/hp";
        max_key = "combat/max_hp";
    } else if (type == "mp" || type == "neili") {
        hp_key = "combat/mp";
        max_key = "combat/max_mp";
    } else {
        hp_key = "combat/hp";
        max_key = "combat/max_hp";
    }

    hp = query(hp_key) || 0;
    max_hp = query(max_key) || 100;

    damage = amount;

    if (objectp(attacker))
        set_temp("last_damage_from", attacker);

    hp -= damage;
    if (hp < 0)
        hp = 0;

    if (hp > max_hp)
        hp = max_hp;

    set(hp_key, hp);
    if (function_exists("schedule_owner_hp_read_model_refresh", this_object()))
        call_other(this_object(), "schedule_owner_hp_read_model_refresh");
    if (function_exists("invalidate_xk_score_projection", this_object()))
        call_other(this_object(), "invalidate_xk_score_projection");
    on_damage(damage, DAMAGE_TRUE, attacker);

    if (hp_key == "combat/hp" && hp == 0) {
        die(attacker);
    }

    return damage;
}

/**
 * @brief 受伤事件回调(可覆盖)
 * @param damage 伤害值
 * @param type 伤害类型
 * @param attacker 攻击者
 */
void on_damage(int damage, int type, object attacker)
{
}

/**
 * @brief 死亡处理
 * @param killer 击杀者
 */
void die(object killer)
{
    object env;

    if (living(this_object())) {
        env = environment(this_object());
        if (objectp(env)) {
            msg_to_room(env, this_object()->name() + "倒下了。\n", ({ }));
        }
    }

    delete("enemy");

    // 先设置死亡标记，确保 on_die() 里的 NPC 清理链看到的是最终状态。
    set_temp("is_dying", 1);
    if (objectp(env))
        on_living_visibility_changed(env);

    // 触发死亡事件
    on_die(killer);

    // 清理战斗状态
    delete_temp("fight_ob");
    delete_temp("enemies");
}

/**
 * @brief 死亡事件回调(可覆盖)
 * @param killer 击杀者
 */
void on_die(object killer)
{
    // 子类可覆盖此方法实现自定义死亡逻辑
}
