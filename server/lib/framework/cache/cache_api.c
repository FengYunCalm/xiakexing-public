/**
 * @file    cache_api.c
 * @brief   缓存统一接口



 *
 * @par Description:
 * 提供统一的缓存操作接口，按规则选择 L1/L2 缓存。
 */

#pragma strict_types
#pragma save_types

#include <system/globals.h>
#include <system/constants.h>
#include <game_constants.h>

#define L1_DEFAULT_TTL      60
#define CACHE_L1            "/lib/framework/cache/cache_l1"
#define CACHE_L2            "/lib/framework/cache/cache_l2"
#define CACHE_MANAGER       "/lib/framework/cache/cache_manager"

private void ensure_cache_runtime_started();

private void ensure_cache_runtime_started()
{
    load_object(CACHE_MANAGER);
}


/**
 * @brief 写入 L1 缓存
 * @param key 缓存键
 * @param value 缓存值
 * @param ttl 生存时间（秒）
 * @return 1=成功, 0=失败
 */
int l1_set(string key, mixed value, int ttl)
{
    return call_other(CACHE_L1, "l1_set", key, value, ttl);
}

/**
 * @brief 读取 L1 缓存
 * @param key 缓存键
 * @return 缓存值，未命中返回 0
 */
mixed l1_get(string key)
{
    return call_other(CACHE_L1, "l1_get", key);
}

/**
 * @brief 删除 L1 缓存项
 * @param key 缓存键
 * @return 1=成功, 0=失败
 */
int l1_delete(string key)
{
    return call_other(CACHE_L1, "l1_delete", key);
}

/**
 * @brief 写入 L2 缓存
 * @param key 缓存键
 * @param value 缓存值
 * @param ttl 生存时间（秒）
 * @return 1=成功, 0=失败
 */
int l2_set(string key, mixed value, int ttl)
{
    return call_other(CACHE_L2, "l2_set", key, value, ttl);
}

/**
 * @brief 读取 L2 缓存
 * @param key 缓存键
 * @return 缓存值，未命中返回 0
 */
mixed l2_get(string key)
{
    return call_other(CACHE_L2, "l2_get", key);
}

/**
 * @brief 删除 L2 缓存项
 * @param key 缓存键
 * @return 1=成功, 0=失败
 */
int l2_delete(string key)
{
    return call_other(CACHE_L2, "l2_delete", key);
}

/**
 * @brief 统一写入缓存
 * @param key 缓存键
 * @param value 缓存值
 * @param ttl 生存时间（秒）
 * @param level 缓存层级（1=L1, 2=L2）
 * @return 1=成功, 0=失败
 */
int cache_set(string key, mixed value, int ttl, int level)
{
    int result;

    ensure_cache_runtime_started();
    if (!intp(level))
        level = 1;

    switch (level) {
        case 2:
            result = l2_set(key, value, ttl);
            if (result)
                l1_delete(key);
            return result;
        case 1:
        default:
            result = l1_set(key, value, ttl);
            if (result)
                l2_delete(key);
            return result;
    }
}

/**
 * @brief 统一读取缓存
 * @param key 缓存键
 * @return 缓存值，未命中返回 0
 */
mixed cache_get(string key)
{
    mapping result;
    int expires_at;

    ensure_cache_runtime_started();
    if (!stringp(key) || key == "")
        return 0;

    result = call_other(CACHE_L1, "l1_read", key);
    if (mapp(result) && result["hit"])
        return result["value"];

    result = call_other(CACHE_L2, "l2_read", key);
    if (mapp(result) && result["hit"]) {
        expires_at = intp(result["expires_at"]) ? result["expires_at"] : 0;
        if (expires_at > time())
            call_other(CACHE_L1, "l1_set_until", key,
                result["value"], expires_at);
        return result["value"];
    }

    return 0;
}

/**
 * @brief 统一删除缓存项
 * @param key 缓存键
 * @return 1=成功, 0=失败
 */
int cache_delete(string key)
{
    int result;

    ensure_cache_runtime_started();

    result = 0;

    if (l1_delete(key))
        result = 1;

    if (l2_delete(key))
        result = 1;

    return result;
}
