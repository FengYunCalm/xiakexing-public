/**
 * @file    mail.c
 * @brief   玩家邮件命令



 *
 * @par Description:
 * 文本邮件入口，负责查看、阅读、删除、领取以及发送邮件。
 * 业务真源保持在 MAIL_D，本命令只做参数校验与轻量协调。
 */

#pragma strict_types

#include <system/ansi.h>
#include <system/services.h>
#include <framework/message_facade.h>

inherit "/lib/framework/command";
inherit "/lib/std/utils/util_player";

int main(object me, string arg);
int help(object me);

private mapping require_mail_record(object me, int mail_id,
    mapping response_context);
private mapping build_mail_dialog_extra(string dialog_id, mapping *actions);
private mapping *build_mail_list_actions();
private mapping *build_mail_read_actions(mapping mail);
private mapping *build_mail_send_select_actions();
private mapping *build_mail_send_compose_actions(string target_id);
private void emit_mail_list_dialog(object me, int unread_only, string prefix,
    mapping response_context);
private void emit_mail_read_dialog(object me, mapping mail, string prefix,
    mapping response_context);
private void emit_mail_send_select_dialog(object me, mapping response_context);
private void emit_mail_send_compose_dialog(object me, string target_id,
    mapping response_context);
private string format_mail_read_all_plain(int count);
private string format_mail_claim_all_plain(int count);
private string format_mail_delete_read_plain(int deleted_count, int protected_count);
private string *query_mail_reward_types();
private string *query_mail_command_lines(object me);
private int is_reward_mail_type(string reward_type);
private string query_mail_usage();
private string query_mail_send_usage();
private string query_mail_help_text(object me);
private int dispatch_mail_send(object me, string rest,
    mapping response_context);
private mapping split_mail_send_args(string rest);
private int do_mail_list(object me, int unread_only,
    mapping response_context);
private int do_mail_read(object me, int mail_id,
    mapping response_context);
private int do_mail_read_all(object me, mapping response_context);
private int do_mail_delete(object me, int mail_id,
    mapping response_context);
private int do_mail_claim(object me, int mail_id,
    mapping response_context);
private int do_mail_claim_all(object me, mapping response_context);
private int do_mail_send_plain(object me, string target_id, string payload);
private int do_mail_send_reward(object me, string reward_type, string rest);
private int do_mail_send_all(object me, int within_hours, string payload,
    mapping response_context);
private int do_mail_send_all_item(object me, int within_hours, string rest,
    mapping response_context);
private int do_mail_delete_read(object me, mapping response_context);

private int can_send_broadcast_mail(object me)
{
    return is_admin_operator(me);
}

private mapping require_mail_record(object me, int mail_id,
    mapping response_context)
{
    mapping mail;

    mail = MAIL_D->query_mail_record(me, mail_id);
    if (!mapp(mail))
        msg_send(me, "没有这封邮件。\n", response_context);

    return mail;
}

private mapping build_mail_dialog_extra(string dialog_id, mapping *actions)
{
    return DIALOG_PROJECTION_D->build_dialog_extra("mail", dialog_id, ([
        "replace_mode": "replace",
        "actions": arrayp(actions) ? actions : ({ }),
    ]));
}

private mapping *build_mail_list_actions()
{
    return ({
        (["label":"全部邮件", "cmd":"mail"]),
        (["label":"未读邮件", "cmd":"mail unread"]),
        (["label":"全部已读", "cmd":"mail read all"]),
        (["label":"全部领取", "cmd":"mail claim all"]),
        (["label":"清理已读", "cmd":"mail delete read"]),
        (["label":"发送邮件", "cmd":"mail send-select"]),
    });
}

private mapping *build_mail_read_actions(mapping mail)
{
    mapping *actions;

    actions = ({
        (["label":"返回邮箱", "cmd":"mail"]),
        (["label":"未读邮件", "cmd":"mail unread"]),
    });

    if (MAIL_D->query_mail_claim_block_reason(mail) == "")
        actions += ({ (["label":"领取附件", "cmd":"mail claim " + mail["id"]]) });
    if (MAIL_D->query_mail_delete_block_reason(mail) == "")
        actions += ({ (["label":"删除邮件", "cmd":"mail delete " + mail["id"]]) });

    return actions;
}

