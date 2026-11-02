/**
 * @file    util_item.c
 * @brief   物品查找工具 - Item Finding Utilities



 *
 * @par Description:
 * 提供物品查找功能，包括在玩家身上、环境中、容器中查找物品。
 */

#pragma strict_types
#pragma save_types

#include <system/globals.h>
#include <system/ansi.h>
#include <system/constants.h>
#include <system/daemon.h>
#include <system/errors.h>
#include <game_constants.h>

string get_item_display_name(object item, string fallback);

private string normalize_item_lookup_token(string token)
{
    if (!stringp(token) || token == "")
        return "";

    return lower_case(token);
}

private int item_token_matches_exact(string candidate, string expected)
{
    if (!stringp(candidate) || candidate == "" || !stringp(expected) || expected == "")
        return 0;
    if (candidate == expected)
        return 1;

    candidate = normalize_item_lookup_token(candidate);
    expected = normalize_item_lookup_token(expected);
    return candidate != "" && expected != "" && candidate == expected;
}

private int item_token_matches_fuzzy(string candidate, string expected)
{
    if (!stringp(candidate) || candidate == "" || !stringp(expected) || expected == "")
        return 0;
    if (strsrch(candidate, expected) != -1)
        return 1;

    candidate = normalize_item_lookup_token(candidate);
    expected = normalize_item_lookup_token(expected);

    return candidate != "" && expected != "" && strsrch(candidate, expected) != -1;
}

private string *query_item_lookup_tokens(object item)
{
    string *tokens;
    mixed ids;
    string token;

    if (!objectp(item))
        return ({ });

    tokens = ({
        get_item_display_name(item, ""),
        item->query("name") || "",
        item->query("template_id") || "",
        get_object_id(item),
    });

    ids = function_exists("query_id", item)
        ? item->query_id() : ({ get_object_id(item) });
    if (stringp(ids) && ids != "")
        tokens += ({ ids });
    else if (arrayp(ids))
        foreach (token in ids)
            if (stringp(token) && token != "")
                tokens += ({ token });

    return filter_array(tokens, (: stringp($1) && $1 != "" :));
}

private int item_matches_name_exact(object item, string item_name)
{
    string token;

    foreach (token in query_item_lookup_tokens(item))
        if (item_token_matches_exact(token, item_name))
            return 1;

    return 0;
}

private int item_matches_name_fuzzy(object item, string item_name)
{
    string token;

    foreach (token in query_item_lookup_tokens(item))
        if (item_token_matches_fuzzy(token, item_name))
            return 1;

    return 0;
}

object find_item_in_collection(object *items, string item_name)
{
    object item;

    if (!arrayp(items) || !stringp(item_name) || item_name == "")
        return 0;

    foreach (item in items)
        if (objectp(item) && item_matches_name_exact(item, item_name))
            return item;

    foreach (item in items)
        if (objectp(item) && item_matches_name_fuzzy(item, item_name))
            return item;

    return 0;
}

/**
 * @brief 查找玩家身上的物品
 * @param player 玩家对象
 * @param item_name 物品名称
 * @return 物品对象，找不到返回 0
 */
object find_item_in_player(object player, string item_name)
{
    object item;

    if (!player || !item_name || item_name == "")
        return 0;

    item = present(item_name, player);
    if (!objectp(item))
        item = find_item_in_collection(all_inventory(player), item_name);

    if (!item) {
        return call_other(ERROR_D, "notify_fail_code", ERR_ITEM_NOT_FOUND,
            sprintf("你身上没有「%s」", item_name), 0);
    }

    return item;
}

/**
 * @brief 查找房间/环境中的物品
 * @param env 环境对象
 * @param item_name 物品名称
 * @return 物品对象，找不到返回 0
 */
object find_item_in_environment(object env, string item_name)
{
    object item;

    if (!env || !item_name || item_name == "")
        return 0;

    item = present(item_name, env);
    if (!objectp(item))
        item = find_item_in_collection(all_inventory(env), item_name);
    if (!item) {
        return call_other(ERROR_D, "notify_fail_code", ERR_ITEM_NOT_FOUND,
            sprintf("这里没有「%s」", item_name), 0);
    }

    return item;
}

/**
 * @brief 查找容器中的物品
 * @param container 容器对象
 * @param item_name 物品名称
 * @return 物品对象，找不到返回 0
 */
object find_item_in_container(object container, string item_name)
{
    object item;

    if (!container || !item_name || item_name == "")
        return 0;

    item = present(item_name, container);
    if (!objectp(item))
        item = find_item_in_collection(all_inventory(container), item_name);
    if (!item) {
        return call_other(ERROR_D, "notify_fail_code", ERR_ITEM_NOT_FOUND,
            sprintf("%s里没有「%s」", container->query_name(), item_name), 0);
    }

    return item;
}

/**
 * @brief 查找容器对象
 * @param player 玩家对象
 * @param container_name 容器名称
 * @return 容器对象，找不到返回 0
 */
object find_container(object player, string container_name)
{
    object container;
    object env;

    if (!player || !container_name || container_name == "")
        return 0;

    env = environment(player);
    if (!env)
        return 0;

    container = present(container_name, player);

    if (!container)
        container = present(container_name, env);

    if (!container) {
        return call_other(ERROR_D, "notify_fail_code", ERR_ITEM_NOT_FOUND,
            sprintf("你身上和这里都没有「%s」", container_name), 0);
    }

    if (!container->query("is_container")) {
        return call_other(ERROR_D, "notify_fail_code", ERR_ITEM_CANT_USE,
            sprintf("%s不是容器", container->query_name()), 0);
    }

    return container;
}

string get_item_display_name(object item, string fallback)
{
    string name;

    if (!objectp(item))
        return stringp(fallback) && fallback != "" ? fallback : "物品";

    name = get_object_name(item);
    if (stringp(name) && name != "")
        return name;

    name = item->query("name");
    if (stringp(name) && name != "")
        return name;

    return stringp(fallback) && fallback != "" ? fallback : "物品";
}

string get_item_command_target(object item)
{
    string item_id;

    if (!objectp(item))
        return "";

    item_id = get_object_id(item);
    if (stringp(item_id) && item_id != "")
        return item_id;

    return item->query("name") || "";
}
