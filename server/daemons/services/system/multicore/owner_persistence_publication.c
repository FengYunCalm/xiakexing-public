/* File evidence belongs to the snapshot receipt, never to business replay. */
#define PUBLICATION_RETRY_BATCH 32
#define PUBLICATION_RETRY_LIMIT 3
#define PUBLICATION_RETRY_SECONDS 5

nosave int _publication_retry_handle;
nosave mapping _publication_attempts;

mapping mark_snapshot_state(int snapshot_id, string state, string reason);
mapping recover_snapshot_publication(int snapshot_id);
mapping retire_unwritten_snapshot(int snapshot_id, string reason);
private void schedule_publication_retry();

private string publication_file_digest(string path)
{
    string content;
    mixed err;

    if (file_size(path) == -1)
        return "missing";
    if (file_size(path) < 0)
        return "";
    err = catch(content = read_file(path));
    if (err || !stringp(content))
        return "";
    return "sha256:" + hash("sha256", content);
}

mapping query_snapshot_publication(int snapshot_id)
{
    mapping snapshot;
    mapping publication;
    string observed;
    string outcome;

    snapshot = _snapshots[snapshot_id];
    publication = mapp(snapshot) ? snapshot["publication"] : 0;
    if (!mapp(publication)) {
        /* A terminal snapshot may release its publication payload after the
         * owner has durably recorded the verified receipt.  This is a receipt
         * fact, not an inference from target-file existence. */
        if (mapp(snapshot) && snapshot["state"] == "saved"
            && snapshot["terminal_recorded"] == 1)
            return ([ "outcome": "committed", "persisted": 1,
                "snapshot_id": snapshot_id, "evidence_released": 1,
                "retry_token": sprintf("snapshot-retry/%d", snapshot_id) ]);
        return ([ "outcome": "unknown", "persisted": -1,
            "error_code": "PUBLICATION_EVIDENCE_MISSING" ]);
    }
    observed = publication_file_digest(publication["file"]);
    if (observed == "" || (publication["before_digest"] == publication["content_digest"]
        && snapshot["state"] != "saved"))
        outcome = "unknown";
    else if (observed == publication["content_digest"])
        outcome = snapshot["state"] == "saved" && snapshot["terminal_recorded"]
            ? "committed" : "persisted_receipt_pending";
    else if (observed == publication["before_digest"])
        outcome = "not_written";
    else
        outcome = "unknown";
    return ([ "outcome": outcome,
        "persisted": outcome == "unknown" ? -1 : outcome != "not_written",
        "snapshot_id": snapshot_id,
        "matches_candidate": observed != "" && observed == publication["content_digest"],
        "publication": copy(publication),
        "retry_token": sprintf("snapshot-retry/%d", snapshot_id),
    ]);
}

/* Resolve earlier receipts for a target before accepting a newer candidate.
 * A later business mutation must never silently replace an older
 * persisted-but-unreceipted file or strand a not-written receipt in front of
 * the same path. Unknown evidence remains a hard stop. */
mapping recover_publication_for_path(string path, string operation_id)
{
    mapping snapshot;
    mapping evidence;
    mapping result;
    int snapshot_id;
    int recovered;
    int retired;

    if (!stringp(path) || path == "")
        return ([ "ok": 0, "outcome": "unknown",
            "error_code": "PUBLICATION_PATH_INVALID" ]);
    foreach (snapshot_id, snapshot in _snapshots) {
        if (!mapp(snapshot) || snapshot["terminal_recorded"]
            || !mapp(snapshot["publication"])
            || snapshot["publication"]["file"] != path)
            continue;
        if (stringp(operation_id) && operation_id != ""
            && snapshot["idempotency_key"] == operation_id)
            continue;
        evidence = query_snapshot_publication(snapshot_id);
        if (!mapp(evidence))
            return ([ "ok": 0, "outcome": "unknown",
                "snapshot_id": snapshot_id,
                "error_code": "PUBLICATION_EVIDENCE_UNAVAILABLE" ]);
        if (evidence["outcome"] == "committed")
            continue;
        if (evidence["outcome"] == "persisted_receipt_pending") {
            result = recover_snapshot_publication(snapshot_id);
            if (!mapp(result) || result["ok"] != 1) {
                return ([ "ok": 0,
                    "outcome": result["outcome"] || "persisted_receipt_pending",
                    "snapshot_id": snapshot_id,
                    "error_code": result["error_code"] ||
                        "PUBLICATION_RECEIPT_PENDING" ]);
            }
            recovered++;
            continue;
        }
        if (evidence["outcome"] == "not_written") {
            result = retire_unwritten_snapshot(snapshot_id,
                "publication evidence proves prior candidate was not written");
            if (!mapp(result) || result["ok"] != 1) {
                return ([ "ok": 0,
                    "outcome": result["outcome"] || "persisted_receipt_pending",
                    "snapshot_id": snapshot_id,
                    "error_code": result["error_code"] ||
                        "PUBLICATION_RETIRE_PENDING" ]);
            }
            retired++;
            continue;
        }
        return ([ "ok": 0, "outcome": "unknown",
            "snapshot_id": snapshot_id,
            "error_code": "PRIOR_PUBLICATION_UNRESOLVED" ]);
    }
    return ([ "ok": 1, "outcome": "committed",
        "recovered": recovered, "retired": retired ]);
}

