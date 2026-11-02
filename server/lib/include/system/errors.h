/**
 * @file    errors.h
 * @brief   错误码定义头文件 - Error Code Definitions



 *
 * @par Description:
 * 定义系统所有错误码，按功能模块分类：
 * - 通用错误码 (0-99)
 * - 数据库错误码 (100-199)
 * - 认证错误码 (200-299)
 * - 玩家错误码 (300-399)
 * - 物品错误码 (400-499)
 * - 移动错误码 (500-599)
 * - 战斗错误码 (600-699)
 * - 技能错误码 (700-799)
 * - 任务错误码 (800-899)
 * - 社交错误码 (900-999)
 *
 * @par Usage:
 * @code
 * #include <system/errors.h>
 * if (result < 0) {
 *     tell_object(player, GET_ERROR_MESSAGE(-result));
 * }
 * @endcode
 *
 */

#pragma strict_types

#ifndef __SYSTEM_ERRORS_H__
#define __SYSTEM_ERRORS_H__

// 通用错误码 (0-99)
/** @name 通用错误码 */
/** @{ */

/** 成功 */
#define ERR_OK                  0
/** 未知错误 */
#define ERR_UNKNOWN             1
/** 无效参数 */
#define ERR_INVALID_PARAM       2
/** 空指针 */
#define ERR_NULL_POINTER        3
/** 内存不足 */
#define ERR_OUT_OF_MEMORY       4
/** 未实现 */
#define ERR_NOT_IMPLEMENTED     5
/** 系统错误 */
#define ERR_SYSTEM_ERROR        6
/** 超时 */
#define ERR_TIMEOUT             7
/** 系统繁忙 */
#define ERR_BUSY                8
/** 未找到 */
#define ERR_NOT_FOUND           9

/** @} */

// 数据库错误码 (100-199)
/** @name 数据库错误码 */
/** @{ */

/** 数据库错误 */
#define ERR_DB_ERROR            100
/** 连接失败 */
#define ERR_DB_CONNECT          101
/** 查询失败 */
#define ERR_DB_QUERY            102
/** 保存失败 */
#define ERR_DB_SAVE             103
/** 加载失败 */
#define ERR_DB_LOAD             104
/** 数据不存在 */
#define ERR_DB_NOT_FOUND        105
/** 数据已存在 */
#define ERR_DB_EXISTS           106

/** @} */

// 认证错误码 (200-299)
/** @name 认证错误码 */
/** @{ */

/** 认证失败 */
#define ERR_AUTH_FAILED         200
/** 无效认证 */
#define ERR_AUTH_INVALID        201
/** 认证过期 */
#define ERR_AUTH_EXPIRED        202
/** 权限不足 */
#define ERR_AUTH_DENIED         203
/** 账号锁定 */
#define ERR_ACCOUNT_LOCKED      204
/** 账号封禁 */
#define ERR_ACCOUNT_BANNED      205
/** 密码错误 */
#define ERR_PASSWORD_WRONG      206
/** 密码过期 */
#define ERR_PASSWORD_EXPIRED    207
/** 登录重试超限 */
#define ERR_LOGIN_MAX_RETRY     208

/** @} */

// 玩家错误码 (300-399)
/** @name 玩家错误码 */
/** @{ */

/** 玩家不存在 */
#define ERR_PLAYER_NOT_FOUND    300
/** 玩家离线 */
#define ERR_PLAYER_OFFLINE      301
/** 玩家已存在 */
#define ERR_PLAYER_EXISTS       302
/** 无效名称 */
#define ERR_NAME_INVALID        303
/** 名称已存在 */
#define ERR_NAME_EXISTS         304
/** 名称保留 */
#define ERR_NAME_RESERVED       305

/** @} */

// 物品错误码 (400-499)
/** @name 物品错误码 */
/** @{ */

/** 物品不存在 */
#define ERR_ITEM_NOT_FOUND      400
/** 物品不属于你 */
#define ERR_ITEM_NOT_OWNED      401
/** 物品无法使用 */
#define ERR_ITEM_CANT_USE       402
/** 物品无法装备 */
#define ERR_ITEM_CANT_EQUIP     403
/** 背包已满 */
#define ERR_ITEM_FULL           404
/** 物品不足 */
#define ERR_ITEM_NOT_ENOUGH     405

