/**
 * @file    mail_send_helpers.c
 * @brief   邮件发送辅助子服务



 */

#pragma strict_types
#pragma save_types

#include <system/ansi.h>
#include <system/services.h>

#define PLAYER_SERVICE "/daemons/services/player/player_d"

mapping query_sender_context(object me)
{
    string sender_id;
    string sender_name;

    sender_id = get_object_id(me);
    sender_name = get_object_name(me);
    if (!stringp(sender_name) || sender_name == "")
        sender_name = sender_id;

    return ([
        "id": sender_id,
        "name": sender_name,
    ]);
}

string *query_recent_mail_targets(int within_hours)
{
    int within_seconds;

    if (!intp(within_hours) || within_hours <= 0)
        return ({ });

    within_seconds = within_hours * 3600;
    return PLAYER_SERVICE->query_recent_account_ids(within_seconds);
}

int send_recent_named_mail_batch(object mail_service, string *targets, object operator,
    string sender_name, string subject, string body, mixed attachments)
{
    string target_id;
    string sender_id;
    int sent_count;

    if (!objectp(mail_service) || !objectp(operator)
        || !CONFIG_D->is_admin_player(operator)
        || !arrayp(targets) || !sizeof(targets))
        return 0;

    sender_id = get_object_id(operator);
    sent_count = 0;
    foreach (target_id in targets) {
        if (!stringp(target_id) || target_id == "")
            continue;
        if (mail_service->send_named_mail("player", sender_id, sender_name, target_id,
            subject, body, attachments, 0, 0, 0) == 1)
            sent_count++;
    }

    return sent_count;
}

int emit_direct_mail_feedback(object me, string target_id, int sent)
{
    if (sent != 1) {
        msg_send(me, sent == 2
            ? "邮件已写入，收据尚未完成，请稍后确认。\n"
            : sent < 0
                ? "邮件持久化结果未知，已保留恢复责任，请稍后确认。\n"
                : "发送邮件失败。\n");
        return 1;
    }

    msg_send(me, HIG "邮件已发送给 " + target_id + "。" NOR "\n");
    return 1;
}

int emit_recent_mail_feedback(object me, int within_hours, int sent_count,
    int has_attachment)
{
    if (has_attachment)
        msg_send(me, HIG + sprintf("已向最近 %d 小时内登录的 %d 个账号发送物品邮件。", within_hours, sent_count) + NOR + "\n");
    else
        msg_send(me, HIG + sprintf("已向最近 %d 小时内登录的 %d 个账号发送邮件。", within_hours, sent_count) + NOR + "\n");

    return 1;
}
