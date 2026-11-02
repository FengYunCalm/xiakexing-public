/**
 * @file    reward_settlement_d.c
 * @brief   共享角色档案之前的幂等奖励结算边界。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

#undef is_player

inherit "/lib/framework/daemon";

#define SETTLEMENT_SCHEMA "xkx_reward_settlement_v2"
#define SETTLEMENT_IDENTITY_FIELDS ({ \
    "source_content_id", "choice_id", "resolution_token" \
})

nosave mapping _health_status;
nosave mapping _last_persist_phase;

int has_pending_settlement(object actor);

private mixed deep_copy(mixed value)
{
    return restore_variable(save_variable(value));
}

private int is_player_actor(object actor)
{
    return objectp(actor) && function_exists("is_player", actor)
        && actor->is_player();
}

private int commit_actor_state(object actor, mapping changes, string operation)
{
    mapping response;

    if (!objectp(actor) || !mapp(changes) || sizeof(changes) == 0)
        return 0;
    if (!is_player_actor(actor))
        return 0;
    response = PLAYER_STATE_D->commit_state_fields(actor, changes,
        PLAYER_STATE_D->build_state_idempotency_key(actor,
            operation || "reward/settlement_state", changes));
    return mapp(response) && response["ok"];
}

private int delete_actor_state(object actor, string path, string operation)
{
    mapping response;

    if (!is_player_actor(actor) || !stringp(path) || path == "")
        return 0;
    response = PLAYER_STATE_D->commit_state_deletions(actor, ({ path }),
        PLAYER_STATE_D->build_state_idempotency_key(actor,
            operation || "reward/settlement_delete", path));
    return mapp(response) && response["ok"];
}

private mapping failure(string code, string message)
{
    return ([
        "ok": 0,
        "error_code": code || "settlement_failed",
        "error": message || "reward settlement failed",
    ]);
}

private int valid_settlement_key(string settlement_key)
{
    return stringp(settlement_key) && settlement_key != ""
        && strlen(settlement_key) <= 192
        && strsrch(settlement_key, "/") == -1
        && strsrch(settlement_key, " ") == -1
        && strsrch(settlement_key, "\t") == -1
        && strsrch(settlement_key, "\n") == -1;
}

private mapping query_ledger(object actor)
{
    mixed raw;

    if (!objectp(actor))
        return ([]);
    raw = actor->query("reward/settlements");
    return mapp(raw) ? deep_copy(raw) : ([]);
}

private int persist_ledger(object actor, mapping ledger)
{
    mapping previous;
    mapping phase;
    mixed err;

    previous = query_ledger(actor);
    if (!commit_actor_state(actor, ([ "reward/settlements":
        deep_copy(ledger) ]), "reward/settlement_ledger"))
        return 0;
    if (!function_exists("save_phases", actor)) {
        _last_persist_phase = ([ "outcome": "not_written",
            "persisted": 0, "save_result": 0,
            "error": "player save phases unavailable" ]);
        commit_actor_state(actor, ([ "reward/settlements": previous ]),
            "reward/settlement_ledger_save_unavailable");
        return 0;
    }
    err = catch(phase = actor->save_phases());
    _last_persist_phase = mapp(phase) ? phase : ([
        "outcome": "unknown", "persisted": -1, "save_result": 0,
    ]);
    if (!err && mapp(phase) && phase["outcome"] == "committed")
        return 1;

    /* pending/unknown 不能恢复旧账本：内存候选可能已经落盘，原收据
     * 责任必须保留给 recovery owner。只有 not_written 才回滚。 */
    if (!mapp(phase) || phase["outcome"] == "not_written") {
        if (!commit_actor_state(actor, ([ "reward/settlements": previous ]),
            "reward/settlement_ledger_rollback"))
            _health_status["last_error"] = "reward settlement state rollback failed";
    }
    _health_status["persist_failures"]++;
    _health_status["last_error"] = err ? sprintf("%O", err)
        : "reward settlement player save phase incomplete";
    return 0;
}

