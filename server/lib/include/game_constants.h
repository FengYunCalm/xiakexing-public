/**
 * @file    game_constants.h
 * @brief   游戏常量定义 - Game Constants



 *
 * @par Description:
 * 统一的游戏常量定义，消除魔法数字。
 * 所有时间单位：秒
 * 所有货币单位：文铜钱
 */

#pragma strict_types

#ifndef __GAME_CONSTANTS_H__
#define __GAME_CONSTANTS_H__

/* 时间常量 (单位: 秒)
 * ============================================================ */

#define TIME_SECOND     1
#define TIME_MINUTE     60
#define TIME_HOUR       3600
#define TIME_DAY        86400
#define TIME_WEEK       604800

/* 常用时间间隔 */
#define SHUTDOWN_DELAY          (5 * TIME_MINUTE)   /* 关机延迟: 5分钟 */
#define GUILD_WAR_DURATION      (30 * TIME_MINUTE)  /* 帮派战时长: 30分钟 */
#define GUILD_WAR_PREPARE       (5 * TIME_MINUTE)   /* 帮派战准备: 5分钟 */
#define MAIL_CLEANUP_INTERVAL   TIME_DAY            /* 邮件清理: 1天 */
#define BAN_CLEANUP_INTERVAL    (5 * TIME_MINUTE)   /* 封禁清理: 5分钟 */
#define DUNGEON_MONITOR_INTERVAL TIME_MINUTE        /* 副本监控: 1分钟 */
#define DUNGEON_DEFAULT_TIME    (10 * TIME_MINUTE)  /* 副本默认时长: 10分钟 */
#define HEALTH_CHECK_INTERVAL   15                  /* 健康检查: 15秒 */
#define RATE_LIMIT_SETUP_DELAY  5                   /* 速率限制初始化: 5秒 */
#define REGISTRY_VALIDATE_DELAY 10                  /* 注册表验证: 10秒 */
#define MONITOR_START_DELAY     5                   /* 监控启动: 5秒 */
#define HEARTBEAT_INTERVAL      TIME_MINUTE         /* 心跳间隔: 1分钟 */
#define RELOAD_CHECK_INTERVAL   TIME_MINUTE         /* 重载检查: 1分钟 */
#define CHECK_INTERVAL          TIME_MINUTE         /* 检查间隔: 1分钟 */
#define WINDOW_SIZE             TIME_MINUTE         /* 时间窗口: 1分钟 */
#define CLEANUP_CHECK           TIME_MINUTE         /* 清理检查: 1分钟 */
#define BAN_DURATION_MEDIUM     TIME_HOUR           /* 封禁时长: 1小时 */
#define RESTART_WINDOW          TIME_HOUR           /* 重启窗口: 1小时 */

/* 消息长度限制
 * ============================================================ */

#define MAX_TELL_LENGTH         200     /* 私聊消息最大长度 */
#define MAX_MESSAGE_LENGTH      500     /* 一般消息最大长度 */
#define MAX_SAY_LENGTH          200     /* say命令消息最大长度 */
#define MAX_INPUT_LENGTH        256     /* 输入最大长度 */
#define MAX_NAME_LENGTH         32      /* 名称最大长度 */
/* 聊天长度上限的唯一真源是 system/constants.h（256）；此处不再维护第二份默认值。 */
#define DEFAULT_MAX_MSG_BUFFER  MAX_MESSAGE_LENGTH  /* 默认消息缓冲区大小 */

/* 游戏数值常量
 * ============================================================ */

/* 概率基数 */
#define MAX_PROBABILITY     100     /* 最大概率 100% */

/* 货币换算 */
#define SILVER_PER_GOLD     100     /* 1两黄金 = 100两银子 */
#define COPPER_PER_SILVER   100     /* 1两银子 = 100文铜钱 */
#define COPPER_PER_GOLD     10000   /* 1两黄金 = 10000文铜钱 */

