/**
 * @file    owner_domain_handler_d.c
 * @brief   非 player 域的 owner 事务真实 handler 宿主。
 *
 * 背景：owner_commit_d 的默认 handler（prepare/apply/rollback_commit_proposal）
 * 只写提交日志，不改真实领域状态。方案要求每个域事务都能被真实执行：
 * 本宿主把域事务的 payload 分发到各域服务真源的既有提交原语。
 *
 * payload 约定（必须可序列化，禁止对象引用——用稳定 ID / 路径 / token）：
 * - 命令边界事务与 ownercheck 探针不含域 mutation 键：一律 skipped
 *   （boundary_only），与 player 域 handler 的边界语义一致。
 * - move    : ([ "move": ([ "move_token": token ]) ])
 *             token 由 WORLD_TRAVEL_D->register_move_proposal 签发（真源
 *             持有对象句柄），apply 委托 apply_move_proposal（位置 CAS），
 *             rollback 委托 rollback_move_proposal（按 from_path 移回）。
 * - economy : ([ "money": ([ "player_id", "operation_id", "direction",
 *                             "amount" [, "domain", "action"] ]) ])
 *             apply 委托 MONEY_D->apply_money_operation（幂等账本、
 *             参数冲突保护、read-model 失效内建）；rollback 反向账本。
 * - world   : ([ "world_state": ([ "scope_key", "flags": ([k:v]),
 *                                  [, "counter_deltas": ([k:delta]) ] ]) ])
 *             apply 委托 WORLD_STATE_D->set_scope_flags（单次候选发布，
 *             原子批量）+ add_scope_counter；rollback 按 before 整组还原。
 * - item    : ([ "item": ([ "operation": "transfer", "item_instance_id",
 *                            "source_player_id", "target_player_id" ]) ])
 *             payload 只携稳定实例 ID；ITEM_INVENTORY_D 持有物品移动和
 *             回滚真源，handler 的 applied record 才保存本地对象句柄。
 * - team    : ([ "team": ([ "operation", "actor_player_id",
 *                            [, "target_player_id"] ]) ])
 *             TEAM_D 以 stable-ID ledger snapshot 作为本地回滚边界。
 * - mail    : ([ "mail": ([ "operation": "read|read_all|delete|delete_read",
 *                            "player_id" [, "mail_id"] ]) ])
 *             MAIL_D 以 mailbox fingerprint 做 CAS；claim 不走此通用
 *             handler，避免把奖励副作用藏在 mailbox 回滚后。
 * - persistence : ([ "persistence": ([ "operation": "save",
 *                                       "player_id" ]) ])
 *             save 是不可逆 durable side effect；prepare 失败则不保存，
 *             apply 失败显式报告 recovery_required，不伪造 rollback。
 * - trade  : request/accept proposal 以 player ID 解析并以 session_id
 *             校验 cancel 补偿，reject/cancel 不接受通用回滚。
 * - session : ([ "session": ([ "operation": "close", "session_id" ]) ])
 *             gateway close 会先释放 FIFO/绑定；这是不可逆的终止操作。
 * - 玩家状态支撑域（combat/reward/quest/guild/sect/rank/crafting/
 *   life_skill/idle/thievery）:
 *             ([ "player_state": ([ "player_id", "changes": ([path:v]),
 *                                   "deletes": [path], "operation_id" ]) ])
 *             持久效果的真源唯一（PLAYER_STATE_D）：apply 委托
 *             commit_state_mutations；rollback 按 before 逐路径还原，
 *             原删除路径无法还原 → compensation_required。
 * - 其余未列域 : 尚无真实 mutation schema，一律显式 fail-closed，不得静默。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

#undef is_player

inherit "/lib/framework/daemon";

#define DOMAIN_HANDLER_SCHEMA_VERSION 1
#define MAX_APPLIED_MUTATION_HISTORY  4096

int queue_money_compensation(object who, mapping spec);

nosave mapping _applied_mutations;
nosave int _applied_sequence;
nosave int _applied_evictions;
nosave mapping _domain_generations;

mapping query_health_snapshot()
{
    return ([
        "ok": mapp(_applied_mutations),
        "service": "owner_domain_handler",
        "applied_tracked": mapp(_applied_mutations) ? sizeof(_applied_mutations) : 0,
        "applied_evictions": _applied_evictions,
    ]);
}

/* Registration and mutation-schema readiness are separate facts. A domain
 * may use the handler host for command-boundary transactions while still
 * rejecting an unrecognised business proposal; exposing the split prevents a
 * generic host from being mistaken for a domain implementation. */
mapping query_mutation_schema_status()
{
    return ([
        "schema_version": DOMAIN_HANDLER_SCHEMA_VERSION,
        "supported_domains": ({
            "move", "item", "economy", "world",
            "combat", "reward", "quest", "guild", "sect", "rank",
            "crafting", "life_skill", "idle", "thievery", "team", "mail",
            "trade", "persistence", "session",
        }),
        "unsupported_domains": ({
            "room",
        }),
        "boundary_only_is_not_business_apply": 1,
        "direct_cross_owner_write": 0,
    ]);
}

void create()
{
    ::create();
    _applied_mutations = ([ ]);
    _applied_sequence = 0;
    _applied_evictions = 0;
    _domain_generations = ([ ]);
}

private string query_domain(mapping txn)
{
    return mapp(txn) && stringp(txn["domain"]) ? txn["domain"] : "";
}

private mapping query_payload(mapping txn)
{
    return mapp(txn) && mapp(txn["payload"]) ? txn["payload"] : 0;
}

private int is_player_state_delegate_domain(string domain)
{
    return member_array(domain, ({
        "combat", "reward", "quest", "guild", "sect", "rank",
        "crafting", "life_skill", "idle", "thievery",
    })) != -1;
}

private int is_boundary_payload(mapping payload)
{
    /* Only an explicit command/probe marker is boundary-only. An arbitrary
     * payload with no currently-known mutation key must fail closed instead of
     * being mistaken for a harmless command wrapper. */
    if (!mapp(payload))
        return 0;
    if (!undefinedp(payload["move"]) || !undefinedp(payload["money"])
        || !undefinedp(payload["world_state"])
        || !undefinedp(payload["player_state"])
        || !undefinedp(payload["item"])
        || !undefinedp(payload["team"])
        || !undefinedp(payload["mail"])
        || !undefinedp(payload["persistence"])
        || !undefinedp(payload["trade"])
        || !undefinedp(payload["session"]))
        return 0;
    return (stringp(payload["verb"]) && payload["verb"] != "")
        || (stringp(payload["ownercheck"]) && payload["ownercheck"] != "")
        || (!undefinedp(payload["message_id"])
            && intp(payload["message_id"]));
}

private object query_online_player_by_id(string player_id)
{
    object candidate;

    if (!stringp(player_id) || player_id == "")
        return 0;
    foreach (candidate in users()) {
        if (!objectp(candidate) || !function_exists("is_player", candidate)
            || !candidate->is_player())
            continue;
        if (get_object_id(candidate) == player_id)
            return candidate;
    }
    return 0;
}

private object find_item_in_inventory(object owner, string instance_id, int depth)
{
    object item;
    object nested;

