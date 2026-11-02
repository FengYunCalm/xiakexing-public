/**
 * @file    types.h
 * @brief   核心类型定义 - Core Type Definitions



 *
 * @par Description:
 * 定义核心框架使用的类型常量：
 * - 基础数据类型
 * - 游戏对象类型
 * - 性别类型
 * - 状态类型
 * - 类型检查宏
 *
 * @par Usage:
 * #include <core/types.h>
 */

#pragma strict_types

#ifndef __CORE_TYPES_H__
#define __CORE_TYPES_H__

// 包含driver层基础类型定义，避免重复
#include <driver/type.h>

// 注意: T_* 基础类型定义在 <type.h> 中已包含
// 此处仅保留游戏特定的类型扩展


#define TYPE_PLAYER     1
#define TYPE_NPC        2
#define TYPE_ITEM       3
#define TYPE_ROOM       4
#define TYPE_WEAPON     5
#define TYPE_ARMOR      6
#define TYPE_MEDICINE   7
#define TYPE_SKILL      8
#define TYPE_QUEST      9
#define TYPE_GUILD      10
#define TYPE_DAEMON     99


#define GENDER_MALE     1
#define GENDER_FEMALE   2
#define GENDER_NEUTRAL  0

#define GENDER_NAME     ([ \
    GENDER_MALE: "男性", \
    GENDER_FEMALE: "女性", \
    GENDER_NEUTRAL: "中性" \
])


#define STATUS_NORMAL       0
#define STATUS_FIGHTING     1
#define STATUS_UNCONSCIOUS  2
#define STATUS_DEAD         3
#define STATUS_BUSY         4
#define STATUS_RESTING      5
#define STATUS_SLEEPING     6


#define DAMAGE_TYPE_PHYSICAL    1
#define DAMAGE_TYPE_INTERNAL    2
#define DAMAGE_TYPE_POISON      3
#define DAMAGE_TYPE_FIRE        4
#define DAMAGE_TYPE_COLD        5

// 注意: UNLIMITED(-2), UNDEFINED(-1), NULL(0) 定义在 <core/constants.h>
// 请使用 #include <core/constants.h> 获取这些定义

#define RANDOM_SELECT   -10
#define ALL_SELECT      -11


#define IS_INT(x)       (intp(x))
#define IS_STRING(x)    (stringp(x))
#define IS_ARRAY(x)     (arrayp(x))
#define IS_OBJECT(x)    (objectp(x))
#define IS_MAPPING(x)   (mapp(x))
#define IS_FUNCTION(x)  (functionp(x))
#define IS_FLOAT(x)     (floatp(x))
#define IS_NULL(x)      (undefinedp(x) || nullp(x))

// 注意: TRUE(1), FALSE(0) 定义在 <core/constants.h>
// 请使用 #include <core/constants.h> 获取这些定义

#endif // __CORE_TYPES_H__
