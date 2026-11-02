/**
 * @file    life_skill_d.c
 * @brief   生活技能服务真源

 * @note
 * 本文件负责生活技能定义、玩家技能等级与经验累计规则。
 * 采集点分布、配方内容、产物模板和 UI 展示不在这里维护，
 * 因此它不是资源点/配方/产物三类内容配置的替身。



 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

/* L-66：等级经验阈值的单一真源（升级循环与详情展示共用，协议层不得自造公式）。 */
#define LIFE_SKILL_LEVEL_EXP_THRESHOLD 10

nosave mapping _skill_defs;
nosave mapping _last_skill_save_phase;

private mapping save_player_phase(object player)
{
    mapping phase;
    mixed err;

    if (!objectp(player) || !function_exists("save_phases", player))
        return ([ "outcome": "not_written", "persisted": 0,
            "save_result": 0, "error": "player save phases unavailable" ]);
    err = catch(phase = player->save_phases());
    if (err || !mapp(phase))
        return ([ "outcome": "unknown", "persisted": -1,
            "save_result": 0, "error": err ? sprintf("%O", err)
                : "save phase unavailable" ]);
    return phase;
}

mapping perform_activity(object player, string skill_id, mixed target, mapping options);
private mapping query_skill_operation_ledger(object player);
private int set_skill_operation_ledger(object player, mapping ledger);

private void invalidate_skill_protocol_cache(object player)
{
    if (!objectp(player))
        return;
    player->delete_temp("xk/skill_list_cache");
    player->delete_temp("xk/skill_list_threshold_cache");
    if (function_exists("invalidate_xk_skill_projection", player))
        player->invalidate_xk_skill_projection();
}

void create()
{
    ::create();
    _skill_defs = ([ ]);
}

mapping query_skill_definition(string skill_id)
{
    return mapp(_skill_defs[skill_id]) ? copy(_skill_defs[skill_id]) : 0;
}

string *query_all_life_skills()
{
    return keys(_skill_defs || ([]));
}

int get_skill_level(object player, string skill_id)
{
    if (!objectp(player) || !stringp(skill_id))
        return 0;
    return player->query("life_skills/" + skill_id) || 0;
}

/* L-66：经验余量的 owner 读取入口（详情/训练同源，调用方不再直接拼属性路径）。 */
int get_skill_exp(object player, string skill_id)
{
    if (!objectp(player) || !stringp(skill_id) || skill_id == "")
        return 0;
    return player->query("life_skills_exp/" + skill_id) || 0;
}

int query_skill_level_exp_threshold()
{
    return LIFE_SKILL_LEVEL_EXP_THRESHOLD;
}

/* L-68：等级/经验写入必须返回可验证结果——调用方（授予/经验结算）据失败
 * 决定不报 applied 与回滚，不再由 void setter 吞掉提交失败。返回 1 表示
 * 已落盘或本无需写入，0 表示未知技能/参数非法/提交失败。 */
int set_skill_level(object player, string skill_id, int level)
{
    mapping def;
    mapping response;
    mapping change;
    string path;
    int current_level;

    def = query_skill_definition(skill_id);
    if (!objectp(player) || !mapp(def))
        return 0;
    path = "life_skills/" + skill_id;
    current_level = player->query(path) || 0;
    if (level < 0) level = 0;
    if (level > (def["max_level"] || 100)) level = def["max_level"] || 100;
    if (current_level == level)
        return 1;
    change = ([ path: level ]);
    response = PLAYER_STATE_D->commit_state_fields(player, change,
        PLAYER_STATE_D->build_state_idempotency_key(player,
            "life_skill/level", change));
    if (mapp(response) && response["ok"]) {
        invalidate_skill_protocol_cache(player);
        return 1;
    }
    return 0;
}

