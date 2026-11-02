/**
 * @file    cleanup.c
 * @brief   清理系统 forwarding shell
 * @author  XiaKeXing Development Team
 * @date    2026-03-20
 * @version 3.0.1
 *
 * @par Description:
 * forwarding shell，真实对象清理能力统一收口到 `/kernel/framework/cleanup`。
 * 保留本文件只为兼容旧继承路径，不在这里添加新清理逻辑。
 */

#pragma strict_types
#pragma save_types

inherit "/kernel/framework/cleanup";
