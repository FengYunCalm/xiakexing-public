/**
 * @file    world_orchestrator_d.c
 * @brief   世界编排守护进程,协调玩家房间快照发送。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>
#include <system/event_names.h>

inherit "/lib/framework/daemon";

private void emit_player_room_snapshot(object player, object room);

private mapping query_projection_actor_def(string actor_id)
{
    mapping snapshot;
    mapping actors;

    if (!stringp(actor_id) || actor_id == "")
        return 0;

    snapshot = WORLD_PROJECTION_D->query_projection_registry_snapshot();
    actors = mapp(snapshot) ? snapshot["actors"] : 0;
    return mapp(actors) && mapp(actors[actor_id]) ? actors[actor_id] : 0;
}

private object resolve_player(string player_id)
{
    if (!stringp(player_id) || player_id == "")
        return 0;

    return find_player_online(player_id);
}

private string query_actor_persistence_scope_key(object player)
{
    string player_id;

    player_id = objectp(player) ? PLAYER_STATE_D->query_player_id(player) : "unknown";
    if (!stringp(player_id) || player_id == "")
        player_id = "unknown";
    return "player:" + player_id + ":actors";
}

private int has_active_room_object(object room, string present_id, string object_path)
{
    object ob;
    int hp;

    if (!objectp(room) || !stringp(present_id) || present_id == ""
        || !stringp(object_path) || object_path == "")
        return 0;

    ob = present(present_id, room);
    if (!objectp(ob) || base_name(ob) != object_path)
        return 0;
    if (ob->query_temp("is_dying"))
        return 0;

    hp = ob->query("combat/hp");
    return !intp(hp) || hp > 0;
}

private void refresh_room_observers(object room)
{
    object observer;

    if (!objectp(room))
        return;

    foreach (observer in all_inventory(room)) {
        if (!objectp(observer) || !interactive(observer))
            continue;
        if (stringp(observer->query_temp("xk_protocol_version"))
            && observer->query_temp("xk_protocol_version") != "") {
            emit_player_room_snapshot(observer, room);
            continue;
        }
        call_other("/lib/std/utils/util_view", "refresh_room_view", observer, room);
    }
}

private void emit_player_room_snapshot(object player, object room)
{
    if (!objectp(player) || !interactive(player) || !objectp(room))
        return;

    player->emit_xk_room_snapshot(player, room);
}

varargs int refresh_player_world_state(object player, object room, int emit_snapshot)
{
    int changed;

    if (!objectp(player))
        return 0;
    if (!objectp(room))
        room = environment(player);
    if (!objectp(room))
        return 0;

    changed = WORLD_OBSERVER_D->refresh_player_projection_state(player, room);
    if (changed && emit_snapshot)
        emit_player_room_snapshot(player, room);

    return changed;
}

/* quest world rule 的 scope 变更组走 world 域 owner 事务：一组 flags
 * （含可选 counter delta）单事务提交，handler 委托 WORLD_STATE_D 的
 * 原子批量发布，apply 失败由协调器按 before 值统一回滚，消除组内
 * 部分提交。 */
private int apply_world_scope_mutation(object player, string apply_scope,
    string quest_id, string phase, mapping flags, mapping counter_deltas);

/* L-169：任务世界规则的后台恢复队列（有界）。事务被拒或提交失败时保留
 * 原操作身份，由后续任务规则应用或显式恢复重试，绝不静默丢弃。 */
#define MAX_PENDING_QUEST_RULES 64
nosave mapping *_pending_quest_rules;

private void record_pending_quest_rule(object player, string apply_scope,
    string quest_id, string phase, mapping flags, mapping counter_deltas)
{
    if (!arrayp(_pending_quest_rules))
        _pending_quest_rules = ({ });
    if (sizeof(_pending_quest_rules) >= MAX_PENDING_QUEST_RULES)
        _pending_quest_rules = _pending_quest_rules[
            sizeof(_pending_quest_rules) - MAX_PENDING_QUEST_RULES + 1 ..];
    _pending_quest_rules += ({ ([
        "player_id": get_object_id(player) || "",
        "player": player,
        "apply_scope": apply_scope,
        "quest_id": quest_id,
        "phase": phase,
        "flags": mapp(flags) ? copy(flags) : ([ ]),
        "counter_deltas": mapp(counter_deltas) ? copy(counter_deltas) : ([ ]),
        "created_at": time(),
        "attempts": 0,
    ]) });
    log_file("system/quest", sprintf(
        "[%s] quest world rule pending recovery: quest=%s phase=%s scope=%s player=%s\n",
        ctime(time()), quest_id, phase, apply_scope, get_object_id(player) || "?"));
}

