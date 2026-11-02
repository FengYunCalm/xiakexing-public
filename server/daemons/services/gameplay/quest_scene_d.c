/**
 * @file    quest_scene_d.c
 * @brief   任务场景交互渲染门面,转发 NPC_VIEW_D 的 provider interaction 展示。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

string render_provider_interaction(object player, string provider_id, string default_text)
{
    return NPC_VIEW_D->render_provider_interaction(player, provider_id, "", default_text);
}