int set_skill_exp(object player, string skill_id, int exp)
{
    mapping def;
    mapping response;
    mapping change;
    string path;
    int current_exp;

    def = query_skill_definition(skill_id);
    if (!objectp(player) || !mapp(def))
        return 0;
    if (exp < 0)
        exp = 0;
    path = "life_skills_exp/" + skill_id;
    current_exp = player->query(path) || 0;
    if (current_exp == exp)
        return 1;
    change = ([ path: exp ]);
    response = PLAYER_STATE_D->commit_state_fields(player, change,
        PLAYER_STATE_D->build_state_idempotency_key(player,
            "life_skill/exp", change));
    if (mapp(response) && response["ok"]) {
        invalidate_skill_protocol_cache(player);
        return 1;
    }
    return 0;
}

mapping query_initial_skill_grant(string provider_id)
{
    mapping template;
    mapping grant;

    if (!stringp(provider_id) || provider_id == "")
        return 0;
    template = CONTENT_REGISTRY_D->query_npc_template(provider_id);
    if (!mapp(template)
        || (template["provider_id"] || "") != provider_id)
        return 0;
    grant = template["life_skill_grant"];
    if (!mapp(grant))
        return 0;
    grant = copy(grant);
    grant["provider_id"] = provider_id;
    return grant;
}

string build_initial_skill_operation_id(object player, string provider_id)
{
    string player_id;

    if (!objectp(player) || !stringp(provider_id) || provider_id == "")
        return "";
    player_id = PLAYER_STATE_D->query_player_id(player);
    if (!stringp(player_id) || player_id == "")
        return "";
    return sprintf("life-skill-grant:%s:%s", provider_id, player_id);
}

private mapping build_initial_skill_result(int ok, int duplicate,
    string skill_id, string skill_name, int level, string provider_id,
    string operation_id, int persisted, string error_code)
{
    return ([
        "ok": ok,
        "duplicate": duplicate,
        "skill_id": skill_id,
        "skill_name": skill_name,
        "level": level,
        "provider_id": provider_id,
        "operation_id": operation_id,
        "persisted": persisted,
        "error_code": error_code,
        "error": error_code,
    ]);
}

