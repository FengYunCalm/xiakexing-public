/**
 * @file    cache_manager.c
 * @brief   缓存管理器兼容层



 *
 * @par Description:
 * 缓存管理器兼容层，聚合 L1/L2、API、管理和统计模块。
 * 这里是 aggregate facade / compatibility entry，不单独持有缓存真源状态；
 * L1/L2 条目与命中统计仍分别归对应子模块所有。
 *
 * 已拆分为以下模块：
 * - /lib/framework/cache/cache_l1.c    - L1缓存管理
 * - /lib/framework/cache/cache_l2.c    - L2缓存管理
 * - /lib/framework/cache/cache_api.c   - 统一缓存接口
 * - /lib/framework/cache/cache_mgmt.c  - 缓存管理
 * - /lib/framework/cache/cache_stats.c - 缓存统计
 */

#pragma strict_types
#pragma save_types

#include <system/globals.h>
#include <system/constants.h>
#include <game_constants.h>

inherit "/lib/framework/cache/cache_api";
inherit "/lib/framework/cache/cache_mgmt";
inherit "/lib/framework/cache/cache_stats";

void cache_l1_init();
void cache_l2_init();
void scheduled_cleanup();

void create()
{
    cache_l1_init();
    cache_l2_init();
    scheduled_cleanup();
}
