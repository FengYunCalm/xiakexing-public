/**
 * @file    quest_state_store.c
 * @brief   任务状态存储辅助子服务



 */

#pragma strict_types

#include <game/quest.h>
#include <system/services.h>

#define QUEST_TRANSITION_HELPER  "/daemons/services/gameplay/quest/quest_transition_d"
#define QUEST_STEP_MACHINE_HELPER "/daemons/services/gameplay/quest/quest_step_machine"

string query_player_state_key(object player)
{
    string player_key;

    if (!objectp(player))
        return "";

    player_key = PLAYER_STATE_D->query_player_id(player);
    if ((!stringp(player_key) || player_key == "") && function_exists("query_account_id", player))
        player_key = player->query_account_id();
    if (!stringp(player_key) || player_key == "")
        player_key = base_name(player);

    return stringp(player_key) ? player_key : "";
}

int player_has_quest_event_state(mapping player_quests, object player)
{
    mapping data;
    string player_key;
    string bucket;

    if (!objectp(player))
        return 0;

    if (!mapp(player_quests))
        return 0;

    player_key = query_player_state_key(player);
    if (!stringp(player_key) || player_key == "")
        return 0;
    data = player_quests[player_key];
    if (!mapp(data))
        return 0;

    foreach (bucket in ({ "active", "completed", "abandoned" })) {
        if (mapp(data[bucket]) && sizeof(data[bucket]) > 0)
            return 1;
    }
    return 0;
}

private void normalize_player_quest_buckets(mapping data)
{
    string *buckets;
    string bucket;
    string *quest_ids;
    string quest_id;
    mapping quest_info;
    mapping merged;
    mapping merged_priorities;
    int status;
    int priority;
    string expected_bucket;

    if (!mapp(data))
        return;

    buckets = ({ "active", "completed", "abandoned" });
    merged = ([]);
    merged_priorities = ([]);

    foreach (bucket in buckets) {
        if (!mapp(data[bucket]))
            data[bucket] = ([]);

        quest_ids = keys(copy(data[bucket]));
        foreach (quest_id in quest_ids) {
            quest_info = data[bucket][quest_id];
            if (!mapp(quest_info)) {
                map_delete(data[bucket], quest_id);
                continue;
            }

            status = quest_info["status"];
            switch (status) {
                case QUEST_STATUS_FAILED:
                    priority = 4;
                    break;
                case QUEST_STATUS_DELIVERED:
                    priority = 3;
                    break;
                case QUEST_STATUS_COMPLETED:
                    priority = 2;
                    break;
                case QUEST_STATUS_ACCEPTED:
                    priority = 1;
                    break;
                default:
                    priority = 0;
            }

            if (!mapp(merged[quest_id]) || priority >= (merged_priorities[quest_id] || -1)) {
                merged[quest_id] = copy(quest_info);
                merged_priorities[quest_id] = priority;
            }
        }
    }

    data["active"] = ([]);
    data["completed"] = ([]);
    data["abandoned"] = ([]);

    foreach (quest_id, quest_info in merged) {
        expected_bucket = call_other(QUEST_TRANSITION_HELPER,
            "query_status_bucket", quest_info["status"]);
        data[expected_bucket][quest_id] = quest_info;
    }
}

mapping ensure_runtime_data(mapping data, mapping quest_definitions)
{
    if (!mapp(data))
        data = ([]);
    if (!mapp(data["active"]))
        data["active"] = ([]);
    if (!mapp(data["completed"]))
        data["completed"] = ([]);
    if (!mapp(data["abandoned"]))
        data["abandoned"] = ([]);
    if (!mapp(data["player_vars"]))
        data["player_vars"] = ([]);
    if (!intp(data["domain_revision"]) || data["domain_revision"] < 0)
        data["domain_revision"] = 0;
    if (!intp(data["acceptance_serial"]) || data["acceptance_serial"] < 0)
        data["acceptance_serial"] = 0;

    normalize_player_quest_buckets(data);
    call_other(QUEST_STEP_MACHINE_HELPER, "normalize_quest_runtime",
        quest_definitions, data);
    return data;
}

mapping ensure_player_data(mapping player_quests, mapping quest_definitions, object player)
{
    string player_key;

    if (!objectp(player) || !mapp(player_quests))
        return 0;

    player_key = query_player_state_key(player);
    if (!stringp(player_key) || player_key == "")
        return 0;

    player_quests[player_key] = ensure_runtime_data(
        player_quests[player_key], quest_definitions);
    return player_quests[player_key];
}