    if (!objectp(owner) || !stringp(instance_id) || instance_id == ""
        || depth > 16)
        return 0;
    foreach (item in all_inventory(owner)) {
        if (!objectp(item))
            continue;
        if (item->query("instance_id") == instance_id)
            return item;
        nested = find_item_in_inventory(item, instance_id, depth + 1);
        if (objectp(nested))
            return nested;
    }
    return 0;
}

private object resolve_actor_by_ref(string actor_ref)
{
    object candidate;

    if (!stringp(actor_ref) || actor_ref == "")
        return 0;
    /* 在线角色必在 users()：按 file_name 精确匹配，避免动态对象解析。 */
    foreach (candidate in users()) {
        if (objectp(candidate) && file_name(candidate) == actor_ref)
            return candidate;
    }
    return 0;
}

private string validate_domain_schema(string domain, mapping payload)
{
    mapping mutation;

    switch (domain) {
        case "move":
            mutation = payload["move"];
            if (!mapp(mutation))
                return "move mutation is required";
            if (stringp(mutation["move_token"]) && mutation["move_token"] != "")
                return "";
            if (!stringp(mutation["actor_ref"]) || mutation["actor_ref"] == "")
                return "move actor_ref is required";
            if (!stringp(mutation["target_path"]) || mutation["target_path"] == "")
                return "move target_path is required";
            return "";
        case "economy":
            mutation = payload["money"];
            if (!mapp(mutation))
                return "money mutation is required";
            if (!stringp(mutation["player_id"]) || mutation["player_id"] == "")
                return "money player_id is required";
            if (!stringp(mutation["operation_id"]) || mutation["operation_id"] == "")
                return "money operation_id is required";
            if (mutation["direction"] != "add" && mutation["direction"] != "deduct")
                return "money direction must be add or deduct";
            if (!intp(mutation["amount"]) || mutation["amount"] <= 0)
                return "money amount must be a positive integer";
            return "";
        case "item":
            mutation = payload["item"];
            if (!mapp(mutation))
                return "item mutation is required";
            if (mutation["operation"] != "transfer")
                return "item operation must be transfer";
            if (!stringp(mutation["item_instance_id"])
                || mutation["item_instance_id"] == "")
                return "item_instance_id is required";
            if (!stringp(mutation["source_player_id"])
                || mutation["source_player_id"] == "")
                return "item source_player_id is required";
            if (!stringp(mutation["target_player_id"])
                || mutation["target_player_id"] == "")
                return "item target_player_id is required";
            if (mutation["source_player_id"] == mutation["target_player_id"])
                return "item transfer target must differ from source";
            return "";
        case "team":
            mutation = payload["team"];
            if (!mapp(mutation))
                return "team mutation is required";
            if (member_array(mutation["operation"], ({
                "invite", "accept", "reject", "dismiss", "leave", "kick",
            })) == -1)
                return "team operation is invalid";
            if (!stringp(mutation["actor_player_id"])
                || mutation["actor_player_id"] == "")
                return "team actor_player_id is required";
            if ((mutation["operation"] == "invite"
                || mutation["operation"] == "kick")
                && (!stringp(mutation["target_player_id"])
                    || mutation["target_player_id"] == ""))
                return "team target_player_id is required";
            return "";
        case "mail":
            mutation = payload["mail"];
            if (!mapp(mutation))
                return "mail mutation is required";
            if (member_array(mutation["operation"], ({
                "read", "read_all", "delete", "delete_read",
            })) == -1)
                return "mail operation is unsupported or irreversible";
            if (!stringp(mutation["player_id"]) || mutation["player_id"] == "")
                return "mail player_id is required";
            if ((mutation["operation"] == "read"
                || mutation["operation"] == "delete")
                && (undefinedp(mutation["mail_id"])
                    || !intp(mutation["mail_id"])))
                return "mail_id is required";
            return "";
        case "persistence":
            mutation = payload["persistence"];
            if (!mapp(mutation) || mutation["operation"] != "save")
                return "persistence operation is unsupported";
            if (!stringp(mutation["player_id"]) || mutation["player_id"] == "")
                return "persistence player_id is required";
            return "";
        case "trade":
            mutation = payload["trade"];
            if (!mapp(mutation)
                || member_array(mutation["operation"], ({ "request", "accept" })) == -1)
                return "trade operation is unsupported or irreversible";
            if (!stringp(mutation["actor_player_id"])
                || mutation["actor_player_id"] == "")
                return "trade actor_player_id is required";
            if (mutation["operation"] == "request"
                && (!stringp(mutation["target_player_id"])
                    || mutation["target_player_id"] == ""))
                return "trade target_player_id is required";
            return "";
        case "session":
            mutation = payload["session"];
            if (!mapp(mutation) || mutation["operation"] != "close")
                return "session operation is unsupported";
            if (!stringp(mutation["session_id"])
                || mutation["session_id"] == "")
                return "session_id is required";
            return "";
        case "world":
            mutation = payload["world_state"];
            if (!mapp(mutation))
                return "world_state mutation is required";
            if (!stringp(mutation["scope_key"]) || mutation["scope_key"] == "")
                return "world_state scope_key is required";
            if (!mapp(mutation["flags"]) && !mapp(mutation["counter_deltas"])
                && !stringp(mutation["flag_key"]))
                return "world_state flags or flag_key is required";
            return "";
        default:
            if (is_player_state_delegate_domain(domain)
                && mapp(payload["player_state"]))
                return "";
            return domain + " has no real mutation schema";
    }
}

private string validate_player_state_schema(mapping mutation)
{
    if (!mapp(mutation))
        return "player_state mutation is required";
    if (!stringp(mutation["player_id"]) || mutation["player_id"] == "")
        return "player_state player_id is required";
    if (!stringp(mutation["operation_id"]) || mutation["operation_id"] == "")
        return "player_state operation_id is required";
    if ((!mapp(mutation["changes"]) || sizeof(mutation["changes"]) == 0)
        && (!arrayp(mutation["deletes"]) || sizeof(mutation["deletes"]) == 0))
        return "player_state changes or deletes is required";
    return "";
}

/* apply 成功后记录回滚所需的前状态；键为事务幂等键（字符串），
 * 淘汰按记录内的单调 seq 找最旧，上限 MAX_APPLIED_MUTATION_HISTORY。 */
private void remember_applied(string idempotency_key, mapping record)
{
    string key;
    string oldest_key;

    if (!mapp(_applied_mutations))
        _applied_mutations = ([ ]);
    while (sizeof(_applied_mutations) >= MAX_APPLIED_MUTATION_HISTORY) {
        oldest_key = "";
        foreach (key in keys(_applied_mutations)) {
            if (!mapp(_applied_mutations[key]))
                continue;
            if (oldest_key == ""
                || _applied_mutations[key]["seq"] < _applied_mutations[oldest_key]["seq"])
                oldest_key = key;
        }
        if (oldest_key == "")
            break;
        map_delete(_applied_mutations, oldest_key);
        _applied_evictions++;
    }
    _applied_sequence++;
    record["seq"] = _applied_sequence;
    _applied_mutations[idempotency_key] = record;
}

private void forget_applied(string idempotency_key)
{
    if (mapp(_applied_mutations))
        map_delete(_applied_mutations, idempotency_key);
}

/* apply 成功后向协调器发布本宿主该域的单调水位（观测用水印；
 * 各域真实 CAS 仍由域真源自身保证）。 */
