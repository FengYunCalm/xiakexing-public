/**
 * @file    room_view_d.c
 * @brief   房间视图投影服务



 *
 * @par Description:
 * 统一负责把房间运行时状态投影为文本 look 与 XK snapshot 共用的房间视图。
 * 当前先收口标题、区域、出口、可见 NPC 和可见物品分类，
 * 避免 look_room 与 room_sync 各自维护第二套房间可见性规则。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

#define ROOM_VIEW_NPC_DISPLAY_LIMIT 12
#define ROOM_VIEW_CHARACTER_CANDIDATE_LIMIT (ROOM_VIEW_NPC_DISPLAY_LIMIT + 1)
#define ROOM_VIEW_SHARED_CACHE_KEY "room_view/shared_cache"
#define ROOM_LIVING_LOOKUP_CACHE_KEY "room_view/living_lookup_cache"

private string query_entity_display_name(object ob)
{
    return call_other("/lib/std/utils/util_player", "get_object_short_name", ob, "");
}

private string query_entity_display_id(object ob)
{
    return call_other("/lib/std/utils/util_player", "get_object_display_id", ob, "");
}

private string query_entity_lookup_name(object ob)
{
    return call_other("/lib/std/utils/util_player", "get_object_display_name", ob, "");
}

private string query_room_item_chest_id(object item)
{
    if (!objectp(item))
        return "";
    return item->query("exploration_chest_id") || item->query("chest_id") || "";
}

private string query_cached_entity_display_name(object target, object *objects,
    string *display_names)
{
    int i;

    if (!objectp(target) || !arrayp(objects) || !arrayp(display_names))
        return "";

    for (i = 0; i < sizeof(objects); i++) {
        if (objects[i] != target)
            continue;
        if (i < sizeof(display_names) && stringp(display_names[i]))
            return display_names[i];
        return "";
    }

    return "";
}

private int is_character_object(object ob)
{
    if (!objectp(ob))
        return 0;

    return living(ob)
        || (function_exists("is_character", ob) && call_other(ob, "is_character"))
        || (function_exists("is_npc", ob) && call_other(ob, "is_npc"))
        || (function_exists("is_player", ob) && call_other(ob, "is_player"));
}

private int is_living_lookup_candidate(object ob)
{
    return objectp(ob)
        && (living(ob)
        || (function_exists("is_character", ob) && call_other(ob, "is_character")));
}

private int should_include_npc_view(object npc)
{
    int hp;
    int max_hp;

    if (!objectp(npc))
        return 0;
    if (npc->query_temp("is_dying"))
        return 0;

    hp = npc->query("combat/hp");
    max_hp = npc->query("combat/max_hp");
    if (intp(max_hp) && max_hp > 0 && intp(hp) && hp <= 0)
        return 0;

    return 1;
}

private mapping build_room_meta_view(object room)
{
    mapping exits;
    string title;
    string zone;
    string instance_kind;
    string dungeon_type;
    string dungeon_gate;

    if (!objectp(room))
        return 0;

    title = function_exists("query_short", room) ? room->query_short() : 0;
    if (!stringp(title) || title == "")
        title = room->query("short") || "未知地点";
    exits = room->query("exits");
    if (!mapp(exits))
        exits = ([ ]);
    zone = room->query("zone") || "";
    /* 实例身份权威标记：dungeon 实例房间由 instance_room_d 设置
     * dungeon/runtime + dungeon/type（world_travel_d 已消费）。
     * 客户端据此区分共享地图与副本实例，不再靠标题/zone 启发式。 */
    instance_kind = "";
    dungeon_type = "";
    if (room->query("dungeon/runtime")) {
        instance_kind = "dungeon";
        dungeon_type = room->query("dungeon/type") || "";
    }
    /* 公共 gate 标记：共享地图房间若等于任一 dungeon 的 public_entry_room，
     * 下发 dungeon_gate（客户端据此显示“进入副本”入口，不再靠标题特例）。 */
    dungeon_gate = "";
    if (instance_kind == "") {
        string *dungeon_types;
        string dungeon_type_id;
        mapping def;

        dungeon_types = DUNGEON_D->query_all_dungeon_types();
        foreach (dungeon_type_id in dungeon_types) {
            def = DUNGEON_D->query_dungeon_definition(dungeon_type_id);
            if (mapp(def) && def["public_entry_room"] == base_name(room)) {
                dungeon_gate = dungeon_type_id;
                break;
            }
        }
    }

    return ([
        "title": title,
        "zone": zone,
        "exits": exits,
        "instance_kind": instance_kind,
        "dungeon_type": dungeon_type,
        "dungeon_gate": dungeon_gate,
    ]);
}