private mapping *build_mail_send_select_actions()
{
    return ({
        (["label":"返回邮箱", "cmd":"mail"]),
        (["label":"未读邮件", "cmd":"mail unread"]),
    });
}

private mapping *build_mail_send_compose_actions(string target_id)
{
    return ({
        (["label":"返回邮箱", "cmd":"mail"]),
        (["label":"更换收件人", "cmd":"mail send-select"]),
        (["label":"问候", "cmd":"mail send " + target_id + " 问候|江湖路远，近来可好？"]),
        (["label":"任务协助", "cmd":"mail send " + target_id + " 任务协助|我这边有些任务线索，方便时可以一同查看。"]),
        (["label":"交易沟通", "cmd":"mail send " + target_id + " 交易沟通|我想和你确认一笔交易，方便时回信。"]),
        (["label":"道具归还", "cmd":"mail send " + target_id + " 道具归还|物品已整理好，稍后当面交还。"]),
    });
}

private void emit_mail_list_dialog(object me, int unread_only, string prefix,
    mapping response_context)
{
    mixed *mailbox;
    string content;

    if (!objectp(me))
        return;

    mailbox = MAIL_D->query_mailbox(me, unread_only);
    content = stringp(prefix) && prefix != "" ? prefix + "\n\n" : "";
    content += MAIL_D->format_mail_list_view(mailbox, unread_only);
    xk_send_dialog(me,
        unread_only ? "未读邮件" : "邮箱",
        content,
        build_mail_dialog_extra(unread_only ? "mail_unread" : "mail_inbox",
            build_mail_list_actions()), response_context);
}

private void emit_mail_read_dialog(object me, mapping mail, string prefix,
    mapping response_context)
{
    string content;
    string dialog_id;
    string title;

    if (!objectp(me) || !mapp(mail))
        return;

    content = stringp(prefix) && prefix != "" ? prefix + "\n\n" : "";
    content += MAIL_D->format_mail_read_view(mail);
    dialog_id = sprintf("mail_read:%d", intp(mail["id"]) ? mail["id"] : 0);
    title = mail["subject"] || "邮件详情";
    xk_send_dialog(me, title, content,
        build_mail_dialog_extra(dialog_id, build_mail_read_actions(mail)),
        response_context);
}

private void emit_mail_send_select_dialog(object me, mapping response_context)
{
    mapping roster;
    mixed *entries;
    mapping entry;
    mapping *actions;
    string target_id;
    string target_label;
    string content;

    if (!objectp(me))
        return;

    roster = PLAYER_ACTOR_ANALYSIS_D->query_online_player_roster_for(me, 1, 64);
    entries = mapp(roster) && arrayp(roster["entries"]) ? roster["entries"] : ({ });
    actions = build_mail_send_select_actions();
    content = "选择收件人：\n";
    foreach (entry in entries) {
        if (!mapp(entry))
            continue;
        target_id = entry["id"];
        if (!stringp(target_id) || target_id == "")
            continue;
        if (target_id == get_object_display_id(me, ""))
            continue;
        target_label = stringp(entry["name"]) && entry["name"] != "" ? entry["name"] : target_id;
        content += sprintf("- %s（%s）\n", target_label, target_id);
        actions += ({ (["label": target_label, "cmd": "mail send-compose " + target_id]) });
    }
    if (sizeof(actions) == sizeof(build_mail_send_select_actions()))
        content += "当前没有可选的在线收件人。";

    xk_send_dialog(me, "发送邮件", content,
        build_mail_dialog_extra("mail_send_select", actions), response_context);
}

private void emit_mail_send_compose_dialog(object me, string target_id,
    mapping response_context)
{
    object target;
    string target_label;
    string content;

    if (!objectp(me) || !stringp(target_id) || target_id == "")
        return;

    target = find_player_online(target_id);
    target_label = objectp(target) ? get_object_display_name(target, target_id) : target_id;
    content = sprintf("收件人：%s（%s）\n", target_label, target_id);
    content += "选择一封预设邮件即可直接发送。";

    xk_send_dialog(me, "发送邮件", content,
        build_mail_dialog_extra("mail_send_compose:" + target_id,
            build_mail_send_compose_actions(target_id)), response_context);
}

private string format_mail_read_all_plain(int count)
{
    if (count <= 0)
        return "没有需要标记为已读的邮件。";

    return sprintf("已将 %d 封邮件标记为已读。", count);
}

