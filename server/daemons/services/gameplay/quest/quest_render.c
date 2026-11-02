/**
 * @file    quest_render.c
 * @brief   任务文本渲染子服务



 */

#pragma strict_types
#pragma save_types

#include <system/services.h>
#include <game/quest.h>

#define QUEST_PROVIDER_BLACKSMITH "blacksmith"
#define QUEST_STEP_MACHINE_HELPER "/daemons/services/gameplay/quest/quest_step_machine"

mapping query_visible_quest_definition(object player, string quest_id);
mapping query_visible_quest_definitions(object player);
mapping query_visible_quests_by_type(object player, string type);
mapping query_visible_quests_by_provider(object player, string provider_id);
mapping query_player_quests_by_status(object player, string status);
mapping query_player_quest_record(object player, string quest_id);
int get_quest_status(object player, string quest_id);
int is_quest_visible_for_player(object player, string quest_id);
int can_accept_quest(object player, string quest_id);
private mapping filter_player_visible_quests(mapping quests, object player);

mixed *get_quest_objectives(object player, string quest_id)
{
    mapping quest_def;
    mapping quest_info;
    mixed *objectives;
    mixed *result;
    int current_progress;
    int index;

    quest_def = query_visible_quest_definition(player, quest_id);
    quest_info = query_player_quest_record(player, quest_id);
    objectives = call_other(QUEST_PROGRESS_D, "build_objectives", quest_def, quest_info);
    if (arrayp(objectives) && sizeof(objectives) > 0)
        return objectives;

    objectives = mapp(quest_def) && arrayp(quest_def["objectives"]) ? quest_def["objectives"] : ({ });
    current_progress = mapp(quest_info) ? (quest_info["progress"] || 0) : 0;
    result = ({ });

    for (index = 0; index < sizeof(objectives); index++) {
        int target_count;
        int current_count;

        target_count = objectives[index]["count"] || 0;
        current_count = current_progress;
        if (mapp(quest_info) &&
            member_array(quest_info["status"], ({ QUEST_STATUS_COMPLETED, QUEST_STATUS_DELIVERED })) != -1 &&
            current_count < target_count) {
            current_count = target_count;
        }

        result += ({ ([
            "desc": objectives[index]["desc"] || "未命名目标",
            "current": current_count,
            "target": target_count,
            "completed": current_count >= target_count
        ]) });
    }

    return result;
}

string get_quest_progress_text(object player, string quest_id)
{
    mixed *objectives;
    mapping objective;
    string text;

    objectives = get_quest_objectives(player, quest_id);
    if (!sizeof(objectives))
        return "无目标";

    text = "";
    foreach (objective in objectives)
        text += sprintf("- %s (%d/%d)\n", objective["desc"], objective["current"], objective["target"]);

    return text;
}

/* "名称 数量"顿连，铜钱=gold 字段展示名。
 * L-143：字段协议与实发共用 reward_feedback_d.project_reward_entries
 * （声望 scope→delta mapping、附件与多物品不再缺席）；与实发的冒号风格
 * 刻意不同，仅拼接风格差异。 */
private string format_quest_reward_line(mapping rewards)
{
    mixed *entries;
    mixed entry;
    string *parts;
    string kind;

    parts = ({ });
    entries = REWARD_FEEDBACK_D->project_reward_entries(rewards);
    if (!arrayp(entries))
        return "";
    foreach (kind in ({ "exp", "potential", "gold", "sect_contribution",
        "reputation", "title", "item", "room_item" })) {
        foreach (entry in entries) {
            if (entry["kind"] != kind)
                continue;
            switch (kind) {
            case "exp":
                parts += ({ sprintf("经验 %d", entry["amount"]) });
                break;
            case "potential":
                parts += ({ sprintf("潜能 %d", entry["amount"]) });
                break;
            case "gold":
                parts += ({ sprintf("铜钱 %d", entry["amount"]) });
                break;
            case "sect_contribution":
                parts += ({ sprintf("门派贡献 %d", entry["amount"]) });
                break;
            case "reputation":
                parts += ({ sprintf("%s %d", entry["label"], entry["amount"]) });
                break;
            case "title":
                parts += ({ sprintf("称号 %s", entry["text"]) });
                break;
            case "item":
                parts += ({ sprintf("%s x%d", entry["text"], entry["count"]) });
                break;
            case "room_item":
                parts += ({ sprintf("%s x%d（掉落在地上）", entry["text"],
                    entry["count"]) });
                break;
            }
        }
    }
    return implode(parts, "，");
}

