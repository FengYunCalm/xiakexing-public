/**
 * @file    container.c
 * @brief   容器基类



 *
 * @par Description:
 * 容器基类，继承自 `item.c`，提供容量、开关、锁定和内容管理能力。
 */

#pragma strict_types

#include <system/globals.h>
#include <system/ansi.h>
#include <framework/dbase.h>

inherit "/lib/base/item/item";

nosave int max_items;
nosave int max_weight;
nosave mapping contents;
nosave int is_locked;
nosave string key_id;
nosave int _max_capacity;
nosave int _current_capacity;
nosave int _is_open;
nosave int _is_locked;
nosave string _key_id;

/**
 * @brief 设置最大容量
 * @param cap 容量值
  * @return 无返回值
 */
void set_max_capacity(int cap)
{
    _max_capacity = cap;
    set("max_capacity", cap);
}

/**
 * @brief 获取最大容量
 * @return 容量值
 */
int query_max_capacity()
{
    return _max_capacity || query("max_capacity") || 50;
}

/**
 * @brief 获取当前容量
 * @return 当前容量
 */
int query_current_capacity()
{
    object *inv = all_inventory(this_object());
    int total = 0;
    int i;

    int __sizeof_inv = sizeof(inv);
    for (i = 0; i < __sizeof_inv; i++) {
        total += inv[i]->query_weight() || 1;
    }

    return total;
}

/**
 * @brief 检查是否可以容纳物品
 * @param ob 物品对象
 * @return 1=可以, 0=不可以
 */
int can_hold(object ob)
{
    int current = query_current_capacity();
    int max_cap = query_max_capacity();
    int item_weight = ob->query_weight() || 1;

    return (current + item_weight) <= max_cap;
}

/**
 * @brief 打开容器
 * @return 1=成功, 0=失败
 */
int open()
{
    if (_is_locked) {
        return 0;
    }

    _is_open = 1;
    set("open", 1);
    return 1;
}

/**
 * @brief 关闭容器
 * @return 1=成功
 */
int close()
{
    _is_open = 0;
    set("open", 0);
    return 1;
}

/**
 * @brief 检查是否开启
 * @return 1=开启, 0=关闭
 */
int query_is_open()
{
    return _is_open || query("open");
}

/**
 * @brief 设置钥匙ID
 * @param key 钥匙ID
  * @return 无返回值
 */
void set_key_id(string key)
{
    _key_id = key;
    set("key_id", key);
}

/**
 * @brief 获取钥匙ID
 * @return 钥匙ID
 */
string query_key_id()
{
    return _key_id || query("key_id");
}

/**
 * @brief 检查是否锁定
 * @return 1=锁定, 0=未锁定
 */
int query_is_locked()
{
    return _is_locked || query("locked");
}

/**
 * @brief 锁定容器
 * @param key 钥匙对象
 * @return 1=成功, 0=失败
 */
int lock(object key)
{
    if (!query_key_id()) {
        return 0;
    }

    if (key && key->query_key_id() != query_key_id()) {
        return 0;
    }

    if (query_is_open()) {
        return 0;
    }

    _is_locked = 1;
    set("locked", 1);
    return 1;
}

/**
 * @brief 解锁容器
 * @param key 钥匙对象
 * @return 1=成功, 0=失败
 */
int unlock(object key)
{
    if (!query_is_locked()) {
        return 1;
    }

    if (!key) {
        return 0;
    }

    if (key->query_key_id() != query_key_id()) {
        return 0;
    }

    _is_locked = 0;
    set("locked", 0);
    return 1;
}

/**
 * @brief 放入物品
 * @param ob 物品对象
 * @return 1=成功, 0=失败
 */
int put_in(object ob)
{
    if (!objectp(ob)) {
        return 0;
    }

    if (!query_is_open()) {
        return 0;
    }

    if (!can_hold(ob)) {
        return 0;
    }

    return ob->move(this_object());
}

/**
 * @brief 取出物品
 * @param ob 物品对象
 * @param to 目标对象
 * @return 1=成功, 0=失败
 */
int take_out(object ob, object to)
{
    if (!objectp(ob) || !objectp(to)) {
        return 0;
    }

    if (!query_is_open()) {
        return 0;
    }

    if (environment(ob) != this_object()) {
        return 0;
    }

    return ob->move(to);
}

/**
 * @brief 获取容器内物品列表
 * @return 物品对象数组
 */
object *query_inventory()
{
    return all_inventory(this_object());
}

/**
 * @brief 获取简短描述
 * @return 描述字符串
 */
string query_short()
{
    string desc = ::query_short();

    if (query_is_open()) {
        desc += "(开启)";
    } else {
        desc += "(关闭)";
    }

    if (query_is_locked()) {
        desc += "(锁定)";
    }

    return desc;
}

/**
 * @brief 获取详细描述
 * @return 描述字符串
 */
string query_long()
{
    string desc = ::query_long();
    object *inv;
    int i;

    desc += sprintf("\n容量: %d/%d\n", query_current_capacity(), query_max_capacity());

    if (query_is_open()) {
        inv = query_inventory();
        if (sizeof(inv) > 0) {
            int __sizeof_inv = sizeof(inv);
            desc += "里面装着：\n";
            for (i = 0; i < __sizeof_inv; i++) {
                desc += "  " + inv[i]->query_short() + "\n";
            }
        } else {
            desc = desc + "里面是空的。\n";
        }
    } else {
        desc += "容器是关闭的，看不见里面。\n";
    }

    return desc;
}

/**
 * @brief 创建时初始化
  * @return 无返回值
 */
void create()
{
    ::create();

    set_item_type("container");
    set("is_container", 1);
    _max_capacity = 50;
    _is_open = 1;  // 默认开启
    _is_locked = 0;
    _key_id = 0;
}

// End of container.c
