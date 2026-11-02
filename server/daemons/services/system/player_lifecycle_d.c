/* player_lifecycle_d.c —— 玩家生命周期事件 dispatcher（services/system 层）。
 *
 * kernel（login_transition）与 player.c net_dead 只发
 * 稳定系统事件（dispatch("login"/"logout", player)），不直接依赖
 * gameplay 业务 daemon；handler 装配经 manifest 唯一真源
 * （server/config/player_lifecycle_handlers.c），业务 daemon 不再
 * create 时注册——dispatcher reload 后 handler 集合不变（reload-safe）。
 *
 * 约束：
 * - 同一 event 内按 priority/id 稳定排序，小者先执行；
 * - 每个 handler 独立 catch，抛错不中断其余 handler 与主序列；
 * - changed_domains 仅允许 player_lifecycle.h 固定词汇，加载时 fail loud；
 * - id 重复在 load 时 fail loud（manifest schema 限制条目数）；
 * - handler 正常返回值不得丢弃——结构化结果（recovered/pending 等）
 *   进入 dispatch 聚合与 health。
 */
#pragma strict_types
#pragma save_types

#include <system/services.h>
#include <player_lifecycle.h>

#define LIFECYCLE_MANIFEST         "/config/player_lifecycle_handlers"
#define LIFECYCLE_MAX_HANDLERS     32

inherit "/lib/framework/daemon";

nosave mapping _manifest;   /* event -> ({ handler entries }) */
nosave mapping _health;     /* 最近 dispatch 聚合（id -> outcome/duration） */
nosave int _manifest_loaded;
nosave mapping _pending_terminations;
nosave mapping _termination_callouts;
nosave mapping _termination_terminal_failures;
nosave mapping _termination_inflight;

#define TERMINATION_RETRY_MIN_SECONDS 1
#define TERMINATION_RETRY_MAX_SECONDS 60
#define TERMINATION_MAX_RETRIES      8
#define TERMINATION_TEST_FAILURE_KEY "task15/fault_injection/termination/"

private int arm_termination_test_failure(object player, string stage)
{
    if (!objectp(player) || member_array(stage,
        ({ "logout", "save", "destruct" })) == -1)
        return 0;
    player->set_temp(TERMINATION_TEST_FAILURE_KEY + stage, 1);
    return 1;
}

private int consume_termination_test_failure(object player, string stage)
{
    if (!objectp(player) || !stringp(stage)
        || player->query_temp(TERMINATION_TEST_FAILURE_KEY + stage) != 1)
        return 0;
    player->delete_temp(TERMINATION_TEST_FAILURE_KEY + stage);
    return 1;
}

private int validate_manifest_entry(mapping entry, string *known_ids)
{
    string domain;

    if (!mapp(entry) || !stringp(entry["id"]) || entry["id"] == ""
        || !stringp(entry["event"]) || entry["event"] == ""
        || !stringp(entry["handler"]) || entry["handler"] == ""
        || !stringp(entry["function"]) || entry["function"] == "")
        return 0;
    if (member_array(entry["id"], known_ids) != -1)
        return 0;
    if (entry["event"] != "login" && entry["event"] != "logout")
        return 0;
    if (!intp(entry["priority"]))
        return 0;
    if (!arrayp(entry["changed_domains"]))
        return 0;
    foreach (domain in entry["changed_domains"]) {
        if (member_array(domain, LIFECYCLE_DOMAINS_ALL) == -1)
            return 0;
    }
    return 1;
}

/* 加载 manifest（唯一装配真源）。id 重复/非法 domain/函数缺失均
 * fail loud：不静默丢 handler。 */
