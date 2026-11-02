/**
 * @file    meridian_d.c
 * @brief   经脉成长（时间型纵深·简化版）。
 *
 * 三条经脉各三个穴位，冲穴消耗内力+潜能，失败走火（经脉受损，
 * 需修复费用）；通脉进度存玩家 meridian/state（id→已通穴位数）。
 * 加成在 numeric_rule_d 查询时叠加（数值真源统一入口）。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

#define WIND_FIRE_COST     200

nosave mapping _meridians;

private int load_meridians();

void create()
{
    ::create();
    _meridians = ([ ]);
    if (!load_meridians())
        error("MERIDIAN_CONFIG_INVALID");
    CONFIG_D->compat_register_config_callback("meridians", "on_config_reloaded", this_object());
}

private int load_meridians()
{
    mapping config;
    mapping meridians;

    config = CONFIG_D->query_full_config("meridians");
    if (!mapp(config) || !mapp(config["meridians"]))
        return 0;
    meridians = copy(config["meridians"]);
    _meridians = meridians;
    return 1;
}

void on_config_reloaded(string key, string config_name)
{
    if (key != "meridians" && config_name != "meridians")
        return;
    if (!load_meridians())
        error("MERIDIAN_CONFIG_INVALID");
}

void on_config_reload_rollback(string key, string config_name)
{
    if (key != "meridians" && config_name != "meridians")
        return;
    if (!load_meridians())
        error("MERIDIAN_CONFIG_INVALID");
}

mapping query_meridians()
{
    return copy(_meridians);
}

mapping query_meridian(string meridian_id)
{
    mapping def;

    def = _meridians[meridian_id];
    return mapp(def) ? copy(def) : 0;
}

int query_progress(object player, string meridian_id)
{
    mixed state;

    if (!objectp(player))
        return 0;
    state = player->query("meridian/state");
    return mapp(state) && intp(state[meridian_id]) ? state[meridian_id] : 0;
}

int is_complete(object player, string meridian_id)
{
    mapping def;

    def = query_meridian(meridian_id);
    if (!mapp(def))
        return 0;
    return query_progress(player, meridian_id) >= intp(def["points"]) ? def["points"] : 0;
}

/* 冲穴：消耗内力+潜能，成功进度+1，失败走火（可修复） */
int attempt_charge(object player, string meridian_id)
{
    mapping def;
    mapping state;
    mapping previous_state;
    int progress;
    int mp;
    int potential;
    int cost_mp;
    int cost_potential;
    int roll;
    mapping changes;
    mapping response;
    mapping save_phase;
    mixed save_error;

    if (!objectp(player))
        return 0;
    def = query_meridian(meridian_id);
    if (!mapp(def))
        return -1;
    if (intp(player->query("meridian/wind_fire"))
        && player->query("meridian/wind_fire") > 0)
        return -2;

    progress = query_progress(player, meridian_id);
    if (progress >= intp(def["points"]))
        return -3;

    mp = intp(player->query("combat/mp")) ? player->query("combat/mp") : 0;
    potential = intp(player->query("potential")) ? player->query("potential") : 0;
    cost_mp = intp(def["cost_mp_each"]) ? def["cost_mp_each"] : 100;
    cost_potential = intp(def["cost_potential_each"]) ? def["cost_potential_each"] : 50;
    if (mp < cost_mp || potential < cost_potential)
        return -4;

    if (!PROGRESSION_D->spend_potential(player, cost_potential))
        return -4;
    roll = random(100);
    changes = ([ "combat/mp": mp - cost_mp ]);
    state = player->query("meridian/state");
    previous_state = mapp(state) ? copy(state) : ([ ]);
    if (roll < intp(def["fail_rate"]))
        changes["meridian/wind_fire"] = time();
    else {
        state = copy(previous_state);
        state[meridian_id] = progress + 1;
        changes["meridian/state"] = state;
    }
    response = PLAYER_STATE_D->commit_state_fields(player, changes,
        PLAYER_STATE_D->build_state_idempotency_key(player,
            "meridian/charge", changes));
    if (!mapp(response) || !response["ok"]) {
        PROGRESSION_D->add_potential(player, cost_potential);
        return -4;
    }
    if (!function_exists("save_phases", player))
        save_phase = ([ "outcome": "not_written", "persisted": 0,
            "save_result": 0, "error": "player save phases unavailable" ]);
    else
        save_error = catch(save_phase = player->save_phases());
    if (save_error || !mapp(save_phase)
        || (save_phase["outcome"] != "committed"
            && save_phase["outcome"] != "not_written"))
        return -6;
    if (save_phase["outcome"] == "not_written") {
        if (!PLAYER_STATE_D->commit_state_fields(player,
            ([ "combat/mp": mp, "meridian/state": previous_state ]),
            PLAYER_STATE_D->build_state_idempotency_key(player,
                "meridian/charge_restore", meridian_id)))
            log_file("system/meridian", sprintf(
                "[%s] charge rollback incomplete: player=%s meridian=%s\n",
                ctime(time()), PLAYER_STATE_D->query_player_id(player),
                meridian_id));
        PROGRESSION_D->add_potential(player, cost_potential);
        return -6;
    }
    return roll < intp(def["fail_rate"]) ? -5 : progress + 1;
}

