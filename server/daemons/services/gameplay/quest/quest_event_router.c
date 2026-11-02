/**
 * @file    quest_event_router.c
 * @brief   任务运行时事件路由模块
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>
#include <game/quest.h>

#define QUEST_PROGRESS_D "/daemons/services/gameplay/quest/quest_progress_d"
#define QUEST_STEP_MACHINE_HELPER "/daemons/services/gameplay/quest/quest_step_machine"
#define QUEST_ESCORT_RUNTIME_HELPER "/daemons/services/gameplay/quest/quest_escort_runtime"

mapping ensure_player_data(object player);
int player_has_quest_event_state(object player);
int begin_quest_mutation(object player);
int finish_quest_mutation(object player, int succeeded, int refresh_world);
mapping query_visible_quest_definitions(object player);
int set_player_quest_var(object player, string key, mixed value);
string *query_object_ids(object target);

int record_kill_progress(object player, string target_id, int count)
{
    mapping data;
    int updated;

    if (!objectp(player)
        || !stringp(target_id) || target_id == "" || count <= 0)
        return 0;

    data = ensure_player_data(player);
    if (!mapp(data) || !begin_quest_mutation(player))
        return 0;
    updated = call_other(QUEST_PROGRESS_D, "record_kill",
        query_visible_quest_definitions(player), data, target_id, count);

    if (updated) {
        if (!finish_quest_mutation(player, 1, updated))
            return 0;
    } else {
        finish_quest_mutation(player, 0, 0);
    }
    return updated;
}

int record_collect_progress(object player, string item_id, int count)
{
    mapping data;
    int updated;

    if (!objectp(player)
        || !stringp(item_id) || item_id == "" || count <= 0)
        return 0;

    data = ensure_player_data(player);
    if (!mapp(data) || !begin_quest_mutation(player))
        return 0;
    updated = call_other(QUEST_PROGRESS_D, "record_collect",
        query_visible_quest_definitions(player), data, item_id, count);

    if (updated) {
        if (!finish_quest_mutation(player, 1, updated))
            return 0;
    } else {
        finish_quest_mutation(player, 0, 0);
    }
    return updated;
}

int record_item_acquired(object player, string item_id, int count)
{
    /* 统一获取事件广播：物品被拾取、采集、钓获或购买时推进任务。
     * 各获取源只保留前处理，避免重复调用任务进度入口。 */
    if (!objectp(player) || !stringp(item_id) || item_id == "" || count <= 0)
        return 0;
    record_collect_progress(player, item_id, count);
    return 1;
}

int record_item_inspection(object player, object item)
{
    mapping data;
    string *ids;
    string id;
    int updated;

    if (!objectp(player) || !objectp(item))
        return 0;

    data = ensure_player_data(player);
    if (!mapp(data) || !begin_quest_mutation(player))
        return 0;
    ids = query_object_ids(item);
    foreach (id in ids)
        if (set_player_quest_var(player, "inspect:" + id, 1))
            updated = 1;
    if (updated) {
        if (!finish_quest_mutation(player, 1, 0))
            return 0;
    } else {
        finish_quest_mutation(player, 0, 0);
    }
    return updated;
}

string *query_object_ids(object target)
{
    mixed ids;
    string *result;
    string content_entity_id;
    string stable_id;
    string name;
    mapping seen;
    string item;

    if (!objectp(target))
        return ({ });

    result = ({ });

    /* World content uses a stable entity ID in addition to player-facing
     * aliases. Quest definitions target that ID so respawned entities remain
     * unambiguous across rooms and reloads. */
    if (function_exists("query_temp", target)) {
        content_entity_id = target->query_temp(
            "world_lifecycle/content_entity_id");
        if (stringp(content_entity_id) && content_entity_id != "")
            result += ({ content_entity_id });
    }

    stable_id = get_object_id(target);
    if (stringp(stable_id) && stable_id != "")
        result += ({ stable_id });
    ids = function_exists("query_id", target)
        ? target->query_id() : stable_id;
    if (stringp(ids))
        result += ({ ids });
    if (arrayp(ids)) {
        result += filter_array(ids, (: stringp($1) && $1 != "" :));
    }

    /* Quest steps may target a content template id rather than the
     * present id or aliases (e.g. give/collect steps keyed by template).
     * Include it so both identity styles resolve to the same event. */
    if (function_exists("query", target)) {
        name = target->query("template_id");
        if (stringp(name) && name != "")
            result += ({ name });
    }

    if (function_exists("query_name", target)) {
        name = target->query_name();
        if (stringp(name) && name != "")
            result += ({ name });
    }

    name = target->query("name");
    if (stringp(name) && name != "")
        result += ({ name });

    if (!sizeof(result))
        result = ({ base_name(target) });

    seen = ([]);
    result = filter_array(result, (: stringp($1) && $1 != "" :));
    ids = ({ });
    foreach (item in result) {
        if (seen[item])
            continue;
        seen[item] = 1;
        ids += ({ item });
    }
    return ids;
}

