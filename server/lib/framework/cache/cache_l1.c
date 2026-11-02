/**
 * @file    cache_l1.c
 * @brief   L1 缓存管理



 *
 * @par Description:
 * L1 缓存：对象级缓存（内存，最快）。
 */

#pragma strict_types
#pragma save_types

#include <system/globals.h>
#include <system/constants.h>
#include <game_constants.h>

#define L1_MAX_SIZE         100
#define L1_DEFAULT_TTL      60

nosave mapping l1_cache;
nosave int cache_initialized = 0;

int l1_evict_lru();
int l1_trim_to_capacity();
int l1_set_max_size(int size);
int l1_cleanup_expired();
void l1_flush();

void cache_l1_init()
{
    if (cache_initialized)
        return;

    l1_cache = ([
        "entries": ([]),
        "max_size": L1_MAX_SIZE,
        "hits": 0,
        "misses": 0,
    ]);

    cache_initialized = 1;
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

mapping query_l1_cache_state()
{
    if (!cache_initialized)
        cache_l1_init();
    return ([
        "entry_count": sizeof(l1_cache["entries"]),
        "max_size": l1_cache["max_size"],
        "hits": l1_cache["hits"],
        "misses": l1_cache["misses"],
    ]);
}

mapping peek_l1_cache_state()
{
    return cache_initialized ? ([
        "entry_count": sizeof(l1_cache["entries"]),
        "max_size": l1_cache["max_size"],
        "hits": l1_cache["hits"],
        "misses": l1_cache["misses"],
    ]) : 0;
}

mapping query_l1_entry(string key)
{
    mapping entry;

    if (!cache_initialized)
        cache_l1_init();
    entry = l1_cache["entries"][key];
    if (!mapp(entry))
        return 0;
    entry = copy(entry);
    entry["value"] = clone_cache_value(entry["value"]);
    return entry;
}

int l1_entry_count()
{
    if (!cache_initialized)
        cache_l1_init();
    return sizeof(l1_cache["entries"]);
}

int is_l1_cache_initialized()
{
    return cache_initialized;
}

private int l1_store_until(string key, mixed value, int expires_at)
{
    mapping entry;
    int now;
    int ttl;

    if (!cache_value_safe(value, 0))
        return 0;

    if (!cache_initialized)
        cache_l1_init();
    if (!stringp(key) || key == "")
        return 0;

    now = time();
    if (!intp(expires_at))
        return 0;
    ttl = expires_at - now;
    if (ttl < 0)
        ttl = 0;

    if (!mapp(l1_cache["entries"][key])) {
        while (sizeof(l1_cache["entries"]) >= l1_cache["max_size"]) {
            if (l1_evict_lru() <= 0)
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
    l1_cache["entries"][key] = entry;
    return 1;
}

int l1_set(string key, mixed value, int ttl)
{
    int effective_ttl;

    effective_ttl = ttl > 0 ? ttl : L1_DEFAULT_TTL;
    return l1_store_until(key, value, time() + effective_ttl);
}

int l1_set_until(string key, mixed value, int expires_at)
{
    return l1_store_until(key, value, expires_at);
}

mapping l1_read(string key)
{
    mapping entry;
    int now;
    int expires_at;

    if (!cache_initialized)
        cache_l1_init();
    if (!stringp(key) || key == "")
        return ([ "hit": 0, "reason": "invalid_key" ]);

    entry = l1_cache["entries"][key];
    if (!mapp(entry)) {
        l1_cache["misses"]++;
        return ([ "hit": 0, "reason": "miss" ]);
    }

    now = time();
    expires_at = intp(entry["expires_at"])
        ? entry["expires_at"] : entry["timestamp"] + entry["ttl"];
    if (now >= expires_at) {
        map_delete(l1_cache["entries"], key);
        l1_cache["misses"]++;
        return ([ "hit": 0, "reason": "expired" ]);
    }

    entry["access_count"]++;
    entry["last_access"] = now;
    l1_cache["hits"]++;
    return ([
        "hit": 1,
        "value": clone_cache_value(entry["value"]),
        "expires_at": expires_at,
    ]);
}

mixed l1_get(string key)
{
    mapping result;

    result = l1_read(key);
    return mapp(result) && result["hit"] ? result["value"] : 0;
}

int l1_delete(string key)
{
    if (!cache_initialized)
        cache_l1_init();

    if (!stringp(key) || key == "")
        return 0;

    if (!mapp(l1_cache["entries"][key]))
        return 0;

    map_delete(l1_cache["entries"], key);
    return 1;
}

int l1_evict_lru()
{
    string *entry_keys;
    string oldest_key;
    int oldest_time;
    int access_time;
    string key;
    mapping entry;
    int evicted;
    int threshold;

    if (!cache_initialized)
        cache_l1_init();
    if (!intp(l1_cache["max_size"]) || l1_cache["max_size"] <= 0)
        return 0;

    entry_keys = keys(l1_cache["entries"]);
    if (sizeof(entry_keys) == 0)
        return 0;

    threshold = l1_cache["max_size"] / 5;
    if (threshold < 1)
        threshold = 1;
    evicted = 0;
    while (sizeof(l1_cache["entries"]) >= l1_cache["max_size"]
        && evicted < threshold) {
        oldest_key = 0;
        oldest_time = time() + 1;
        foreach (key in entry_keys) {
            entry = l1_cache["entries"][key];
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
        map_delete(l1_cache["entries"], oldest_key);
        evicted++;
    }
    return evicted;
}

int l1_trim_to_capacity()
{
    if (!cache_initialized)
        cache_l1_init();
    while (sizeof(l1_cache["entries"]) > l1_cache["max_size"]) {
        if (l1_evict_lru() <= 0)
            return 0;
    }
    return 1;
}

int l1_set_max_size(int size)
{
    if (!intp(size) || size <= 0)
        return 0;
    if (!cache_initialized)
        cache_l1_init();
    l1_cache["max_size"] = size;
    return l1_trim_to_capacity();
}

int l1_cleanup_expired()
{
    string key;
    int now;
    int cleaned;
    mapping entry;

    if (!cache_initialized)
        cache_l1_init();
    now = time();
    cleaned = 0;
    foreach (key in keys(l1_cache["entries"])) {
        entry = l1_cache["entries"][key];
        if (mapp(entry) && intp(entry["expires_at"]) && now >= entry["expires_at"]) {
            map_delete(l1_cache["entries"], key);
            cleaned++;
        }
    }
    return cleaned;
}

void l1_flush()
{
    if (!cache_initialized)
        cache_l1_init();
    l1_cache["entries"] = ([]);
    l1_cache["hits"] = 0;
    l1_cache["misses"] = 0;
}