private int refund_wind_fire_repair(object player, string actor_id,
    int incident)
{
    mapping receipt;
    string operation_id;

    operation_id = sprintf("meridian/repair/%s/%d/refund", actor_id,
        incident);
    receipt = ECONOMY_SETTLEMENT_D->settle_money_operation(player,
        "meridian", "repair_refund", "add", WIND_FIRE_COST,
        operation_id);
    if (mapp(receipt) && receipt["ok"])
        return 1;
    return ECONOMY_SETTLEMENT_D->queue_compensation(player,
        "meridian", "repair_refund", actor_id, "wind_fire", incident,
        WIND_FIRE_COST);
}

/* 走火修复：铜钱修复 */
int repair_wind_fire(object player)
{
    mapping receipt;
    string actor_id;
    string operation_id;
    int incident;
    mapping save_phase;
    mixed save_error;

    if (!objectp(player))
        return 0;
    incident = player->query("meridian/wind_fire");
    if (!intp(incident) || incident <= 0)
        return 0;
    actor_id = PLAYER_STATE_D->query_player_id(player);
    operation_id = sprintf("meridian/repair/%s/%d", actor_id, incident);
    receipt = ECONOMY_SETTLEMENT_D->settle_money_operation(player,
        "meridian", "repair", "deduct", WIND_FIRE_COST, operation_id);
    if (!mapp(receipt) || !receipt["ok"])
        return mapp(receipt) && receipt["state"] == "deduct_failed" ? -1 : 0;

    if (!PLAYER_STATE_D->commit_state_deletions(player,
        ({ "meridian/wind_fire" }),
        PLAYER_STATE_D->build_state_idempotency_key(player,
            "meridian/repair_clear", "meridian/wind_fire")))
        return -2;
    if (!function_exists("save_phases", player))
        save_phase = ([ "outcome": "not_written", "persisted": 0,
            "save_result": 0, "error": "player save phases unavailable" ]);
    else
        save_error = catch(save_phase = player->save_phases());
    if (save_error)
        save_phase = ([ "outcome": "unknown", "persisted": -1,
            "save_result": 0, "error": sprintf("%O", save_error) ]);
    if (mapp(save_phase) && save_phase["outcome"] == "committed")
        return 1;
    if (mapp(save_phase)
        && (save_phase["outcome"] == "persisted_receipt_pending"
            || save_phase["outcome"] == "unknown"))
        return -2;

    if (!PLAYER_STATE_D->commit_state_fields(player,
        ([ "meridian/wind_fire": incident ]),
        PLAYER_STATE_D->build_state_idempotency_key(player,
            "meridian/repair_restore", incident)))
        return -2;
    if (!refund_wind_fire_repair(player, actor_id, incident))
        return -2;
    return -2;
}

/* 经脉加成：查询时叠加到基础属性（numeric_rule_d 统一调用） */
mapping query_meridian_bonuses(object player)
{
    mapping result;
    mapping def;
    mapping bonus;
    string *ids;
    int progress;
    int i;

    result = ([ ]);
    if (!objectp(player))
        return result;
    ids = keys(_meridians);
    for (i = 0; i < sizeof(ids); i++) {
        def = _meridians[ids[i]];
        progress = query_progress(player, ids[i]);
        if (progress <= 0)
            continue;
        bonus = def["bonus"];
        if (!mapp(bonus))
            continue;
        foreach (string attr in keys(bonus)) {
            int value;

            value = bonus[attr];
            if (intp(value))
                result[attr] = intp(result[attr]) ? result[attr] + value * progress : value * progress;
        }
    }
    return result;
}