/** @} */

// 移动错误码 (500-599)
/** @name 移动错误码 */
/** @{ */

/** 无效移动 */
#define ERR_MOVE_INVALID        500
/** 移动被阻挡 */
#define ERR_MOVE_BLOCKED        501
/** 负重过高 */
#define ERR_MOVE_TOO_HEAVY      502
/** 无法进入 */
#define ERR_MOVE_NO_ENTER       503
/** 无法离开 */
#define ERR_MOVE_NO_LEAVE       504
/** 目标锁定 */
#define ERR_MOVE_LOCKED         505

/** @} */

// 战斗错误码 (600-699)
/** @name 战斗错误码 */
/** @{ */

/** 无效战斗 */
#define ERR_COMBAT_INVALID      600
/** 无效目标 */
#define ERR_COMBAT_TARGET       601
/** 超出范围 */
#define ERR_COMBAT_RANGE        602
/** 技能冷却中 */
#define ERR_COMBAT_COOLDOWN     603
/** 内力不足 */
#define ERR_COMBAT_NO_MP        604
/** 生命不足 */
#define ERR_COMBAT_NO_HP        605
/** 被眩晕 */
#define ERR_COMBAT_STUNNED      606

/** @} */

/* 数字错误码命名空间规则：
 * - 全仓数字错误码只在本文档定义；game 域头（skill.h、quest.h）不得另立同号含义。
 * - 取文案用 GET_ERROR_MESSAGE(code)（本表）；域内自定义文案走 error_handler 的
 *   set_error_message() 覆盖，不新增第二张数字表。 */
// 技能错误码 (700-799)
/** @name 技能错误码 */
/** @{ */

/** 技能不存在 */
#define ERR_SKILL_NOT_FOUND     700
/** 技能未学习 */
#define ERR_SKILL_NOT_LEARNED   701
/** 技能等级不足 */
#define ERR_SKILL_LEVEL_LOW     702
/** 技能无法使用 */
#define ERR_SKILL_CANT_USE      703
/** 前置条件不满足 */
#define ERR_SKILL_PREREQ        704

/** @} */

// 任务错误码 (800-899)
/** @name 任务错误码 */
/** @{ */

/** 任务不存在 */
#define ERR_QUEST_NOT_FOUND     800
/** 任务未接受 */
#define ERR_QUEST_NOT_ACCEPTED  801
/** 无法接受任务 */
#define ERR_QUEST_CANT_ACCEPT   802
/** 无法完成任务 */
#define ERR_QUEST_CANT_COMPLETE 803
/** 任务条件不满足 */
#define ERR_QUEST_REQUIREMENT   804

/** @} */

// 社交错误码 (900-999)
/** @name 社交错误码 */
/** @{ */

/** 队伍不存在 */
#define ERR_TEAM_NOT_FOUND      900
/** 队伍已满 */
#define ERR_TEAM_FULL           901
/** 不是队员 */
#define ERR_TEAM_NOT_MEMBER     902
/** 已是队员 */
#define ERR_TEAM_IS_MEMBER      903
/** 帮派不存在 */
#define ERR_GUILD_NOT_FOUND     910
/** 不是帮众 */
#define ERR_GUILD_NOT_MEMBER    911

/** @} */

// 错误消息映射
/**
 * @brief 错误消息映射表
 * @note 用于快速查找错误码对应的中文消息
 */