private mapping normalize_provenance(mapping provenance)
{
    return ([
        "source_mode": mapp(provenance) ? (provenance["source_mode"] || "") : "",
        "source_content_id": mapp(provenance) ? (provenance["source_content_id"] || "") : "",
        "choice_id": mapp(provenance) && stringp(provenance["choice_id"])
            ? provenance["choice_id"] : "",
        "resolution_token": mapp(provenance)
            && stringp(provenance["resolution_token"])
            ? provenance["resolution_token"] : "",
        "binding_policy": mapp(provenance) ? (provenance["binding_policy"] || "character") : "character",
        "usage_modes": mapp(provenance) && arrayp(provenance["usage_modes"])
            ? copy(provenance["usage_modes"])
            : ({ }),
    ]);
}

private mapping query_record_provenance(mapping record)
{
    if (mapp(record) && mapp(record["provenance"]))
        return normalize_provenance(record["provenance"]);
    return normalize_provenance(record);
}

private mapping validate_settlement_identity(mapping record, mapping incoming)
{
    mapping stored;
    string field;
    string incoming_value;
    string stored_value;

    if (!mapp(record))
        return ([ "ok": 1 ]);
    stored = query_record_provenance(record);
    foreach (field in SETTLEMENT_IDENTITY_FIELDS) {
        stored_value = stored[field] || "";
        incoming_value = incoming[field] || "";
        if (stored_value != "" && incoming_value != ""
            && stored_value != incoming_value) {
            return failure("settlement_identity_conflict",
                "settlement key is already bound to a different source identity");
        }
    }
    return ([ "ok": 1 ]);
}

mapping bind_settlement_identity(object actor, string settlement_key,
    mapping identity)
{
    mapping incoming;
    mapping ledger;
    mapping record;
    mapping stored;
    mapping validated;
    string field;
    int changed;

    if (!objectp(actor) || !valid_settlement_key(settlement_key)
        || !mapp(identity))
        return failure("invalid_request", "settlement identity binding is invalid");
    ledger = query_ledger(actor);
    record = ledger[settlement_key];
    if (!mapp(record))
        return failure("settlement_not_found", "settlement record does not exist");
    incoming = normalize_provenance(identity);
    validated = validate_settlement_identity(record, incoming);
    if (!validated["ok"])
        return validated;

    stored = query_record_provenance(record);
    foreach (field in SETTLEMENT_IDENTITY_FIELDS) {
        if ((stored[field] || "") == "" && (incoming[field] || "") != "") {
            stored[field] = incoming[field];
            changed = 1;
        }
    }
    if (!changed)
        return ([ "ok": 1, "settlement": copy(record), "changed": 0 ]);

    record["provenance"] = stored;
    record["choice_id"] = stored["choice_id"] || "";
    record["resolution_token"] = stored["resolution_token"] || "";
    record["updated_at"] = time();
    if (!persist_ledger(actor, ledger))
        return failure("identity_persist_failed",
            "settlement identity could not be persisted");
    return ([ "ok": 1, "settlement": copy(record), "changed": 1 ]);
}

private int apply_entitlements(object actor, mapping reward_spec,
    mapping provenance)
{
    mixed entitlement_id;
    mapping result;

    if (!arrayp(reward_spec["entitlements"]))
        return 1;
    foreach (entitlement_id in reward_spec["entitlements"]) {
        if (!stringp(entitlement_id) || entitlement_id == "")
            return 0;
        result = ENTITLEMENT_D->grant_entitlement(actor, entitlement_id, provenance);
        if (!mapp(result) || !result["ok"])
            return 0;
    }
    return 1;
}

private mapping *build_item_summaries(mixed raw_gains, string location)
{
    mapping *summaries;
    mapping gain;
    mapping summary;
    object item;

    summaries = ({ });
    if (!arrayp(raw_gains))
        return summaries;
    foreach (gain in raw_gains) {
        if (!mapp(gain) || !objectp(gain["item"]))
            continue;
        item = gain["item"];
        summary = ([
            "template_id": item->query("template_id") || "",
            "item_path": base_name(item),
            "count": intp(gain["count"]) && gain["count"] > 0
                ? gain["count"] : 1,
            "location": location,
        ]);
        summaries += ({ summary });
    }
    return summaries;
}