/* 无任务提示文案单一生产者（do_quest 分流与其它消费方共用）。 */
string query_no_quest_hint()
{
    return "这里暂时没有发布任务。\n";
}

/* 锁定任务不开面板，走飘窗同通道提示。 */
string query_quest_locked_hint()
{
    return "该任务尚未解锁，先推进前置任务后再来。\n";
}

/* 接取反馈任务块（主文本区直显，单一 owner 供 XK/文本模式共用）。
 * 块以分隔线包夹，块内只放任务自身信息；分隔线外不得出现任何任务相关文字。
 * 客户端把分隔行绘制为一整行宽度的横线，并按块统一着色。 */
string render_quest_accept_text(object player, string quest_id)
{
    mapping quest_def;
    mixed *objectives;
    mapping objective;
    string text;
    string reward_line;

    quest_def = query_visible_quest_definition(player, quest_id);
    if (!mapp(quest_def))
        return "";

    text = "-----\n";
    text += sprintf("任务：%s\n",
        stringp(quest_def["name"]) && quest_def["name"] != "" ? quest_def["name"] : "任务");
    text += sprintf("描述：%s\n", stringp(quest_def["description"]) ? quest_def["description"] : "无");
    objectives = get_quest_objectives(player, quest_id);
    if (sizeof(objectives)) {
        text += "目标：\n";
        foreach (objective in objectives)
            text += sprintf("- %s(%d/%d)\n", objective["desc"], objective["current"], objective["target"]);
    }
    reward_line = format_quest_reward_line(quest_def["rewards"]);
    if (reward_line != "")
        text += sprintf("奖励：%s\n", reward_line);
    text += "-----\n";
    return text;
}

/* 失败收尾只报告已到账事实，不依赖可能已经回滚或隐藏的任务状态。 */
string render_quest_reward_text(string quest_name, string *lines)
{
    if (!arrayp(lines) || !sizeof(lines))
        return "";
    return "-----\n任务：" + quest_name + "\n"
        + implode(lines, "\n") + "\n-----\n";
}

/* 进度、完成状态与实际奖励在同一块内按序呈现。 */
varargs string render_quest_step_text(object player, string quest_id,
    string *feedback_lines)
{
    mapping quest_def;
    string text;

    quest_def = query_visible_quest_definition(player, quest_id);
    if (!mapp(quest_def))
        return "";

    text = "-----\n";
    text += sprintf("任务：%s\n",
        stringp(quest_def["name"]) && quest_def["name"] != "" ? quest_def["name"] : "任务");
    text += get_quest_progress_text(player, quest_id);
    if (arrayp(feedback_lines) && sizeof(feedback_lines))
        text += implode(feedback_lines, "\n") + "\n";
    text += "-----\n";
    return text;
}

private string query_status_title(string status)
{
    if (status == "completed")
        return "已完成任务";
    if (status == "abandoned")
        return "失败/放弃任务";
    return "进行中的任务";
}

private string query_status_text(mapping quest_info)
{
    int status;

    status = mapp(quest_info) ? (quest_info["status"] || QUEST_STATUS_AVAILABLE) : QUEST_STATUS_AVAILABLE;
    switch (status) {
        case QUEST_STATUS_ACCEPTED:
            return "进行中";
        case QUEST_STATUS_COMPLETED:
            return "已完成";
        case QUEST_STATUS_DELIVERED:
            return "已交付";
        case QUEST_STATUS_FAILED:
            return quest_info["failure_source"] == "abandon" ? "已放弃" : "已失败";
        default:
            return "可接取";
    }
}