private void publish_domain_generation(string domain)
{
    if (!mapp(_domain_generations))
        _domain_generations = ([ ]);
    _domain_generations[domain] =
        (intp(_domain_generations[domain]) ? _domain_generations[domain] : 0) + 1;
    catch(OWNER_COMMIT_D->set_owner_generation("owner/" + domain + "/handler",
        _domain_generations[domain]));
}

private mapping boundary_result(string phase)
{
    return ([
        "ok": 1,
        phase: 1,
        "skipped": 1,
        "boundary_only": 1,
        "real_apply": 0,
        "direct_cross_owner_write": 0,
    ]);
}

private mapping schema_reject(string phase, string reason)
{
    return ([
        "ok": 0,
        phase: 0,
        "reason": reason,
        "schema_version": DOMAIN_HANDLER_SCHEMA_VERSION,
        "direct_cross_owner_write": 0,
    ]);
}

mapping prepare_domain_proposal(mapping txn)
{
    string domain;
    string error;
    mapping payload;

    domain = query_domain(txn);
    payload = query_payload(txn);
    if (domain == "")
        return schema_reject("prepared", "domain is required");
    if (!mapp(payload))
        return schema_reject("prepared", "domain payload is required");
    if (is_boundary_payload(payload))
        return boundary_result("prepared");
    if (!mapp(txn) || !stringp(txn["idempotency_key"])
        || txn["idempotency_key"] == "")
        return schema_reject("prepared", "idempotency_key is required");
    if (!undefinedp(payload["schema_version"])
        && intp(payload["schema_version"])
        && payload["schema_version"] != DOMAIN_HANDLER_SCHEMA_VERSION)
        return schema_reject("prepared", "domain payload schema version mismatch");
    error = validate_domain_schema(domain, payload);
    if (error != "")
        return schema_reject("prepared", error);
    if (domain == "move" && mapp(payload["move"])
        && stringp(payload["move"]["actor_ref"])
        && !objectp(resolve_actor_by_ref(payload["move"]["actor_ref"])))
        return schema_reject("prepared", "move actor is not online");
    if (domain == "economy"
        && !objectp(query_online_player_by_id(payload["money"]["player_id"])))
        return schema_reject("prepared", "economy owner is not online");
    if (domain == "item" && (!objectp(query_online_player_by_id(
            payload["item"]["source_player_id"]))
        || !objectp(query_online_player_by_id(
            payload["item"]["target_player_id"]))))
        return schema_reject("prepared", "item transfer owner is not online");
    if (domain == "team" && !objectp(query_online_player_by_id(
            payload["team"]["actor_player_id"])))
        return schema_reject("prepared", "team actor is not online");
    if (domain == "team"
        && (payload["team"]["operation"] == "invite"
            || payload["team"]["operation"] == "kick")
        && !objectp(query_online_player_by_id(
            payload["team"]["target_player_id"])))
        return schema_reject("prepared", "team target is not online");
    if (domain == "mail" && !objectp(query_online_player_by_id(
            payload["mail"]["player_id"])))
        return schema_reject("prepared", "mail owner is not online");
    if (domain == "persistence" && !objectp(query_online_player_by_id(
            payload["persistence"]["player_id"])))
        return schema_reject("prepared", "persistence owner is not online");
    if (domain == "trade" && !objectp(query_online_player_by_id(
            payload["trade"]["actor_player_id"])))
        return schema_reject("prepared", "trade actor is not online");
    if (domain == "trade" && payload["trade"]["operation"] == "request"
        && !objectp(query_online_player_by_id(
            payload["trade"]["target_player_id"])))
        return schema_reject("prepared", "trade target is not online");
    if (domain == "session" && !mapp(GATEWAY_SESSION_D->query_session(
            payload["session"]["session_id"])))
        return schema_reject("prepared", "session does not exist");
    if (is_player_state_delegate_domain(domain) && mapp(payload["player_state"])) {
        error = validate_player_state_schema(payload["player_state"]);
        if (error != "")
            return schema_reject("prepared", error);
        if (!objectp(query_online_player_by_id(
                payload["player_state"]["player_id"])))
            return schema_reject("prepared", "player state owner is not online");
    }
    return ([
        "ok": 1,
        "prepared": 1,
        "real_apply": 1,
        "domain": domain,
        "direct_cross_owner_write": 0,
    ]);
}

private mapping apply_move_mutation(mapping txn, mapping mutation,
    string idempotency_key)
{
    mapping applied;
    mapping moved;
    object actor;

    applied = (["domain": "move", "recorded_at": time()]);
    if (stringp(mutation["move_token"]) && mutation["move_token"] != "") {
        moved = WORLD_TRAVEL_D->apply_move_proposal(mutation["move_token"]);
        if (!mapp(moved) || moved["ok"] != 1)
            return ([ "ok": 0, "applied": 0,
                "reason": "move proposal was rejected",
                "handler_result": moved,
                "direct_cross_owner_write": 0 ]);
        /* token 形态的 from_path 由真源（WORLD_TRAVEL_D）登记与回滚。 */
        applied["move_token"] = mutation["move_token"];
        remember_applied(idempotency_key, applied);
        publish_domain_generation("move");
        return ([
            "ok": 1,
            "applied": 1,
            "real_apply": 1,
            "domain": "move",
            "move_result": moved,
            "direct_cross_owner_write": 0,
        ]);
    }
    actor = resolve_actor_by_ref(mutation["actor_ref"]);
    if (!objectp(actor))
        return schema_reject("applied", "move actor is not online");
    applied["from_path"] = objectp(environment(actor))
        ? base_name(environment(actor)) : "";
    applied["actor_ref"] = mutation["actor_ref"];
    moved = WORLD_TRAVEL_D->move_actor(actor, mutation["target_path"], ([
        "reason": stringp(mutation["reason"])
            ? mutation["reason"] : "owner_domain_move",
    ]));
    if (!mapp(moved) || moved["ok"] != 1)
        return ([ "ok": 0, "applied": 0,
            "reason": "move_actor rejected the move",
            "handler_result": moved,
            "direct_cross_owner_write": 0 ]);
    remember_applied(idempotency_key, applied);
    publish_domain_generation("move");
    return ([
        "ok": 1,
        "applied": 1,
        "real_apply": 1,
        "domain": "move",
        "move_result": moved,
        "direct_cross_owner_write": 0,
    ]);
}

private mapping apply_item_mutation(mapping txn, mapping mutation,
    string idempotency_key)
{
    mapping applied;
    mapping transfer;
    object source;
    object target;
    object item;

    source = query_online_player_by_id(mutation["source_player_id"]);
    target = query_online_player_by_id(mutation["target_player_id"]);
    if (!objectp(source) || !objectp(target))
        return schema_reject("applied", "item transfer owner is not online");
    item = find_item_in_inventory(source, mutation["item_instance_id"], 0);
    if (!objectp(item))
        return schema_reject("applied", "item instance is not owned by source");
    transfer = ITEM_INVENTORY_D->transfer_item_between_players(source, target, item);
    if (!mapp(transfer) || transfer["ok"] != 1)
        return ([ "ok": 0, "applied": 0,
            "reason": "item transfer was rejected",
            "handler_result": transfer,
            "direct_cross_owner_write": 0 ]);
    applied = ([
        "domain": "item",
        "recorded_at": time(),
        "source_player_id": mutation["source_player_id"],
        "target_player_id": mutation["target_player_id"],
        "item_instance_id": mutation["item_instance_id"],
        /* The object reference never crosses the transaction payload. */
        "transfer": transfer,
    ]);
    remember_applied(idempotency_key, applied);
    publish_domain_generation("item");
    return ([
        "ok": 1,
        "applied": 1,
        "real_apply": 1,
        "domain": "item",
        "item_instance_id": mutation["item_instance_id"],
        "source_player_id": mutation["source_player_id"],
        "target_player_id": mutation["target_player_id"],
        "direct_cross_owner_write": 0,
    ]);
}

