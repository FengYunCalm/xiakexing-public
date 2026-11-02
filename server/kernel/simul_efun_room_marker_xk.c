/**
 * @file    simul_efun_room_marker_xk.c
 * @brief   房间 quest marker 共享子系统（自 simul_efun_room_meta_xk.c 拆分；
 *          exits 与 mapgrid 共同消费）
 */

#pragma strict_types

#include <system/room_meta_helpers.h>
#include <system/services.h>
#include <system/xk_protocol.h>


inherit "/lib/std/utils/util_owner_projection";

// ---- 前向声明 ----
public varargs string *query_room_marker_quest_ids(object player, string room_path,
    mapping quest_ids_by_provider, mapping room_provider_ids);
public varargs string *query_provider_marker_quest_ids(object player, string provider_id,
    mapping quest_ids_by_provider);
public void merge_quest_marker_detail(mapping quest_marker_details, string key,
    string entry_room_id, string target_room_id, string entry_direction,
    string *quest_ids, string label);
public void record_quest_marker(mapping quest_markers, mapping quest_marker_details,
    string key, string entry_room_id, string target_room_id, string entry_direction,
    string *quest_ids, string label);
public mixed *quest_marker_details_array(mapping quest_marker_details);
public varargs mapping query_indoor_entry_quest_marker(object player, string entry_room_path,
    string start_room_path, mapping quest_ids_by_provider);
public varargs mapping query_room_indoor_entry_quest_marker(object player,
    string entry_room_path, mapping quest_ids_by_provider);
public mapping query_indoor_entry_quest_marker_from_topology(mapping candidate,
    mapping quest_ids_by_provider);
public mapping query_exit_indoor_quest_marker_from_topology(mixed *candidates,
    mapping quest_ids_by_provider);
public string *append_unique_strings(string *base, string *values);
public string query_entity_provider_id(mapping entity);
public int is_indoor_subroom_path(string room_path);
public int should_scan_indoor_entry_exit(string current_room_path, string target_room_path,
    string canonical_dir);
public string indoor_entry_root_of(string room_path);


#define INDOOR_QUEST_SCAN_MAX_STEPS 20
#define INDOOR_QUEST_SCAN_MAX_ROOMS 80

public varargs string *query_room_marker_quest_ids(object player, string room_path,
    mapping quest_ids_by_provider, mapping room_provider_ids)
{
    mapping entity;
    mapping *entities;
    string *marker_ids;
    string *provider_ids;
    string normalized_room_path;
    string provider_id;

    if (!objectp(player) || !stringp(room_path) || room_path == "")
        return ({ });

    normalized_room_path = strlen(room_path) > 2 && room_path[<2..] == ".c"
        ? room_path[0..<3] : room_path;
    provider_ids = mapp(room_provider_ids)
        ? room_provider_ids[normalized_room_path]
        : CONTENT_REGISTRY_D->query_room_quest_provider_ids(room_path);
    if (arrayp(provider_ids) && sizeof(provider_ids) > 0) {
        marker_ids = ({ });
        foreach (provider_id in provider_ids) {
            if (!stringp(provider_id) || provider_id == "")
                continue;
            /* 与 ensure 补刷同真源：当前时辰不在本房间的 provider 不出现在标记中 */
            if (!NPC_SCHEDULE_D->query_provider_present_in_room(
                provider_id, normalized_room_path))
                continue;
            marker_ids = append_unique_strings(marker_ids,
                query_provider_marker_quest_ids(player, provider_id,
                    quest_ids_by_provider));
        }
        return marker_ids;
    }
    if (mapp(room_provider_ids))
        return ({ });

    entities = CONTENT_REGISTRY_D->query_room_entities(room_path);
    marker_ids = ({ });
    foreach (entity in entities) {
        provider_id = query_entity_provider_id(entity);
        if (provider_id == "")
            continue;
        if (!NPC_SCHEDULE_D->query_provider_present_in_room(
            provider_id, normalized_room_path))
            continue;
        marker_ids = append_unique_strings(marker_ids,
            query_provider_marker_quest_ids(player, provider_id,
                quest_ids_by_provider));
    }
    return marker_ids;
}