private string query_type_name(string type)
{
    switch (type) {
        case QUEST_TYPE_KILL:
            return "杀怪";
        case QUEST_TYPE_COLLECT:
            return "收集";
        case QUEST_TYPE_TALK:
            return "对话";
        case QUEST_TYPE_EXPLORE:
            return "探索";
        case QUEST_TYPE_ESCORT:
            return "护送";
        case QUEST_TYPE_DELIVERY:
            return "递送";
        case QUEST_TYPE_PUZZLE:
            return "解密";
        default:
            return stringp(type) && type != "" ? type : "未知";
    }
}

private int use_compact_player_copy(object player)
{
    return objectp(player)
        && stringp(player->query_temp("xk_protocol_version"))
        && player->query_temp("xk_protocol_version") != "";
}

#define QUEST_PROGRESS_D        "/daemons/services/gameplay/quest/quest_progress_d"

private string format_reward_text(mapping rewards)
{
    string text;

    if (!mapp(rewards))
        return "无";

    text = REWARD_FEEDBACK_D->format_reward_summary(rewards);
    return text == "无附件" ? "无" : text;
}

private string format_meta_tags(mapping quest_info)
{
    string *tags;

    if (!mapp(quest_info))
        return "";

    tags = ({ });
    if (quest_info["hidden"])
        tags += ({ "隐藏" });
    if (quest_info["type"] == QUEST_TYPE_PUZZLE || mapp(quest_info["puzzle"]))
        tags += ({ "解密" });
    if (stringp(quest_info["chain_id"]) && quest_info["chain_id"] != "")
        tags += ({ "长链" });
    if (quest_info["scope"] == QUEST_SCOPE_WORLD)
        tags += ({ "世界" });
    else if (quest_info["scope"] == QUEST_SCOPE_REGION)
        tags += ({ "区域" });

    return sizeof(tags) ? (" [" + implode(tags, "/") + "]") : "";
}

private string join_rendered_sections(string primary, string secondary,
    int compact_copy)
{
    if (!stringp(primary) || primary == "")
        return stringp(secondary) ? secondary : "";
    if (!stringp(secondary) || secondary == "")
        return primary;
    return primary + (compact_copy ? "\n" : "\n\n") + secondary;
}

private string render_quest_entry(string quest_id, mapping quest_info, int show_id,
    int compact_copy)
{
    string text;
    string quest_name;
    string meta_tags;

    if (!stringp(quest_id) || !mapp(quest_info))
        return "";

    quest_name = stringp(quest_info["name"]) && quest_info["name"] != ""
        ? quest_info["name"]
        : "任务";
    meta_tags = compact_copy ? "" : format_meta_tags(quest_info);
    if (show_id)
        text = sprintf("[%s] %s%s\n", quest_id, quest_name, meta_tags);
    else
        text = sprintf("%s%s\n", quest_name, meta_tags);
    text += sprintf("类型：%s\n", query_type_name(quest_info["type"]));
    text += sprintf("描述：%s\n", quest_info["description"] || "无");
    text += sprintf("奖励：%s\n", format_reward_text(quest_info["rewards"]));
    return text;
}

private string render_player_quests_text_mode(object player, string status,
    int compact_copy)
{
    mapping quests;
    mapping quest_def;
    mapping quest_info;
    string *quest_ids;
    string text;
    string quest_id;

    quests = query_player_quests_by_status(player, status);
    if (!mapp(quests) || sizeof(quests) == 0)
        return sprintf("当前没有%s。", query_status_title(status));

    text = compact_copy ? "" : sprintf("%s：\n", query_status_title(status));
    quest_ids = keys(quests);
    foreach (quest_id in quest_ids) {
        quest_def = query_visible_quest_definition(player, quest_id);
        quest_info = query_player_quest_record(player, quest_id);
        if (compact_copy) {
            text += sprintf("- %s（%s）\n",
                mapp(quest_def) && stringp(quest_def["name"]) && quest_def["name"] != "" ? quest_def["name"] : "任务",
                query_status_text(quest_info));
        } else {
            text += sprintf("- [%s] %s（%s）\n",
                quest_id,
                mapp(quest_def) && stringp(quest_def["name"]) && quest_def["name"] != "" ? quest_def["name"] : "任务",
                query_status_text(quest_info));
        }
    }

    return text;
}

