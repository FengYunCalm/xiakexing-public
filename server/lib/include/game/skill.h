/**
 * @file    skill.h
 * @brief   技能系统头文件 - Skill System Interface



 *
 * @par Description:
 * 定义技能系统的常量、类型和接口：
 * - 技能类别常量
 * - 技能等级常量
 * - 熟练度等级
 * - 技能相关宏
 *
 */

#pragma strict_types

#ifndef __GAME_SKILL_H__
#define __GAME_SKILL_H__

// 技能类别
/** 基本技能 */
#define SKILL_CATEGORY_BASIC        "basic"
/** 武器技能 */
#define SKILL_CATEGORY_WEAPON       "weapon"
/** 内功技能 */
#define SKILL_CATEGORY_FORCE        "force"
/** 轻功技能 */
#define SKILL_CATEGORY_DODGE        "dodge"
/** 知识技能 */
#define SKILL_CATEGORY_KNOWLEDGE    "knowledge"
/** 法术技能 */
#define SKILL_CATEGORY_SPELL        "spell"

// 技能等级范围
/** 技能最小等级 */
#define SKILL_MIN_LEVEL             0
/** 技能最大等级 */
#define SKILL_MAX_LEVEL             1000
/** 技能默认值 */
#define SKILL_DEFAULT_LEVEL         0

// 熟练度等级 (基于技能等级)
/** 初学乍练: 0-29 */
#define SKILL_LEVEL_BEGINNER        0
/** 略知一二: 30-59 */
#define SKILL_LEVEL_NOVICE          30
/** 粗通皮毛: 60-89 */
#define SKILL_LEVEL_APPRENTICE      60
/** 半生不熟: 90-119 */
#define SKILL_LEVEL_JOURNEYMAN      90
/** 马马虎虎: 120-149 */
#define SKILL_LEVEL_ADEPT           120
/** 驾轻就熟: 150-179 */
#define SKILL_LEVEL_EXPERT          150
/** 出类拔萃: 180-209 */
#define SKILL_LEVEL_MASTER          180
/** 神乎其技: 210-239 */
#define SKILL_LEVEL_GRANDMASTER     210
/** 出神入化: 240-269 */
#define SKILL_LEVEL_LEGENDARY       240
/** 登峰造极: 800-999 */
#define SKILL_LEVEL_TRANSCENDENT    800
/** 宗师: 1000 */
#define SKILL_LEVEL_ZONGSHI         1000

// 熟练度等级名称
#define SKILL_RANK_NAME_0           "初学乍练"
#define SKILL_RANK_NAME_1           "略知一二"
#define SKILL_RANK_NAME_2           "粗通皮毛"
#define SKILL_RANK_NAME_3           "半生不熟"
#define SKILL_RANK_NAME_4           "马马虎虎"
#define SKILL_RANK_NAME_5           "驾轻就熟"
#define SKILL_RANK_NAME_6           "出类拔萃"
#define SKILL_RANK_NAME_7           "神乎其技"
#define SKILL_RANK_NAME_8           "出神入化"
#define SKILL_RANK_NAME_9           "登峰造极"

// 技能学习常量
/** 最大悟性值 */
#define SKILL_MAX_COMPREHENSION     100
/** 默认悟性值 */
#define SKILL_DEFAULT_COMPREHENSION 50
/** 学习效率基数 */
#define SKILL_LEARN_BASE            10

// 技能冷却时间（秒）
/** 默认技能冷却 */
#define SKILL_COOLDOWN_DEFAULT      3
/** 短冷却技能 */
#define SKILL_COOLDOWN_SHORT        1
/** 长冷却技能 */
#define SKILL_COOLDOWN_LONG         10
/** 绝技冷却 */
#define SKILL_COOLDOWN_ULTIMATE     30

// 内力消耗
/** 普通技能内力消耗 */
#define SKILL_FORCE_COST_NORMAL     10
/** 高级技能内力消耗 */
#define SKILL_FORCE_COST_HIGH       30
/** 绝技内力消耗 */
#define SKILL_FORCE_COST_ULTIMATE   100

// 基础武学类别
/** 拳法 */
#define WEAPON_SKILL_CUFF           "cuff"
/** 掌法 */
#define WEAPON_SKILL_PALM           "palm"
/** 指法 */
#define WEAPON_SKILL_FINGER         "finger"
/** 爪法 */
#define WEAPON_SKILL_CLAW           "claw"
/** 腿法 */
#define WEAPON_SKILL_LEG            "leg"
/** 剑法 */
#define WEAPON_SKILL_SWORD          "sword"
/** 刀法 */
#define WEAPON_SKILL_BLADE          "blade"
/** 枪法 */
#define WEAPON_SKILL_SPEAR          "spear"
/** 棍法 */
#define WEAPON_SKILL_CLUB           "club"
/** 暗器 */
#define WEAPON_SKILL_THROWING       "throwing"

// 辅助技能
/** 招架 */
#define SKILL_PARRY                 "parry"
/** 闪躲 */
#define SKILL_DODGE                 "dodge"
/** 内功 */
#define SKILL_FORCE                 "force"

// 错误码
/* 数字错误码的唯一命名空间是 system/errors.h（此处原 700-706 与其中 703/704 同号异义，
 * 且全仓无消费者，2026-09 复核后删除）。域内新增错误请登记到 errors.h。 */

#endif /* __GAME_SKILL_H__ */