public varargs string *query_provider_marker_quest_ids(object player, string provider_id,
    mapping quest_ids_by_provider)
{
    string *quest_ids;

    if (!stringp(provider_id) || provider_id == "")
        return ({ });

    if (mapp(quest_ids_by_provider)) {
        quest_ids = quest_ids_by_provider[provider_id];
        return arrayp(quest_ids) ? quest_ids + ({ }) : ({ });
    }
    return CLIENT_PROJECTION_D->query_provider_marker_quest_ids(player, provider_id);
}

public void merge_quest_marker_detail(mapping quest_marker_details, string key,
    string entry_room_id, string target_room_id, string entry_direction,
    string *quest_ids, string label)
{
    mapping detail;
    string *existing_quest_ids;

    if (!mapp(quest_marker_details) || !stringp(key) || key == "")
        return;
    if (!arrayp(quest_ids) || sizeof(quest_ids) == 0)
        return;

    detail = quest_marker_details[key];
    if (!mapp(detail)) {
        detail = ([
            "coord": key,
            "entry_room_id": stringp(entry_room_id) ? entry_room_id : "",
            "target_room_id": stringp(target_room_id) ? target_room_id : "",
            "entry_direction": stringp(entry_direction) ? entry_direction : "",
            "entry_command": stringp(entry_direction) && entry_direction != "" ? sprintf("go %s", entry_direction) : "",
            "label": stringp(label) && label != "" ? label : "任务地点",
            "quest_ids": ({ }),
        ]);
    }

    existing_quest_ids = arrayp(detail["quest_ids"]) ? detail["quest_ids"] : ({ });
    detail["quest_ids"] = append_unique_strings(existing_quest_ids, quest_ids);
    quest_marker_details[key] = detail;
}

public void record_quest_marker(mapping quest_markers, mapping quest_marker_details,
    string key, string entry_room_id, string target_room_id, string entry_direction,
    string *quest_ids, string label)
{
    if (!mapp(quest_markers) || !stringp(key) || key == "")
        return;
    if (!arrayp(quest_ids) || sizeof(quest_ids) == 0)
        return;

    quest_markers[key] = 1;
    merge_quest_marker_detail(quest_marker_details, key, entry_room_id, target_room_id,
        entry_direction, quest_ids, label);
}

public mixed *quest_marker_details_array(mapping quest_marker_details)
{
    string *detail_keys;
    mixed *details;
    int index;

    if (!mapp(quest_marker_details) || sizeof(quest_marker_details) == 0)
        return ({ });

    detail_keys = keys(quest_marker_details);
    details = allocate(sizeof(detail_keys));
    for (index = 0; index < sizeof(detail_keys); index++)
        details[index] = quest_marker_details[detail_keys[index]];

    return details;
}