private string render_available_quests_text_mode(string type, object player,
    int compact_copy)
{
    mapping quests;
    string *quest_ids;
    string text;
    string title;
    string quest_id;

    if (stringp(type) && type != "") {
        quests = query_visible_quests_by_type(player, type);
        title = sprintf("%s任务", query_type_name(type));
    } else {
        quests = query_visible_quest_definitions(player);
        title = "所有可用任务";
    }

    quests = filter_player_visible_quests(quests, player);

    if (!mapp(quests) || sizeof(quests) == 0)
        return sprintf("当前没有%s。", title);

    text = compact_copy ? "" : sprintf("%s：\n", title);
    quest_ids = keys(quests);
    foreach (quest_id in quest_ids)
        text += render_quest_entry(quest_id, quests[quest_id], !compact_copy ? 1 : 0,
            compact_copy);

    return text;
}

private string render_quest_info_text_mode(object player, string quest_id,
    int compact_copy)
{
    mapping quest_def;
    mapping quest_info;
    string text;

    if (!stringp(quest_id) || quest_id == "")
        return "请指定任务。";

    quest_def = query_visible_quest_definition(player, quest_id);
    if (!mapp(quest_def) || !is_quest_visible_for_player(player, quest_id))
        return "没有找到该任务。";

    quest_info = query_player_quest_record(player, quest_id);

    /* 详情只留 名称→描述→目标→奖励（用户规格），删 ID/类型/范围/链路/
     * 状态/恢复/重开/谜题行；列表态视图（render_quest_entry）不受影响。 */
    text = compact_copy
        ? sprintf("%s\n", quest_def["name"] || "任务")
        : sprintf("【%s】\n", quest_def["name"] || "任务");
    text += sprintf("描述：%s\n", quest_def["description"] || "无");
    /* 文本模式与 XK 结构化路同格式（desc(p/t) 无前缀 + 名称 数量顿连）。 */
    {
        mixed *accept_objectives;
        mapping accept_objective;

        accept_objectives = get_quest_objectives(player, quest_id);
        if (sizeof(accept_objectives) > 0) {
            text += "目标：\n";
            foreach (accept_objective in accept_objectives)
                text += sprintf("%s(%d/%d)\n", accept_objective["desc"],
                    accept_objective["current"], accept_objective["target"]);
        }
    }
    text += sprintf("奖励：%s\n", format_quest_reward_line(quest_def["rewards"]));
    return text;
}

