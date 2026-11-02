/**
 * @file    mail_send.c
 * @brief   邮件发送服务（真源）



 */

#pragma strict_types
#pragma save_types

#include <system/ansi.h>
#include <system/services.h>

#define MAIL_COMMAND_API_HELPER "/daemons/services/social/mail_send_command_api"
#define MAIL_PLAYER_REWARD_HELPER "/daemons/services/social/mail_send_player_rewards"
#define MAIL_PUBLIC_API_HELPER "/daemons/services/social/mail_send_public_api"
#define MAIL_PLAIN_HELPER "/daemons/services/social/mail_send_player_plain"
#define MAIL_ITEM_REWARD_HELPER "/daemons/services/social/mail_send_player_item_rewards"
#define MAIL_NUMERIC_REWARD_HELPER "/daemons/services/social/mail_send_player_numeric_rewards"
#define MAIL_OPERATOR_HELPER "/daemons/services/social/mail_send_operator"
#define MAIL_RECORD_HELPER "/daemons/services/social/mail_send_record"
#define MAIL_PARSE_HELPER "/daemons/services/social/mail_send_parse"
#define MAIL_ATTACHMENT_HELPER "/daemons/services/social/mail_send_attachments"
#define MAIL_SEND_HELPER "/daemons/services/social/mail_send_helpers"
#define PLAYER_SERVICE "/daemons/services/player/player_d"

mapping query_mailboxes();
int query_mail_counter();
void set_mail_counter(int val);
void schedule_mail_save();
mapping parse_mail_payload(string payload);
mapping parse_signed_mail_payload(string payload);
int send_named_mail(string from_type, string from_id, string from_name,
    string to, string subject, string content, mixed attachments,
    int gold, int potential, int exp);

mapping build_item_attachment(object me, string item_name, int count)
{
    return call_other(MAIL_ATTACHMENT_HELPER, "build_item_attachment", me, item_name, count);
}

int consume_item_attachment(object me, mapping attachment)
{
    return call_other(MAIL_ATTACHMENT_HELPER, "consume_item_attachment", me, attachment);
}

int send_named_mail(string from_type, string from_id, string from_name,
    string to, string subject, string content, mixed attachments,
    int gold, int potential, int exp)
{
    return call_other(MAIL_RECORD_HELPER, "send_named_mail", this_object(),
        from_type, from_id, from_name, to, subject, content, attachments,
        gold, potential, exp);
}

int send_mail(string from, string to, string subject, string content, mixed attachments, int gold)
{
    return call_other(MAIL_PUBLIC_API_HELPER, "send_mail", this_object(), from, to, subject, content, attachments, gold);
}

int send_system_mail(string to, string subject, string content, mixed attachments, int gold)
{
    return call_other(MAIL_PUBLIC_API_HELPER, "send_system_mail", this_object(), to, subject, content, attachments, gold);
}

int send_reward_mail(string from_type, string from_id, string from_name,
    string to, string subject, string content, mixed attachments,
    int gold, int potential, int exp)
{
    return call_other(MAIL_PUBLIC_API_HELPER, "send_reward_mail", this_object(),
        from_type, from_id, from_name, to, subject, content, attachments,
        gold, potential, exp);
}

int send_npc_mail(string npc_id, string npc_name, string to, string subject,
    string content, mixed attachments, int gold, int potential, int exp)
{
    return call_other(MAIL_PUBLIC_API_HELPER, "send_npc_mail", this_object(),
        npc_id, npc_name, to, subject, content, attachments,
        gold, potential, exp);
}

int send_recent_plain_from_operator(object me, int within_hours, string payload)
{
    return call_other(MAIL_COMMAND_API_HELPER, "send_recent_plain_from_operator",
        this_object(), me, within_hours, payload);
}

int send_recent_item_from_operator(object me, int within_hours, string rest)
{
    return call_other(MAIL_COMMAND_API_HELPER, "send_recent_item_from_operator",
        this_object(), me, within_hours, rest);
}

int send_plain_from_player(object me, string target_id, string payload)
{
    return call_other(MAIL_COMMAND_API_HELPER, "send_plain_from_player",
        this_object(), me, target_id, payload);
}

int send_reward_from_player(object me, string reward_type, string rest)
{
    return call_other(MAIL_COMMAND_API_HELPER, "send_reward_from_player",
        this_object(), me, reward_type, rest);
}