private int query_room_view_content_version(object room)
{
    mixed version;

    if (!objectp(room))
        return 0;

    version = room->query_temp("room_view/content_version");
    return intp(version) && version > 0 ? version : 0;
}

void invalidate_room_view_content(object room)
{
    int version;

    if (!objectp(room))
        return;

    version = query_room_view_content_version(room);
    room->set_temp("room_view/content_version", version + 1);
    room->set_temp("room_view/content_updated_at", time());
    room->delete_temp(ROOM_VIEW_SHARED_CACHE_KEY);
    room->delete_temp(ROOM_LIVING_LOOKUP_CACHE_KEY);
}

/* logout 生命周期 handler 包装（player 签名统一）——失效玩家
 * 所在房间的 room-view 内容。 */
mapping handle_player_logout(object player)
{
    object room;

    if (!objectp(player))
        return ([ "ok": 0, "error": "player unavailable" ]);
    room = environment(player);
    if (objectp(room))
        invalidate_room_view_content(room);
    return ([ "ok": 1, "state": "room_view_invalidated" ]);
}

private void add_living_lookup_token(mapping token_set, string token)
{
    if (!mapp(token_set) || !stringp(token) || token == "")
        return;
    token_set[token] = 1;
}

private string *query_living_lookup_tokens(object candidate)
{
    mapping token_set;
    mixed raw_ids;
    mixed raw_id;

    token_set = ([ ]);
    if (!objectp(candidate))
        return ({ });

    add_living_lookup_token(token_set, query_entity_display_id(candidate));
    add_living_lookup_token(token_set, query_entity_lookup_name(candidate));
    raw_ids = function_exists("query_id", candidate)
        ? candidate->query_id() : get_object_id(candidate);
    if (stringp(raw_ids))
        add_living_lookup_token(token_set, raw_ids);
    else if (arrayp(raw_ids)) {
        foreach (raw_id in raw_ids) {
            if (stringp(raw_id))
                add_living_lookup_token(token_set, raw_id);
        }
    }

    return keys(token_set);
}

private void add_living_lookup_candidate(mapping by_token, string token,
    object candidate)
{
    object *candidates;

    if (!mapp(by_token) || !stringp(token) || token == "" || !objectp(candidate))
        return;

    candidates = arrayp(by_token[token]) ? by_token[token] : ({ });
    by_token[token] = candidates + ({ candidate });
}

private mapping build_room_living_lookup(object room)
{
    mapping by_token;
    object *all_obs;
    object candidate;
    string token;

    if (!objectp(room))
        return 0;

    by_token = ([ ]);
    all_obs = all_inventory(room);
    foreach (candidate in all_obs) {
        if (!is_living_lookup_candidate(candidate))
            continue;
        foreach (token in query_living_lookup_tokens(candidate))
            add_living_lookup_candidate(by_token, token, candidate);
    }

    return ([
        "content_version": query_room_view_content_version(room),
        "by_token": by_token,
    ]);
}

private mapping query_room_living_lookup(object room)
{
    mapping entry;

    if (!objectp(room))
        return 0;

    entry = room->query_temp(ROOM_LIVING_LOOKUP_CACHE_KEY);
    if (!mapp(entry)
        || (entry["content_version"] || 0) != query_room_view_content_version(room)
        || !mapp(entry["by_token"])) {
        entry = build_room_living_lookup(room);
        if (mapp(entry))
            room->set_temp(ROOM_LIVING_LOOKUP_CACHE_KEY, entry);
    }
    return entry;
}

private int living_token_matches(object candidate, string token)
{
    if (!objectp(candidate) || !stringp(token) || token == "")
        return 0;
    if (query_entity_display_id(candidate) == token
        || query_entity_lookup_name(candidate) == token)
        return 1;
    return function_exists("id", candidate) && candidate->id(token);
}

