/**
 * @file    alchemy_recipe_registry_d.c
 * @brief   炼药 scope 配方注册表 facade
 *
 * @note
 * 当前炼药通过药师 provider scope 读取固定配方；真正的 recipe 真源
 * 已回收到通用 crafting registry，本文件只负责稳定路径转发。
 */

#pragma strict_types

#include <system/services.h>

inherit "/lib/framework/daemon";

mapping query_recipe(string recipe_id)
{
    return CRAFTING_RECIPE_REGISTRY_D->query_recipe("pharmacist", recipe_id);
}
