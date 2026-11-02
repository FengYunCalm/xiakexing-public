/**
 * @file    mail_send_player_numeric_rewards.c
 * @brief   邮件玩家数值奖励发送辅助子服务



 */

#pragma strict_types

#include <system/ansi.h>
#include <system/services.h>

#define MAIL_PARSE_HELPER "/daemons/services/social/mail_send_parse"
#define MAIL_SEND_HELPER  "/daemons/services/social/mail_send_helpers"

int send_numeric_reward_from_player(object mail_service, object me, string reward_type, string rest)
{
    mapping args;
    mapping parsed;
    mapping sender;
    string target_id;
    string payload;
    int amount;
    int sent;
    int current_value;
    mapping rollback_result;
    mapping spend_result;
    string usage;

    if (!objectp(mail_service) || !objectp(me))
        return 1;

    if (!stringp(rest) || rest == "") {
        msg_send(me, "参数不足。\n");
        return 1;
    }

    switch (reward_type) {
    case "gold":
        usage = "用法：mail send gold <玩家ID> <金额> <标题>|<正文>\n";
        break;
    case "potential":
        usage = "用法：mail send potential <玩家ID> <数量> <标题>|<正文>\n";
        break;
    case "exp":
        usage = "用法：mail send exp <玩家ID> <数量> <标题>|<正文>\n";
        break;
    default:
        return 0;
    }

    sender = call_other(MAIL_SEND_HELPER, "query_sender_context", me);
    args = call_other(MAIL_PARSE_HELPER, "parse_numeric_reward_args", me, rest, usage);
    if (!mapp(args))
        return 1;

    target_id = args["target_id"];
    amount = args["amount"];
    payload = args["payload"];

    switch (reward_type) {
    case "gold":
        if (amount <= 0) {
            msg_send(me, "收件人或金额无效。\n");
            return 1;
        }
        parsed = call_other(MAIL_PARSE_HELPER, "prepare_direct_mail_request",
            me, target_id, payload, usage, mail_service);
        if (!mapp(parsed)) {
            return 1;
        }
        spend_result = REWARD_APPLY_D->spend_reward_package_detail(me, ([
            "gold": amount,
            "operation_id": ECONOMY_SETTLEMENT_D->issue_money_operation_id(
                "reward", "mail_send", PLAYER_STATE_D->query_player_id(me),
                target_id),
        ]));
        if (!mapp(spend_result) || spend_result["ok"] != 1) {
            msg_send(me, "你的金钱不足。\n");
            return 1;
        }
        sent = mail_service->send_named_mail("player", sender["id"], sender["name"],
            parsed["target_id"], parsed["subject"], parsed["body"], ({ }), amount, 0, 0);
        if (sent == 0) {
            rollback_result = call_other(MAIL_PARSE_HELPER,
                "revert_numeric_reward", me, reward_type, amount,
                spend_result["operation_id"]);
            if (!mapp(rollback_result) || rollback_result["ok"] != 1)
                msg_send(me, "邮件发送失败，原奖励补偿尚未完成，请稍后重试或联系管理员。\n");
        }
        break;

    case "potential":
        current_value = PROGRESSION_D->query_player_potential(me);
        if (amount <= 0 || current_value < amount) {
            msg_send(me, "收件人无效或潜能不足。\n");
            return 1;
        }
        parsed = call_other(MAIL_PARSE_HELPER, "prepare_direct_mail_request",
            me, target_id, payload, usage, mail_service);
        if (!mapp(parsed)) {
            return 1;
        }
        spend_result = REWARD_APPLY_D->spend_reward_package_detail(me, ([
            "potential": amount,
            "operation_id": ECONOMY_SETTLEMENT_D->issue_money_operation_id(
                "reward", "mail_send", PLAYER_STATE_D->query_player_id(me),
                target_id),
        ]));
        if (!mapp(spend_result) || spend_result["ok"] != 1) {
            msg_send(me, "收件人无效或潜能不足。\n");
            return 1;
        }
        sent = mail_service->send_named_mail("player", sender["id"], sender["name"],
            parsed["target_id"], parsed["subject"], parsed["body"], ({ }), 0, amount, 0);
        if (sent == 0) {
            rollback_result = call_other(MAIL_PARSE_HELPER,
                "revert_numeric_reward", me, reward_type, amount,
                spend_result["operation_id"]);
            if (!mapp(rollback_result) || rollback_result["ok"] != 1)
                msg_send(me, "邮件发送失败，原奖励补偿尚未完成，请稍后重试或联系管理员。\n");
        }
        break;

    case "exp":
        current_value = PROGRESSION_D->query_player_combat_exp(me);
        if (amount <= 0 || current_value < amount) {
            msg_send(me, "收件人无效或经验不足。\n");
            return 1;
        }
        parsed = call_other(MAIL_PARSE_HELPER, "prepare_direct_mail_request",
            me, target_id, payload, usage, mail_service);
        if (!mapp(parsed)) {
            return 1;
        }
        spend_result = REWARD_APPLY_D->spend_reward_package_detail(me, ([
            "exp": amount,
            "operation_id": ECONOMY_SETTLEMENT_D->issue_money_operation_id(
                "reward", "mail_send", PLAYER_STATE_D->query_player_id(me),
                target_id),
        ]));
        if (!mapp(spend_result) || spend_result["ok"] != 1) {
            msg_send(me, "收件人无效或经验不足。\n");
            return 1;
        }
        sent = mail_service->send_named_mail("player", sender["id"], sender["name"],
            parsed["target_id"], parsed["subject"], parsed["body"], ({ }), 0, 0, amount);
        if (sent == 0) {
            rollback_result = call_other(MAIL_PARSE_HELPER,
                "revert_numeric_reward", me, reward_type, amount,
                spend_result["operation_id"]);
            if (!mapp(rollback_result) || rollback_result["ok"] != 1)
                msg_send(me, "邮件发送失败，原奖励补偿尚未完成，请稍后重试或联系管理员。\n");
        }
        break;
    }

    return call_other(MAIL_SEND_HELPER, "emit_direct_mail_feedback", me, target_id, sent);
}