private mapping build_applied_summary(mapping applied, mapping reward_spec)
{
    mapping summary;

    if (!mapp(applied))
        return ([]);
    if (mapp(applied["applied_summary"])) {
        summary = copy(applied["applied_summary"]);
        summary["entitlements"] = arrayp(reward_spec["entitlements"])
            ? copy(reward_spec["entitlements"]) : ({ });
        return summary;
    }
    return ([
        "ok": applied["ok"] ? 1 : 0,
        "gold": applied["gold"] || 0,
        "potential": applied["potential"] || 0,
        "exp": applied["exp"] || 0,
        "sect_contribution": applied["sect_contribution"] || 0,
        "reputation": copy(applied["reputation"] || ([])),
        "title": applied["title"] || "",
        "inventory_items": build_item_summaries(
            applied["inventory_item_gains"], "inventory"),
        "room_items": build_item_summaries(
            applied["room_item_gains"], "room"),
        "entitlements": arrayp(reward_spec["entitlements"])
            ? copy(reward_spec["entitlements"]) : ({ }),
    ]);
}

private mapping build_reward_options(mapping record)
{
    mapping provenance;

    provenance = mapp(record["provenance"])
        ? copy(record["provenance"]) : normalize_provenance(record);
    return ([
        "drop_on_full": 1,
        "idempotent": 1,
        "source": "reward_settlement",
        "target_key": record["settlement_key"],
        "provenance": provenance,
        "binding_policy": provenance["binding_policy"],
        "usage_modes": provenance["usage_modes"],
    ]);
}

private mapping mark_recovery(object actor, mapping ledger, mapping record,
    string error_code, string message)
{
    record["status"] = "pending_recovery";
    record["error"] = message || "reward settlement recovery is required";
    record["updated_at"] = time();
    if (!persist_ledger(actor, ledger))
        return failure("recovery_persist_failed",
            "reward settlement recovery state could not be persisted");
    _health_status["failed"]++;
    _health_status["pending_recoveries"]++;
    return failure(error_code || "pending_recovery", record["error"]);
}

private mapping complete_settlement(object actor, mapping ledger, mapping record,
    mapping runtime_applied)
{
    record["phase"] = "committed";
    record["status"] = "applied";
    record["error"] = "";
    record["applied_at"] = time();
    record["updated_at"] = time();
    if (!persist_ledger(actor, ledger)) {
        record["status"] = "pending_recovery";
        record["error"] = "settlement completion save failed";
        _health_status["pending_recoveries"]++;
        return failure("completion_persist_failed",
            "reward applied but ledger completion save failed");
    }
    _health_status["settled"]++;
    return ([
        "ok": 1,
        "settlement": copy(record),
        "applied": mapp(runtime_applied)
            ? runtime_applied : copy(record["applied"] || ([])),
        "applied_summary": copy(record["applied"] || ([])),
    ]);
}

private mapping resume_entitlement_phase(object actor, mapping ledger,
    mapping record, mapping runtime_applied)
{
    mapping provenance;
    mapping reward_spec;

    reward_spec = mapp(record["reward_spec"])
        ? record["reward_spec"] : ([]);
    provenance = mapp(record["provenance"])
        ? record["provenance"] : normalize_provenance(record);
    if (!apply_entitlements(actor, reward_spec, provenance))
        return mark_recovery(actor, ledger, record, "entitlement_apply_failed",
            "reward was applied but entitlement recovery is still pending");
    record["phase"] = "entitlements_applied";
    record["entitlements_applied_at"] = time();
    return complete_settlement(actor, ledger, record, runtime_applied);
}

private mapping resume_reward_phase(object actor, mapping ledger, mapping record)
{
    mapping applied;
    mapping reward_spec;

    reward_spec = mapp(record["reward_spec"])
        ? record["reward_spec"] : ([]);
    applied = REWARD_APPLY_D->apply_reward_package(actor, reward_spec,
        build_reward_options(record));
    if (!mapp(applied) || !applied["ok"])
        return mark_recovery(actor, ledger, record, "reward_apply_failed",
            "reward application failed and can be retried safely");

    record["phase"] = "reward_applied";
    record["status"] = "pending";
    record["error"] = "";
    record["applied"] = build_applied_summary(applied, reward_spec);
    record["reward_applied_at"] = time();
    record["updated_at"] = time();
    /* This save is the durable boundary before entitlement grants, whose own
     * actor saves could otherwise persist rewards without a resumable phase. */
    if (!persist_ledger(actor, ledger)) {
        record["status"] = "pending_recovery";
        record["error"] = "reward phase save failed";
        _health_status["pending_recoveries"]++;
        return failure("reward_phase_persist_failed",
            "reward was applied but its recovery phase could not be persisted");
    }
    return resume_entitlement_phase(actor, ledger, record, applied);
}