private int load_manifest()
{
    mapping *entries;
    mapping entry;
    object handler_ob;
    string *ids;
    string *sorted;
    string event;
    int i;

    entries = call_other(LIFECYCLE_MANIFEST, "query_player_lifecycle_manifest");
    if (!arrayp(entries) || sizeof(entries) > LIFECYCLE_MAX_HANDLERS)
        return 0;
    _manifest = ([ "login": ({ }), "logout": ({ }) ]);
    ids = ({ });
    foreach (entry in entries) {
        if (!validate_manifest_entry(entry, ids)) {
            log_file("player_lifecycle", sprintf("%s manifest entry rejected: %O\n",
                ctime(time()), entry));
            return 0;
        }
        ids += ({ entry["id"] });
        /* handler object/function 验证（启动时即失败，不拖到 dispatch）。 */
        handler_ob = load_object(entry["handler"]);
        if (!objectp(handler_ob)
            || !function_exists(entry["function"], handler_ob)) {
            log_file("player_lifecycle", sprintf("%s manifest handler missing: %O\n",
                ctime(time()), entry));
            return 0;
        }
        _manifest[entry["event"]] += ({ entry });
    }
    /* 按 priority/id 稳定排序（小者先执行）。 */
    foreach (event in keys(_manifest)) {
        sorted = sort_array(_manifest[event],
            (: $1["priority"] == $2["priority"]
                ? $1["id"] < $2["id"] : $1["priority"] < $2["priority"] :));
        _manifest[event] = sorted;
    }
    return 1;
}

void create()
{
    ::create();
    _health = ([ ]);
    _pending_terminations = ([ ]);
    _termination_callouts = ([ ]);
    _termination_terminal_failures = ([ ]);
    _termination_inflight = ([ ]);
    _manifest_loaded = load_manifest();
    if (!_manifest_loaded) {
        /* 启动期 manifest 加载失败：fail loud，保留空集合并记录——
         * 登录恢复缺失是数据风险，宁可明确失败由运维修复。 */
        _manifest = ([ "login": ({ }), "logout": ({ }) ]);
        log_file("player_lifecycle", sprintf("%s manifest load failed\n",
            ctime(time())));
    }
}

/* 玩家 JSON save 的 owner 事务窄门：kernel/data 只依赖生命周期 facade，
 * 不直连持久化服务；实际 snapshot 与 receipt 仍由 OWNER_PERSISTENCE_D 真源处理。 */
mapping prepare_player_json_snapshot(object player, string owner_id,
    mapping payload, string operation_id)
{
    object persistence;
    mapping result;
    mixed err;

    if (!objectp(player) || !stringp(owner_id) || owner_id == ""
        || !mapp(payload) || !stringp(operation_id) || operation_id == "")
        return ([ "ok": 0, "error_code": "PLAYER_JSON_SNAPSHOT_INPUT" ]);
    persistence = find_object(OWNER_PERSISTENCE_D);
    if (!objectp(persistence))
        err = catch(persistence = load_object(OWNER_PERSISTENCE_D));
    if (err || !objectp(persistence))
        return ([ "ok": 0, "error_code": "OWNER_PERSISTENCE_UNAVAILABLE" ]);
    err = catch(result = persistence->create_snapshot(player, owner_id,
        "player_json_state", payload, operation_id));
    if (err)
        return ([ "ok": 0, "error_code": "OWNER_SNAPSHOT_REJECTED",
            "detail": sprintf("%O", err) ]);
    return mapp(result) ? result : ([
        "ok": 0, "error_code": "OWNER_SNAPSHOT_REJECTED"
    ]);
}

mapping query_pending_player_json_snapshot(string owner_id,
    string content_digest)
{
    object persistence;
    mapping snapshot;
    mixed err;

    if (!stringp(owner_id) || owner_id == "" || !stringp(content_digest)
        || content_digest == "")
        return 0;
    persistence = find_object(OWNER_PERSISTENCE_D);
    if (!objectp(persistence))
        err = catch(persistence = load_object(OWNER_PERSISTENCE_D));
    if (err || !objectp(persistence))
        return 0;
    err = catch(snapshot = persistence->query_pending_snapshot(owner_id,
        "player_json_state", content_digest));
    return err || !mapp(snapshot) ? 0 : snapshot;
}

/* Recovery lookup is path-based so an old file cannot hide a pending
 * candidate merely because the observed digest differs from the candidate. */
