/**
 * @file    owner_gameplay_d.c
 * @brief   全游戏 owner runtime 迁移协调器
 *
 * 该服务只生成 owner intent、事务、FIFO 与持久化 replay 合同，不复制各
 * gameplay daemon 的真状态。正式玩法写入仍回到 item/economy/mail/combat
 * 等既有真源，并通过 owner_commit/message/session/persistence 边界验收。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

#define MAX_OWNER_INTENT_HISTORY 8192

nosave int _next_intent_id;
nosave int _ownercheck_sequence;
nosave int _intent_history_evictions;
nosave mapping _intents;
nosave mapping _command_domains;
nosave mapping _admin_command_domains;
nosave mapping _domain_contracts;

mapping query_status();
private mapping query_admin_command_domains();

private string *query_readonly_commands()
{
    return ({
        "look", "look_room", "look_living", "look_hints", "inventory", "bag",
        "i", "inv", "skills", "status", "score", "hp", "who", "map",
        "quests", "list", "help", "time", "diag", "finger", "contentcheck",
        "servicecheck"
    });
}

private string *query_write_domains()
{
    return ({
        "move", "player", "room", "session", "item", "economy", "combat",
        "mail", "reward", "world", "persistence", "team", "guild", "sect",
        "quest", "rank", "crafting", "life_skill", "idle", "thievery"
    });
}

private string query_registered_task_method(string domain)
{
    mapping registry;
    string method;

    switch (domain) {
        case "move":
            method = "owner_task_player";
            break;
        default:
            method = "owner_task_" + domain;
            break;
    }

    registry = OWNER_RUNTIME_D->query_lpc_task_registry_contract();
    if (!mapp(registry) || member_array(method, registry["allowed_methods"]) == -1)
        return "";
    return method;
}

private mapping build_domain_contract(string domain, string mode, string txn_domain, string *commands)
{
    return ([
        "ok": 1,
        "domain": domain,
        "execution_mode": mode,
        "transaction_domain": txn_domain,
        "commands": arrayp(commands) ? commands : ({}),
        "single_writer": 1,
        "same_owner_fifo": 1,
        "requires_message_bus": domain == "readonly" ? 0 : 1,
        "requires_commit_boundary": domain == "readonly" ? 0 : 1,
        "requires_session_fifo": 1,
        "requires_persistence": member_array(domain, ({ "readonly", "world" })) == -1,
        "prepare_required": domain == "readonly" ? 0 : 1,
        "rollback_supported": domain == "readonly" ? 0 : 1,
        "idempotent": 1,
        "business_crash_replay": 0,
        "direct_cross_owner_write": 0,
        "ordinary_lpc_default_closed": 1,
        "registered_task_only": 1,
    ]);
}

private void initialize_contracts()
{
    _domain_contracts = ([
        "readonly": build_domain_contract("readonly", "registered_readonly_task", "readonly", query_readonly_commands()),
        "move": build_domain_contract("move", "owner_message_transaction", "move", ({ "go", "route" })),
        "player": build_domain_contract("player", "player_owner_serial_task", "player", ({ "alias", "activate", "learn", "practice", "study", "breakthrough", "growth", "update", "testcharacter" })),
        "room": build_domain_contract("room", "room_owner_message", "room", ({ "open", "housing" })),
        "session": build_domain_contract("session", "session_owner_fifo", "session", ({ "say", "shout", "tell", "whisper", "emote" })),
        "item": build_domain_contract("item", "player_room_transaction", "item", ({ "get", "drop", "give", "equip", "unequip", "eat", "drink" })),
        "economy": build_domain_contract("economy", "global_service_transaction", "economy", ({ "buy", "sell", "bank", "trade", "auction" })),
        "mail": build_domain_contract("mail", "mail_claim_transaction", "mail", ({ "mail" })),
        "reward": build_domain_contract("reward", "reward_apply_transaction", "reward", ({ "dungeon", "reputation" })),
        "combat": build_domain_contract("combat", "combat_owner_transaction", "combat", ({ "fight", "attack", "kill", "flee", "perform", "exert", "debate", "buff", "heal" })),
        "world": build_domain_contract("world", "room_region_owner_tick", "world", ({ "follow" })),
        "persistence": build_domain_contract("persistence", "snapshot_wal_replay", "persistence", ({ "save", "quit" })),
        "team": build_domain_contract("team", "team_owner_transaction", "team", ({ "team" })),
        "guild": build_domain_contract("guild", "guild_owner_transaction", "guild", ({ "guild" })),
        "sect": build_domain_contract("sect", "sect_owner_transaction", "sect", ({ "sect", "claimweapon", "surrender" })),
        "quest": build_domain_contract("quest", "quest_owner_transaction", "quest", ({ "ask", "talk", "quest", "questcreate" })),
        "rank": build_domain_contract("rank", "rank_owner_transaction", "rank", ({ "rank" })),
        "crafting": build_domain_contract("crafting", "crafting_owner_transaction", "crafting", ({ "alchemy", "forge", "tailor", "repair" })),
        "life_skill": build_domain_contract("life_skill", "life_skill_owner_transaction", "life_skill", ({ "gather", "fish", "meridian" })),
        "idle": build_domain_contract("idle", "idle_owner_transaction", "idle", ({ "dazuo", "idle", "rest" })),
        "thievery": build_domain_contract("thievery", "thievery_owner_transaction", "thievery", ({ "steal", "jail", "listen" })),
    ]);
}

private void initialize_command_domains()
{
    string domain;
    string command;

    _command_domains = ([]);
    foreach (domain in keys(_domain_contracts)) {
        foreach (command in _domain_contracts[domain]["commands"])
            _command_domains[command] = domain;
    }
}

void create()
{
    ::create();
    _next_intent_id = 1;
    _ownercheck_sequence = 0;
    _intent_history_evictions = 0;
    _intents = ([]);
    initialize_contracts();
    initialize_command_domains();
    _admin_command_domains = query_admin_command_domains();
}

private void prune_intent_history(int latest_intent_id)
{
    int stale_id;

    stale_id = latest_intent_id - MAX_OWNER_INTENT_HISTORY;
    if (stale_id <= 0)
        return;
    if (undefinedp(_intents[stale_id]))
        return;
    map_delete(_intents, stale_id);
    _intent_history_evictions++;
}

private string query_actor_id(object actor)
{
    string value;

    value = objectp(actor) ? get_object_id(actor) : "";
    if (stringp(value) && value != "")
        return value;
    value = objectp(actor) ? geteuid(actor) : 0;
    return stringp(value) && value != "" ? value : (objectp(actor) ? file_name(actor) : "unknown");
}

private string query_actor_session_id(object actor)
{
    string session_id;

    session_id = objectp(actor) ? GATEWAY_SESSION_D->query_object_session_id(actor) : "";
    if (stringp(session_id) && session_id != "")
        return session_id;
    return "owner/session/offline/" + query_actor_id(actor);
}

private string query_actor_player_owner(object actor)
{
    return "owner/player/" + query_actor_id(actor);
}

private string query_actor_room_owner(object actor)
{
    object env;

    env = objectp(actor) ? environment(actor) : 0;
    return objectp(env) ? "owner/room/" + file_name(env) : "owner/room/unknown";
}

private string query_actor_session_owner(object actor)
{
    return "owner/session/" + query_actor_session_id(actor);
}

private string query_actor_region_owner(object actor)
{
    object env;
    mapping route;
    string scope;

    env = objectp(actor) ? environment(actor) : 0;
    route = objectp(env) ? OWNER_ROUTE_D->query_owner_route_for_object(env) : 0;
    scope = mapp(route) && route["ok"] ? route["owner_scope_id"] : "";
    if (stringp(scope) && strsrch(scope, "shared:") == 0
        && strlen(scope) > strlen("shared:"))
        return "owner/region/" + scope[7..];
    return "owner/region/unresolved";
}

private string *build_owner_set(object actor, string domain)
{
    string player_owner;

    player_owner = query_actor_player_owner(actor);

    switch (domain) {
        case "readonly":
            return ({ player_owner, query_actor_session_owner(actor) });
        case "move":
            return ({ player_owner, query_actor_room_owner(actor), "owner/room/pending-target",
                query_actor_session_owner(actor) });
        case "player":
            return ({ player_owner });
        case "room":
            return ({ query_actor_room_owner(actor), player_owner });
        case "session":
            return ({ query_actor_session_owner(actor), player_owner });
        case "item":
            return ({ player_owner, query_actor_room_owner(actor) });
        case "economy":
            return ({ player_owner, "owner/global/service/economy_settlement" });
        case "mail":
            return ({ player_owner, "owner/global/service/mail" });
        case "reward":
            return ({ player_owner, "owner/global/service/reward_apply" });
        case "combat":
            return ({ player_owner, "owner/combat/pending", query_actor_room_owner(actor) });
        case "world":
            return ({ query_actor_room_owner(actor), query_actor_region_owner(actor) });
        case "persistence":
            return ({ player_owner, "owner/worker/persistence" });
        case "team":
            return ({ player_owner, "owner/global/service/team" });
        case "guild":
            return ({ player_owner, "owner/global/service/guild" });
        case "sect":
            return ({ player_owner, "owner/global/service/sect" });
        case "quest":
            return ({ player_owner, query_actor_room_owner(actor), "owner/global/service/quest" });
        case "rank":
            return ({ player_owner, "owner/global/service/rank" });
        case "crafting":
            return ({ player_owner, query_actor_room_owner(actor), "owner/global/service/crafting" });
        case "life_skill":
            return ({ player_owner, query_actor_room_owner(actor), "owner/global/service/life_skill" });
    }
    return ({ player_owner });
}

string query_command_domain(string verb)
{
    if (!stringp(verb) || verb == "")
        return "";
    verb = lower_case(verb);
    return stringp(_command_domains[verb]) ? _command_domains[verb] : "";
}

/* admin 命令 owner 合同单一声明表：命令名 → "readonly"、现有写 domain 或
 * ""（显式无合同，query_dispatch_domain 返回 "" fail-closed）。子命令敏感的
 * 只读诊断用 mapping 声明 domain + 只读子命令，未列出的子命令一律 fail-closed。
 * 新增 admin 命令必须先在此登记，不允许再维护第二份分类列表。 */
