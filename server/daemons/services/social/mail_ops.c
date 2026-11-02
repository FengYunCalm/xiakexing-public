/**
 * @file    mail_ops.c
 * @brief   邮件操作服务（真源）



 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

mapping query_mailboxes();
int query_mail_state_ready();
void schedule_mail_save();
int has_unclaimed_rewards(mapping mail);
string format_reward_summary(mapping mail);
mixed *deep_copy_mail_list(mixed *mailbox);
mapping claim_attachments(object player, int mail_id);

private int mail_owner_internal(object player)
{
    return objectp(player) && player->query_temp("owner/internal/mail") == 1;
}

private mapping commit_mail_owner_operation(object player, string operation,
    int mail_id)
{
    mapping created;
    mapping prepared;
    mapping committed;
    mapping transaction;
    mapping applied;
    mapping payload;
    string player_id;
    string idempotency_key;
    string explicit_id;

    player_id = objectp(player) ? get_object_id(player) : "";
    if (!stringp(player_id) || player_id == "")
        return ([ "ok": 0, "error": "mail player identity is required" ]);
    /* L-07：同一秒内的合法操作不能共用同一身份（旧实现的秒级 key 会把
     * “同秒第二次操作”误判成重放）。身份 = 操作 + 玩家 + 邮件 + 非重复序号：
     * 真正的重试必须复用调用方传入的显式 operation_id。 */
    explicit_id = stringp(MAIL_D->query_mail_operation_id())
        ? MAIL_D->query_mail_operation_id() : "";
    idempotency_key = explicit_id != ""
        ? sprintf("mail/%s/%s/%s", operation, player_id, explicit_id)
        : sprintf("mail/%s/%s/%d/%d", operation, player_id,
            intp(mail_id) ? mail_id : 0, MAIL_D->next_mail_operation_sequence());
    payload = ([
        "schema_version": 1,
        "mail": ([
            "operation": operation,
            "player_id": player_id,
            "mail_id": mail_id,
        ]),
    ]);
    created = OWNER_COMMIT_D->create_domain_transaction("mail",
        idempotency_key, ({ "owner/player/" + player_id,
            "owner/global/service/mail" }), payload);
    if (!mapp(created) || created["ok"] != 1
        || !mapp(created["transaction"]))
        return ([ "ok": 0, "error": "mail owner transaction create failed" ]);
    prepared = OWNER_COMMIT_D->prepare_transaction(
        created["transaction"]["id"]);
    if (!mapp(prepared) || prepared["ok"] != 1)
        return ([ "ok": 0, "error": "mail owner transaction prepare failed" ]);
    committed = OWNER_COMMIT_D->commit_transaction(
        created["transaction"]["id"]);
    if (!mapp(committed) || committed["ok"] != 1)
        return ([ "ok": 0, "error": "mail owner transaction commit failed",
            "owner_transaction": committed ]);
    transaction = committed["transaction"];
    applied = mapp(transaction) ? transaction["apply_result"] : 0;
    if (!mapp(applied) || applied["ok"] != 1)
        return ([ "ok": 0, "error": "mail owner apply receipt missing" ]);
    return ([ "ok": 1, "data": applied,
        "owner_transaction": committed ]);
}

private int read_mail_internal(object player, int mail_id)
{
    mixed *mailbox;
    mapping mail;
    string player_id;
    int save_result;

    if (!objectp(player) || !intp(mail_id))
        return 0;
    player_id = get_object_id(player);
    if (!stringp(player_id) || player_id == "")
        return 0;

    mailbox = query_mailboxes()[player_id];
    if (!arrayp(mailbox))
        return 0;
    foreach (mail in mailbox) {
        if (mapp(mail) && mail["id"] == mail_id) {
            if (mail["read"])
                return 1;
            mail["read"] = 1;
            save_result = MAIL_D->save_mail_data_checked();
            if (save_result == 1)
                return 1;
            if (save_result == 0)
                mail["read"] = 0;
            return save_result == 2 ? 2 : save_result < 0 ? -1 : 0;
        }
    }
    return 0;
}

