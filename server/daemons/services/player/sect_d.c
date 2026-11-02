/**
 * @file    sect_d.c
 * @brief   门派守护进程,管理门派定义与阶位阶梯。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

nosave mapping _sect_defs;
nosave mapping _rank_ladders;

private int load_sect_config();
mapping query_provider_scope(string provider_id);
mapping query_sect_definition(string sect_id);
string query_player_sect_id(object player);

private string query_rank_ladder_id(string sect_id)
{
    mapping def;
    string ladder_id;

    def = query_sect_definition(sect_id);
    if (!mapp(def))
        return "";

    ladder_id = def["rank_ladder"];

    return stringp(ladder_id) ? ladder_id : "";
}

void on_config_reloaded(string key, string config_name)
{
    if (key != "sect" && config_name != "sect")
        return;

    if (!load_sect_config())
        error("SECT_CONFIG_INVALID");
}

void on_config_reload_rollback(string key, string config_name)
{
    if (key != "sect" && config_name != "sect")
        return;

    if (!load_sect_config())
        error("SECT_CONFIG_INVALID");
}

private mapping query_sect_config()
{
    mapping config;

    config = CONFIG_D->query_full_config("sect");
    return mapp(config) ? config : ([]);
}

private int load_sect_config()
{
    mapping config;

    config = query_sect_config();
    if (!mapp(config["sects"]) || !mapp(config["rank_ladders"]))
        return 0;
    _sect_defs = copy(config["sects"]);
    _rank_ladders = copy(config["rank_ladders"]);
    return 1;
}

private mixed *query_rank_definitions(string sect_id)
{
    string ladder_id;
    mixed *rank_defs;

    ladder_id = query_rank_ladder_id(sect_id);
    rank_defs = mapp(_rank_ladders) ? _rank_ladders[ladder_id] : 0;
    return arrayp(rank_defs) ? rank_defs : ({ });
}

private mapping resolve_rank_definition(string sect_id, int contribution)
{
    mixed *rank_defs;
    mapping current_rank;
    mapping rank_def;

    rank_defs = query_rank_definitions(sect_id);
    if (!arrayp(rank_defs) || sizeof(rank_defs) == 0)
        return 0;

    current_rank = rank_defs[0];
    foreach (rank_def in rank_defs) {
        if (!mapp(rank_def))
            continue;
        if (contribution < (rank_def["min_contribution"] || 0))
            break;
        current_rank = rank_def;
    }

    return current_rank;
}

private mapping resolve_next_rank_definition(string sect_id, int contribution)
{
    mixed *rank_defs;
    mapping rank_def;

    rank_defs = query_rank_definitions(sect_id);
    foreach (rank_def in rank_defs) {
        if (!mapp(rank_def))
            continue;
        if (contribution < (rank_def["min_contribution"] || 0))
            return rank_def;
    }

    return 0;
}

private int commit_player_state_fields(object player, mapping changes,
    string operation)
{
    mapping response;

    if (!objectp(player) || !mapp(changes) || sizeof(changes) == 0)
        return 0;
    response = PLAYER_STATE_D->commit_state_fields(player, changes,
        PLAYER_STATE_D->build_state_idempotency_key(player,
            operation || "sect/state", changes));
    return mapp(response) && response["ok"];
}

private int refresh_player_rank(object player)
{
    mapping changes;
    mapping rank_def;
    string sect_id;
    string rank_name;
    int contribution;
    int rank;
    int current_rank;
    string current_rank_name;

    if (!objectp(player))
        return 0;

    sect_id = query_player_sect_id(player);
    if (sect_id == "")
        return 0;

    contribution = player->query("family/contribution") || 0;
    if (contribution < 0)
        contribution = 0;

    rank_def = resolve_rank_definition(sect_id, contribution);
    if (!mapp(rank_def))
        return 0;

    rank = rank_def["level"] || 1;
    rank_name = rank_def["name"] || "初入山门";
    current_rank = player->query("family/rank") || 1;
    current_rank_name = player->query("family/rank_name") || "初入山门";
    changes = ([ ]);
    if (current_rank != rank)
        changes["family/rank"] = rank;
    if (current_rank_name != rank_name)
        changes["family/rank_name"] = rank_name;
    if (sizeof(changes) == 0)
        return 1;
    if (!commit_player_state_fields(player, changes, "sect/rank"))
        return 0;
    return 1;
}

void create()
{
    ::create();
    if (!mapp(_sect_defs))
        _sect_defs = ([]);
    if (!mapp(_rank_ladders))
        _rank_ladders = ([]);

    load_sect_config();
    CONFIG_D->compat_register_config_callback("sect", "on_config_reloaded", this_object());
}

mapping query_sect_definition(string sect_id)
{
    return mapp(_sect_defs[sect_id]) ? copy(_sect_defs[sect_id]) : 0;
}

string *query_all_sects()
{
    return keys(_sect_defs || ([]));
}

mapping query_provider_scope(string provider_id)
{
    mapping bindings;
    mapping config;

    if (!stringp(provider_id) || provider_id == "")
        return 0;

    config = query_sect_config();
    bindings = mapp(config["provider_bindings"]) ? config["provider_bindings"] : ([]);
    return mapp(bindings[provider_id]) ? copy(bindings[provider_id]) : 0;
}

string query_player_sect_id(object player)
{
    string sect_id;

    if (!objectp(player))
        return "";

    sect_id = player->query("family/id") || "";
    return stringp(sect_id) ? sect_id : "";
}

int query_player_contribution(object player)
{
    int contribution;

    if (!objectp(player))
        return 0;

    contribution = player->query("family/contribution") || 0;
    return contribution > 0 ? contribution : 0;
}

mapping query_player_sect_status(object player)
{
    mapping def;
    mapping next_rank;
    mapping provider_scope;
    string sect_id;
    string provider_scope_name;
    int contribution;

    sect_id = query_player_sect_id(player);
    if (sect_id == "")
        return ([ "joined": 0 ]);

    def = query_sect_definition(sect_id);
    contribution = query_player_contribution(player);
    refresh_player_rank(player);
    next_rank = resolve_next_rank_definition(sect_id, contribution);
    provider_scope = query_provider_scope("public");
    provider_scope_name = mapp(provider_scope) && stringp(provider_scope["name"])
        ? provider_scope["name"]
        : "门派公开入口";

    return ([
        "joined": 1,
        "sect_id": sect_id,
        "sect_name": mapp(def) ? (def["name"] || sect_id) : sect_id,
        "provider_scope_name": provider_scope_name,
        "contribution": contribution,
        "rank": player->query("family/rank") || 1,
        "rank_name": player->query("family/rank_name") || "初入山门",
        "next_rank": mapp(next_rank) ? (next_rank["level"] || 0) : 0,
        "next_rank_name": mapp(next_rank) ? (next_rank["name"] || "") : "",
        "next_rank_contribution": mapp(next_rank) ? (next_rank["min_contribution"] || 0) : 0,
        "next_rank_gap": mapp(next_rank)
            ? ((next_rank["min_contribution"] || 0) - contribution)
            : 0,
    ]);
}

mapping query_player_sect_skill_catalog(object player)
{
    mapping status;

    status = query_player_sect_status(player);
    if (!mapp(status) || !status["joined"])
        return ([]);

    return SECT_SKILL_D->query_sect_skill_catalog(status["sect_id"] || "");
}

mapping query_sect_skill_definition(string sect_id, string sect_skill_id)
{
    return SECT_SKILL_D->query_sect_skill_definition(sect_id, sect_skill_id);
}

mapping apply_contribution_event(object player, int amount, string source, string reason)
{
    mapping changes;
    mapping rank_def;
    int applied_amount;
    int current_contribution;
    int contribution;

    if (!objectp(player) || query_player_sect_id(player) == "")
        return ([ "ok": 0 ]);
    if (!intp(amount) || amount == 0)
        return ([ "ok": 0 ]);

    current_contribution = query_player_contribution(player);
    contribution = current_contribution + amount;
    if (contribution < 0) {
        return ([
            "ok": 0,
            "error": "insufficient_contribution",
            "contribution": current_contribution,
            "applied_amount": 0,
            "source": stringp(source) ? source : "",
            "reason": stringp(reason) ? reason : "",
        ]);
    }

    applied_amount = contribution - current_contribution;
    rank_def = resolve_rank_definition(query_player_sect_id(player), contribution);
    changes = ([ "family/contribution": contribution ]);
    if (stringp(source) && source != "")
        changes["family/last_contribution_source"] = source;
    if (stringp(reason) && reason != "")
        changes["family/last_contribution_reason"] = reason;
    if (mapp(rank_def)) {
        changes["family/rank"] = rank_def["level"] || 1;
        changes["family/rank_name"] = rank_def["name"] || "初入山门";
    }
    if (!commit_player_state_fields(player, changes, "sect/contribution"))
        return ([ "ok": 0, "error": "player_state_commit_failed" ]);
    return ([
        "ok": 1,
        "contribution": contribution,
        "applied_amount": applied_amount,
        "source": stringp(source) ? source : "",
        "reason": stringp(reason) ? reason : "",
    ]);
}

int add_contribution(object player, int amount, string reason)
{
    return apply_contribution_event(player, amount, "legacy_add", reason)["ok"];
}

private string resolve_sect_id(string raw)
{
    string sect_id;
    mapping def;

    if (!stringp(raw) || raw == "")
        return "";

    foreach (sect_id in keys(_sect_defs)) {
        def = _sect_defs[sect_id];
        if (raw == sect_id)
            return sect_id;
        if (mapp(def) && stringp(def["name"]) && def["name"] == raw)
            return sect_id;
    }
    return "";
}

mapping command_join_sect(object player, string raw)
{
    mapping changes;
    mapping def;
    mapping rank_def;
    mapping response;
    string sect_id;
    string current_name;
    int repentant;
    string required_gender;
    string player_gender;

    if (!objectp(player))
        return ([ "success": 0, "message": "无效角色。\n" ]);

    sect_id = resolve_sect_id(raw);
    if (sect_id == "")
        return ([ "success": 0, "message": "没有这个门派。\n" ]);

    current_name = player->query("family/family_name") || "";
    if (current_name != "")
        return ([ "success": 0, "message": "你已经有所属门派了。\n" ]);

    def = _sect_defs[sect_id];
    required_gender = def["require_gender"] || "";
    player_gender = player->query("gender") || "男性";
    if (required_gender != "" && player_gender != required_gender)
        return ([ "success": 0, "message": sprintf("%s当前只收%s。\n", def["name"], required_gender) ]);

    rank_def = resolve_rank_definition(sect_id, 0);
    repentant = sect_id == "liushanmen"
        && (player->query("moral") || 0) < -20;
    changes = ([
        "family/id": sect_id,
        "family/family_name": def["name"],
        "family/contribution": 0,
        "family/rank": mapp(rank_def) ? (rank_def["level"] || 1) : 1,
        "family/rank_name": mapp(rank_def)
            ? (rank_def["name"] || "初入山门") : "初入山门",
        "family/joined_at": time(),
    ]);
    if (repentant) {
        changes["moral"] = (player->query("moral") || 0) + 50;
        changes["sect/repentant"] = 1;
    }
    response = PLAYER_STATE_D->commit_state_fields(player, changes,
        PLAYER_STATE_D->build_state_idempotency_key(player,
            "sect/join", changes));
    if (!mapp(response) || !response["ok"])
        return ([ "success": 0, "message": "门派状态保存失败，请稍后再试。\n" ]);
    /* 投名状欢迎礼：完成入门任务的弟子入门即得 10 点贡献 */
    if (sect_id == "liushanmen"
        && QUEST_D->is_quest_completed(player, "yz_sect_recruit")) {
        apply_contribution_event(player, 10, "sect_recruit_bonus", "recruit_welcome");
    }
    player->schedule_save();

    /* 戴罪入门（六扇门）：恶名玩家投门洗一半恶名，走赎罪线（设计案 2.2） */
    if (repentant)
        return ([ "success": 1, "message":
            "捕头打量你半晌：「牢里老贼荐的人？行，衙门收你。\n"
            "从今日起你的罪先记着，用功劳来赎——你身上的恶名，衙门替你压了一半。\n"
            "记住，六扇门不收吃里扒外的人。」\n" ]);

    return ([ "success": 1, "message": sprintf("你已加入%s。\n", def["name"]) ]);
}
