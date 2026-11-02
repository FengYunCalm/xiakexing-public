/**
 * @file    cache_l2.c
 * @brief   L2 缓存管理



 *
 * @par Description:
 * L2 缓存：服务级缓存（内存，中等）。
 */

#pragma strict_types
#pragma save_types

#include <system/globals.h>
#include <system/constants.h>
#include <game_constants.h>

#define L2_MAX_SIZE         5000
#define L2_DEFAULT_TTL      300

nosave mapping l2_cache;

int l2_evict_lru();
int l2_trim_to_capacity();
int l2_set_max_size(int size);
int l2_cleanup_expired();
void l2_flush();

void cache_l2_init()
{
    if (mapp(l2_cache))
        return;
    l2_cache = ([
        "entries": ([]),
        "max_size": L2_MAX_SIZE,
        "hits": 0,
        "misses": 0,
    ]);
}

private int cache_value_safe(mixed value, int depth)
{
    mixed key;
    mixed item;

    if (depth > 8 || undefinedp(value) || objectp(value) || functionp(value))
        return 0;
    if (stringp(value) || intp(value) || floatp(value))
        return 1;
    if (arrayp(value)) {
        foreach (item in value)
            if (!cache_value_safe(item, depth + 1))
                return 0;
        return 1;
    }
    if (mapp(value)) {
        foreach (key, item in value) {
            if (!stringp(key) || !cache_value_safe(item, depth + 1))
                return 0;
        }
        return 1;
    }
    return 0;
}

private mixed clone_cache_value(mixed value)
{
    mixed cloned;

    if (catch(cloned = restore_variable(save_variable(value))))
        return 0;
    return cloned;
}

mapping query_l2_cache_state()
{
    if (!mapp(l2_cache))
        cache_l2_init();
    return ([
        "entry_count": sizeof(l2_cache["entries"]),
        "max_size": l2_cache["max_size"],
        "hits": l2_cache["hits"],
        "misses": l2_cache["misses"],
    ]);
}

mapping peek_l2_cache_state()
{
    return mapp(l2_cache) ? ([
        "entry_count": sizeof(l2_cache["entries"]),
        "max_size": l2_cache["max_size"],
        "hits": l2_cache["hits"],
        "misses": l2_cache["misses"],
    ]) : 0;
}

mapping query_l2_entry(string key)
{
    mapping entry;

    if (!mapp(l2_cache))
        cache_l2_init();
    entry = l2_cache["entries"][key];
    if (!mapp(entry))
        return 0;
    entry = copy(entry);
    entry["value"] = clone_cache_value(entry["value"]);
    return entry;
}

int l2_entry_count()
{
    if (!mapp(l2_cache))
        cache_l2_init();
    return sizeof(l2_cache["entries"]);
}

int is_l2_cache_initialized()
{
    return mapp(l2_cache);
}

private int l2_store_until(string key, mixed value, int expires_at)
{
    mapping entry;
    int now;
    int ttl;

    if (!cache_value_safe(value, 0))
        return 0;

    if (!mapp(l2_cache))
        cache_l2_init();
    if (!stringp(key) || key == "")
        return 0;

    now = time();
    if (!intp(expires_at))
        return 0;
    ttl = expires_at - now;
    if (ttl < 0)
        ttl = 0;

    if (!mapp(l2_cache["entries"][key])) {
        while (sizeof(l2_cache["entries"]) >= l2_cache["max_size"]) {
            if (l2_evict_lru() <= 0)
                return 0;
        }
    }

    value = clone_cache_value(value);
    entry = ([
        "value": value,
        "timestamp": now,
        "expires_at": expires_at,
        "ttl": ttl,
        "access_count": 0,
        "last_access": now,
    ]);
    l2_cache["entries"][key] = entry;
    return 1;
}

int l2_set(string key, mixed value, int ttl)
{
    int effective_ttl;

    effective_ttl = ttl > 0 ? ttl : L2_DEFAULT_TTL;
    return l2_store_until(key, value, time() + effective_ttl);
}

int l2_set_until(string key, mixed value, int expires_at)
{
    return l2_store_until(key, value, expires_at);
}

