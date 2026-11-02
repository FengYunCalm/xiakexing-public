/**
 * @file    weapon.c
 * @brief   武器基类



 *
 * @par Description:
 * 武器基类，继承自 `item.c`，提供装备、伤害、技能类型和耐久度能力。
 */

#pragma strict_types

#include <system/globals.h>
#include <system/ansi.h>
#include <framework/message_facade.h>
#include <framework/dbase.h>

inherit "/lib/base/item/item";

nosave int weapon_type;
nosave int damage;
nosave int damage_type;
nosave int required_str;
nosave int required_dex;
nosave string _skill_type;
nosave int _damage;
nosave int _flags;
nosave int _equipped;
nosave object _owner;

#define TWO_HANDED  1   // 双手武器
#define SECONDARY   2   // 可作为副手
#define EDGED       4   // 锋利
#define POINTED     8   // 尖锐
#define LONG        16  // 长柄

#define SKILL_SWORD     "sword"     // 剑法

/**
 * @brief 设置技能类型
 * @param type 技能类型字符串
  * @return 无返回值
 */
void set_skill_type(string type)
{
    _skill_type = type;
    set("skill_type", type);
}

/**
 * @brief 获取技能类型
 * @return 技能类型
 */
string query_skill_type()
{
    string stored_type;

    /* Item snapshots restore the data mapping after create(). The persisted
     * type must therefore win over the constructor's default cache. */
    stored_type = query("skill_type");
    if (stringp(stored_type) && stored_type != "")
        return stored_type;
    return stringp(_skill_type) && _skill_type != "" ? _skill_type : "";
}

/**
 * @brief 设置伤害值
 * @param d 伤害值
  * @return 无返回值
 */
void set_damage(int d)
{
    _damage = d;
    set("damage", d);
}

/**
 * @brief 获取伤害值
 * @return 伤害值
 */
int query_damage()
{
    int base;
    int enhance_level;
    int stored_damage;

    stored_damage = query("damage");
    base = intp(stored_damage) ? stored_damage : _damage;
    enhance_level = query("enhance_level") || 0;
    return base + enhance_level * 2;
}

/**
 * @brief 设置武器标志
 * @param f 标志位
  * @return 无返回值
 */
void set_weapon_flags(int f)
{
    _flags = f;
    set("weapon_flags", f);
}

/**
 * @brief 获取武器标志
 * @return 标志位
 */
int query_weapon_flags()
{
    return _flags || query("weapon_flags") || 0;
}

/**
 * @brief 检查是否有指定标志
 * @param flag 标志位
 * @return 1=有, 0=无
 */
int has_flag(int flag)
{
    return (query_weapon_flags() & flag) != 0;
}

/**
 * @brief 装备武器
 * @return 1=成功, 0=失败
 */
varargs int wield(object actor)
{
    object env;

    env = objectp(actor) ? actor : environment();
    if (!objectp(env)
        || !function_exists("is_character", env)
        || !env->is_character()
        || environment(this_object()) != env) {
        return 0;
    }

    if (query_durability() <= 0) {
        msg_send(env, "这把武器已经破损不堪，无法使用了。\n");
        return 0;
    }

    /* A restored item may retain its instance flag while the player has no
     * transient weapon slot yet. Rebind both sides before treating wield as
     * an idempotent success. */
    if (_equipped) {
        _owner = env;
        set("equipped", 1);
        if (env->query_temp("weapon") != this_object())
            env->set_temp("weapon", this_object());
        return 1;
    }

    _equipped = 1;
    _owner = env;
    set("equipped", 1);

    if (env) {
        env->set_temp("weapon", this_object());
    }

    msg_send(env, sprintf("你「唰」的一声抽出%s，握在手中。\n", query("name")));

    return 1;
}

/* canonical object-level equip alias for callers that do not care about weapon wording */
varargs int equip(object actor)
{
    return wield(actor);
}