mapping build_target_event_payload(object player, mixed target)
{
    mapping payload;
    object env;

    payload = ([ "count": 1 ]);
    if (!objectp(player))
        return payload;

    if (objectp(target)) {
        if (target == environment(player)) {
            payload["room"] = base_name(target);
            return payload;
        }

        payload["target_ids"] = query_object_ids(target);
        env = environment(target);
        if (objectp(env) && env == environment(player))
            payload["room"] = base_name(env);
        return payload;
    }

    if (stringp(target) && target != "")
        payload["target_ids"] = ({ target });
    else if (objectp(environment(player)))
        payload["room"] = base_name(environment(player));

    return payload;
}

int payload_has_runtime_match_key(mapping payload)
{
    return mapp(payload)
        && ((arrayp(payload["target_ids"]) && sizeof(payload["target_ids"]) > 0)
            || (stringp(payload["room"]) && payload["room"] != ""));
}

int record_look_progress(object player, mixed target)
{
    mapping data;
    mapping payload;
    int updated;

    if (!objectp(player))
        return 0;
    if (!player_has_quest_event_state(player))
        return 0;

    payload = build_target_event_payload(player, target);
    if (!payload_has_runtime_match_key(payload))
        return 0;

    data = ensure_player_data(player);
    if (!mapp(data) || !begin_quest_mutation(player))
        return 0;
    updated = call_other(QUEST_PROGRESS_D, "record_event",
        query_visible_quest_definitions(player), data, "look", payload);

    if (updated) {
        if (!finish_quest_mutation(player, 1, updated))
            return 0;
    } else {
        finish_quest_mutation(player, 0, 0);
    }
    return updated;
}

varargs int record_action_progress(object player, string action, mixed target, string outcome)
{
    mapping data;
    mapping payload;
    int updated;

    if (!objectp(player)
        || !stringp(action) || action == "")
        return 0;

    payload = build_target_event_payload(player, target);
    payload["action"] = action;
    if (stringp(outcome) && outcome != "")
        payload["outcome"] = outcome;

    data = ensure_player_data(player);
    if (!mapp(data) || !begin_quest_mutation(player))
        return 0;
    updated = call_other(QUEST_PROGRESS_D, "record_event",
        query_visible_quest_definitions(player), data, "action", payload);

    if (updated) {
        if (!finish_quest_mutation(player, 1, updated))
            return 0;
    } else {
        finish_quest_mutation(player, 0, 0);
    }
    return updated;
}

private int scene_action_matches_current_step(mapping step_state, string action_id,
    object room, mixed target)
{
    string command;
    string expected_target;
    string *target_ids;

    if (!mapp(step_state) || !stringp(action_id) || action_id == ""
        || !objectp(room))
        return 0;
    if (step_state["type"] != QUEST_STEP_ACTION || step_state["action"] != action_id)
        return 0;

    command = step_state["command"] || "";
    if (command != ""
        && command != "quest action " + action_id
        && strsrch(command, "quest action " + action_id + " ") != 0)
        return 0;
    if (stringp(step_state["room"]) && step_state["room"] != ""
        && base_name(room) != step_state["room"])
        return 0;

    expected_target = step_state["target"] || "";
    if (expected_target == "")
        return 1;
    if (!objectp(target) || environment(target) != room)
        return 0;
    target_ids = query_object_ids(target);
    return member_array(expected_target, target_ids) != -1;
}

/*
 * Scene actions are the only task events a player may invoke manually. Every
 * other action event must come from its successful gameplay service.
 */
