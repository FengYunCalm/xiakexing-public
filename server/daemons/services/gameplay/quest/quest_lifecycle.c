/**
 * @file    quest_lifecycle.c
 * @brief   任务生命周期动作子服务



 */

#pragma strict_types
#pragma save_types

#include <system/globals.h>
#include <system/event_names.h>
#include <system/services.h>
#include <game/quest.h>

#define QUEST_STEP_MACHINE_HELPER "/daemons/services/gameplay/quest/quest_step_machine"
#define QUEST_TRANSITION_HELPER   "/daemons/services/gameplay/quest/quest_transition_d"
#define QUEST_ESCORT_RUNTIME_HELPER "/daemons/services/gameplay/quest/quest_escort_runtime"
#define QUEST_SETTLEMENT_HELPER   "/daemons/services/gameplay/quest/quest_settlement_d"

mapping ensure_player_data(object player);
int begin_quest_mutation(object player);
int finish_quest_mutation(object player, int succeeded, int refresh_world);
void queue_quest_event(object player, string event_name, string quest_id);
void mark_player_map_projection_changed(object player);
mapping query_visible_quest_definition(object player, string quest_id);
int can_accept_quest(object player, string quest_id);
int accept_quest(object player, string quest_id);
int can_turn_in_quest(object player, string quest_id);
varargs int record_action_progress(object player, string action, mixed target, string outcome);

private int is_active_or_completed_status(int status)
{
    return member_array(status, ({ QUEST_STATUS_ACCEPTED, QUEST_STATUS_COMPLETED })) != -1;
}

private void mark_failure_state(mapping quest_info, int previous_status,
    string source, string reason, int recoverable)
{
    if (!mapp(quest_info))
        return;

    quest_info["failed_from_status"] = previous_status;
    quest_info["failed_at"] = time();
    quest_info["failure_source"] = stringp(source) && source != "" ? source : "system";
    quest_info["failure_reason"] = stringp(reason) ? reason : "";
    quest_info["recoverable"] = recoverable ? 1 : 0;
}

private int is_recoverable_failure(mapping quest_info)
{
    int previous_status;

    if (!mapp(quest_info) || (quest_info["status"] || 0) != QUEST_STATUS_FAILED)
        return 0;

    previous_status = quest_info["failed_from_status"] || QUEST_STATUS_ACCEPTED;
    if (!is_active_or_completed_status(previous_status))
        return 0;

    return quest_info["recoverable"] != 0;
}

private void clear_failure_state(mapping quest_info)
{
    if (!mapp(quest_info))
        return;

    map_delete(quest_info, "failed_from_status");
    map_delete(quest_info, "failed_at");
    map_delete(quest_info, "failure_source");
    map_delete(quest_info, "failure_reason");
    map_delete(quest_info, "recoverable");
}

private void accept_configured_follow_up_quests(object player, mapping quest_def)
{
    mixed next_quests;
    string quest_id;

    if (!objectp(player) || !mapp(quest_def))
        return;
    next_quests = quest_def["auto_accept_next_quests"];
    if (!arrayp(next_quests))
        return;
    foreach (quest_id in next_quests) {
        if (!stringp(quest_id) || quest_id == "" || !can_accept_quest(player, quest_id))
            continue;
        accept_quest(player, quest_id);
    }
}