/* 数值限制 */
#define MAX_HP                  999999  /* 最大生命值 */
#define MAX_MP                  999999  /* 最大内力值 */
#define MAX_EP                  999999  /* 最大精力值 */
#define MAX_ATTRIBUTE_VALUE     999999  /* 最大属性值 */
#define MAX_LEVEL               100     /* legacy: 不作为新数值主轴 */

/* 定时器限制 */
#define MAX_TIMER_SEQUENCE  1000000000  /* 定时器序列号最大值 */
#define TIMER_RESET_VALUE   1           /* 定时器序列号重置值 */
#define API_METRIC_LIMIT    100         /* API指标保留数量 */

/* 技能等级常量
 * ============================================================ */

#define SKILL_MASTERY_APPRENTICE    60      /* 略有小成 */
#define SKILL_MASTERY_MASTER        200     /* 出神入化 */
#define GUILD_ELDER                 60      /* 帮派长老等级 */

/* 年龄常量
 * ============================================================ */

#define AGE_OLD_START           60      /* 老年开始年龄 */
#define TOTAL_ATTR_POINTS       60      /* 可分配总属性点数 */

/* 系统限制
 * ============================================================ */

#define MAX_LOG_FILE_SIZE       500000  /* 日志文件最大大小(字节) */
#define MAX_CACHE_SIZE          50000   /* 最大缓存大小 */
#define MAX_SESSION_TIMEOUT     TIME_HOUR  /* 会话超时: 1小时 */

/* 经验等级常量
 * ============================================================ */

#define EXP_LEVEL_1     1000
#define EXP_LEVEL_2     5000
#define EXP_LEVEL_3     20000
#define EXP_LEVEL_4     100000

/* 战斗常量
 * ============================================================ */

#define COMBAT_INTERVAL     3       /* legacy combat heartbeat interval */
#define MAX_COMBAT_ROUNDS   100     /* 最大战斗回合数 */

/* 战斗结果 */
#define COMBAT_RESULT_WIN   "win"   /* 胜利 */
#define COMBAT_RESULT_LOSE  "lose"  /* 失败 */
#define COMBAT_RESULT_DRAW  "draw"  /* 平局 */

/* 副本难度常量
 * ============================================================ */

#define DUNGEON_DIFFICULTY_NORMAL 1

/* 战斗状态 */
#define COMBAT_STATE_IDLE       0   /* 空闲 */
#define COMBAT_STATE_FIGHTING   1   /* 战斗中 */
#define COMBAT_STATE_ENDED      2   /* 已结束 */

/* 银行/经济常量
 * ============================================================ */

#define ESTATE_PRICE        10000000    /* 庄园价格: 1000万银两 */

/* 速率限制常量
 * ============================================================ */

#define RATE_LIMIT_COMBAT       500     /* 战斗请求限制 */
#define RATE_LIMIT_BANK         200     /* 银行操作限制 */
#define RATE_LIMIT_QUEST        200     /* 任务操作限制 */

/* 物品品质（配置模板与实例通用，item_template.c 的 #ifndef 本地定义兼容保留） */
#ifndef ITEM_QUALITY_COMMON
#define ITEM_QUALITY_COMMON 1
#endif

#ifndef ITEM_QUALITY_UNCOMMON
#define ITEM_QUALITY_UNCOMMON 2
#endif

#ifndef ITEM_QUALITY_RARE
#define ITEM_QUALITY_RARE 3
#endif

#ifndef ITEM_QUALITY_EPIC
#define ITEM_QUALITY_EPIC 4
#endif

/* 防具装备槽位（词条聚合/耐久消耗/展示共用，勿在各处硬编码） */
#ifndef EQUIPMENT_ARMOR_SLOTS
#define EQUIPMENT_ARMOR_SLOTS ({ "head", "neck", "chest", "cloak", "waist", "hands", "finger", "arm", "legs", "feet", "shield" })
#endif
#define RATE_LIMIT_LOG          500     /* 日志记录限制 */
#define RATE_LIMIT_SECURITY     200     /* 安全操作限制 */

#endif /* __GAME_CONSTANTS_H__ */