private mapping query_admin_command_domains()
{
    return ([
        /* 只读诊断：只查询状态，不需要 owner 提交边界 */
        "healthcheck": "readonly",
        "gathercheck": "readonly",
        "workercheck": "readonly",
        "projectioncheck": "readonly",
        "ownercheck": "player",
        "servicecheck": "readonly",
        "sessioncheck": "readonly",
        "playerreadcheck": "readonly",
        "charinfo": "readonly",
        "iteminfo": "readonly",
        "itemlist": "readonly",
        "npcinfo": "readonly",
        "npclist": "readonly",
        "listrooms": "readonly",
        "mapinfo": "readonly",
        "maproom": "readonly",
        "playerbag": "readonly",
        "questinfo": "readonly",
        "questlist": "readonly",
        "questprogress": "readonly",
        "qguidecheck": "readonly",
        "dataaudit": "readonly",
        "eventlist": "readonly",
        "exitmarkcheck": "readonly",
        /* 子命令敏感的只读诊断 */
        "worldcheck": ([
            "domain": "readonly",
            "readonly_args": ({ "", "health", "defs", "instance", "projection" }),
            "readonly_arg_prefixes": ({ "state " }),
            "write_args": ([ "synthetic": "world" ]),
        ]),
        "skillcheck": ([
            "domain": "readonly",
            "readonly_args": ({ "health", "defs" }),
            "readonly_arg_prefixes": ({ }),
        ]),
        "dungeoncheck": ([
            "domain": "readonly",
            "readonly_args": ({ "", "health", "defs" }),
            "readonly_arg_prefixes": ({ }),
        ]),
        "mapcheck": ([
            "domain": "readonly",
            "readonly_args": ({ "", "summary", "smoke", "export-status" }),
            "readonly_arg_prefixes": ({ "room " }),
        ]),
        /* 写/维护命令：复用现有写 domain 的 owner 事务与提交边界 */
        "goto": "move",
        "saveall": "persistence",
        "setday": "world",
        "settime": "world",
        "testcharacter": "player",
        /* 显式无合同：未映射的维护/测试命令保持 fail-closed，
         * 不得绕过 owner 提交边界执行 */
        "alchemycheck": "",
        "announce": "",
        "auctioncheck": "",
        "bankcheck": "",
        "buffcheck": "",
        "chatcheck": "",
        "clearbuffs": "",
        "combatcheck": "",
        "combatend": "",
        "contentcheck": "",
        "craftingcheck": "",
        "dataclean": "",
        "destroynpc": "",
        "economycheck": "player",
        "escortcheck": "",
        "followcheck": "",
        "forgecheck": "",
        "freeze": "",
        "giveitem": "",
        "growthcheck": "",
        "guildtest": "player",
        "healcheck": "",
        "housingcheck": "",
        "killnpcs": "",
        "lifecyclecheck": "",
        "loadtestfixture": "",
        "mailcheck": "",
        "mailtest": "",
        "migration_cmd": "",
        "npcbattle": "",
        "numericcheck": "player",
        "ownercheck_probe": "",
        "playererase": "",
        "questactorcheck": "",
        "questblueprintcheck": "",
        "questcheck": "",
        "questcomplete": "",
        "rankcheck": "",
        "ranktest": "",
        "registrationcheck": "",
        "repaircheck": "",
        "reputationcheck": "",
        "sendto": "",
        "shutdown": "",
        "spawnitem": "",
        "spawnnpc": "",
        "summon": "",
        "tailorcheck": "",
        "teamcheck": "",
        "testgrant": "",
        "tradecheck": "",
        "triggerevent": "",
        "unfreeze": "",
        "worldstate": "",
    ]);
}