/* Bind once, durably, before the caller can publish. Other operations cannot
 * overwrite this file until the old receipt has been settled or proved unwritten. */
mapping prepare_snapshot_publication(int snapshot_id, string path, string content_digest)
{
    mapping snapshot;
    mapping publication;
    mapping other;
    mapping evidence;
    int other_id;

    snapshot = _snapshots[snapshot_id];
    /* The lifecycle daemon is the authorized adapter for player JSON
     * publication; previous_object() is that adapter, not the player object. */
    if (!mapp(snapshot) || !stringp(path) || path == ""
        || !stringp(content_digest) || content_digest == "" )
        return ([ "ok": 0, "error_code": "PUBLICATION_IDENTITY_INVALID" ]);
    publication = snapshot["publication"];
    if (mapp(publication)) {
        if (publication["file"] != path)
            return ([ "ok": 0, "error_code": "PUBLICATION_PATH_CONFLICT" ]);
        if (publication["accepted"] == 1)
            return ([ "ok": 1, "duplicate": 1, "publication": copy(publication) ]);
        if (publication["content_digest"] != content_digest)
            return ([ "ok": 0, "error_code": "PUBLICATION_FROZEN_CANDIDATE_MISMATCH" ]);
    } else {
        foreach (other_id, other in _snapshots) {
            if (other_id == snapshot_id || !mapp(other) || other["terminal_recorded"])
                continue;
            if (!mapp(other["publication"])) {
                if (other["object_id"] == snapshot["object_id"]
                    && other["snapshot_type"] == snapshot["snapshot_type"])
                    return ([ "ok": 0, "error_code": "PRIOR_PUBLICATION_EVIDENCE_MISSING",
                        "snapshot_id": other_id ]);
                continue;
            }
            if (other["publication"]["file"] != path)
                continue;
            evidence = query_snapshot_publication(other_id);
            if (evidence["outcome"] == "persisted_receipt_pending") {
                evidence = mark_snapshot_state(other_id, "saved", "verified file before next publication");
                if (evidence["ok"] == 1)
                    continue;
            } else if (evidence["outcome"] == "not_written") {
                continue;
            }
            return ([ "ok": 0, "error_code": "PRIOR_PUBLICATION_UNRESOLVED",
                "snapshot_id": other_id ]);
        }
        publication = ([ "file": path,
            "before_digest": publication_file_digest(path),
            "content_digest": content_digest, "accepted": 0 ]);
        snapshot["publication"] = publication;
    }
    publication["accepted"] = 1;
    if (!persist_runtime_state()) {
        publication["accepted"] = 0;
        return ([ "ok": 0, "error_code": "PUBLICATION_ACCEPTANCE_FAILED" ]);
    }
    schedule_publication_retry();
    return ([ "ok": 1, "publication": copy(publication) ]);
}

mapping recover_snapshot_publication(int snapshot_id)
{
    mapping evidence;
    mapping terminal;

    evidence = query_snapshot_publication(snapshot_id);
    if (evidence["outcome"] == "committed")
        return evidence + ([ "ok": 1 ]);
    if (evidence["outcome"] != "persisted_receipt_pending")
        return evidence + ([ "ok": 0 ]);
    terminal = mark_snapshot_state(snapshot_id, "saved", "verified frozen publication receipt recovery");
    return evidence + ([ "ok": mapp(terminal) && terminal["ok"] == 1,
        "outcome": mapp(terminal) && terminal["ok"] == 1
            ? "committed" : "persisted_receipt_pending" ]);
}

private void retry_snapshot_publications()
{
    mapping snapshot;
    mapping publication;
    int snapshot_id;
    int processed;

    _publication_retry_handle = 0;
    if (!mapp(_publication_attempts))
        _publication_attempts = ([]);
    foreach (snapshot_id, snapshot in _snapshots) {
        publication = mapp(snapshot) ? snapshot["publication"] : 0;
        if (!mapp(publication) || publication["accepted"] != 1
            || snapshot["terminal_recorded"]
            || _publication_attempts[snapshot_id] >= PUBLICATION_RETRY_LIMIT)
            continue;
        if (processed++ >= PUBLICATION_RETRY_BATCH)
            break;
        _publication_attempts[snapshot_id]++;
        recover_snapshot_publication(snapshot_id);
    }
    schedule_publication_retry();
}

private void schedule_publication_retry()
{
    mapping snapshot;
    int snapshot_id;

    if (_publication_retry_handle)
        return;
    if (!mapp(_publication_attempts))
        _publication_attempts = ([]);
    foreach (snapshot_id, snapshot in _snapshots) {
        if (!mapp(snapshot) || snapshot["terminal_recorded"]
            || !mapp(snapshot["publication"]) || snapshot["publication"]["accepted"] != 1
            || _publication_attempts[snapshot_id] >= PUBLICATION_RETRY_LIMIT)
            continue;
        _publication_retry_handle = call_out((: retry_snapshot_publications :), PUBLICATION_RETRY_SECONDS);
        return;
    }
}
