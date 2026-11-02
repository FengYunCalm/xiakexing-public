/**
 * @file    progression_d.c
 * @brief   成长消耗守护进程,管理 potential 消耗判定。
 */

#pragma strict_types
#pragma save_types

#include <system/globals.h>
#include <system/event_names.h>
#include <system/services.h>

inherit "/lib/framework/daemon";

int can_spend_potential(object player, int amount);
int add_potential(object player, int amount);
int adjust_combat_exp(object player, int amount);
int spend_potential(object player, int amount);
int can_spend_combat_exp(object player, int amount);
int spend_combat_exp(object player, int amount);
int query_player_potential(object player);
int query_player_combat_exp(object player);

private mapping query_progression_config()
{
    mixed config;

    config = CONFIG_D->query_full_config("progression");
    return mapp(config) ? config : ([]);
}

private int query_config_int(mapping config, string section_name, string key, int default_value)
{
    mapping section;
    mixed value;
    int parsed;

    if (!mapp(config))
        return default_value;

    section = config[section_name];
    if (!mapp(section))
        return default_value;

    value = section[key];
    if (intp(value))
        return value;
    if (stringp(value) && sscanf(value, "%d", parsed) == 1)
        return parsed;
    return default_value;
}

private int query_skill_type_multiplier(mapping config, string skill_type)
{
    mapping multipliers;

    multipliers = mapp(config["skill_type_multiplier"]) ? config["skill_type_multiplier"] : ([]);
    if (!stringp(skill_type) || skill_type == "")
        return 1;

    return intp(multipliers[skill_type]) && multipliers[skill_type] > 0
        ? multipliers[skill_type]
        : 1;
}

private int query_progression_action_base_cost(mapping config, string action)
{
    switch (action) {
        case "study":
            return query_config_int(config, "progression", "default_study_cost", 1);
        case "practice":
            return query_config_int(config, "progression", "default_practice_cost", 1);
        case "train":
            return query_config_int(config, "progression", "default_train_cost", 1);
        case "learn":
        default:
            return query_config_int(config, "progression", "default_skill_cost", 1);
    }
}

private int query_progression_action_cost(mapping config, string action,
    string skill_type, int current_level)
{
    int base_cost;
    int level_band;
    int level_step;
    int multiplier;

    base_cost = query_progression_action_base_cost(config, action);
    if (base_cost <= 0)
        base_cost = 1;

    level_step = query_config_int(config, "progression", "skill_level_step", 20);
    if (level_step <= 0)
        level_step = 20;

    multiplier = query_skill_type_multiplier(config, skill_type);
    level_band = current_level > 0 ? current_level / level_step : 0;
    return base_cost * multiplier + level_band;
}

private string query_cost_reason(mapping cost_spec)
{
    string action;
    string skill_id;

    action = mapp(cost_spec) && stringp(cost_spec["action"]) ? cost_spec["action"] : "learn";
    skill_id = mapp(cost_spec) && stringp(cost_spec["skill_id"]) ? cost_spec["skill_id"] : "skill";
    return sprintf("skill_%s:%s", action, skill_id);
}

varargs mapping query_skill_action_cost_spec(object player, string action,
    string skill_id, mapping source, mixed known_context)
{
    mapping skill_def;
    string cost_kind;
    string cost_policy;
    string skill_type;
    int amount;
    int current_level;

    if (!objectp(player) || !stringp(action) || action == ""
        || !stringp(skill_id) || skill_id == "") {
        return ([
            "ok": 0,
            "error": "invalid_arguments",
        ]);
    }

    cost_kind = mapp(source) && stringp(source["cost_kind"])
        ? lower_case(source["cost_kind"])
        : "none";
    cost_policy = mapp(source) && stringp(source["cost_policy"])
        ? lower_case(source["cost_policy"])
        : "none";
    skill_def = SKILL_D->get_skill_definition(skill_id);
    skill_type = mapp(skill_def) && stringp(skill_def["type"])
        ? skill_def["type"]
        : "combat";
    amount = mapp(source) && intp(source["cost_amount"]) ? source["cost_amount"] : 0;

    if (cost_kind == "" || cost_kind == "none" || cost_policy == "none") {
        return ([
            "ok": 1,
            "kind": "none",
            "amount": 0,
            "action": action,
            "skill_id": skill_id,
            "skill_type": skill_type,
            "source_id": mapp(source) ? (source["id"] || "") : "",
            "source_type": mapp(source) ? (source["source_type"] || "") : "",
        ]);
    }

    current_level = mapp(known_context) && intp(known_context["current_level"])
        ? known_context["current_level"]
        : SKILL_D->query_player_skill_level(player, skill_id);
    if (amount <= 0 && cost_policy == "progression_default")
        amount = NUMERIC_RULE_D->query_skill_upgrade_cost(player, skill_id);
    if (amount < 0)
        amount = 0;

    return ([
        "ok": 1,
        "kind": cost_kind,
        "amount": amount,
        "action": action,
        "skill_id": skill_id,
        "skill_type": skill_type,
        "current_level": current_level,
        "source_id": mapp(source) ? (source["id"] || "") : "",
        "source_type": mapp(source) ? (source["source_type"] || "") : "",
    ]);
}

