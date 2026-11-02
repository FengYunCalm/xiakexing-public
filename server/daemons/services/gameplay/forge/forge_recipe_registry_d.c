/**
 * @file    forge_recipe_registry_d.c
 * @brief   锻造 scope 配方注册表 facade
 *
 * @note
 * 当前锻造仍通过这个稳定路径读配方，但真正的 recipe/enhance rule 真源
 * 已回收到通用 crafting registry；本文件只负责 blacksmith scope 转发。
 */

#pragma strict_types

#include <system/services.h>

inherit "/lib/framework/daemon";

mapping query_recipe(string recipe_id)
{
    return CRAFTING_RECIPE_REGISTRY_D->query_recipe("blacksmith", recipe_id);
}

mapping query_enhance_rule(string item_type)
{
    return CRAFTING_RECIPE_REGISTRY_D->query_enhance_rule("blacksmith", item_type);
}