mapping query_pending_player_json_publication(string owner_id, string file)
{
    object persistence;
    mapping result;
    mixed err;

    if (!stringp(owner_id) || owner_id == "" || !stringp(file) || file == "")
        return ([ "ok": 0, "error_code": "PLAYER_JSON_PUBLICATION_INPUT" ]);
    persistence = find_object(OWNER_PERSISTENCE_D);
    if (!objectp(persistence))
        err = catch(persistence = load_object(OWNER_PERSISTENCE_D));
    if (err || !objectp(persistence))
        return ([ "ok": 0, "error_code": "OWNER_PERSISTENCE_UNAVAILABLE" ]);
    err = catch(result = persistence->query_pending_publication(owner_id,
        "player_json_state", file));
    return err || !mapp(result) ? ([
        "ok": 0, "error_code": "PUBLICATION_LOOKUP_FAILED"
    ]) : result;
}

mapping prepare_player_json_publication(int snapshot_id, string file,
    string content_digest)
{
    object persistence;
    mapping result;
    mixed err;

    if (!intp(snapshot_id) || snapshot_id <= 0
        || !stringp(file) || file == ""
        || !stringp(content_digest) || content_digest == "")
        return ([ "ok": 0, "error_code": "PLAYER_JSON_PUBLICATION_INPUT" ]);
    persistence = find_object(OWNER_PERSISTENCE_D);
    if (!objectp(persistence))
        err = catch(persistence = load_object(OWNER_PERSISTENCE_D));
    if (err || !objectp(persistence))
        return ([ "ok": 0, "error_code": "OWNER_PERSISTENCE_UNAVAILABLE" ]);
    err = catch(result = persistence->prepare_snapshot_publication(
        snapshot_id, file, content_digest));
    return err || !mapp(result) ? ([
        "ok": 0, "error_code": "PUBLICATION_ACCEPTANCE_FAILED"
    ]) : result;
}

mapping query_player_json_publication(int snapshot_id, string file,
    string observed_digest)
{
    object persistence;
    mapping result;
    mapping publication;
    mixed err;

    if (!intp(snapshot_id) || snapshot_id <= 0
        || !stringp(file) || file == "")
        return ([ "outcome": "unknown", "persisted": -1,
            "error_code": "PLAYER_JSON_PUBLICATION_INPUT" ]);
    persistence = find_object(OWNER_PERSISTENCE_D);
    if (!objectp(persistence))
        err = catch(persistence = load_object(OWNER_PERSISTENCE_D));
    if (err || !objectp(persistence))
        return ([ "outcome": "unknown", "persisted": -1,
            "error_code": "OWNER_PERSISTENCE_UNAVAILABLE" ]);
    err = catch(result = persistence->query_snapshot_publication(snapshot_id));
    if (err || !mapp(result))
        return ([ "outcome": "unknown", "persisted": -1,
            "error_code": "PUBLICATION_LOOKUP_FAILED" ]);
    publication = result["publication"];
    if (!mapp(publication))
        return result["outcome"] == "committed" && result["evidence_released"] == 1
            ? result
            : ([ "outcome": "unknown", "persisted": -1,
                "error_code": "PUBLICATION_PATH_MISMATCH" ]);
    if (publication["file"] != file)
        return ([ "outcome": "unknown", "persisted": -1,
            "error_code": "PUBLICATION_PATH_MISMATCH" ]);
    return result;
}

mapping recover_player_json_publication(int snapshot_id)
{
    object persistence;
    mapping result;
    mixed err;

    if (!intp(snapshot_id) || snapshot_id <= 0)
        return ([ "ok": 0, "error_code": "PLAYER_JSON_RECEIPT_INPUT" ]);
    persistence = find_object(OWNER_PERSISTENCE_D);
    if (!objectp(persistence))
        err = catch(persistence = load_object(OWNER_PERSISTENCE_D));
    if (err || !objectp(persistence))
        return ([ "ok": 0, "error_code": "OWNER_PERSISTENCE_UNAVAILABLE" ]);
    err = catch(result = persistence->recover_snapshot_publication(snapshot_id));
    return err || !mapp(result) ? ([
        "ok": 0, "error_code": "PUBLICATION_RECOVERY_FAILED"
    ]) : result;
}

