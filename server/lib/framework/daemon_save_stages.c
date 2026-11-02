/**
 * @file    daemon_save_stages.c
 * @brief   daemon 基类 owner 保存阶段逻辑（由 daemon.c 文本包含）
 *
 * @par Description:
 * owner 保存需要区分快照、实际保存与终态登记三个阶段；`daemon.c` 受
 * architecture ratchet（`daemon_framework_lines` 只减不增）约束，因此
 * 本 fragment 由 `daemon.c` 文本包含，编译与运行仍是同一翻译单元。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

nosave mapping _owner_save_admitted_snapshot_ids;

/**
 * @brief 按阶段执行 owner 对象保存并返回结构化结果。
 *
 * 快照、实际保存与终态登记分别判定；任一阶段失败时整体 ok=0，调用方的
 * `saved` 重试路径据此保留待保存 generation。没有真实 owner 快照接纳时
 * 不执行文件写入，避免产生无法归责的业务持久化结果。调用者必须传入
 * 已冻结的非空操作身份；缺失时在快照与文件写入之前返回 not_written。
 */
mapping owner_save_object_detail(string file, string owner_id, string snapshot_type, mapping payload, string idempotency_key)
{
    object persistence;
    mapping snapshot;
    mapping terminal;
    mapping publication;
    mapping evidence;
    mapping prior_publication;
    string candidate;
    string observed_outcome;
    int receipt_only;
    string snapshot_state;
    string save_state;
    string terminal_state;
    string snapshot_error;
    string save_error;
    string terminal_error;
    string retry_token;
    mixed err;
    mixed persist_err;
    mixed terminal_err;
    int snapshot_id;
    int snapshot_ok;
    int save_ok;
    int terminal_ok;
    int saved;
    int ok;
    int snapshot_injected;
    int terminal_injected;
    int save_injected;

    snapshot_state = "failed";
    save_state = "failed";
    terminal_state = "failed";
    snapshot_error = "";
    save_error = "";
    terminal_error = "";
    retry_token = "";
    snapshot_id = 0;
    snapshot_ok = 0;
    save_ok = 0;
    terminal_ok = 0;
    saved = 0;

    if (!stringp(file) || file == ""
        || !stringp(idempotency_key) || idempotency_key == "") {
        snapshot_error = !stringp(file) || file == ""
            ? "SAVE_FILE_INVALID" : "SAVE_IDENTITY_REQUIRED";
        return ([
            "ok": 0,
            "persisted": 0,
            "outcome": "not_written",
            "stages": ([
                "snapshot": ([ "ok": 0, "state": "not_attempted", "error_code": snapshot_error ]),
                "save": ([ "ok": 0, "state": "not_attempted", "error_code": snapshot_error ]),
                "terminal": ([ "ok": 0, "state": "not_attempted", "error_code": snapshot_error ]),
            ]),
            "snapshot_id": 0,
            "retry_token": "",
        ]);
    }

    if (!stringp(owner_id) || owner_id == "")
        owner_id = "owner/global/service/" + file_name(this_object());
    if (!stringp(snapshot_type) || snapshot_type == "")
        snapshot_type = "daemon_state";

    err = catch(persistence = find_object(OWNER_PERSISTENCE_D) || load_object(OWNER_PERSISTENCE_D));
    if (!err && objectp(persistence)) {
        persist_err = catch(prior_publication = persistence->recover_publication_for_path(
            file + ".o", idempotency_key));
        if (persist_err || !mapp(prior_publication)
            || prior_publication["ok"] != 1) {
            if (!mapp(prior_publication))
                prior_publication = ([ "outcome": "unknown",
                    "error_code": "PUBLICATION_RECOVERY_UNAVAILABLE" ]);
            snapshot_error = persist_err ? sprintf("%O", persist_err)
                : prior_publication["error_code"] ||
                    "PRIOR_PUBLICATION_UNRESOLVED";
            return ([
                "ok": 0,
                "persisted": prior_publication["outcome"] ==
                    "persisted_receipt_pending" ? 1 : -1,
                "outcome": prior_publication["outcome"] || "unknown",
                "stages": ([
                    "snapshot": ([ "ok": 0, "state": "blocked",
                        "error_code": snapshot_error ]),
                    "save": ([ "ok": 0, "state": "not_attempted",
                        "error_code": snapshot_error ]),
                    "terminal": ([ "ok": 0, "state": "not_attempted",
                        "error_code": snapshot_error ]),
                ]),
                "snapshot_id": prior_publication["snapshot_id"] || 0,
                "retry_token": prior_publication["retry_token"] || "",
            ]);
        }
    }
    snapshot_injected = consume_test_fault("snapshot_create");
    if (snapshot_injected) {
        snapshot_ok = 0;
        snapshot_state = "failed";
        snapshot_error = "FAULT_INJECTED_SNAPSHOT_REJECTED";
    } else if (!err && objectp(persistence)) {
        persist_err = catch(snapshot = persistence->create_snapshot(this_object(), owner_id,
            snapshot_type, mapp(payload) ? payload : ([]), idempotency_key));
        if (!persist_err && mapp(snapshot) && snapshot["ok"] == 1 && mapp(snapshot["snapshot"])) {
            snapshot_ok = 1;
            snapshot_state = "captured";
            snapshot_id = snapshot["snapshot"]["id"];
        } else {
            if (persist_err)
                snapshot_error = sprintf("%O", persist_err);
            else if (mapp(snapshot) && stringp(snapshot["error_code"]))
                snapshot_error = snapshot["error_code"];
            else
                snapshot_error = "SNAPSHOT_CREATE_REJECTED";
            if (!persist_err && mapp(snapshot) && mapp(snapshot["snapshot"])) {
                snapshot_id = snapshot["snapshot"]["id"];
                if (stringp(snapshot["retry_token"]))
                    retry_token = snapshot["retry_token"];
            }
        }
    } else {
        snapshot_ok = 0;
        snapshot_state = "unavailable";
        snapshot_error = err ? sprintf("%O", err) : "OWNER_PERSISTENCE_UNAVAILABLE";
    }

    /* 已提交操作只重放原收据，不能用旧操作覆盖之后提交的业务状态。
     * pending/失败快照不走此分支；身份冲突仍由 create_snapshot 拒绝。 */
    if (snapshot_ok && snapshot["duplicate"] == 1
        && snapshot["snapshot"]["state"] == "saved"
        && snapshot["snapshot"]["terminal_recorded"] == 1) {
        if (mapp(_owner_save_admitted_snapshot_ids))
            map_delete(_owner_save_admitted_snapshot_ids, snapshot_id);
        return ([
            "ok": 1,
            "persisted": 1,
            "outcome": "committed",
            "stages": ([
                "snapshot": ([ "ok": 1, "state": "captured", "error_code": "" ]),
                "save": ([ "ok": 1, "state": "saved", "error_code": "" ]),
                "terminal": ([ "ok": 1, "state": "saved", "error_code": "" ]),
            ]),
            "snapshot_id": snapshot_id,
            "retry_token": "",
        ]);
    }

    if (!mapp(_owner_save_admitted_snapshot_ids))
        _owner_save_admitted_snapshot_ids = ([]);
    if (snapshot_id > 0 && mapp(snapshot) && snapshot["duplicate"] != 1)
        _owner_save_admitted_snapshot_ids[snapshot_id] = 1;
    if (snapshot_ok && snapshot["duplicate"] == 1
        && !mapp(snapshot["snapshot"]["publication"])
        && !_owner_save_admitted_snapshot_ids[snapshot_id])
        return ([ "ok": 0, "persisted": -1, "outcome": "unknown",
            "snapshot_id": snapshot_id,
            "retry_token": sprintf("snapshot-retry/%d", snapshot_id),
            "stages": ([ "save": ([ "ok": 0, "state": "unknown",
                "error_code": "PUBLICATION_EVIDENCE_MISSING" ]) ]) ]);

    if (snapshot_ok) {
        err = catch(candidate = save_object());
        if (err || !stringp(candidate)) {
            snapshot_ok = 0;
            snapshot_error = "SAVE_CANDIDATE_UNAVAILABLE";
        } else {
            err = catch(publication = persistence->prepare_snapshot_publication(
                snapshot_id, file + ".o", "sha256:" + hash("sha256", candidate)));
            if (err || !mapp(publication) || publication["ok"] != 1) {
                snapshot_ok = 0;
                snapshot_error = err ? sprintf("%O", err)
                    : mapp(publication) ? publication["error_code"] : "PUBLICATION_NOT_ACCEPTED";
            } else if (publication["duplicate"] == 1) {
                evidence = persistence->query_snapshot_publication(snapshot_id);
                observed_outcome = evidence["outcome"];
                if (observed_outcome == "unknown")
                    return ([ "ok": 0, "persisted": -1, "outcome": "unknown",
                        "snapshot_id": snapshot_id,
                        "retry_token": sprintf("snapshot-retry/%d", snapshot_id),
                        "stages": ([ "save": ([ "ok": 0, "state": "unknown",
                            "error_code": "PUBLICATION_EVIDENCE_MISMATCH" ]) ]) ]);
                receipt_only = observed_outcome == "persisted_receipt_pending";
            }
            if (snapshot_ok && !receipt_only
                && publication["publication"]["content_digest"]
                    != "sha256:" + hash("sha256", candidate)) {
                snapshot_ok = 0;
                snapshot_error = "SAVE_FROZEN_CANDIDATE_MISMATCH";
            }
        }
    }

    /*
     * FluffOS 的 save_object() 成功时返回写入的字节数（>0），失败返回 0；
     * 这里按“非零即成功”判定，不能与 1 比较。没有快照接纳时不得写文件。
     */
    if (!snapshot_ok) {
        save_state = "not_attempted";
        save_error = "SNAPSHOT_NOT_ACCEPTED";
    } else if (receipt_only) {
        save_ok = 1;
        save_state = "saved";
        retry_token = sprintf("snapshot-retry/%d", snapshot_id);
    } else {
        save_injected = consume_test_fault("save_object");
        if (save_injected) {
            err = 0;
            saved = 0;
        } else {
            err = catch {
                if (save_object() != candidate)
                    error("save candidate changed after snapshot acceptance");
                saved = save_object(file);
            };
        }
        save_ok = (!err && saved > 0);
        if (save_ok)
            save_state = "saved";
        else
            save_error = err ? sprintf("%O", err) : "save_object returned failure";
    }

    if (save_ok) {
        evidence = persistence->query_snapshot_publication(snapshot_id);
        if (evidence["matches_candidate"] != 1)
            return ([ "ok": 0, "persisted": -1, "outcome": "unknown",
                "snapshot_id": snapshot_id,
                "retry_token": sprintf("snapshot-retry/%d", snapshot_id),
                "stages": ([ "save": ([ "ok": 0, "state": "unknown",
                    "error_code": "PUBLICATION_EVIDENCE_MISMATCH" ]) ]) ]);
        retry_token = sprintf("snapshot-retry/%d", snapshot_id);
    }
    terminal_injected = consume_test_fault("terminal_state");
    if (terminal_injected) {
        terminal_ok = 0;
        terminal_state = "retry_pending";
        terminal_error = "FAULT_INJECTED_TERMINAL_REJECTED";
    } else if (snapshot_ok && snapshot_id > 0) {
        terminal_err = catch(terminal = persistence->mark_snapshot_state(snapshot_id,
            save_ok ? "saved" : "save_failed",
            save_ok ? "save_object applied by owner_save_object" : save_error));
        if (!terminal_err && mapp(terminal) && terminal["ok"] == 1) {
            terminal_ok = 1;
            terminal_state = save_ok ? "saved" : "save_failed";
        } else {
            terminal_state = "retry_pending";
            terminal_error = terminal_err ? sprintf("%O", terminal_err) :
                (mapp(terminal) && stringp(terminal["error_code"]) ? terminal["error_code"] :
                 "SNAPSHOT_TERMINAL_REJECTED");
            if (!terminal_err && mapp(terminal) && stringp(terminal["retry_token"])
                && terminal["retry_token"] != "")
                retry_token = terminal["retry_token"];
        }
    } else if (snapshot_ok) {
        /* 快照阶段已跳过时没有可登记的终态，terminal 随之跳过。 */
        terminal_ok = snapshot_ok;
        terminal_state = "skipped";
    } else {
        terminal_state = "unavailable";
        terminal_error = "SNAPSHOT_MISSING";
    }

    ok = 0;
    if (snapshot_ok && save_ok && terminal_ok) {
        ok = 1;
        map_delete(_owner_save_admitted_snapshot_ids, snapshot_id);
    }

    /* L-89：数据落盘与收据完成必须分开暴露。save 成功但 terminal 待补时
     * ok 仍为 0（阶段未闭合），但 persisted=1 表示磁盘上已经是新数据；
     * 调用者据此重试补收据，绝不能把已落盘的结果当“没写成功”去补偿。 */
    return ([
        "ok": ok,
        "persisted": save_ok,
        "outcome": ok ? "committed"
            : save_ok ? "persisted_receipt_pending" : "not_written",
        "stages": ([
            "snapshot": ([ "ok": snapshot_ok, "state": snapshot_state, "error_code": snapshot_error ]),
            "save": ([ "ok": save_ok, "state": save_state, "error_code": save_error ]),
            "terminal": ([ "ok": terminal_ok, "state": terminal_state, "error_code": terminal_error ]),
        ]),
        "snapshot_id": snapshot_id,
        "retry_token": retry_token,
    ]);
}