private mapping apply_team_mutation(mapping txn, mapping mutation,
    string idempotency_key)
{
    mapping applied;
    mapping operation_result;
    mapping before;
    object actor;
    object target;
    int ok;
    string operation;
    int previous_internal;

    actor = query_online_player_by_id(mutation["actor_player_id"]);
    target = stringp(mutation["target_player_id"])
        ? query_online_player_by_id(mutation["target_player_id"]) : 0;
    if (!objectp(actor)
        || ((mutation["operation"] == "invite"
            || mutation["operation"] == "kick") && !objectp(target)))
        return schema_reject("applied", "team owner is not online");
    before = TEAM_D->query_owner_proposal_snapshot();
    if (!mapp(before))
        return schema_reject("applied", "team owner snapshot failed");

    operation = mutation["operation"];
    previous_internal = actor->query_temp("owner/internal/team") ? 1 : 0;
    actor->set_temp("owner/internal/team", 1);
    if (operation == "invite") {
        operation_result = TEAM_D->request_team_invite(actor, target);
        ok = mapp(operation_result) && operation_result["ok"] == 1;
    } else if (operation == "accept") {
        operation_result = TEAM_D->accept_team_invite(actor);
        ok = mapp(operation_result) && operation_result["ok"] == 1;
    } else if (operation == "reject") {
        operation_result = TEAM_D->reject_team_invite(actor);
        ok = mapp(operation_result) && operation_result["ok"] == 1;
    } else if (operation == "dismiss") {
        ok = TEAM_D->dismiss_team(actor);
    } else if (operation == "leave") {
        ok = TEAM_D->leave_team(actor);
    } else if (operation == "kick") {
        ok = TEAM_D->kick_team_member(actor, target);
    }
    if (previous_internal)
        actor->set_temp("owner/internal/team", 1);
    else
        actor->delete_temp("owner/internal/team");
    if (!ok)
        return ([ "ok": 0, "applied": 0,
            "reason": "team operation was rejected",
            "handler_result": operation_result,
            "direct_cross_owner_write": 0 ]);

    applied = ([
        "domain": "team",
        "recorded_at": time(),
        "operation": operation,
        "actor_player_id": mutation["actor_player_id"],
        "target_player_id": mutation["target_player_id"] || "",
        "before": before,
    ]);
    remember_applied(idempotency_key, applied);
    publish_domain_generation("team");
    return ([
        "ok": 1,
        "applied": 1,
        "real_apply": 1,
        "domain": "team",
        "operation": operation,
        "actor_player_id": mutation["actor_player_id"],
        "target_player_id": mutation["target_player_id"] || "",
        "team_id": TEAM_D->query_team_id(actor) || "",
        "direct_cross_owner_write": 0,
    ]);
}

private mapping apply_session_mutation(mapping txn, mapping mutation,
    string idempotency_key)
{
    mapping applied;
    mapping result;

    result = GATEWAY_SESSION_D->close_session(mutation["session_id"]);
    if (!mapp(result) || result["ok"] != 1)
        return ([ "ok": 0, "applied": 0,
            "reason": "gateway session close was rejected",
            "handler_result": result,
            "recovery_required": 1, "rollback_supported": 0,
            "direct_cross_owner_write": 0 ]);
    applied = ([
        "domain": "session",
        "recorded_at": time(),
        "operation": "close",
        "session_id": mutation["session_id"],
        "irreversible": 1,
    ]);
    remember_applied(idempotency_key, applied);
    publish_domain_generation("session");
    return ([
        "ok": 1,
        "applied": 1,
        "real_apply": 1,
        "domain": "session",
        "operation": "close",
        "session_id": mutation["session_id"],
        "irreversible": 1,
        "rollback_supported": 0,
        "direct_cross_owner_write": 0,
    ]);
}

private mapping apply_persistence_mutation(mapping txn, mapping mutation,
    string idempotency_key)
{
    mapping applied;
    object player;
    mapping save_phase;
    mixed err;

    player = query_online_player_by_id(mutation["player_id"]);
    if (!objectp(player) || !function_exists("save_phases", player))
        return schema_reject("applied", "persistence owner cannot save phases");
    err = catch {
        save_phase = player->save_phases();
    };
    if (err || !mapp(save_phase)
        || save_phase["outcome"] != "committed")
        return ([ "ok": 0, "applied": 0,
            "reason": err ? sprintf("%O", err)
                : (save_phase["error"] || "durable save phase incomplete"),
            "state": err ? "unknown" : save_phase["outcome"],
            "persisted": err ? -1 : save_phase["persisted"],
            "save_result": err ? 0 : save_phase["save_result"],
            "recovery_required": 1, "rollback_supported": 0,
            "direct_cross_owner_write": 0 ]);

    applied = ([
        "domain": "persistence",
        "recorded_at": time(),
        "operation": "save",
        "player_id": mutation["player_id"],
        "irreversible": 1,
    ]);
    remember_applied(idempotency_key, applied);
    publish_domain_generation("persistence");
    return ([
        "ok": 1,
        "applied": 1,
        "real_apply": 1,
        "domain": "persistence",
        "operation": "save",
        "player_id": mutation["player_id"],
        "irreversible": 1,
        "rollback_supported": 0,
        "direct_cross_owner_write": 0,
    ]);
}

private mapping apply_trade_mutation(mapping txn, mapping mutation,
    string idempotency_key)
{
    mapping applied;
    mapping result;
    mapping session;
    object actor;
    object target;
    int previous_internal;
    mixed previous_response_context;
    mapping response_context;

    actor = query_online_player_by_id(mutation["actor_player_id"]);
    target = mutation["operation"] == "request"
        ? query_online_player_by_id(mutation["target_player_id"]) : 0;
    if (!objectp(actor)
        || (mutation["operation"] == "request" && !objectp(target)))
        return schema_reject("applied", "trade owner is not online");
    response_context = mapp(mutation["response_context"])
        ? copy(mutation["response_context"]) : 0;
    previous_internal = actor->query_temp("owner/internal/trade") ? 1 : 0;
    previous_response_context = actor->query_temp("owner/trade_response_context");
    actor->set_temp("owner/internal/trade", 1);
    actor->set_temp("owner/trade_response_context", response_context);
    if (mutation["operation"] == "request")
        result = TRADE_D->request_trade(actor, target);
    else
        result = TRADE_D->accept_trade(actor);
    if (previous_internal)
        actor->set_temp("owner/internal/trade", 1);
    else
        actor->delete_temp("owner/internal/trade");
    if (mapp(previous_response_context))
        actor->set_temp("owner/trade_response_context", previous_response_context);
    else
        actor->delete_temp("owner/trade_response_context");
    if (!mapp(result) || result["success"] != 1
        || !stringp(result["session_id"]))
        return ([ "ok": 0, "applied": 0,
            "reason": "trade operation was rejected",
            "handler_result": result,
            "direct_cross_owner_write": 0 ]);
    session = TRADE_D->query_trade_session(actor);
    if (!mapp(session) || session["id"] != result["session_id"])
        return schema_reject("applied", "trade session receipt mismatch");

    applied = ([
        "domain": "trade",
        "recorded_at": time(),
        "operation": mutation["operation"],
        "actor_player_id": mutation["actor_player_id"],
        "target_player_id": mutation["target_player_id"] || "",
        "session_id": result["session_id"],
    ]);
    remember_applied(idempotency_key, applied);
    publish_domain_generation("trade");
    return ([
        "ok": 1,
        "applied": 1,
        "real_apply": 1,
        "domain": "trade",
        "operation": mutation["operation"],
        "session_id": result["session_id"],
        "direct_cross_owner_write": 0,
    ]);
}

