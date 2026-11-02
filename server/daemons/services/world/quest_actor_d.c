/**
 * @file    quest_actor_d.c
 * @brief   任务演员稳定身份与玩家敏感视图 owner。
 *
 * 内容实体的身份由 room + content_entity_id 组成，解析只通过
 * WORLD_ENTITY_LIFECYCLE_D 的反向索引；WORLD_PROJECTION_D 仅保留通用隐藏规则
 * 与旧的技术投影 actor 兼容，不再承担任务演员的内容解析。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

private string query_actor_entity_id(mapping entity)
{
    string entity_id;

    if (!mapp(entity))
        return "";
    entity_id = entity["id"] || entity["present_id"]
        || entity["template_id"] || entity["path"] || "";
    return stringp(entity_id) ? entity_id : "";
}

private int is_actor_entity(mapping entity)
{
    string content_type;

    if (!mapp(entity))
        return 0;
    content_type = entity["content_type"] || entity["type"] || "";
    return content_type == "npc" || content_type == "actor";
}

private mapping query_actor_template(object player, mapping entity)
{
    string template_id;
    mapping template;

    template_id = mapp(entity) && stringp(entity["template_id"])
        ? entity["template_id"] : "";
    if (template_id == "")
        return ([ ]);
    template = CONTENT_REGISTRY_D->query_npc_template_for_actor(player, template_id);
    return mapp(template) ? template : ([ ]);
}

private string *append_actor_alias(string *aliases, string alias)
{
    if (!stringp(alias) || alias == "" || member_array(alias, aliases) != -1)
        return aliases;
    return aliases + ({ alias });
}

private string *query_actor_aliases(mapping entity, mapping template)
{
    string *aliases;
    mixed *values;
    string value;

    aliases = ({ });
    if (!mapp(entity))
        return aliases;
    aliases = append_actor_alias(aliases, entity["id"]);
    aliases = append_actor_alias(aliases, entity["present_id"]);
    aliases = append_actor_alias(aliases, entity["provider_id"]);
    aliases = append_actor_alias(aliases, entity["template_id"]);
    values = arrayp(entity["ids"]) ? entity["ids"] : ({ });
    foreach (value in values)
        aliases = append_actor_alias(aliases, value);
    if (mapp(template)) {
        aliases = append_actor_alias(aliases, template["id"]);
        aliases = append_actor_alias(aliases, template["provider_id"]);
        values = arrayp(template["ids"]) ? template["ids"] : ({ });
        foreach (value in values)
            aliases = append_actor_alias(aliases, value);
    }
    return aliases;
}

private object query_live_actor(object room, mapping entity)
{
    string entity_id;
    string index_key;

    if (!objectp(room) || !mapp(entity))
        return 0;
    entity_id = query_actor_entity_id(entity);
    if (entity_id == "")
        return 0;
    index_key = WORLD_ENTITY_LIFECYCLE_D->query_entity_index_key(room, entity_id);
    if (index_key == "")
        return 0;
    return WORLD_ENTITY_LIFECYCLE_D->query_live_entity_object(index_key);
}

private int actor_alias_matches(mapping entity, mapping template, string arg)
{
    return stringp(arg) && arg != ""
        && member_array(arg, query_actor_aliases(entity, template)) != -1;
}

private mapping build_content_actor_payload(object player, object room,
    mapping entity)
{
    mapping template;
    mapping payload;
    mapping live_payload;
    object target;
    string entity_id;
    string present_id;
    string name;

    if (!objectp(player) || !objectp(room) || !is_actor_entity(entity))
        return 0;
    entity_id = query_actor_entity_id(entity);
    if (entity_id == "")
        return 0;
    template = query_actor_template(player, entity);
    present_id = stringp(entity["present_id"]) && entity["present_id"] != ""
        ? entity["present_id"] : entity_id;
    name = stringp(entity["name"]) && entity["name"] != ""
        ? entity["name"]
        : (stringp(template["name"]) && template["name"] != ""
            ? template["name"] : present_id);
    payload = ([
        "id": present_id,
        "projection_id": "content:" + base_name(room) + ":" + entity_id,
        "content_entity_id": entity_id,
        "target_present_id": present_id,
        "room": base_name(room),
        "name": name,
        "ids": query_actor_aliases(entity, template),
        "type": "npc",
        "type_label": entity["type_label"] || template["type_label"] || "人物",
        "summary": entity["summary"] || template["summary"] || name + "正在此处等候。",
        "description": entity["description"] || template["description"] || name,
        "content_generation": CONTENT_REGISTRY_D->query_content_generation(),
    ]);

    target = query_live_actor(room, entity);
    if (!objectp(target))
        return payload;
    live_payload = CLIENT_PROJECTION_D->build_room_npc_payload(target, player);
    if (!mapp(live_payload))
        return payload;
    if (arrayp(live_payload["action_groups"]))
        payload["action_groups"] = live_payload["action_groups"];
    if (live_payload["quest_available"])
        payload["quest_available"] = 1;
    if (arrayp(live_payload["available_quest_ids"]))
        payload["available_quest_ids"] = live_payload["available_quest_ids"];
    return payload;
}

private mapping *query_content_actor_payloads(object player, object room)
{
    mapping *entities;
    mapping *result;
    mapping entity;
    mapping payload;

    result = ({ });
    if (!objectp(player) || !objectp(room))
        return result;
    entities = CONTENT_REGISTRY_D->query_room_entities_for_actor(player,
        base_name(room));
    foreach (entity in entities) {
        if (!is_actor_entity(entity))
            continue;
        payload = build_content_actor_payload(player, room, entity);
        if (mapp(payload))
            result += ({ payload });
    }
    return result;
}

private int view_contains_object(mixed *view, object target)
{
    mixed entry;

    if (!arrayp(view) || !objectp(target))
        return 0;
    foreach (entry in view)
        if (entry == target)
            return 1;
    return 0;
}

mapping *query_room_actor_payloads(object player, object room)
{
    mapping *result;
    mapping *technical_payloads;

    result = query_content_actor_payloads(player, room);
    technical_payloads = WORLD_PROJECTION_D->query_room_actor_payloads(player, room);
    if (arrayp(technical_payloads))
        result += technical_payloads;
    return result;
}

int should_hide_shared_npc(object player, object npc, object room)
{
    return WORLD_PROJECTION_D->should_hide_shared_npc(player, npc, room);
}

mixed *query_room_npc_view(object player, object room, object *shared_npcs)
{
    mixed *view;
    mapping *actors;
    mapping actor;
    object target;

    view = WORLD_PROJECTION_D->query_room_npc_view(player, room, shared_npcs);
    actors = query_content_actor_payloads(player, room);
    foreach (actor in actors) {
        target = query_live_actor(room, ([
            "id": actor["content_entity_id"],
            "present_id": actor["target_present_id"],
        ]));
        if (objectp(target)) {
            if (should_hide_shared_npc(player, target, room))
                continue;
            if (!view_contains_object(view, target))
                view += ({ actor });
            continue;
        }
        view += ({ actor });
    }
    return view;
}

object resolve_actor_target(object player, object room, string arg)
{
    mapping *entities;
    mapping entity;
    mapping template;
    object target;

    if (!objectp(player) || !objectp(room) || !stringp(arg) || arg == "")
        return 0;
    entities = CONTENT_REGISTRY_D->query_room_entities_for_actor(player,
        base_name(room));
    foreach (entity in entities) {
        if (!is_actor_entity(entity))
            continue;
        template = query_actor_template(player, entity);
        if (!actor_alias_matches(entity, template, arg))
            continue;
        target = query_live_actor(room, entity);
        if (objectp(target) && !should_hide_shared_npc(player, target, room))
            return target;
    }
    return WORLD_PROJECTION_D->resolve_actor_target(player, room, arg);
}