private int visible_living_candidate_matches(object room, string token,
    object viewer, object candidate)
{
    if (!objectp(candidate) || environment(candidate) != room
        || candidate == viewer || !is_living_lookup_candidate(candidate))
        return 0;
    if (objectp(viewer)
        && QUEST_ACTOR_D->should_hide_shared_npc(viewer, candidate, room))
        return 0;
    return living_token_matches(candidate, token);
}

object query_indexed_visible_living(object room, string token, object viewer)
{
    mapping entry;
    object *candidates;
    object candidate;
    int stale;

    if (!objectp(room) || !stringp(token) || token == "")
        return 0;

    candidate = present(token, room);
    if (visible_living_candidate_matches(room, token, viewer, candidate))
        return candidate;

    entry = query_room_living_lookup(room);
    candidates = mapp(entry) && mapp(entry["by_token"])
        && arrayp(entry["by_token"][token])
        ? entry["by_token"][token]
        : ({ });
    foreach (candidate in candidates) {
        if (!objectp(candidate) || environment(candidate) != room) {
            stale = 1;
            continue;
        }
        if (visible_living_candidate_matches(room, token, viewer, candidate))
            return candidate;
    }

    if (stale)
        room->delete_temp(ROOM_LIVING_LOOKUP_CACHE_KEY);
    return 0;
}

private string query_room_exits_signature(mapping exits)
{
    string dir;
    string *dirs;
    string *parts;
    int size;

    if (!mapp(exits) || sizeof(exits) == 0)
        return "";

    dirs = sort_array(keys(exits), 1);
    size = sizeof(dirs);
    parts = allocate(size);
    for (int i = 0; i < size; i++) {
        dir = dirs[i];
        parts[i] = dir + "=" + (stringp(exits[dir]) ? exits[dir] : "");
    }
    return implode(parts, "|");
}

private mapping copy_shared_room_view(mapping entry)
{
    return ([
        "content_version": entry["content_version"] || 0,
        "title": entry["title"] || "",
        "zone": entry["zone"] || "",
        "exits": mapp(entry["exits"]) ? copy(entry["exits"]) : ([ ]),
        "exits_signature": entry["exits_signature"] || "",
        "character_total": entry["character_total"] || 0,
        "display_characters": arrayp(entry["display_characters"])
            ? entry["display_characters"] + ({ })
            : ({ }),
        "display_character_names": arrayp(entry["display_character_names"])
            ? entry["display_character_names"] + ({ })
            : ({ }),
        "items": arrayp(entry["items"]) ? entry["items"] + ({ }) : ({ }),
        "item_display_names": arrayp(entry["item_display_names"])
            ? entry["item_display_names"] + ({ })
            : ({ }),
        "item_chest_ids": arrayp(entry["item_chest_ids"])
            ? entry["item_chest_ids"] + ({ })
            : ({ }),
    ]);
}

