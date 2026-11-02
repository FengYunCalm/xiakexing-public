/**
 * @file    cleanup.c
 * @brief   清理命令基类



 *
 * @par Description:
 * 需要清理功能的命令基类，提供临时资源注册与统一清理能力。
 */

#pragma strict_types
#pragma save_types

inherit "/kernel/framework/cleanup";
inherit "/lib/framework/command";

private nosave mapping _temp_resources = ([]);

/**
 * @brief 注册临时资源
 * @param key 资源标识
 * @param value 资源值
 */
void register_temp_resource(string key, mixed value)
{
    if (!key || key == "")
        return;

    _temp_resources[key] = value;
}

/**
 * @brief 获取临时资源
 * @param key 资源标识
 * @return 资源值
 */
mixed query_temp_resource(string key)
{
    if (!key || key == "")
        return 0;

    return _temp_resources[key];
}

/**
 * @brief 清理临时资源
 * @param key 资源标识
 */
void cleanup_temp_resource(string key)
{
    if (!key || key == "") {
        _temp_resources = ([]);
    } else {
        map_delete(_temp_resources, key);
    }
}

/**
 * @brief 命令销毁时的清理
 */
void destruct()
{
    cleanup_temp_resource(0);
}