private mapping apply_mail_mutation(mapping txn, mapping mutation,
    string idempotency_key)
{
    mapping applied;
    mapping before;
    mapping after;
    mapping operation_result;
    mapping save_phase;
    object player;
    int operation_code;
    int ok;
    int pending_recovery;
    string operation;

    player = query_online_player_by_id(mutation["player_id"]);
    if (!objectp(player))
        return schema_reject("applied", "mail owner is not online");
    before = MAIL_D->query_owner_proposal_snapshot(mutation["player_id"]);
    if (!mapp(before))
        return schema_reject("applied", "mail owner snapshot failed");

    operation = mutation["operation"];
    if (operation == "read") {
        operation_code = MAIL_D->read_mail(player, mutation["mail_id"]);
        ok = operation_code != 0;
    } else if (operation == "read_all") {
        operation_code = MAIL_D->read_all_mails(player);
        ok = operation_code >= -1;
    } else if (operation == "delete") {
        operation_code = MAIL_D->delete_mail(player, mutation["mail_id"]);
        ok = operation_code != 0;
    } else if (operation == "delete_read") {
        operation_result = MAIL_D->delete_read_mails(player);
        ok = mapp(operation_result)
            && operation_result["state"] != "not_written";
    }
    save_phase = MAIL_D->query_last_mail_save_phase();
    if (operation_code < 0 && save_phase["outcome"] == "not_written")
        ok = 0;
    if ((mapp(operation_result)
            && (operation_result["state"] == "persisted_receipt_pending"
                || operation_result["state"] == "unknown"))
        || operation_code == 2 || operation_code < 0
        || (operation == "read_all" && operation_code > 0
            && (save_phase["outcome"] == "persisted_receipt_pending"
                || save_phase["outcome"] == "unknown")))
        pending_recovery = 1;
    if (!ok)
        return ([ "ok": 0, "applied": 0,
            "reason": "mail operation was rejected",
            "handler_result": operation_result,
            "direct_cross_owner_write": 0 ]);
    after = MAIL_D->query_owner_proposal_snapshot(mutation["player_id"]);
    if (!mapp(after) || !stringp(after["fingerprint"]))
        return schema_reject("applied", "mail post-apply snapshot failed");

    applied = ([
        "domain": "mail",
        "recorded_at": time(),
        "player_id": mutation["player_id"],
        "operation": operation,
        "before": before,
        "after_fingerprint": after["fingerprint"],
        "pending_recovery": pending_recovery,
        "save_phase": save_phase,
        "operation_result": operation_result,
        "read_count": operation == "read_all" ? operation_code : 0,
    ]);
    remember_applied(idempotency_key, applied);
    publish_domain_generation("mail");
    return ([
        "ok": 1,
        "applied": 1,
        "real_apply": 1,
        "domain": "mail",
        "operation": operation,
        "player_id": mutation["player_id"],
        "mail_id": mutation["mail_id"] || 0,
        "pending_recovery": pending_recovery,
        "persisted": pending_recovery ? save_phase["persisted"] : 1,
        "state": pending_recovery ? save_phase["outcome"] : "committed",
        "operation_result": operation_result,
        "read_count": operation == "read_all" ? operation_code : 0,
        "direct_cross_owner_write": 0,
    ]);
}

private mapping apply_money_mutation(mapping txn, mapping mutation,
    string idempotency_key)
{
    mapping applied;
    mapping receipt;
    mapping spec;
    object player;
    int pending_recovery;

    player = query_online_player_by_id(mutation["player_id"]);
    if (!objectp(player))
        return schema_reject("applied", "economy owner is not online");
    spec = ([
        "operation_id": mutation["operation_id"],
        "direction": mutation["direction"],
        "amount": mutation["amount"],
    ]);
    if (stringp(mutation["money_domain"]) && mutation["money_domain"] != "")
        spec["domain"] = mutation["money_domain"];
    if (stringp(mutation["money_action"]) && mutation["money_action"] != "")
        spec["action"] = mutation["money_action"];
    receipt = MONEY_D->apply_money_operation(player, spec);
    if (!mapp(receipt))
        return ([ "ok": 0, "applied": 0,
            "reason": "money operation returned a non-mapping result",
            "handler_result": receipt,
            "direct_cross_owner_write": 0 ]);
    pending_recovery = receipt["pending_recovery"] ? 1 : 0;
    /* persisted_receipt_pending 已经完成资金业务腿，只缺原收据；接受这
     * 个结果并把恢复责任留给 MONEY_D。unknown/其它失败不能被伪装成提交。 */
    if (receipt["ok"] != 1
        && !(pending_recovery && receipt["applied"] == 1
            && receipt["state"] == "persisted_receipt_pending")) {
        return ([ "ok": 0,
            "applied": receipt["applied"] ? 1 : 0,
            "reason": "money operation was rejected",
            "handler_result": receipt,
            "pending_recovery": pending_recovery,
            "recovery_required": pending_recovery,
            "state": receipt["state"] || "unknown",
            "direct_cross_owner_write": 0 ]);
    }
    applied = ([
        "domain": "economy",
        "recorded_at": time(),
        /* 幂等重复（duplicate）说明账本已无新变更：回滚无需补偿。 */
        "changed": (!receipt["duplicate"] && receipt["applied"]) ? 1 : 0,
        "player_id": mutation["player_id"],
        "operation_id": mutation["operation_id"],
        "direction": mutation["direction"],
        "amount": mutation["amount"],
        "money_domain": stringp(mutation["money_domain"])
            && mutation["money_domain"] != ""
                ? mutation["money_domain"] : "economy",
        "money_action": stringp(mutation["money_action"])
            && mutation["money_action"] != ""
                ? mutation["money_action"] : "owner_mutation",
        "pending_recovery": pending_recovery,
        "state": receipt["state"] || "committed",
    ]);
    remember_applied(idempotency_key, applied);
    publish_domain_generation("economy");
    return ([
        "ok": 1,
        "applied": receipt["applied"] ? 1 : 0,
        "real_apply": 1,
        "domain": "economy",
        "pending_recovery": pending_recovery,
        "state": receipt["state"] || "committed",
        "persisted": receipt["persisted"],
        "money_receipt": ([
            "operation_id": mutation["operation_id"],
            "duplicate": receipt["duplicate"] ? 1 : 0,
            "balance": receipt["balance"],
            "state": receipt["state"] || "committed",
            "pending_recovery": pending_recovery,
        ]),
        "direct_cross_owner_write": 0,
    ]);
}

