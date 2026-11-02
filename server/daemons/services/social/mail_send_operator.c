/**
 * @file    mail_send_operator.c
 * @brief   邮件运营批量发送辅助子服务



 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

#define MAIL_PARSE_HELPER "/daemons/services/social/mail_send_parse"
#define MAIL_SEND_HELPER "/daemons/services/social/mail_send_helpers"

int send_recent_plain_from_operator(object mail_service, object me, int within_hours, string payload)
{
    mapping parsed;
    string *targets;
    int sent_count;

    if (!objectp(me))
        return 0;
    if (!CONFIG_D->is_admin_player(me)) {
        msg_send(me, "你没有权限群发邮件。\n");
        return 0;
    }
    if (!intp(within_hours) || within_hours <= 0) {
        msg_send(me, "时间窗口无效。\n");
        return 1;
    }

    parsed = call_other(MAIL_PARSE_HELPER, "parse_signed_mail_payload", payload);
    if (!mapp(parsed)) {
        msg_send(me, "用法：mail send all <小时数> <署名>|<标题>|<正文>\n");
        return 1;
    }

    targets = call_other(MAIL_SEND_HELPER, "query_recent_mail_targets", within_hours);
    if (!sizeof(targets)) {
        msg_send(me, "指定时间范围内没有符合条件的账号。\n");
        return 1;
    }

    sent_count = call_other(MAIL_SEND_HELPER, "send_recent_named_mail_batch",
        mail_service, targets, me, parsed["sender_name"], parsed["subject"], parsed["body"], ({ })
    );

    return call_other(MAIL_SEND_HELPER, "emit_recent_mail_feedback", me, within_hours, sent_count, 0);
}

int send_recent_item_from_operator(object mail_service, object me, int within_hours, string rest)
{
    mapping attachment;
    mapping parsed;
    mapping sender;
    string item_name;
    string *targets;
    string target_id;
    string stop_reason;
    int count;
    int sent_count;
    int sent;

    if (!objectp(me))
        return 0;
    if (!CONFIG_D->is_admin_player(me)) {
        msg_send(me, "你没有权限群发邮件。\n");
        return 0;
    }
    if (!intp(within_hours) || within_hours <= 0) {
        msg_send(me, "时间窗口无效。\n");
        return 1;
    }

    count = 1;
    if (sscanf(rest, "%s %d %s", item_name, count, rest) != 3) {
        if (sscanf(rest, "%s %s", item_name, rest) != 2) {
            msg_send(me, "用法：mail send allitem <小时数> <物品> [数量] <署名>|<标题>|<正文>\n");
            return 1;
        }
        count = 1;
    }

    parsed = call_other(MAIL_PARSE_HELPER, "parse_signed_mail_payload", rest);
    if (!mapp(parsed)) {
        msg_send(me, "用法：mail send allitem <小时数> <物品> [数量] <署名>|<标题>|<正文>\n");
        return 1;
    }

    targets = call_other(MAIL_SEND_HELPER, "query_recent_mail_targets", within_hours);
    if (!sizeof(targets)) {
        msg_send(me, "指定时间范围内没有符合条件的账号。\n");
        return 1;
    }

    sender = call_other(MAIL_SEND_HELPER, "query_sender_context", me);
    sent_count = 0;
    stop_reason = "";

    foreach (target_id in targets) {
        if (!stringp(target_id) || target_id == "")
            continue;

        /*
         * 物品批量邮件必须按收件人逐次构造并消费附件，
         * 不能复用同一份 attachment metadata，否则会出现“只扣一次、发出多封”的复制漏洞。
         */
        attachment = mail_service->build_item_attachment(me, item_name, count);
        if (!mapp(attachment)) {
            stop_reason = "可发送的附件物品不足，批量发送已停止。";
            break;
        }

        if (!mail_service->consume_item_attachment(me, attachment)) {
            stop_reason = "移出附件物品失败，批量发送已停止。";
            break;
        }

        sent = mail_service->send_named_mail("player", sender["id"],
            parsed["sender_name"], target_id, parsed["subject"],
            parsed["body"], ({ attachment }), 0, 0, 0);
        if (sent != 1) {
            /* Only a proven not-written result permits item restoration.
             * Pending/unknown publication retains the consumed attachment in
             * the original delivery candidate for recovery. */
            if (sent == 0 && !ITEM_INVENTORY_D->restore_attachment_to_player(
                    me, attachment))
                log_file("mail", sprintf(
                    "%s batch mail attachment refund failed: sender=%s target=%s template=%s\n",
                    ctime(time()), get_object_id(me) || "?", target_id,
                    attachment["template_id"] || "?"));
            stop_reason = sent == 2
                ? "批量邮件已写入，收据尚未完成，请稍后确认。"
                : sent < 0
                    ? "批量邮件持久化结果未知，已保留恢复责任，请稍后确认。"
                    : "批量邮件发送中断，请稍后重试。";
            break;
        }

        sent_count++;
    }

    if (stop_reason != "")
        msg_send(me, stop_reason + "\n");

    if (sent_count > 0)
        ITEM_FEEDBACK_D->sync_inventory(me);

    return call_other(MAIL_SEND_HELPER, "emit_recent_mail_feedback", me, within_hours, sent_count, 1);
}