mapping l2_read(string key)
{
    mapping entry;
    int now;
    int expires_at;

    if (!mapp(l2_cache))
        cache_l2_init();
    if (!stringp(key) || key == "")
        return ([ "hit": 0, "reason": "invalid_key" ]);

    entry = l2_cache["entries"][key];
    if (!mapp(entry)) {
        l2_cache["misses"]++;
        return ([ "hit": 0, "reason": "miss" ]);
    }

    now = time();
    expires_at = intp(entry["expires_at"])
        ? entry["expires_at"] : entry["timestamp"] + entry["ttl"];
    if (now >= expires_at) {
        map_delete(l2_cache["entries"], key);
        l2_cache["misses"]++;
        return ([ "hit": 0, "reason": "expired" ]);
    }

    entry["access_count"]++;
    entry["last_access"] = now;
    l2_cache["hits"]++;
    return ([
        "hit": 1,
        "value": clone_cache_value(entry["value"]),
        "expires_at": expires_at,
    ]);
}

mixed l2_get(string key)
{
    mapping result;

    result = l2_read(key);
    return mapp(result) && result["hit"] ? result["value"] : 0;
}

int l2_delete(string key)
{
    if (!mapp(l2_cache))
        cache_l2_init();

    if (!stringp(key) || key == "")
        return 0;

    if (!mapp(l2_cache["entries"][key]))
        return 0;

    map_delete(l2_cache["entries"], key);
    return 1;
}

int l2_evict_lru()
{
    string *entry_keys;
    string oldest_key;
    int oldest_time;
    int access_time;
    string key;
    mapping entry;
    int evicted;
    int threshold;

    if (!mapp(l2_cache))
        cache_l2_init();
    if (!intp(l2_cache["max_size"]) || l2_cache["max_size"] <= 0)
        return 0;

    entry_keys = keys(l2_cache["entries"]);
    if (sizeof(entry_keys) == 0)
        return 0;

    threshold = l2_cache["max_size"] / 10;
    if (threshold < 1)
        threshold = 1;
    evicted = 0;
    while (sizeof(l2_cache["entries"]) >= l2_cache["max_size"]
        && evicted < threshold) {
        oldest_key = 0;
        oldest_time = time() + 1;
        foreach (key in entry_keys) {
            entry = l2_cache["entries"][key];
            if (!mapp(entry))
                continue;
            access_time = intp(entry["last_access"])
                ? entry["last_access"] : 0;
            if (!stringp(oldest_key) || oldest_key == ""
                || access_time < oldest_time) {
                oldest_time = access_time;
                oldest_key = key;
            }
        }
        if (!stringp(oldest_key) || oldest_key == "")
            break;
        map_delete(l2_cache["entries"], oldest_key);
        evicted++;
    }
    return evicted;
}

int l2_trim_to_capacity()
{
    if (!mapp(l2_cache))
        cache_l2_init();
    while (sizeof(l2_cache["entries"]) > l2_cache["max_size"]) {
        if (l2_evict_lru() <= 0)
            return 0;
    }
    return 1;
}

int l2_set_max_size(int size)
{
    if (!intp(size) || size <= 0)
        return 0;
    if (!mapp(l2_cache))
        cache_l2_init();
    l2_cache["max_size"] = size;
    return l2_trim_to_capacity();
}

int l2_cleanup_expired()
{
    string key;
    int now;
    int cleaned;
    mapping entry;

    if (!mapp(l2_cache))
        cache_l2_init();
    now = time();
    cleaned = 0;
    foreach (key in keys(l2_cache["entries"])) {
        entry = l2_cache["entries"][key];
        if (mapp(entry) && intp(entry["expires_at"]) && now >= entry["expires_at"]) {
            map_delete(l2_cache["entries"], key);
            cleaned++;
        }
    }
    return cleaned;
}

void l2_flush()
{
    if (!mapp(l2_cache))
        cache_l2_init();
    l2_cache["entries"] = ([]);
    l2_cache["hits"] = 0;
    l2_cache["misses"] = 0;
}