private int admin_readonly_args_allow(mapping entry, string args)
{
    string normalized;
    string prefix;

    if (!mapp(entry))
        return 0;
    normalized = stringp(args) ? lower_case(trim(args)) : "";
    if (arrayp(entry["readonly_args"])
        && member_array(normalized, entry["readonly_args"]) != -1)
        return 1;
    if (arrayp(entry["readonly_arg_prefixes"])) {
        foreach (prefix in entry["readonly_arg_prefixes"])
            if (stringp(prefix) && prefix != ""
                && strsrch(normalized, prefix) == 0)
                return 1;
    }
    return 0;
}

private int is_readonly_admin_command(string verb, string args)
{
    mapping entry;

    verb = stringp(verb) ? lower_case(verb) : "";
    entry = mapp(_admin_command_domains[verb])
        ? _admin_command_domains[verb] : 0;
    if (!mapp(entry) || entry["domain"] != "readonly")
        return 0;
    return admin_readonly_args_allow(entry, args);
}

private string query_admin_command_domain(string verb, string args)
{
    mixed entry;
    string domain;

    verb = stringp(verb) ? lower_case(verb) : "";
    if (verb == "" || !mapp(_admin_command_domains)
        || undefinedp(_admin_command_domains[verb]))
        return "";
    entry = _admin_command_domains[verb];
    if (stringp(entry))
        return entry;
    if (!mapp(entry))
        return "";
    domain = stringp(entry["domain"]) ? entry["domain"] : "";
    if (mapp(entry["write_args"]) && stringp(entry["write_args"][
            stringp(args) ? lower_case(trim(args)) : ""]))
        return entry["write_args"][stringp(args) ? lower_case(trim(args)) : ""];
    if (domain == "readonly" && !admin_readonly_args_allow(entry, args))
        return "";
    return domain;
}