varargs int wear(object actor)
{
    /* compatibility alias; weapon canonical verb remains wield/equip */
    return wield(actor);
}

/**
 * @brief 卸下武器
 * @return 1=成功, 0=失败
 */
int unequip()
{
    if (!_equipped) return 0;

    _equipped = 0;
    set("equipped", 0);

    if (_owner) {
        msg_send(_owner, sprintf("你将%s插回腰间，空出了手。\n", query("name")));
        if (_owner->query_temp("weapon") == this_object())
            _owner->delete_temp("weapon");
    }

    _owner = 0;

    return 1;
}

varargs int unwear(object actor)
{
    /* compatibility alias; canonical object-level exit is unequip() */
    return unequip();
}

/**
 * @brief 检查是否已装备
 * @return 1=已装备, 0=未装备
 */
int query_equipped()
{
    return _equipped;
}

int query_is_equipped()
{
    /* compatibility alias; canonical query API is query_equipped() */
    return query_equipped();
}

/**
 * @brief 获取装备者
 * @return 装备者对象
 */
object query_owner()
{
    return _owner;
}

/**
 * @brief 计算命中加值（子类可覆盖）
 * @param me 使用者
 * @param victim 目标
 * @return 命中加值
 */
int hit_bonus(object me, object victim)
{
    return 0;
}

/**
 * @brief 计算伤害加值
 * @param me 使用者
 * @param victim 目标
 * @return 伤害加值
 */
int damage_bonus(object me, object victim)
{
    int base = query_damage();
    int max_durability = query_max_durability();
    int durability_percent;

    if (max_durability <= 0)
        return base;

    durability_percent = query_durability() * 100 / max_durability;

    // 耐久度低于20%时伤害降低
    if (durability_percent < 20) {
        base = base * durability_percent / 100;
    }

    return base;
}

/**
 * @brief 击中效果（子类覆盖）
 * @param me 使用者
 * @param victim 目标
 * @param damage 伤害值
 * @return 额外效果描述
 */
mixed hit_ob(object me, object victim, int _damage)
{
    // 消耗耐久度
    consume_durability(1);

    return 0;
}

/**
 * @brief 获取简短描述
 * @return 描述字符串
 */
string query_short()
{
    string desc = ::query_short();

    if (_equipped) {
        desc += "(已装备)";
    }

    if (has_flag(TWO_HANDED)) {
        desc += "(双手)";
    }

    return desc;
}

/**
 * @brief 获取详细描述
 * @return 描述字符串
 */
string query_long()
{
    string desc = ::query_long();
    int durability_percent;
    int max_durability;

    desc += sprintf("\n伤害: %d\n", query_damage());
    desc += sprintf("技能类型: %s\n", query_skill_type());

    max_durability = query_max_durability();
    if (max_durability <= 0)
        max_durability = 1;
    durability_percent = query_durability() * 100 / max_durability;
    desc += sprintf("耐久: %d/%d\n", query_durability(), max_durability);
    if (durability_percent >= 80)
        desc += "状态: 完好\n";
    else if (durability_percent >= 50)
        desc += "状态: 良好\n";
    else if (durability_percent >= 20)
        desc += "状态: 磨损\n";
    else
        desc += "状态: 严重破损\n";

    if (has_flag(TWO_HANDED)) {
        desc += "类型: 双手武器\n";
    }
    if (has_flag(SECONDARY)) {
        desc += "可作为副手武器\n";
    }
    if (has_flag(EDGED)) {
        desc += "特征: 锋利\n";
    }
    if (has_flag(POINTED)) {
        desc += "特征: 尖锐\n";
    }

    return desc;
}

/**
 * @brief 创建时初始化
  * @return 无返回值
 */
void create()
{
    ::create();

    set_item_type("weapon");
    set_max_durability(100);
    set_durability(100);
    set_skill_type(SKILL_SWORD);
    set_damage(10);
    set_weapon_flags(0);
    _equipped = 0;
    _owner = 0;
}
