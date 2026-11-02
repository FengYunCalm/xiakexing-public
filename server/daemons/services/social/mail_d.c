/**
 * @file    mail_d.c
 * @brief   邮件服务真源



 */

#pragma strict_types
#pragma save_types

#include <system/globals.h>
#include <system/constants.h>
#include <system/daemon.h>
#include <system/errors.h>
#include <system/ansi.h>
#include <system/services.h>

inherit "/lib/framework/daemon";
// `mail_d.c` is the public mail service facade. Send flow, mailbox operations,
// and helper routines stay split into subservices so callers keep one entry path.
inherit "/daemons/services/social/mail_send";
inherit "/daemons/services/social/mail_ops";
inherit "/daemons/services/social/mail_utils";

mapping _mailboxes;
mapping _broadcast_mails;
int _mail_counter;
nosave int _cleanup_enabled;
nosave int _cleanup_mail_call_out_handle;
nosave int _mail_save_delay;
nosave int _mail_save_handle;
nosave int _mail_save_failures;
nosave mapping _last_mail_save_phase;
nosave mapping _mail_save_receipt_pending;
/* Existing invalid mail state is recovery-pending, not an empty mailbox. */
nosave int _mail_state_status;
nosave string _mail_state_error;

private string get_mail_save_file();
private void restore_mail_data();
private void schedule_mail_save_retry();
int save_mail_data_checked();

mapping query_mailboxes() { return _mailboxes || ([]); }
mapping query_broadcast_mails() { return _broadcast_mails || ([]); }
int query_mail_counter() { return _mail_counter; }
void set_mail_counter(int val) { _mail_counter = val; }
int query_mail_state_ready() { return _mail_state_status == 1; }
string query_mail_state_error() { return _mail_state_error || ""; }
int query_cleanup_enabled() { return _cleanup_enabled; }
int query_cleanup_mail_call_out_handle() { return _cleanup_mail_call_out_handle; }
void set_cleanup_mail_call_out_handle(int val) { _cleanup_mail_call_out_handle = val; }
int query_mail_save_delay() { return _mail_save_delay; }
void set_mail_save_delay(int val) { _mail_save_delay = intp(val) && val >= 0 ? val : _mail_save_delay; }

mapping query_mail_owner_route(string key)
{
    mapping route;
    string route_key;
    mixed err;

    route_key = stringp(key) && key != "" ? key : "mailbox";
    err = catch(route = OWNER_RUNTIME_D->query_service_owner_route("mail", route_key));
    if (err || !mapp(route) || route["ok"] != 1)
        return ([
            "ok": 0,
            "service": "mail",
            "key": route_key,
            "failure_reason": err ? sprintf("owner route query failed: %O", err)
                : (mapp(route) ? route["failure_reason"] : "owner route unavailable"),
            "readiness": mapp(route) ? route["readiness"] : ([ "ok": 0 ]),
            "dependencies": mapp(route) ? route["dependencies"] : ([ ]),
            "direct_cross_owner_write": 0,
        ]);

    route["commit_boundary"] = "service_owner_mailbox_commit";
    route["direct_cross_owner_write"] = 0;
    return route;
}

mapping query_mail_owner_status()
{
    mapping route;

    route = query_mail_owner_route("mailbox");
    if (!mapp(route) || route["ok"] != 1)
        return route;
    return ([
        "service_map_v2": 1,
        "service": "mail",
        "policy": route["policy"],
        "owner_id": route["owner_id"],
        "commit_boundary_required": 1,
        "commit_boundary": route["commit_boundary"],
        "service_shard_owner": route["service_shard_owner"] ? 1 : 0,
        "service_shard_key_required": route["service_shard_key_required"] ? 1 : 0,
        "keyed_service_shard_ready": route["keyed_service_shard_ready"] ? 1 : 0,
        "hot_path_service_owner_single_point": 0,
        "message_only_cross_owner": 1,
        "direct_cross_owner_write": 0,
    ]);
}

mixed *query_mailbox_by_player_id(string player_id, int unread_only)
{
    mixed *mailbox;
    mixed *filtered;
    mapping mail;

    if (!stringp(player_id) || player_id == "")
        return ({ });

    mailbox = query_mailboxes()[player_id];
    if (!arrayp(mailbox))
        return ({ });
    if (!unread_only)
        return mailbox + ({ });

    filtered = ({ });
    foreach (mail in mailbox) {
        if (mapp(mail) && !mail["read"])
            filtered += ({ mail });
    }

    return filtered;
}