int accept_quest(object player, string quest_id)
{
    mapping data;
    mapping quest_def;
    mapping result;
    mapping runtime_state;
    int acceptance_serial;

    if (!objectp(player)
        || !stringp(quest_id) || quest_id == "")
        return 0;

    quest_def = query_visible_quest_definition(player, quest_id);
    if (!mapp(quest_def))
        return 0;
    if (!can_accept_quest(player, quest_id))
        return 0;

    data = ensure_player_data(player);
    if (!mapp(data))
        return 0;
    if (mapp(data["active"][quest_id]) || mapp(data["completed"][quest_id]))
        return 0;

    if (!begin_quest_mutation(player))
        return 0;
    map_delete(data["abandoned"], quest_id);
    runtime_state = call_other(QUEST_STEP_MACHINE_HELPER,
        "build_initial_quest_runtime", quest_def);
    if (!mapp(runtime_state)) {
        finish_quest_mutation(player, -1, 0);
        return 0;
    }
    /* 护送任务重新接取——先经统一 release 清理旧租约；
     * 存在 pending release（目标未归还）时停止接取，成功释放后才允许
     * 新 acceptance（防止双目标）。无旧租约（首次接取）不阻塞。 */
    if (quest_def["type"] == QUEST_TYPE_ESCORT) {
        if (mapp(player->query("escort/leases"))
            && mapp(player->query("escort/leases")[quest_id])) {
            result = call_other(QUEST_ESCORT_RUNTIME_HELPER,
                "release_escort_lease", player, quest_id, "reaccept");
            if (!mapp(result) || !result["ok"] || result["pending"]) {
                finish_quest_mutation(player, -1, 0);
                return 0;
            }
        }
    }
    acceptance_serial = intp(data["acceptance_serial"])
        && data["acceptance_serial"] >= 0
        ? data["acceptance_serial"] + 1 : 1;
    data["acceptance_serial"] = acceptance_serial;
    runtime_state["acceptance_id"] = sprintf("%s:%d:%d", quest_id,
        runtime_state["accepted_at"] || time(), acceptance_serial);
    runtime_state["acceptance_revision"] = acceptance_serial;
    if (!call_other(QUEST_TRANSITION_HELPER, "initialize_accept", data,
        quest_id, runtime_state)) {
        finish_quest_mutation(player, -1, 0);
        return 0;
    }
    queue_quest_event(player, EVENT_PLAYER_QUEST_ACCEPTED, quest_id);
    if (!finish_quest_mutation(player, 1, 1))
        return 0;
    return 1;
}

int fail_quest(object player, string quest_id, string source, string reason,
    int recoverable)
{
    mapping data;
    mapping quest_info;
    mapping failed_info;
    int previous_status;

    if (!objectp(player)
        || !stringp(quest_id) || quest_id == ""
        || !mapp(query_visible_quest_definition(player, quest_id)))
        return 0;

    data = ensure_player_data(player);
    if (!mapp(data))
        return 0;

    quest_info = data["active"][quest_id];
    if (!mapp(quest_info))
        quest_info = data["completed"][quest_id];
    if (!mapp(quest_info))
        return 0;

    previous_status = quest_info["status"] || QUEST_STATUS_ACCEPTED;
    if (!is_active_or_completed_status(previous_status))
        return 0;

    if (!begin_quest_mutation(player))
        return 0;
    failed_info = call_other(QUEST_TRANSITION_HELPER, "apply_transition",
        data, quest_id, QUEST_STATUS_FAILED);
    if (!mapp(failed_info)) {
        finish_quest_mutation(player, -1, 0);
        return 0;
    }

    mark_failure_state(failed_info, previous_status, source, reason,
        recoverable);
    if (!finish_quest_mutation(player, 1, 1))
        return 0;
    return 1;
}

int recover_quest(object player, string quest_id)
{
    mapping data;
    mapping quest_info;
    mapping recovered_info;
    int target_status;

    if (!objectp(player)
        || !stringp(quest_id) || quest_id == ""
        || !mapp(query_visible_quest_definition(player, quest_id)))
        return 0;

    data = ensure_player_data(player);
    if (!mapp(data))
        return 0;

    quest_info = data["abandoned"][quest_id];
    if (!is_recoverable_failure(quest_info))
        return 0;

    target_status = quest_info["failed_from_status"] || QUEST_STATUS_ACCEPTED;
    if (!is_active_or_completed_status(target_status))
        target_status = QUEST_STATUS_ACCEPTED;

    if (!begin_quest_mutation(player))
        return 0;
    recovered_info = call_other(QUEST_TRANSITION_HELPER, "apply_transition",
        data, quest_id, target_status);
    if (!mapp(recovered_info)) {
        finish_quest_mutation(player, -1, 0);
        return 0;
    }

    clear_failure_state(recovered_info);
    queue_quest_event(player, EVENT_PLAYER_QUEST_ACCEPTED, quest_id);
    if (!finish_quest_mutation(player, 1, 1))
        return 0;
    return 1;
}

int complete_quest(object player, string quest_id)
{
    mapping data;

    if (!objectp(player)
        || !stringp(quest_id) || quest_id == ""
        || !mapp(query_visible_quest_definition(player, quest_id)))
        return 0;

    data = ensure_player_data(player);
    if (!mapp(data) || !mapp(data["active"][quest_id]))
        return 0;

    if (!begin_quest_mutation(player))
        return 0;
    if (!call_other(QUEST_TRANSITION_HELPER, "apply_transition", data, quest_id,
        QUEST_STATUS_COMPLETED)) {
        finish_quest_mutation(player, -1, 0);
        return 0;
    }

    if (!finish_quest_mutation(player, 1, 1))
        return 0;
    return 1;
}

