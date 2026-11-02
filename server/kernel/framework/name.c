/**
 * @file    name.c
 * @brief   名称管理框架
 * @author  XiaKeXing Development Team
 * @date    2026-03-20
 * @version 1.0.1
 *
 * @par Description:
 * 提供对象名称、短描述、长描述和 ID 别名管理功能。
 */

#pragma strict_types
#pragma save_types

#include <system/globals.h>

int set(string path, mixed value);
mixed query(string path);

void create()
{
}

/**
 * @brief 设置对象名称
 * @param name 名称字符串
 */
void set_name(string name)
{
    if (!stringp(name) || name == "")
        return;

    set("name", name);
    if (function_exists("invalidate_xk_score_projection", this_object()))
        call_other(this_object(), "invalidate_xk_score_projection");
}

/**
 * @brief 查询对象名称
 * @return 名称字符串
 */
string query_name()
{
    return query("name") || "无名";
}

/**
 * @brief 设置短描述
 * @param short 短描述字符串
 */
void set_short(string short_desc)
{
    if (!stringp(short_desc))
        return;

    set("short", short_desc);
}

/**
 * @brief 查询短描述
 * @return 短描述字符串
 */
string query_short()
{
    return query("short") || query_name();
}

/**
 * @brief 设置长描述
 * @param long 长描述字符串
 */
void set_long(string long_desc)
{
    if (!stringp(long_desc))
        return;

    set("long", long_desc);
}

/**
 * @brief 查询长描述
 * @return 长描述字符串
 */
string query_long()
{
    return query("long") || "这是一只普通的" + query_name() + "。\n";
}

/**
 * @brief 设置对象ID
 * @param id ID字符串或字符串数组
 */
void set_id(mixed id)
{
    if (stringp(id)) {
        set("id", ({ id }));
    } else if (arrayp(id)) {
        set("id", id);
    }
}

/**
 * @brief 查询对象ID
 * @return ID字符串数组
 */
string *query_id()
{
    mixed id = query("id");
    if (stringp(id))
        return ({ id });
    if (arrayp(id))
        return id;
    return ({});
}

/**
 * @brief 检查ID是否匹配
 * @param str ID字符串
 * @return 1匹配, 0不匹配
 */
int id(string str)
{
    string *ids = query_id();

    if (!stringp(str) || str == "")
        return 0;

    return (member_array(str, ids) != -1);
}

/**
 * @brief 添加ID别名
 * @param alias 别名字符串
 */
void add_id(string alias)
{
    string *ids;

    if (!stringp(alias) || alias == "")
        return;

    ids = query_id();

    if (member_array(alias, ids) != -1)
        return;

    ids += ({ alias });
    set("id", ids);
}