private int player_has_broadcast_mail(string player_id, string broadcast_id)
{
    mixed *mailbox;
    mapping mail;

    if (!stringp(player_id) || player_id == "" || !stringp(broadcast_id) || broadcast_id == "")
        return 1;

    mailbox = query_mailboxes()[player_id];
    if (!arrayp(mailbox))
        return 0;

    foreach (mail in mailbox) {
        if (mapp(mail) && mail["broadcast_id"] == broadcast_id)
            return 1;
    }
    return 0;
}

private mapping build_broadcast_mail_record(string player_id, string broadcast_id, mapping spec)
{
    int next_id;

    next_id = query_mail_counter() + 1;
    set_mail_counter(next_id);

    return ([
        "id": next_id,
        "broadcast_id": broadcast_id,
        "from_type": "system",
        "from_id": "system",
        "from_name": spec["from_name"] || "系统",
        "to": player_id,
        "subject": spec["subject"] || "全服邮件",
        "content": spec["content"] || "",
        "attachments": arrayp(spec["attachments"]) ? spec["attachments"] : ({ }),
        "gold": spec["gold"] || 0,
        "potential": spec["potential"] || 0,
        "exp": spec["exp"] || 0,
        "read": 0,
        "claimed": 0,
        "timestamp": spec["start_time"] || time(),
    ]);
}

int sync_broadcast_mails_for_player(object player)
{
    mapping mailboxes;
    mapping spec;
    string broadcast_id;
    string player_id;
    int now;
    int synced;
    int previous_count;
    int previous_counter;
    int save_result;
    int had_mailbox;

    if (!objectp(player))
        return 0;
    player_id = get_object_id(player);
    if (!stringp(player_id) || player_id == "")
        return 0;

    now = time();
    mailboxes = query_mailboxes();
    had_mailbox = arrayp(mailboxes[player_id]);
    if (!had_mailbox)
        mailboxes[player_id] = ({ });
    previous_count = sizeof(mailboxes[player_id]);
    previous_counter = query_mail_counter();

    foreach (broadcast_id, spec in query_broadcast_mails()) {
        if (!mapp(spec) || !spec["enabled"])
            continue;
        if ((spec["start_time"] || 0) > now)
            continue;
        if ((spec["end_time"] || 0) > 0 && (spec["end_time"] || 0) < now)
            continue;
        if (player_has_broadcast_mail(player_id, broadcast_id))
            continue;
        mailboxes[player_id] += ({ build_broadcast_mail_record(player_id, broadcast_id, spec) });
        synced++;
    }

    if (synced <= 0)
        return 0;

    save_result = save_mail_data_checked();
    if (save_result == 0) {
        mailboxes[player_id] = previous_count > 0
            ? mailboxes[player_id][0 .. previous_count - 1] : ({ });
        if (!had_mailbox)
            map_delete(mailboxes, player_id);
        set_mail_counter(previous_counter);
        return 0;
    }
    if (save_result < 0)
        return -1;
    return save_result == 2 ? -synced : synced;
}

int upsert_broadcast_reward_mail(string broadcast_id, string subject, string content,
    int gold, int potential, int exp, int start_time, int end_time)
{
    mapping previous;
    int had_previous;
    int save_result;

    if (!stringp(broadcast_id) || broadcast_id == "")
        return 0;
    if (!mapp(_broadcast_mails))
        _broadcast_mails = ([]);
    had_previous = !undefinedp(_broadcast_mails[broadcast_id]);
    previous = _broadcast_mails[broadcast_id];

    _broadcast_mails[broadcast_id] = ([
        "enabled": 1,
        "from_name": "系统",
        "subject": stringp(subject) ? subject : "全服邮件",
        "content": stringp(content) ? content : "",
        "attachments": ({ }),
        "gold": gold > 0 ? gold : 0,
        "potential": potential > 0 ? potential : 0,
        "exp": exp > 0 ? exp : 0,
        "start_time": start_time > 0 ? start_time : time(),
        "end_time": end_time > 0 ? end_time : 0,
    ]);
    save_result = save_mail_data_checked();
    if (save_result == 1)
        return 1;
    if (save_result == 2) {
        log_file("mail", sprintf(
            "%s broadcast mail receipt pending: %s\n",
            ctime(time()), broadcast_id));
        return 2;
    }
    if (save_result < 0) {
        /* Unknown publication retains the candidate and its receipt owner;
         * restoring the old broadcast would make a later retry publish a
         * different operation. */
        return -1;
    }
    if (had_previous)
        _broadcast_mails[broadcast_id] = previous;
    else
        map_delete(_broadcast_mails, broadcast_id);
    return 0;
}