private int read_all_mails_internal(object player)
{
    mixed *mailbox;
    mixed *before;
    mapping mailboxes;
    mapping mail;
    string player_id;
    int read_count;
    int save_result;

    if (!objectp(player))
        return 0;
    player_id = get_object_id(player);
    if (!stringp(player_id) || player_id == "")
        return 0;

    mailboxes = query_mailboxes();
    mailbox = mailboxes[player_id];
    if (!arrayp(mailbox))
        return 0;
    before = deep_copy_mail_list(mailbox);

    read_count = 0;
    foreach (mail in mailbox) {
        if (!mapp(mail))
            continue;
        if (!mail["read"]) {
            mail["read"] = 1;
            read_count++;
        }
    }

    if (read_count <= 0)
        return 0;
    save_result = MAIL_D->save_mail_data_checked();
    if (save_result == 1)
        return read_count;
    if (save_result == 0)
        mailboxes[player_id] = before;
    return save_result == 2 ? read_count : save_result < 0 ? -1 : -2;
}

private int delete_mail_internal(object player, int mail_id)
{
    mapping mailboxes;
    mixed *mailbox;
    mixed *before;
    mixed *filtered;
    mapping mail;
    string player_id;
    int save_result;

    if (!objectp(player) || !intp(mail_id))
        return 0;
    player_id = get_object_id(player);
    if (!stringp(player_id) || player_id == "")
        return 0;

    mailboxes = query_mailboxes();
    mailbox = mailboxes[player_id];
    if (!arrayp(mailbox))
        return 0;
    before = deep_copy_mail_list(mailbox);

    filtered = ({ });
    foreach (mail in mailbox) {
        if (mapp(mail) && mail["id"] == mail_id) {
            if (has_unclaimed_rewards(mail))
                return 0;
            continue;
        }
        filtered += ({ mail });
    }

    if (sizeof(filtered) == sizeof(mailbox))
        return 0;

    mailboxes[player_id] = filtered;
    save_result = MAIL_D->save_mail_data_checked();
    if (save_result == 1)
        return 1;
    if (save_result == 0)
        mailboxes[player_id] = before;
    return save_result == 2 ? 2 : save_result < 0 ? -1 : 0;
}

mapping claim_attachments(object player, int mail_id)
{
    mixed *mailbox;
    mapping mail;
    mapping applied;
    mapping settlement;
    mapping marker;
    mixed previous_attachments;
    int previous_gold;
    int previous_potential;
    int previous_exp;
    int previous_claimed;
    int previous_read;
    int save_result;
    string player_id;
    string settlement_key;

    if (!query_mail_state_ready()
        || !objectp(player) || !intp(mail_id))
        return 0;
    player_id = get_object_id(player);
    if (!stringp(player_id) || player_id == "")
        return 0;

    mailbox = query_mailboxes()[player_id];
    if (!arrayp(mailbox))
        return 0;

    foreach (mail in mailbox) {
        if (!mapp(mail) || mail["id"] != mail_id)
            continue;
        if (mail["claimed"])
            return 0;

        /* L-05 / L-230：奖励发放与领取标记原先分离（发奖后只 schedule 保存），
         * 崩溃或保存失败会造成“已到账且仍可再领”。改为一次性结算：
         * 结算与 claim marker 走 REWARD_SETTLEMENT_D（同一稳定 operation id，
         * 重放只返回 duplicate），再同步落盘邮箱；落盘失败时回滚内存标记，
         * 由结算侧幂等保证重试不会重复发奖。 */
        settlement_key = "mail:" + hash("sha256",
            "claim:" + player_id + ":" + mail_id);
        settlement = REWARD_SETTLEMENT_D->settle_reward(player,
            settlement_key, mail,
            ([ "source_mode": "mail", "source_content_id": "mail/" + mail_id ]));
        if (!mapp(settlement) || !settlement["ok"])
            return 0;
        applied = settlement["applied"] || ([ ]);

        marker = REWARD_SETTLEMENT_D->commit_claim_marker(player,
            settlement_key, "mail/claim", ([
                "mail_id": mail_id,
                "player_id": player_id,
                "claimed_at": time(),
            ]));
        if (!mapp(marker) || !marker["ok"])
            return 0;

        previous_attachments = mail["attachments"];
        previous_gold = mail["gold"];
        previous_potential = mail["potential"];
        previous_exp = mail["exp"];
        previous_claimed = mail["claimed"];
        previous_read = mail["read"];
        mail["attachments"] = ({ });
        mail["gold"] = 0;
        mail["potential"] = 0;
        mail["exp"] = 0;
        mail["claimed"] = 1;
        mail["read"] = 1;
        save_result = MAIL_D->save_mail_data_checked();
        if (save_result == 2) {
            /* 邮箱新状态已落盘，不能恢复旧附件；只把原 snapshot 的收据责任交给恢复器。 */
            applied["ok"] = 0;
            applied["state"] = "persisted_receipt_pending";
            applied["persisted"] = 1;
            applied["pending_recovery"] = 1;
            applied["save_phase"] = MAIL_D->query_last_mail_save_phase();
            return applied;
        }
        if (save_result != 1) {
            /* 仅 not_written 才能恢复领取标记；unknown 保留新状态并拒绝继续。 */
            if (save_result == 0) {
                mail["attachments"] = previous_attachments;
                mail["gold"] = previous_gold;
                mail["potential"] = previous_potential;
                mail["exp"] = previous_exp;
                mail["claimed"] = previous_claimed;
                mail["read"] = previous_read;
            }
            log_file("mail", sprintf(
                "%s mail claim marker save failed: player=%s mail=%d phase=%O\n",
                ctime(time()), player_id, mail_id,
                MAIL_D->query_last_mail_save_phase()));
            applied["ok"] = 0;
            applied["state"] = save_result == 0 ? "not_written" : "unknown";
            applied["persisted"] = save_result == 0 ? 0 : -1;
            applied["pending_recovery"] = save_result != 0;
            return applied;
        }
        applied["ok"] = 1;
        applied["state"] = "committed";
        applied["persisted"] = 1;
        return applied;
    }
    return 0;
}