mapping can_pay_skill_cost(object player, mapping cost_spec)
{
    string kind;
    int amount;
    int can_pay;
    mapping guild_status;

    if (!objectp(player) || !mapp(cost_spec) || !cost_spec["ok"])
        return ([ "ok": 0, "error": "invalid_cost_spec" ]);

    kind = cost_spec["kind"] || "none";
    amount = intp(cost_spec["amount"]) ? cost_spec["amount"] : 0;
    if (amount <= 0 || kind == "none")
        return ([ "ok": 1, "kind": kind, "amount": 0 ]);

    switch (kind) {
        case "potential":
            can_pay = can_spend_potential(player, amount);
            return ([
                "ok": can_pay,
                "error": can_pay ? "" : "insufficient_potential",
                "kind": kind,
                "amount": amount,
                "current": query_player_potential(player),
            ]);

        case "combat_exp":
            can_pay = can_spend_combat_exp(player, amount);
            return ([
                "ok": can_pay,
                "error": can_pay ? "" : "insufficient_combat_exp",
                "kind": kind,
                "amount": amount,
                "current": query_player_combat_exp(player),
            ]);

        case "sect_contribution":
            can_pay = SECT_D->query_player_contribution(player) >= amount;
            return ([
                "ok": can_pay,
                "error": can_pay ? "" : "insufficient_sect_contribution",
                "kind": kind,
                "amount": amount,
                "current": SECT_D->query_player_contribution(player),
            ]);

        case "guild_contribution":
            guild_status = GUILD_D->query_player_guild_status(player);
            can_pay = mapp(guild_status) && guild_status["joined"]
                && (guild_status["contribution"] || 0) >= amount;
            return ([
                "ok": can_pay,
                "error": can_pay ? "" : "insufficient_guild_contribution",
                "kind": kind,
                "amount": amount,
                "current": mapp(guild_status)
                    ? (guild_status["contribution"] || 0) : 0,
            ]);

        default:
            return ([ "ok": 0, "error": "unsupported_cost_kind", "kind": kind, "amount": amount ]);
    }
}

mapping spend_skill_cost(object player, mapping cost_spec)
{
    mapping result;
    string kind;
    int amount;
    string reason;

    result = can_pay_skill_cost(player, cost_spec);
    if (!mapp(result) || !result["ok"])
        return result;

    kind = cost_spec["kind"] || "none";
    amount = intp(cost_spec["amount"]) ? cost_spec["amount"] : 0;
    reason = query_cost_reason(cost_spec);
    if (amount <= 0 || kind == "none")
        return ([ "ok": 1, "kind": kind, "amount": 0 ]);

    switch (kind) {
        case "potential":
            if (!spend_potential(player, amount))
                return ([ "ok": 0, "error": "spend_failed", "kind": kind, "amount": amount ]);
            return ([ "ok": 1, "kind": kind, "amount": amount ]);

        case "combat_exp":
            if (!spend_combat_exp(player, amount))
                return ([ "ok": 0, "error": "spend_failed", "kind": kind, "amount": amount ]);
            return ([ "ok": 1, "kind": kind, "amount": amount ]);

        case "sect_contribution":
            return SECT_D->apply_contribution_event(player, -amount, "skill_cost", reason)
                + ([ "kind": kind, "amount": amount ]);

        case "guild_contribution":
            return GUILD_D->apply_contribution_event(player, -amount, "skill_cost", reason)
                + ([ "kind": kind, "amount": amount ]);
    }

    return ([ "ok": 0, "error": "unsupported_cost_kind", "kind": kind, "amount": amount ]);
}

mapping refund_skill_cost(object player, mapping cost_spec)
{
    string kind;
    int amount;
    string reason;

    if (!objectp(player) || !mapp(cost_spec) || !cost_spec["ok"])
        return ([ "ok": 0, "error": "invalid_cost_spec" ]);

    kind = cost_spec["kind"] || "none";
    amount = intp(cost_spec["amount"]) ? cost_spec["amount"] : 0;
    reason = query_cost_reason(cost_spec);
    if (amount <= 0 || kind == "none")
        return ([ "ok": 1, "kind": kind, "amount": 0 ]);

    switch (kind) {
        case "potential":
            return ([ "ok": add_potential(player, amount), "kind": kind, "amount": amount ]);

        case "combat_exp":
            return ([ "ok": adjust_combat_exp(player, amount), "kind": kind, "amount": amount ]);

        case "sect_contribution":
            return SECT_D->apply_contribution_event(player, amount, "skill_refund", reason)
                + ([ "kind": kind, "amount": amount ]);

        case "guild_contribution":
            return GUILD_D->apply_contribution_event(player, amount, "skill_refund", reason)
                + ([ "kind": kind, "amount": amount ]);
    }

    return ([ "ok": 0, "error": "unsupported_cost_kind", "kind": kind, "amount": amount ]);
}

