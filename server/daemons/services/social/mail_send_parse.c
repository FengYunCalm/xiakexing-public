/**
 * @file    mail_send_parse.c
 * @brief   邮件发送解析辅助子服务



 */

#pragma strict_types
#pragma save_types

#define PLAYER_SERVICE "/daemons/services/player/player_d"

string trim_mail_text(string text)
{
    int start;
    int end;

    if (!stringp(text) || text == "")
        return "";

    start = 0;
    end = strlen(text) - 1;

    while (start <= end && (text[start] == ' ' || text[start] == '\t' || text[start] == '\r'))
        start++;
    while (end >= start && (text[end] == ' ' || text[end] == '\t' || text[end] == '\r'))
        end--;

    if (start > end)
        return "";

    return text[start..end];
}

mapping parse_signed_mail_payload(string payload)
{
    string signer;
    string subject;
    string body;

    if (!stringp(payload) || payload == "")
        return 0;

    if (sscanf(payload, "%s|%s|%s", signer, subject, body) != 3)
        return 0;

    signer = trim_mail_text(signer);
    subject = trim_mail_text(subject);
    body = trim_mail_text(body);
    if (signer == "" || subject == "" || body == "")
        return 0;

    return ([
        "sender_name": signer,
        "subject": subject,
        "body": body,
    ]);
}

mapping parse_mail_payload(string payload)
{
    string subject;
    string body;

    if (!stringp(payload) || payload == "")
        return 0;

    if (sscanf(payload, "%s|%s", subject, body) != 2)
        return 0;

    subject = trim_mail_text(subject);
    body = trim_mail_text(body);
    if (subject == "" || body == "")
        return 0;

    return ([
        "subject": subject,
        "body": body,
    ]);
}

int validate_recipient(string target_id)
{
    if (!stringp(target_id) || target_id == "")
        return 0;

    return PLAYER_SERVICE->account_exists(target_id);
}

mapping prepare_direct_mail_request(object me, string target_id, string payload,
    string usage, object mail_service)
{
    mapping parsed;

    if (!validate_recipient(target_id)) {
        msg_send(me, "收件人不存在。\n");
        return 0;
    }

    parsed = parse_mail_payload(payload);
    if (!mapp(parsed)) {
        msg_send(me, usage);
        return 0;
    }

    parsed["target_id"] = target_id;
    return parsed;
}

mapping parse_numeric_reward_args(object me, string rest, string usage)
{
    string target_id;
    string payload;
    int amount;

    if (sscanf(rest, "%s %d %s", target_id, amount, payload) != 3) {
        msg_send(me, usage);
        return 0;
    }

    return ([
        "target_id": target_id,
        "amount": amount,
        "payload": payload,
    ]);
}

mapping parse_item_reward_args(object me, string rest, string usage)
{
    string target_id;
    string item_name;
    string payload;
    int count;

    count = 1;
    if (sscanf(rest, "%s %s %d %s", target_id, item_name, count, payload) != 4) {
        if (sscanf(rest, "%s %s %s", target_id, item_name, payload) != 3) {
            msg_send(me, usage);
            return 0;
        }
        count = 1;
    }

    return ([
        "target_id": target_id,
        "item_name": item_name,
        "count": count,
        "payload": payload,
    ]);
}

mapping revert_numeric_reward(object me, string reward_type, int amount,
    string operation_id)
{
    if (!objectp(me) || amount <= 0)
        return ([ "ok": 0, "error_code": "INVALID_REWARD_REVERT" ]);

    switch (reward_type) {
        case "gold":
            return REWARD_APPLY_D->revert_reward_package(me, ([
                "gold": amount,
                "operation_id": operation_id,
            ]));
        case "potential":
            return REWARD_APPLY_D->revert_reward_package(me, ([
                "potential": amount,
                "operation_id": operation_id,
            ]));
        case "exp":
            return REWARD_APPLY_D->revert_reward_package(me, ([
                "exp": amount,
                "operation_id": operation_id,
            ]));
    }

    return ([ "ok": 0, "error_code": "UNKNOWN_REWARD_TYPE" ]);
}
