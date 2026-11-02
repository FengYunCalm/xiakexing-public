#pragma strict_types

#include <system/ansi.h>
#include <system/services.h>

inherit "/lib/framework/command";
inherit "/lib/std/command_utils";

int main(object me, string arg);
int help(object me);

private string query_node_status_text(mapping node)
{
    if (!mapp(node))
        return "未知";
    if (node["claimed"])
        return "已领取";
    if (node["claimable"])
        return "可领取";
    if (node["unlocked"])
        return "已解锁";
    return "未解锁";
}

private string build_growth_status_text(object me)
{
    mapping snapshot;
    mapping context;
    mapping meta;
    mapping stage;
    string *categories;
    string *groups;
    string *tags;
    int history_count;
    mixed *stage_nodes;
    mixed *goals;
    mapping stage_node;
    mapping goal;
    string output;

    snapshot = GROWTH_D->query_growth_framework_snapshot(me);
    meta = snapshot["meta"] || ([]);
    context = snapshot["context"] || ([]);
    stage = snapshot["stage"] || ([]);
    categories = snapshot["categories"] || ({ });
    groups = snapshot["groups"] || ({ });
    tags = snapshot["tags"] || ({ });
    history_count = snapshot["history_count"] || 0;
    stage_nodes = snapshot["stage_nodes"] || ({ });
    goals = snapshot["next_goals"] || ({ });

    output = HIC "╔════════════ 成长总览 ════════════╗" NOR "\n";
    output += sprintf(HIC "║" NOR " 框架版本：%-28s" HIC "║" NOR "\n",
        meta["version"] || "unknown");
    output += sprintf(HIC "║" NOR " 当前阶段：%-28s" HIC "║" NOR "\n",
        stage["title"] || "初入江湖");
    output += sprintf(HIC "║" NOR " 战斗经验：%-28d" HIC "║" NOR "\n",
        context["combat_exp"] || 0);
    output += sprintf(HIC "║" NOR " 当前潜能：%-28d" HIC "║" NOR "\n",
        context["potential"] || 0);
    output += sprintf(HIC "║" NOR " 当前门派：%-28s" HIC "║" NOR "\n",
        mapp(context["sect"]) && context["sect"]["joined"]
            ? (context["sect"]["sect_name"] || "已加入门派")
            : "未加入门派");
    output += sprintf(HIC "║" NOR " 分类数量：%-28d" HIC "║" NOR "\n",
        sizeof(categories));
    output += sprintf(HIC "║" NOR " 分组数量：%-28d" HIC "║" NOR "\n",
        sizeof(groups));
    output += sprintf(HIC "║" NOR " 标签数量：%-28d" HIC "║" NOR "\n",
        sizeof(tags));
    output += sprintf(HIC "║" NOR " 领取记录：%-28d" HIC "║" NOR "\n",
        history_count);
    output += HIC "╠════════════ 当前阶段里程碑 ════════════╣" NOR "\n";
    if (!arrayp(stage_nodes) || sizeof(stage_nodes) == 0) {
        output += sprintf(HIC "║" NOR " %-36s" HIC "║" NOR "\n", "当前阶段没有定义里程碑节点");
    } else {
        foreach (stage_node in stage_nodes) {
            output += sprintf(HIC "║" NOR " %-28s [%s]" HIC "║" NOR "\n",
                stringp(stage_node["title"]) ? stage_node["title"] : (stage_node["id"] || "未知节点"),
                query_node_status_text(stage_node));
        }
    }
    output += HIC "╠════════════ 下一步目标 ════════════╣" NOR "\n";
    if (!arrayp(goals) || sizeof(goals) == 0) {
        output += sprintf(HIC "║" NOR " %-36s" HIC "║" NOR "\n", "当前没有待推进的成长目标");
    } else {
        foreach (goal in goals) {
            output += sprintf(HIC "║" NOR " %-36s" HIC "║" NOR "\n",
                stringp(goal["title"]) ? goal["title"] : (goal["id"] || "未知节点"));
        }
    }
    output += HIC "╚══════════════════════════════════╝" NOR "\n";
    return output;
}

private int do_meta(object me)
{
    mapping meta;
    string output;

    meta = GROWTH_D->query_growth_meta();
    output = HIC "【成长元信息】" NOR "\n";
    output += sprintf("版本：%s\n", meta["version"] || "unknown");
    output += sprintf("Schema版本：%d\n", meta["schema_version"] || 0);
    output += sprintf("来源：%s\n", meta["source"] || "unknown");
    output += sprintf("标题：%s\n", meta["title"] || "未命名");
    output += sprintf("摘要：%s\n", meta["summary"] || "无");
    output += sprintf("节点数：%d\n", meta["node_count"] || 0);
    output += sprintf("阶段数：%d\n", meta["stage_count"] || 0);
    msg_send(me, output);
    return 1;
}