mapping query_pending_quest_rules()
{
    return arrayp(_pending_quest_rules) ? copy(_pending_quest_rules) : ({ });
}

/* 重试仍待恢复的任务世界规则；成功的出队，失败的保留并累计 attempts。 */
int retry_pending_quest_rules()
{
    mapping entry;
    mapping *kept;
    int recovered;

    if (!arrayp(_pending_quest_rules) || sizeof(_pending_quest_rules) == 0)
        return 0;
    kept = ({ });
    foreach (entry in _pending_quest_rules) {
        if (!mapp(entry))
            continue;
        entry["attempts"] = (entry["attempts"] || 0) + 1;
        if (apply_world_scope_mutation(entry["player"], entry["apply_scope"],
            entry["quest_id"], entry["phase"], entry["flags"],
            entry["counter_deltas"]))
            recovered++;
        else
            kept += ({ entry });
    }
    _pending_quest_rules = kept;
    return recovered;
}

private int apply_world_scope_mutation(object player, string apply_scope,
    string quest_id, string phase, mapping flags, mapping counter_deltas)
{
    mapping payload;
    mapping created;
    mapping committed;
    mapping prepared;
    string key;
    mixed err;
    int txn_id;

    payload = ([
        "schema_version": 1,
        "world_state": ([
            "scope_key": apply_scope,
            "flags": flags,
            "counter_deltas": mapp(counter_deltas) ? counter_deltas : ([ ]),
        ]),
    ]);
    key = sprintf("world/quest_rule/%s/%s/%s/%d", quest_id, phase,
        get_object_id(player) || "unknown", time());
    err = catch(created = OWNER_COMMIT_D->create_domain_transaction("world",
        key, ({ "owner/world/" + apply_scope }), payload));
    if (err || !mapp(created) || created["ok"] != 1)
        return 0;
    txn_id = mapp(created["transaction"]) ? created["transaction"]["id"] : 0;
    err = catch(prepared = OWNER_COMMIT_D->prepare_transaction(txn_id));
    if (err || !mapp(prepared) || prepared["ok"] != 1)
        return 0;
    err = catch(committed = OWNER_COMMIT_D->commit_transaction(txn_id));
    return !err && mapp(committed) && committed["ok"] == 1;
}

private void apply_world_rule_for_quest(object player, string quest_id, string phase)
{
    mapping definition;
    mapping policy;
    string scope_key;
    string personal_scope_key;
    string apply_scope;
    object room;

    if (!objectp(player) || !stringp(quest_id) || quest_id == "")
        return;

    definition = QUEST_D->query_visible_quest_definition(player, quest_id);
    policy = mapp(definition)
        ? CONTENT_MODE_POLICY_D->compile_content_policy("quest", definition) : 0;
    if (!mapp(policy) || !policy["ok"])
        return;

    room = environment(player);
    scope_key = WORLD_CONTEXT_D->query_player_world_scope_key(player, room);
    personal_scope_key = WORLD_CONTEXT_D->query_player_personal_scope_key(player, room);
    apply_scope = "";
    if (policy["state_scope"] == "character"
        || policy["instance_scope"] == "character")
        apply_scope = personal_scope_key;
    else if ((policy["state_scope"] == "world"
        || policy["state_scope"] == "mmo")
        && policy["instance_scope"] == "shared")
        apply_scope = scope_key;
    if (apply_scope == "")
        return;

    /* L-169：世界规则是任务结果的一部分，不能“任务推进了、世界变更丢了”。
     * 失败时登记待恢复（有界），并在后续任意规则应用前先补。 */
    retry_pending_quest_rules();

    if (phase == "accepted") {
        if (!apply_world_scope_mutation(player, apply_scope, quest_id, phase, ([
                "quest/" + quest_id + "/accepted": 1,
                "quest/" + quest_id + "/completed": 0,
            ]), 0))
            record_pending_quest_rule(player, apply_scope, quest_id, phase, ([
                "quest/" + quest_id + "/accepted": 1,
                "quest/" + quest_id + "/completed": 0,
            ]), 0);
        return;
    }

    if (phase == "turned_in") {
        if (!apply_world_scope_mutation(player, apply_scope, quest_id, phase, ([
                "quest/" + quest_id + "/accepted": 0,
                "quest/" + quest_id + "/completed": 1,
            ]), ([ "quest/completed_count": 1 ])))
            record_pending_quest_rule(player, apply_scope, quest_id, phase, ([
                "quest/" + quest_id + "/accepted": 0,
                "quest/" + quest_id + "/completed": 1,
            ]), ([ "quest/completed_count": 1 ]));
        return;
    }

    if (phase == "abandoned") {
        if (!apply_world_scope_mutation(player, apply_scope, quest_id, phase, ([
                "quest/" + quest_id + "/accepted": 0,
                "quest/" + quest_id + "/completed": 0,
            ]), 0))
            record_pending_quest_rule(player, apply_scope, quest_id, phase, ([
                "quest/" + quest_id + "/accepted": 0,
                "quest/" + quest_id + "/completed": 0,
            ]), 0);
    }
}

