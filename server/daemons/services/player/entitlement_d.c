/**
 * @file    entitlement_d.c
 * @brief   跨模式传闻、里程碑与内容解锁追加式账本。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

#define ENTITLEMENT_SCHEMA "xkx_entitlement_ledger_v1"

nosave mapping _health_status;
nosave mapping _last_persist_phase;

private int commit_entitlement_state(object actor, mapping ledger,
    string operation)
{
    mapping change;
    mapping response;

    if (!objectp(actor) || !mapp(ledger))
        return 0;
    change = ([ "player/entitlements": ledger ]);
    response = PLAYER_STATE_D->commit_state_fields(actor, change,
        PLAYER_STATE_D->build_state_idempotency_key(actor,
            operation || "entitlement/ledger", change));
    return mapp(response) && response["ok"];
}

private mapping failure(string code, string message)
{
    return ([
        "ok": 0,
        "error_code": code || "entitlement_failed",
        "error": message || "entitlement operation failed",
    ]);
}

private int valid_entitlement_id(string entitlement_id)
{
    return stringp(entitlement_id) && entitlement_id != ""
        && strlen(entitlement_id) <= 160
        && strsrch(entitlement_id, "/") == -1
        && strsrch(entitlement_id, " ") == -1
        && strsrch(entitlement_id, "\t") == -1
        && strsrch(entitlement_id, "\n") == -1;
}

private mapping query_ledger(object actor)
{
    mixed raw;

    if (!objectp(actor))
        return ([]);
    raw = actor->query("player/entitlements");
    return mapp(raw) ? raw : ([]);
}

private int persist_ledger(object actor, mapping ledger)
{
    mapping phase;
    mapping previous;
    mixed err;

    if (!objectp(actor) || !mapp(ledger))
        return 0;
    previous = query_ledger(actor);
    if (!commit_entitlement_state(actor, ledger, "entitlement/ledger"))
        return 0;
    if (!function_exists("save_phases", actor)) {
        _last_persist_phase = ([ "outcome": "not_written",
            "persisted": 0, "save_result": 0,
            "error": "player save phases unavailable" ]);
        commit_entitlement_state(actor, previous,
            "entitlement/ledger_save_unavailable");
        return 0;
    }
    err = catch(phase = actor->save_phases());
    _last_persist_phase = mapp(phase) ? phase : ([
        "outcome": "unknown", "persisted": -1, "save_result": 0,
    ]);
    if (!err && mapp(phase) && phase["outcome"] == "committed")
        return 1;
    /* 只有明确 not_written 才能撤销内存候选；已落盘待收据和
     * unknown 保留新 ledger，交给原 entitlement/recovery 责任继续补收据。 */
    if (!err && mapp(phase) && phase["outcome"] == "not_written")
        commit_entitlement_state(actor, previous, "entitlement/ledger_rollback");
    _health_status["persist_failures"]++;
    _health_status["last_error"] = err ? sprintf("%O", err)
        : "entitlement ledger player save phase incomplete";
    return 0;
}

mapping grant_entitlement(object actor, string entitlement_id, mapping provenance)
{
    mapping ledger;
    mapping record;
    string kind;

    if (!objectp(actor) || !valid_entitlement_id(entitlement_id))
        return failure("invalid_entitlement", "entitlement id is invalid");
    ledger = query_ledger(actor);
    if (mapp(ledger[entitlement_id])) {
        _health_status["duplicate_grants"]++;
        return ([
            "ok": 1,
            "idempotent": 1,
            "entitlement": copy(ledger[entitlement_id]),
        ]);
    }

    kind = mapp(provenance) ? (provenance["kind"] || "content_unlock")
        : "content_unlock";
    if (member_array(kind, ({ "rumor", "milestone", "content_unlock" })) == -1)
        kind = "content_unlock";
    record = ([
        "schema": ENTITLEMENT_SCHEMA,
        "entitlement_id": entitlement_id,
        "kind": kind,
        "source_mode": mapp(provenance) ? (provenance["source_mode"] || "") : "",
        "source_content_id": mapp(provenance) ? (provenance["source_content_id"] || "") : "",
        "granted_at": time(),
    ]);
    ledger[entitlement_id] = record;
    if (!persist_ledger(actor, ledger)) {
        if (!mapp(_last_persist_phase)
            || (_last_persist_phase["outcome"] != "persisted_receipt_pending"
                && _last_persist_phase["outcome"] != "unknown"))
            map_delete(ledger, entitlement_id);
        return failure("persist_failed", "entitlement could not be persisted")
            + ([ "state": mapp(_last_persist_phase)
                    ? _last_persist_phase["outcome"] : "unknown",
                "persisted": mapp(_last_persist_phase)
                    ? _last_persist_phase["persisted"] : -1,
                "pending_recovery": 1 ]);
    }
    _health_status["grants"]++;
    return ([ "ok": 1, "entitlement": copy(record) ]);
}

int has_entitlement(object actor, string entitlement_id)
{
    return mapp(query_ledger(actor)[entitlement_id]);
}

mapping query_entitlements(object actor)
{
    return ([
        "schema": ENTITLEMENT_SCHEMA,
        "entitlements": copy(query_ledger(actor)),
    ]);
}

mapping query_health_snapshot()
{
    mapping snapshot;

    snapshot = copy(_health_status || ([]));
    snapshot["status"] = snapshot["last_error"] == "" ? "ok" : "degraded";
    snapshot["schema"] = ENTITLEMENT_SCHEMA;
    return snapshot;
}

void create()
{
    ::create();
    _health_status = ([
        "grants": 0,
        "duplicate_grants": 0,
        "persist_failures": 0,
        "last_error": "",
    ]);
}
