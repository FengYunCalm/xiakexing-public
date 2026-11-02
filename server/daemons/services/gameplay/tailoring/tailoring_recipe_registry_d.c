/**
 * @file    tailoring_recipe_registry_d.c
 * @brief   裁缝 scope 配方注册表 facade
 */

#pragma strict_types

#include <system/services.h>

inherit "/lib/framework/daemon";

mapping query_recipe(string recipe_id)
{
    return CRAFTING_RECIPE_REGISTRY_D->query_recipe("tailor", recipe_id);
}
