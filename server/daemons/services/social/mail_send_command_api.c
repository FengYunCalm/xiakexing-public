/**
 * @file    mail_send_command_api.c
 * @brief   邮件命令入口门面子服务



 */

#pragma strict_types

int send_recent_plain_from_operator(object mail_service, object me, int within_hours, string payload)
{
    return call_other("/daemons/services/social/mail_send_operator",
        "send_recent_plain_from_operator", mail_service, me, within_hours, payload);
}

int send_recent_item_from_operator(object mail_service, object me, int within_hours, string rest)
{
    return call_other("/daemons/services/social/mail_send_operator",
        "send_recent_item_from_operator", mail_service, me, within_hours, rest);
}

int send_plain_from_player(object mail_service, object me, string target_id, string payload)
{
    return call_other("/daemons/services/social/mail_send_player_plain",
        "send_plain_from_player", mail_service, me, target_id, payload);
}

int send_reward_from_player(object mail_service, object me, string reward_type, string rest)
{
    return call_other("/daemons/services/social/mail_send_player_rewards",
        "send_reward_from_player", mail_service, me, reward_type, rest);
}