private mapping sanitize_shared_room_view(mapping shared_view)
{
    object *display_characters;
    object *item_buffer;
    object *items;
    string *display_character_names;
    string *item_chest_ids_buffer;
    string *item_display_buffer;
    string *source_item_chest_ids;
    string *source_item_display_names;
    int item_count;
    int size;

    if (!mapp(shared_view))
        return 0;

    display_characters = arrayp(shared_view["display_characters"])
        ? shared_view["display_characters"]
        : ({ });
    display_character_names = arrayp(shared_view["display_character_names"])
        ? shared_view["display_character_names"]
        : ({ });
    size = sizeof(display_characters);
    for (int i = 0; i < size; i++) {
        if (!objectp(display_characters[i]) || !should_include_npc_view(display_characters[i]))
            return 0;
    }

    items = arrayp(shared_view["items"]) ? shared_view["items"] : ({ });
    source_item_display_names = arrayp(shared_view["item_display_names"])
        ? shared_view["item_display_names"]
        : ({ });
    source_item_chest_ids = arrayp(shared_view["item_chest_ids"])
        ? shared_view["item_chest_ids"]
        : ({ });
    item_buffer = allocate(sizeof(items));
    item_display_buffer = allocate(sizeof(items));
    item_chest_ids_buffer = allocate(sizeof(items));
    item_count = 0;
    size = sizeof(items);
    for (int i = 0; i < size; i++) {
        if (!objectp(items[i]))
            continue;
        item_buffer[item_count] = items[i];
        item_display_buffer[item_count] = i < sizeof(source_item_display_names)
            ? source_item_display_names[i]
            : "";
        item_chest_ids_buffer[item_count] = i < sizeof(source_item_chest_ids)
            ? source_item_chest_ids[i]
            : "";
        item_count++;
    }

    return ([
        "content_version": shared_view["content_version"] || 0,
        "title": shared_view["title"] || "",
        "zone": shared_view["zone"] || "",
        "exits": mapp(shared_view["exits"]) ? copy(shared_view["exits"]) : ([ ]),
        "exits_signature": shared_view["exits_signature"] || "",
        "character_total": shared_view["character_total"] || 0,
        "display_characters": display_characters + ({ }),
        "display_character_names": display_character_names + ({ }),
        "items": item_count > 0 ? item_buffer[0..item_count - 1] : ({ }),
        "item_display_names": item_count > 0
            ? item_display_buffer[0..item_count - 1]
            : ({ }),
        "item_chest_ids": item_count > 0
            ? item_chest_ids_buffer[0..item_count - 1]
            : ({ }),
    ]);
}

private void cache_shared_room_view(object room, mapping shared_view)
{
    if (!objectp(room) || !mapp(shared_view))
        return;

    room->set_temp(ROOM_VIEW_SHARED_CACHE_KEY, copy_shared_room_view(shared_view));
}

private mapping query_cached_shared_room_view(object room, mapping meta)
{
    mapping entry;
    mapping exits;
    string title;
    string zone;
    string exits_signature;

    if (!objectp(room) || !mapp(meta))
        return 0;

    entry = room->query_temp(ROOM_VIEW_SHARED_CACHE_KEY);
    if (!mapp(entry))
        return 0;

    title = meta["title"] || "";
    zone = meta["zone"] || "";
    exits = mapp(meta["exits"]) ? meta["exits"] : ([ ]);
    exits_signature = query_room_exits_signature(exits);
    if ((entry["content_version"] || 0) != query_room_view_content_version(room))
        return 0;
    if ((entry["title"] || "") != title || (entry["zone"] || "") != zone)
        return 0;
    if ((entry["exits_signature"] || "") != exits_signature)
        return 0;

    return sanitize_shared_room_view(entry);
}

private mapping build_shared_room_view(object room, mapping meta)
{
    object *all_obs;
    object *display_character_buffer;
    object *item_buffer;
    string *display_character_name_buffer;
    string *item_chest_ids_buffer;
    string *item_display_buffer;
    int character_total;
    int display_character_count;
    int item_count;

    all_obs = all_inventory(room);
    display_character_buffer = allocate(ROOM_VIEW_CHARACTER_CANDIDATE_LIMIT);
    item_buffer = allocate(sizeof(all_obs));
    display_character_name_buffer = allocate(ROOM_VIEW_CHARACTER_CANDIDATE_LIMIT);
    item_display_buffer = allocate(sizeof(all_obs));
    item_chest_ids_buffer = allocate(sizeof(all_obs));
    character_total = 0;
    display_character_count = 0;
    item_count = 0;

    foreach (object ob in all_obs) {
        if (!objectp(ob))
            continue;

        if (is_character_object(ob)) {
            if (!should_include_npc_view(ob))
                continue;
            character_total++;
            if (display_character_count < ROOM_VIEW_CHARACTER_CANDIDATE_LIMIT) {
                display_character_buffer[display_character_count] = ob;
                display_character_name_buffer[display_character_count] =
                    query_entity_display_name(ob);
                display_character_count++;
            }
        } else {
            item_buffer[item_count] = ob;
            item_display_buffer[item_count] = query_entity_display_name(ob);
            item_chest_ids_buffer[item_count] = query_room_item_chest_id(ob);
            item_count++;
        }
    }

    return ([
        "content_version": query_room_view_content_version(room),
        "title": meta["title"] || "",
        "zone": meta["zone"] || "",
        "exits": mapp(meta["exits"]) ? meta["exits"] : ([ ]),
        "exits_signature": query_room_exits_signature(meta["exits"]),
        "character_total": character_total,
        "display_characters": display_character_count > 0
            ? display_character_buffer[0..display_character_count - 1]
            : ({ }),
        "display_character_names": display_character_count > 0
            ? display_character_name_buffer[0..display_character_count - 1]
            : ({ }),
        "items": item_count > 0 ? item_buffer[0..item_count - 1] : ({ }),
        "item_display_names": item_count > 0
            ? item_display_buffer[0..item_count - 1]
            : ({ }),
        "item_chest_ids": item_count > 0
            ? item_chest_ids_buffer[0..item_count - 1]
            : ({ }),
    ]);
}