mapping grant_initial_skill(object player, string skill_id,
    string provider_id, string operation_id)
{
    mapping def;
    mapping grant;
    mapping ledger;
    mapping prior_ledger;
    mapping record;
    string skill_name;
    int current_level;
    int grant_level;
    int max_level;
    int previous_level;
    mapping save_phase;

    if (!objectp(player))
        return build_initial_skill_result(0, 0, skill_id || "", "", 0,
            provider_id || "", operation_id || "", 0, "invalid_player");
    if (!stringp(skill_id) || skill_id == "")
        return build_initial_skill_result(0, 0, "", "", 0,
            provider_id || "", operation_id || "", 0, "invalid_skill");
    if (!stringp(provider_id) || provider_id == "")
        return build_initial_skill_result(0, 0, skill_id, "", 0, "",
            operation_id || "", 0, "invalid_provider");
    if (!stringp(operation_id) || operation_id == "")
        return build_initial_skill_result(0, 0, skill_id, "", 0,
            provider_id, "", 0, "invalid_operation_id");

    def = query_skill_definition(skill_id);
    if (!mapp(def))
        return build_initial_skill_result(0, 0, skill_id, "", 0,
            provider_id, operation_id, 0, "unknown_skill");
    skill_name = def["name"] || skill_id;
    grant = query_initial_skill_grant(provider_id);
    if (!mapp(grant))
        return build_initial_skill_result(0, 0, skill_id, skill_name, 0,
            provider_id, operation_id, 0, "invalid_provider");
    if ((grant["skill_id"] || "") != skill_id)
        return build_initial_skill_result(0, 0, skill_id, skill_name, 0,
            provider_id, operation_id, 0, "provider_skill_mismatch");
    if ((grant["policy"] || "") != "free_trial")
        return build_initial_skill_result(0, 0, skill_id, skill_name, 0,
            provider_id, operation_id, 0, "unsupported_grant_policy");

    grant_level = intp(grant["grant_level"]) ? grant["grant_level"] : 0;
    max_level = intp(grant["max_level"]) ? grant["max_level"] : 0;
    if (grant_level <= 0 || max_level < grant_level
        || grant_level > (def["max_level"] || 0))
        return build_initial_skill_result(0, 0, skill_id, skill_name, 0,
            provider_id, operation_id, 0, "invalid_grant_policy");

    ledger = query_skill_operation_ledger(player);
    prior_ledger = copy(ledger);
    record = ledger[operation_id];
    if (mapp(record)
        && ((record["skill_id"] || "") != skill_id
        || (record["provider_id"] || "") != provider_id))
        return build_initial_skill_result(0, 0, skill_id, skill_name, 0,
            provider_id, operation_id, 0, "operation_id_conflict");
    current_level = get_skill_level(player, skill_id);
    if (mapp(record) && record["status"] == "applied") {
        save_phase = save_player_phase(player);
        if (!mapp(save_phase) || save_phase["outcome"] != "committed")
            return build_initial_skill_result(0, 1, skill_id, skill_name,
                current_level, provider_id, operation_id,
                mapp(save_phase) ? save_phase["persisted"] : -1,
                mapp(save_phase) ? save_phase["outcome"] : "unknown");
        return build_initial_skill_result(1, 1, skill_id, skill_name,
            current_level, provider_id, operation_id, 1, "already_granted");
    }

    /* A save may have completed the level mutation before its caller observed
     * failure. A pending receipt plus the target level is safe to finalize. */
    if (mapp(record) && record["status"] == "pending"
        && intp(record["after_level"])
        && current_level >= record["after_level"]
        && current_level >= grant_level) {
        record["status"] = "applied";
        record["applied_at"] = time();
        ledger[operation_id] = record;
        set_skill_operation_ledger(player, ledger);
        save_phase = save_player_phase(player);
        if (mapp(save_phase) && save_phase["outcome"] == "committed")
            return build_initial_skill_result(1, 1, skill_id, skill_name,
                current_level, provider_id, operation_id, 1, "already_granted");
        if (mapp(save_phase)
            && (save_phase["outcome"] == "persisted_receipt_pending"
                || save_phase["outcome"] == "unknown"))
            return build_initial_skill_result(0, 1, skill_id, skill_name,
                current_level, provider_id, operation_id,
                save_phase["persisted"], save_phase["outcome"]);
        set_skill_operation_ledger(player, prior_ledger);
        return build_initial_skill_result(0, 0, skill_id, skill_name,
            current_level, provider_id, operation_id, 0, "skill_persist_failed");
    }

    if (current_level >= grant_level)
        return build_initial_skill_result(1, 1, skill_id, skill_name,
            current_level, provider_id, operation_id, 1, "already_granted");

    previous_level = current_level;
    record = ([
        "status": "pending",
        "operation_type": "initial_grant",
        "operation_id": operation_id,
        "skill_id": skill_id,
        "provider_id": provider_id,
        "before_level": previous_level,
        "after_level": grant_level,
        "created_at": time(),
    ]);
    ledger[operation_id] = record;
    if (!set_skill_level(player, skill_id, grant_level)) {
        /* L-68：等级写入失败不得报 applied；回执留在 pending 并记录原因。 */
        record["status"] = "pending";
        record["last_error"] = "skill_write_failed";
        record["updated_at"] = time();
        ledger[operation_id] = record;
        set_skill_operation_ledger(player, ledger);
        return build_initial_skill_result(0, 0, skill_id, skill_name,
            current_level, provider_id, operation_id, 0, "skill_write_failed");
    }
    record["status"] = "applied";
    record["applied_at"] = time();
    ledger[operation_id] = record;
    set_skill_operation_ledger(player, ledger);

    save_phase = save_player_phase(player);
    if (mapp(save_phase) && save_phase["outcome"] == "committed")
        return build_initial_skill_result(1, 0, skill_id, skill_name,
            grant_level, provider_id, operation_id, 1, "");
    if (mapp(save_phase)
        && (save_phase["outcome"] == "persisted_receipt_pending"
            || save_phase["outcome"] == "unknown"))
        return build_initial_skill_result(0, 0, skill_id, skill_name,
            grant_level, provider_id, operation_id,
            save_phase["persisted"], save_phase["outcome"]);

    set_skill_level(player, skill_id, previous_level);
    record["status"] = "pending";
    record["last_error"] = "save_failed";
    record["updated_at"] = time();
    ledger[operation_id] = record;
    set_skill_operation_ledger(player, ledger);
    return build_initial_skill_result(0, 0, skill_id, skill_name,
        previous_level, provider_id, operation_id, 0, "skill_persist_failed");
}