private mapping resume_settlement(object actor, mapping ledger, mapping record)
{
    mixed entitlement_id;
    mixed *entitlements;
    string phase;

    phase = stringp(record["phase"]) ? record["phase"] : "";
    if (phase == "") {
        entitlements = mapp(record["reward_spec"])
            && arrayp(record["reward_spec"]["entitlements"])
            ? record["reward_spec"]["entitlements"] : ({ });
        if (record["status"] == "pending_recovery" && sizeof(entitlements) > 0)
            return failure("legacy_pending_recovery",
                "legacy entitlement settlement needs explicit repair");
        phase = "intent_persisted";
        if (record["status"] == "pending" && sizeof(entitlements) > 0) {
            foreach (entitlement_id in entitlements) {
                if (stringp(entitlement_id) && entitlement_id != ""
                    && ENTITLEMENT_D->has_entitlement(actor, entitlement_id)) {
                    phase = "reward_applied";
                    break;
                }
            }
        }
        record["schema"] = SETTLEMENT_SCHEMA;
        record["phase"] = phase;
        record["provenance"] = normalize_provenance(record);
        record["legacy_recovery_upgraded_at"] = time();
        if (!mapp(record["applied"]))
            record["applied"] = ([]);
        if (!persist_ledger(actor, ledger))
            return failure("legacy_recovery_persist_failed",
                "legacy settlement recovery phase could not be persisted");
        _health_status["legacy_recoveries_upgraded"]++;
    }
    if (phase == "reward_applied" || phase == "entitlements_applied")
        return resume_entitlement_phase(actor, ledger, record, 0);
    if (phase == "committed")
        return complete_settlement(actor, ledger, record, 0);
    if (phase == "intent_persisted")
        return resume_reward_phase(actor, ledger, record);
    return failure("legacy_pending_recovery",
        "legacy settlement has no safe recovery phase and needs explicit repair");
}

mapping commit_claim_marker(object actor, string settlement_key,
    string marker_path, mixed marker_value)
{
    mapping ledger;
    mapping record;
    mixed previous_value;
    int had_previous;

    if (!objectp(actor) || !valid_settlement_key(settlement_key)
        || !stringp(marker_path) || marker_path == "")
        return failure("invalid_marker_request", "claim marker request is invalid");

    ledger = query_ledger(actor);
    record = ledger[settlement_key];
    if (!mapp(record) || record["status"] != "applied")
        return failure("settlement_not_applied",
            "claim marker requires an applied settlement");
    if (record["claim_marker_status"] == "committed")
        return ([ "ok": 1, "already_committed": 1,
            "settlement": copy(record) ]);

    previous_value = actor->query(marker_path);
    /* Deployed LPC returns 0 for an absent mapping path; markers are nonzero. */
    had_previous = previous_value != 0;
    if (!commit_actor_state(actor, ([ marker_path: marker_value ]),
        "reward/claim_marker"))
        return failure("claim_marker_commit_failed",
            "claim marker could not be committed");
    record["claim_marker"] = ([
        "path": marker_path,
        "value": copy(marker_value),
    ]);
    record["claim_marker_status"] = "committed";
    record["claim_marker_at"] = time();
    record["updated_at"] = time();
    if (!persist_ledger(actor, ledger)) {
        if (had_previous)
            commit_actor_state(actor, ([ marker_path: previous_value ]),
                "reward/claim_marker_rollback");
        else
            delete_actor_state(actor, marker_path,
                "reward/claim_marker_rollback");
        record["claim_marker_status"] = "pending";
        _health_status["pending_recoveries"]++;
        return failure("claim_marker_persist_failed",
            "claim reward was applied but its success marker could not be persisted");
    }
    return ([ "ok": 1, "settlement": copy(record) ]);
}

