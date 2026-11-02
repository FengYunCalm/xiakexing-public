/**
 * @file    tailoring_d.c
 * @brief   裁缝服务真源骨架
 *
 * @note
 * 裁缝铺场景下的固定 recipe 制作统一委托给 `CRAFTING_RUNTIME_D`。
 * 裁衣领域自己仍负责 recipe 真源、裁缝 scope 语义和玩家文案。
 */

#pragma strict_types

#include <system/services.h>

inherit "/lib/framework/daemon";

private mapping query_tailor_craft_options()
{
    return ([
        "action": "tailor",
        "usage_message": "用法：tailor craft <recipe_id>\n",
        "unknown_recipe_message": "苏裁缝这里没有这件现成衣样。\n",
        "invalid_scope_error": "invalid_tailor_scope",
        "invalid_scope_message": "这里只有苏裁缝能替你裁这件衣裳。\n",
        "invalid_materials_message": "这个裁衣配方的材料定义有问题，暂时不能使用。\n",
        "missing_materials_message": "你的布料还不齐。\n",
        "consume_missing_materials_message": "你的布料不够，没法继续裁制。\n",
        "money_action_verb": "裁制",
        "default_recipe_name": "这件衣裳",
        "create_item_failed_message": "苏裁缝摇了摇头，这件衣裳没能裁出来。\n",
        "move_failed_message": "你的背包放不下成衣，这次裁制被中止了。\n",
        "success_actor_name": "苏裁缝",
        "success_action_verb": "裁好了",
    ]);
}

mapping craft(object player, object tailor, string recipe_id)
{
    return CRAFTING_RUNTIME_D->execute_recipe_craft(player,
        tailor,
        recipe_id,
        TAILOR_RECIPE_REGISTRY_D->query_recipe(recipe_id),
        query_tailor_craft_options());
}
