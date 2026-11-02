/**
 * @file    mail_send_public_api.c
 * @brief   邮件公开发送接口辅助子服务



 */

#pragma strict_types

int send_mail(object mail_service, string from, string to, string subject,
    string content, mixed attachments, int gold)
{
    return mail_service->send_named_mail("player", from, from, to, subject, content, attachments, gold, 0, 0);
}

int send_system_mail(object mail_service, string to, string subject,
    string content, mixed attachments, int gold)
{
    return mail_service->send_named_mail("system", "system", "系统", to, subject, content, attachments, gold, 0, 0);
}

int send_reward_mail(object mail_service, string from_type, string from_id,
    string from_name, string to, string subject, string content,
    mixed attachments, int gold, int potential, int exp)
{
    return mail_service->send_named_mail(from_type, from_id, from_name, to, subject, content, attachments,
        gold, potential, exp);
}

int send_npc_mail(object mail_service, string npc_id, string npc_name, string to,
    string subject, string content, mixed attachments, int gold,
    int potential, int exp)
{
    return mail_service->send_named_mail("npc", npc_id, npc_name, to, subject, content, attachments,
        gold, potential, exp);
}
