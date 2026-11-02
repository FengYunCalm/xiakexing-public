/**
 * @file    quest_progress_write.c
 * @brief   任务进度写侧辅助子服务



 */

#pragma strict_types

#include <game/quest.h>

#define QUEST_QUERY_HELPER         "/daemons/services/gameplay/quest/quest_query"
#define QUEST_STEP_MACHINE_HELPER  "/daemons/services/gameplay/quest/quest_step_machine"
#define QUEST_TRANSITION_HELPER    "/daemons/services/gameplay/quest/quest_transition_d"

int can_turn_in_quest(mapping quest_definitions, mapping player_data, string quest_id)
{
    mapping quest_def;
    mapping quest_info;
    mixed *objectives;
    int current_progress;
    int index;

    if (!mapp(quest_definitions) || !mapp(player_data) || !stringp(quest_id))
        return 0;

    if (call_other(QUEST_STEP_MACHINE_HELPER, "can_turn_in_runtime",
        quest_definitions, player_data, quest_id))
        return 1;

    quest_def = quest_definitions[quest_id];
    quest_info = call_other(QUEST_QUERY_HELPER, "query_player_quest_record", player_data, quest_id);
    objectives = mapp(quest_def) && arrayp(quest_def["objectives"]) ? quest_def["objectives"] : ({ });
    current_progress = mapp(quest_info) ? (quest_info["progress"] || 0) : 0;

    if (!sizeof(objectives))
        return 1;

    for (index = 0; index < sizeof(objectives); index++) {
        int target_count;

        target_count = objectives[index]["count"] || 0;
        if (current_progress < target_count)
            return 0;
    }

    return 1;
}

int update_quest_progress(mapping quest_definitions, mapping player_data,
    string quest_id, int count)
{
    mapping quest_info;
    mapping quest_def;

    if (!stringp(quest_id) || quest_id == "" || count <= 0)
        return 0;
    if (!mapp(quest_definitions) || !mapp(player_data))
        return 0;

    quest_def = quest_definitions[quest_id];
    if (!mapp(quest_def))
        return 0;

    quest_info = player_data["active"][quest_id];
    if (!mapp(quest_info))
        return 0;

    if (arrayp(quest_info["steps"]) && sizeof(quest_info["steps"]) > 0)
        return call_other(QUEST_STEP_MACHINE_HELPER, "record_event_progress",
            quest_definitions, player_data, "progress", ([
                "quest_id": quest_id,
                "count": count
            ]));

    quest_info["progress"] = (quest_info["progress"] || 0) + count;
    if (can_turn_in_quest(quest_definitions, player_data, quest_id))
        call_other(QUEST_TRANSITION_HELPER, "set_completed_if_ready", player_data, quest_id, 1);

    return 1;
}

int record_kill_progress(mapping quest_definitions, mapping player_data,
    string target_id, int count)
{
    mapping quest_info;
    mapping quest_def;
    mixed *objectives;
    mapping objective;
    string quest_id;
    int updated;

    if (!stringp(target_id) || target_id == "" || count <= 0)
        return 0;
    if (!mapp(quest_definitions) || !mapp(player_data) || !mapp(player_data["active"]))
        return 0;

    if (call_other(QUEST_STEP_MACHINE_HELPER, "record_event_progress",
        quest_definitions, player_data, "kill", ([
            "target": target_id,
            "count": count
        ])))
        return 1;

    foreach (quest_id, quest_info in player_data["active"]) {
        quest_def = quest_definitions[quest_id];
        objectives = mapp(quest_def) && arrayp(quest_def["objectives"]) ? quest_def["objectives"] : ({ });
        if (!mapp(quest_info) || !mapp(quest_def) || quest_def["type"] != QUEST_TYPE_KILL)
            continue;

        foreach (objective in objectives) {
            if (!mapp(objective) || objective["target"] != target_id)
                continue;

            quest_info["progress"] = (quest_info["progress"] || 0) + count;
            if (can_turn_in_quest(quest_definitions, player_data, quest_id))
                call_other(QUEST_TRANSITION_HELPER, "set_completed_if_ready", player_data, quest_id, 1);
            updated = 1;
            break;
        }
    }

    return updated;
}

private int collect_objective_matches_item(mapping objective, string item_id)
{
    return mapp(objective)
        && stringp(item_id)
        && item_id != ""
        && stringp(objective["item_id"])
        && objective["item_id"] == item_id;
}

int record_collect_progress(mapping quest_definitions, mapping player_data,
    string item_id, int count)
{
    mapping quest_info;
    mapping quest_def;
    mixed *objectives;
    mapping objective;
    string quest_id;
    int updated;

    if (!stringp(item_id) || item_id == "" || count <= 0)
        return 0;
    if (!mapp(quest_definitions) || !mapp(player_data) || !mapp(player_data["active"]))
        return 0;

    if (call_other(QUEST_STEP_MACHINE_HELPER, "record_event_progress",
        quest_definitions, player_data, "collect", ([
            "item_ids": ({ item_id }),
            "count": count
        ])))
        return 1;

    foreach (quest_id, quest_info in player_data["active"]) {
        quest_def = quest_definitions[quest_id];
        objectives = mapp(quest_def) && arrayp(quest_def["objectives"])
            ? quest_def["objectives"]
            : ({ });

        if (!mapp(quest_info)
            || !mapp(quest_def)
            || quest_def["type"] != QUEST_TYPE_COLLECT
            || (arrayp(quest_info["steps"]) && sizeof(quest_info["steps"]) > 0))
            continue;

        foreach (objective in objectives) {
            if (!collect_objective_matches_item(objective, item_id))
                continue;

            quest_info["progress"] = (quest_info["progress"] || 0) + count;
            if (can_turn_in_quest(quest_definitions, player_data, quest_id))
                call_other(QUEST_TRANSITION_HELPER, "set_completed_if_ready", player_data, quest_id, 1);
            updated = 1;
            break;
        }
    }

    return updated;
}
