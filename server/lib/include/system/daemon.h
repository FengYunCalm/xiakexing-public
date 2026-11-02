/**
 * @file    daemon.h
 * @brief   守护进程接口头文件 - Daemon Service Interface



 *
 * @par Description:
 * 定义守护进程的接口和访问宏。
 * 服务路径宏已统一移到 services.h。
 * 本头文件中的函数声明偏向跨模块调用约定，用于描述常见 daemon 能力；
 * 它们不等同于 C 风格链接保证，实际可用性仍取决于目标 daemon/base object
 * 是否实现了相同名字和参数语义。
 *
 * @par Usage:
 * @code
 * #include <system/daemon.h>
 * object combat_d = find_object(COMBAT_D);
 * @endcode
 *
 */

#pragma strict_types

#ifndef __SYSTEM_DAEMON_H__
#define __SYSTEM_DAEMON_H__

// 包含统一服务路径定义
#include <system/services.h>

// 守护进程基类路径
/**
 * 守护进程兼容基类。
 * 当前真正的 daemon 基类能力在 /lib/framework/daemon，
 * 这里保留给仍在旧命名空间中的 compat daemons 使用。
 */
#ifndef CORE_DAEMON_BASE
#define CORE_DAEMON_BASE    "/daemons/daemon_base"
#endif

/** 错误处理守护进程 */
#ifndef ERROR_D
#define ERROR_D             "/daemons/core/error_d"
#endif

/** 定时器守护进程 */
#ifndef TIMER_D
#define TIMER_D             "/daemons/core/timer_d"
#endif

/** 事件总线守护进程 */
#ifndef EVENT_D
#define EVENT_D             "/daemons/core/event_d"
#endif

/** 聊天频道守护进程 */
#ifndef CHAT_D
#define CHAT_D              "/daemons/services/social/chat_d"
#endif

// 系统对象路径
#ifndef LOGIN_OB
#define LOGIN_OB            "/kernel/login"
#endif

#ifndef COMMAND_OB
#define COMMAND_OB          0
#endif

// 注意：本头文件当前只保留“稳定路径宏 + 兼容基类路径”。
// 过去那组泛化的 service 函数声明（combat_start/query_room_name/create_npc 等）
// 在仓内并没有形成一套可验证、可依赖的统一 API，继续保留只会扩大假契约面。
// 调用方应直接 include 对应真源头文件，或通过 services.h 中的路径宏访问具体 daemon。

#endif /* __SYSTEM_DAEMON_H__ */