mapping finalize_player_json_snapshot(int snapshot_id, string state,
    string reason)
{
    object persistence;
    mapping result;
    mixed err;

    if (!intp(snapshot_id) || snapshot_id <= 0
        || member_array(state, ({ "saved", "save_failed", "verified" })) == -1)
        return ([ "ok": 0, "error_code": "PLAYER_JSON_RECEIPT_INPUT" ]);
    persistence = find_object(OWNER_PERSISTENCE_D);
    if (!objectp(persistence))
        err = catch(persistence = load_object(OWNER_PERSISTENCE_D));
    if (err || !objectp(persistence))
        return ([ "ok": 0, "error_code": "OWNER_PERSISTENCE_UNAVAILABLE" ]);
    err = catch(result = persistence->mark_snapshot_state(snapshot_id, state,
        stringp(reason) ? reason : ""));
    if (err)
        return ([ "ok": 0, "error_code": "OWNER_RECEIPT_REJECTED",
            "detail": sprintf("%O", err) ]);
    return mapp(result) ? result : ([
        "ok": 0, "error_code": "OWNER_RECEIPT_REJECTED"
    ]);
}

/* 统一 dispatch：event=login/logout，player 为对象。返回聚合结果：
 * ([ ok, results: ([ id: ([ recovered, pending, duration_seconds, error,
 *     changed_domains, state ]) ]), health: ([ ok, errors, handlers ]) ])。
 * catch 包裹赋值以保留 handler 返回值（LPC 的 catch 自身只返回错误）。 */
mapping recover_player_after_environment_loss(object player, object recovery_room)
{
    mapping moved;

    if (!objectp(player) || !objectp(recovery_room))
        return ([ "ok": 0, "error": "recovery target unavailable" ]);
    moved = WORLD_TRAVEL_D->move_actor(player, recovery_room, ([
        "reason": "destruct_recovery",
        "quiet": 1,
    ]));
    if (!mapp(moved) || !moved["ok"])
        return mapp(moved) ? moved : ([
            "ok": 0, "error": "recovery travel failed"
        ]);
    moved["recovered"] = 1;
    return moved;
}

mapping dispatch(string event, object player)
{
    mapping *handlers;
    mapping aggregated;
    mapping entry;
    mapping health;
    mixed handler_result;
    mixed thrown;
    int started_at;
    int duration_seconds;

    aggregated = ([ "ok": 1, "results": ([ ]) ]);
    health = ([ "ok": 1, "errors": 0, "handlers": 0 ]);
    if (!_manifest_loaded) {
        aggregated["ok"] = 0;
        health["ok"] = 0;
        health["errors"] = 1;
        return aggregated + ([ "health": health,
            "error": "lifecycle manifest not loaded" ]);
    }
    if (!objectp(player))
        return aggregated + ([ "health": health ]);
    if (!mapp(_manifest) || !arrayp(_manifest[event]))
        return aggregated + ([ "health": health ]);
    handlers = _manifest[event];
    foreach (entry in handlers) {
        started_at = time();
        handler_result = 0;
        thrown = catch(handler_result = call_other(entry["handler"],
            entry["function"], player));
        duration_seconds = time() - started_at;
        if (thrown != 0) {
            health["ok"] = 0;
            health["errors"]++;
            aggregated["ok"] = 0;
            aggregated["results"][entry["id"]] = ([
                "error": sprintf("%O", thrown),
                "duration_seconds": duration_seconds,
                "changed_domains": entry["changed_domains"],
            ]);
            log_file("player_lifecycle", sprintf("%s %s %s handler %s threw: %O\n",
                ctime(time()), get_object_id(player) || "?", event,
                entry["id"], thrown));
        } else {
            aggregated["results"][entry["id"]] = ([
                "recovered": mapp(handler_result) ? handler_result["recovered"] : 0,
                "pending": mapp(handler_result) ? handler_result["pending"] : 0,
                "state": mapp(handler_result) && handler_result["state"]
                    ? handler_result["state"] : "ok",
                "duration_seconds": duration_seconds,
                "changed_domains": entry["changed_domains"],
            ]);
            /* Handler mappings with an explicit ok=0 are failed recovery,
             * even when the handler returned normally. Preserve the error in
             * the aggregate so login health cannot report a false success. */
            if (mapp(handler_result)
                && !undefinedp(handler_result["ok"])
                && !handler_result["ok"]) {
                health["ok"] = 0;
                health["errors"]++;
                aggregated["ok"] = 0;
                aggregated["results"][entry["id"]]["error"] =
                    handler_result["error"] || handler_result["reason"]
                        || "handler reported failure";
            }
        }
        health["handlers"]++;
    }
    _health = health;
    return aggregated + ([ "health": health ]);
}
private string termination_key(object player)
{
    string player_id;

    if (!objectp(player))
        return "";
    player_id = get_object_id(player);
    if (!stringp(player_id) || player_id == "")
        player_id = file_name(player);
    return stringp(player_id) && player_id != ""
        ? "player/termination/" + player_id : "";
}