void on_player_quest_accepted(string event_name, mapping payload)
{
    object player;

    player = resolve_player(payload["player_id"]);
    apply_world_rule_for_quest(player, payload["quest_id"], "accepted");
}

void on_player_quest_turned_in(string event_name, mapping payload)
{
    object player;

    player = resolve_player(payload["player_id"]);
    apply_world_rule_for_quest(player, payload["quest_id"], "turned_in");
}

void on_player_quest_abandoned(string event_name, mapping payload)
{
    object player;

    player = resolve_player(payload["player_id"]);
    apply_world_rule_for_quest(player, payload["quest_id"], "abandoned");
}

void on_world_story_triggered(string event_name, mapping payload)
{
    mixed *actions;
    mixed action;
    mapping profile;
    mapping trigger;
    string object_path;
    string present_id;
    string room_path;
    object room;
    string reason;

    if (!mapp(payload) || !stringp(payload["story_id"]))
        return;

    trigger = mapp(payload["story"]) ? payload["story"] : STORY_D->query_story_trigger(payload["story_id"]);
    if (!mapp(trigger))
        return;

    actions = trigger["actions"];
    if (!arrayp(actions))
        return;

    foreach (action in actions) {
        if (!mapp(action) || !stringp(action["type"]))
            continue;

        switch (action["type"]) {
            case "spawn_request":
                room_path = action["room"] || trigger["room"];
                if (!stringp(room_path) || room_path == "")
                    continue;

                room = WORLD_TRAVEL_D->resolve_public_room(room_path, ([
                    "reason": "world_story_spawn",
                ]));
                profile = SPAWN_D->query_room_spawn_profile(room_path);
                present_id = mapp(profile) ? (profile["present_id"] || "") : "";
                object_path = mapp(profile) ? (profile["object_path"] || "") : "";
                if (objectp(room) && has_active_room_object(room, present_id, object_path))
                    continue;

                reason = mapp(payload["context"]) && stringp(payload["context"]["reason"])
                    ? payload["context"]["reason"]
                    : (action["reason"] || "story_trigger");
                SPAWN_D->emit_room_spawn_request(room_path, reason);
                continue;

            case "refresh_player_world_state":
                refresh_player_world_state(resolve_player(payload["actor"]), 0, 1);
                continue;
        }
    }
}

void on_world_spawn_requested(string event_name, mapping payload)
{
    mapping profile;
    string object_path;
    string present_id;
    string room_path;
    object room;
    object spawned;

    room_path = mapp(payload) ? payload["room"] : 0;
    if (!stringp(room_path) || room_path == "")
        return;

    profile = SPAWN_D->query_room_spawn_profile(room_path);
    if (!mapp(profile))
        return;

    present_id = profile["present_id"] || "";
    object_path = profile["object_path"] || "";
    if (!stringp(present_id) || present_id == ""
        || !stringp(object_path) || object_path == "")
        return;

    room = WORLD_TRAVEL_D->resolve_public_room(room_path, ([
        "reason": "world_spawn_request",
    ]));
    if (!objectp(room))
        return;
    if (has_active_room_object(room, present_id, object_path))
        return;

    spawned = call_other("/lib/std/utils/util_view", "ensure_room_object",
        room, present_id, object_path);
    if (!objectp(spawned))
        return;

    SPAWN_D->emit_room_spawn_complete(room_path,
        stringp(payload["reason"]) ? payload["reason"] : "spawn_request");
    refresh_room_observers(room);
}

void create()
{
    ::create();
    EVENT_D->on(EVENT_PLAYER_QUEST_ACCEPTED, this_object(), "on_player_quest_accepted", 0);
    EVENT_D->on(EVENT_PLAYER_QUEST_TURNED_IN, this_object(), "on_player_quest_turned_in", 0);
    EVENT_D->on(EVENT_PLAYER_QUEST_ABANDONED, this_object(), "on_player_quest_abandoned", 0);
    EVENT_D->on(EVENT_WORLD_STORY_TRIGGERED, this_object(), "on_world_story_triggered", 0);
    EVENT_D->on(EVENT_WORLD_SPAWN_REQUESTED, this_object(), "on_world_spawn_requested", 0);
}