#define ERROR_MESSAGES          ([ \
    ERR_OK : "成功", \
    ERR_UNKNOWN : "未知错误", \
    ERR_INVALID_PARAM : "无效参数", \
    ERR_NULL_POINTER : "空指针", \
    ERR_OUT_OF_MEMORY : "内存不足", \
    ERR_NOT_IMPLEMENTED : "未实现", \
    ERR_SYSTEM_ERROR : "系统错误", \
    ERR_TIMEOUT : "超时", \
    ERR_BUSY : "系统繁忙", \
    ERR_NOT_FOUND : "未找到目标", \
    ERR_DB_ERROR : "数据库错误", \
    ERR_DB_CONNECT : "数据库连接失败", \
    ERR_DB_QUERY : "数据库查询失败", \
    ERR_DB_SAVE : "数据库保存失败", \
    ERR_DB_LOAD : "数据库加载失败", \
    ERR_DB_NOT_FOUND : "数据不存在", \
    ERR_DB_EXISTS : "数据已存在", \
    ERR_AUTH_FAILED : "认证失败", \
    ERR_AUTH_INVALID : "无效认证", \
    ERR_AUTH_EXPIRED : "认证过期", \
    ERR_AUTH_DENIED : "权限不足", \
    ERR_ACCOUNT_LOCKED : "账号锁定", \
    ERR_ACCOUNT_BANNED : "账号封禁", \
    ERR_PASSWORD_WRONG : "密码错误", \
    ERR_PASSWORD_EXPIRED : "密码过期", \
    ERR_LOGIN_MAX_RETRY : "登录重试超限", \
    ERR_PLAYER_NOT_FOUND : "玩家不存在", \
    ERR_PLAYER_OFFLINE : "玩家离线", \
    ERR_PLAYER_EXISTS : "玩家已存在", \
    ERR_NAME_INVALID : "名称无效", \
    ERR_NAME_EXISTS : "名称已存在", \
    ERR_NAME_RESERVED : "名称保留", \
    ERR_ITEM_NOT_FOUND : "物品不存在", \
    ERR_ITEM_NOT_OWNED : "物品不属于你", \
    ERR_ITEM_CANT_USE : "物品无法使用", \
    ERR_ITEM_CANT_EQUIP : "物品无法装备", \
    ERR_ITEM_FULL : "背包已满", \
    ERR_ITEM_NOT_ENOUGH : "物品不足", \
    ERR_MOVE_INVALID : "无效移动", \
    ERR_MOVE_BLOCKED : "移动被阻挡", \
    ERR_MOVE_TOO_HEAVY : "负重过高", \
    ERR_MOVE_NO_ENTER : "无法进入", \
    ERR_MOVE_NO_LEAVE : "无法离开", \
    ERR_MOVE_LOCKED : "目标已锁定", \
    ERR_COMBAT_INVALID : "无效战斗", \
    ERR_COMBAT_TARGET : "无效目标", \
    ERR_COMBAT_RANGE : "超出范围", \
    ERR_COMBAT_COOLDOWN : "技能冷却中", \
    ERR_COMBAT_NO_MP : "内力不足", \
    ERR_COMBAT_NO_HP : "生命不足", \
    ERR_COMBAT_STUNNED : "被眩晕", \
    ERR_SKILL_NOT_FOUND : "技能不存在", \
    ERR_SKILL_NOT_LEARNED : "技能未学习", \
    ERR_SKILL_LEVEL_LOW : "技能等级不足", \
    ERR_SKILL_CANT_USE : "技能无法使用", \
    ERR_SKILL_PREREQ : "前置条件不满足", \
    ERR_QUEST_NOT_FOUND : "任务不存在", \
    ERR_QUEST_NOT_ACCEPTED : "任务未接受", \
    ERR_QUEST_CANT_ACCEPT : "无法接受任务", \
    ERR_QUEST_CANT_COMPLETE : "无法完成任务", \
    ERR_QUEST_REQUIREMENT : "任务条件不满足", \
    ERR_TEAM_NOT_FOUND : "队伍不存在", \
    ERR_TEAM_FULL : "队伍已满", \
    ERR_TEAM_NOT_MEMBER : "不是队员", \
    ERR_TEAM_IS_MEMBER : "已是队员", \
    ERR_GUILD_NOT_FOUND : "帮派不存在", \
    ERR_GUILD_NOT_MEMBER : "不是帮众", \
])

/**
 * @brief 获取错误消息
 * @param code 错误码
 * @return 错误消息字符串
 * @code
 * string msg = GET_ERROR_MESSAGE(ERR_NOT_FOUND);
 * @endcode
 */
#define GET_ERROR_MESSAGE(code) (ERROR_MESSAGES[code] || "未知错误")

#endif /* __SYSTEM_ERRORS_H__ */
