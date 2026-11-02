/**
 * @file    debate_d.c
 * @brief   辩论(文斗)守护进程,管理辩论会话与回合序列。
 */

#pragma strict_types
#pragma save_types

#include <framework/message_facade.h>
#include <system/services.h>

inherit "/lib/framework/daemon";

#define MAX_DEBATE_ROUNDS 100

nosave mapping _debates;
nosave int _debate_sequence;

private string generate_debate_id()
{
    _debate_sequence++;
    return sprintf("debate_%d_%d", time(), _debate_sequence);
}

void create()
{
    ::create();
    if (!mapp(_debates))
        _debates = ([]);
    _debate_sequence = 0;
}

private string query_role(mapping debate, object actor)
{
    if (!mapp(debate) || !objectp(actor))
        return "";
    if (debate["challenger"] == actor)
        return "challenger";
    if (debate["target"] == actor)
        return "target";
    return "";
}

private string query_opponent_role(string role)
{
    return role == "challenger" ? "target" : "challenger";
}

private object query_actor_by_role(mapping debate, string role)
{
    if (!mapp(debate) || !stringp(role))
        return 0;
    if (role == "challenger")
        return debate["challenger"];
    if (role == "target")
        return debate["target"];
    return 0;
}

private int is_valid_action(string action)
{
    return member_array(action, ({ "attack", "defend", "focus", "probe", "skip" })) != -1;
}

private mapping build_public_scores(mapping scores)
{
    if (!mapp(scores))
        return ([ ]);

    return ([
        "challenger": ([
            "pressure": scores["challenger"]["pressure"] || 0,
            "stability": scores["challenger"]["stability"] || 0,
        ]),
        "target": ([
            "pressure": scores["target"]["pressure"] || 0,
            "stability": scores["target"]["stability"] || 0,
        ]),
    ]);
}

private mapping build_public_pending(mapping pending)
{
    return ([
        "challenger": mapp(pending) && mapp(pending["challenger"]) ? 1 : 0,
        "target": mapp(pending) && mapp(pending["target"]) ? 1 : 0,
    ]);
}

private mapping build_public_debate_state(mapping debate)
{
    object challenger;
    object target;
    string winner;

    if (!mapp(debate))
        return ([ ]);

    challenger = debate["challenger"];
    target = debate["target"];
    winner = debate["winner"];
    if (winner == "challenger")
        winner = objectp(challenger) ? (challenger->query_name() || "") : "";
    else if (winner == "target")
        winner = objectp(target) ? (target->query_name() || "") : "";
    else if (winner != "draw")
        winner = "";

    return ([
        "debate_id": debate["id"] || "",
        "mode": debate["mode"] || "simultaneous",
        "round_policy": debate["round_policy"] || "ready_or_timeout",
        "round": debate["round"] || 1,
        "status": debate["status"] || "active",
        "winner": winner,
        "challenger": objectp(challenger) ? (challenger->query_name() || "") : "",
        "target": objectp(target) ? (target->query_name() || "") : "",
        "pending": build_public_pending(debate["pending_actions"]),
        "action_deadline": debate["action_deadline"] || 0,
        "action_timeout": debate["action_timeout"] || NUMERIC_RULE_D->query_debate_action_timeout_seconds(),
        "scores": build_public_scores(debate["scores"]),
    ]);
}

private void emit_debate_state_report(mapping debate)
{
    object challenger;
    object target;
    mapping payload;

    if (!mapp(debate))
        return;

    challenger = debate["challenger"];
    target = debate["target"];
    payload = build_public_debate_state(debate);
    send_xk_report(challenger, "debate_state", payload);
    send_xk_report(target, "debate_state", payload);
}

