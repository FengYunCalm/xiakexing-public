/**
 * @file    combat_damage_d.c
 * @brief   战斗伤害多核 proposal 服务
 *
 * 对象读取仍由主线程 combat_math 完成；worker 只消费纯数值快照并产出伤害
 * proposal。本服务负责把 proposal 交给主线程提交守门服务，不能直接扣 HP。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

#define COMBAT_MATH_HELPER "/daemons/services/gameplay/combat/combat_math"
#define WORKER_TIMEOUT_MS  1000
#define MAX_REASONABLE_DAMAGE 1000000

nosave int _worker_damage_count;
nosave int _fallback_damage_count;
nosave int _proposal_committed_count;
nosave int _proposal_rejected_count;
nosave string _last_error;

void create()
{
    ::create();
    _worker_damage_count = 0;
    _fallback_damage_count = 0;
    _proposal_committed_count = 0;
    _proposal_rejected_count = 0;
    _last_error = "";
}

private string query_actor_key(object actor)
{
    string actor_id;

    if (!objectp(actor))
        return "unknown";
    if (function_exists("query_account_id", actor)) {
        actor_id = actor->query_account_id() || "";
        if (actor_id != "")
            return actor_id;
    }
    actor_id = get_object_id(actor);
    if (stringp(actor_id) && actor_id != "")
        return actor_id;
    actor_id = geteuid(actor) || file_name(actor);
    return actor_id != "" ? actor_id : file_name(actor);
}

private string query_owner_key(object attacker, object defender)
{
    return "combat/" + query_actor_key(attacker) + "/" + query_actor_key(defender);
}

private int query_combat_epoch(object attacker, object defender)
{
    int epoch;

    if (objectp(attacker))
        epoch += attacker->query("combat/hp") || 0;
    if (objectp(defender))
        epoch += (defender->query("combat/hp") || 0) * 31;
    return epoch;
}

private int query_snapshot_hash(mapping snapshot)
{
    return call_other(COMBAT_MATH_HELPER, "query_damage_snapshot_hash", snapshot);
}

private int query_fallback_damage(mapping snapshot)
{
    return call_other(COMBAT_MATH_HELPER, "calculate_damage_from_snapshot", snapshot);
}

private int query_reasonable_damage_cap(mapping snapshot)
{
    int attack;
    int cap;

    attack = mapp(snapshot) && intp(snapshot["attack"]) ? snapshot["attack"] : 0;
    cap = attack * 20 + 10000;
    if (cap < 10000)
        cap = 10000;
    if (cap > MAX_REASONABLE_DAMAGE)
        cap = MAX_REASONABLE_DAMAGE;
    return cap;
}

private string query_worker_snapshot_error(mapping snapshot)
{
    mixed key;
    mixed value;

    if (!mapp(snapshot))
        return "snapshot is not mapping";
    foreach (key, value in snapshot) {
        if (!stringp(key))
            return sprintf("snapshot key is not string: %O", key);
        if (undefinedp(value))
            return sprintf("snapshot field is undefined: %s", key);
        if (!intp(value))
            return sprintf("snapshot field is not int: %s=%O", key, value);
    }
    return "";
}

private int query_worker_snapshot_int(mapping snapshot, string field, int default_value)
{
    mixed value;

    value = mapp(snapshot) ? snapshot[field] : 0;
    if (undefinedp(value) || !intp(value))
        return default_value;
    return value;
}

private mapping normalize_worker_snapshot(mapping snapshot)
{
    return ([
        "attack": query_worker_snapshot_int(snapshot, "attack", 1),
        "defense": query_worker_snapshot_int(snapshot, "defense", 0),
        "armor_break": query_worker_snapshot_int(snapshot, "armor_break", 0),
        "critical": query_worker_snapshot_int(snapshot, "critical", 0),
        "critical_resist": query_worker_snapshot_int(snapshot, "critical_resist", 0),
        "armor_break_defense_factor_bp": query_worker_snapshot_int(snapshot, "armor_break_defense_factor_bp", 9000),
        "armor_break_flat": query_worker_snapshot_int(snapshot, "armor_break_flat", 5000),
        "armor_break_cap_bp": query_worker_snapshot_int(snapshot, "armor_break_cap_bp", 8000),
        "reduction_attack_factor_bp": query_worker_snapshot_int(snapshot, "reduction_attack_factor_bp", 16000),
        "reduction_flat": query_worker_snapshot_int(snapshot, "reduction_flat", 8000),
        "reduction_min_bp": query_worker_snapshot_int(snapshot, "reduction_min_bp", 500),
        "reduction_max_bp": query_worker_snapshot_int(snapshot, "reduction_max_bp", 7500),
        "damage_base": query_worker_snapshot_int(snapshot, "damage_base", 1),
        "damage_skill_factor_bp": query_worker_snapshot_int(snapshot, "damage_skill_factor_bp", 10000),
        "damage_random_min_bp": query_worker_snapshot_int(snapshot, "damage_random_min_bp", 9500),
        "damage_random_max_bp": query_worker_snapshot_int(snapshot, "damage_random_max_bp", 10500),
        "damage_min": query_worker_snapshot_int(snapshot, "damage_min", 1),
        "variance_roll_bp": query_worker_snapshot_int(snapshot, "variance_roll_bp", 0),
        "critical_base_bp": query_worker_snapshot_int(snapshot, "critical_base_bp", 500),
        "critical_swing_bp": query_worker_snapshot_int(snapshot, "critical_swing_bp", 3500),
        "critical_flat": query_worker_snapshot_int(snapshot, "critical_flat", 6000),
        "critical_min": query_worker_snapshot_int(snapshot, "critical_min", 0),
        "critical_max": query_worker_snapshot_int(snapshot, "critical_max", 50),
        "critical_roll": query_worker_snapshot_int(snapshot, "critical_roll", 100),
        "critical_damage_factor_bp": query_worker_snapshot_int(snapshot, "critical_damage_factor_bp", 15000),
    ]);
}

private int valid_damage_result(mapping result_spec, mapping snapshot, int expected_hash)
{
    int damage;

    if (!mapp(result_spec) || !intp(result_spec["damage"]))
        return 0;
    if (!intp(result_spec["snapshot_hash"]) || result_spec["snapshot_hash"] != expected_hash)
        return 0;
    if (!intp(result_spec["worker_input_hash"]) || result_spec["worker_input_hash"] == 0)
        return 0;
    damage = result_spec["damage"];
    return damage >= 1 && damage <= query_reasonable_damage_cap(snapshot);
}

private mapping normalize_worker_damage_result(mapping result_spec, int snapshot_hash)
{
    if (!mapp(result_spec))
        return 0;
    if (!intp(result_spec["damage"]) || !intp(result_spec["input_hash"]))
        return 0;
    if (!intp(result_spec["snapshot_hash"]) || result_spec["snapshot_hash"] != snapshot_hash)
        return 0;
    return ([
        "type": "combat_damage",
        "damage": result_spec["damage"],
        "input_hash": snapshot_hash,
        "snapshot_hash": snapshot_hash,
        "worker_input_hash": result_spec["input_hash"],
    ]);
}

private string query_damage_rejection_reason(mapping response, mapping result_spec, mapping snapshot, int expected_hash)
{
    int damage;

    if (!mapp(result_spec)) {
        if (mapp(response) && stringp(response["error"]))
            return "worker result_spec missing: " + response["error"];
        return sprintf("worker result_spec missing: response=%O", response);
    }
    if (!intp(result_spec["damage"]))
        return sprintf("worker damage missing or invalid: %O", result_spec);
    if (!intp(result_spec["snapshot_hash"]) || result_spec["snapshot_hash"] != expected_hash)
        return sprintf("worker snapshot_hash mismatch: expected=%d actual=%O", expected_hash, result_spec["snapshot_hash"]);
    if (!intp(result_spec["worker_input_hash"]) || result_spec["worker_input_hash"] == 0)
        return sprintf("worker input_hash missing or invalid: %O", result_spec);
    damage = result_spec["damage"];
    if (damage < 1 || damage > query_reasonable_damage_cap(snapshot))
        return sprintf("worker damage out of range: damage=%d cap=%d", damage, query_reasonable_damage_cap(snapshot));
    return "worker proposal rejected";
}

private mapping submit_damage_proposal(string owner_key, int owner_epoch, mapping result_spec, string source)
{
    mapping commit;
    mapping proposal;
    mapping submitted;
    int input_hash;

    if (!mapp(result_spec) || !intp(result_spec["damage"]))
        return (["ok":0, "error":"invalid combat damage result"]);
    input_hash = intp(result_spec["input_hash"]) ? result_spec["input_hash"] : 0;
    submitted = PROPOSAL_COMMIT_D->submit_proposal(([
        "type": "combat_damage",
        "owner_key": owner_key,
        "owner_epoch": owner_epoch,
        "input_hash": input_hash,
        "payload": copy(result_spec),
        "expires_at": time() + 5,
    ]));
    if (!mapp(submitted) || !submitted["ok"]) {
        _proposal_rejected_count++;
        _last_error = mapp(submitted) ? (submitted["error"] || "proposal submit failed") : "proposal submit failed";
        return (["ok":0, "error":_last_error]);
    }

    proposal = submitted["proposal"];
    commit = PROPOSAL_COMMIT_D->commit_proposal(submitted["proposal_id"], ([
        "owner_key": owner_key,
        "owner_epoch": owner_epoch,
        "input_hash": input_hash,
    ]), ([
        "damage": result_spec["damage"],
        "source": source,
    ]));
    if (!mapp(commit) || !commit["ok"]) {
        _proposal_rejected_count++;
        _last_error = mapp(commit) ? (commit["reason"] || commit["error"] || "proposal commit failed") : "proposal commit failed";
        return (["ok":0, "error":_last_error, "proposal":proposal]);
    }

    _proposal_committed_count++;
    return ([
        "ok": 1,
        "proposal_id": submitted["proposal_id"],
        "damage": result_spec["damage"],
        "result_spec": copy(result_spec),
    ]);
}

private mapping safe_submit_damage_proposal(string owner_key, int owner_epoch, mapping result_spec, string source)
{
    mapping commit;
    mixed err;

    err = catch(commit = submit_damage_proposal(owner_key, owner_epoch, result_spec, source));
    if (err) {
        _proposal_rejected_count++;
        _last_error = sprintf("proposal %s failed: %O", source || "unknown", err);
        return (["ok":0, "error":_last_error]);
    }
    return mapp(commit) ? commit : (["ok":0, "error":"proposal commit returned invalid response"]);
}

private mapping fallback_damage_proposal(string owner_key, int owner_epoch, mapping snapshot, int snapshot_hash)
{
    mapping commit;
    mapping result_spec;
    int damage;
    mixed err;

    err = catch(damage = query_fallback_damage(snapshot));
    if (err) {
        _fallback_damage_count++;
        _last_error = sprintf("combat damage fallback failed: %O", err);
        return (["ok":0, "error":_last_error]);
    }
    result_spec = ([
        "type": "combat_damage",
        "owner_key": owner_key,
        "damage": damage,
        "input_hash": snapshot_hash,
        "snapshot_hash": snapshot_hash,
        "fallback": 1,
    ]);
    commit = safe_submit_damage_proposal(owner_key, owner_epoch, result_spec, "fallback");
    if (mapp(commit) && commit["ok"]) {
        _fallback_damage_count++;
        return commit + (["source":"fallback"]);
    }
    return commit;
}

mapping query_damage_proposal(object attacker, object defender)
{
    mapping commit;
    mapping response;
    mapping result_spec;
    mapping snapshot;
    string owner_key;
    int snapshot_hash;
    int owner_epoch;
    mixed err;

    if (!objectp(attacker) || !objectp(defender))
        return (["ok":0, "error":"combat participants are required"]);
    owner_key = query_owner_key(attacker, defender);
    owner_epoch = query_combat_epoch(attacker, defender);
    err = catch(snapshot = call_other(COMBAT_MATH_HELPER, "query_damage_worker_snapshot", attacker, defender));
    if (err || !mapp(snapshot)) {
        _last_error = err ? sprintf("combat damage snapshot failed: %O", err) : "combat damage snapshot invalid";
        return (["ok":0, "error":_last_error]);
    }
    snapshot = normalize_worker_snapshot(snapshot);
    snapshot_hash = query_snapshot_hash(snapshot);
    snapshot["snapshot_hash"] = snapshot_hash;
    _last_error = query_worker_snapshot_error(snapshot);
    if (_last_error != "")
        return fallback_damage_proposal(owner_key, owner_epoch, snapshot, snapshot_hash);

    err = catch(response = WORKER_BUS_D->run_driver_worker_task("combat_damage", snapshot, ([
        "owner_key": owner_key,
        "timeout_ms": WORKER_TIMEOUT_MS,
        "ttl_ms": 0,
    ])));
    if (err)
        _last_error = sprintf("combat damage worker failed: %O", err);
    result_spec = !err && mapp(response) && response["success"] && mapp(response["result_spec"])
        ? normalize_worker_damage_result(response["result_spec"], snapshot_hash) : 0;
    if (valid_damage_result(result_spec, snapshot, snapshot_hash)) {
        commit = safe_submit_damage_proposal(owner_key, owner_epoch, result_spec, "worker");
        if (mapp(commit) && commit["ok"]) {
            _worker_damage_count++;
            _last_error = "";
            return commit + (["source":"worker"]);
        }
    }
    if (!err)
        _last_error = query_damage_rejection_reason(response, result_spec, snapshot, snapshot_hash);
    commit = fallback_damage_proposal(owner_key, owner_epoch, snapshot, snapshot_hash);
    if (mapp(commit) && commit["ok"])
        return commit;
    return (["ok":0, "error":_last_error || "combat damage proposal failed"]);
}

int calculate_damage(object attacker, object defender)
{
    mapping proposal;

    proposal = query_damage_proposal(attacker, defender);
    if (mapp(proposal) && proposal["ok"] && intp(proposal["damage"]))
        return proposal["damage"];
    return call_other(COMBAT_MATH_HELPER, "calculate_damage", attacker, defender);
}

mapping query_combat_damage_status()
{
    return ([
        "worker_damage": _worker_damage_count,
        "fallback_damage": _fallback_damage_count,
        "proposal_committed": _proposal_committed_count,
        "proposal_rejected": _proposal_rejected_count,
        "last_error": _last_error,
    ]);
}