private string query_dispatch_domain(string verb, string args)
{
    string domain;

    domain = query_command_domain(verb);
    if (domain != "")
        return domain;
    /* admin 命令分类单一声明表；未登记或显式无合同一律 fail-closed。 */
    return query_admin_command_domain(verb, args);
}

private mixed query_domain_contract_ref(string domain)
{
    if (!stringp(domain) || !mapp(_domain_contracts[domain]))
        return 0;
    return _domain_contracts[domain];
}

mapping query_domain_contract(string domain)
{
    mapping contract;

    contract = query_domain_contract_ref(domain);
    if (!mapp(contract))
        return ([ "ok": 0, "reason": "unknown owner gameplay domain" ]);
    return copy(contract);
}

mapping query_all_domain_contracts()
{
    mapping result;
    string domain;

    result = ([]);
    foreach (domain in keys(_domain_contracts))
        result[domain] = copy(_domain_contracts[domain]);
    return result;
}

mapping build_command_owner_intent(object actor, string verb, string args)
{
    mapping contract;
    string domain;
    string registered_task_method;

    domain = query_dispatch_domain(verb, args);
    if (domain == "")
        return ([ "ok": 0, "reason": "command has no owner gameplay contract" ]);
    contract = _domain_contracts[domain];
    registered_task_method = query_registered_task_method(domain);
    if (!stringp(registered_task_method) || registered_task_method == "")
        return ([ "ok": 0, "reason": "owner task is not registered" ]);
    return ([
        "ok": 1,
        "verb": lower_case(verb),
        "args": stringp(args) ? args : "",
        "domain": domain,
        "transaction_domain": contract["transaction_domain"],
        "owners": build_owner_set(actor, domain),
        "execution_mode": contract["execution_mode"],
        "requires_message_bus": contract["requires_message_bus"],
        "requires_commit_boundary": contract["requires_commit_boundary"],
        "requires_session_fifo": contract["requires_session_fifo"],
        "requires_persistence": contract["requires_persistence"],
        "registered_task_method": registered_task_method,
        "readonly_snapshot_only": domain == "readonly" ? 1 : 0,
        "prepare_required": contract["prepare_required"],
        "rollback_supported": contract["rollback_supported"],
        "idempotent": contract["idempotent"],
        "business_crash_replay": contract["business_crash_replay"],
        "direct_cross_owner_write": 0,
    ]);
}

private mapping build_compact_intent_record(mapping intent)
{
    return ([
        "id": intent["id"],
        "verb": intent["verb"],
        "domain": intent["domain"],
        "transaction_domain": intent["transaction_domain"],
        "registered_task_method": intent["registered_task_method"],
        "execution_mode": intent["execution_mode"],
        "requires_commit_boundary": intent["requires_commit_boundary"],
        "owner_count": arrayp(intent["owners"]) ? sizeof(intent["owners"]) : 0,
        "args_present": intp(intent["args_present"]) ? intent["args_present"]
            : (stringp(intent["args"]) && intent["args"] != "" ? 1 : 0),
        "recorded_at": intent["recorded_at"],
        "state": intent["state"],
        "compact_audit": 1,
        "direct_cross_owner_write": 0,
    ]);
}