private mapping apply_world_mutation(mapping txn, mapping mutation,
    string idempotency_key)
{
    mapping applied;
    mapping response;
    mapping counter_deltas;
    mapping flags;

    flags = mapp(mutation["flags"]) ? mutation["flags"]
        : (stringp(mutation["flag_key"])
            ? ([ mutation["flag_key"]: mutation["value"] ]) : 0);
    counter_deltas = mapp(mutation["counter_deltas"])
        ? mutation["counter_deltas"] : 0;
    if (!mapp(flags) && !mapp(counter_deltas))
        return schema_reject("applied", "world_state flags is required");

    /* One owner proposal becomes one world-state candidate. The service
     * records the before/after bucket and its generation for CAS rollback;
     * the handler never performs a flag write followed by separate counter
     * writes. */
    response = WORLD_STATE_D->apply_scope_mutation(mutation["scope_key"],
        mapp(flags) ? flags : ([ ]),
        mapp(counter_deltas) ? counter_deltas : ([ ]),
        stringp(mutation["operation_id"]) && mutation["operation_id"] != ""
            ? mutation["operation_id"] : idempotency_key,
        /* L-09：把发起事务身份交给世界服务，后台重试发布前据此做终态围栏。 */
        mapp(txn) && intp(txn["id"]) ? txn["id"] : 0);
    if (!mapp(response) || response["ok"] != 1)
        return ([ "ok": 0, "applied": 0,
            "reason": "atomic world scope mutation was rejected",
            "handler_result": response,
            "direct_cross_owner_write": 0 ]);
    if (response["duplicate"])
        return ([ "ok": 1, "applied": 0, "duplicate": 1,
            "real_apply": 1, "domain": "world",
            "direct_cross_owner_write": 0 ]);

    applied = ([
        "domain": "world",
        "recorded_at": time(),
        "scope_key": mutation["scope_key"],
        "before_bucket": response["before_bucket"],
        "after_bucket": response["after_bucket"],
        "after_generation": response["generation"],
        "operation_id": stringp(mutation["operation_id"])
            && mutation["operation_id"] != ""
                ? mutation["operation_id"] : idempotency_key,
    ]);
    remember_applied(idempotency_key, applied);
    publish_domain_generation("world");
    return ([
        "ok": 1,
        "applied": 1,
        "real_apply": 1,
        "domain": "world",
        "generation": response["generation"],
        "direct_cross_owner_write": 0,
    ]);
}

private mapping apply_player_state_mutation(mapping txn, mapping mutation,
    string idempotency_key)
{
    mapping applied;
    mapping before;
    mapping before_exists;
    mapping deleted_before;
    mapping deleted_exists;
    mapping changes;
    mapping response;
    object player;
    string key;
    string player_id;
    string *deleted_paths;
    int after_generation;

    player_id = mutation["player_id"];
    player = query_online_player_by_id(player_id);
    if (!objectp(player))
        return schema_reject("applied", "player state owner is not online");
    changes = mapp(mutation["changes"]) ? mutation["changes"] : ([]);
    deleted_paths = arrayp(mutation["deletes"])
        ? mutation["deletes"] + ({ }) : ({ });
    /* 回滚前值记录包含“是否存在”，不能把缺失路径和显式 0 混为一谈。 */
    before = ([ ]);
    before_exists = ([ ]);
    foreach (key in keys(changes)) {
        before[key] = PLAYER_STATE_D->query_state_value(player, key);
        before_exists[key] = PLAYER_STATE_D->query_state_path_exists(player, key);
    }
    deleted_before = ([ ]);
    deleted_exists = ([ ]);
    foreach (key in deleted_paths) {
        deleted_before[key] = PLAYER_STATE_D->query_state_value(player, key);
        deleted_exists[key] = PLAYER_STATE_D->query_state_path_exists(player, key);
    }
    response = PLAYER_STATE_D->commit_state_mutations(player, changes,
        deleted_paths,
        "domain_handler/" + (stringp(mutation["operation_id"])
            ? mutation["operation_id"] : idempotency_key));
    if (!mapp(response) || response["ok"] != 1)
        return ([ "ok": 0, "applied": 0,
            "reason": "player state mutation was rejected",
            "handler_result": response,
            "direct_cross_owner_write": 0 ]);
    after_generation = intp(response["generation"])
        ? response["generation"]
        : PLAYER_STATE_D->query_state_value(player,
            "persistence/player_state_generation");
    applied = ([
        "domain": "player_state",
        "recorded_at": time(),
        "player_id": player_id,
        "before": before,
        "before_exists": before_exists,
        "deleted_before": deleted_before,
        "deleted_exists": deleted_exists,
        "changes_keys": keys(changes),
        "deleted_paths": deleted_paths,
        "changed": response["duplicate"] ? 0 : 1,
        "after_generation": after_generation,
        "operation_id": stringp(mutation["operation_id"])
            ? mutation["operation_id"] : idempotency_key,
    ]);
    remember_applied(idempotency_key, applied);
    return ([
        "ok": 1,
        "applied": 1,
        "real_apply": 1,
        "domain": query_domain(txn),
        "direct_cross_owner_write": 0,
    ]);
}

mapping apply_domain_proposal(mapping txn)
{
    string domain;
    string error;
    string idempotency_key;
    mapping payload;

    domain = query_domain(txn);
    payload = query_payload(txn);
    idempotency_key = mapp(txn) && stringp(txn["idempotency_key"])
        ? txn["idempotency_key"] : "";
    if (domain == "")
        return schema_reject("applied", "domain is required");
    if (!mapp(payload))
        return schema_reject("applied", "domain payload is required");
    if (is_boundary_payload(payload))
        return boundary_result("applied");
    if (idempotency_key == "")
        return schema_reject("applied", "idempotency_key is required");
    /* The owner proposal key is the handler's replay fence. Domain services
     * also keep their own receipts, but the generic handler must not invoke a
     * non-idempotent producer twice while the commit response is retried. */
    if (mapp(_applied_mutations) && mapp(_applied_mutations[idempotency_key])
        && !_applied_mutations[idempotency_key]["pending_recovery"])
        return ([
            "ok": 1,
            "applied": 0,
            "duplicate": 1,
            "real_apply": 1,
            "domain": domain,
            "direct_cross_owner_write": 0,
        ]);
    error = validate_domain_schema(domain, payload);
    if (error != "")
        return schema_reject("applied", error);
    if (domain == "move")
        return apply_move_mutation(txn, payload["move"], idempotency_key);
    if (domain == "item")
        return apply_item_mutation(txn, payload["item"], idempotency_key);
    if (domain == "team")
        return apply_team_mutation(txn, payload["team"], idempotency_key);
    if (domain == "mail")
        return apply_mail_mutation(txn, payload["mail"], idempotency_key);
    if (domain == "persistence")
        return apply_persistence_mutation(txn, payload["persistence"], idempotency_key);
    if (domain == "session")
        return apply_session_mutation(txn, payload["session"], idempotency_key);
    if (domain == "trade")
        return apply_trade_mutation(txn, payload["trade"], idempotency_key);
    if (domain == "economy")
        return apply_money_mutation(txn, payload["money"], idempotency_key);
    if (domain == "world")
        return apply_world_mutation(txn, payload["world_state"],
            idempotency_key);
    if (is_player_state_delegate_domain(domain))
        return apply_player_state_mutation(txn, payload["player_state"],
            idempotency_key);
    return schema_reject("applied", domain + " has no real mutation schema");
}