private string format_mail_claim_all_plain(int count)
{
    if (count <= 0)
        return "没有可领取的邮件附件。";

    return sprintf("已领取 %d 封邮件的附件。", count);
}

private string format_mail_delete_read_plain(int deleted_count, int protected_count)
{
    string result;

    if (deleted_count <= 0 && protected_count <= 0)
        return "没有可删除的已读邮件。";

    result = deleted_count > 0
        ? sprintf("已删除 %d 封已读邮件。", deleted_count)
        : "";
    if (protected_count > 0) {
        if (result != "")
            result += "\n";
        result += sprintf("另有 %d 封已读邮件仍含未领取附件，已保留。", protected_count);
    }

    return result;
}

private string *query_mail_reward_types()
{
    return ({ "gold", "item", "potential", "exp" });
}

private string *query_mail_command_lines(object me)
{
    string *lines;

    lines = ({
        "mail",
        "mail unread",
        "mail read all",
        "mail read <编号>",
        "mail claim <编号>",
        "mail claim all",
        "mail delete <编号>",
        "mail delete read",
        "mail send <玩家ID> <标题>|<正文>",
        "mail send gold <玩家ID> <金额> <标题>|<正文>",
        "mail send potential <玩家ID> <数量> <标题>|<正文>",
        "mail send exp <玩家ID> <数量> <标题>|<正文>",
        "mail send item <玩家ID> <物品> [数量] <标题>|<正文>",
    });

    if (can_send_broadcast_mail(me)) {
        lines += ({
            "mail send all <小时数> <署名>|<标题>|<正文>",
            "mail send allitem <小时数> <物品> [数量] <署名>|<标题>|<正文>",
        });
    }

    return lines;
}

private int is_reward_mail_type(string reward_type)
{
    return stringp(reward_type) && member_array(reward_type, query_mail_reward_types()) != -1;
}

private string query_mail_usage()
{
    return "用法：mail、mail unread、mail read <编号>、mail claim <编号>、mail delete <编号>、mail send ...\n";
}

private string query_mail_send_usage()
{
    return "用法：mail send <玩家ID> <标题>|<正文>\n";
}

private string query_mail_help_text(object me)
{
    string result;
    string line;

    result = "命令：mail\n用法：\n";
    foreach (line in query_mail_command_lines(me))
        result += "  " + line + "\n";

    result += "说明：标题和正文用 | 分隔；read all 只标记已读；claim 会顺便领取附件；delete read 只删除已读且没有未领附件的邮件。\n";
    if (!can_send_broadcast_mail(me))
        result += "全服邮件和批量物品邮件仅管理员可用。\n";

    return result;
}

private mapping split_mail_send_args(string rest)
{
    int space_pos;

    if (!stringp(rest) || rest == "")
        return 0;

    space_pos = strsrch(rest, " ");
    if (space_pos <= 0 || space_pos >= strlen(rest) - 1)
        return 0;

    return ([
        "head": rest[0..space_pos - 1],
        "tail": rest[space_pos + 1..],
    ]);
}

private int dispatch_mail_send(object me, string rest,
    mapping response_context)
{
    int within_hours;
    mapping parsed;
    string payload;
    string reward_type;
    string target_id;

    if (rest == "select") {
        emit_mail_send_select_dialog(me, response_context);
        return 1;
    }

    if (sscanf(rest, "compose %s", target_id) == 1) {
        emit_mail_send_compose_dialog(me, target_id, response_context);
        return 1;
    }

    if (sscanf(rest, "allitem %d %s", within_hours, payload) == 2)
        return do_mail_send_all_item(me, within_hours, payload,
            response_context);

    if (sscanf(rest, "all %d %s", within_hours, payload) == 2)
        return do_mail_send_all(me, within_hours, payload, response_context);

    parsed = split_mail_send_args(rest);
    if (!mapp(parsed)) {
        msg_send(me, query_mail_send_usage(), response_context);
        return 1;
    }

    reward_type = parsed["head"];
    payload = parsed["tail"];
    if (is_reward_mail_type(reward_type)) {
        return do_mail_send_reward(me, reward_type, payload);
    }

    target_id = parsed["head"];
    return do_mail_send_plain(me, target_id, payload);
}

