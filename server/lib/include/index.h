/**
 * @file    index.h
 * @brief   Server Unified Header Index - 统一头文件索引



 *
 * @par Description:
 * 这个文件提供所有服务器头文件的统一入口。
 * 通过包含这个文件，可以访问所有系统头文件。
 *
 * @par Usage:
 * @note
 * 这是 umbrella header，不是推荐的默认 include 入口。
 * 新代码应优先按需包含最小必要头文件；只有在旧代码或快速原型需要一站式兼容入口时，
 * 才使用本文件。
 *
 * @par Usage:
 * @code
 * #include <index.h>
 * @endcode
 *
 * @par Organization:
 * - System Headers: 系统接口（配置、守护进程、错误）
 * - Framework Headers: 框架接口（数据库、战斗、命令）
 * - Game Headers: 游戏接口（ANSI、协议、技能）
 * - Core Headers: 核心定义（常量、类型、宏）
 */

#pragma strict_types

#ifndef __SERVER_INCLUDES_H__
#define __SERVER_INCLUDES_H__

/* System Headers - 系统接口
 * ============================================================ */

/**
 * @defgroup SystemHeaders System Interfaces
 * 系统层头文件，提供底层系统服务接口
 */

#include <system/ansi.h>     /**< ANSI颜色代码 */
#include <system/config.h>   /**< 系统配置 */
#include <system/daemon.h>   /**< 守护进程接口 */
#include <system/errors.h>   /**< 错误代码定义 */
#include <system/globals.h>  /**< 全局变量 */
#include <system/trait.h>    /**< 特性系统接口 */
#include <system/xk_protocol.h>  /**< XK协议接口 */

/* Framework Headers - 框架接口
 * ============================================================ */

/**
 * @defgroup FrameworkHeaders Framework Interfaces
 * 框架层头文件，提供MUD系统核心框架接口
 */

#include <framework/dbase.h>           /**< 数据存储接口 */
#include <framework/data_module.h>     /**< 数据模块接口 */

/* Game Headers - 游戏接口
 * ============================================================ */

/**
 * @defgroup GameHeaders Game Interfaces
 * 游戏层头文件，提供游戏特定功能接口
 */

#include <game/command.h>    /**< 游戏命令定义 */
#include <game/protocol.h>   /**< 通信协议定义 */
#include <game/quest.h>      /**< 任务系统定义 */
#include <game/room.h>       /**< 房间系统定义 */
#include <game/skill.h>      /**< 技能系统定义 */

/* Core Headers - 核心定义
 * ============================================================ */

/**
 * @defgroup CoreHeaders Core Definitions
 * 核心层头文件，提供基础类型、常量和宏定义
 */

#include <core/constants.h>    /**< 系统常量定义 */
#include <core/types.h>        /**< 数据类型定义 */
#include <core/macros.h>       /**< 常用宏定义 */

/* Convenience Macros
 * ============================================================ */

/**
 * @brief 检查头文件版本兼容性
 * @param major 主版本号
 * @param minor 次版本号
 * @return 1兼容, 0不兼容
 */
#define CHECK_HEADER_VERSION(major, minor) \
    (SERVER_HEADER_MAJOR > (major) || \
     (SERVER_HEADER_MAJOR == (major) && SERVER_HEADER_MINOR >= (minor)))

/**
 * @brief 统一头文件版本
 */
#define SERVER_HEADER_MAJOR 1
#define SERVER_HEADER_MINOR 0
#define SERVER_HEADER_PATCH 0

#endif /* __SERVER_INCLUDES_H__ */
