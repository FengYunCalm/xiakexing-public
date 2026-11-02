/**
 * @file    mail_send_player_item_rewards.c
 * @brief   邮件玩家物品奖励发送辅助子服务



 */

#pragma strict_types

#include <system/ansi.h>
#include <system/services.h>

#define MAIL_PARSE_HELPER "/daemons/services/social/mail_send_parse"
#define MAIL_SEND_HELPER  "/daemons/services/social/mail_send_helpers"

int send_item_reward_from_player(object mail_service, object me, string rest)
{
    mapping args;
    mapping attachment;
    mapping parsed;
    mapping sender;
    string target_id;
    string item_name;
    string payload;
    int count;
    int sent;

    if (!objectp(mail_service) || !objectp(me))
        return 1;

    args = call_other(MAIL_PARSE_HELPER, "parse_item_reward_args", me, rest,
        "用法：mail send item <玩家ID> <物品> [数量] <标题>|<正文>\n");
    if (!mapp(args))
        return 1;

    target_id = args["target_id"];
    item_name = args["item_name"];
    count = args["count"];
    payload = args["payload"];
    if (count <= 0) {
        msg_send(me, "收件人或物品数量无效。\n");
        return 1;
    }

    parsed = call_other(MAIL_PARSE_HELPER, "prepare_direct_mail_request",
        me, target_id, payload,
        "格式错误。用法：mail send item <玩家ID> <物品> [数量] <标题>|<正文>\n",
        mail_service);
    if (!mapp(parsed)) {
        return 1;
    }

    attachment = mail_service->build_item_attachment(me, item_name, count);
    if (!mapp(attachment)) {
        msg_send(me, "无法附上这个物品。\n");
        return 1;
    }
    if (!mail_service->consume_item_attachment(me, attachment)) {
        msg_send(me, "移出附件物品失败。\n");
        return 1;
    }

    sender = call_other(MAIL_SEND_HELPER, "query_sender_context", me);
    sent = mail_service->send_named_mail("player", sender["id"], sender["name"],
        parsed["target_id"], parsed["subject"], parsed["body"], ({ attachment }), 0, 0, 0);

    if (sent == 0) {
        /* L-06：投递被拒（未落盘）必须退还被消耗的附件，否则玩家白丢物品。 */
        if (!ITEM_INVENTORY_D->restore_attachment_to_player(me, attachment))
            log_file("mail", sprintf(
                "%s mail attachment refund failed: sender=%s target=%s template=%s\n",
                ctime(time()), get_object_id(me) || "?",
                target_id, attachment["template_id"] || "?"));
    }

    ITEM_FEEDBACK_D->sync_inventory(me);

    return call_other(MAIL_SEND_HELPER, "emit_direct_mail_feedback", me, target_id, sent);
}
