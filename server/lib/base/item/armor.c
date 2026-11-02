/**
 * @file    armor.c
 * @brief   防具基类



 *
 * @par Description:
 * 防具基类，继承自 `item.c`，提供防具类型、防御值、装备和耐久度能力。
 */

#pragma strict_types

#include <system/globals.h>
#include <system/ansi.h>
#include <framework/message_facade.h>
#include <framework/dbase.h>

inherit "/lib/base/item/item";

nosave int armor_type;
nosave int defense;
nosave int armor_class;
nosave string _armor_type;
nosave int _defense;
nosave int _equipped;
nosave object _owner;

#define ARMOR_HEAD      "head"      // 头部
#define ARMOR_NECK      "neck"      // 颈部
#define ARMOR_CHEST     "chest"     // 胸部
#define ARMOR_CLOAK     "cloak"     // 披风
#define ARMOR_WAIST     "waist"     // 腰部
#define ARMOR_HANDS     "hands"     // 手部
#define ARMOR_FINGER    "finger"    // 手指
#define ARMOR_ARM       "arm"       // 手臂
#define ARMOR_LEGS      "legs"      // 腿部
#define ARMOR_FEET      "feet"      // 脚部
#define ARMOR_SHIELD    "shield"    // 盾牌

/**
 * @brief 设置护甲类型
 * @param type 类型字符串
  * @return 无返回值
 */
void set_armor_type(string type)
{
    string *valid_types = ({
        ARMOR_HEAD, ARMOR_NECK, ARMOR_CHEST, ARMOR_CLOAK,
        ARMOR_WAIST, ARMOR_HANDS, ARMOR_FINGER, ARMOR_ARM,
        ARMOR_LEGS, ARMOR_FEET, ARMOR_SHIELD
    });

    if (member_array(type, valid_types) != -1) {
        _armor_type = type;
        set("armor_type", type);
    }
}

/**
 * @brief 获取护甲类型
 * @return 护甲类型
 */
string query_armor_type()
{
    return _armor_type || query("armor_type") || ARMOR_CHEST;
}

/**
 * @brief 类型转中文
 * @param type 类型字符串
 * @return 中文名称
 */
string type_to_chinese(string type)
{
    switch (type) {
        case ARMOR_HEAD:   return "头盔";
        case ARMOR_NECK:   return "项链";
        case ARMOR_CHEST:  return "护甲";
        case ARMOR_CLOAK:  return "披风";
        case ARMOR_WAIST:  return "腰带";
        case ARMOR_HANDS:  return "手套";
        case ARMOR_FINGER: return "戒指";
        case ARMOR_ARM:    return "护臂";
        case ARMOR_LEGS:   return "护腿";
        case ARMOR_FEET:   return "靴子";
        case ARMOR_SHIELD: return "盾牌";
        default:           return "护甲";
    }
}

/**
 * @brief 设置防御值
 * @param d 防御值
  * @return 无返回值
 */
void set_defense(int d)
{
    if (d < 0) d = 0;
    if (d > 500) d = 500;
    _defense = d;
    set("defense", d);
}

/**
 * @brief 获取防御值
 * @return 防御值（考虑耐久度）
 */
int query_defense()
{
    int def = _defense || query("defense") || 0;
    int max_durability = query_max_durability();
    int durability_percent;
    int enhance_level;

    enhance_level = query("enhance_level") || 0;
    def += enhance_level * 2;

    if (max_durability <= 0)
        return def;

    durability_percent = query_durability() * 100 / max_durability;

    if (durability_percent < 20) {
        def = def * durability_percent / 100;
    }

    return def;
}

/**
 * @brief 装备防具
 * @return 1=成功, 0=失败
 */
varargs int wear(object actor)
{
    object env;

    env = environment();
    if (!objectp(env)
        || !function_exists("is_character", env)
        || !env->is_character()) {
        return 0;
    }

    if (query_durability() <= 0) {
        msg_send(env, "这件防具已经破损不堪，无法穿戴了。\n");
        return 0;
    }

    if (_equipped) return 1;

    _equipped = 1;
    _owner = env;
    set("equipped", 1);

    if (env) {
        env->set_temp("armor/" + query_armor_type(), this_object());
    }

    msg_send(env, sprintf("你披上%s，紧了紧衣带。\n", query("name")));

    return 1;
}

int equip(object actor)
{
    /* canonical object-level equip alias for callers that do not care about armor wording */
    return wear(actor);
}

/**
 * @brief 卸下防具
 * @return 1=成功, 0=失败
 */
varargs int remove_armor(object actor)
{
    if (!_equipped) return 0;

    _equipped = 0;
    set("equipped", 0);

    if (_owner) {
        msg_send(_owner, sprintf("你解开%s，随手搭在臂弯。\n", query("name")));
        _owner->delete_temp("armor/" + query_armor_type());
    }

    _owner = 0;

    return 1;
}

/**
 * @brief 卸下防具（兼容旧代码）
 * @return 1=成功, 0=失败
 * @deprecated 请使用 remove_armor()
 */
int remove()
{
    /* compatibility alias; canonical armor exit remains remove_armor()/unequip() */
    return remove_armor();
}

int unequip(object actor)
{
    return remove_armor(actor);
}

varargs int unwear(object actor)
{
    /* compatibility alias; canonical object-level exit is unequip()/remove_armor() */
    return remove_armor();
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
 * @brief 受到伤害
 * @param amount 伤害值
 * @param source 攻击者
 * @return 减免后的伤害值
 */
int receive_damage(int amount, object source)
{
    int defense = query_defense();
    int reduced;

    // 计算减免后的伤害
    reduced = amount - defense;
    if (reduced < 1) reduced = 1;  // 至少造成1点伤害

    // 消耗耐久度
    consume_durability(1);

    return reduced;
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

    max_durability = query_max_durability();
    if (max_durability <= 0)
        max_durability = 1;

    desc += sprintf("\n护甲类型: %s\n", type_to_chinese(query_armor_type()));
    desc += sprintf("防御值: %d\n", query_defense());

    // 添加耐久度描述
    durability_percent = query_durability() * 100 / max_durability;
    if (durability_percent >= 80) {
        desc += "状态: 完好\n";
    } else if (durability_percent >= 50) {
        desc += "状态: 良好\n";
    } else if (durability_percent >= 20) {
        desc += "状态: 磨损\n";
    } else {
        desc += "状态: 严重破损\n";
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

    set_item_type("armor");
    set_max_durability(100);
    set_durability(100);
    set_armor_type(ARMOR_CHEST);
    set_defense(5);
    _equipped = 0;
    _owner = 0;
}
