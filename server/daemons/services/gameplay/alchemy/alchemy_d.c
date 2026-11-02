/**
 * @file    alchemy_d.c
 * @brief   炼药服务真源骨架
 *
 * @note
 * 药师场景下的固定 recipe 制作统一委托给 `CRAFTING_RUNTIME_D`。
 * 炼药领域自己仍负责 recipe 真源、药师 scope 语义和玩家文案。
 */

#pragma strict_types

#include <system/services.h>

inherit "/lib/framework/daemon";

private mapping query_alchemy_craft_options()
{
    return ([
        "action": "alchemy",
        "usage_message": "用法：alchemy craft <recipe_id>\n",
        "unknown_recipe_message": "孙大夫这里没有这副固定药方。\n",
        "invalid_scope_error": "invalid_pharmacist_scope",
        "invalid_scope_message": "这里只有孙大夫能替你配这副方子。\n",
        "invalid_materials_message": "这个药方的材料定义有问题，暂时不能使用。\n",
        "missing_materials_message": "你的药材还不齐。\n",
        "consume_missing_materials_message": "你的药材不够，没法继续配制。\n",
        "money_action_verb": "配制",
        "default_recipe_name": "这副药",
        "create_item_failed_message": "孙大夫摇了摇头，这副药没能配出来。\n",
        "move_failed_message": "你的背包放不下成药，这次配制被中止了。\n",
        "success_actor_name": "孙大夫",
        "success_action_verb": "配好了",
    ]);
}

mapping craft(object player, object pharmacist, string recipe_id)
{
    return CRAFTING_RUNTIME_D->execute_recipe_craft(player,
        pharmacist,
        recipe_id,
        ALCHEMY_RECIPE_REGISTRY_D->query_recipe(recipe_id),
        query_alchemy_craft_options());
}
