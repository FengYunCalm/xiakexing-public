/**
 * @file    growth_schema_d.c
 * @brief   成长模式守护进程,管理成长节点与阶段定义。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

nosave mapping _growth_nodes;
nosave mixed *_growth_stages;

private int load_growth_schema_from_config();
string resolve_growth_node_id(string node_id);
string resolve_growth_stage_id(string stage_id);

private string *query_supported_growth_categories()
{
    return ({ "progression", "reputation", "sect", "rank" });
}

string *query_growth_categories()
{
    return copy(query_supported_growth_categories());
}

void on_config_reloaded(string key, string config_name)
{
    if (key != "growth" && config_name != "growth")
        return;
    if (!load_growth_schema_from_config())
        error("GROWTH_CONFIG_INVALID");
}

void on_config_reload_rollback(string key, string config_name)
{
    if (key != "growth" && config_name != "growth")
        return;
    if (!load_growth_schema_from_config())
        error("GROWTH_CONFIG_INVALID");
}

string *query_supported_requirement_types()
{
    return ({
        "combat_exp_gte",
        "potential_gte",
        "skill_cap_gte",
        "progression_rank_gte",
        "reputation_gte",
        "sect_joined",
        "sect_id_eq",
        "sect_contribution_gte",
        "sect_rank_gte",
        "rank_present",
        "claimed_node",
    });
}

string *query_supported_reward_keys()
{
    return ({
        "gold",
        "potential",
        "exp",
        "sect_contribution",
        "title",
        "item",
        "item_count",
        "attachments",
    });
}

string *query_growth_groups()
{
    mapping groups;
    mapping node;
    string group_id;

    groups = ([]);
    foreach (node in values(_growth_nodes || ([]))) {
        group_id = mapp(node) ? (node["display_group"] || "") : "";
        if (group_id == "")
            continue;
        groups[group_id] = 1;
    }

    return sort_array(keys(groups), 1);
}

string *query_growth_tags()
{
    mapping tags;
    mapping node;
    mixed tag;

    tags = ([]);
    foreach (node in values(_growth_nodes || ([]))) {
        foreach (tag in node["tags"] || ({ })) {
            if (!stringp(tag) || tag == "")
                continue;
            tags[tag] = 1;
        }
    }

    return sort_array(keys(tags), 1);
}

private mapping copy_growth_node(mapping node)
{
    return mapp(node) ? copy(node) : ([]);
}

private mixed *copy_growth_stage_array(mixed *stages)
{
    mixed *copied;
    mapping stage;

    copied = ({ });
    if (!arrayp(stages))
        return copied;

    foreach (stage in stages) {
        copied += ({ mapp(stage) ? copy(stage) : stage });
    }

    return copied;
}

private int load_growth_schema_from_config()
{
    mapping config;
    mapping node;
    mixed stage;

    config = CONFIG_D->query_full_config("growth");
    if (!mapp(config)
        || !mapp(config["meta"])
        || !intp(config["meta"]["schema_version"])
        || !stringp(config["meta"]["source"]) || config["meta"]["source"] == ""
        || !mapp(config["nodes"]) || !arrayp(config["stages"]))
        return 0;

    foreach (node in values(config["nodes"])) {
        if (!mapp(node) || !stringp(node["source"]) || node["source"] == "")
            return 0;
    }
    foreach (stage in config["stages"]) {
        if (!mapp(stage) || !stringp(stage["source"]) || stage["source"] == "")
            return 0;
    }

    _growth_nodes = copy(config["nodes"]);
    _growth_stages = copy_growth_stage_array(config["stages"]);
    return 1;
}

void create()
{
    ::create();
    CONFIG_D->compat_register_config_callback("growth", "on_config_reloaded", this_object());
    if (!load_growth_schema_from_config())
        error("GROWTH_CONFIG_INVALID");
}

mapping query_growth_node(string node_id)
{
    return copy_growth_node(_growth_nodes[resolve_growth_node_id(node_id)]);
}

string resolve_growth_node_id(string node_id)
{
    mapping node;
    string canonical_id;
    mixed alias;

    if (!stringp(node_id) || node_id == "")
        return "";
    if (mapp(_growth_nodes[node_id]))
        return node_id;

    foreach (canonical_id, node in _growth_nodes || ([])) {
        foreach (alias in node["aliases"] || ({ })) {
            if (alias == node_id)
                return canonical_id;
        }
        foreach (alias in node["deprecated_ids"] || ({ })) {
            if (alias == node_id)
                return canonical_id;
        }
    }

    return "";
}

string resolve_growth_stage_id(string stage_id)
{
    mapping stage;
    mixed alias;

    if (!stringp(stage_id) || stage_id == "")
        return "";

    foreach (stage in _growth_stages || ({ })) {
        if (!mapp(stage))
            continue;
        if ((stage["id"] || "") == stage_id)
            return stage_id;
        foreach (alias in stage["aliases"] || ({ })) {
            if (alias == stage_id)
                return stage["id"] || "";
        }
        foreach (alias in stage["deprecated_ids"] || ({ })) {
            if (alias == stage_id)
                return stage["id"] || "";
        }
    }

    return "";
}

mapping query_all_growth_nodes()
{
    mapping copied;
    string node_id;

    copied = ([]);
    foreach (node_id in keys(_growth_nodes || ([]))) {
        copied[node_id] = copy_growth_node(_growth_nodes[node_id]);
    }

    return copied;
}

string format_growth_requirements(mapping requirements)
{
    string *parts;
    mapping rep_spec;
    string key;
    mixed value;

    if (!mapp(requirements) || sizeof(keys(requirements)) == 0)
        return "无";

    parts = ({ });
    foreach (key, value in requirements) {
        switch (key) {
            case "combat_exp_gte":
                parts += ({ sprintf("战斗经验达到 %d", value) });
                break;
            case "potential_gte":
                parts += ({ sprintf("潜能达到 %d", value) });
                break;
            case "skill_cap_gte":
                parts += ({ sprintf("技能上限达到 %d", value) });
                break;
            case "progression_rank_gte":
                parts += ({ sprintf("成长档位达到 %d", value) });
                break;
            case "reputation_gte":
                rep_spec = value;
                parts += ({ sprintf("声望[%s]达到 %d", rep_spec["scope"] || "unknown", rep_spec["value"] || 0) });
                break;
            case "sect_joined":
                parts += ({ "加入任意门派" });
                break;
            case "sect_id_eq":
                parts += ({ sprintf("门派为 %s", value) });
                break;
            case "sect_contribution_gte":
                parts += ({ sprintf("门派贡献达到 %d", value) });
                break;
            case "sect_rank_gte":
                parts += ({ sprintf("门派身份等级达到 %d", value) });
                break;
            case "rank_present":
                parts += ({ sprintf("进入榜单 %s", value) });
                break;
            case "claimed_node":
                parts += ({ sprintf("已领取节点 %s", value) });
                break;
            default:
                parts += ({ sprintf("未知 requirement 类型 %s", key) });
                break;
        }
    }

    return implode(parts, "；");
}

string format_growth_rewards(mapping rewards)
{
    if (!mapp(rewards) || sizeof(keys(rewards)) == 0)
        return "无";

    return REWARD_FEEDBACK_D->format_reward_summary(rewards);
}

mixed *query_growth_stages()
{
    return copy_growth_stage_array(_growth_stages);
}

mapping validate_growth_schema()
{
    mapping report;
    mapping config;
    mapping meta;
    mapping node;
    mixed stage;
    string node_id;
    string *issues;
    string milestone;
    string key;
    mixed value;

    issues = ({ });
    config = CONFIG_D->query_full_config("growth");
    if (!mapp(config))
        config = ([]);
    meta = mapp(config["meta"]) ? config["meta"] : ([]);
    if (!mapp(config["meta"]))
        issues += ({ "growth 配置缺少 meta。" });
    if (!intp(meta["schema_version"]) || meta["schema_version"] < 1)
        issues += ({ "growth meta.schema_version 必须是正整数。" });
    if (!stringp(meta["source"]) || meta["source"] == "")
        issues += ({ "growth meta.source 必须是非空字符串。" });

    foreach (node_id, node in _growth_nodes || ([])) {
        if (!mapp(node)) {
            issues += ({ "节点定义必须是 mapping。" });
            continue;
        }
        if ((node["id"] || "") != node_id)
            issues += ({ sprintf("节点 %s 的 id 与键不一致。", node_id) });
        if (!arrayp(node["aliases"]))
            issues += ({ sprintf("节点 %s 的 aliases 必须是数组。", node_id) });
        if (!arrayp(node["deprecated_ids"]))
            issues += ({ sprintf("节点 %s 的 deprecated_ids 必须是数组。", node_id) });
        if (!stringp(node["source"]) || node["source"] == "")
            issues += ({ sprintf("节点 %s 缺少 source。", node_id) });
        if (!stringp(node["display_group"]) || node["display_group"] == "")
            issues += ({ sprintf("节点 %s 缺少 display_group。", node_id) });
        if (!arrayp(node["tags"]))
            issues += ({ sprintf("节点 %s 的 tags 必须是数组。", node_id) });
        if (!intp(node["order"]) || node["order"] < 0)
            issues += ({ sprintf("节点 %s 的 order 必须是非负整数。", node_id) });
        if (member_array(node["category"] || "", query_supported_growth_categories()) == -1)
            issues += ({ sprintf("节点 %s 的 category 不受支持。", node_id) });
        if (!stringp(node["title"]) || node["title"] == "")
            issues += ({ sprintf("节点 %s 缺少 title。", node_id) });
        if (!stringp(node["claim_mode"]) || node["claim_mode"] != "manual")
            issues += ({ sprintf("节点 %s 的 claim_mode 只允许 manual。", node_id) });
        if (!mapp(node["requirements"]))
            issues += ({ sprintf("节点 %s 缺少 requirements mapping。", node_id) });
        if (!mapp(node["rewards"]))
            issues += ({ sprintf("节点 %s 缺少 rewards mapping。", node_id) });

        foreach (key, value in node["requirements"] || ([])) {
            if (member_array(key, query_supported_requirement_types()) == -1) {
                issues += ({ sprintf("节点 %s 包含未知 requirement 类型 %s。", node_id, key) });
                continue;
            }
            switch (key) {
                case "combat_exp_gte":
                case "potential_gte":
                case "skill_cap_gte":
                case "progression_rank_gte":
                case "sect_contribution_gte":
                case "sect_rank_gte":
                    if (!intp(value) || value < 0)
                        issues += ({ sprintf("节点 %s 的 %s 必须是非负整数。", node_id, key) });
                    break;
                case "reputation_gte":
                    if (!mapp(value) || !stringp(value["scope"]) || !intp(value["value"]))
                        issues += ({ sprintf("节点 %s 的 reputation_gte 必须包含 scope/value。", node_id) });
                    break;
                case "sect_joined":
                    if (value != 1)
                        issues += ({ sprintf("节点 %s 的 sect_joined 只允许 1。", node_id) });
                    break;
                case "sect_id_eq":
                case "rank_present":
                case "claimed_node":
                    if (!stringp(value) || value == "")
                        issues += ({ sprintf("节点 %s 的 %s 必须是非空字符串。", node_id, key) });
                    break;
            }
        }

        foreach (key, value in node["visible_if"] || ([])) {
            if (member_array(key, query_supported_requirement_types()) == -1)
                issues += ({ sprintf("节点 %s 的 visible_if 包含未知 requirement 类型 %s。", node_id, key) });
        }

        foreach (key, value in node["rewards"] || ([])) {
            if (member_array(key, query_supported_reward_keys()) == -1) {
                issues += ({ sprintf("节点 %s 包含未知奖励键 %s。", node_id, key) });
                continue;
            }
            if ((key == "gold" || key == "potential" || key == "exp" || key == "sect_contribution")
                && (!intp(value) || value < 0))
                issues += ({ sprintf("节点 %s 的奖励 %s 必须是非负整数。", node_id, key) });
        }

        foreach (value in node["tags"] || ({ })) {
            if (!stringp(value) || value == "")
                issues += ({ sprintf("节点 %s 的 tags 只能包含非空字符串。", node_id) });
        }
        foreach (value in node["aliases"] || ({ })) {
            if (!stringp(value) || value == "")
                issues += ({ sprintf("节点 %s 的 aliases 只能包含非空字符串。", node_id) });
        }
        foreach (value in node["deprecated_ids"] || ({ })) {
            if (!stringp(value) || value == "")
                issues += ({ sprintf("节点 %s 的 deprecated_ids 只能包含非空字符串。", node_id) });
        }
    }

    foreach (stage in _growth_stages || ({ })) {
        if (!mapp(stage)) {
            issues += ({ "阶段定义必须是 mapping。" });
            continue;
        }
        if (!stringp(stage["id"]) || stage["id"] == "")
            issues += ({ "阶段定义缺少 id。" });
        if (!arrayp(stage["aliases"]))
            issues += ({ sprintf("阶段 %s 的 aliases 必须是数组。", stage["id"] || "<unknown>") });
        if (!arrayp(stage["deprecated_ids"]))
            issues += ({ sprintf("阶段 %s 的 deprecated_ids 必须是数组。", stage["id"] || "<unknown>") });
        if (!stringp(stage["source"]) || stage["source"] == "")
            issues += ({ sprintf("阶段 %s 缺少 source。", stage["id"] || "<unknown>") });
        if (!intp(stage["order"]) || stage["order"] < 0)
            issues += ({ sprintf("阶段 %s 的 order 必须是非负整数。", stage["id"] || "<unknown>") });
        if (!stringp(stage["title"]) || stage["title"] == "")
            issues += ({ sprintf("阶段 %s 缺少 title。", stage["id"] || "<unknown>") });
        if (!mapp(stage["enter_requirements"]))
            issues += ({ sprintf("阶段 %s 的 enter_requirements 必须是 mapping。", stage["id"] || "<unknown>") });
        foreach (milestone in stage["milestone_nodes"] || ({ })) {
            if (!mapp(_growth_nodes[milestone]))
                issues += ({ sprintf("阶段 %s 引用了不存在的节点 %s。",
                    stage["id"] || "<unknown>", milestone) });
        }
        foreach (value in stage["aliases"] || ({ })) {
            if (!stringp(value) || value == "")
                issues += ({ sprintf("阶段 %s 的 aliases 只能包含非空字符串。", stage["id"] || "<unknown>") });
        }
        foreach (value in stage["deprecated_ids"] || ({ })) {
            if (!stringp(value) || value == "")
                issues += ({ sprintf("阶段 %s 的 deprecated_ids 只能包含非空字符串。", stage["id"] || "<unknown>") });
        }
    }

    report = ([
        "success": sizeof(issues) == 0,
        "issues": issues,
        "node_count": sizeof(keys(_growth_nodes || ([]))),
        "stage_count": sizeof(_growth_stages || ({ })),
    ]);
    return report;
}
