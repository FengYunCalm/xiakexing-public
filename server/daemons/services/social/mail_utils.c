/**
 * @file    mail_utils.c
 * @brief   邮件工具子服务



 */

#pragma strict_types
#pragma save_types

#include <system/ansi.h>
#include <system/services.h>

#define MAIL_PARSE_HELPER "/daemons/services/social/mail_send_parse"

mapping query_mailboxes();
mapping deep_copy_mail(mapping mail);
mixed *deep_copy_mail_list(mixed *mailbox);
int sync_broadcast_mails_for_player(object player);
int query_cleanup_enabled();
int query_mail_state_ready();
void save_mail_data();
void set_cleanup_mail_call_out_handle(int val);

int has_unclaimed_rewards(mapping mail)
{
    if (!mapp(mail) || mail["claimed"])
        return 0;

    return (arrayp(mail["attachments"]) && sizeof(mail["attachments"]) > 0)
        || ((mail["gold"] || 0) > 0)
        || ((mail["potential"] || 0) > 0)
        || ((mail["exp"] || 0) > 0);
}

string mail_sender_name(mapping mail)
{
    string name;

    if (!mapp(mail))
        return "系统";

    name = mail["from_name"];
    if (!stringp(name) || name == "")
        name = mail["from_id"];
    if (!stringp(name) || name == "")
        name = "系统";

    return name;
}

private string mail_box_top()
{
    return HIC "╔════════════════════════════════════════╗" NOR "\n";
}

private string mail_box_divider()
{
    return HIC "╠════════════════════════════════════════╣" NOR "\n";
}

private string mail_box_bottom()
{
    return HIC "╚════════════════════════════════════════╝" NOR "\n";
}

private string mail_box_row(string content)
{
    return sprintf(HIC "║" NOR " %-38s" HIC "║" NOR "\n", content || "");
}

string format_reward_summary(mapping mail)
{
    return REWARD_FEEDBACK_D->format_reward_summary(mail);
}

mapping parse_mail_payload(string payload)
{
    return call_other(MAIL_PARSE_HELPER, "parse_mail_payload", payload);
}

mapping parse_signed_mail_payload(string payload)
{
    return call_other(MAIL_PARSE_HELPER, "parse_signed_mail_payload", payload);
}

int parse_subject_and_body(string payload, string *subject, string *body)
{
    mapping parsed;

    parsed = parse_mail_payload(payload);
    if (!mapp(parsed))
        return 0;

    subject[0] = parsed["subject"];
    body[0] = parsed["body"];
    return 1;
}

int parse_signature_subject_and_body(string payload, string *sender_name, string *subject, string *body)
{
    mapping parsed;

    parsed = parse_signed_mail_payload(payload);
    if (!mapp(parsed))
        return 0;

    sender_name[0] = parsed["sender_name"];
    subject[0] = parsed["subject"];
    body[0] = parsed["body"];
    return 1;
}

mixed *query_mailbox(object player, int unread_only)
{
    mixed *mailbox;
    mixed *filtered;
    mapping mail;
    string player_id;

    if (!objectp(player))
        return ({ });
    player_id = get_object_id(player);
    if (!stringp(player_id) || player_id == "")
        return ({ });

    sync_broadcast_mails_for_player(player);
    mailbox = query_mailboxes()[player_id];
    if (!arrayp(mailbox))
        return ({ });
    if (!unread_only)
        return deep_copy_mail_list(mailbox);

    filtered = ({ });
    foreach (mail in mailbox) {
        if (mapp(mail) && !mail["read"])
            filtered += ({ mail });
    }
    return filtered;
}

mapping query_mail_record(object player, int mail_id)
{
    mixed *mailbox;
    mapping mail;
    string player_id;

    if (!objectp(player) || !intp(mail_id))
        return 0;

    player_id = get_object_id(player);
    if (!stringp(player_id) || player_id == "")
        return 0;

    sync_broadcast_mails_for_player(player);
    mailbox = query_mailboxes()[player_id];
    if (!arrayp(mailbox))
        return 0;

    foreach (mail in mailbox) {
        if (mapp(mail) && mail["id"] == mail_id)
            return deep_copy_mail(mail);
    }

    return 0;
}

string format_mail_time(int timestamp)
{
    mixed *lt;

    lt = localtime(timestamp);
    return sprintf("%04d-%02d-%02d %02d:%02d",
        lt[5], lt[4] + 1, lt[3], lt[2], lt[1]);
}

string format_mail_read_view(mapping mail)
{
    string result;

    if (!mapp(mail))
        return "";

    result = "发件人：" + mail_sender_name(mail) + "\n";
    result += "标题：" + (mail["subject"] || "(无标题)") + "\n";
    result += "时间：" + format_mail_time(mail["timestamp"] || time()) + "\n\n";
    result += (mail["content"] || "") + "\n\n";
    result += "附件：" + format_reward_summary(mail) + "\n";

    return result;
}