private int do_mail_list(object me, int unread_only,
    mapping response_context)
{
    mixed *mailbox;

    mailbox = MAIL_D->query_mailbox(me, unread_only);
    READ_SUMMARY_D->query_named_summary(me, unread_only ? "mail/unread" : "mail/inbox", ([
        "type": "mail_list_summary",
        "unread_only": unread_only ? 1 : 0,
        "mail_count": arrayp(mailbox) ? sizeof(mailbox) : 0,
    ]));
    if (!is_xk_client(me))
        msg_send(me, MAIL_D->format_mail_list_view(mailbox, unread_only),
            response_context);
    emit_mail_list_dialog(me, unread_only, "", response_context);
    return 1;
}

private int do_mail_read(object me, int mail_id,
    mapping response_context)
{
    mapping mail;
    int read_result;

    mail = require_mail_record(me, mail_id, response_context);
    if (!mapp(mail))
        return 1;

    read_result = MAIL_D->read_mail(me, mail_id);
    if (read_result <= 0) {
        msg_send(me, read_result < 0
            ? "邮件已读取，但保存结果未知，请稍后确认。\n"
            : "邮件已读取，但保存失败，请稍后重试。\n", response_context);
        return 1;
    }
    if (!is_xk_client(me))
        msg_send(me, MAIL_D->format_mail_read_view(mail), response_context);
    emit_mail_read_dialog(me, mail,
        read_result == 2 ? "邮件已读取，收据尚未完成，请稍后确认。" : "",
        response_context);
    return 1;
}

private int do_mail_read_all(object me, mapping response_context)
{
    int read_count;
    mapping phase;
    string feedback;

    read_count = MAIL_D->read_all_mails(me);
    phase = MAIL_D->query_last_mail_save_phase();
    if (read_count < 0) {
        msg_send(me, read_count == -2
            ? "邮件已读取，但保存失败，请稍后重试。\n"
            : "邮件已读取，但保存结果未知，请稍后确认。\n", response_context);
        return 1;
    }
    feedback = format_mail_read_all_plain(read_count);
    if (read_count > 0 && mapp(phase)
        && (phase["outcome"] == "persisted_receipt_pending"
            || phase["outcome"] == "unknown"))
        feedback = "邮件已读取，收据尚未完成，请稍后确认。";
    if (!is_xk_client(me))
        msg_send(me, feedback + "\n", response_context);
    emit_mail_list_dialog(me, 0, feedback, response_context);
    return 1;
}

private int do_mail_delete(object me, int mail_id,
    mapping response_context)
{
    mapping mail;
    int delete_result;

    mail = require_mail_record(me, mail_id, response_context);
    if (!mapp(mail))
        return 1;

    if (MAIL_D->query_mail_delete_block_reason(mail) != "") {
        msg_send(me, MAIL_D->query_mail_delete_block_reason(mail) + "\n",
            response_context);
        return 1;
    }

    delete_result = MAIL_D->delete_mail(me, mail_id);
    if (delete_result <= 0) {
        msg_send(me, delete_result < 0
            ? "删除邮件结果未知，请稍后确认。\n" : "删除邮件失败。\n",
            response_context);
        return 1;
    }
    if (!is_xk_client(me))
        msg_send(me, delete_result == 2
            ? "邮件已删除，收据尚未完成，请稍后确认。\n"
            : HIG "邮件已删除。" NOR "\n", response_context);
    emit_mail_list_dialog(me, 0, delete_result == 2
        ? "邮件已删除，收据尚未完成，请稍后确认。" : "邮件已删除。",
        response_context);
    return 1;
}

private int do_mail_claim(object me, int mail_id,
    mapping response_context)
{
    mapping applied;
    mapping mail;
    string claim_message;

    mail = require_mail_record(me, mail_id, response_context);
    if (!mapp(mail))
        return 1;

    if (MAIL_D->query_mail_claim_block_reason(mail) != "") {
        msg_send(me, MAIL_D->query_mail_claim_block_reason(mail) + "\n",
            response_context);
        return 1;
    }

    applied = MAIL_D->claim_attachments(me, mail_id);
    if (!mapp(applied) || !applied["ok"]) {
        msg_send(me, "领取附件失败。\n", response_context);
        return 1;
    }

    claim_message = REWARD_FEEDBACK_D->format_mail_claim_success(applied);
    REWARD_FEEDBACK_D->sync_reward_client_state(me, applied,
        response_context);
    if (!is_xk_client(me))
        msg_send(me, claim_message, response_context);
    mail = MAIL_D->query_mail_record(me, mail_id);
    if (mapp(mail))
        emit_mail_read_dialog(me, mail, "已领取邮件附件。", response_context);
    else
        emit_mail_list_dialog(me, 0, "已领取邮件附件。", response_context);
    return 1;
}

