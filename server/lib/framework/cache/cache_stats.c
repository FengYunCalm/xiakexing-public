/**
 * @file    cache_stats.c
 * @brief   缓存统计



 *
 * @par Description:
 * 提供缓存统计信息与缓存容量配置功能。
 */

#pragma strict_types
#pragma save_types

#include <system/globals.h>
#include <system/constants.h>
#include <game_constants.h>

#define CACHE_VERSION       "2.0.0"
#define CACHE_L1            "/lib/framework/cache/cache_l1"
#define CACHE_L2            "/lib/framework/cache/cache_l2"

mapping query_l1_cache_state()
{
    return call_other(CACHE_L1, "query_l1_cache_state");
}

mapping peek_l1_cache_state()
{
    return call_other(CACHE_L1, "peek_l1_cache_state");
}

mapping query_l2_cache_state()
{
    return call_other(CACHE_L2, "query_l2_cache_state");
}

mapping peek_l2_cache_state()
{
    return call_other(CACHE_L2, "peek_l2_cache_state");
}

void cache_l1_init()
{
    call_other(CACHE_L1, "cache_l1_init");
}

void cache_l2_init()
{
    call_other(CACHE_L2, "cache_l2_init");
}

int is_cache_initialized();

mapping cache_stats()
{
    mapping stats;
    mapping l1_cache;
    mapping l2_cache;
    float l1_hit_rate;
    float l2_hit_rate;
    int l1_initialized;
    int l2_initialized;
    int l1_hits;
    int l1_misses;
    int l2_hits;
    int l2_misses;
    int l1_size;
    int l2_size;
    int l1_max;
    int l2_max;
    int l1_total;
    int l2_total;

    l1_cache = peek_l1_cache_state();
    l2_cache = peek_l2_cache_state();
    l1_initialized = mapp(l1_cache);
    l2_initialized = mapp(l2_cache);

    if (l1_initialized) {
        l1_size = intp(l1_cache["entry_count"])
            ? l1_cache["entry_count"] : 0;
        l1_max = intp(l1_cache["max_size"]) ? l1_cache["max_size"] : 0;
        l1_hits = intp(l1_cache["hits"]) ? l1_cache["hits"] : 0;
        l1_misses = intp(l1_cache["misses"]) ? l1_cache["misses"] : 0;
    }
    if (l2_initialized) {
        l2_size = intp(l2_cache["entry_count"])
            ? l2_cache["entry_count"] : 0;
        l2_max = intp(l2_cache["max_size"]) ? l2_cache["max_size"] : 0;
        l2_hits = intp(l2_cache["hits"]) ? l2_cache["hits"] : 0;
        l2_misses = intp(l2_cache["misses"]) ? l2_cache["misses"] : 0;
    }

    l1_total = l1_hits + l1_misses;
    l2_total = l2_hits + l2_misses;
    l1_hit_rate = l1_total > 0 ? l1_hits * 100.0 / l1_total : 0.0;
    l2_hit_rate = l2_total > 0 ? l2_hits * 100.0 / l2_total : 0.0;

    stats = ([
        "l1_initialized": l1_initialized,
        "l1_size": l1_size,
        "l1_max": l1_max,
        "l1_hits": l1_hits,
        "l1_misses": l1_misses,
        "l1_hit_rate": l1_hit_rate,
        "l2_initialized": l2_initialized,
        "l2_size": l2_size,
        "l2_max": l2_max,
        "l2_hits": l2_hits,
        "l2_misses": l2_misses,
        "l2_hit_rate": l2_hit_rate,
        "total_entries": l1_size + l2_size,
        "version": CACHE_VERSION,
    ]);
    return stats;
}

int set_l1_max_size(int size)
{
    if (size <= 0)
        return 0;
    return call_other(CACHE_L1, "l1_set_max_size", size);
}

int set_l2_max_size(int size)
{
    if (size <= 0)
        return 0;
    return call_other(CACHE_L2, "l2_set_max_size", size);
}

int is_cache_initialized()
{
    return mapp(peek_l1_cache_state()) && mapp(peek_l2_cache_state());
}