private mapping rollback_move_applied(mapping applied)
{
    mapping moved;

    if (stringp(applied["move_token"]) && applied["move_token"] != "") {
        moved = WORLD_TRAVEL_D->rollback_move_proposal(applied["move_token"]);
        if (!mapp(moved) || moved["ok"] != 1)
            return ([ "ok": 0, "rolled_back": 0,
                "reason": "move proposal rollback failed",
                "handler_result": moved, "compensation_required": 1,
                "direct_cross_owner_write": 0 ]);
        return ([ "ok": 1, "rolled_back": 1, "real_apply": 0,
            "compensated": 1, "direct_cross_owner_write": 0 ]);
    }
    if (!stringp(applied["from_path"]) || applied["from_path"] == "")
        return ([ "ok": 1, "rolled_back": 1, "skipped": 1,
            "direct_cross_owner_write": 0 ]);
    if (!objectp(resolve_actor_by_ref(applied["actor_ref"])))
        return ([ "ok": 0, "rolled_back": 0,
            "reason": "move actor is not online for rollback",
            "compensation_required": 1, "direct_cross_owner_write": 0 ]);
    moved = WORLD_TRAVEL_D->move_actor(
        resolve_actor_by_ref(applied["actor_ref"]), applied["from_path"], ([
            "reason": "owner_domain_move_rollback",
        ]));
    if (!mapp(moved) || moved["ok"] != 1)
        return ([ "ok": 0, "rolled_back": 0,
            "reason": "position rollback failed",
            "handler_result": moved, "compensation_required": 1,
            "direct_cross_owner_write": 0 ]);
    return ([ "ok": 1, "rolled_back": 1, "real_apply": 0,
        "compensated": 1, "direct_cross_owner_write": 0 ]);
}

private mapping rollback_session_applied(mapping applied)
{
    return ([ "ok": 0, "rolled_back": 0,
        "reason": "gateway session close is irreversible",
        "recovery_required": 1, "compensation_required": 1,
        "rollback_supported": 0, "direct_cross_owner_write": 0 ]);
}

private mapping rollback_persistence_applied(mapping applied)
{
    return ([ "ok": 0, "rolled_back": 0,
        "reason": "durable save is irreversible",
        "recovery_required": 1, "compensation_required": 1,
        "rollback_supported": 0, "direct_cross_owner_write": 0 ]);
}

private mapping rollback_trade_applied(mapping applied)
{
    mapping session;
    mapping result;
    object actor;

    actor = query_online_player_by_id(applied["actor_player_id"]);
    if (!objectp(actor))
        return ([ "ok": 0, "rolled_back": 0,
            "reason": "trade actor is not online for cancel compensation",
            "compensation_required": 1, "direct_cross_owner_write": 0 ]);
    session = TRADE_D->query_trade_session(actor);
    if (!mapp(session))
        return ([ "ok": 1, "rolled_back": 1, "real_apply": 0,
            "compensated": 1, "already_absent": 1,
            "domain": "trade", "direct_cross_owner_write": 0 ]);
    if (session["id"] != applied["session_id"])
        return ([ "ok": 0, "rolled_back": 0,
            "reason": "trade session changed before compensation",
            "compensation_required": 1, "direct_cross_owner_write": 0 ]);
    result = TRADE_D->cancel_trade(actor);
    if (!mapp(result) || result["success"] != 1)
        return ([ "ok": 0, "rolled_back": 0,
            "reason": "trade cancel compensation failed",
            "compensation_required": 1, "direct_cross_owner_write": 0 ]);
    return ([ "ok": 1, "rolled_back": 1, "real_apply": 0,
        "compensated": 1, "domain": "trade",
        "direct_cross_owner_write": 0 ]);
}

private mapping rollback_team_applied(mapping applied)
{
    if (!mapp(applied["before"])
        || !TEAM_D->restore_owner_proposal_snapshot(applied["before"]))
        return ([ "ok": 0, "rolled_back": 0,
            "reason": "team owner snapshot rollback failed",
            "compensation_required": 1, "direct_cross_owner_write": 0 ]);
    return ([ "ok": 1, "rolled_back": 1, "real_apply": 0,
        "compensated": 1, "domain": "team",
        "direct_cross_owner_write": 0 ]);
}

private mapping rollback_mail_applied(mapping applied)
{
    if (!mapp(applied["before"])
        || !MAIL_D->restore_owner_proposal_snapshot(applied["before"],
            applied["after_fingerprint"]))
        return ([ "ok": 0, "rolled_back": 0,
            "reason": "mail owner snapshot rollback failed or stale",
            "compensation_required": 1, "direct_cross_owner_write": 0 ]);
    return ([ "ok": 1, "rolled_back": 1, "real_apply": 0,
        "compensated": 1, "domain": "mail",
        "direct_cross_owner_write": 0 ]);
}

private mapping rollback_item_applied(mapping applied)
{
    if (!mapp(applied["transfer"]))
        return ([ "ok": 0, "rolled_back": 0,
            "reason": "item transfer record is missing",
            "compensation_required": 1, "direct_cross_owner_write": 0 ]);
    if (!ITEM_INVENTORY_D->rollback_player_item_transfer(applied["transfer"]))
        return ([ "ok": 0, "rolled_back": 0,
            "reason": "item transfer rollback failed",
            "compensation_required": 1, "direct_cross_owner_write": 0 ]);
    return ([ "ok": 1, "rolled_back": 1, "real_apply": 0,
        "compensated": 1, "item_instance_id": applied["item_instance_id"],
        "direct_cross_owner_write": 0 ]);
}

private mapping rollback_money_applied(mapping applied, string idempotency_key)
{
    mapping receipt;
    mapping compensation;
    object player;
    string compensation_operation_id;
    string money_domain;
    string money_action;
    int queued;

    if (!applied["changed"])
        return ([ "ok": 1, "rolled_back": 1, "skipped": 1,
            "direct_cross_owner_write": 0 ]);
    player = query_online_player_by_id(applied["player_id"]);
    if (!objectp(player))
        return ([ "ok": 0, "rolled_back": 0,
            "reason": "economy owner is not online for rollback",
            "compensation_required": 1, "direct_cross_owner_write": 0 ]);
    /* 反向账本：原幂等键换为回滚键，MONEY_D 账本保证不重复。 */
    compensation_operation_id = "txn_rollback/" + idempotency_key;
    money_domain = stringp(applied["money_domain"])
        ? applied["money_domain"] : "economy";
    money_action = stringp(applied["money_action"])
        ? applied["money_action"] : "owner_mutation";
    compensation = ([
        "operation_id": compensation_operation_id,
        "direction": applied["direction"] == "add" ? "deduct" : "add",
        "amount": applied["amount"],
        "domain": money_domain + "_rollback",
        "action": money_action + "_rollback",
    ]);
    receipt = MONEY_D->apply_money_operation(player, compensation);
    if (!mapp(receipt) || receipt["ok"] != 1) {
        queued = MONEY_D->queue_money_compensation(player, compensation);
        return ([ "ok": 0, "rolled_back": 0,
            "reason": "compensation money operation failed",
            "handler_result": receipt, "compensation_required": 1,
            "recovery_required": 1, "compensation_queued": queued,
            "operation_id": compensation_operation_id,
            "direct_cross_owner_write": 0 ]);
    }
    return ([ "ok": 1, "rolled_back": 1, "real_apply": 0,
        "compensated": 1, "direct_cross_owner_write": 0 ]);
}

