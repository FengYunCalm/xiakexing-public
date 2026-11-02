/**
 * @file    globals.h
 * @brief   全局定义头文件 - Global Definitions



 *
 * @par Description:
 * 定义全局常量和宏：
 * - 标准布尔常量 (TRUE/FALSE)
 * - 数据目录路径
 * - 用户断开连接类型
 * - 继承路径
 * - 对象类型宏
 * - 实用工具宏
 *
 * 服务守护进程路径已统一移到 services.h。
 *
 * @par Usage:
 * #include <system/globals.h>
 */

#pragma strict_types
#pragma save_types
#pragma warnings

#ifndef __SYSTEM_GLOBALS_H__
#define __SYSTEM_GLOBALS_H__

#include <system/services.h>

// 标准常量
#ifndef TRUE
#define TRUE  1
#endif

#ifndef FALSE
#define FALSE 0
#endif

/* ROOT_UID and BACKBONE_UID */
#define ROOT_UID           "Root"
#define BACKBONE_UID       "Backbone"

// 数据目录路径
#define DATA_DIR           "/data/"
#define ACCOUNT_DIR        DATA_DIR + "account/"
#define CHAR_DIR           DATA_DIR + "user/"     /* 角色存档当前落在 data/user */
#define QUEST_DIR          DATA_DIR + "runtime/"  /* 当前无独立 quests 目录 */
#define LOG_DIR            DATA_DIR + "log/"
#define BACKUP_DIR         DATA_DIR + "backup/"

// 用户断开连接类型
#define EXIT_NORMAL        0
#define EXIT_QUIT          1
#define EXIT_DISCONNECT    2
#define EXIT_TIMEOUT       3
#define EXIT_ERROR         4

// 核心继承路径
#define CORE_OB            "/kernel/master"
#define SIMUL_EFUN         "/kernel/simul_efun"
#define LOGIN_OB           "/kernel/login"

// 实体继承路径
#define OBJECT             "/lib/base/object"
#define LIVING             "/lib/base/living"
#define PLAYER             "/lib/base/player"
#define NPC                "/lib/base/npc"
#define ROOM               "/lib/base/room"

// 物品继承路径
#define ITEM               "/lib/base/item/item"
#define WEAPON             "/lib/base/item/weapon"
#define ARMOR              "/lib/base/item/armor"
#define CONTAINER          "/lib/base/item/container"

// 框架继承路径
#define DBASE              "/kernel/framework/data"
#define COMMAND            "/kernel/framework/command"
#define NAME               "/kernel/framework/name"
#define CLEANUP            "/kernel/framework/cleanup"
#define MOVE               "/kernel/framework/move"
#define SKILL_TRAIT        "/kernel/framework/skill"

/* F_* 路径宏的唯一参考位置是 system/trait.h；这里不再维护第二份别名。 */

// 其他守护进程路径
#define TIMER_D            "/daemons/core/timer_d"
#define EVENT_D            "/daemons/core/event_d"

// 对象类型宏
#define is_player(ob)      (ob->is_player())
#define is_npc(ob)         (ob->is_npc())
#define is_living(ob)      (ob->is_living())
#define is_room(ob)        (ob->is_room())
#define is_item(ob)        (ob->is_item())
#define is_weapon(ob)      (ob->is_weapon())
#define is_armor(ob)       (ob->is_armor())

// 实用宏
#ifndef max
#define max(a, b)          ((a) > (b) ? (a) : (b))
#endif

#ifndef min
#define min(a, b)          ((a) < (b) ? (a) : (b))
#endif

#ifndef abs
#define abs(x)             ((x) < 0 ? -(x) : (x))
#endif

#ifndef clamp
#define clamp(val, lo, hi) (max(lo, min(val, hi)))
#endif

// 安全宏
#define safe_call(fun, arg) \
    catch(evaluate((: fun, arg :)))

#define safe_load(path) \
    catch(load_object(path))

#define safe_destruct(ob) \
    do { if (objectp(ob)) destruct(ob); } while(0)

// 调试宏
#ifdef DEBUG
#define debug_log(msg)      write_file(LOG_DIR + "debug.log", msg, 1)
#define debug_print(msg)    write_file(LOG_DIR + "debug.log", msg, 1)
#else
#define debug_log(msg)      ((void)0)
#define debug_print(msg)    ((void)0)
#endif

#endif /* __SYSTEM_GLOBALS_H__ */