mapping claim_all_attachments(object player)
{
    mixed *mailbox;
    mapping mail;
    string player_id;
    int claimed_count;
    mapping aggregated;
    mapping applied;

    if (!query_mail_state_ready() || !objectp(player))
        return ([ "count": 0, "summary": "" ]);

    player_id = get_object_id(player);
    if (!stringp(player_id) || player_id == "")
        return ([ "count": 0, "summary": "" ]);

    mailbox = query_mailboxes()[player_id];
    if (!arrayp(mailbox))
        return ([ "count": 0, "summary": "" ]);

    claimed_count = 0;
    aggregated = REWARD_FEEDBACK_D->create_feedback_state();

    foreach (mail in mailbox) {
        if (!mapp(mail) || !has_unclaimed_rewards(mail))
            continue;

        applied = claim_attachments(player, mail["id"]);
        if (mapp(applied) && applied["ok"]) {
            claimed_count++;
            aggregated = REWARD_FEEDBACK_D->merge_applied_feedback(aggregated, applied);
        }
    }

    return ([
        "count": claimed_count,
        "applied": aggregated,
    ]);
}

private mapping delete_read_mails_internal(object player)
{
    mapping mailboxes;
    mixed *mailbox;
    mixed *before;
    mixed *filtered;
    mapping mail;
    string player_id;
    int deleted_count;
    int protected_count;
    int save_result;
    string state;

    if (!query_mail_state_ready() || !objectp(player))
        return ([ "deleted": 0, "protected": 0, "state": "not_written" ]);

    player_id = get_object_id(player);
    if (!stringp(player_id) || player_id == "")
        return ([ "deleted": 0, "protected": 0, "state": "not_written" ]);

    mailboxes = query_mailboxes();
    mailbox = mailboxes[player_id];
    if (!arrayp(mailbox))
        return ([ "deleted": 0, "protected": 0, "state": "not_written" ]);
    before = deep_copy_mail_list(mailbox);

    filtered = ({ });
    deleted_count = 0;
    protected_count = 0;
    foreach (mail in mailbox) {
        if (mapp(mail) && (mail["read"] || mail["claimed"])) {
            if (has_unclaimed_rewards(mail)) {
                protected_count++;
                filtered += ({ mail });
                continue;
            }
            deleted_count++;
            continue;
        }
        filtered += ({ mail });
    }

    state = "committed";
    if (deleted_count > 0) {
        mailboxes[player_id] = filtered;
        save_result = MAIL_D->save_mail_data_checked();
        if (save_result == 0) {
            mailboxes[player_id] = before;
            deleted_count = 0;
            state = "not_written";
        } else if (save_result == 2) {
            state = "persisted_receipt_pending";
        } else if (save_result != 1) {
            state = "unknown";
        }
    }

    return ([ "deleted": deleted_count, "protected": protected_count,
        "state": state ]);
}