private int do_mail_claim_all(object me, mapping response_context)
{
    mapping result;

    result = MAIL_D->claim_all_attachments(me);
    if ((result["count"] || 0) > 0)
        REWARD_FEEDBACK_D->sync_reward_client_state(me, result["applied"],
            response_context);
    if (!is_xk_client(me))
        msg_send(me, REWARD_FEEDBACK_D->format_mail_claim_all_success(
            result["count"], result["applied"]), response_context);
    emit_mail_list_dialog(me, 0, format_mail_claim_all_plain(result["count"]),
        response_context);
    return 1;
}

private int do_mail_send_plain(object me, string target_id, string payload)
{
    return MAIL_D->send_plain_from_player(me, target_id, payload);
}

private int do_mail_send_reward(object me, string reward_type, string rest)
{
    return MAIL_D->send_reward_from_player(me, reward_type, rest);
}

private int do_mail_send_all(object me, int within_hours, string payload,
                              mapping response_context)
{
    if (!can_send_broadcast_mail(me)) {
        msg_send(me, "你没有权限发送全服邮件。\n", response_context);
        return 1;
    }

    return MAIL_D->send_recent_plain_from_operator(me, within_hours, payload);
}

private int do_mail_send_all_item(object me, int within_hours, string rest,
                                   mapping response_context)
{
    if (!can_send_broadcast_mail(me)) {
        msg_send(me, "你没有权限发送全服邮件。\n", response_context);
        return 1;
    }

    return MAIL_D->send_recent_item_from_operator(me, within_hours, rest);
}

private int do_mail_delete_read(object me, mapping response_context)
{
    mapping result;
    string feedback;

    result = MAIL_D->delete_read_mails(me);
    if (!mapp(result)) {
        msg_send(me, "清理邮件失败。\n", response_context);
        return 1;
    }
    if (result["state"] == "not_written") {
        msg_send(me, "清理邮件失败，未落盘。\n", response_context);
        return 1;
    }
    feedback = format_mail_delete_read_plain(result["deleted"], result["protected"]);
    if (result["state"] == "persisted_receipt_pending"
        || result["state"] == "unknown")
        feedback = "邮件已清理，收据尚未完成，请稍后确认。";
    if (!is_xk_client(me))
        msg_send(me, feedback + (result["state"] == "committed" ? "" : "\n"),
                 response_context);
    emit_mail_list_dialog(me, 0, feedback, response_context);
    return 1;
}

int main(object me, string arg)
{
    int mail_id;
    string rest;
    string target_id;
    mapping response_context;

    if (!objectp(me))
        return 0;

    response_context = CLIENT_SYNC_D->capture_command_response_context(me);

    if (!stringp(arg) || arg == "")
        return do_mail_list(me, 0, response_context);

    if (arg == "unread")
        return do_mail_list(me, 1, response_context);

    if (arg == "read all")
        return do_mail_read_all(me, response_context);

    if (sscanf(arg, "read %d", mail_id) == 1)
        return do_mail_read(me, mail_id, response_context);

    if (sscanf(arg, "delete %d", mail_id) == 1)
        return do_mail_delete(me, mail_id, response_context);

    if (arg == "delete read")
        return do_mail_delete_read(me, response_context);

    if (sscanf(arg, "claim %d", mail_id) == 1)
        return do_mail_claim(me, mail_id, response_context);

    if (arg == "claim all")
        return do_mail_claim_all(me, response_context);

    if (arg == "send-select") {
        emit_mail_send_select_dialog(me, response_context);
        return 1;
    }

    if (sscanf(arg, "send-compose %s", target_id) == 1) {
        emit_mail_send_compose_dialog(me, target_id, response_context);
        return 1;
    }

    if (sscanf(arg, "send %s", rest) == 1)
        return dispatch_mail_send(me, rest, response_context);

    msg_send(me, query_mail_usage(), response_context);
    return 1;
}

int help(object me, mapping response_context)
{
    if (!mapp(response_context))
        response_context = CLIENT_SYNC_D->capture_command_response_context(me);
    msg_send(me, query_mail_help_text(me), response_context);
    return 1;
}
