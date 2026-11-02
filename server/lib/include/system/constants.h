/**
 * @file    constants.h
 * @brief   系统统一常量定义 - System Constants



 *
 * @par Description:
 * 定义系统各模块使用的统一常量，消除重复定义：
 * - 战斗系统常量
 * - 聊天系统常量
 * - 配置系统常量
 * - 临时数据常量
 * - 系统限制常量
 * - 游戏数值常量
 *
 * @par Usage:
 * @code
 * #include <system/constants.h>
 *
 * // 使用系统限制
 * if (strlen(cmd) > MAX_COMMAND_LENGTH) {
 *     return "命令过长";
 * }
 *
 * @endcode
 *
 */

#pragma strict_types

#ifndef __SYSTEM_CONSTANTS_H__
#define __SYSTEM_CONSTANTS_H__

// 战斗系统常量 - Combat System Constants
/** @name 战斗属性默认值 */
/** @{ */
/** 默认攻击值 */
#define DEFAULT_ATTACK      10

/** 默认防御值 */
#define DEFAULT_DEFENSE     10

/** 默认招架值 */
#define DEFAULT_PARRY       10
/** @} */

/** @name 战斗属性最大值 */
/** @{ */
/** 攻击上限 */
#define MAX_ATTACK          1000

/** 防御力上限 */
#define MAX_DEFENSE         1000

/** 招架上限 */
#define MAX_PARRY           1000
/** @} */

/** @name 战斗时间控制 */
/** @{ */
/** 战斗回合间隔时间（秒）- 每个回合的处理间隔 */
#define COMBAT_ROUND_TIME   1
/** @} */

// 聊天系统常量 - Chat System Constants
/** @name 聊天消息限制 */
/** @{ */
/** 聊天消息最大长度（字符数）- 超过将被截断 */
#define MAX_CHAT_LENGTH         256

/** 聊天频道历史记录保存条数 */
#define CHANNEL_HISTORY_SIZE    50

/** 表情动作冷却时间（秒）- 防止刷屏 */
#define EMOTE_COOLDOWN          2
/** @} */

// 配置系统常量 - Configuration System Constants
/** @name 配置文件路径 */
/** @{ */
/** 主配置文件路径 */
#define CONFIG_FILE         "/data/config.o"

/** 配置文件版本号 - 用于兼容性检查 */
#define CONFIG_VERSION      1

/** 配置项最大数量限制 */
#define MAX_CONFIG_SIZE     1024
/** @} */

// 临时数据常量 - Temporary Data Constants
/** @name 临时数据清理 */
/** @{ */
/** 临时数据清理间隔（秒）- 每300秒清理一次过期数据 */
#define TEMP_CLEANUP_INTERVAL   300

/** 临时数据最大条目数 - 超过将触发清理 */
#define MAX_TEMP_ENTRIES        1000
/** @} */

// 系统限制常量 - System Limit Constants
/** @name 玩家和物品限制 */
/** @{ */
/** 背包最大容量 - 玩家可携带的不同物品种类数上限（堆叠合并不占新种类） */
#define INVENTORY_CAPACITY      200

/** 命令最大长度（字符数）- 超过的命令将被拒绝 */
#define MAX_COMMAND_LENGTH      512

/** 兼容成长投影最大值；正式成长上限以技能/经验框架为准 */
#define MAX_USER_LEVEL          100

/** 帮派/门派最大成员数 */
#define MAX_GUILD_SIZE          100

/** 队伍最大成员数 */
#define MAX_TEAM_SIZE           5

/** 错误历史记录最大条数 */
#define MAX_ERROR_HISTORY       100

/** 日志级别：写入方与过滤方共用同一套编号 */
#define LOG_LEVEL_DEBUG         0
#define LOG_LEVEL_INFO          1
#define LOG_LEVEL_WARN          2
#define LOG_LEVEL_ERROR         3
#define LOG_LEVEL_FATAL         4

/** 通用历史记录最大条数 */
#define MAX_HISTORY             100

/** 邮件收件箱最大容量 */
#define MAIL_MAX_INBOX_SIZE     100

/** 邮件附件金钱上限（100万） */
#define MAIL_MAX_ATTACH_GOLD    1000000

/** 邮件过期时间（30天，单位：秒） */
#define MAIL_EXPIRE_TIME        2592000
/** @} */

// 游戏数值常量 - Game Value Constants
/** @name 玩家属性默认值 */
/** @{ */
/** 默认生命值 */
#define DEFAULT_HP              100

/** 默认法力值 */
#define DEFAULT_MP              100

/** 默认经验值 */
#define DEFAULT_EXP             0
/** @} */

/** @name 数值限制 */
/** @{ */
/** 银两/金钱上限 */
#define MAX_GOLD_ATTACH         1000000

/** 房间负重上限 */
#define MAX_ROOM_ENCUMBRANCE    100000000000
/** @} */

/** @name 时间常量（秒） */
/** @{ */
/** 每分钟秒数 */
#define SECONDS_PER_MINUTE      60

/** 每小时秒数 */
#define SECONDS_PER_HOUR        3600

/** 每天秒数 */
#define SECONDS_PER_DAY         86400
/** @} */

#endif /* __SYSTEM_CONSTANTS_H__ */