private string build_skill_operation_id(object player, string skill_id)
{
    string player_id;

    if (!objectp(player) || !stringp(skill_id) || skill_id == "")
        return "";
    player_id = PLAYER_STATE_D->query_player_id(player);
    if (!stringp(player_id) || player_id == "")
        player_id = base_name(player);
    return sprintf("life-skill:%s:%s:%d:%d", player_id, skill_id,
        time(), random(2147483647));
}

private mapping query_skill_operation_ledger(object player)
{
    mapping ledger;

    if (!objectp(player))
        return 0;
    ledger = player->query("life_skills/operations");
    return mapp(ledger) ? copy(ledger) : ([]);
}

private int set_skill_operation_ledger(object player, mapping ledger)
{
    mapping change;
    mapping response;

    if (!objectp(player) || !mapp(ledger))
        return 0;
    change = ([ "life_skills/operations": ledger ]);
    response = PLAYER_STATE_D->commit_state_fields(player, change,
        PLAYER_STATE_D->build_state_idempotency_key(player,
            "life_skill/operations", change));
    return mapp(response) && response["ok"];
}

private int persist_skill_operation(object player, mapping ledger)
{
    if (!objectp(player) || !mapp(ledger)
        || !function_exists("save", player)) {
        _last_skill_save_phase = ([ "outcome": "not_written",
            "persisted": 0, "save_result": 0 ]);
        return 0;
    }
    if (!set_skill_operation_ledger(player, ledger)) {
        _last_skill_save_phase = ([ "outcome": "not_written",
            "persisted": 0, "save_result": 0,
            "error": "skill ledger mutation failed" ]);
        return 0;
    }
    _last_skill_save_phase = save_player_phase(player);
    return mapp(_last_skill_save_phase)
        && _last_skill_save_phase["outcome"] == "committed";
}