public varargs mapping query_indoor_entry_quest_marker(object player, string entry_room_path,
    string start_room_path, mapping quest_ids_by_provider)
{
    mapping marker_topology;
    mapping start_room_snapshot;
    mixed *topology_candidates;
    mixed *queue;
    mapping visited;
    string *quest_ids;
    string target_room_id;
    int index;

    if (!objectp(player) || !stringp(start_room_path) || start_room_path == "")
        return ([ "target_room_id": "", "quest_ids": ({ }) ]);

    start_room_snapshot = ROOM_EXITS_XK_HELPER->query_map_room_snapshot(start_room_path, 0, 0);
    marker_topology = mapp(start_room_snapshot)
        && start_room_snapshot["source"] == "content"
        ? CONTENT_REGISTRY_D->query_map_quest_marker_topology(
            ({ start_room_path }), ({ ([
                "entry_room_id": entry_room_path,
                "target_room_id": start_room_path,
            ]) }), INDOOR_QUEST_SCAN_MAX_STEPS, INDOOR_QUEST_SCAN_MAX_ROOMS)
        : 0;
    topology_candidates = mapp(marker_topology)
        && arrayp(marker_topology["indoor_entry_candidates"])
        ? marker_topology["indoor_entry_candidates"] : ({ });
    if (sizeof(topology_candidates) > 0 && mapp(topology_candidates[0])
        && arrayp(topology_candidates[0]["room_candidates"])) {
        return query_indoor_entry_quest_marker_from_topology(
            topology_candidates[0], quest_ids_by_provider);
    }

    queue = ({ ([ "room": start_room_path, "steps": 0 ]) });
    visited = ([ start_room_path: 1 ]);
    quest_ids = ({ });
    target_room_id = "";

    for (index = 0; index < sizeof(queue); index++) {
        mapping item;
        string room_path;
        string *room_quest_ids;
        mapping room_snapshot;
        mapping exits;
        int steps;

        item = queue[index];
        if (!mapp(item))
            continue;

        room_path = stringp(item["room"]) ? item["room"] : "";
        steps = intp(item["steps"]) ? item["steps"] : 0;
        if (room_path == "")
            continue;

        room_quest_ids = query_room_marker_quest_ids(player, room_path,
            quest_ids_by_provider);
        if (sizeof(room_quest_ids) > 0) {
            if (target_room_id == "")
                target_room_id = room_path;
            quest_ids = append_unique_strings(quest_ids, room_quest_ids);
        }

        if (steps >= INDOOR_QUEST_SCAN_MAX_STEPS || sizeof(visited) >= INDOOR_QUEST_SCAN_MAX_ROOMS)
            continue;

        room_snapshot = ROOM_EXITS_XK_HELPER->query_map_room_snapshot(room_path, 0, 0);
        if (!mapp(room_snapshot))
            continue;

        exits = room_snapshot["exits"];
        if (!mapp(exits) || sizeof(exits) == 0)
            continue;

        foreach (string dir, mixed next_room_ref in exits) {
            string next_room_path;

            if (!stringp(next_room_ref))
                continue;
            next_room_path = next_room_ref;
            if (next_room_path == "" || next_room_path == entry_room_path)
                continue;
            if (ROOM_MAPGRID_XK_HELPER->normalize_grid_direction(dir) == "out")
                continue;
            if (is_indoor_subroom_path(next_room_path)
                && CONTENT_REGISTRY_D->query_indoor_root_room_path(next_room_path)
            != entry_room_path)
                continue;
            if (visited[next_room_path])
                continue;

            visited[next_room_path] = 1;
            queue += ({ ([ "room": next_room_path, "steps": steps + 1 ]) });
        }
    }

    return ([
        "target_room_id": target_room_id == "" ? start_room_path : target_room_id,
        "quest_ids": quest_ids,
    ]);
}

public varargs mapping query_room_indoor_entry_quest_marker(object player,
    string entry_room_path, mapping quest_ids_by_provider)
{
    mapping room_snapshot;
    mapping exits;
    string *quest_ids;
    string target_room_id;

    if (!objectp(player) || !stringp(entry_room_path) || entry_room_path == "")
        return ([ "target_room_id": "", "quest_ids": ({ }) ]);
    if (is_indoor_subroom_path(entry_room_path))
        return ([ "target_room_id": "", "quest_ids": ({ }) ]);

    room_snapshot = ROOM_EXITS_XK_HELPER->query_map_room_snapshot(entry_room_path, 0, 0);
    if (!mapp(room_snapshot))
        return ([ "target_room_id": "", "quest_ids": ({ }) ]);

    exits = room_snapshot["exits"];
    if (!mapp(exits) || sizeof(exits) == 0)
        return ([ "target_room_id": "", "quest_ids": ({ }) ]);

    quest_ids = ({ });
    target_room_id = "";
    foreach (string dir, mixed room_ref in exits) {
        string target_path;
        mapping marker;
        string *marker_quest_ids;

        if (!stringp(room_ref))
            continue;
        target_path = room_ref;
        if (!should_scan_indoor_entry_exit(entry_room_path, target_path, ROOM_MAPGRID_XK_HELPER->normalize_grid_direction(dir)))
            continue;

        marker = query_indoor_entry_quest_marker(player, entry_room_path, target_path,
            quest_ids_by_provider);
        marker_quest_ids = arrayp(marker["quest_ids"]) ? marker["quest_ids"] : ({ });
        if (sizeof(marker_quest_ids) == 0)
            continue;
        if (target_room_id == "")
            target_room_id = stringp(marker["target_room_id"]) ? marker["target_room_id"] : target_path;
        quest_ids = append_unique_strings(quest_ids, marker_quest_ids);
    }

    return ([ "target_room_id": target_room_id, "quest_ids": quest_ids ]);
}

