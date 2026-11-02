/**
 * @file    mail_send_player_plain.c
 * @brief   邮件玩家普通文本发送辅助子服务



 */

#pragma strict_types

#include <system/ansi.h>

#define MAIL_PARSE_HELPER "/daemons/services/social/mail_send_parse"
#define MAIL_SEND_HELPER  "/daemons/services/social/mail_send_helpers"

int send_plain_from_player(object mail_service, object me, string target_id, string payload)
{
    mapping parsed;
    mapping sender;
    int sent;

    if (!objectp(mail_service) || !objectp(me))
        return 1;

    parsed = call_other(MAIL_PARSE_HELPER, "prepare_direct_mail_request",
        me, target_id, payload,
        "格式错误。用法：mail send <玩家ID> <标题>|<正文>\n",
        mail_service);
    if (!mapp(parsed)) {
        return 1;
    }

    sender = call_other(MAIL_SEND_HELPER, "query_sender_context", me);
    sent = mail_service->send_named_mail("player", sender["id"], sender["name"],
        parsed["target_id"], parsed["subject"], parsed["body"], ({ }), 0, 0, 0);

    return call_other(MAIL_SEND_HELPER, "emit_direct_mail_feedback", me, target_id, sent);
}
