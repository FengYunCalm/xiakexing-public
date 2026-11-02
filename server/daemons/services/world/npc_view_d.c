/**
 * @file    npc_view_d.c
 * @brief   NPC 展示守护进程,provider interaction 等统一展示入口。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>
#include <game/quest.h>

inherit "/lib/framework/daemon";

varargs int present_provider_interaction(object player, object npc, string provider_id,
    string default_title, string default_text, string dialog_id, mapping extra);
varargs int present_provider_panel(object player, object npc, string provider_id,
    string default_title, string dialog_id, mapping extra);

private mapping *append_dialog_action(mapping *actions, string label, string cmd, string kind, string role)
{
    mapping action;

    if (!arrayp(actions))
        actions = ({ });
    if (!stringp(label) || label == "" || !stringp(cmd) || cmd == "")
        return actions;

    foreach (action in actions) {
        if (mapp(action) && action["cmd"] == cmd)
            return actions;
    }

    action = ([
        "label": label,
        "cmd": cmd,
        "kind": kind,
        "surface": "quest",
        "role": role,
        "target_group": "quest",
    ]);
    return actions + ({ action });
}

private mapping build_dialog_action_group(string id, string title, mapping *actions)
{
    if (!arrayp(actions) || sizeof(actions) == 0)
        return 0;
    return ([
        "id": id,
        "title": title,
        "surface": id,
        "actions": actions,
    ]);
}

private string quest_display_name(mapping quest_info)
{
    if (mapp(quest_info) && stringp(quest_info["name"]) && quest_info["name"] != "")
        return quest_info["name"];
    return "任务";
}

private mapping *build_provider_dialog_actions(object player, string provider_id,
    mapping snapshot)
{
    mapping *actions;
    mapping quests;
    mapping quest_info;
    string quest_id;

    actions = ({ });
    if (!objectp(player) || !stringp(provider_id) || provider_id == "" || !mapp(snapshot))
        return actions;

    quests = snapshot["completed"];
    if (mapp(quests)) {
        foreach (quest_id in sort_array(keys(quests), 1)) {
            string quest_name;

            quest_name = quest_display_name(quests[quest_id]);
            actions = append_dialog_action(actions, "详情 " + quest_name,
                "quest info " + quest_id, "quest_info", "secondary");
            actions = append_dialog_action(actions, "交付 " + quest_name,
                "quest complete " + quest_id, "quest_complete", "primary");
        }
    }

    quests = snapshot["active"];
    if (mapp(quests)) {
        foreach (quest_id in sort_array(keys(quests), 1)) {
            string quest_name;

            quest_name = quest_display_name(quests[quest_id]);
            actions = append_dialog_action(actions, "详情 " + quest_name,
                "quest info " + quest_id, "quest_info", "secondary");
        }
    }

    quests = snapshot["failed"];
    if (mapp(quests)) {
        foreach (quest_id in sort_array(keys(quests), 1)) {
            string quest_name;

            quest_name = quest_display_name(quests[quest_id]);
            actions = append_dialog_action(actions, "详情 " + quest_name,
                "quest info " + quest_id, "quest_info", "secondary");
            quest_info = QUEST_D->query_player_quest_runtime(player, quest_id);
            if (!mapp(quest_info) || !quest_info["recoverable"])
                continue;
            actions = append_dialog_action(actions, "恢复 " + quest_name,
                "quest recover " + quest_id, "quest_recover", "secondary");
        }
    }

    quests = snapshot["available"];
    if (mapp(quests)) {
        foreach (quest_id in sort_array(keys(quests), 1)) {
            string quest_name;

            quest_name = quest_display_name(quests[quest_id]);
            actions = append_dialog_action(actions, "详情 " + quest_name,
                "quest info " + quest_id, "quest_info", "secondary");
            actions = append_dialog_action(actions, "接取 " + quest_name,
                "quest accept " + quest_id, "quest_accept", "primary");
        }
    }

    return actions;
}

private mapping build_dialog_extra(string source, string provider_id, string bucket,
    object player, mapping snapshot)
{
    mapping payload;
    mapping *actions;

    payload = DIALOG_PROJECTION_D->build_provider_dialog_extra(
        stringp(source) && source != "" ? source : "npc_interaction",
        "",
        provider_id,
        bucket,
        ([]));
    actions = build_provider_dialog_actions(player, provider_id, snapshot);
    if (arrayp(actions) && sizeof(actions) > 0)
        payload["action_groups"] = ({ build_dialog_action_group("quest", "任务", actions) });
    return payload;
}

mapping query_provider_snapshot(object player, string provider_id)
{
    mapping defs;
    mapping active;
    mapping completed;
    mapping delivered;
    mapping failed;
    mapping available;
    string quest_id;
    int status;

    if (!objectp(player))
        return ([
            "active": ([]),
            "completed": ([]),
            "delivered": ([]),
            "failed": ([]),
            "available": ([]),
        ]);
    defs = QUEST_D->query_visible_quest_definitions(player) || ([]);
    active = ([]);
    completed = ([]);
    delivered = ([]);
    failed = ([]);
    available = ([]);

    foreach (quest_id in keys(defs)) {
        if (!mapp(defs[quest_id]) || defs[quest_id]["provider"] != provider_id)
            continue;
        status = QUEST_D->get_quest_status(player, quest_id);
        switch (status) {
            case QUEST_STATUS_ACCEPTED:
                active[quest_id] = defs[quest_id];
                break;

            case QUEST_STATUS_COMPLETED:
                completed[quest_id] = defs[quest_id];
                break;

            case QUEST_STATUS_DELIVERED:
                delivered[quest_id] = defs[quest_id];
                break;

            case QUEST_STATUS_FAILED:
                failed[quest_id] = defs[quest_id];
                break;

            default:
                if (QUEST_D->is_quest_visible_for_player(player, quest_id))
                    available[quest_id] = defs[quest_id];
        }
    }

    return ([
        "provider": provider_id,
        "available": available,
        "active": active,
        "completed": completed,
        "delivered": delivered,
        "failed": failed,
    ]);
}

private string query_dialogue_title(object npc)
{
    string title;

    if (!objectp(npc))
        return "对方";

    title = npc->query_name();
    if (!stringp(title) || title == "")
        title = "对方";

    return title;
}

private string normalize_dialogue_topic(string topic)
{
    if (!stringp(topic) || topic == "")
        return "";

    topic = lower_case(topic);
    if (member_array(topic, ({ "help", "newbie", "intro", "talk" })) != -1)
        return "help";
    if (member_array(topic, ({ "帮助", "新手", "介绍", "对话" })) != -1)
        return "help";
    if (member_array(topic, ({ "quest", "任务" })) != -1)
        return "quest";

    return topic;
}

private string resolve_dialogue_bucket(string action, string topic)
{
    if (!stringp(action) || action == "")
        return "";

    action = lower_case(action);
    if (action == "talk")
        return "talk";
    if (action != "ask")
        return "";

    return normalize_dialogue_topic(topic);
}

int present_profile_interaction(object player, object npc, string action, string topic)
{
    string profile_id;
    string provider_id;
    string bucket;
    string title;
    string text;
    string dialog_id;

    if (!objectp(player) || !objectp(npc))
        return 0;

    profile_id = npc->query_dialogue_profile();
    if (!stringp(profile_id) || profile_id == "")
        return 0;

    bucket = resolve_dialogue_bucket(action, topic);
    if (!stringp(bucket) || bucket == "")
        return 0;

    title = query_dialogue_title(npc);
    provider_id = npc->query_quest_provider_id();
    dialog_id = profile_id + "_" + bucket;

    if (bucket == "quest" && stringp(provider_id) && provider_id != "")
        return present_provider_panel(player, npc, provider_id, title, dialog_id);

    text = DIALOG_D->render_dialogue_text(profile_id, bucket, player,
        ([ "npc_name": title ]));
    if (!stringp(text) || text == "")
        return 0;

    if (stringp(provider_id) && provider_id != "")
        return present_provider_interaction(player, npc, provider_id,
            title, text, dialog_id,
            build_dialog_extra("npc_profile_interaction", provider_id, bucket,
                player, query_provider_snapshot(player, provider_id)));

    return npc->present_interaction(player, title, text, dialog_id,
        build_dialog_extra("npc_profile_interaction", 0, bucket, player, 0));
}

private string render_active_summary(object player, mapping quests)
{
    string *quest_ids;
    string text;
    string quest_id;

    if (!mapp(quests) || sizeof(quests) == 0)
        return "";

    text = "【你当前相关的任务】\n";
    quest_ids = keys(quests);
    foreach (quest_id in quest_ids) {
        text += sprintf("- %s\n", quest_display_name(quests[quest_id]));
        text += QUEST_D->get_quest_progress_text(player, quest_id);
    }
    return text;
}

private string render_delivered_summary(mapping quests)
{
    string *quest_ids;
    string text;
    string quest_id;

    if (!mapp(quests) || sizeof(quests) == 0)
        return "";

    text = "【你已完成的相关任务】\n";
    quest_ids = keys(quests);
    foreach (quest_id in quest_ids)
        text += sprintf("- %s\n", quest_display_name(quests[quest_id]));

    return text;
}

private string render_provider_progress_summary(object player, mapping snapshot)
{
    string text;

    if (!mapp(snapshot))
        return "";

    text = render_active_summary(player, snapshot["active"]);
    if (text != "")
        return text;

    text = render_active_summary(player, snapshot["completed"]);
    if (text != "")
        return text;

    return render_delivered_summary(snapshot["delivered"]);
}

private string render_provider_specific_text(object player, string provider_id,
    mapping snapshot, string default_text)
{
    return default_text;
}

private string render_provider_available_text(object player, string provider_id,
    int compact_copy)
{
    if (compact_copy)
        return QUEST_D->render_provider_quests_dialog_text(provider_id, player);
    return QUEST_D->render_provider_quests_text(provider_id, player);
}

private int should_skip_provider_progress(object player)
{
    if (!objectp(player))
        return 0;

    return player->query_temp("dialog/skip_provider_progress") ? 1 : 0;
}

private int is_empty_provider_text(string text)
{
    return !stringp(text) || text == "";
}

private string join_provider_sections(string primary, string secondary,
    int compact_copy)
{
    if (!stringp(primary) || primary == "")
        return stringp(secondary) ? secondary : "";
    if (!stringp(secondary) || secondary == "")
        return primary;
    return primary + (compact_copy ? "\n" : "\n\n") + secondary;
}

string render_provider_interaction(object player, string provider_id,
    string default_title, string default_text, int compact_copy)
{
    mapping snapshot;
    string text;
    string summary_text;
    string available_text;

    snapshot = query_provider_snapshot(player, provider_id);
    text = render_provider_specific_text(player, provider_id, snapshot, default_text);
    if (should_skip_provider_progress(player))
        return text;

    summary_text = render_provider_progress_summary(player, snapshot);
    if (summary_text != "") {
        return join_provider_sections(text, summary_text, compact_copy);
    }

    available_text = render_provider_available_text(player, provider_id,
        compact_copy);
    if (!is_empty_provider_text(available_text)) {
        return join_provider_sections(text, available_text, compact_copy);
    }

    return text;
}

string render_provider_panel_text(object player, string provider_id,
    int compact_copy)
{
    mapping snapshot;
    string summary_text;
    string available_text;

    snapshot = query_provider_snapshot(player, provider_id);
    summary_text = render_provider_progress_summary(player, snapshot);
    available_text = render_provider_available_text(player, provider_id,
        compact_copy);

    if (summary_text != "") {
        if (!is_empty_provider_text(available_text))
            return join_provider_sections(summary_text, available_text,
                compact_copy);
        return summary_text;
    }

    return stringp(available_text) ? available_text : "";
}

varargs int present_provider_interaction(object player, object npc, string provider_id,
    string default_title, string default_text, string dialog_id, mapping extra)
{
    string content;
    string plain_text;
    mapping snapshot;

    if (!objectp(player) || !objectp(npc) || !stringp(provider_id) || provider_id == "")
        return 0;

    snapshot = query_provider_snapshot(player, provider_id);
    plain_text = render_provider_interaction(player, provider_id, default_title,
        default_text, 0);
    content = render_provider_interaction(player, provider_id, default_title,
        default_text, 1);
    if (!stringp(content) || content == "")
        return 0;

    return npc->present_interaction(player,
        stringp(default_title) && default_title != "" ? default_title : npc->query_name(),
        content,
        dialog_id,
        (mapp(extra) ? copy(extra) : build_dialog_extra("npc_provider_interaction", provider_id, 0, player, snapshot))
            + ([ "plain_text": plain_text ]));
}

varargs int present_provider_panel(object player, object npc, string provider_id,
    string default_title, string dialog_id, mapping extra)
{
    string content;
    string plain_text;
    mapping snapshot;

    if (!objectp(player) || !objectp(npc) || !stringp(provider_id) || provider_id == "")
        return 0;

    snapshot = query_provider_snapshot(player, provider_id);
    plain_text = render_provider_panel_text(player, provider_id, 0);
    content = render_provider_panel_text(player, provider_id, 1);
    if (!stringp(content) || content == "")
        return 0;

    return npc->present_interaction(player,
        stringp(default_title) && default_title != "" ? default_title : npc->query_name(),
        content,
        dialog_id,
        (mapp(extra) ? copy(extra) : build_dialog_extra("npc_provider_panel", provider_id, "quest", player, snapshot))
            + ([ "plain_text": plain_text ]));
}

int present_specific_quest_progress(object player, object npc, string *quest_ids)
{
    string content;
    string quest_id;

    /* 进度反馈统一主文本区文本块，不再开大弹窗。 */
    if (!objectp(player) || !objectp(npc) || !arrayp(quest_ids) || sizeof(quest_ids) == 0)
        return 0;

    content = "";
    foreach (quest_id in quest_ids) {
        string block = QUEST_D->render_quest_step_text(player, quest_id);
        if (stringp(block) && block != "")
            content += block + "\n";
    }
    if (content == "")
        return 0;
    msg_send(player, content);
    return 1;
}

/* 话题/给予后的任务反馈统一走 present_specific_quest_progress（本次命中的
 * 任务进度块）与 mutation 的 emit_step_completion_feedback——历史上此处的
 * present_post_{ask,talk,give}_view 会叠加 provider 全量摘要
 * （"【你当前相关的任务】"）直发主文本区，属重复且错位的反馈源，已删除。 */
