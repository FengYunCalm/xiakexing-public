/**
 * @file    quest_settlement_d.c
 * @brief   任务结算守护进程,应用任务奖励 spec 到统一奖励链。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

mapping query_quest_reward_owner_route(string key)
{
    mapping route;
    string route_key;
    mixed err;

    route_key = stringp(key) && key != "" ? key : "quest_reward";
    err = catch(route = OWNER_RUNTIME_D->query_service_owner_route("quest_reward", route_key));
    if (err || !mapp(route) || route["ok"] != 1)
        return ([
            "ok": 0,
            "service": "quest_reward",
            "key": route_key,
            "failure_reason": err ? sprintf("owner route query failed: %O", err)
                : (mapp(route) ? route["failure_reason"] : "owner route unavailable"),
            "readiness": mapp(route) ? route["readiness"] : ([ "ok": 0 ]),
            "dependencies": mapp(route) ? route["dependencies"] : ([ ]),
            "direct_cross_owner_write": 0,
        ]);

    route["commit_boundary"] = "service_owner_reward_commit";
    route["direct_cross_owner_write"] = 0;
    return route;
}

mapping query_quest_reward_owner_status()
{
    mapping route;

    route = query_quest_reward_owner_route("quest_reward");
    if (!mapp(route) || route["ok"] != 1)
        return route;
    return ([
        "service_map_v2": 1,
        "service": "quest_reward",
        "policy": route["policy"],
        "owner_id": route["owner_id"],
        "commit_boundary_required": 1,
        "commit_boundary": route["commit_boundary"],
        "service_shard_owner": route["service_shard_owner"] ? 1 : 0,
        "service_shard_key_required": route["service_shard_key_required"] ? 1 : 0,
        "keyed_service_shard_ready": route["keyed_service_shard_ready"] ? 1 : 0,
        "hot_path_service_owner_single_point": 0,
        "message_only_cross_owner": 1,
        "direct_cross_owner_write": 0,
    ]);
}

private string query_settlement_key(object player, string quest_id,
    mapping quest_state)
{
    string player_id;
    string acceptance_id;
    int acceptance_revision;

    player_id = objectp(player) ? PLAYER_STATE_D->query_player_id(player) : "";
    acceptance_id = mapp(quest_state) ? (quest_state["acceptance_id"] || "") : "";
    acceptance_revision = mapp(quest_state)
        ? (quest_state["acceptance_revision"] || 0) : 0;
    return "quest:" + hash("sha256",
        "player:" + player_id + ":quest:" + quest_id
        + ":acceptance:" + acceptance_id
        + ":revision:" + acceptance_revision);
}

/* 在实际到账时固定任务身份与奖励文案，避免后续堆叠或其他任务覆盖反馈。 */
private void notify_reward_client_state(object player, string quest_id,
    string quest_name, mapping applied)
{
    mixed *queue;

    if (!objectp(player) || !mapp(applied) || sizeof(applied) == 0)
        return;
    queue = player->query_temp("quest/pending_reward_feedback");
    if (!arrayp(queue))
        return;
    queue += ({ ([
        "quest_id": quest_id,
        "quest_name": quest_name,
        "applied": applied,
        "lines": REWARD_FEEDBACK_D->build_reward_gain_lines(applied),
    ]) });
    player->set_temp("quest/pending_reward_feedback", queue);
}

varargs int settle_turn_in(object player, string quest_id, mapping quest_def,
    mapping quest_state)
{
    mapping policy;
    mapping rewards;
    mapping settlement;
    string settlement_key;

    if (!objectp(player) || !stringp(quest_id) || quest_id == "" || !mapp(quest_def))
        return 0;
    if (!arrayp(player->query_temp("quest/pending_reward_feedback")))
        return 0;

    rewards = quest_def["rewards"];
    if (!mapp(rewards))
        return 1;

    policy = CONTENT_MODE_POLICY_D->compile_content_policy("quest", quest_def);
    if (!mapp(policy) || !policy["ok"])
        return 0;
    if (policy["state_scope"] != "character")
        return 0;
    settlement_key = query_settlement_key(player, quest_id, quest_state);
    settlement = REWARD_SETTLEMENT_D->settle_reward(player, settlement_key,
        rewards, ([
            "source_content_id": quest_id,
            "player_id": PLAYER_STATE_D->query_player_id(player),
            "acceptance_id": mapp(quest_state)
                ? (quest_state["acceptance_id"] || "") : "",
            "acceptance_revision": mapp(quest_state)
                ? (quest_state["acceptance_revision"] || 0) : 0,
            "binding_policy": "character",
            "usage_modes": copy(policy["usage_modes"]),
        ]));
    if (!mapp(settlement) || !settlement["ok"])
        return 0;
    if (!settlement["already_settled"])
        notify_reward_client_state(player, quest_id,
            stringp(quest_def["name"]) ? quest_def["name"] : quest_id,
            settlement["applied"] || ([ ]));
    return 1;
}

int settle_abandon(object player, string quest_id, mapping quest_def)
{
    if (!objectp(player) || !stringp(quest_id) || quest_id == "")
        return 0;
    return 1;
}
