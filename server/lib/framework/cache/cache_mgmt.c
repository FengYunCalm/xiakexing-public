/**
 * @file    cache_mgmt.c
 * @brief   缓存管理



 *
 * @par Description:
 * 提供缓存清空、过期清理和周期性清理调度功能。
 */

#pragma strict_types
#pragma save_types

#include <system/globals.h>
#include <system/constants.h>
#include <game_constants.h>

#define CLEANUP_INTERVAL    300
#define CACHE_L1            "/lib/framework/cache/cache_l1"
#define CACHE_L2            "/lib/framework/cache/cache_l2"

int is_cache_initialized();

void cache_flush()
{
    if (!is_cache_initialized())
        return;

    call_other(CACHE_L1, "l1_flush");
    call_other(CACHE_L2, "l2_flush");
}

int cache_cleanup()
{
    if (!is_cache_initialized())
        return 0;

    return call_other(CACHE_L1, "l1_cleanup_expired")
        + call_other(CACHE_L2, "l2_cleanup_expired");
}

void scheduled_cleanup()
{
    int cleaned;

    cleaned = cache_cleanup();

    if (cleaned > 0) {
        log_file("system/cache", sprintf("[%s] Cleaned %d expired entries\n",
            ctime(time()), cleaned));
    }

    call_out("scheduled_cleanup", CLEANUP_INTERVAL);
}