private mapping terminate_once(object player, string reason,
    string farewell_message)
{
    mapping logout_result;
    mapping pending;
    mapping result;
    string key;
    mixed farewell_error;
    mixed dispatch_error;
    mixed save_error;
    mixed destruct_error;
    mapping save_phase;
    int saved;

    key = termination_key(player);
    pending = stringp(key) && key != "" ? _pending_terminations[key] : 0;
    if ((!stringp(farewell_message) || farewell_message == "")
        && mapp(pending) && stringp(pending["farewell_message"]))
        farewell_message = pending["farewell_message"];

    if (!objectp(player))
        return ([ "ok": 0, "stage": "player", "error": "player unavailable" ]);

    if (consume_termination_test_failure(player, "logout"))
        return ([
            "ok": 0,
            "stage": "logout",
            "failure_injected": 1,
            "pending_cleanup": 1,
            "error": "fault injection reached",
        ]);
    if (mapp(pending) && pending["logout_completed"]) {
        logout_result = mapp(pending["logout_result"])
            ? pending["logout_result"] : ([ "ok": 1, "recovered": 1 ]);
    } else {
        dispatch_error = catch(logout_result = dispatch("logout", player));
        if (dispatch_error || !mapp(logout_result) || logout_result["ok"] != 1)
            return ([
                "ok": 0,
                "stage": "logout",
                "error": dispatch_error ? sprintf("%O", dispatch_error)
                    : (mapp(logout_result) ? logout_result["error"]
                        : "logout dispatch failed"),
                "logout": logout_result,
            ]);
    }
    if (!mapp(logout_result) || logout_result["ok"] != 1)
        return ([
            "ok": 0,
            "stage": "logout",
            "error": dispatch_error ? sprintf("%O", dispatch_error)
                : (mapp(logout_result) ? logout_result["error"]
                    : "logout dispatch failed"),
            "logout": logout_result,
        ]);

    if (mapp(pending) && pending["save_completed"]) {
        saved = 1;
    } else {
        if (consume_termination_test_failure(player, "save"))
            return ([
                "ok": 0,
                "stage": "save",
                "failure_injected": 1,
                "pending_cleanup": 1,
                "error": "fault injection reached",
                "logout": logout_result,
                "saved": 0,
            ]);
        save_error = catch {
            if (!function_exists("save_phases", player))
                error("player save phases unavailable");
            save_phase = player->save_phases();
        };
        if (save_error || !mapp(save_phase)
            || save_phase["outcome"] != "committed")
            return ([
                "ok": 0,
                "stage": "save",
                "error": save_error ? sprintf("%O", save_error)
                    : (save_phase["error"] || "player save phase incomplete"),
                "logout": logout_result,
                "saved": 0,
                "persisted": save_error ? -1 : save_phase["persisted"],
                "save_result": save_error ? 0 : save_phase["save_result"],
                "save_state": save_error ? "unknown" : save_phase["outcome"],
            ]);
    }
    saved = 1;

    /* 故障注入在最终通知前返回，保证 destruct 未完成时不播告别语。真实
     * destruct 是同步终态；若 driver 抛错，终结仍进入 pending。 */
    if (consume_termination_test_failure(player, "destruct"))
        return ([
            "ok": 0,
            "stage": "destruct",
            "failure_injected": 1,
            "pending_cleanup": 1,
            "error": "fault injection reached",
            "logout": logout_result,
            "saved": 1,
        ]);

    if (stringp(farewell_message) && farewell_message != ""
        && (!mapp(pending) || !pending["farewell_sent"])) {
        farewell_error = catch(msg_send(player, farewell_message));
        if (farewell_error)
            log_file("system/error", sprintf("[%s] termination farewell failed for %s: %O\n",
                ctime(time()), get_object_id(player) || "?", farewell_error));
    }
    destruct_error = catch(destruct(player));
    if (destruct_error || objectp(player))
        return ([
            "ok": 0,
            "stage": "destruct",
            "error": destruct_error ? sprintf("%O", destruct_error)
                : "player remained after destruct",
            "logout": logout_result,
            "saved": 1,
        ]);

    result = ([
        "ok": 1,
        "stage": "complete",
        "reason": stringp(reason) ? reason : "",
        "logout": logout_result,
        "saved": 1,
        "destructed": 1,
        "farewell_sent": stringp(farewell_message) && farewell_message != "" ? 1 : 0,
    ]);
    return result;
}

