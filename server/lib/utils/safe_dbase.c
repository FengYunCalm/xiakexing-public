/**
 * @file    safe_dbase.c
 * @brief   安全 DBase 工具库



 *
 * @par Description:
 * 提供安全的DBase操作工具：
 * - 路径验证和清理
 * - 安全的query/set操作
 * - 对象存在性检查
 * - 防止路径遍历攻击
 *
 * @note
 * 这里的“安全路径”明确只面向系统字段、配置键和机器生成的 DBase 路径，
 * 采用 ASCII + `_` + `/` 的保守白名单，不服务于任意自然语言键名。
 *
 * @par Usage:
 * @code
 * inherit "/lib/utils/safe_dbase";
 *
 * // 使用安全的查询
 * mixed value = safe_query(player, "skills/sword", 0);
 *
 * // 使用安全的设置
 * safe_set(player, "level", 10);
 * @endcode
 */

#pragma strict_types

#include <system/globals.h>
#include <system/errors.h>

#define MAX_PATH_LENGTH         64
#define MAX_COMPONENT_LENGTH    32

string sanitize_path_component(string component);
string sanitize_full_path(string path);
int is_valid_path_char(int c);
int safe_set(object target, string path, mixed value);
mixed safe_query(object target, string path, mixed default_value);
int safe_add(object target, string path, int value);
int safe_delete(object target, string path);
int object_exists(object ob);

/**
 * @brief 检查字符是否为有效的路径字符
 * @param c 字符
 * @return 1=有效, 0=无效
 */
int is_valid_path_char(int c)
{
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') ||
           c == '_' || c == '/';  // 允许路径分隔符
}

/**
 * @brief 清理路径组件（单个路径段）
 * @param component 路径组件
 * @return 清理后的组件，非法则返回空字符串
 *
 * 清理单个路径组件，防止非法字符和注入。
 * 允许字符集刻意保持为 ASCII 字母/数字/下划线，不接受中文或其他自由文本键名。
 */
string sanitize_path_component(string component)
{
    int i, len;
    string cleaned;

    if (!stringp(component) || component == "")
        return "";

    len = strlen(component);
    if (len > MAX_COMPONENT_LENGTH)
        return "";

    if (strsrch(component, "..") >= 0 || strsrch(component, "//") >= 0)
        return "";

    cleaned = "";
    for (i = 0; i < len; i++) {
        if (!((component[i] >= 'a' && component[i] <= 'z') ||
            (component[i] >= 'A' && component[i] <= 'Z') ||
            (component[i] >= '0' && component[i] <= '9') ||
            component[i] == '_'))
            return "";
        cleaned += component[i..i];
    }

    return cleaned;
}

/**
 * @brief 清理完整路径
 * @param path 完整路径
 * @return 清理后的路径，非法则返回空字符串
 *
 * 清理完整路径，检查路径遍历。
 * 返回值仅适用于当前项目已约定的机器路径键空间。
 */
string sanitize_full_path(string path)
{
    string *components;
    string cleaned_path;
    string comp;
    int i;

    if (!stringp(path) || path == "")
        return "";

    if (strlen(path) > MAX_PATH_LENGTH)
        return "";

    if (strsrch(path, "..") >= 0 || strsrch(path, "//") >= 0)
        return "";

    components = explode(path, "/");
    cleaned_path = "";

    for (i = 0; i < sizeof(components); i++) {
        if (components[i] == "")
            return "";
        comp = sanitize_path_component(components[i]);
        if (comp == "")
            return "";

        if (cleaned_path == "")
            cleaned_path = comp;
        else
            cleaned_path += "/" + comp;
    }

    return cleaned_path;
}

/**
 * @brief 检查对象是否存在且有效
 * @param ob 对象
 * @return 1=存在, 0=不存在
 *
 * 所有 DBase 操作前检查对象存在性。
 */
int object_exists(object ob)
{
    return objectp(ob) && ob != 0;
}

/**
 * @brief 安全设置DBase值
 * @param target 目标对象
 * @param path DBase路径
 * @param value 要设置的值
 * @return 1=成功, 0=失败
 *
 * 路径验证 + 对象存在性检查。
 */
