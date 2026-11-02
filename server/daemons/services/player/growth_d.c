/**
 * @file    growth_d.c
 * @brief   成长守护进程,构建成长命令结果与成长结算。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

private mapping build_command_result(int success, string message, mapping extra)
{
    mapping result;

    result = ([
        "success": success ? 1 : 0,
        "message": stringp(message) ? message : "",
    ]);
    if (mapp(extra))
        result += extra;
    return result;
}

mixed *query_next_growth_goals(object player);
mixed *query_growth_stage_nodes(object player);
mixed *query_growth_stages_list();
string *query_growth_categories();
string *query_growth_groups();
string *query_growth_tags();
mapping query_growth_taxonomy_snapshot(object player);
mapping query_growth_stage_detail(string stage_id);
string resolve_growth_stage_id(string stage_id);
mixed *query_growth_nodes_by_category(object player, string category);
mixed *query_growth_nodes_by_group(object player, string group_id);
mixed *query_growth_nodes_by_tag(object player, string tag);
mixed *query_growth_requirement_gaps(mapping context, mapping requirements);
mapping query_growth_meta();
mixed *query_growth_claim_history(object player);
mapping query_growth_framework_snapshot(object player);
mapping query_growth_export_snapshot(object player, mapping filters);
mapping query_growth_node_detail(object player, string node_id);

private mapping query_rankings_context(object player)
{
    mapping snapshot;

    snapshot = RANK_D->query_player_rank_snapshot(player);
    return mapp(snapshot) ? snapshot : ([]);
}

private mapping query_claimed_growth_nodes(object player)
{
    mapping claimed;

    if (!objectp(player))
        return ([]);

    claimed = player->query("growth/claimed_nodes");
    return mapp(claimed) ? copy(claimed) : ([]);
}

mapping query_growth_meta()
{
    mapping config;
    mapping meta;
    mapping nodes;
    mixed *stages;

    config = CONFIG_D->query_full_config("growth");
    if (!mapp(config) || !mapp(config["meta"]))
        return ([
            "node_count": 0,
            "stage_count": 0,
            "error_code": "GROWTH_CONFIG_UNAVAILABLE",
        ]);
    meta = copy(config["meta"]);
    nodes = mapp(config["nodes"]) ? config["nodes"] : ([]);
    stages = arrayp(config["stages"]) ? config["stages"] : ({ });

    meta["node_count"] = sizeof(keys(nodes));
    meta["stage_count"] = sizeof(stages);
    return meta;
}

string resolve_growth_node_id(string node_id)
{
    return GROWTH_SCHEMA_D->resolve_growth_node_id(node_id);
}

string resolve_growth_stage_id(string stage_id)
{
    return GROWTH_SCHEMA_D->resolve_growth_stage_id(stage_id);
}

mixed *query_growth_claim_history(object player)
{
    mapping claimed;
    mapping all_nodes;
    mapping node;
    mixed *history;
    mapping entry;
    string node_id;

    claimed = query_claimed_growth_nodes(player);
    all_nodes = GROWTH_SCHEMA_D->query_all_growth_nodes();
    history = ({ });
    foreach (node_id in sort_array(keys(claimed), 1)) {
        node = all_nodes[node_id];
        entry = ([]);
        entry["node_id"] = node_id;
        entry["title"] = mapp(node) ? (node["title"] || node_id) : node_id;
        entry["category"] = mapp(node) ? (node["category"] || "unknown") : "unknown";
        entry["reward_text"] = mapp(node)
            ? GROWTH_SCHEMA_D->format_growth_rewards(node["rewards"] || ([]))
            : "无";
        entry["claimed_at"] = claimed[node_id];
        history += ({ entry });
    }

    history = sort_array(history, (:
        ($1["claimed_at"] || 0) == ($2["claimed_at"] || 0)
            ? strcmp($1["node_id"] || "", $2["node_id"] || "")
            : (($1["claimed_at"] || 0) > ($2["claimed_at"] || 0) ? -1 : 1)
    :) );

    return history;
}

private int query_claimed_growth_node(object player, string node_id)
{
    mapping claimed;
    string resolved_id;

    claimed = query_claimed_growth_nodes(player);
    resolved_id = resolve_growth_node_id(node_id);
    return stringp(resolved_id) && resolved_id != "" && intp(claimed[resolved_id]) && claimed[resolved_id] > 0;
}

private mapping settle_growth_claim(object player, string resolved_id,
    mapping rewards, int repeatable)
{
    mapping claimed;
    mapping marker;
    mapping settlement;
    string settlement_key;
    int claim_epoch;

    if (!objectp(player) || !stringp(resolved_id) || resolved_id == ""
        || !mapp(rewards))
        return ([ "ok": 0, "error": "invalid_growth_claim" ]);

    claim_epoch = repeatable ? time() / 86400 : 0;
    settlement_key = "growth:" + hash("sha256", sprintf("%s:%d",
        resolved_id, claim_epoch));
    settlement = REWARD_SETTLEMENT_D->settle_reward(player, settlement_key,
        rewards, ([
            "source_mode": "growth",
            "source_content_id": resolved_id,
            "resolution_token": sprintf("%d", claim_epoch),
        ]));
    if (!mapp(settlement) || !settlement["ok"])
        return settlement;

    claimed = query_claimed_growth_nodes(player);
    claimed[resolved_id] = time();
    marker = REWARD_SETTLEMENT_D->commit_claim_marker(player,
        settlement_key, "growth/claimed_nodes", claimed);
    if (!mapp(marker) || !marker["ok"])
        return marker;
    return settlement + ([ "marker": marker ]);
}

private int requirement_is_satisfied(mapping context, string key, mixed value)
{
    mapping rep_spec;
    mapping sect_status;
    mapping rankings;
    string scope;

    if (!mapp(context) || !stringp(key) || key == "")
        return 0;

    switch (key) {
        case "combat_exp_gte":
            return (context["combat_exp"] || 0) >= value;
        case "potential_gte":
            return (context["potential"] || 0) >= value;
        case "skill_cap_gte":
            return (context["skill_cap"] || 0) >= value;
        case "progression_rank_gte":
            return (context["progression_rank"] || 0) >= value;
        case "reputation_gte":
            rep_spec = value;
            if (!mapp(rep_spec))
                return 0;
            scope = rep_spec["scope"] || "";
            return (context["reputation"][scope] || 0) >= (rep_spec["value"] || 0);
        case "sect_joined":
            sect_status = context["sect"];
            return mapp(sect_status) && sect_status["joined"];
        case "sect_id_eq":
            sect_status = context["sect"];
            return mapp(sect_status) && (sect_status["sect_id"] || "") == value;
        case "sect_contribution_gte":
            sect_status = context["sect"];
            return mapp(sect_status) && (sect_status["contribution"] || 0) >= value;
        case "sect_rank_gte":
            sect_status = context["sect"];
            return mapp(sect_status) && (sect_status["rank"] || 0) >= value;
        case "rank_present":
            rankings = context["rankings"];
            return mapp(rankings) && intp(rankings[value]) && rankings[value] > 0;
        case "claimed_node":
            return mapp(context["claimed_nodes"]) && context["claimed_nodes"][value];
    }

    return 0;
}

private int requirements_satisfied(mapping context, mapping requirements)
{
    string key;
    mixed value;

    if (!mapp(requirements) || sizeof(keys(requirements)) == 0)
        return 1;

    foreach (key, value in requirements) {
        if (!requirement_is_satisfied(context, key, value))
            return 0;
    }

    return 1;
}

mixed *query_growth_requirement_gaps(mapping context, mapping requirements)
{
    mixed *gaps;
    string key;
    mixed value;

    gaps = ({ });
    if (!mapp(requirements) || sizeof(keys(requirements)) == 0)
        return gaps;

    foreach (key, value in requirements) {
        if (requirement_is_satisfied(context, key, value))
            continue;
        gaps += ({ ([
            "key": key,
            "value": value,
            "text": GROWTH_SCHEMA_D->format_growth_requirements(([ key: value ])),
        ]) });
    }

    return gaps;
}

private mapping decorate_growth_node(mapping node, mapping context)
{
    mapping decorated;
    mixed *gaps;
    string *gap_texts;
    int visible;
    int unlocked;
    int claimed;
    int claimable;

    decorated = copy(node);
    visible = requirements_satisfied(context, decorated["visible_if"] || ([]));
    unlocked = visible && requirements_satisfied(context, decorated["requirements"] || ([]));
    claimed = query_claimed_growth_node(context["player"], decorated["id"] || "");
    claimable = unlocked && !claimed;
    gaps = query_growth_requirement_gaps(context, decorated["requirements"] || ([]));
    gap_texts = map(gaps, (: mapp($1) ? ($1["text"] || "") : "" :));

    decorated["visible"] = visible;
    decorated["unlocked"] = unlocked;
    decorated["claimed"] = claimed;
    decorated["claimable"] = claimable;
    decorated["missing_requirements"] = gaps;
    decorated["gap_text"] = sizeof(gap_texts) > 0 ? implode(gap_texts, "；") : "";
    return decorated;
}

private mapping export_growth_node(mapping node)
{
    mapping exported;

    if (!mapp(node))
        return ([]);

    exported = ([
        "id": node["id"] || "",
        "title": node["title"] || "",
        "category": node["category"] || "",
        "display_group": node["display_group"] || "",
        "tags": arrayp(node["tags"]) ? node["tags"] : ({ }),
        "aliases": arrayp(node["aliases"]) ? node["aliases"] : ({ }),
        "deprecated_ids": arrayp(node["deprecated_ids"]) ? node["deprecated_ids"] : ({ }),
        "description": node["description"] || "",
        "source": node["source"] || "",
        "order": node["order"] || 0,
        "status": node["claimed"] ? "claimed" : (node["claimable"] ? "claimable" : (node["unlocked"] ? "unlocked" : "locked")),
        "gap_text": node["gap_text"] || "",
        "requirement_text": node["requirement_text"] || "",
        "visible_if_text": node["visible_if_text"] || "",
        "reward_text": node["reward_text"] || "",
    ]);
    return exported;
}

private mapping export_growth_stage(mapping stage)
{
    mapping exported;

    if (!mapp(stage))
        return ([]);

    exported = ([
        "id": stage["id"] || "",
        "title": stage["title"] || "",
        "aliases": arrayp(stage["aliases"]) ? stage["aliases"] : ({ }),
        "deprecated_ids": arrayp(stage["deprecated_ids"]) ? stage["deprecated_ids"] : ({ }),
        "source": stage["source"] || "",
        "order": stage["order"] || 0,
        "summary": stage["summary"] || "",
        "milestone_nodes": arrayp(stage["milestone_nodes"]) ? stage["milestone_nodes"] : ({ }),
    ]);
    return exported;
}

private int export_node_matches_filters(mapping node, mapping filters)
{
    if (!mapp(node) || !mapp(filters) || sizeof(keys(filters)) == 0)
        return 1;

    if (stringp(filters["category"]) && filters["category"] != ""
        && (node["category"] || "") != filters["category"])
        return 0;
    if (stringp(filters["group"]) && filters["group"] != ""
        && (node["display_group"] || "") != filters["group"])
        return 0;
    if (stringp(filters["tag"]) && filters["tag"] != ""
        && member_array(filters["tag"], node["tags"] || ({ })) == -1)
        return 0;

    return 1;
}

private mixed *filter_export_nodes(mixed *nodes, mapping filters)
{
    mixed *filtered;
    mapping node;

    if (!arrayp(nodes))
        return ({ });
    if (!mapp(filters) || sizeof(keys(filters)) == 0)
        return nodes;

    filtered = ({ });
    foreach (node in nodes) {
        if (export_node_matches_filters(node, filters))
            filtered += ({ node });
    }

    return filtered;
}

private int query_growth_order_value(mapping entry)
{
    if (!mapp(entry) || !intp(entry["order"]))
        return 999999;
    return entry["order"];
}

private string query_growth_entry_id(mapping entry)
{
    if (!mapp(entry) || !stringp(entry["id"]))
        return "";
    return entry["id"];
}

private mixed *sort_growth_nodes(mixed *nodes)
{
    if (!arrayp(nodes))
        return ({ });

    return sort_array(nodes, (:
        query_growth_order_value($1) == query_growth_order_value($2)
            ? strcmp(query_growth_entry_id($1), query_growth_entry_id($2))
            : (query_growth_order_value($1) < query_growth_order_value($2) ? -1 : 1)
    :) );
}

private mixed *sort_growth_stages(mixed *stages)
{
    if (!arrayp(stages))
        return ({ });

    return sort_array(stages, (:
        query_growth_order_value($1) == query_growth_order_value($2)
            ? strcmp(query_growth_entry_id($1), query_growth_entry_id($2))
            : (query_growth_order_value($1) < query_growth_order_value($2) ? -1 : 1)
    :) );
}

mapping query_growth_context(object player)
{
    mapping progression;
    mapping reputation;
    mapping sect_status;
    mapping context;

    if (!objectp(player))
        return ([]);

    progression = PROGRESSION_D->query_progression_snapshot(player);
    reputation = REPUTATION_D->query_reputation_snapshot(player);
    sect_status = SECT_D->query_player_sect_status(player);

    context = ([
        "player": player,
        "player_id": PLAYER_STATE_D->query_player_id(player),
        "combat_exp": progression["combat_exp"] || 0,
        "potential": progression["potential"] || 0,
        "progression_rank": progression["progression_rank"] || 1,
        "skill_cap": progression["skill_cap"] || 0,
        "reputation": reputation,
        "sect": sect_status,
        "rankings": query_rankings_context(player),
        "claimed_nodes": query_claimed_growth_nodes(player),
        "flags": ([]),
    ]);
    return context;
}

mixed *query_visible_growth_nodes(object player)
{
    mapping context;
    mapping nodes;
    mapping decorated;
    string node_id;
    mixed *visible_nodes;

    context = query_growth_context(player);
    nodes = GROWTH_SCHEMA_D->query_all_growth_nodes();
    visible_nodes = ({ });

    foreach (node_id in sort_array(keys(nodes), 1)) {
        decorated = decorate_growth_node(nodes[node_id], context);
        if (decorated["visible"])
            visible_nodes += ({ decorated });
    }

    return sort_growth_nodes(visible_nodes);
}

mixed *query_growth_nodes_by_category(object player, string category)
{
    mixed *visible_nodes;
    mixed *filtered;
    mapping node;

    visible_nodes = query_visible_growth_nodes(player);
    if (!stringp(category) || category == "")
        return visible_nodes;

    filtered = ({ });
    foreach (node in visible_nodes) {
        if (mapp(node) && (node["category"] || "") == category)
            filtered += ({ node });
    }

    return filtered;
}

string *query_growth_categories()
{
    return GROWTH_SCHEMA_D->query_growth_categories();
}

string *query_growth_groups()
{
    return GROWTH_SCHEMA_D->query_growth_groups();
}

string *query_growth_tags()
{
    return GROWTH_SCHEMA_D->query_growth_tags();
}

mapping query_growth_taxonomy_snapshot(object player)
{
    mixed *visible_nodes;
    mapping category_counts;
    mapping group_counts;
    mapping tag_counts;
    mapping node;
    mixed tag;

    visible_nodes = query_visible_growth_nodes(player);
    category_counts = ([]);
    group_counts = ([]);
    tag_counts = ([]);

    foreach (node in visible_nodes) {
        if (!mapp(node))
            continue;
        if (stringp(node["category"]) && node["category"] != "")
            category_counts[node["category"]] = (category_counts[node["category"]] || 0) + 1;
        if (stringp(node["display_group"]) && node["display_group"] != "")
            group_counts[node["display_group"]] = (group_counts[node["display_group"]] || 0) + 1;
        foreach (tag in node["tags"] || ({ })) {
            if (!stringp(tag) || tag == "")
                continue;
            tag_counts[tag] = (tag_counts[tag] || 0) + 1;
        }
    }

    return ([
        "categories": category_counts,
        "groups": group_counts,
        "tags": tag_counts,
    ]);
}

mixed *query_growth_nodes_by_group(object player, string group_id)
{
    mixed *visible_nodes;
    mixed *filtered;
    mapping node;

    visible_nodes = query_visible_growth_nodes(player);
    if (!stringp(group_id) || group_id == "")
        return ({ });

    filtered = ({ });
    foreach (node in visible_nodes) {
        if (mapp(node) && (node["display_group"] || "") == group_id)
            filtered += ({ node });
    }

    return filtered;
}

mixed *query_growth_nodes_by_tag(object player, string tag)
{
    mixed *visible_nodes;
    mixed *filtered;
    mapping node;

    visible_nodes = query_visible_growth_nodes(player);
    if (!stringp(tag) || tag == "")
        return ({ });

    filtered = ({ });
    foreach (node in visible_nodes) {
        if (mapp(node) && member_array(tag, node["tags"] || ({ })) != -1)
            filtered += ({ node });
    }

    return filtered;
}

mixed *query_unlocked_growth_nodes(object player)
{
    mixed *visible_nodes;
    mixed *unlocked_nodes;
    mapping node;

    visible_nodes = query_visible_growth_nodes(player);
    unlocked_nodes = ({ });
    foreach (node in visible_nodes) {
        if (mapp(node) && node["unlocked"])
            unlocked_nodes += ({ node });
    }

    return unlocked_nodes;
}

mixed *query_claimable_growth_nodes(object player)
{
    mixed *unlocked_nodes;
    mixed *claimable_nodes;
    mapping node;

    unlocked_nodes = query_unlocked_growth_nodes(player);
    claimable_nodes = ({ });
    foreach (node in unlocked_nodes) {
        if (mapp(node) && node["claimable"])
            claimable_nodes += ({ node });
    }

    return claimable_nodes;
}

mapping query_growth_stage(object player)
{
    mapping context;
    mapping current_stage;
    mapping stage;
    mixed *stages;

    context = query_growth_context(player);
    stages = GROWTH_SCHEMA_D->query_growth_stages();
    current_stage = ([
        "id": "growth_stage_beginner",
        "title": "初入江湖",
        "summary": "正在建立最基础的成长认知。",
        "milestone_nodes": ({ }),
    ]);

    stages = sort_growth_stages(stages);
    foreach (stage in stages) {
        if (!mapp(stage))
            continue;
        if (!requirements_satisfied(context, stage["enter_requirements"] || ([])))
            continue;
        current_stage = copy(stage);
    }

    return current_stage;
}

mixed *query_growth_stages_list()
{
    return sort_growth_stages(GROWTH_SCHEMA_D->query_growth_stages());
}

mapping query_growth_stage_detail(string stage_id)
{
    mapping stage;
    mixed *stages;
    string resolved_id;

    if (!stringp(stage_id) || stage_id == "")
        return ([]);

    resolved_id = resolve_growth_stage_id(stage_id);
    if (resolved_id == "")
        return ([]);

    stages = query_growth_stages_list();
    foreach (stage in stages) {
        if (mapp(stage) && (stage["id"] || "") == resolved_id)
            return copy(stage);
    }

    return ([]);
}

mixed *query_growth_stage_nodes(object player)
{
    mapping context;
    mapping current_stage;
    mapping all_nodes;
    mapping node;
    mixed *stage_nodes;
    string node_id;

    context = query_growth_context(player);
    current_stage = query_growth_stage(player);
    all_nodes = GROWTH_SCHEMA_D->query_all_growth_nodes();
    stage_nodes = ({ });

    foreach (node_id in current_stage["milestone_nodes"] || ({ })) {
        node = all_nodes[node_id];
        if (!mapp(node))
            continue;
        stage_nodes += ({ decorate_growth_node(node, context) });
    }

    return sort_growth_nodes(stage_nodes);
}

mapping query_growth_framework_snapshot(object player)
{
    mapping context;
    mapping stage;
    mixed *visible_nodes;
    mixed *claimable_nodes;
    mixed *goals;
    mixed *stage_nodes;

    context = query_growth_context(player);
    stage = query_growth_stage(player);
    visible_nodes = query_visible_growth_nodes(player);
    claimable_nodes = query_claimable_growth_nodes(player);
    goals = query_next_growth_goals(player);
    stage_nodes = query_growth_stage_nodes(player);

    return ([
        "meta": query_growth_meta(),
        "context": context,
        "stage": stage,
        "categories": query_growth_categories(),
        "groups": query_growth_groups(),
        "tags": query_growth_tags(),
        "history_count": sizeof(query_growth_claim_history(player)),
        "stage_nodes": stage_nodes,
        "visible_nodes": visible_nodes,
        "claimable_nodes": claimable_nodes,
        "next_goals": goals,
    ]);
}

mapping query_growth_export_snapshot(object player, mapping filters)
{
    mapping snapshot;
    mixed *visible_nodes;
    mixed *claimable_nodes;
    mixed *stage_nodes;
    mixed *next_goals;
    mixed *history;

    snapshot = query_growth_framework_snapshot(player);
    visible_nodes = map(snapshot["visible_nodes"] || ({ }), (: export_growth_node($1) :));
    claimable_nodes = map(snapshot["claimable_nodes"] || ({ }), (: export_growth_node($1) :));
    stage_nodes = map(snapshot["stage_nodes"] || ({ }), (: export_growth_node($1) :));
    next_goals = map(snapshot["next_goals"] || ({ }), (: export_growth_node($1) :));
    history = query_growth_claim_history(player);

    visible_nodes = filter_export_nodes(visible_nodes, filters);
    claimable_nodes = filter_export_nodes(claimable_nodes, filters);
    stage_nodes = filter_export_nodes(stage_nodes, filters);
    next_goals = filter_export_nodes(next_goals, filters);

    return ([
        "export_version": "growth_export_v1",
        "generated_at": time(),
        "player_id": objectp(player) ? PLAYER_STATE_D->query_player_id(player) : "",
        "filters": mapp(filters) ? copy(filters) : ([]),
        "meta": snapshot["meta"] || ([]),
        "stage": export_growth_stage(snapshot["stage"] || ([])),
        "categories": snapshot["categories"] || ({ }),
        "groups": snapshot["groups"] || ({ }),
        "tags": snapshot["tags"] || ({ }),
        "visible_nodes": visible_nodes,
        "claimable_nodes": claimable_nodes,
        "stage_nodes": stage_nodes,
        "next_goals": next_goals,
        "history": history,
        "history_count": snapshot["history_count"] || 0,
    ]);
}

mapping query_growth_node_detail(object player, string node_id)
{
    mapping context;
    mapping node;
    mapping decorated;
    string resolved_id;

    if (!objectp(player) || !stringp(node_id) || node_id == "")
        return ([]);

    resolved_id = resolve_growth_node_id(node_id);
    node = GROWTH_SCHEMA_D->query_growth_node(resolved_id);
    if (!mapp(node) || sizeof(node) == 0)
        return ([]);

    context = query_growth_context(player);
    decorated = decorate_growth_node(node, context);
    decorated["query_id"] = node_id;
    decorated["requirement_text"] = GROWTH_SCHEMA_D->format_growth_requirements(node["requirements"] || ([]));
    decorated["visible_if_text"] = GROWTH_SCHEMA_D->format_growth_requirements(node["visible_if"] || ([]));
    decorated["reward_text"] = GROWTH_SCHEMA_D->format_growth_rewards(node["rewards"] || ([]));
    return decorated;
}

mixed *query_next_growth_goals(object player)
{
    mixed *visible_nodes;
    mixed *goals;
    mapping node;

    visible_nodes = query_visible_growth_nodes(player);
    goals = ({ });
    foreach (node in visible_nodes) {
        if (!mapp(node))
            continue;
        if (!node["claimable"] && !node["claimed"])
            goals += ({ node });
        if (sizeof(goals) >= 3)
            break;
    }

    if (sizeof(goals) == 0)
        return query_claimable_growth_nodes(player);
    return goals;
}

varargs mapping command_claim_growth_reward(object player, string node_id,
    mapping response_context)
{
    mapping node;
    mapping decorated;
    mapping context;
    mapping settlement;
    string resolved_id;

    if (!objectp(player) || !stringp(node_id) || node_id == "")
        return build_command_result(0, "无效的成长节点。\n", 0);

    resolved_id = resolve_growth_node_id(node_id);
    node = GROWTH_SCHEMA_D->query_growth_node(resolved_id);
    if (!mapp(node) || sizeof(node) == 0)
        return build_command_result(0, "没有这个成长节点。\n", 0);

    context = query_growth_context(player);
    decorated = decorate_growth_node(node, context);
    if (!decorated["visible"])
        return build_command_result(0, "当前还无法查看这个成长节点。\n", 0);
    if (!decorated["unlocked"])
        return build_command_result(0, "当前条件尚未满足，无法领取该成长奖励。\n", 0);
    if (decorated["claimed"] && !decorated["repeatable"])
        return build_command_result(0, "该成长奖励已经领取过了。\n", 0);

    settlement = settle_growth_claim(player, resolved_id,
        decorated["rewards"] || ([]), decorated["repeatable"] ? 1 : 0);
    if (!mapp(settlement) || !settlement["ok"])
        return build_command_result(0, "成长奖励领取失败。\n", ([
            "settlement": settlement,
        ]));

    REWARD_FEEDBACK_D->sync_reward_client_state(player,
        settlement["applied"] || ([]), response_context);
    return build_command_result(1,
        REWARD_FEEDBACK_D->format_reward_notice("领取成长奖励成功：", decorated["rewards"] || ([])),
        ([ "node": decorated, "applied": settlement["applied"] || ([])]));
}