string format_mail_list_view(mixed *mailbox, int unread_only)
{
    mapping mail;
    string result;
    string status;

    if (!arrayp(mailbox) || !sizeof(mailbox))
        return unread_only ? "没有未读邮件。\n" : "邮箱里空空如也。\n";

    result = unread_only ? "未读邮件：\n" : "邮件列表：\n";
    foreach (mail in mailbox) {
        if (!mapp(mail))
            continue;
        status = mail["read"] ? "已读" : "未读";
        result += sprintf("#%d [%s] %s %s\n",
            mail["id"], status, mail_sender_name(mail), mail["subject"] || "(无标题)");
    }
    result += "可用操作：mail read <编号>、mail claim <编号>、mail delete <编号>\n";

    return result;
}

string query_mail_delete_block_reason(mapping mail)
{
    if (!mapp(mail))
        return "没有这封邮件。";

    if (has_unclaimed_rewards(mail))
        return "这封邮件还有未领取奖励，不能删除。";

    return "";
}

string query_mail_claim_block_reason(mapping mail)
{
    if (!mapp(mail))
        return "没有这封邮件。";

    if (!has_unclaimed_rewards(mail))
        return "这封邮件没有可领取的奖励。";

    return "";
}

string format_mail_claim_success(mapping mail)
{
    return REWARD_FEEDBACK_D->format_mail_claim_success(mail);
}

string format_mail_claim_all_success(int count, mixed applied)
{
    return REWARD_FEEDBACK_D->format_mail_claim_all_success(count,
        mapp(applied) ? applied : ([ ]));
}

string format_mail_delete_read_success(int count)
{
    if (count <= 0)
        return "没有可删除的已读邮件。\n";

    return HIG + sprintf("已删除 %d 封已读邮件。", count) + NOR + "\n";
}

string format_mail_delete_read_result(int deleted_count, int protected_count)
{
    string result;

    if (deleted_count <= 0 && protected_count <= 0)
        return "没有可删除的已读邮件。\n";

    result = "";
    if (deleted_count > 0)
        result += sprintf("已删除 %d 封已读邮件。", deleted_count);
    if (protected_count > 0) {
        if (result != "")
            result += "\n";
        result += sprintf("另有 %d 封邮件仍有未领取附件，已保留。", protected_count);
    }

    return result + "\n";
}

string format_mail_read_all_success(int count)
{
    if (count <= 0)
        return "没有需要标记为已读的邮件。\n";

    return sprintf("已将 %d 封邮件标记为已读。\n", count);
}

/* L-125：查询返回隔离视图，调用方不能再直接改写服务内部集合；
 * 邮件记录含嵌套数组/映射，copy() 只复制顶层，故用序列化往返做深拷贝。 */
mapping deep_copy_mail(mapping mail)
{
    mixed copied;
    mixed err;

    if (!mapp(mail))
        return 0;
    err = catch(copied = restore_variable(save_variable(mail)));
    return err ? 0 : (mapp(copied) ? copied : 0);
}

mixed *deep_copy_mail_list(mixed *mailbox)
{
    mixed *result;
    mapping mail;

    if (!arrayp(mailbox))
        return ({ });
    result = ({ });
    foreach (mail in mailbox) {
        mapping copied;

        copied = deep_copy_mail(mail);
        result += ({ mapp(copied) ? copied : mail });
    }
    return result;
}

void cleanup_expired_mails()
{
    mapping mailboxes;
    string player_id;
    mixed *mailbox;
    mixed *filtered;
    mapping mail;
    int cutoff;
    int changed;
    int save_result;
    mapping before_mailboxes;
    mixed err;

    if (!query_cleanup_enabled() || !query_mail_state_ready())
    {
        set_cleanup_mail_call_out_handle(0);
        return;
    }

    cutoff = time() - 30 * 24 * 60 * 60;
    mailboxes = query_mailboxes();
    err = catch(before_mailboxes = restore_variable(save_variable(mailboxes)));
    if (err || !mapp(before_mailboxes))
        before_mailboxes = ([]);
    changed = 0;
    foreach (player_id, mailbox in mailboxes) {
        if (!arrayp(mailbox))
            continue;
        filtered = ({ });
        foreach (mail in mailbox) {
            if (!mapp(mail))
                continue;
            if ((mail["timestamp"] || 0) >= cutoff) {
                filtered += ({ mail });
                continue;
            }

            if (!mail["read"] || has_unclaimed_rewards(mail))
                filtered += ({ mail });
        }
        /* L-128：到期规则不变，只在确实删掉了记录时才重建并保存整箱；
         * 旧实现每次清理都无条件重写全量邮箱。 */
        if (sizeof(filtered) != sizeof(mailbox)) {
            mailboxes[player_id] = filtered;
            changed = 1;
        }
    }

    if (changed) {
        save_result = MAIL_D->save_mail_data_checked();
        if (save_result == 0) {
            foreach (player_id, mailbox in before_mailboxes)
                mailboxes[player_id] = mailbox;
            log_file("mail", sprintf("%s expired mail cleanup not written\n",
                ctime(time())));
        } else if (save_result != 1) {
            log_file("mail", sprintf(
                "%s expired mail cleanup receipt pending or unknown: %O\n",
                ctime(time()), MAIL_D->query_last_mail_save_phase()));
        }
    }

    set_cleanup_mail_call_out_handle(call_out("cleanup_expired_mails", 86400));
}