mapping settle_reward(object actor, string settlement_key, mapping reward_spec,
    mapping provenance)
{
    mapping identity_check;
    mapping ledger;
    mapping normalized;
    mapping record;

    if (!objectp(actor) || !valid_settlement_key(settlement_key)
        || !mapp(reward_spec))
        return failure("invalid_request", "settlement request is invalid");
    ledger = query_ledger(actor);
    record = ledger[settlement_key];
    normalized = normalize_provenance(provenance);
    identity_check = validate_settlement_identity(record, normalized);
    if (!identity_check["ok"])
        return identity_check;
    if (mapp(record) && record["status"] == "applied") {
        _health_status["deduplicated"]++;
        return ([
            "ok": 1,
            "already_settled": 1,
            "settlement": copy(record),
            "applied": copy(record["applied"] || ([])),
            "applied_summary": copy(record["applied"] || ([])),
        ]);
    }
    if (mapp(record) && member_array(record["status"],
        ({ "pending", "pending_recovery" })) != -1) {
        _health_status["resumed"]++;
        return resume_settlement(actor, ledger, record);
    }

    record = ([
        "schema": SETTLEMENT_SCHEMA,
        "settlement_key": settlement_key,
        "status": "pending",
        "phase": "intent_persisted",
        "source_mode": normalized["source_mode"],
        "source_content_id": normalized["source_content_id"],
        "choice_id": normalized["choice_id"],
        "resolution_token": normalized["resolution_token"],
        "provenance": normalized,
        "reward_spec": copy(reward_spec),
        "created_at": time(),
    ]);
    ledger[settlement_key] = record;
    if (!persist_ledger(actor, ledger)) {
        map_delete(ledger, settlement_key);
        return failure("pending_persist_failed", "settlement intent was not persisted");
    }
    return resume_settlement(actor, ledger, record);
}

mapping recover_settlement(object actor, string settlement_key)
{
    mapping ledger;
    mapping record;

    if (!objectp(actor) || !valid_settlement_key(settlement_key))
        return failure("invalid_request", "settlement recovery request is invalid");
    ledger = query_ledger(actor);
    record = ledger[settlement_key];
    if (!mapp(record))
        return failure("settlement_not_found", "settlement record does not exist");
    if (record["status"] == "applied")
        return settle_reward(actor, settlement_key,
            record["reward_spec"] || ([]), record["provenance"] || ([]));
    _health_status["resumed"]++;
    return resume_settlement(actor, ledger, record);
}

/* 登录恢复的唯一批量消费者：只推进已有 pending 账本，不创建新奖励意图。
 * legacy 无安全 phase 的记录仍由 resume_settlement 拒绝，交给显式修复，
 * 不能因为登录触发而把未知历史状态当成可安全重放。 */
mapping recover_pending_settlements(object actor)
{
    mapping ledger;
    mapping result;
    mapping settlement_result;
    mixed settlement_key;
    mixed record;
    int attempted;
    int recovered;
    int failed;

    if (!objectp(actor))
        return failure("invalid_actor", "reward recovery requires an actor");
    ledger = query_ledger(actor);
    foreach (settlement_key, record in ledger) {
        if (!stringp(settlement_key) || !mapp(record)
            || member_array(record["status"],
                ({ "pending", "pending_recovery" })) == -1)
            continue;
        attempted++;
        settlement_result = recover_settlement(actor, settlement_key);
        if (mapp(settlement_result) && settlement_result["ok"])
            recovered++;
        else
            failed++;
    }
    result = ([
        "ok": failed == 0 ? 1 : 0,
        "attempted": attempted,
        "recovered": recovered,
        "failed": failed,
        "pending": has_pending_settlement(actor),
    ]);
    return result;
}

int is_settled(object actor, string settlement_key)
{
    mapping record;

    record = query_ledger(actor)[settlement_key];
    return mapp(record) && record["status"] == "applied";
}

mapping query_settlement(object actor, string settlement_key)
{
    mapping record;

    record = query_ledger(actor)[settlement_key];
    return mapp(record) ? copy(record) : ([]);
}

int has_pending_settlement(object actor)
{
    mapping record;

    foreach (record in values(query_ledger(actor))) {
        if (mapp(record) && member_array(record["status"],
            ({ "pending", "pending_recovery" })) != -1)
            return 1;
    }
    return 0;
}

mapping query_health_snapshot()
{
    mapping snapshot;

    snapshot = copy(_health_status || ([]));
    snapshot["status"] = snapshot["last_error"] == "" ? "ok" : "degraded";
    snapshot["schema"] = SETTLEMENT_SCHEMA;
    return snapshot;
}

void create()
{
    ::create();
    _health_status = ([
        "settled": 0,
        "deduplicated": 0,
        "resumed": 0,
        "legacy_recoveries_upgraded": 0,
        "failed": 0,
        "pending_recoveries": 0,
        "persist_failures": 0,
        "last_error": "",
    ]);
}
