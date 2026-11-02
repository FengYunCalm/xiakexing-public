/**
 * @file    explore_reward_d.c
 * @brief   探索奖励守护进程,管理探索宝箱定义与个人开启状态。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

private int commit_exploration_state(object player, mapping state,
    string operation)
{
    mapping change;
    mapping response;

    if (!objectp(player) || !mapp(state))
        return 0;
    change = ([ "exploration/chests": state ]);
    response = PLAYER_STATE_D->commit_state_fields(player, change,
        PLAYER_STATE_D->build_state_idempotency_key(player,
            operation || "exploration/chests", change));
    return mapp(response) && response["ok"];
}

private mapping query_chest_definitions()
{
    mapping defs;

    defs = CONFIG_D->query_config("content", "exploration_chests");
    return mapp(defs) ? defs : ([ ]);
}

private mapping query_reward_tables()
{
    mapping tables;

    tables = CONFIG_D->query_config("content", "reward_tables");
    return mapp(tables) ? tables : ([ ]);
}

private mapping query_chest_state(object player)
{
    mapping state;

    if (!objectp(player))
        return ([ ]);
    state = player->query("exploration/chests");
    if (!mapp(state)) {
        state = ([ ]);
        if (!commit_exploration_state(player, state,
            "exploration/init_chests"))
            return ([ ]);
    }
    return state;
}

int query_chest_open_count(object player, string chest_id)
{
    mapping state;
    mapping entry;

    if (!objectp(player) || !stringp(chest_id) || chest_id == "")
        return 0;
    state = player->query("exploration/chests");
    entry = mapp(state) ? state[chest_id] : 0;
    return mapp(entry) ? (entry["count"] || 0) : 0;
}

private mapping build_chest_open_state(object player, string chest_id)
{
    mapping state;
    mapping entry;

    if (!objectp(player) || !stringp(chest_id) || chest_id == "")
        return ([ ]);
    state = copy(query_chest_state(player));
    entry = mapp(state[chest_id]) ? copy(state[chest_id]) : ([ ]);
    entry["count"] = (entry["count"] || 0) + 1;
    entry["opened_at"] = time();
    state[chest_id] = entry;
    return state;
}

private int record_chest_open(object player, string chest_id)
{
    mapping state;

    if (!objectp(player) || !stringp(chest_id) || chest_id == "")
        return 0;
    state = build_chest_open_state(player, chest_id);
    return commit_exploration_state(player, state, "exploration/open_chest");
}

private mapping resolve_chest_rewards(mapping chest_def)
{
    mapping tables;
    mapping rewards;
    string table_id;

    rewards = ([ ]);
    if (!mapp(chest_def))
        return rewards;
    table_id = chest_def["reward_table"];
    tables = query_reward_tables();
    if (stringp(table_id) && mapp(tables[table_id]))
        rewards += copy(tables[table_id]);
    if (mapp(chest_def["rewards"]))
        rewards += copy(chest_def["rewards"]);
    return rewards;
}

private mapping apply_repeatable_chest_reward(object player, string chest_id,
    mapping rewards)
{
    mapping applied;

    applied = REWARD_APPLY_D->apply_reward_package(player, rewards, ([
        "drop_on_full": 1,
        "source": "exploration_chest",
        "target_key": chest_id,
    ]));
    return mapp(applied) && applied["ok"] ? applied
        : ([ "ok": 0, "error": "reward_apply_failed", "applied": applied ]);
}

varargs mapping open_chest(object player, object chest,
    mapping response_context)
{
    mapping defs;
    mapping def;
    mapping rewards;
    mapping applied;
    mapping marker;
    mapping settlement;
    mapping state;
    string chest_id;
    string settlement_key;

    if (!objectp(player) || !objectp(chest))
        return ([ "ok": 0, "message": "你要开启什么？\n" ]);
    chest_id = chest->query("exploration_chest_id") || chest->query("chest_id") || "";
    if (chest_id == "")
        return ([ "ok": 0, "message": "这不是可开启的探索宝箱。\n" ]);
    defs = query_chest_definitions();
    def = defs[chest_id];
    if (!mapp(def))
        return ([ "ok": 0, "message": "这个宝箱没有有效的探索奖励配置。\n" ]);
    if (!def["repeatable"] && query_chest_open_count(player, chest_id) > 0)
        return ([ "ok": 0, "message": "这个宝箱你已经开启过了。\n" ]);

    rewards = resolve_chest_rewards(def);
    if (def["repeatable"]) {
        applied = apply_repeatable_chest_reward(player, chest_id, rewards);
        if (!mapp(applied) || !applied["ok"])
            return ([ "ok": 0, "message": "宝箱奖励结算失败。\n" ]);
        if (!record_chest_open(player, chest_id))
            return ([ "ok": 0, "message": "宝箱开启标记保存失败，请稍后重试。\n" ]);
    } else {
        settlement_key = "explore:" + hash("sha256",
            "chest:" + chest_id);
        settlement = REWARD_SETTLEMENT_D->settle_reward(player,
            settlement_key, rewards, ([
                "source_mode": "exploration",
                "source_content_id": chest_id,
            ]));
        if (!mapp(settlement) || !settlement["ok"])
            return ([ "ok": 0, "message": "宝箱奖励结算失败。\n",
                "settlement": settlement ]);

        state = build_chest_open_state(player, chest_id);
        marker = REWARD_SETTLEMENT_D->commit_claim_marker(player,
            settlement_key, "exploration/chests", state);
        if (!mapp(marker) || !marker["ok"])
            return ([ "ok": 0, "message": "宝箱奖励已结算，开启标记待恢复。\n",
                "settlement": settlement ]);
        applied = settlement["applied"] || ([ ]);
    }

    REWARD_FEEDBACK_D->sync_reward_client_state(player, applied,
        response_context);
    return ([
        "ok": 1,
        "message": "你开启了「" + (def["title"] || chest->query("name") || chest_id) + "」，获得了探索奖励。\n",
        "applied": applied,
        "hide_chest": def["repeatable"] ? 0 : 1,
    ]);
}

mapping query_player_exploration_snapshot(object player)
{
    if (!objectp(player))
        return ([ ]);
    return ([
        "chests": copy(query_chest_state(player)),
        "definitions": query_chest_definitions(),
    ]);
}