private mapping attempt_termination(object player, string reason,
    string farewell_message)
{
    mapping result;
    string key;
    mixed thrown;

    key = termination_key(player);
    if (key != "" && _termination_inflight[key])
        return ([
            "ok": 0,
            "stage": "inflight",
            "pending_cleanup": 1,
            "error": "player termination already in progress",
        ]);
    if (key != "")
        _termination_inflight[key] = 1;
    thrown = catch(result = terminate_once(player, reason, farewell_message));
    if (key != "")
        map_delete(_termination_inflight, key);
    if (thrown)
        return ([
            "ok": 0,
            "stage": "exception",
            "pending_cleanup": 1,
            "error": sprintf("%O", thrown),
        ]);
    return result;
}

private void schedule_termination_retry(string key)
{
    mapping pending;
    int delay;
    int retry_count;

    if (!stringp(key) || key == "" || _termination_callouts[key])
        return;
    pending = _pending_terminations[key];
    if (!mapp(pending))
        return;
    retry_count = intp(pending["retry_count"]) ? pending["retry_count"] : 0;
    delay = TERMINATION_RETRY_MIN_SECONDS << (retry_count < 6 ? retry_count : 6);
    if (delay > TERMINATION_RETRY_MAX_SECONDS)
        delay = TERMINATION_RETRY_MAX_SECONDS;
    _termination_callouts[key] = call_out("retry_pending_termination", delay, key);
}

private mapping retain_failed_termination(object player, string reason,
    mapping result, string farewell_message)
{
    string key;
    string player_id;
    mapping pending;
    mapping terminal;
    int retry_count;

    key = termination_key(player);
    if (key == "")
        return 0;
    pending = _pending_terminations[key];
    if (!mapp(pending))
        pending = ([
            "player": player,
            "reason": stringp(reason) ? reason : "",
            "retry_count": 0,
            "first_failed_at": time(),
        ]);
    if (stringp(farewell_message) && farewell_message != "")
        pending["farewell_message"] = farewell_message;
    pending["last_result"] = result;
    if (mapp(result) && mapp(result["logout"])
        && result["logout"]["ok"] == 1) {
        pending["logout_completed"] = 1;
        pending["logout_result"] = result["logout"];
    }
    if (mapp(result) && result["saved"] == 1)
        pending["save_completed"] = 1;
    if (mapp(result) && result["farewell_sent"] == 1)
        pending["farewell_sent"] = 1;
    retry_count = (pending["retry_count"] || 0) + 1;
    pending["retry_count"] = retry_count;
    pending["last_attempt_at"] = time();
    if (retry_count >= TERMINATION_MAX_RETRIES) {
        player_id = get_object_id(player);
        if (!stringp(player_id) || player_id == "")
            player_id = file_name(player);
        terminal = ([
            "state": "terminal_failure",
            "reason": "termination retry limit exceeded",
            "player_id": player_id,
            "retry_count": retry_count,
            "first_failed_at": pending["first_failed_at"] || time(),
            "last_attempt_at": pending["last_attempt_at"],
            "last_result": result,
            "object_reference_released": 1,
        ]);
        _termination_terminal_failures[key] = terminal;
        map_delete(_pending_terminations, key);
        if (intp(_termination_callouts[key]) && _termination_callouts[key] > 0)
            remove_call_out(_termination_callouts[key]);
        map_delete(_termination_callouts, key);
        log_file("system/error", sprintf("[%s] player termination terminal failure %s: %O\n",
            ctime(time()), key, terminal));
        return ([
            "ok": 0,
            "stage": "terminal_failure",
            "terminal_failure": 1,
            "pending_cleanup": 0,
            "error": "termination retry limit exceeded",
            "retry_count": retry_count,
        ]);
    }
    _pending_terminations[key] = pending;
    schedule_termination_retry(key);
    return 0;
}