mapping query_mail_record_by_player_id(string player_id, int mail_id)
{
    mixed *mailbox;
    mapping mail;

    if (!stringp(player_id) || player_id == "" || !intp(mail_id))
        return 0;

    mailbox = query_mailboxes()[player_id];
    if (!arrayp(mailbox))
        return 0;

    foreach (mail in mailbox) {
        if (mapp(mail) && mail["id"] == mail_id)
            return mail;
    }

    return 0;
}

mapping query_mail_runtime_snapshot()
{
    mapping mailboxes;
    mixed *mailbox;
    mapping mail;
    string player_id;
    int mailbox_count;
    int mail_count;
    int unread_count;
    int claimable_count;

    mailboxes = query_mailboxes();
    mailbox_count = 0;
    mail_count = 0;
    unread_count = 0;
    claimable_count = 0;

    foreach (player_id, mailbox in mailboxes) {
        if (!arrayp(mailbox))
            continue;

        mailbox_count += 1;
        foreach (mail in mailbox) {
            if (!mapp(mail))
                continue;
            mail_count += 1;
            if (!mail["read"])
                unread_count += 1;
            if (has_unclaimed_rewards(mail))
                claimable_count += 1;
        }
    }

    return ([
        "mailbox_count": mailbox_count,
        "mail_count": mail_count,
        "unread_count": unread_count,
        "claimable_count": claimable_count,
        "mail_counter": query_mail_counter(),
        "cleanup_enabled": query_cleanup_enabled() ? 1 : 0,
        "mail_save_delay": query_mail_save_delay(),
        "save_pending": _mail_save_handle ? 1 : 0,
        "save_file": get_mail_save_file() + ".o",
    ]);
}

/* Stable-ID owner proposal snapshot for reversible mailbox metadata changes.
 * Reward claiming is intentionally not included here: it must use a dedicated
 * reward+mail settlement proposal rather than restoring a mailbox while the
 * reward side effect remains applied. */
mapping query_owner_proposal_snapshot(string player_id)
{
    mixed mailbox;
    mixed copied;
    mixed err;

    if (!stringp(player_id) || player_id == "")
        return 0;
    mailbox = query_mailboxes()[player_id];
    if (!arrayp(mailbox))
        mailbox = ({ });
    err = catch(copied = restore_variable(save_variable(mailbox)));
    if (err || !arrayp(copied))
        return 0;
    return ([
        "player_id": player_id,
        "mailbox": copied,
        "fingerprint": save_variable(copied),
    ]);
}

int restore_owner_proposal_snapshot(mapping snapshot,
    string expected_current_fingerprint)
{
    string player_id;
    mixed mailbox;
    mixed copied;
    mixed current;
    mixed err;

    if (!mapp(snapshot) || !stringp(snapshot["player_id"])
        || snapshot["player_id"] == "" || !arrayp(snapshot["mailbox"])
        || !stringp(expected_current_fingerprint))
        return 0;
    player_id = snapshot["player_id"];
    current = query_mailboxes()[player_id];
    if (!arrayp(current))
        current = ({ });
    if (save_variable(current) != expected_current_fingerprint)
        return 0;
    err = catch(copied = restore_variable(save_variable(snapshot["mailbox"])));
    if (err || !arrayp(copied))
        return 0;
    if (!mapp(_mailboxes))
        _mailboxes = ([]);
    _mailboxes[player_id] = copied;
    schedule_mail_save();
    return 1;
}

int reload_mail_runtime()
{
    restore_mail_data();
    return 1;
}

private string get_mail_save_file()
{
    return DATA_DIR + "runtime/mail_d";
}