public mapping query_indoor_entry_quest_marker_from_topology(mapping candidate,
    mapping quest_ids_by_provider)
{
    mixed *room_candidates;
    mapping room_candidate;
    string *provider_ids;
    string *provider_quest_ids;
    string *room_quest_ids;
    string *quest_ids;
    string provider_id;
    string target_room_id;

    if (!mapp(candidate) || !mapp(quest_ids_by_provider))
        return ([ "target_room_id": "", "quest_ids": ({ }) ]);

    room_candidates = arrayp(candidate["room_candidates"])
        ? candidate["room_candidates"] : ({ });
    quest_ids = ({ });
    target_room_id = "";
    foreach (room_candidate in room_candidates) {
        if (!mapp(room_candidate))
            continue;
        provider_ids = arrayp(room_candidate["provider_ids"])
            ? room_candidate["provider_ids"] : ({ });
        room_quest_ids = ({ });
        foreach (provider_id in provider_ids) {
            if (!stringp(provider_id) || provider_id == "")
                continue;
            if (!NPC_SCHEDULE_D->query_provider_present_in_room(
                provider_id, stringp(room_candidate["room_id"])
                    ? room_candidate["room_id"] : ""))
                continue;
            provider_quest_ids = quest_ids_by_provider[provider_id];
            room_quest_ids = append_unique_strings(room_quest_ids,
                arrayp(provider_quest_ids) ? provider_quest_ids : ({ }));
        }
        if (sizeof(room_quest_ids) == 0)
            continue;
        if (target_room_id == "" && stringp(room_candidate["room_id"]))
            target_room_id = room_candidate["room_id"];
        quest_ids = append_unique_strings(quest_ids, room_quest_ids);
    }

    return ([
        "target_room_id": target_room_id == ""
            ? candidate["target_room_id"] : target_room_id,
        "quest_ids": quest_ids,
    ]);
}

public mapping query_exit_indoor_quest_marker_from_topology(mixed *candidates,
    mapping quest_ids_by_provider)
{
    mapping candidate;
    mapping marker;
    string *marker_quest_ids;
    string *quest_ids;
    string target_room_id;

    quest_ids = ({ });
    target_room_id = "";
    if (!arrayp(candidates) || !mapp(quest_ids_by_provider))
        return ([ "target_room_id": target_room_id, "quest_ids": quest_ids ]);

    foreach (candidate in candidates) {
        if (!mapp(candidate))
            continue;
        marker = query_indoor_entry_quest_marker_from_topology(
            candidate, quest_ids_by_provider);
        marker_quest_ids = arrayp(marker["quest_ids"])
            ? marker["quest_ids"] : ({ });
        if (sizeof(marker_quest_ids) == 0)
            continue;
        if (target_room_id == "" && stringp(marker["target_room_id"]))
            target_room_id = marker["target_room_id"];
        quest_ids = append_unique_strings(quest_ids, marker_quest_ids);
    }

    return ([ "target_room_id": target_room_id, "quest_ids": quest_ids ]);
}

public string *append_unique_strings(string *base, string *values)
{
    string value;

    if (!arrayp(base))
        base = ({ });
    if (!arrayp(values))
        return base;
    foreach (value in values) {
        if (stringp(value) && value != "" && member_array(value, base) == -1)
            base += ({ value });
    }
    return base;
}