private int do_history(object me)
{
    mixed *history;
    mapping entry;
    string output;

    history = GROWTH_D->query_growth_claim_history(me);
    output = HIC "【成长领取记录】" NOR "\n";
    if (!arrayp(history) || sizeof(history) == 0) {
        output += "暂无领取记录\n";
        msg_send(me, output);
        return 1;
    }

    foreach (entry in history) {
        output += sprintf("- %s [%s] 奖励:%s @ %d\n",
            entry["title"] || entry["node_id"] || "unknown",
            entry["category"] || "unknown",
            entry["reward_text"] || "无",
            entry["claimed_at"] || 0);
    }
    msg_send(me, output);
    return 1;
}

private int do_detail(object me, string node_id)
{
    mapping detail;
    string output;

    detail = GROWTH_D->query_growth_node_detail(me, node_id);
    if (!mapp(detail) || sizeof(detail) == 0) {
        msg_send(me, "没有这个成长节点。\n");
        return 1;
    }

    output = HIC "【成长节点详情】" NOR "\n";
    output += sprintf("标题：%s\n", detail["title"] || detail["id"] || "未知节点");
    output += sprintf("分类：%s\n", detail["category"] || "unknown");
    output += sprintf("分组：%s\n", detail["display_group"] || "unknown");
    output += sprintf("标签：%s\n", implode(detail["tags"] || ({ }), "、"));
    output += sprintf("别名：%s\n", implode(detail["aliases"] || ({ }), "、"));
    output += sprintf("废弃ID：%s\n", implode(detail["deprecated_ids"] || ({ }), "、"));
    output += sprintf("来源：%s\n", detail["source"] || "unknown");
    output += sprintf("状态：%s\n", query_node_status_text(detail));
    output += sprintf("前置：%s\n", detail["requirement_text"] || "无");
    if (stringp(detail["gap_text"]) && detail["gap_text"] != "")
        output += sprintf("缺口：%s\n", detail["gap_text"]);
    output += sprintf("可见条件：%s\n", detail["visible_if_text"] || "无");
    output += sprintf("奖励：%s\n", detail["reward_text"] || "无");
    if (stringp(detail["description"]) && detail["description"] != "")
        output += sprintf("说明：%s\n", detail["description"]);
    msg_send(me, output);
    return 1;
}

private int do_stages(object me)
{
    mixed *stages;
    mapping stage;
    string output;

    stages = GROWTH_D->query_growth_stages_list();
    output = HIC "【成长阶段】" NOR "\n";
    foreach (stage in stages) {
        output += sprintf("- %s (%s)\n",
            stage["title"] || "未知阶段",
            stage["id"] || "unknown");
    }

    msg_send(me, output);
    return 1;
}

private int do_stage_detail(object me, string stage_id)
{
    mapping stage;
    string output;

    stage = GROWTH_D->query_growth_stage_detail(stage_id);
    if (!mapp(stage) || sizeof(stage) == 0) {
        msg_send(me, "没有这个成长阶段。\n");
        return 1;
    }

    output = HIC "【成长阶段详情】" NOR "\n";
    output += sprintf("标题：%s\n", stage["title"] || stage_id);
    output += sprintf("阶段ID：%s\n", stage["id"] || stage_id);
    output += sprintf("别名：%s\n", implode(stage["aliases"] || ({ }), "、"));
    output += sprintf("废弃ID：%s\n", implode(stage["deprecated_ids"] || ({ }), "、"));
    output += sprintf("来源：%s\n", stage["source"] || "unknown");
    output += sprintf("摘要：%s\n", stage["summary"] || "无");
    output += sprintf("里程碑：%s\n", implode(stage["milestone_nodes"] || ({ }), "、"));
    msg_send(me, output);
    return 1;
}

private int do_categories(object me)
{
    string *categories;
    string output;

    categories = GROWTH_D->query_growth_categories();
    output = HIC "【成长分类】" NOR "\n" + implode(categories || ({ }), "\n") + "\n";
    msg_send(me, output);
    return 1;
}

private int do_groups(object me)
{
    string *groups;
    string output;

    groups = GROWTH_D->query_growth_groups();
    output = HIC "【成长分组】" NOR "\n" + implode(groups || ({ }), "\n") + "\n";
    msg_send(me, output);
    return 1;
}

private int do_tags(object me)
{
    string *tags;
    string output;

    tags = GROWTH_D->query_growth_tags();
    output = HIC "【成长标签】" NOR "\n" + implode(tags || ({ }), "\n") + "\n";
    msg_send(me, output);
    return 1;
}

private int do_taxonomy(object me)
{
    mapping taxonomy;
    string output;
    string key;

    taxonomy = GROWTH_D->query_growth_taxonomy_snapshot(me);
    output = HIC "【成长Taxonomy】" NOR "\n";
    output += "分类：\n";
    foreach (key in sort_array(keys(taxonomy["categories"] || ([])), 1)) {
        output += sprintf("- %s: %d\n", key, taxonomy["categories"][key]);
    }
    output += "分组：\n";
    foreach (key in sort_array(keys(taxonomy["groups"] || ([])), 1)) {
        output += sprintf("- %s: %d\n", key, taxonomy["groups"][key]);
    }
    output += "标签：\n";
    foreach (key in sort_array(keys(taxonomy["tags"] || ([])), 1)) {
        output += sprintf("- %s: %d\n", key, taxonomy["tags"][key]);
    }
    msg_send(me, output);
    return 1;
}