private string render_provider_quests_text_mode(string provider_id, object player,
    int compact_copy)
{
    mapping quests;
    mapping quest_def;
    mapping quest_info;
    string *quest_ids;
    string text;
    string title;
    string quest_id;
    string quest_name;
    string recovery_text;
    int status;
    int has_rendered;

    quests = query_visible_quests_by_provider(player, provider_id);
    if (!mapp(quests) || sizeof(quests) == 0)
        return "这里暂时没有发布任务。";

    if (provider_id == QUEST_PROVIDER_BLACKSMITH)
        title = "张铁匠的活计";
    else
        title = "任务列表";
    text = compact_copy ? "" : sprintf("%s：\n", title);
    recovery_text = "";
    quest_ids = keys(quests);
    has_rendered = 0;
    foreach (quest_id in quest_ids) {
        quest_def = quests[quest_id];
        if (!mapp(quest_def))
            continue;
        if (member_array("released", keys(quest_def)) != -1 && quest_def["released"] == 0)
            continue;
        quest_name = stringp(quest_def["name"]) && quest_def["name"] != "" ? quest_def["name"] : "任务";
        quest_info = query_player_quest_record(player, quest_id);
        if (mapp(quest_info) && quest_info["status"] == QUEST_STATUS_FAILED) {
            has_rendered = 1;
            if (recovery_text == "") {
                recovery_text = compact_copy
                    ? "当前有可恢复的任务进度。\n"
                    : "当前有可恢复的任务进度：\n";
            }
            if (quest_info["recoverable"]) {
                recovery_text += compact_copy
                    ? sprintf("%s：可恢复之前的进度。\n", quest_name)
                    : sprintf("- %s：可恢复之前的进度\n", quest_name);
            }
            recovery_text += compact_copy
                ? sprintf("%s：也可以重新接取。\n", quest_name)
                : sprintf("- %s：也可以重新接取\n", quest_name);
            continue;
        }

        status = get_quest_status(player, quest_id);
        if (status == QUEST_STATUS_DELIVERED)
            continue;

        if (status == QUEST_STATUS_ACCEPTED) {
            has_rendered = 1;
            text += compact_copy
                ? sprintf("%s：已接取，可查看详情继续推进。\n", quest_name)
                : sprintf("- %s：已接取，可查看详情继续推进\n", quest_name);
            continue;
        }
        if (status == QUEST_STATUS_COMPLETED) {
            has_rendered = 1;
            text += compact_copy
                ? sprintf("%s：可交付。\n", quest_name)
                : sprintf("- %s：可交付\n", quest_name);
            continue;
        }
        if (!is_quest_visible_for_player(player, quest_id)) {
            has_rendered = 1;
            text += compact_copy
                ? sprintf("%s：尚未解锁，继续推进前置任务。\n", quest_name)
                : sprintf("- %s：尚未解锁，继续推进前置任务\n", quest_name);
            continue;
        }

        text += render_quest_entry(quest_id, quests[quest_id], !compact_copy ? 1 : 0,
            compact_copy);
        has_rendered = 1;
    }

    if (!has_rendered
        && (!stringp(recovery_text) || recovery_text == ""))
        return "这里暂时没有可展示的任务。";

    if (stringp(recovery_text) && recovery_text != "")
        text = join_rendered_sections(text, recovery_text, compact_copy);
    return text;
}

private mapping filter_player_visible_quests(mapping quests, object player)
{
    mapping filtered;
    string quest_id;

    filtered = copy(quests || ([]));
    if (!mapp(filtered))
        return ([]);

    foreach (quest_id in keys(filtered)) {
        if (get_quest_status(player, quest_id) != QUEST_STATUS_AVAILABLE
            || !is_quest_visible_for_player(player, quest_id))
            map_delete(filtered, quest_id);
    }
    return filtered;
}

private string render_specific_progress_summary(object player, mapping quests)
{
    string *quest_ids;
    string text;
    string quest_id;

    if (!mapp(quests) || sizeof(quests) == 0)
        return "";

    text = "【你当前相关的任务】\n";
    quest_ids = keys(quests);
    foreach (quest_id in quest_ids) {
        text += sprintf("- %s\n", quests[quest_id]["name"] || "任务");
        text += get_quest_progress_text(player, quest_id);
    }

    return text;
}

string render_player_quests_text(object player, string status)
{
    return render_player_quests_text_mode(player, status,
        use_compact_player_copy(player));
}

string render_player_quests_dialog_text(object player, string status)
{
    return render_player_quests_text_mode(player, status, 1);
}

string render_available_quests_text(string type, object player)
{
    return render_available_quests_text_mode(type, player,
        use_compact_player_copy(player));
}

string render_available_quests_dialog_text(string type, object player)
{
    return render_available_quests_text_mode(type, player, 1);
}

string render_quest_info_text(object player, string quest_id)
{
    return render_quest_info_text_mode(player, quest_id,
        use_compact_player_copy(player));
}

string render_quest_info_dialog_text(object player, string quest_id)
{
    return render_quest_info_text_mode(player, quest_id, 1);
}

string render_provider_quests_text(string provider_id, object player)
{
    return render_provider_quests_text_mode(provider_id, player,
        use_compact_player_copy(player));
}

string render_provider_quests_dialog_text(string provider_id, object player)
{
    return render_provider_quests_text_mode(provider_id, player, 1);
}