private void emit_debate_action_report(object player, mapping debate, mapping history_entry)
{
    mapping payload;

    if (!objectp(player) || !mapp(debate) || !mapp(history_entry))
        return;

    payload = build_public_debate_state(debate);
    payload += ([ "last_action": history_entry, ]);
    send_xk_report(player, "debate_action", payload);
    SKILL_D->xk_send_skill_event(player, ([
        "event": "debate_action",
        "skill_id": history_entry["skill_id"] || "",
        "action": history_entry["action"] || "",
        "impact": history_entry["impact"] || 0,
        "round": history_entry["round"] || 0,
    ]));
}

private string choose_npc_action(mapping actor_scores, mapping target_scores)
{
    if (!mapp(actor_scores) || !mapp(target_scores))
        return "focus";
    if ((target_scores["stability"] || 0) <= (actor_scores["pressure"] || 0))
        return "attack";
    if ((actor_scores["stability"] || 0) < 8)
        return "defend";
    return "focus";
}

private void clear_timeout(mapping debate)
{
    int handle;

    if (!mapp(debate))
        return;
    handle = debate["timeout_call_out"] || 0;
    if (handle)
        remove_call_out(handle);
    debate["timeout_call_out"] = 0;
}

private void schedule_timeout(mapping debate)
{
    int timeout;

    if (!mapp(debate) || debate["status"] != "active")
        return;

    clear_timeout(debate);
    timeout = NUMERIC_RULE_D->query_debate_action_timeout_seconds();
    if (timeout <= 0)
        timeout = 10;
    debate["action_timeout"] = timeout;
    debate["action_deadline"] = time() + timeout;
    debate["timeout_call_out"] = call_out("handle_debate_timeout", timeout, debate["id"], debate["round"]);
}

private void store_active_debate(string debate_id, mapping debate)
{
    if (!stringp(debate_id) || debate_id == "")
        return;
    if (!mapp(debate) || debate["status"] != "active") {
        map_delete(_debates, debate_id);
        return;
    }
    _debates[debate_id] = debate;
}

private void discard_finished_debate(mapping debate)
{
    if (!mapp(debate) || !stringp(debate["id"]))
        return;
    map_delete(_debates, debate["id"]);
}

private string query_active_debate_id(object player)
{
    mapping debate;
    string debate_id;
    string active_id;
    string *stale_ids;
    object challenger;
    object target;

    if (!objectp(player) || !mapp(_debates))
        return "";

    stale_ids = ({ });
    foreach (debate_id, debate in _debates) {
        if (!mapp(debate) || debate["status"] != "active")
            continue;
        challenger = debate["challenger"];
        target = debate["target"];
        if (!objectp(challenger) || !objectp(target)) {
            stale_ids += ({ debate_id });
            continue;
        }
        if (challenger == player || target == player)
            active_id = debate_id;
    }

    foreach (debate_id in stale_ids) {
        clear_timeout(_debates[debate_id]);
        map_delete(_debates, debate_id);
    }
    return active_id || "";
}

private mapping build_pending_action(object actor, string action, int timed_out)
{
    return ([
        "actor": actor,
        "action": action,
        "submitted_at": time(),
        "timeout": timed_out ? 1 : 0,
    ]);
}

private void record_pending_action(mapping debate, string role, string action, int timed_out)
{
    object actor;
    mapping pending;

    if (!mapp(debate) || !stringp(role) || !is_valid_action(action))
        return;

    actor = query_actor_by_role(debate, role);
    if (!objectp(actor))
        return;

    pending = debate["pending_actions"];
    if (!mapp(pending))
        pending = ([ ]);
    pending[role] = build_pending_action(actor, action, timed_out);
    debate["pending_actions"] = pending;
}

private void record_npc_actions(mapping debate)
{
    mapping scores;
    string role;
    string target_role;
    object actor;
    string action;

    if (!mapp(debate) || debate["status"] != "active")
        return;

    scores = debate["scores"];
    foreach (role in ({ "challenger", "target" })) {
        actor = query_actor_by_role(debate, role);
        if (!objectp(actor) || userp(actor))
            continue;
        if (mapp(debate["pending_actions"]) && mapp(debate["pending_actions"][role]))
            continue;
        target_role = query_opponent_role(role);
        action = choose_npc_action(scores[role], scores[target_role]);
        record_pending_action(debate, role, action, 0);
    }
}