mapping record_compact_dispatch_intent(object actor, string verb, string args)
{
    int intent_id;
    int requires_commit_boundary;
    mapping contract;
    mapping intent;
    string domain;
    string *owners;
    string registered_task_method;

    domain = query_dispatch_domain(verb, args);
    if (domain == "")
        return ([ "ok": 0, "reason": "command has no owner gameplay contract" ]);
    contract = query_domain_contract_ref(domain);
    if (!mapp(contract))
        return ([ "ok": 0, "reason": "command owner gameplay contract is unavailable" ]);
    registered_task_method = query_registered_task_method(domain);
    if (!stringp(registered_task_method) || registered_task_method == "")
        return ([ "ok": 0, "reason": "owner task is not registered" ]);

    requires_commit_boundary = contract["requires_commit_boundary"] ? 1 : 0;
    owners = requires_commit_boundary ? build_owner_set(actor, domain) : ({});
    intent_id = _next_intent_id++;
    intent = ([
        "id": intent_id,
        "verb": lower_case(verb),
        "args_present": stringp(args) && args != "" ? 1 : 0,
        "domain": domain,
        "transaction_domain": contract["transaction_domain"],
        "registered_task_method": registered_task_method,
        "owners": owners,
        "execution_mode": contract["execution_mode"],
        "requires_commit_boundary": requires_commit_boundary,
        "recorded_at": time(),
        "state": "observed",
        "compact_audit": 1,
        "direct_cross_owner_write": 0,
    ]);
    _intents[intent_id] = build_compact_intent_record(intent);
    prune_intent_history(intent_id);
    return ([ "ok": 1, "intent": intent ]);
}

mapping record_command_owner_intent(object actor, string verb, string args)
{
    mapping intent;
    int intent_id;

    intent = build_command_owner_intent(actor, verb, args);
    if (!mapp(intent) || intent["ok"] != 1)
        return intent;

    intent_id = _next_intent_id++;
    intent["id"] = intent_id;
    intent["recorded_at"] = time();
    intent["state"] = "observed";
    _intents[intent_id] = build_compact_intent_record(intent);
    prune_intent_history(intent_id);
    return ([ "ok": 1, "intent": intent ]);
}

mapping create_gameplay_transaction(string domain, string idempotency_key, string *owners, mapping payload)
{
    mapping contract;

    contract = query_domain_contract_ref(domain);
    if (!mapp(contract))
        return ([ "ok": 0, "reason": "unknown owner gameplay domain" ]);
    if (contract["transaction_domain"] == "readonly")
        return ([ "ok": 1, "transaction_required": 0, "domain": domain ]);
    return OWNER_COMMIT_D->create_domain_transaction(contract["transaction_domain"], idempotency_key, owners, payload);
}

mapping create_compact_gameplay_transaction(string domain, string idempotency_key,
    string *owners, mapping payload)
{
    mapping contract;

    contract = query_domain_contract_ref(domain);
    if (!mapp(contract))
        return ([ "ok": 0, "reason": "unknown owner gameplay domain" ]);
    if (contract["transaction_domain"] == "readonly")
        return ([ "ok": 1, "transaction_required": 0, "domain": domain ]);
    return OWNER_COMMIT_D->create_compact_domain_transaction(
        contract["transaction_domain"], idempotency_key, owners, payload);
}