mapping apply_skill_experience_operation(object player, string skill_id,
    int exp, string operation_id)
{
    mapping def;
    mapping ledger;
    mapping record;
    mapping prior_ledger;
    string effective_operation_id;
    int previous_level;
    int previous_exp;
    int current_level;
    int current_exp;
    int max_level;

    if (!objectp(player) || !stringp(skill_id) || skill_id == ""
        || exp <= 0)
        return ([ "ok": 0, "error": "invalid_skill_experience_operation" ]);
    def = query_skill_definition(skill_id);
    if (!mapp(def))
        return ([ "ok": 0, "error": "unknown_skill" ]);

    effective_operation_id = stringp(operation_id) && operation_id != ""
        ? operation_id : build_skill_operation_id(player, skill_id);
    if (effective_operation_id == "")
        return ([ "ok": 0, "error": "missing_operation_id" ]);

    ledger = query_skill_operation_ledger(player);
    prior_ledger = copy(ledger);
    record = ledger[effective_operation_id];
    current_level = get_skill_level(player, skill_id);
    current_exp = player->query("life_skills_exp/" + skill_id) || 0;

    if (mapp(record) && record["status"] == "applied") {
        /* L-68：同 ID 重试必须核对参数——改变经验值的同键请求不得被当成
         * 合法 duplicate（否则调用方以为期望数量已发放）。 */
        if ((record["skill_id"] || "") != skill_id
            || (record["exp_awarded"] || 0) != exp)
            return ([ "ok": 0, "error": "operation_id_conflict",
                "operation_id": effective_operation_id,
                "skill_id": skill_id, "exp": exp ]);
        return ([
            "ok": 1,
            "duplicate": 1,
            "operation_id": effective_operation_id,
            "skill_id": skill_id,
            "level": current_level,
            "exp": current_exp,
        ]);
    }

    /* A retry after a successful state write but before the caller observed
     * success completes the durable receipt instead of applying experience
     * twice. */
    if (mapp(record) && record["status"] == "pending"
        && intp(record["after_level"])
        && intp(record["after_exp"])
        && current_level == record["after_level"]
        && current_exp == record["after_exp"]) {
        record["status"] = "applied";
        record["applied_at"] = time();
        ledger[effective_operation_id] = record;
        if (!persist_skill_operation(player, ledger)) {
            if (mapp(_last_skill_save_phase)
                && (_last_skill_save_phase["outcome"] == "persisted_receipt_pending"
                    || _last_skill_save_phase["outcome"] == "unknown"))
                return ([ "ok": 0, "error": "skill_persist_failed",
                    "state": _last_skill_save_phase["outcome"],
                    "persisted": _last_skill_save_phase["persisted"],
                    "pending_recovery": 1,
                    "operation_id": effective_operation_id ]);
            set_skill_operation_ledger(player, prior_ledger);
            return ([ "ok": 0, "error": "skill_persist_failed",
                "pending_recovery": 1,
                "operation_id": effective_operation_id ]);
        }
        return ([ "ok": 1, "duplicate": 1,
            "operation_id": effective_operation_id,
            "skill_id": skill_id, "level": current_level,
            "exp": current_exp ]);
    }

    previous_level = current_level;
    previous_exp = current_exp;
    max_level = intp(def["max_level"]) && def["max_level"] > 0
        ? def["max_level"] : 100;
    record = ([
        "status": "pending",
        "operation_id": effective_operation_id,
        "skill_id": skill_id,
        "exp_awarded": exp,
        "before_level": previous_level,
        "before_exp": previous_exp,
        "created_at": time(),
    ]);
    ledger[effective_operation_id] = record;

    /* The pending marker is durable before any skill mutation. A later save
     * failure therefore leaves a retryable operation instead of a silent gap. */
    if (!persist_skill_operation(player, ledger)) {
        if (mapp(_last_skill_save_phase)
            && (_last_skill_save_phase["outcome"] == "persisted_receipt_pending"
                || _last_skill_save_phase["outcome"] == "unknown"))
            return ([ "ok": 0, "error": "skill_persist_failed",
                "state": _last_skill_save_phase["outcome"],
                "persisted": _last_skill_save_phase["persisted"],
                "pending_recovery": 1, "operation_id": effective_operation_id ]);
        set_skill_operation_ledger(player, prior_ledger);
        return ([ "ok": 0, "error": "skill_persist_failed",
            "pending_recovery": 1, "operation_id": effective_operation_id ]);
    }

    current_exp += exp;
    while (current_exp >= LIFE_SKILL_LEVEL_EXP_THRESHOLD && current_level < max_level) {
        current_exp -= LIFE_SKILL_LEVEL_EXP_THRESHOLD;
        current_level += 1;
    }
    if (!set_skill_level(player, skill_id, current_level)
        || !set_skill_exp(player, skill_id, current_exp)) {
        /* L-68：等级/经验写入失败不得报 applied；回滚到先前状态并留证据。 */
        if (!set_skill_level(player, skill_id, previous_level)
            || !set_skill_exp(player, skill_id, previous_exp))
            log_file("system/skill", sprintf(
                "[%s] life skill write rollback incomplete: player=%s skill=%s op=%s\n",
                ctime(time()), PLAYER_STATE_D->query_player_id(player),
                skill_id, effective_operation_id));
        record["status"] = "pending";
        record["last_error"] = "skill_write_failed";
        record["updated_at"] = time();
        ledger[effective_operation_id] = record;
        set_skill_operation_ledger(player, ledger);
        return ([ "ok": 0, "error": "skill_write_failed",
            "pending_recovery": 1, "operation_id": effective_operation_id,
            "level": previous_level, "exp": previous_exp ]);
    }
    record["after_level"] = current_level;
    record["after_exp"] = current_exp;
    record["status"] = "applied";
    record["applied_at"] = time();
    ledger[effective_operation_id] = record;

    if (!persist_skill_operation(player, ledger)) {
        if (mapp(_last_skill_save_phase)
            && (_last_skill_save_phase["outcome"] == "persisted_receipt_pending"
                || _last_skill_save_phase["outcome"] == "unknown"))
            return ([ "ok": 0, "error": "skill_persist_failed",
                "state": _last_skill_save_phase["outcome"],
                "persisted": _last_skill_save_phase["persisted"],
                "pending_recovery": 1, "operation_id": effective_operation_id,
                "level": current_level, "exp": current_exp ]);
        if (!set_skill_level(player, skill_id, previous_level)
            || !set_skill_exp(player, skill_id, previous_exp))
            log_file("system/skill", sprintf(
                "[%s] life skill persist rollback incomplete: player=%s skill=%s op=%s\n",
                ctime(time()), PLAYER_STATE_D->query_player_id(player),
                skill_id, effective_operation_id));
        record["status"] = "pending";
        record["last_error"] = "save_failed";
        ledger[effective_operation_id] = record;
        set_skill_operation_ledger(player, ledger);
        return ([ "ok": 0, "error": "skill_persist_failed",
            "pending_recovery": 1, "operation_id": effective_operation_id,
            "level": previous_level, "exp": previous_exp ]);
    }

    return ([ "ok": 1, "operation_id": effective_operation_id,
        "skill_id": skill_id, "level": current_level,
        "exp": current_exp, "levelled_up": current_level > previous_level ]);
}