private mapping rollback_world_applied(mapping applied)
{
    mapping response;
    string operation_id;

    if (!stringp(applied["scope_key"]) || applied["scope_key"] == ""
        || !intp(applied["after_generation"]))
        return ([ "ok": 0, "rolled_back": 0,
            "reason": "world rollback metadata is missing",
            "compensation_required": 1, "direct_cross_owner_write": 0 ]);
    operation_id = stringp(applied["operation_id"])
        ? applied["operation_id"] : "owner_world_rollback";
    response = WORLD_STATE_D->restore_scope_snapshot(applied["scope_key"],
        applied["after_generation"], applied["before_bucket"],
        operation_id + "/rollback");
    if (!mapp(response) || response["ok"] != 1)
        return ([ "ok": 0, "rolled_back": 0,
            "reason": "atomic world snapshot rollback failed",
            "handler_result": response, "compensation_required": 1,
            "direct_cross_owner_write": 0 ]);
    return ([ "ok": 1, "rolled_back": 1, "real_apply": 0,
        "compensated": 1, "direct_cross_owner_write": 0 ]);
}

private mapping rollback_player_state_applied(mapping applied)
{
    mapping changes;
    string *deletes;
    mapping response;
    object player;
    string key;
    string player_id;
    int actual_generation;

    player_id = applied["player_id"];
    if (!applied["changed"])
        return ([ "ok": 1, "rolled_back": 1, "skipped": 1,
            "direct_cross_owner_write": 0 ]);
    player = query_online_player_by_id(player_id);
    if (!objectp(player))
        return ([ "ok": 0, "rolled_back": 0,
            "reason": "player state owner is not online for rollback",
            "compensation_required": 1, "direct_cross_owner_write": 0 ]);
    actual_generation = PLAYER_STATE_D->query_state_value(player,
        "persistence/player_state_generation");
    if (intp(applied["after_generation"]) && intp(actual_generation)
        && actual_generation != applied["after_generation"])
        return ([ "ok": 0, "rolled_back": 0,
            "reason": "player state changed before rollback",
            "error_code": "PLAYER_STATE_ROLLBACK_STALE_GENERATION",
            "expected_generation": applied["after_generation"],
            "actual_generation": actual_generation,
            "compensation_required": 1, "direct_cross_owner_write": 0 ]);
    changes = ([ ]);
    deletes = ({ });
    foreach (key in applied["changes_keys"] || ({ })) {
        if (mapp(applied["before_exists"])
            && applied["before_exists"][key])
            changes[key] = applied["before"][key];
        else
            deletes += ({ key });
    }
    foreach (key in applied["deleted_paths"] || ({ })) {
        if (mapp(applied["deleted_exists"])
            && applied["deleted_exists"][key])
            changes[key] = applied["deleted_before"][key];
        /* A delete of an absent path is already restored by doing nothing. */
    }
    if (sizeof(changes) > 0 || sizeof(deletes) > 0) {
        response = PLAYER_STATE_D->commit_state_mutations(player, changes,
            deletes,
            "domain_handler_rollback/" + player_id + "/"
            + (stringp(applied["operation_id"])
                ? applied["operation_id"]
                : sprintf("%d", applied["recorded_at"])));
        if (!mapp(response) || response["ok"] != 1)
            return ([ "ok": 0, "rolled_back": 0,
                "reason": "player state rollback was rejected",
                "handler_result": response, "compensation_required": 1,
                "direct_cross_owner_write": 0 ]);
    }
    return ([ "ok": 1, "rolled_back": 1, "real_apply": 0,
        "compensated": 1, "direct_cross_owner_write": 0 ]);
}

private mapping finish_rollback(string idempotency_key, mapping result)
{
    if (mapp(result) && result["ok"] == 1)
        forget_applied(idempotency_key);
    return result;
}

mapping rollback_domain_proposal(mapping txn)
{
    string domain;
    string idempotency_key;
    mapping applied;
    mapping payload;

    domain = query_domain(txn);
    payload = query_payload(txn);
    idempotency_key = mapp(txn) && stringp(txn["idempotency_key"])
        ? txn["idempotency_key"] : "";
    if (domain == "")
        return schema_reject("rolled_back", "domain is required");
    if (is_boundary_payload(payload))
        return boundary_result("rolled_back");
    if (idempotency_key == "")
        return schema_reject("rolled_back", "idempotency_key is required");
    applied = mapp(_applied_mutations) && idempotency_key != ""
        && mapp(_applied_mutations[idempotency_key])
        ? _applied_mutations[idempotency_key] : 0;
    if (!mapp(applied)) {
        /* L-11：apply 历史是有界的，缺失记录不再等于“没应用过”。
         * 只有**从未淘汰过任何记录**时才能断定无事可回滚；否则状态未知，
         * 必须报 recovery_required，不能返回 rolled_back 伪装成功。 */
        if (intp(_applied_evictions) && _applied_evictions > 0)
            return ([
                "ok": 0,
                "rolled_back": 0,
                "skipped": 0,
                "real_apply": 0,
                "recovery_required": 1,
                "rollback_supported": 0,
                "direct_cross_owner_write": 0,
                "state": "rollback_record_unknown",
                "error": "applied history was trimmed; cannot prove the mutation was not applied",
            ]);
        /* 没有真实 apply 过（prepare 失败 / 未知事务）：无事可回滚。 */
        return ([
            "ok": 1,
            "rolled_back": 1,
            "skipped": 1,
            "real_apply": 0,
            "direct_cross_owner_write": 0,
        ]);
    }
    if (applied["domain"] == "move") {
        domain = "move";
        return finish_rollback(idempotency_key,
            rollback_move_applied(applied));
    }
    if (applied["domain"] == "item") {
        return finish_rollback(idempotency_key,
            rollback_item_applied(applied));
    }
    if (applied["domain"] == "team") {
        return finish_rollback(idempotency_key,
            rollback_team_applied(applied));
    }
    if (applied["domain"] == "mail") {
        return finish_rollback(idempotency_key,
            rollback_mail_applied(applied));
    }
    if (applied["domain"] == "persistence") {
        return finish_rollback(idempotency_key,
            rollback_persistence_applied(applied));
    }
    if (applied["domain"] == "session") {
        return finish_rollback(idempotency_key,
            rollback_session_applied(applied));
    }
    if (applied["domain"] == "trade") {
        return finish_rollback(idempotency_key,
            rollback_trade_applied(applied));
    }
    if (applied["domain"] == "economy") {
        return finish_rollback(idempotency_key,
            rollback_money_applied(applied, idempotency_key));
    }
    if (applied["domain"] == "world") {
        return finish_rollback(idempotency_key,
            rollback_world_applied(applied));
    }
    if (applied["domain"] == "player_state") {
        return finish_rollback(idempotency_key,
            rollback_player_state_applied(applied));
    }
    return ([ "ok": 0, "rolled_back": 0,
        "reason": domain + " has no real mutation schema",
        "compensation_required": 1, "direct_cross_owner_write": 0 ]);
}
