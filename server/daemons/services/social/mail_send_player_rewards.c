/**
 * @file    mail_send_player_rewards.c
 * @brief   邮件玩家奖励路由辅助子服务



 */

#pragma strict_types

int send_reward_from_player(object mail_service, object me, string reward_type, string rest)
{
    if (!objectp(mail_service) || !objectp(me))
        return 1;

    if (!stringp(rest) || rest == "") {
        msg_send(me, "参数不足。\n");
        return 1;
    }

    switch (reward_type) {
        case "gold":
        case "potential":
        case "exp":
            return call_other("/daemons/services/social/mail_send_player_numeric_rewards",
                "send_numeric_reward_from_player", mail_service, me, reward_type, rest);

        case "item":
            return call_other("/daemons/services/social/mail_send_player_item_rewards",
                "send_item_reward_from_player", mail_service, me, rest);

        default:
            msg_send(me, "不支持的邮件奖励类型。\n");
            return 1;
    }
}