private int actions_ready(mapping debate)
{
    mapping pending;

    if (!mapp(debate))
        return 0;
    pending = debate["pending_actions"];
    return mapp(pending) && mapp(pending["challenger"]) && mapp(pending["target"]);
}

private mapping apply_debate_action(mapping debate, string role, mapping action_record)
{
    mapping actor_scores;
    mapping action_resolution;
    mapping target_scores;
    mapping history_entry;
    string action;
    string target_role;
    object actor;

    actor = query_actor_by_role(debate, role);
    target_role = query_opponent_role(role);
    actor_scores = debate["scores"][role];
    target_scores = debate["scores"][target_role];
    action = mapp(action_record) ? (action_record["action"] || "skip") : "skip";

    history_entry = ([
        "actor": objectp(actor) ? (actor->query_name() || "") : "",
        "action": action,
        "round": debate["round"],
        "skill_id": "",
        "timeout": mapp(action_record) && action_record["timeout"] ? 1 : 0,
        "impact": 0,
    ]);

    if (action == "skip" || !objectp(actor)) {
        debate["history"] += ({ history_entry });
        emit_debate_action_report(actor, debate, history_entry);
        return debate;
    }

    action_resolution = SKILL_BEHAVIOR_D->resolve_debate_action(actor, action);
    history_entry["skill_id"] = action_resolution["skill_id"] || "";
    if (action_resolution["impact"] > 0) {
        target_scores["stability"] -= actor_scores["pressure"] + action_resolution["impact"];
        history_entry["impact"] = actor_scores["pressure"] + action_resolution["impact"];
    }
    actor_scores["pressure"] += action_resolution["pressure_gain"];
    actor_scores["stability"] += action_resolution["stability_gain"];

    debate["scores"][role] = actor_scores;
    debate["scores"][target_role] = target_scores;
    debate["history"] += ({ history_entry });
    emit_debate_action_report(actor, debate, history_entry);
    return debate;
}

private mapping start_next_round(mapping debate)
{
    if (!mapp(debate) || debate["status"] != "active")
        return debate;

    debate["pending_actions"] = ([ ]);
    debate["action_started_at"] = time();
    schedule_timeout(debate);
    record_npc_actions(debate);
    return debate;
}

private mapping settle_round_once(mapping debate)
{
    mapping scores;
    mapping pending;

    if (!mapp(debate) || debate["status"] != "active")
        return debate;

    clear_timeout(debate);
    pending = debate["pending_actions"];
    debate = apply_debate_action(debate, "challenger", pending["challenger"]);
    debate = apply_debate_action(debate, "target", pending["target"]);
    scores = debate["scores"];
    if ((scores["challenger"]["stability"] || 0) <= 0 && (scores["target"]["stability"] || 0) <= 0) {
        debate["status"] = "finished";
        debate["winner"] = "draw";
    } else if ((scores["target"]["stability"] || 0) <= 0) {
        debate["status"] = "finished";
        debate["winner"] = "challenger";
    } else if ((scores["challenger"]["stability"] || 0) <= 0) {
        debate["status"] = "finished";
        debate["winner"] = "target";
    } else if ((debate["round"] || 1) >= MAX_DEBATE_ROUNDS) {
        debate["status"] = "finished";
        debate["winner"] = "draw";
        debate["finish_reason"] = "round_limit";
    }

    if (debate["status"] == "finished") {
        emit_debate_state_report(debate);
        discard_finished_debate(debate);
        return debate;
    }

    debate["round"] = (debate["round"] || 1) + 1;
    debate = start_next_round(debate);
    emit_debate_state_report(debate);
    return debate;
}

private mapping settle_round(mapping debate)
{
    int rounds;

    while (mapp(debate) && debate["status"] == "active"
        && actions_ready(debate) && rounds < MAX_DEBATE_ROUNDS) {
        debate = settle_round_once(debate);
        rounds++;
    }
    return debate;
}