int query_player_potential(object player)
{
    if (!objectp(player))
        return 0;
    return player->query("potential") || 0;
}

int query_player_combat_exp(object player)
{
    if (!objectp(player))
        return 0;
    return player->query("combat_exp") || 0;
}

int can_spend_potential(object player, int amount)
{
    if (!objectp(player) || amount <= 0)
        return 0;
    return query_player_potential(player) >= amount;
}

int add_potential(object player, int amount)
{
    mapping change;
    mapping response;
    int current;

    if (!objectp(player) || amount == 0)
        return 0;
    current = player->query("potential") || 0;
    change = ([ "potential": current + amount ]);
    response = PLAYER_STATE_D->commit_state_fields(player, change,
        PLAYER_STATE_D->build_state_idempotency_key(player,
            "progression/potential", change));
    if (!mapp(response) || !response["ok"])
        return 0;
    EVENT_D->emit(EVENT_PLAYER_PROGRESS_POTENTIAL_CHANGED, ([
        "player_id": PLAYER_STATE_D->query_player_id(player),
        "delta": amount,
        "current": player->query("potential") || 0,
        "timestamp": time()
    ]));
    return 1;
}

int adjust_combat_exp(object player, int amount)
{
    mapping change;
    mapping response;
    int current;

    if (!objectp(player) || amount == 0)
        return 0;
    current = player->query("combat_exp") || 0;
    change = ([ "combat_exp": current + amount ]);
    response = PLAYER_STATE_D->commit_state_fields(player, change,
        PLAYER_STATE_D->build_state_idempotency_key(player,
            "progression/combat_exp", change));
    if (!mapp(response) || !response["ok"])
        return 0;
    if (function_exists("invalidate_xk_vital_maxima_projection", player))
        player->invalidate_xk_vital_maxima_projection(1);
    if (function_exists("invalidate_xk_score_projection", player))
        player->invalidate_xk_score_projection();
    EVENT_D->emit(EVENT_PLAYER_PROGRESS_EXP_CHANGED, ([
        "player_id": PLAYER_STATE_D->query_player_id(player),
        "delta": amount,
        "current": player->query("combat_exp") || 0,
        "timestamp": time()
    ]));
    return 1;
}

int spend_potential(object player, int amount)
{
    if (!can_spend_potential(player, amount))
        return 0;
    return add_potential(player, -amount);
}

int can_spend_combat_exp(object player, int amount)
{
    if (!objectp(player) || amount <= 0)
        return 0;
    return query_player_combat_exp(player) >= amount;
}

int spend_combat_exp(object player, int amount)
{
    if (!can_spend_combat_exp(player, amount))
        return 0;
    return adjust_combat_exp(player, -amount);
}

int grant_combat_exp(object player, int amount)
{
    mapping config;
    int scale;
    int applied;

    if (!objectp(player) || amount == 0)
        return 0;

    config = query_progression_config();
    scale = query_config_int(config, "progression", "combat_exp_gain_scale", 1);
    if (scale <= 0)
        scale = 1;

    applied = amount * scale;
    return adjust_combat_exp(player, applied);
}

int query_skill_cost(object player, string skill_type, int current_level)
{
    mapping config;
    mapping multipliers;
    int base_cost;
    int level_step;
    int multiplier;
    int level_band;

    config = query_progression_config();
    base_cost = query_config_int(config, "progression", "default_skill_cost", 1);
    level_step = query_config_int(config, "progression", "skill_level_step", 20);
    multipliers = mapp(config["skill_type_multiplier"]) ? config["skill_type_multiplier"] : ([]);
    multiplier = 1;
    if (stringp(skill_type) && skill_type != "")
        multiplier = intp(multipliers[skill_type]) ? multipliers[skill_type] : 1;
    if (level_step <= 0)
        level_step = 20;

    level_band = current_level > 0 ? current_level / level_step : 0;
    return base_cost * multiplier + level_band;
}

mapping query_progression_snapshot(object player)
{
    int progression_rank;
    int skill_cap;

    if (!objectp(player))
        return ([]);

    skill_cap = NUMERIC_RULE_D->query_player_exp_skill_cap(player);
    progression_rank = NUMERIC_RULE_D->query_player_progression_rank(player);

    return ([
        "combat_exp": player->query("combat_exp") || 0,
        "potential": player->query("potential") || 0,
        "progression_rank": progression_rank,
        "skill_cap": skill_cap,
        "skills": player->query("skills") || ([]),
        "skill_exp": player->query("skill_exp") || ([])
    ]);
}