int submit_quest(object player, string quest_id)
{
    mapping data;
    mapping quest_info;
    mapping quest_def;
    mapping moral_change;
    mapping moral_response;

    if (!objectp(player)
        || !stringp(quest_id) || quest_id == "")
        return 0;

    quest_def = query_visible_quest_definition(player, quest_id);
    data = ensure_player_data(player);
    if (!mapp(quest_def) || !mapp(data))
        return 0;

    quest_info = data["active"][quest_id];
    if (!mapp(quest_info))
        quest_info = data["completed"][quest_id];
    if (!mapp(quest_info) || quest_info["status"] != QUEST_STATUS_COMPLETED)
        return 0;

    if (!begin_quest_mutation(player))
        return 0;
    if (!call_other(QUEST_SETTLEMENT_HELPER, "settle_turn_in", player,
        quest_id, quest_def, quest_info)) {
        finish_quest_mutation(player, -1, 0);
        return 0;
    }
    if (!call_other(QUEST_TRANSITION_HELPER, "apply_transition", data, quest_id,
        QUEST_STATUS_DELIVERED)) {
        finish_quest_mutation(player, -1, 0);
        return 0;
    }
    /* Events are queued into the mutation batch: the outer owner flushes them
     * only after the final commit, so a rolled-back turn-in never emits the
     * success event or its follow-up acceptance events. */
    queue_quest_event(player, EVENT_PLAYER_QUEST_TURNED_IN, quest_id);
    /* 日常任务：记录完成时的游戏日（同日不可重复接） */
    if (quest_def["daily"]) {
        if (!mapp(data["daily_log"]))
            data["daily_log"] = ([ ]);
        data["daily_log"][quest_id] = TIME_D->query_day();
        moral_change = ([ "moral":
            (intp(player->query("moral")) ? player->query("moral") : 0) + 1 ]);
        moral_response = PLAYER_STATE_D->commit_state_fields(player,
            moral_change,
            PLAYER_STATE_D->build_state_idempotency_key(player,
                "quest/daily_moral", ([ "quest_id": quest_id,
                    "change": moral_change ])));
        if (!mapp(moral_response) || !moral_response["ok"]) {
            finish_quest_mutation(player, -1, 0);
            return 0;
        }
        player->schedule_save();
    }
    accept_configured_follow_up_quests(player, quest_def);
    if (!finish_quest_mutation(player, 1, 1))
        return 0;
    return 1;
}

int turn_in_quest(object player, string quest_id)
{
    mapping data;
    mapping quest_info;

    if (!objectp(player)
        || !stringp(quest_id) || quest_id == ""
        || !mapp(query_visible_quest_definition(player, quest_id)))
        return 0;

    data = ensure_player_data(player);
    if (!mapp(data))
        return 0;

    quest_info = data["active"][quest_id];
    if (!mapp(quest_info))
        quest_info = data["completed"][quest_id];
    if (!mapp(quest_info) || quest_info["status"] != QUEST_STATUS_COMPLETED)
        return 0;

    if (!can_turn_in_quest(player, quest_id))
        return 0;

    if (!begin_quest_mutation(player))
        return 0;
    if (!call_other(QUEST_TRANSITION_HELPER, "set_completed_if_ready", data,
        quest_id, 1)) {
        finish_quest_mutation(player, -1, 0);
        return 0;
    }
    if (!submit_quest(player, quest_id)) {
        finish_quest_mutation(player, -1, 0);
        return 0;
    }
    if (!finish_quest_mutation(player, 1, 1))
        return 0;
    return 1;
}

int abandon_quest(object player, string quest_id)
{
    mapping data;
    mapping quest_def;
    mapping quest_info;

    if (!objectp(player)
        || !stringp(quest_id) || quest_id == "")
        return 0;

    quest_def = query_visible_quest_definition(player, quest_id);
    if (!mapp(quest_def))
        return 0;

    data = ensure_player_data(player);
    if (!mapp(data))
        return 0;

    quest_info = data["active"][quest_id];
    if (!mapp(quest_info))
        return 0;

    if (!begin_quest_mutation(player))
        return 0;
    if (!fail_quest(player, quest_id, "abandon", "玩家主动放弃任务", 1)) {
        finish_quest_mutation(player, -1, 0);
        return 0;
    }
    call_other(QUEST_SETTLEMENT_HELPER, "settle_abandon", player, quest_id,
        quest_def);
    queue_quest_event(player, EVENT_PLAYER_QUEST_ABANDONED, quest_id);
    if (!finish_quest_mutation(player, 1, 1))
        return 0;
    return 1;
}