int read_mail(object player, int mail_id)
{
    mapping transaction;
    mapping applied;
    mapping phase;

    if (!query_mail_state_ready())
        return 0;
    if (mail_owner_internal(player))
        return read_mail_internal(player, mail_id);
    transaction = commit_mail_owner_operation(player, "read", mail_id);
    if (!mapp(transaction) || transaction["ok"] != 1) {
        phase = MAIL_D->query_last_mail_save_phase();
        return mapp(phase) && phase["outcome"] == "unknown" ? -1 : 0;
    }
    applied = transaction["data"];
    if (mapp(applied) && mapp(applied["save_phase"])
        && applied["save_phase"]["outcome"] == "unknown")
        return -1;
    return mapp(applied) && applied["pending_recovery"] ? 2 : 1;
}

int read_all_mails(object player)
{
    mapping transaction;
    mapping phase;

    if (!query_mail_state_ready())
        return 0;
    if (mail_owner_internal(player))
        return read_all_mails_internal(player);
    transaction = commit_mail_owner_operation(player, "read_all", 0);
    if (!mapp(transaction) || transaction["ok"] != 1) {
        phase = MAIL_D->query_last_mail_save_phase();
        if (mapp(phase) && phase["outcome"] == "unknown")
            return -1;
        if (mapp(phase) && phase["outcome"] == "not_written")
            return -2;
        return 0;
    }
    if (mapp(transaction["data"]) && mapp(transaction["data"]["save_phase"])
        && transaction["data"]["save_phase"]["outcome"] == "unknown")
        return -1;
    return mapp(transaction["data"])
        && intp(transaction["data"]["read_count"])
            ? transaction["data"]["read_count"] : 0;
}

int delete_mail(object player, int mail_id)
{
    mapping transaction;
    mapping applied;
    mapping phase;

    if (!query_mail_state_ready())
        return 0;
    if (mail_owner_internal(player))
        return delete_mail_internal(player, mail_id);
    transaction = commit_mail_owner_operation(player, "delete", mail_id);
    if (!mapp(transaction) || transaction["ok"] != 1) {
        phase = MAIL_D->query_last_mail_save_phase();
        return mapp(phase) && phase["outcome"] == "unknown" ? -1 : 0;
    }
    applied = transaction["data"];
    if (mapp(applied) && mapp(applied["save_phase"])
        && applied["save_phase"]["outcome"] == "unknown")
        return -1;
    return mapp(applied) && applied["pending_recovery"] ? 2 : 1;
}

mapping delete_read_mails(object player)
{
    mapping transaction;
    mapping result;
    mapping phase;

    if (!query_mail_state_ready())
        return ([ "deleted": 0, "protected": 0,
            "state": "mail_state_unavailable" ]);
    if (mail_owner_internal(player))
        return delete_read_mails_internal(player);
    transaction = commit_mail_owner_operation(player, "delete_read", 0);
    if (!mapp(transaction) || transaction["ok"] != 1) {
        phase = MAIL_D->query_last_mail_save_phase();
        return ([ "deleted": 0, "protected": 0,
            "state": mapp(phase) && phase["outcome"] == "unknown"
                ? "unknown" : "not_written",
            "owner_transaction": transaction ]);
    }
    result = mapp(transaction["data"])
        && mapp(transaction["data"]["operation_result"])
            ? transaction["data"]["operation_result"] : 0;
    return mapp(result) ? result : ([ "deleted": 0, "protected": 0,
        "state": "not_written" ]);
}
