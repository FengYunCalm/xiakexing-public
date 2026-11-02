/**
 * @file    paths.h
 * @brief   系统路径配置头文件



 *
 * @par Description:
 * 集中管理所有系统路径，消除硬编码路径。
 * 所有路径以"/"开头，表示绝对路径。
 */

#pragma strict_types

#ifndef PATHS_H
#define PATHS_H

// 系统层路径
#define PATH_ADM                    "/adm/"
#define PATH_CORE                   "/kernel/"
#define PATH_CORE_DAEMON            "/daemons/"            // daemon compatibility root
#define PATH_CORE_FRAMEWORK         "/kernel/framework/"
#define PATH_CORE_ENTITY            PATH_CORE_FRAMEWORK
#define PATH_CORE_KERNEL            "/kernel/"
#define PATH_CORE_INCLUDE           "/lib/include/"         // current repo uses lib/include as header root

// 游戏层路径
#define PATH_GAME                   "/"
#define PATH_GAME_WORLD             "/world/"
#define PATH_GAME_COMMANDS          "/cmds/player/"
#define PATH_GAME_ADMIN_COMMANDS    "/cmds/admin/"
#define PATH_GAME_SERVICES          "/daemons/services/"
#define PATH_GAME_MODULES           "/daemons/modules/"
#define PATH_GAME_MODULES_LEGACY    PATH_GAME_MODULES      // historical namespace alias

// 数据层路径
#define PATH_DATA                   "/data/"
#define PATH_DATA_ACCOUNT           "/data/account/"
#define PATH_DATA_LOG               "/data/log/"
#define PATH_DATA_USER              "/data/user/"

// 常用守护进程路径
#define PATH_DAEMON_ALERT           PATH_CORE_DAEMON "core/alert_d"
#define PATH_DAEMON_CHAT            PATH_GAME_SERVICES "social/chat_d"
#define PATH_DAEMON_COMBAT          PATH_GAME_SERVICES "gameplay/combat_d"
#define PATH_DAEMON_ERROR           PATH_CORE_DAEMON "core/error_d"
#define PATH_DAEMON_EVENT           PATH_CORE_DAEMON "core/event_d"
#define PATH_DAEMON_TIMER           PATH_CORE_DAEMON "core/timer_d"

// 常用服务路径
#define PATH_SERVICE_SHOP           PATH_GAME_SERVICES "item/item_shop"
#define PATH_SERVICE_BANK           PATH_GAME_SERVICES "economy/bank_d"
#define PATH_SERVICE_MAIL           PATH_GAME_SERVICES "social/mail_d"
#define PATH_SERVICE_QUEST          PATH_GAME_SERVICES "gameplay/quest_d"

#endif /* PATHS_H */
