/**
 * @file    mail_send_record.c
 * @brief   邮件记录入箱辅助子服务



 */

#pragma strict_types
#pragma save_types

mapping build_mail_record(object mail_service, string from_type, string from_id,
    string from_name, string to, string subject, string content,
    mixed attachments, int gold, int potential, int exp)
{
    int next_id;

    next_id = mail_service->query_mail_counter() + 1;
    mail_service->set_mail_counter(next_id);

    return ([
        "id": next_id,
        "from_type": stringp(from_type) ? from_type : "system",
        "from_id": stringp(from_id) ? from_id : "",
        "from_name": stringp(from_name) ? from_name : "系统",
        "to": to,
        "subject": stringp(subject) ? subject : "",
        "content": stringp(content) ? content : "",
        "attachments": arrayp(attachments) ? attachments : ({ }),
        "gold": gold > 0 ? gold : 0,
        "potential": potential > 0 ? potential : 0,
        "exp": exp > 0 ? exp : 0,
        "operation_id": mail_service->query_mail_operation_id(),
        "read": 0,
        "claimed": 0,
        "timestamp": time(),
    ]);
}

int send_named_mail(object mail_service, string from_type, string from_id,
    string from_name, string to, string subject, string content,
    mixed attachments, int gold, int potential, int exp)
{
    mapping mailboxes;
    mixed *existing_mailbox;
    mapping existing_mail;
    string operation_id;
    int previous_count;
    int previous_counter;
    int save_result;

    if (!objectp(mail_service) || !stringp(to) || to == ""
        || !mail_service->query_mail_state_ready())
        return 0;

    mailboxes = mail_service->query_mailboxes();
    operation_id = mail_service->query_mail_operation_id();
    if (stringp(operation_id) && operation_id != ""
        && arrayp(mailboxes[to])) {
        existing_mailbox = mailboxes[to];
        foreach (existing_mail in existing_mailbox) {
            if (mapp(existing_mail)
                && existing_mail["operation_id"] == operation_id) {
                if (existing_mail["subject"] != subject
                    || existing_mail["content"] != content
                    || (existing_mail["gold"] || 0) != (gold > 0 ? gold : 0)
                    || (existing_mail["potential"] || 0)
                        != (potential > 0 ? potential : 0)
                    || (existing_mail["exp"] || 0) != (exp > 0 ? exp : 0)) {
                    log_file("mail", sprintf(
                        "%s mail operation conflict: %s/%s\n",
                        ctime(time()), to, operation_id));
                    return 0;
                }
                save_result = mail_service->save_mail_data_checked();
                return save_result == 1 ? 1 : save_result == 2 ? 2
                    : save_result < 0 ? -1 : 0;
            }
        }
    }
    previous_counter = mail_service->query_mail_counter();
    if (!arrayp(mailboxes[to]))
        mailboxes[to] = ({ });
    previous_count = sizeof(mailboxes[to]);

    mailboxes[to] += ({ build_mail_record(mail_service, from_type, from_id, from_name,
        to, subject, content, attachments, gold, potential, exp) });

    /* L-06：入箱必须先落盘再报成功。旧实现只 schedule（延迟写）就返回 1，
     * 崩溃会让“已交付”的邮件连同附件一起消失。
     * 落盘失败按同一身份回滚内存入箱，让调用方能安全重试而不产生重复。 */
    save_result = mail_service->save_mail_data_checked();
    if (save_result == 1)
        return 1;
    if (save_result == 2) {
        log_file("mail", sprintf("%s mail delivery persisted; receipt pending for %s\n",
            ctime(time()), to));
        return 2;
    }
    if (save_result < 0) {
        /* The candidate may already be on disk, but the publication receipt is
         * unknown. Keep the mailbox and counter for the owner retry; rolling
         * them back could duplicate the delivery after recovery. */
        log_file("mail", sprintf(
            "%s mail delivery publication unknown; recovery retained for %s\n",
            ctime(time()), to));
        return -1;
    }

    if (sizeof(mailboxes[to]) > previous_count) {
        if (previous_count > 0)
            mailboxes[to] = mailboxes[to][0 .. previous_count - 1];
        else
            map_delete(mailboxes, to);
    }
    mail_service->set_mail_counter(previous_counter);
    log_file("mail", sprintf("%s mail delivery rejected: mailbox save failed for %s\n",
        ctime(time()), to));
    return 0;
}