private int recover_mail_save_receipt()
{
    mapping pending;
    mapping lookup;
    mapping recovered;
    mapping snapshot;
    mixed err;
    int snapshot_id;

    pending = mapp(_mail_save_receipt_pending)
        ? _mail_save_receipt_pending : 0;
    if (!mapp(pending)) {
        err = catch(lookup = OWNER_PERSISTENCE_D->query_pending_publication(
            "owner/global/service/mail", "mail_state", get_mail_save_file() + ".o"));
        if (err || !mapp(lookup) || lookup["ok"] != 1) {
            _last_mail_save_phase = ([ "outcome": "unknown", "persisted": -1,
                "save_result": 0, "error": err ? sprintf("%O", err)
                    : "mail receipt lookup failed" ]);
            return -1;
        }
        snapshot = lookup["snapshot"];
        if (!mapp(snapshot))
            return 1;
        pending = ([ "snapshot_id": snapshot["id"],
            "retry_token": "snapshot-retry/" + snapshot["id"] ]);
        _mail_save_receipt_pending = pending;
    }
    snapshot_id = intp(pending["snapshot_id"]) ? pending["snapshot_id"] : 0;
    if (snapshot_id <= 0) {
        _last_mail_save_phase = ([ "outcome": "unknown", "persisted": -1,
            "save_result": 0, "error": "mail receipt identity missing" ]);
        return -1;
    }
    err = catch(recovered = OWNER_PERSISTENCE_D->recover_snapshot_publication(snapshot_id));
    if (err || !mapp(recovered)) {
        _last_mail_save_phase = ([ "outcome": "unknown", "persisted": -1,
            "save_result": 0, "error": err ? sprintf("%O", err)
                : "mail receipt recovery unavailable" ]);
        return -1;
    }
    if (recovered["ok"] != 1 || recovered["outcome"] != "committed") {
        _last_mail_save_phase = ([
            "outcome": recovered["outcome"] == "unknown" ? "unknown"
                : "persisted_receipt_pending",
            "persisted": recovered["outcome"] == "unknown" ? -1 : 1,
            "save_result": recovered["outcome"] == "unknown" ? 0 : 2,
            "retry_token": pending["retry_token"] || "",
            "error": recovered["error_code"] || "mail receipt pending",
        ]);
        return recovered["outcome"] == "unknown" ? -1 : 2;
    }
    _mail_save_receipt_pending = 0;
    _last_mail_save_phase = ([ "outcome": "committed", "persisted": 1,
        "save_result": 1 ]);
    return 1;
}

private int persist_mail_data()
{
    mapping detail;
    int recovery;
    string file;
    string runtime_dir;

    if (!query_mail_state_ready()) {
        _last_mail_save_phase = ([ "outcome": "not_written", "persisted": 0,
            "save_result": 0,
            "error": _mail_state_error || "mail state unavailable" ]);
        return 0;
    }
    file = get_mail_save_file();
    runtime_dir = DATA_DIR + "runtime";

    if (!mapp(_mailboxes))
        _mailboxes = ([]);
    if (!mapp(_broadcast_mails))
        _broadcast_mails = ([]);
    if (!intp(_mail_counter) || _mail_counter < 0)
        _mail_counter = 0;

    if (file_size(runtime_dir) == -1)
        mkdir(runtime_dir);

    recovery = recover_mail_save_receipt();
    if (recovery != 1) {
        if (recovery == 2)
            return 2;
        return recovery;
    }
    detail = owner_save_object_phases(file, "owner/global/service/mail", "mail_state", ([
        "mailbox_count": sizeof(_mailboxes),
        "broadcast_count": sizeof(_broadcast_mails),
        "mail_counter": _mail_counter,
        "state_digest": hash("sha256", save_variable(_mailboxes)
            + save_variable(_broadcast_mails)
            + sprintf("/%d", _mail_counter)),
    ]), "mail/save/" + hash("sha256", save_variable(_mailboxes)
        + save_variable(_broadcast_mails)
        + sprintf("/%d", _mail_counter)));
    if (!mapp(detail)) {
        _last_mail_save_phase = ([ "outcome": "unknown", "persisted": -1,
            "save_result": 0, "error": "mail owner save result unavailable" ]);
        return -1;
    }
    _last_mail_save_phase = copy(detail);
    if (detail["persisted"] != 1) {
        if (detail["outcome"] == "not_written")
            return 0;
        return -1;
    }
    if (detail["ok"] == 1) {
        _mail_save_receipt_pending = 0;
        _last_mail_save_phase["outcome"] = "committed";
        _last_mail_save_phase["save_result"] = 1;
        if (_mail_save_handle) {
            remove_call_out(_mail_save_handle);
            _mail_save_handle = 0;
        }
        return 1;
    }
    _mail_save_receipt_pending = ([
        "snapshot_id": detail["snapshot_id"],
        "retry_token": detail["retry_token"] || "",
    ]);
    _last_mail_save_phase["outcome"] = "persisted_receipt_pending";
    _last_mail_save_phase["save_result"] = 2;
    return 2;
}