mapping query_room_meta_view(object player, object room)
{
    if (!objectp(room))
        room = objectp(player) ? environment(player) : 0;
    return build_room_meta_view(room);
}

private mapping build_room_view(object player, object room, mapping timing_ns)
{
    object *items;
    object *display_characters;
    string *item_chest_ids;
    string *item_display_names;
    string *npc_display_names;
    string *display_character_names;
    string ensure_source;
    string ensure_timing_source;
    mapping ensure_timing;
    mapping meta;
    mapping result;
    mapping shared_view;
    mapping exits;
    mixed *npcs;
    mixed npc_entry;
    string title;
    string zone;
    int character_total;
    int npc_total;
    int npc_truncated;
    int phase_finished_at;
    int phase_started_at;
    int profile_timing;
    int room_view_started_at;
    int custom_ensure_started_at;

    if (!objectp(room))
        room = objectp(player) ? environment(player) : 0;
    if (!objectp(room))
        return 0;

    profile_timing = mapp(timing_ns);
    if (profile_timing) {
        room_view_started_at = perf_counter_ns();
        phase_started_at = room_view_started_at;
    }

    if (profile_timing) {
        ensure_timing = ([ ]);
        ensure_source = function_exists("ensure_room_entities", room);
        ensure_timing_source = function_exists("ensure_room_entities_with_timing", room);
        if (stringp(ensure_timing_source) && ensure_timing_source != ""
            && ensure_timing_source == ensure_source)
            room->ensure_room_entities_with_timing(ensure_timing);
        else if (stringp(ensure_source) && ensure_source != "") {
            custom_ensure_started_at = perf_counter_ns();
            room->ensure_room_entities();
            ensure_timing["owner_route"] = 0;
            ensure_timing["gate"] = 0;
            ensure_timing["entities"] = perf_counter_ns() - custom_ensure_started_at;
        }
        else
            WORLD_ENTITY_LIFECYCLE_D->ensure_room_entities_with_timing(room, ensure_timing);
        timing_ns["ensure_owner_route"] = intp(ensure_timing["owner_route"])
            ? ensure_timing["owner_route"] : 0;
        timing_ns["ensure_gate"] = intp(ensure_timing["gate"])
            ? ensure_timing["gate"] : 0;
        timing_ns["ensure_entities"] = intp(ensure_timing["entities"])
            ? ensure_timing["entities"] : 0;
    } else if (function_exists("ensure_room_entities", room))
        room->ensure_room_entities();
    else
        WORLD_ENTITY_LIFECYCLE_D->ensure_room_entities(room);
    if (profile_timing) {
        phase_finished_at = perf_counter_ns();
        timing_ns["ensure"] = phase_finished_at - phase_started_at;
        phase_started_at = phase_finished_at;
    }

    meta = build_room_meta_view(room);
    title = meta["title"];
    zone = meta["zone"];
    exits = meta["exits"];

    shared_view = query_cached_shared_room_view(room, meta);
    if (!mapp(shared_view)) {
        shared_view = sanitize_shared_room_view(build_shared_room_view(room, meta));
        cache_shared_room_view(room, shared_view);
    }

    character_total = intp(shared_view["character_total"])
        ? shared_view["character_total"] : 0;
    display_characters = arrayp(shared_view["display_characters"])
        ? shared_view["display_characters"]
        : ({ });
    display_character_names = arrayp(shared_view["display_character_names"])
        ? shared_view["display_character_names"]
        : ({ });
    items = arrayp(shared_view["items"]) ? shared_view["items"] : ({ });
    item_display_names = arrayp(shared_view["item_display_names"])
        ? shared_view["item_display_names"]
        : ({ });
    item_chest_ids = arrayp(shared_view["item_chest_ids"])
        ? shared_view["item_chest_ids"]
        : ({ });
    if (profile_timing) {
        phase_finished_at = perf_counter_ns();
        timing_ns["shared"] = phase_finished_at - phase_started_at;
        phase_started_at = phase_finished_at;
    }

    {
        object *character_buffer;
        int candidate_count;

        character_buffer = allocate(ROOM_VIEW_NPC_DISPLAY_LIMIT);
        candidate_count = 0;
        for (int i = 0; i < sizeof(display_characters); i++) {
            if (!objectp(display_characters[i]) || display_characters[i] == player)
                continue;
            character_buffer[candidate_count] = display_characters[i];
            candidate_count++;
            if (candidate_count >= ROOM_VIEW_NPC_DISPLAY_LIMIT)
                break;
        }
        npcs = candidate_count > 0
            ? character_buffer[0..candidate_count - 1]
            : ({ });
    }
    npc_total = character_total;
    if (objectp(player) && environment(player) == room
        && should_include_npc_view(player) && npc_total > 0)
        npc_total--;
    if (profile_timing) {
        phase_finished_at = perf_counter_ns();
        timing_ns["player_filter"] = phase_finished_at - phase_started_at;
        phase_started_at = phase_finished_at;
    }

    if (objectp(player))
        npcs = QUEST_ACTOR_D->query_room_npc_view(player, room, npcs);
    if (profile_timing) {
        phase_finished_at = perf_counter_ns();
        timing_ns["quest_projection"] = phase_finished_at - phase_started_at;
        phase_started_at = phase_finished_at;
    }


    npc_truncated = npc_total > ROOM_VIEW_NPC_DISPLAY_LIMIT
        ? npc_total - ROOM_VIEW_NPC_DISPLAY_LIMIT
        : 0;
    if (sizeof(npcs) > ROOM_VIEW_NPC_DISPLAY_LIMIT) {
        npc_truncated += sizeof(npcs) - ROOM_VIEW_NPC_DISPLAY_LIMIT;
        npcs = npcs[0..ROOM_VIEW_NPC_DISPLAY_LIMIT - 1];
    }

    npc_display_names = allocate(sizeof(npcs));
    for (int i = 0; i < sizeof(npcs); i++) {
        npc_entry = npcs[i];
        if (objectp(npc_entry)) {
            npc_display_names[i] = query_cached_entity_display_name(
                npc_entry, display_characters, display_character_names);
            if (npc_display_names[i] == "")
                npc_display_names[i] = query_entity_display_name(npc_entry);
        } else if (mapp(npc_entry))
            npc_display_names[i] = npc_entry["name"] || npc_entry["id"] || "";
        else
            npc_display_names[i] = "";
    }

    result = ([
        "title": title,
        "zone": zone,
        "exits": exits,
        "instance_kind": meta["instance_kind"] || "",
        "dungeon_type": meta["dungeon_type"] || "",
        "dungeon_gate": meta["dungeon_gate"] || "",
        "npcs": npcs,
        "npc_display_names": npc_display_names,
        "npcs_total": npc_total,
        "npcs_truncated": npc_truncated,
        "npcs_display_limit": ROOM_VIEW_NPC_DISPLAY_LIMIT,
        "items": items,
        "item_display_names": item_display_names,
        "item_chest_ids": item_chest_ids,
    ]);
    if (profile_timing) {
        phase_finished_at = perf_counter_ns();
        timing_ns["finalize"] = phase_finished_at - phase_started_at;
        timing_ns["total"] = phase_finished_at - room_view_started_at;
    }
    return result;
}

mapping query_room_view(object player, object room)
{
    return build_room_view(player, room, 0);
}

mapping query_room_view_with_timing(object player, object room, mapping timing_ns)
{
    return build_room_view(player, room, timing_ns);
}