private int do_group(object me, string group_id)
{
    mixed *nodes;
    mapping node;
    string output;

    nodes = GROWTH_D->query_growth_nodes_by_group(me, group_id);
    output = HIC "【成长分组 - " + group_id + "】" NOR "\n";
    foreach (node in nodes) {
        output += sprintf("- %s [%s]\n",
            node["title"] || node["id"] || "未知节点",
            query_node_status_text(node));
    }
    if (sizeof(nodes) == 0)
        output += "当前没有匹配节点\n";
    msg_send(me, output);
    return 1;
}

private int do_tag(object me, string tag)
{
    mixed *nodes;
    mapping node;
    string output;

    nodes = GROWTH_D->query_growth_nodes_by_tag(me, tag);
    output = HIC "【成长标签 - " + tag + "】" NOR "\n";
    foreach (node in nodes) {
        output += sprintf("- %s [%s]\n",
            node["title"] || node["id"] || "未知节点",
            query_node_status_text(node));
    }
    if (sizeof(nodes) == 0)
        output += "当前没有匹配节点\n";
    msg_send(me, output);
    return 1;
}

private int do_nodes(object me, string category)
{
    mixed *nodes;
    mapping node;
    string output;

    nodes = GROWTH_D->query_growth_nodes_by_category(me, category);
    output = stringp(category) && category != ""
        ? HIC "【成长节点 - " + category + "】" NOR "\n"
        : HIC "【成长节点】" NOR "\n";
    foreach (node in nodes) {
        output += sprintf("- %s [%s]\n",
            node["title"] || node["id"] || "未知节点",
            query_node_status_text(node));
    }

    msg_send(me, output);
    return 1;
}

private int do_claim(object me, string node_id)
{
    mapping result;
    mapping response_context;

    response_context = CLIENT_SYNC_D->capture_command_response_context(me);
    result = GROWTH_D->command_claim_growth_reward(me, node_id,
        response_context);
    msg_send(me, mapp(result) ? result["message"] : "成长奖励领取失败。\n",
        response_context);
    return 1;
}

private int do_export(object me, string rest)
{
    mapping export_snapshot;
    mapping parsed;
    mapping filters;

    filters = ([]);
    if (stringp(rest) && rest != "") {
        parsed = parse_command_args(rest, "%s %s");
        if (!parsed["valid"]) {
            msg_send(me, "用法：growth export [category|group|tag] <值>\n");
            return 1;
        }
        if (member_array(parsed["args"][0], ({ "category", "group", "tag" })) == -1) {
            msg_send(me, "导出过滤只支持 category/group/tag。\n");
            return 1;
        }
        filters[parsed["args"][0]] = parsed["args"][1];
    }

    export_snapshot = GROWTH_D->query_growth_export_snapshot(me, filters);
    msg_send(me, sprintf("growth export: version=%s visible=%d claimable=%d history=%d filters=%d\n",
        export_snapshot["export_version"] || "unknown",
        sizeof(export_snapshot["visible_nodes"] || ({ })),
        sizeof(export_snapshot["claimable_nodes"] || ({ })),
        sizeof(export_snapshot["history"] || ({ })),
        sizeof(export_snapshot["filters"] || ([]))));
    return 1;
}

int main(object me, string arg)
{
    mapping parsed;

    if (!objectp(me))
        return 0;
    if (!stringp(arg) || arg == "") {
        msg_send(me, build_growth_status_text(me));
        return 1;
    }

    parsed = parse_subcommand_args(arg);
    if (!parsed["valid"])
        return help(me);

    switch (parsed["action"]) {
        case "status":
            msg_send(me, build_growth_status_text(me));
            return 1;
        case "meta":
            return do_meta(me);
        case "export":
            return do_export(me, parsed["rest"]);
        case "history":
            return do_history(me);
        case "categories":
            return do_categories(me);
        case "taxonomy":
            return do_taxonomy(me);
        case "groups":
            return do_groups(me);
        case "tags":
            return do_tags(me);
        case "group":
            return do_group(me, parsed["rest"]);
        case "tag":
            return do_tag(me, parsed["rest"]);
        case "stage":
            return do_stage_detail(me, parsed["rest"]);
        case "stages":
            return do_stages(me);
        case "nodes":
            return do_nodes(me, parsed["rest"]);
        case "detail":
            return do_detail(me, parsed["rest"]);
        case "claim":
            return do_claim(me, parsed["rest"]);
        default:
            return help(me);
    }
}

int help(object me)
{
    msg_send(me, "用法：growth [status|meta|export|history|taxonomy|categories|groups|tags|stages|stage <阶段ID>|group <组ID>|tag <标签>|nodes [category]|detail <节点ID>|claim <节点ID>]\n");
    return 1;
}
