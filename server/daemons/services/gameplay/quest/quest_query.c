/**
 * @file    quest_query.c
 * @brief   任务只读查询辅助子服务



 */

#pragma strict_types

#include <game/quest.h>

mapping query_player_quest_record(mapping data, string quest_id)
{
    if (!mapp(data) || !stringp(quest_id))
        return 0;
    if (mapp(data["active"][quest_id]))
        return data["active"][quest_id];
    if (mapp(data["completed"][quest_id]))
        return data["completed"][quest_id];
    if (mapp(data["abandoned"][quest_id]))
        return data["abandoned"][quest_id];

    return 0;
}

int is_quest_completed(mapping data, string quest_id)
{
    mapping quest_info;

    if (!mapp(data) || !stringp(quest_id))
        return 0;

    quest_info = data["completed"][quest_id];
    return mapp(quest_info) && (quest_info["status"] || 0) == QUEST_STATUS_DELIVERED;
}

mapping query_player_quests_by_status(mapping data, mapping quest_definitions, string status)
{
    mapping result;
    mapping seen;
    string *quest_ids;
    string bucket;
    string quest_id;
    mapping quest_info;
    int quest_status;

    if (!mapp(data))
        return ([]);

    result = ([]);
    seen = ([]);
    quest_ids = keys(quest_definitions || ([]));
    foreach (quest_id in quest_ids)
        seen[quest_id] = 1;

    foreach (bucket in ({ "active", "completed", "abandoned" })) {
        foreach (quest_id in keys(data[bucket]))
            seen[quest_id] = 1;
    }

    quest_ids = keys(seen);
    foreach (quest_id in quest_ids) {
        if (!mapp(quest_definitions[quest_id]))
            continue;

        quest_info = query_player_quest_record(data, quest_id);
        if (!mapp(quest_info))
            continue;

        quest_status = quest_info["status"] || QUEST_STATUS_AVAILABLE;
        if (status == "active"
            && member_array(quest_status, ({ QUEST_STATUS_ACCEPTED, QUEST_STATUS_COMPLETED })) != -1) {
            result[quest_id] = copy(quest_info);
        }
        if (status == "completed" && quest_status == QUEST_STATUS_DELIVERED)
            result[quest_id] = copy(quest_info);
        if (status == "abandoned" && quest_status == QUEST_STATUS_FAILED)
            result[quest_id] = copy(quest_info);
    }

    return result;
}

int get_quest_status(mapping data, string quest_id)
{
    mapping quest_info;

    quest_info = query_player_quest_record(data, quest_id);
    if (!mapp(quest_info))
        return QUEST_STATUS_AVAILABLE;

    return quest_info["status"] || QUEST_STATUS_AVAILABLE;
}

string *query_active_quests(mapping data, mapping quest_definitions)
{
    return keys(query_player_quests_by_status(data, quest_definitions, "active"));
}