public string query_entity_provider_id(mapping entity)
{
    mapping template;
    string provider_id;
    string template_id;

    if (!mapp(entity))
        return "";

    provider_id = stringp(entity["provider_id"]) ? entity["provider_id"] : "";
    if (provider_id != "")
        return provider_id;

    template_id = stringp(entity["template_id"]) ? entity["template_id"] : "";
    if (template_id == "")
        return "";
    template = CONTENT_REGISTRY_D->query_npc_template(template_id);
    return mapp(template) && stringp(template["provider_id"]) ? template["provider_id"] : "";
}

public int is_indoor_subroom_path(string room_path)
{
    return stringp(room_path) && strsrch(room_path, "__") != -1;
}

public int should_scan_indoor_entry_exit(string current_room_path, string target_room_path,
    string canonical_dir)
{
    string target_root;

    if (!stringp(current_room_path) || current_room_path == "")
        return 0;
    if (!stringp(target_room_path) || target_room_path == "")
        return 0;
    if (ROOM_MAPGRID_XK_HELPER->is_grid_direction(canonical_dir) || canonical_dir == "out")
        return 0;
    /* 旧条件额外要求「当前房非子房且
     * 等于目标建筑根」，把内容里大量真实存在的子房→同建筑子房特殊出口（如当铺
     * 库下层 up→朝奉柜）全部拒之闸外，客户端特殊出口感叹号因此全灭。
     * 现改为：当前房与目标同建筑群（同 "__" 前缀根）即扫——户外根进自家子房、
     * 子房深入同建筑均算入口；跨建筑不扫，防误标与扫描爆炸。 */
    target_root = CONTENT_REGISTRY_D->query_indoor_root_room_path(target_room_path);
    if (!stringp(target_root) || target_root == "")
        return 0;
    return target_root == indoor_entry_root_of(current_room_path);
}

/* 当前房的室内入口归属根：子房取 "__" 前缀根，户外根房取自身。 */
public string indoor_entry_root_of(string room_path)
{
    string root;

    if (!stringp(room_path) || room_path == "")
        return "";
    if (strsrch(room_path, "__") != -1) {
        root = CONTENT_REGISTRY_D->query_indoor_root_room_path(room_path);
        return stringp(root) ? root : "";
    }
    return room_path;
}

/* 非网格出口任务标记的索引直查——目标子房按建筑根键
 * 取 CONTENT_REGISTRY_D 投影索引，与玩家可用任务 provider 求交集，
 * 在场判定复用 NPC_SCHEDULE_D 根房投影语义。零房间遍历。
 * 不变式（单一属主）：楼内归属判定仅此一处；诊断工具与其余标记路径
 * 须经本函数或后续收敛的统一属主取数，禁止再复制谓词。 */
public string *query_indoor_exit_quest_ids_by_index(string target_room_path,
    mapping quest_ids_by_provider)
{
    string *quest_ids;
    string *provider_ids;
    string provider_id;
    string root_room;

    quest_ids = ({ });
    if (!stringp(target_room_path) || target_room_path == ""
        || !mapp(quest_ids_by_provider))
        return quest_ids;
    root_room = CONTENT_REGISTRY_D->query_indoor_root_room_path(target_room_path);
    if (!stringp(root_room) || root_room == "")
        return quest_ids;
    provider_ids = keys(quest_ids_by_provider);
    foreach (provider_id in provider_ids) {
        if (!stringp(provider_id) || provider_id == "")
            continue;
        /* 建筑根投影键含该 provider 即认定其在楼内（静态归属） */
        if (sizeof(CONTENT_REGISTRY_D->query_room_provider_templates(
                root_room, provider_id)) == 0)
            continue;
        /* 在场判定按根房查询：NPC_SCHEDULE_D 对室内日程目标按根房投影 */
        if (!NPC_SCHEDULE_D->query_provider_present_in_room(provider_id, root_room))
            continue;
        quest_ids += arrayp(quest_ids_by_provider[provider_id])
            ? quest_ids_by_provider[provider_id] : ({ });
    }
    return quest_ids;
}
