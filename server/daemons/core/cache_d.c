/**
 * @file    cache_d.c
 * @brief   缓存视图守护进程



 *
 * @par Description:
 * 这里只维护 daemon 侧缓存视图，不替代 `lib/framework/cache/` 目录下的
 * 框架缓存栈。
 *
 * 当前职责：
 * - 为守护进程提供轻量 TTL 缓存
 * - 暴露缓存使用率/清理状态等运行时视图
 *
 * 不承担：
 * - 框架级缓存真源
 * - 配置/模板/业务状态的长期持久化真源
 * - 对 `lib/framework/cache/` 目录下缓存栈的替代或镜像复制
 */

#pragma strict_types
#pragma save_types

#include <game_constants.h>

inherit "/lib/framework/daemon";

nosave mapping _cache;
nosave int _max_cache_size;
nosave int _cleanup_call_out_handle;

private int cache_cleanup();
private void cache_flush();
private int cache_mset(mapping data, int ttl);
private mapping cache_stats();

private string query_oldest_cache_key()
{
    string oldest_key;
    string key;
    int oldest_access;
    mapping entry;

    oldest_key = 0;
    oldest_access = time();

    foreach (key in keys(_cache)) {
        entry = _cache[key];
        if (!mapp(entry))
            continue;
        if (!stringp(oldest_key) || entry["last_access"] < oldest_access) {
            oldest_key = key;
            oldest_access = entry["last_access"];
        }
    }

    return oldest_key;
}

private void enforce_capacity_limit()
{
    string oldest_key;

    if (!intp(_max_cache_size) || _max_cache_size <= 0)
        return;

    cache_cleanup();
    while (sizeof(_cache) >= _max_cache_size) {
        oldest_key = query_oldest_cache_key();
        if (!stringp(oldest_key) || oldest_key == "")
            break;
        map_delete(_cache, oldest_key);
    }
}

void create()
{
    ::create();
    _cache = ([]);
    _max_cache_size = MAX_CACHE_SIZE;
    _cleanup_call_out_handle = 0;
    if (!_cleanup_call_out_handle)
        _cleanup_call_out_handle = call_out("scheduled_cleanup", 300);
    log_file("system/daemon", sprintf("[%s] Cache daemon initialized\n", ctime(time())));
}

private void set_max_cache_size(int size)
{
    if (size > 0)
        _max_cache_size = size;
}

private int cache_set(string key, mixed value, int ttl)
{
    if (!stringp(key) || key == "")
        return 0;

    if (undefinedp(_cache[key]))
        enforce_capacity_limit();

    if (!undefinedp(_cache[key]) || sizeof(_cache) < _max_cache_size) {
        _cache[key] = (["value": value, "expire": ttl > 0 ? time() + ttl : 0, "last_access": time()]);
        return 1;
    }

    return 0;
}

private mixed cache_get(string key)
{
    mapping item;

    item = _cache[key];
    if (!mapp(item))
        return 0;
    if (item["expire"] > 0 && time() > item["expire"]) {
        map_delete(_cache, key);
        return 0;
    }
    item["last_access"] = time();
    return item["value"];
}

private int cache_delete(string key)
{
    if (!mapp(_cache[key]))
        return 0;
    map_delete(_cache, key);
    return 1;
}

private void cache_flush()
{
    _cache = ([]);
}

private int cache_cleanup()
{
    int cleaned;
    string key;

    cleaned = 0;
    foreach (key in keys(_cache)) {
        if (_cache[key]["expire"] > 0 && time() > _cache[key]["expire"]) {
            map_delete(_cache, key);
            cleaned++;
        }
    }
    return cleaned;
}

private int cache_mset(mapping data, int ttl)
{
    string key;
    int count;

    if (!mapp(data))
        return 0;
    count = 0;
    foreach (key in keys(data)) {
        if (cache_set(key, data[key], ttl))
            count++;
    }
    return count;
}

private mapping cache_stats()
{
    return ([
        "size": sizeof(_cache),
        "max_size": _max_cache_size,
        "usage_percent": _max_cache_size > 0 ? sizeof(_cache) * 100 / _max_cache_size : 0
    ]);
}

void reset()
{
    int cleaned;

    cleaned = cache_cleanup();
    if (cleaned > 0)
        log_file("system/cache", sprintf("[%s] Cleanup: removed %d expired entries\n", ctime(time()), cleaned));
}

void remove()
{
    cache_flush();
}

void scheduled_cleanup()
{
    int cleaned;
    mapping stats;

    _cleanup_call_out_handle = 0;

    cleaned = cache_cleanup();
    stats = cache_stats();
    if (cleaned > 0 || stats["usage_percent"] > 80)
        log_file("system/cache", sprintf("[%s] Scheduled cleanup: removed=%d, size=%d/%d (%d%%)\n", ctime(time()), cleaned, stats["size"], stats["max_size"], stats["usage_percent"]));
    if (!_cleanup_call_out_handle)
        _cleanup_call_out_handle = call_out("scheduled_cleanup", 300);
}

mapping query_status()
{
    mapping stats;

    stats = cache_stats();
    return ([
        "daemon": "cache_d",
        "source": "daemon_local_ttl_cache",
        "framework_cache_owner": 0,
        "cache_size": stats["size"],
        "cache_max_size": stats["max_size"],
        "cache_usage_percent": stats["usage_percent"]
    ]);
}

private int warmup_cache(mapping data)
{
    int count;

    count = cache_mset(data, 600);
    log_file("system/cache", sprintf("[%s] Warmup: loaded %d entries\n", ctime(time()), count));
    return count;
}