varargs mapping add_skill_exp(object player, string skill_id, int exp,
    string operation_id)
{
    return apply_skill_experience_operation(player, skill_id, exp,
        operation_id);
}

private int refund_learning_money(object player, string actor_id,
    string skill_id, string operation_id, int amount)
{
    mapping result;
    string refund_operation_id;

    if (!objectp(player) || amount <= 0 || !stringp(operation_id)
        || operation_id == "")
        return 0;
    refund_operation_id = operation_id + "/refund";
    result = ECONOMY_SETTLEMENT_D->settle_money_operation(player,
        "life_skill", "learn_refund", "add", amount, refund_operation_id);
    if (mapp(result) && result["ok"] == 1)
        return 1;
    return ECONOMY_SETTLEMENT_D->queue_compensation_with_operation(player,
        "life_skill", "learn_refund", actor_id, skill_id,
        refund_operation_id, amount);
}

mapping learn_skill_with_cost(object player, string skill_id,
    int money_cost, int potential_cost)
{
    mapping receipt;
    string actor_id;
    string operation_id;
    int previous_level;
    int previous_potential;
    mapping save_phase;

    if (!objectp(player) || !stringp(skill_id) || skill_id == ""
        || money_cost <= 0 || potential_cost <= 0)
        return ([ "ok": 0, "error": "invalid_learning_request" ]);
    /* L-68：支出前先验证技能定义——未知技能不进入任何扣费/发放分支
     * （原实现会先扣钱扣潜能，再由 setter 静默失败并报“学成”）。 */
    if (!mapp(query_skill_definition(skill_id)))
        return ([ "ok": 0, "error": "unknown_skill" ]);
    if (get_skill_level(player, skill_id) > 0)
        return ([ "ok": 0, "error": "already_learned" ]);
    if (!MONEY_D->can_afford(player, money_cost))
        return ([ "ok": 0, "error": "insufficient_money" ]);
    if (!PROGRESSION_D->can_spend_potential(player, potential_cost))
        return ([ "ok": 0, "error": "insufficient_potential" ]);

    actor_id = PLAYER_STATE_D->query_player_id(player);
    /* 操作 id 纳入本次参数：同技能改变花费的请求不会命中旧 operation 的
     * duplicate（避免“改变参数得到假 duplicate”）。 */
    operation_id = ECONOMY_SETTLEMENT_D->issue_money_operation_id(
        "life_skill", "learn", actor_id,
        sprintf("%s/%d/%d", skill_id, money_cost, potential_cost));
    receipt = ECONOMY_SETTLEMENT_D->settle_money_operation(player,
        "life_skill", "learn", "deduct", money_cost, operation_id);
    if (!mapp(receipt) || !receipt["ok"])
        return ([ "ok": 0, "error": "money_settlement_failed",
            "receipt": receipt ]);

    previous_level = get_skill_level(player, skill_id);
    previous_potential = PROGRESSION_D->query_player_potential(player);
    if (!PROGRESSION_D->spend_potential(player, potential_cost)) {
        if (!refund_learning_money(player, actor_id, skill_id, operation_id,
                money_cost))
            return ([ "ok": 0, "error": "compensation_queue_failed",
                "receipt": receipt ]);
        return ([ "ok": 0, "error": "potential_settlement_failed",
            "pending_recovery": 1, "receipt": receipt ]);
    }
    if (!set_skill_level(player, skill_id, previous_level + 1)) {
        /* L-68：等级写入失败不得报“学成”；退潜能与金钱，失败留恢复证据。 */
        PROGRESSION_D->add_potential(player, potential_cost);
        if (!refund_learning_money(player, actor_id, skill_id, operation_id,
                money_cost))
            return ([ "ok": 0, "error": "compensation_queue_failed",
                "receipt": receipt, "pending_recovery": 1 ]);
        return ([ "ok": 0, "error": "skill_write_failed",
            "receipt": receipt, "potential": previous_potential ]);
    }

    save_phase = save_player_phase(player);
    if (mapp(save_phase) && save_phase["outcome"] == "committed")
        return ([ "ok": 1, "skill_id": skill_id,
            "level": previous_level + 1, "receipt": receipt ]);
    if (mapp(save_phase)
        && (save_phase["outcome"] == "persisted_receipt_pending"
            || save_phase["outcome"] == "unknown"))
        return ([ "ok": 0, "state": save_phase["outcome"],
            "persisted": save_phase["persisted"],
            "pending_recovery": 1, "skill_id": skill_id,
            "level": previous_level + 1, "receipt": receipt ]);

    PROGRESSION_D->add_potential(player, potential_cost);
    if (!set_skill_level(player, skill_id, previous_level))
        log_file("system/skill", sprintf(
            "[%s] life skill learn rollback incomplete: player=%s skill=%s op=%s\n",
            ctime(time()), actor_id, skill_id, operation_id));
    if (!refund_learning_money(player, actor_id, skill_id, operation_id,
            money_cost))
        return ([ "ok": 0, "error": "compensation_queue_failed",
            "receipt": receipt ]);
    return ([ "ok": 0, "error": "learning_persist_failed",
        "pending_recovery": 1, "receipt": receipt,
        "potential": previous_potential ]);
}

int can_use_skill(object player, string skill_id)
{
    return objectp(player) && get_skill_level(player, skill_id) > 0;
}

mapping build_synthetic_gather_target(string skill_id, mapping overrides)
{
    return 0;
}

mapping perform_gather(object player, string skill_id, mixed target, mapping options)
{
    return perform_activity(player, skill_id, target, options);
}

mapping perform_activity(object player, string skill_id, mixed target, mapping options)
{
    if (!objectp(player) || !stringp(skill_id) || skill_id == "")
        return ([ "ok": 0, "error": "invalid_skill" ]);
    return ([ "ok": 0, "error": "empty_world", "skill_id": skill_id ]);
}
