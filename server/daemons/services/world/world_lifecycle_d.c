/**
 * @file    world_lifecycle_d.c
 * @brief   世界生命周期兼容 facade；状态分别归实体、投影和观察者 owner。
 *
 * 新代码应直接依赖对应 owner：
 * WORLD_ENTITY_LIFECYCLE_D、WORLD_PROJECTION_D、WORLD_OBSERVER_D。
 * 本 facade 只保留旧调用方所需的稳定方法，不持有任何业务 mapping。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

mapping query_world_room_owner_route(object room)
{
    return WORLD_ENTITY_LIFECYCLE_D->query_world_room_owner_route(room);
}

mapping stamp_world_room_owner_route(object room, string reason)
{
    return WORLD_ENTITY_LIFECYCLE_D->stamp_world_room_owner_route(room, reason);
}

object query_live_entity_object(string index_key)
{
    return WORLD_ENTITY_LIFECYCLE_D->query_live_entity_object(index_key);
}

string query_world_runtime_epoch()
{
    return WORLD_ENTITY_LIFECYCLE_D->query_world_runtime_epoch();
}

int bind_entity_lease(string token, object ob, mapping identity)
{
    return WORLD_ENTITY_LIFECYCLE_D->bind_entity_lease(token, ob, identity);
}

int query_slot_leased(object room, string entity_id)
{
    return WORLD_ENTITY_LIFECYCLE_D->query_slot_leased(room, entity_id);
}

object query_leased_entity_object(string token, string fallback_key)
{
    return WORLD_ENTITY_LIFECYCLE_D->query_leased_entity_object(token, fallback_key);
}

int unbind_entity_lease(string token, object expected)
{
    return WORLD_ENTITY_LIFECYCLE_D->unbind_entity_lease(token, expected);
}

int discard_entity_lease(string token, string slot_key)
{
    return WORLD_ENTITY_LIFECYCLE_D->discard_entity_lease(token, slot_key);
}

string query_entity_index_key(object room, string entity_id)
{
    return WORLD_ENTITY_LIFECYCLE_D->query_entity_index_key(room, entity_id);
}

int reindex_content_entity(object ob, object old_room, object new_room)
{
    return WORLD_ENTITY_LIFECYCLE_D->reindex_content_entity(ob, old_room, new_room);
}

int ensure_room_entities(object room)
{
    return WORLD_ENTITY_LIFECYCLE_D->ensure_room_entities(room);
}

int ensure_room_entities_with_timing(object room, mapping timing_ns)
{
    return WORLD_ENTITY_LIFECYCLE_D->ensure_room_entities_with_timing(room, timing_ns);
}

void ensure_room_baseline(object room)
{
    WORLD_ENTITY_LIFECYCLE_D->ensure_room_baseline(room);
}

void clear_respawn_cooldown(object ob)
{
    WORLD_ENTITY_LIFECYCLE_D->clear_respawn_cooldown(ob);
}

mapping query_projection_registry_snapshot()
{
    return WORLD_PROJECTION_D->query_projection_registry_snapshot();
}

mapping query_player_projection_snapshot(object player, object room)
{
    return WORLD_PROJECTION_D->query_player_projection_snapshot(player, room);
}

mapping *query_room_actor_payloads(object player, object room)
{
    return WORLD_PROJECTION_D->query_room_actor_payloads(player, room);
}

int should_hide_shared_npc(object player, object npc, object room)
{
    return WORLD_PROJECTION_D->should_hide_shared_npc(player, npc, room);
}

mixed *query_room_npc_view(object player, object room, object *shared_npcs)
{
    return WORLD_PROJECTION_D->query_room_npc_view(player, room, shared_npcs);
}

object resolve_actor_target(object player, object room, string arg)
{
    return WORLD_PROJECTION_D->resolve_actor_target(player, room, arg);
}

int refresh_player_projection_state(object player, object room)
{
    return WORLD_OBSERVER_D->refresh_player_projection_state(player, room);
}

int sync_player_room_transition(object player, object old_room, object new_room)
{
    return WORLD_OBSERVER_D->sync_player_room_transition(player, old_room, new_room);
}
