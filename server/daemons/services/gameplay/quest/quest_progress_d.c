/**
 * @file    quest_progress_d.c
 * @brief   任务进度守护进程,分发进度读写/查询/步进到子 helper。
 */

#pragma strict_types
#pragma save_types

#include <game/quest.h>

#define QUEST_PROGRESS_WRITE_HELPER "/daemons/services/gameplay/quest/quest_progress_write"
#define QUEST_QUERY_HELPER          "/daemons/services/gameplay/quest/quest_query"
#define QUEST_STEP_MACHINE_HELPER   "/daemons/services/gameplay/quest/quest_step_machine"

int can_turn_in(mapping quest_definitions, mapping player_data, string quest_id)
{
    return call_other(QUEST_PROGRESS_WRITE_HELPER, "can_turn_in_quest", quest_definitions, player_data, quest_id);
}

int update_progress(mapping quest_definitions, mapping player_data, string quest_id, int count)
{
    return call_other(QUEST_PROGRESS_WRITE_HELPER, "update_quest_progress", quest_definitions, player_data, quest_id, count);
}

int record_kill(mapping quest_definitions, mapping player_data, string target_id, int count)
{
    return call_other(QUEST_PROGRESS_WRITE_HELPER, "record_kill_progress", quest_definitions, player_data, target_id, count);
}

int record_collect(mapping quest_definitions, mapping player_data, string item_id, int count)
{
    return call_other(QUEST_PROGRESS_WRITE_HELPER, "record_collect_progress", quest_definitions, player_data, item_id, count);
}

int record_event(mapping quest_definitions, mapping player_data, string event_name, mapping payload)
{
    return call_other(QUEST_STEP_MACHINE_HELPER, "record_event_progress", quest_definitions, player_data, event_name, payload);
}

mapping query_runtime_record(mapping player_data, string quest_id)
{
    return call_other(QUEST_QUERY_HELPER, "query_player_quest_record", player_data, quest_id);
}

mixed *build_objectives(mapping quest_def, mapping quest_info)
{
    return call_other(QUEST_STEP_MACHINE_HELPER, "build_objective_view", quest_def, quest_info);
}