private mapping run_compact_transaction_lifecycle_check(object actor, int count)
{
    int apply_delta;
    int commit_count;
    int first_transaction_id;
    int i;
    int last_transaction_id;
    int rollback_count;
    int started_at;
    int transaction_seed;
    mapping after;
    mapping before;
    mapping created;
    mapping duplicate;
    mapping finalized;
    mapping last_terminal;
    mapping payload;
    mapping prepared;
    string first_key;
    string idempotency_key;
    string player_id;
    string *owners;

    if (!objectp(actor))
        return ([ "ok": 0, "reason": "actor is required" ]);
    if (count <= 0 || count > 1024)
        return ([ "ok": 0, "reason": "compact lifecycle count out of range" ]);

    player_id = query_actor_id(actor);
    owners = build_owner_set(actor, "player");
    started_at = time();
    payload = ([
        "command": ([ "verb": "ownercheck", "args": "compact-lifecycle" ]),
        "intent": ([
            "domain": "player",
            "execution_mode": "owner_scheduler",
            "requires_commit_boundary": 1,
        ]),
        "route": ([
            "player_id": player_id,
            "owner_count": sizeof(owners),
            "direct_cross_owner_write": 0,
        ]),
    ]);
    before = OWNER_COMMIT_D->query_status();
    transaction_seed = before["transactions_created_total"];

    for (i = 0; i < count; i++) {
        idempotency_key = sprintf("ownercheck/compact/%s/%d/%d/%d",
            player_id, started_at, transaction_seed, i);
        created = OWNER_COMMIT_D->create_compact_domain_transaction(
            "player", idempotency_key, owners, payload);
        if (!mapp(created) || created["ok"] != 1 || !intp(created["transaction_id"]))
            return ([ "ok": 0, "reason": "compact create failed", "index": i,
                      "result": created ]);
        if (i == 0) {
            first_key = idempotency_key;
            first_transaction_id = created["transaction_id"];
        }
        last_transaction_id = created["transaction_id"];
        prepared = OWNER_COMMIT_D->prepare_transaction(last_transaction_id);
        if (!mapp(prepared) || prepared["ok"] != 1)
            return ([ "ok": 0, "reason": "compact prepare failed", "index": i,
                      "result": prepared ]);
        if (i % 2) {
            finalized = OWNER_COMMIT_D->rollback_transaction(
                last_transaction_id, "ownercheck compact rollback");
            rollback_count++;
        } else {
            finalized = OWNER_COMMIT_D->commit_transaction(last_transaction_id);
            commit_count++;
        }
        if (!mapp(finalized) || finalized["ok"] != 1)
            return ([ "ok": 0, "reason": "compact finalize failed", "index": i,
                      "result": finalized ]);
    }

    duplicate = OWNER_COMMIT_D->create_compact_domain_transaction(
        "player", first_key, owners, payload);
    last_terminal = OWNER_COMMIT_D->query_transaction(last_transaction_id);
    after = OWNER_COMMIT_D->query_status();
    apply_delta = after["compact_command_apply_records_total"] -
        before["compact_command_apply_records_total"];

    return ([
        "ok":
            after["compact_command_transactions_created_total"] -
                before["compact_command_transactions_created_total"] == count &&
            after["compact_command_prepare_records_total"] -
                before["compact_command_prepare_records_total"] == count &&
            apply_delta == commit_count &&
            after["compact_command_rollback_records_total"] -
                before["compact_command_rollback_records_total"] == rollback_count &&
            after["transactions_terminal_total"] - before["transactions_terminal_total"] == count &&
            after["transactions_active"] == before["transactions_active"] &&
            after["transaction_conservation_ok"] == 1 &&
            mapp(duplicate) && duplicate["duplicate"] == 1 &&
            duplicate["transaction_id"] == first_transaction_id &&
            mapp(last_terminal) && last_terminal["terminal_recorded"] == 1 &&
            last_terminal["payload_released"] == 1 && undefinedp(last_terminal["payload"]) &&
            after["domain_prepare_log_entries"] == before["domain_prepare_log_entries"] &&
            after["domain_apply_log_entries"] == before["domain_apply_log_entries"] &&
            after["domain_rollback_log_entries"] == before["domain_rollback_log_entries"]
                ? 1 : 0,
        "count": count,
        "committed": commit_count,
        "rolled_back": rollback_count,
        "duplicate_idempotent": mapp(duplicate) && duplicate["duplicate"] == 1 ? 1 : 0,
        "payload_released": mapp(last_terminal) && last_terminal["payload_released"] == 1 &&
            undefinedp(last_terminal["payload"]) ? 1 : 0,
        "domain_log_entries_unchanged":
            after["domain_prepare_log_entries"] == before["domain_prepare_log_entries"] &&
            after["domain_apply_log_entries"] == before["domain_apply_log_entries"] &&
            after["domain_rollback_log_entries"] == before["domain_rollback_log_entries"] ? 1 : 0,
        "transaction_conservation_ok": after["transaction_conservation_ok"],
        "active_before": before["transactions_active"],
        "active_after": after["transactions_active"],
    ]);
}