void retry_pending_termination(string key)
{
    mapping pending;
    mapping result;
    object player;
    string farewell_message;

    if (!stringp(key) || key == "")
        return;
    _termination_callouts[key] = 0;
    pending = _pending_terminations[key];
    if (!mapp(pending))
        return;
    player = pending["player"];
    if (!objectp(player)) {
        _termination_terminal_failures[key] = ([
            "state": "terminal_failure",
            "reason": "player object unavailable during retry",
            "recorded_at": time(),
            "object_reference_released": 1,
        ]);
        map_delete(_pending_terminations, key);
        return;
    }
    farewell_message = stringp(pending["farewell_message"])
        ? pending["farewell_message"] : "";
    result = attempt_termination(player, pending["reason"], farewell_message);
    if (mapp(result) && result["ok"]) {
        map_delete(_pending_terminations, key);
        map_delete(_termination_terminal_failures, key);
        return;
    }
    retain_failed_termination(player, pending["reason"], result, farewell_message);
}

varargs mapping terminate_player(object player, string reason,
    string farewell_message)
{
    mapping result;
    string key;

    if (!objectp(player))
        return ([ "ok": 0, "stage": "player", "error": "player unavailable" ]);
    key = termination_key(player);
    if (key != "" && mapp(_pending_terminations[key]))
        return ([
            "ok": 0,
            "stage": "pending",
            "pending": 1,
            "error": "player termination already pending",
            "last_result": _pending_terminations[key]["last_result"],
        ]);

    result = attempt_termination(player, reason, farewell_message);
    if (!mapp(result) || !result["ok"]) {
        result = retain_failed_termination(player, reason, result,
            farewell_message) || result;
    } else if (key != "") {
        map_delete(_termination_terminal_failures, key);
    }
    return result;
}

mapping query_health_snapshot()
{
    mapping *handlers;
    mapping pending;
    string *ids;
    string event;
    string id;
    string pending_key;
    int total;
    int oldest_pending_at;
    int max_retry_count;
    int retry_count;

    ids = ({ });
    foreach (event in keys(_manifest)) {
        handlers = _manifest[event];
        foreach (id in ({ }) + map_array(handlers,
            (: $1["id"] :))) {
            total++;
            ids += ({ id });
        }
    }
    oldest_pending_at = 0;
    max_retry_count = 0;
    foreach (pending_key, pending in _pending_terminations) {
        if (!mapp(pending))
            continue;
        if (oldest_pending_at == 0
            || (pending["first_failed_at"] || 0) < oldest_pending_at)
            oldest_pending_at = pending["first_failed_at"] || 0;
        retry_count = intp(pending["retry_count"])
            ? pending["retry_count"] : 0;
        if (retry_count > max_retry_count)
            max_retry_count = retry_count;
    }
    return ([
        "manifest_loaded": _manifest_loaded,
        "handler_count": total,
        "handler_ids": sort_array(ids, (: $1 < $2 :)),
        "last_dispatch": _health || ([ ]),
        "pending_cleanup": sizeof(_pending_terminations),
        "oldest_pending_at": oldest_pending_at,
        "max_retry_count": max_retry_count,
        "inflight_cleanup": sizeof(_termination_inflight),
        "terminal_cleanup_failures": sizeof(_termination_terminal_failures),
        "termination_max_retries": TERMINATION_MAX_RETRIES,
        "termination_retry_backoff": ([
            "min_seconds": TERMINATION_RETRY_MIN_SECONDS,
            "max_seconds": TERMINATION_RETRY_MAX_SECONDS,
        ]),
    ]);
}