void handle_debate_timeout(string debate_id, int round)
{
    mapping debate;
    string role;

    debate = _debates[debate_id];
    if (!mapp(debate) || debate["status"] != "active" || debate["round"] != round)
        return;

    foreach (role in ({ "challenger", "target" })) {
        if (!mapp(debate["pending_actions"]) || !mapp(debate["pending_actions"][role]))
            record_pending_action(debate, role, "skip", 1);
    }

    debate = settle_round(debate);
    store_active_debate(debate_id, debate);
}

mapping query_player_debate_state(object player)
{
    string debate_id;

    debate_id = query_active_debate_id(player);
    if (debate_id == "")
        return 0;
    return copy(_debates[debate_id]);
}

mapping start_debate(object challenger, object target)
{
    mapping challenger_effects;
    mapping target_effects;
    mapping debate;
    string debate_id;

    if (!objectp(challenger) || !objectp(target) || challenger == target)
        return ([ "ok": 0, "error": "invalid_target" ]);
    if (query_active_debate_id(challenger) != ""
        || query_active_debate_id(target) != "")
        return ([ "ok": 0, "error": "debate_in_progress" ]);

    challenger_effects = NUMERIC_RULE_D->query_numeric_effects(challenger, "combat_attributes", target);
    target_effects = NUMERIC_RULE_D->query_numeric_effects(target, "combat_attributes", challenger);
    debate_id = generate_debate_id();
    debate = ([
        "id": debate_id,
        "mode": "simultaneous",
        "round_policy": "ready_or_timeout",
        "round": 1,
        "challenger": challenger,
        "target": target,
        "status": "active",
        "winner": 0,
        "scores": ([
            "challenger": ([
                "pressure": 10 + (challenger_effects["attack"] || 0),
                "stability": 10 + (challenger_effects["parry"] || 0),
            ]),
            "target": ([
                "pressure": 10 + (target_effects["attack"] || 0),
                "stability": 10 + (target_effects["parry"] || 0),
            ]),
        ]),
        "pending_actions": ([ ]),
        "history": ({ }),
    ]);
    debate = start_next_round(debate);
    _debates[debate_id] = debate;

    msg_send(challenger, sprintf("你向%s发起了文斗。\n", target->query_name() || "对手"));
    msg_send(target, sprintf("%s向你发起了文斗。\n", challenger->query_name() || "对手"));
    emit_debate_state_report(debate);
    if (actions_ready(debate))
        debate = settle_round(debate);
    store_active_debate(debate_id, debate);
    return copy(debate);
}

mapping query_debate_state(string debate_id)
{
    if (!stringp(debate_id) || debate_id == "")
        return 0;

    return mapp(_debates[debate_id])
        && _debates[debate_id]["status"] == "active"
        ? copy(_debates[debate_id]) : 0;
}

mapping choose_action(object player, string action)
{
    mapping debate;
    string actor_role;
    string debate_id;

    if (!objectp(player) || !stringp(action) || action == "")
        return ([ "ok": 0, "error": "invalid_arguments" ]);
    if (!is_valid_action(action))
        return ([ "ok": 0, "error": "invalid_action" ]);

    debate_id = query_active_debate_id(player);
    if (debate_id == "")
        return ([ "ok": 0, "error": "missing_debate" ]);
    debate = _debates[debate_id];
    actor_role = query_role(debate, player);
    if (mapp(debate["pending_actions"]) && mapp(debate["pending_actions"][actor_role]))
        return ([ "ok": 0, "error": "action_already_chosen" ]);

    record_pending_action(debate, actor_role, action, 0);
    record_npc_actions(debate);
    if (actions_ready(debate))
        debate = settle_round(debate);
    else
        emit_debate_state_report(debate);
    store_active_debate(debate_id, debate);
    return copy(debate);
}
