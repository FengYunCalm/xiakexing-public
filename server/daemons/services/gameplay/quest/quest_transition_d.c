/**
 * @file    quest_transition_d.c
 * @brief   任务状态迁移守护进程,按状态桶归类与迁移任务状态。
 */

#pragma strict_types
#pragma save_types

#include <game/quest.h>

/* 单一终态谓词（COMPLETED/DELIVERED/FAILED；abandon 由 FAILED 表达）。 */
int is_terminal_quest_status(int status)
{
    return member_array(status,
        ({ QUEST_STATUS_COMPLETED, QUEST_STATUS_DELIVERED, QUEST_STATUS_FAILED })) != -1;
}

/* 状态桶映射唯一 owner（写入与跨桶读取共用；新增桶只需改此处）。 */
string query_status_bucket(int status)
{
    switch (status) {
        case QUEST_STATUS_COMPLETED:
        case QUEST_STATUS_DELIVERED:
            return "completed";
        case QUEST_STATUS_FAILED:
            return "abandoned";
        default:
            return "active";
    }
}

private mapping ensure_bucket(mapping data, string bucket)
{
    if (!mapp(data[bucket]))
        data[bucket] = ([]);
    return data[bucket];
}

private mapping detach_quest_record(mapping data, string quest_id)
{
    mapping quest_info;
    string bucket;

    if (!mapp(data) || !stringp(quest_id) || quest_id == "")
        return 0;

    foreach (bucket in ({ "active", "completed", "abandoned" })) {
        ensure_bucket(data, bucket);
        quest_info = data[bucket][quest_id];
        if (!mapp(quest_info))
            continue;
        map_delete(data[bucket], quest_id);
        return copy(quest_info);
    }

    return 0;
}

mapping apply_transition(mapping data, string quest_id, int status)
{
    mapping quest_info;
    string bucket;
    mapping bucket_map;

    if (!mapp(data) || !stringp(quest_id) || quest_id == "")
        return 0;

    quest_info = detach_quest_record(data, quest_id);
    if (!mapp(quest_info))
        return 0;

    quest_info["status"] = status;
    bucket = query_status_bucket(status);
    bucket_map = ensure_bucket(data, bucket);
    bucket_map[quest_id] = quest_info;
    return quest_info;
}

mapping set_completed_if_ready(mapping data, string quest_id, int ready)
{
    mapping quest_info;

    if (!ready)
        return 0;

    quest_info = data["active"][quest_id];
    if (!mapp(quest_info))
        quest_info = data["completed"][quest_id];
    if (!mapp(quest_info))
        return 0;
    if ((quest_info["status"] || 0) == QUEST_STATUS_COMPLETED)
        return quest_info;

    return apply_transition(data, quest_id, QUEST_STATUS_COMPLETED);
}

mapping initialize_accept(mapping data, string quest_id, mapping runtime_state)
{
    mapping active;

    if (!mapp(data) || !stringp(quest_id) || quest_id == "" || !mapp(runtime_state))
        return 0;

    active = ensure_bucket(data, "active");
    active[quest_id] = runtime_state;
    return runtime_state;
}