mapping run_full_contract_check(object actor)
{
    mapping compact_lifecycle;
    mapping result;
    mapping readonly_intents;
    mapping domain_results;
    mapping domain_final;
    mapping duplicate;
    mapping idempotency_conflict;
    mapping fifo_output;
    mapping snapshot;
    mapping duplicate_snapshot;
    mapping async_persist;
    mapping duplicate_async_persist;
    mapping wal_record;
    mapping wal_duplicate;
    mapping replay;
    mapping partial_snapshot;
    mapping partial_async_persist;
    mapping partial_replay_first;
    mapping partial_replay_second;
    mapping partial_snapshot_after_first;
    mapping partial_snapshot_after_second;
    mapping failed_snapshot;
    mapping failed_snapshot_state;
    mapping saved_after_failure;
    mapping persistence_status;
    mapping message;
    mapping status;
    string player_id;
    string owner_id;
    string session_id;
    string *readonly_commands;
    string *domains;
    string domain;
    string persistence_key_prefix;
    int check_sequence;
    int i;

    _ownercheck_sequence++;
    check_sequence = _ownercheck_sequence;
    player_id = query_actor_id(actor);
    owner_id = "owner/player/" + player_id + "/full";
    session_id = query_actor_session_id(actor);
    if (strsrch(session_id, "owner/session/offline/") == 0 && objectp(actor)) {
        session_id = "ownercheck/full/" + player_id;
        GATEWAY_SESSION_D->bind_session_target(session_id, actor, "player");
    }

    readonly_commands = query_readonly_commands();
    domains = query_write_domains();
    readonly_intents = ([]);
    domain_results = ([]);
    domain_final = ([]);
    compact_lifecycle = run_compact_transaction_lifecycle_check(actor, 256);

    for (i = 0; i < sizeof(readonly_commands); i++)
        readonly_intents[readonly_commands[i]] = record_command_owner_intent(actor, readonly_commands[i], "");

    message = OWNER_MESSAGE_BUS_D->submit_message(owner_id, "owner/room/pending-target", "fire_and_forget", ([
        "causation_id": "ownercheck/full/message",
        "message_type": "full-contract",
    ]), 3);

    for (i = 0; i < sizeof(domains); i++) {
        mapping created;
        mapping prepared;
        mapping finalized;
        string idempotency_key;

        domain = domains[i];
        idempotency_key = sprintf("ownercheck/full/%s/%s/%d", player_id,
            domain, check_sequence);
        created = create_gameplay_transaction(domain, idempotency_key,
            build_owner_set(actor, domain), ([ "ownercheck": "full", "domain": domain ]));
        domain_results[domain] = created;
        if (!mapp(created) || created["ok"] != 1 || !mapp(created["transaction"]))
            continue;
        prepared = OWNER_COMMIT_D->prepare_transaction(created["transaction"]["id"]);
        if (member_array(domain, ({ "item", "economy" })) >= 0)
            finalized = OWNER_COMMIT_D->rollback_transaction(created["transaction"]["id"], "ownercheck full rollback path");
        else
            finalized = OWNER_COMMIT_D->commit_transaction(created["transaction"]["id"]);
        domain_final[domain] = ([ "prepared": prepared, "finalized": finalized ]);
        if (domain == "move") {
            duplicate = create_gameplay_transaction(domain, idempotency_key,
                build_owner_set(actor, domain), ([ "ownercheck": "full", "domain": domain ]));
            idempotency_conflict = create_gameplay_transaction(domain, idempotency_key,
                build_owner_set(actor, domain), ([ "ownercheck": "changed", "domain": domain ]));
        }
    }

    fifo_output = OWNER_SESSION_FIFO_D->preview_owner_output_boundary(actor,
        owner_id, "RPRT", ([
            "stream": "owner-full",
            "intent": "ownercheck_full",
        ]));
    wal_record = OWNER_PERSISTENCE_D->persist_wal_state(
        sprintf("ownercheck/full/%s/output/%d", player_id, check_sequence), "owner_output", ([
            "owner_id": owner_id,
            "stream": "owner-full",
            "intent": "ownercheck_full",
        ]));
    wal_duplicate = mapp(wal_record) && stringp(wal_record["operation_id"])
        ? OWNER_PERSISTENCE_D->persist_wal_state(wal_record["operation_id"], "owner_output", ([
            "owner_id": owner_id,
            "stream": "owner-full",
            "intent": "ownercheck_full",
        ])) : 0;
    snapshot = OWNER_PERSISTENCE_D->create_snapshot(actor, owner_id, "ownercheck_full", ([
        "player_id": player_id,
        "domains": domains,
    ]), sprintf("ownercheck/full/%s/snapshot/%d", player_id, check_sequence));
    duplicate_snapshot = mapp(snapshot) && mapp(snapshot["snapshot"])
        ? OWNER_PERSISTENCE_D->create_snapshot(actor, owner_id, "ownercheck_full", ([
            "player_id": player_id,
            "domains": domains,
        ]), snapshot["snapshot"]["idempotency_key"]) : 0;
    async_persist = mapp(snapshot) && mapp(snapshot["snapshot"])
        ? OWNER_PERSISTENCE_D->enqueue_async_persist(snapshot["snapshot"]["id"],
            snapshot["snapshot"]["idempotency_key"] + "/persist") : 0;
    duplicate_async_persist = mapp(snapshot) && mapp(snapshot["snapshot"])
        ? OWNER_PERSISTENCE_D->enqueue_async_persist(snapshot["snapshot"]["id"],
            snapshot["snapshot"]["idempotency_key"] + "/persist") : 0;
    replay = OWNER_PERSISTENCE_D->replay_pending_logs(16);
    /* The contract below owns the next two replay slots; drain unrelated WAL
     * entries first so its partial-replay assertions do not depend on queue
     * history left by earlier owner probes. */
    OWNER_PERSISTENCE_D->replay_pending_logs(0);
    persistence_key_prefix = sprintf("ownercheck/full/%s/persistence/%d",
        player_id, check_sequence);
    partial_snapshot = OWNER_PERSISTENCE_D->create_snapshot(actor, owner_id,
        "ownercheck_partial_replay", ([ "player_id": player_id ]),
        persistence_key_prefix + "/partial");
    partial_async_persist = mapp(partial_snapshot) && mapp(partial_snapshot["snapshot"])
        ? OWNER_PERSISTENCE_D->enqueue_async_persist(partial_snapshot["snapshot"]["id"],
            persistence_key_prefix + "/partial/persist") : 0;
    partial_replay_first = OWNER_PERSISTENCE_D->replay_pending_logs(1);
    partial_snapshot_after_first = mapp(partial_snapshot) && mapp(partial_snapshot["snapshot"])
        ? OWNER_PERSISTENCE_D->query_snapshot(partial_snapshot["snapshot"]["id"]) : 0;
    partial_replay_second = OWNER_PERSISTENCE_D->replay_pending_logs(1);
    partial_snapshot_after_second = mapp(partial_snapshot) && mapp(partial_snapshot["snapshot"])
        ? OWNER_PERSISTENCE_D->query_snapshot(partial_snapshot["snapshot"]["id"]) : 0;
    failed_snapshot = OWNER_PERSISTENCE_D->create_snapshot(actor, owner_id,
        "ownercheck_failed_save", ([ "player_id": player_id ]),
        persistence_key_prefix + "/failed");
    failed_snapshot_state = mapp(failed_snapshot) && mapp(failed_snapshot["snapshot"])
        ? OWNER_PERSISTENCE_D->mark_snapshot_state(failed_snapshot["snapshot"]["id"],
            "save_failed", "ownercheck injected save failure") : 0;
    saved_after_failure = mapp(failed_snapshot) && mapp(failed_snapshot["snapshot"])
        ? OWNER_PERSISTENCE_D->mark_snapshot_state(failed_snapshot["snapshot"]["id"],
            "saved", "ownercheck retry saved") : 0;
    persistence_status = OWNER_PERSISTENCE_D->query_status();
    status = query_status();

    result = ([
        "ok": 1,
        "owner_id": owner_id,
        "session_id": session_id,
        "readonly_intents": readonly_intents,
        "domain_results": domain_results,
        "domain_final": domain_final,
        "duplicate": duplicate,
        "idempotency_conflict": idempotency_conflict,
        "compact_lifecycle": compact_lifecycle,
        "message": message,
        "fifo_output": fifo_output,
        "wal_record": wal_record,
        "wal_duplicate": wal_duplicate,
        "snapshot": snapshot,
        "duplicate_snapshot": duplicate_snapshot,
        "async_persist": async_persist,
        "duplicate_async_persist": duplicate_async_persist,
        "replay": replay,
        "partial_snapshot": partial_snapshot,
        "partial_async_persist": partial_async_persist,
        "partial_replay_first": partial_replay_first,
        "partial_replay_second": partial_replay_second,
        "partial_snapshot_after_first": partial_snapshot_after_first,
        "partial_snapshot_after_second": partial_snapshot_after_second,
        "failed_snapshot": failed_snapshot,
        "failed_snapshot_state": failed_snapshot_state,
        "saved_after_failure": saved_after_failure,
        "persistence_status": persistence_status,
        "status": status,
        "single_thread_fallback": 0,
        "normal_path_single_thread_fallback": 0,
        "explicit_rollback_fallback": 1,
        "rollback_supported": 1,
        "registered_task_only": 1,
        "direct_cross_owner_write": 0,
    ]);
    return result;
}

mapping query_status()
{
    return ([
        "mode": "owner_gameplay_owner_intent_commit_runtime",
        "domains": sizeof(_domain_contracts),
        "recorded_intents": sizeof(_intents),
        "max_intent_history": MAX_OWNER_INTENT_HISTORY,
        "intent_history_evictions": _intent_history_evictions,
        "compact_intent_history": 1,
        "readonly_commands": query_readonly_commands(),
        "write_domains": query_write_domains(),
        "full_game_domains": query_write_domains() + ({ "readonly" }),
        "registered_task_only": 1,
        "same_owner_fifo": 1,
        "message_only_cross_owner": 1,
        "commit_boundary_only": 1,
        "session_fifo_boundary": 1,
        "async_persistence": 1,
        "idempotent_log": 1,
        "durable_receipt_journal": 1,
        "business_crash_replay": 0,
        "runtime_readiness": MULTICORE_CONFIG_D->query_runtime_readiness(),
        "single_thread_fallback": 0,
        "normal_path_single_thread_fallback": 0,
        "explicit_rollback_fallback": 1,
        "facade_only_runtime_claims": 0,
        "direct_cross_owner_write": 0,
    ]);
}