int attempt_scene_action(object player, string action_id, mixed target)
{
    mapping data;
    mapping definitions;
    mapping quest_info;
    mapping step_state;
    object room;
    string quest_id;
    int step_index;

    if (!objectp(player)
        || !stringp(action_id) || action_id == "")
        return 0;
    room = environment(player);
    if (!objectp(room))
        return 0;

    data = ensure_player_data(player);
    if (!mapp(data) || !mapp(data["active"]))
        return 0;
    definitions = query_visible_quest_definitions(player);
    foreach (quest_id, quest_info in data["active"]) {
        if (!mapp(definitions[quest_id]))
            continue;
        if (!mapp(quest_info) || quest_info["status"] != QUEST_STATUS_ACCEPTED)
            continue;
        step_index = quest_info["current_step"];
        if (!intp(step_index) || step_index < 0
            || !arrayp(quest_info["steps"])
            || step_index >= sizeof(quest_info["steps"]))
            continue;
        step_state = quest_info["steps"][step_index];
        if (!scene_action_matches_current_step(step_state, action_id, room, target))
            continue;
        return record_action_progress(player, action_id, target,
            stringp(step_state["outcome"]) ? step_state["outcome"] : "");
    }

    return 0;
}

int record_dialog_progress(object player, object target, string event_name, string topic)
{
    mapping data;
    int updated;

    if (!objectp(player)
        || !objectp(target) || !stringp(event_name) || event_name == "")
        return 0;

    data = ensure_player_data(player);
    if (!mapp(data) || !begin_quest_mutation(player))
        return 0;
    updated = call_other(QUEST_PROGRESS_D, "record_event",
        query_visible_quest_definitions(player), data, event_name, ([
            "target_ids": query_object_ids(target),
            "topic": stringp(topic) ? topic : "",
            "count": 1
        ]));

    if (updated) {
        if (!finish_quest_mutation(player, 1, updated))
            return 0;
    } else {
        finish_quest_mutation(player, 0, 0);
    }
    return updated;
}

string *query_matching_dialog_quests(object player, object target, string event_name, string topic)
{
    mapping data;

    if (!objectp(player)
        || !objectp(target) || !stringp(event_name) || event_name == "")
        return ({ });

    data = ensure_player_data(player);
    return call_other(QUEST_STEP_MACHINE_HELPER, "query_matching_active_quests",
        query_visible_quest_definitions(player), data, event_name, ([
            "target_ids": query_object_ids(target),
            "topic": stringp(topic) ? topic : "",
            "count": 1,
        ]));
}

string *query_matching_give_quests(object player, object target, object item, int count)
{
    mapping data;

    if (!objectp(player)
        || !objectp(target) || !objectp(item))
        return ({ });

    data = ensure_player_data(player);
    return call_other(QUEST_STEP_MACHINE_HELPER, "query_matching_active_quests",
        query_visible_quest_definitions(player), data, "give", ([
            "target_ids": query_object_ids(target),
            "item_ids": query_object_ids(item),
            "count": count > 0 ? count : 1,
        ]));
}

int record_consume_progress(object player, mixed item_info, string action)
{
    mapping data;
    mixed *item_ids;
    int updated;

    if (!objectp(player)
        || !stringp(action) || action == "")
        return 0;

    if (objectp(item_info))
        item_ids = query_object_ids(item_info);
    else if (stringp(item_info))
        item_ids = ({ item_info });
    else if (arrayp(item_info))
        item_ids = filter_array(item_info, (: stringp($1) && $1 != "" :));
    else
        item_ids = ({ });

    if (!sizeof(item_ids))
        return 0;

    data = ensure_player_data(player);
    if (!mapp(data) || !begin_quest_mutation(player))
        return 0;
    updated = call_other(QUEST_PROGRESS_D, "record_event",
        query_visible_quest_definitions(player), data, "consume", ([
            "item_ids": item_ids,
            "action": action,
            "count": 1
        ]));

    if (updated) {
        if (!finish_quest_mutation(player, 1, updated))
            return 0;
    } else {
        finish_quest_mutation(player, 0, 0);
    }
    return updated;
}


int record_escort_progress(object player, object old_env, object new_env)
{
    mapping data;
    int updated;

    if (!objectp(player)
        || !objectp(old_env) || !objectp(new_env))
        return 0;

    data = ensure_player_data(player);
    if (!mapp(data) || !begin_quest_mutation(player))
        return 0;
    updated = call_other(QUEST_ESCORT_RUNTIME_HELPER, "record_escort_progress",
        query_visible_quest_definitions(player), data, player, old_env, new_env);

    if (updated) {
        if (!finish_quest_mutation(player, 1, updated))
            return 0;
    } else {
        finish_quest_mutation(player, 0, 0);
    }
    return updated;
}

