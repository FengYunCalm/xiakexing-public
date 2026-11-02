/**
 * @file    quest.h
 * @brief   任务系统头文件 - Quest System Interface



 *
 * @par Description:
 * 定义任务系统的常量、类型和接口：
 * - 任务状态常量
 * - 任务类型常量
 * - 任务相关宏
 *
 */

#pragma strict_types

#ifndef __GAME_QUEST_H__
#define __GAME_QUEST_H__

// 任务状态
/** 可接取 */
#define QUEST_STATUS_AVAILABLE      1
/** 已接受 */
#define QUEST_STATUS_ACCEPTED       2
/** 已完成 */
#define QUEST_STATUS_COMPLETED      3
/** 已交付（已领奖） */
#define QUEST_STATUS_DELIVERED      4
/** 已失败 */
#define QUEST_STATUS_FAILED         5

// 任务类型
/** 击杀任务 */
#define QUEST_TYPE_KILL             "kill"
/** 收集任务 */
#define QUEST_TYPE_COLLECT          "collect"
/** 对话任务 */
#define QUEST_TYPE_TALK             "talk"
/** 探索任务 */
#define QUEST_TYPE_EXPLORE          "explore"
/** 护送任务 */
#define QUEST_TYPE_ESCORT           "escort"
/** 递送任务 */
#define QUEST_TYPE_DELIVERY         "delivery"
#define QUEST_TYPE_PUZZLE           "puzzle"

#define QUEST_STEP_KILL             "kill"
#define QUEST_STEP_TALK             "talk"
#define QUEST_STEP_ASK              "ask"
#define QUEST_STEP_ARRIVE           "arrive"
#define QUEST_STEP_GIVE             "give"
#define QUEST_STEP_CONSUME          "consume"
#define QUEST_STEP_COLLECT          "collect"
#define QUEST_STEP_LOOK             "look"
#define QUEST_STEP_ACTION           "action"
#define QUEST_STEP_ESCORT           "escort"

#define QUEST_RUNTIME_VERSION       2

#define QUEST_SCOPE_PERSONAL        "personal"
#define QUEST_SCOPE_REGION          "region"
#define QUEST_SCOPE_WORLD           "world"

// 任务限制
/** 最大任务名称长度 */
#define QUEST_MAX_NAME_LENGTH       50
/** 最大任务描述长度 */
#define QUEST_MAX_DESC_LENGTH       500
/** 默认任务时限（秒，0=无限制） */
#define QUEST_DEFAULT_TIME_LIMIT    0

// 任务奖励类型
/** 经验奖励 */
#define QUEST_REWARD_EXP            "exp"
/** 金钱奖励 */
#define QUEST_REWARD_GOLD           "gold"
/** 物品奖励 */
#define QUEST_REWARD_ITEM           "item"
/** 声望奖励 */
#define QUEST_REWARD_REPUTATION     "reputation"

// 错误码
/* 数字错误码的唯一命名空间是 system/errors.h（此处原 800-806 与其中 800-804 同号异义，
 * 且全仓无消费者，2026-09 复核后删除）。域内新增错误请登记到 errors.h。 */

// 任务事件
/** 任务接受事件 */
#define QUEST_EVENT_ACCEPTED        "quest.accepted"
/** 任务完成事件 */
#define QUEST_EVENT_COMPLETED       "quest.completed"
/** 任务交付事件 */
#define QUEST_EVENT_DELIVERED       "quest.delivered"
/** 任务失败事件 */
#define QUEST_EVENT_FAILED          "quest.failed"
/** 任务进度更新事件 */
#define QUEST_EVENT_PROGRESS        "quest.progress"

#endif /* __GAME_QUEST_H__ */