int safe_set(object target, string path, mixed value)
{
    string safe_path;

    if (!object_exists(target)) {
        log_file("system/dbase_errors", "[SAFE_DBASE] safe_set: target object is null\n");
        return 0;
    }

    safe_path = sanitize_full_path(path);
    if (safe_path == "") {
        log_file("system/security", sprintf("[SAFE_DBASE] safe_set: invalid path '%s'\n", path));
        return 0;
    }

    return target->set(safe_path, value);
}

/**
 * @brief 安全查询DBase值
 * @param target 目标对象
 * @param path DBase路径
 * @param default_value 默认值
 * @return 查询结果或默认值
 *
 * 路径验证 + 对象存在性检查。
 */
mixed safe_query(object target, string path, mixed default_value)
{
    string safe_path;

    if (!object_exists(target)) {
        log_file("system/dbase_errors", "[SAFE_DBASE] safe_query: target object is null\n");
        return default_value;
    }

    safe_path = sanitize_full_path(path);
    if (safe_path == "") {
        return default_value;
    }

    return target->query_default(safe_path, default_value);
}

/**
 * @brief 安全增加DBase数值
 * @param target 目标对象
 * @param path DBase路径
 * @param value 要增加的值
 * @return 1=成功, 0=失败
 *
 * 路径验证 + 对象存在性检查 + 数值验证。
 */
int safe_add(object target, string path, int value)
{
    mixed current;
    string safe_path;

    if (!object_exists(target)) {
        log_file("system/dbase_errors", "[SAFE_DBASE] safe_add: target object is null\n");
        return 0;
    }

    safe_path = sanitize_full_path(path);
    if (safe_path == "") {
        log_file("system/security", sprintf("[SAFE_DBASE] safe_add: invalid path '%s'\n", path));
        return 0;
    }

    current = target->query(safe_path);

    if (!intp(current))
        return 0;

    return target->set(safe_path, current + value);
}

/**
 * @brief 安全删除DBase路径
 * @param target 目标对象
 * @param path DBase路径
 * @return 1=成功, 0=失败
 *
 * SECURITY FIX: 路径验证 + 对象存在性检查
 */
int safe_delete(object target, string path)
{
    string safe_path;

    // 检查对象存在性
    if (!object_exists(target)) {
        log_file("system/dbase_errors", "[SAFE_DBASE] safe_delete: target object is null\n");
        return 0;
    }

    // 验证路径安全
    safe_path = sanitize_full_path(path);
    if (safe_path == "") {
        log_file("system/security", sprintf("[SAFE_DBASE] safe_delete: invalid path '%s'\n", path));
        return 0;
    }

    // 执行安全的delete操作
    return target->delete(safe_path);
}

/**
 * @brief 批量安全设置多个DBase值
 * @param target 目标对象
 * @param data 路径-值映射表
 * @return 成功设置的数量
 *
 * PERFORMANCE FIX: 批量操作减少函数调用开销
 */
int safe_set_batch(object target, mapping data)
{
    string *keys;
    int count, i;

    if (!object_exists(target) || !mapp(data))
        return 0;

    keys = keys(data);
    count = 0;

    for (i = 0; i < sizeof(keys); i++) {
        if (safe_set(target, keys[i], data[keys[i]]))
            count++;
    }

    return count;
}

/**
 * @brief 批量安全查询多个DBase值
 * @param target 目标对象
 * @param paths 路径数组
 * @param default_value 默认值
 * @return 路径-值映射表
 *
 * PERFORMANCE FIX: 批量操作减少函数调用开销
 */
mapping safe_query_batch(object target, string *paths, mixed default_value)
{
    mapping result;
    int i;

    if (!object_exists(target) || !arrayp(paths))
        return ([]);

    result = ([]);

    for (i = 0; i < sizeof(paths); i++) {
        result[paths[i]] = safe_query(target, paths[i], default_value);
    }

    return result;
}

/**
 * @brief 记录安全事件
 * @param event 事件类型
 * @param message 事件消息
 *
 * SECURITY FIX: 统一的安全日志记录
 */
void log_security_event(string event, string message)
{
    log_file("system/security", sprintf("[SAFE_DBASE] %s: %s\n", event, message));
}

/**
 * @brief 记录DBase错误
 * @param function 函数名
 * @param message 错误消息
 *
 * SECURITY FIX: 统一的错误日志记录
 */
void log_dbase_error(string func_name, string message)
{
    log_file("system/dbase_errors", sprintf("[SAFE_DBASE] %s: %s\n", func_name, message));
}