#define MAIL_SAVE_RETRY_BASE_DELAY 5
#define MAIL_SAVE_RETRY_MAX_DELAY 300

private void schedule_mail_save_retry()
{
    int delay;

    if (_mail_save_handle)
        return;
    _mail_save_failures++;
    delay = MAIL_SAVE_RETRY_BASE_DELAY;
    if (_mail_save_failures > 1)
        delay = MAIL_SAVE_RETRY_BASE_DELAY << (_mail_save_failures - 1);
    if (delay <= 0 || delay > MAIL_SAVE_RETRY_MAX_DELAY)
        delay = MAIL_SAVE_RETRY_MAX_DELAY;
    _mail_save_handle = call_out("_flush_mail_save", delay);
}

void save_mail_data()
{
    int result;

    result = persist_mail_data();
    if (result == 1) {
        _mail_save_failures = 0;
        return;
    }
    schedule_mail_save_retry();
}

/* L-07：邮件 owner 操作的身份序号与显式重试身份。
 * 默认每次调用都是**新操作**（序号自增，不会与同秒其它操作碰撞）；
 * 需要“重试同一操作”的调用方通过 set_mail_operation_id() 显式传入身份，
 * 用完由 clear_mail_operation_id() 复位，避免被当成通用重放开关。 */
nosave int _mail_operation_sequence;
nosave string _mail_operation_id;

int next_mail_operation_sequence()
{
    _mail_operation_sequence++;
    if (_mail_operation_sequence <= 0)
        _mail_operation_sequence = 1;
    return _mail_operation_sequence;
}

void set_mail_operation_id(string operation_id)
{
    _mail_operation_id = stringp(operation_id) ? operation_id : "";
}

void clear_mail_operation_id()
{
    _mail_operation_id = "";
}

string query_mail_operation_id()
{
    return stringp(_mail_operation_id) ? _mail_operation_id : "";
}

/* L-06：需要判定“邮件是否真的落盘”的调用方用这个入口（如投递方在报成功前）。 */
int save_mail_data_checked()
{
    int result;

    result = persist_mail_data();
    if (result == 1)
        _mail_save_failures = 0;
    else if (result == 2 || result < 0)
        schedule_mail_save_retry();
    return result;
}

mapping query_last_mail_save_phase()
{
    return mapp(_last_mail_save_phase) ? copy(_last_mail_save_phase)
        : ([ "outcome": "not_attempted", "persisted": 0, "save_result": 0 ]);
}

private void _flush_mail_save()
{
    _mail_save_handle = 0;
    save_mail_data();
}

void schedule_mail_save()
{
    if (_mail_save_delay <= 0) {
        save_mail_data();
        return;
    }

    if (_mail_save_handle)
        return;

    _mail_save_handle = call_out("_flush_mail_save", _mail_save_delay);
}

private void restore_mail_data()
{
    string file;
    mixed err;
    int restored;
    int size;

    _mail_state_status = 0;
    _mail_state_error = "";
    file = get_mail_save_file();
    size = file_size(file + ".o");
    if (size == -1) {
        _mailboxes = ([]);
        _broadcast_mails = ([]);
        _mail_counter = 0;
        _mail_state_status = 1;
        return;
    }
    if (size <= 0) {
        _mailboxes = ([]);
        _broadcast_mails = ([]);
        _mail_counter = 0;
        _mail_state_error = "mail state file is empty or not regular";
        return;
    }

    err = catch(restored = restore_object(file));
    if (err || !restored || !mapp(_mailboxes)
        || !mapp(_broadcast_mails) || !intp(_mail_counter)
        || _mail_counter < 0) {
        _mailboxes = ([]);
        _broadcast_mails = ([]);
        _mail_counter = 0;
        _mail_state_error = err ? sprintf("%O", err)
            : !restored ? "mail state restore failed"
            : "mail state shape invalid";
        return;
    }
    _mail_state_status = 1;
}

void create()
{
    ::create();

    restore_mail_data();
    _cleanup_enabled = 1;
    _cleanup_mail_call_out_handle = 0;
    _mail_save_delay = 3;
    _mail_save_handle = 0;
    _mail_save_failures = 0;

    if (_cleanup_enabled && !_cleanup_mail_call_out_handle)
        _cleanup_mail_call_out_handle = call_out("cleanup_expired_mails", 86400);

    log_file("mail", sprintf("[%s] Mail service initialized (v2.0 Facade)\n", ctime(time())[4..19]));
}