/* mutation 回滚时的 escort 补偿：目标移动与租约提交发生在 quest 快照之外
 * （quest_escort_runtime 经 PLAYER_STATE_D 独立提交），回滚玩家任务数据不会
 * 撤销它们。此处释放该玩家在途的护送租约，避免「任务数据已回滚、护送目标
 * 仍被占用」的半提交态；释放失败只记日志，不阻塞回滚。 */
private void release_escort_leases_for_rollback(object player, mapping definitions)
{
    mapping leases;
    mapping result;
    string quest_id;

    if (!objectp(player) || !mapp(definitions))
        return;
    leases = player->query("escort/leases");
    if (!mapp(leases))
        return;
    foreach (quest_id in keys(leases)) {
        if (!mapp(definitions[quest_id]))
            continue;
        catch(result = call_other(QUEST_ESCORT_RUNTIME_HELPER,
            "release_escort_lease", player, quest_id, "mutation_rollback"));
        if (mapp(result) && result["ok"] && result["released"])
            continue;
        log_file("system/quest", sprintf(
            "[%s] escort lease rollback incomplete for %s: %O\n",
            ctime(time()), quest_id, result));
    }
}

/* One completed movement is one quest transaction. Escort and arrive steps
 * see the same quest ledger snapshot, persist once and advance the domain
 * revision once. World projection is rebuilt without emitting a room packet;
 * player::on_move_complete remains the sole complete-room snapshot sender. */
int record_move_progress(object player, object old_env, object new_env)
{
    mapping data;
    mapping definitions;
    int arrive_updated;
    int escort_updated;
    int updated;
    mixed event_error;

    if (!objectp(player)
        || !objectp(old_env) || !objectp(new_env))
        return 0;
    if (!begin_quest_mutation(player))
        return 0;

    data = ensure_player_data(player);
    definitions = query_visible_quest_definitions(player);
    if (!mapp(data) || !mapp(definitions)) {
        finish_quest_mutation(player, -1, 0);
        return 0;
    }

    event_error = catch(escort_updated = call_other(
        QUEST_ESCORT_RUNTIME_HELPER, "record_escort_progress",
        definitions, data, player, old_env, new_env));
    if (!event_error) {
        event_error = catch(arrive_updated = call_other(
            QUEST_PROGRESS_D, "record_event", definitions, data, "arrive", ([
                "room": base_name(new_env),
                "count": 1,
            ])));
    }
    if (event_error) {
        /* escort 副作用不在 quest 快照内：回滚前先补偿释放在途租约。 */
        release_escort_leases_for_rollback(player, definitions);
        finish_quest_mutation(player, -1, 0);
        log_file("system/quest", sprintf(
            "[%s] move progress rolled back for %s -> %s: %O\n",
            ctime(time()), PLAYER_STATE_D->query_player_id(player),
            base_name(new_env), event_error));
        return 0;
    }

    updated = escort_updated || arrive_updated;
    if (!updated) {
        finish_quest_mutation(player, 0, 0);
        return 0;
    }
    if (!finish_quest_mutation(player, 1, 0))
        return 0;

    if (escort_updated)
        player->set_temp("quest/last_escort_update_at", time());
    WORLD_ORCHESTRATOR_D->refresh_player_world_state(player, new_env, 0);
    return 1;
}

int record_give_progress(object player, object target, object item, int count)
{
    mapping data;
    int updated;

    if (!objectp(player)
        || !objectp(target) || !objectp(item) || count <= 0)
        return 0;

    data = ensure_player_data(player);
    if (!mapp(data) || !begin_quest_mutation(player))
        return 0;
    updated = call_other(QUEST_PROGRESS_D, "record_event",
        query_visible_quest_definitions(player), data, "give", ([
            "target_ids": query_object_ids(target),
            "item_ids": query_object_ids(item),
            "count": count
        ]));

    if (updated) {
        if (!finish_quest_mutation(player, 1, updated))
            return 0;
        /* GIVE 步匹配成功：交付物品被 NPC 收下，从世界中移除（任务消费语义），
         * 并清除重生冷却，房间可立即为下一位玩家刷新任务物品。 */
        if (objectp(item) && function_exists("is_npc", target)
            && call_other(target, "is_npc")) {
            WORLD_ENTITY_LIFECYCLE_D->clear_respawn_cooldown(item);
            destruct(item);
        }
    } else {
        finish_quest_mutation(player, 0, 0);
    }
    return updated;
}
