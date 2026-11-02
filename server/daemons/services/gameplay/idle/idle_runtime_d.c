/**
 * @file    idle_runtime_d.c
 * @brief   挂机运行时真源：离线挂机会话记录、结算与防脚本。
 *
 * 架构（离线挂机，方案 B）：
 *   玩家发起挂机 → 会话持久化到玩家档案（idle/session）→ 客户端断线照常
 *   destruct 对象 → 重连时登录流程调用 settle_offline_session() 一次性结算
 *   → 结算消息随首次快照下发。
 *
 * 扩展点：挂机类型注册表（query_idle_settler/register_idle_type）。
 * 新增类型（钓鱼/采集/……）只需注册结算器 + 配置挂机点，框架不动。
 *
 * 红线：挂机是陪衬不是主餐——单次时长上限、固定结算、
 * 挂机日收益 ≤ 主动游玩 30%。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>
#include <game/skill.h>

inherit "/lib/framework/daemon";

#define IDLE_SESSION_PATH "idle/session"
#define IDLE_SETTLEMENTS_PATH "idle/settlements"
#define IDLE_MAX_HOURS 24
#define IDLE_MAX_SECONDS (IDLE_MAX_HOURS * 3600)

nosave mapping _idle_settlers;

mapping settle_offline_session(object player);

/* ============ 挂机类型注册 ============ */

/*
 * 结算器签名：mapping(object player, int hours, mapping session)
 * 返回 ([ "ok":1, "message":..., "rewards":([...]) ])
 */
mapping register_idle_type(string idle_type, string settler_ref)
{
    object caller;
    object settler;
    mixed err;
    string caller_path;

    caller = previous_object();
    caller_path = objectp(caller) ? file_name(caller) : "";
    if (objectp(caller) && caller != this_object()
        && strsrch(caller_path, "/daemons/services/gameplay/idle/") != 0)
        return ([ "ok": 0, "error": "idle_registration_denied" ]);
    if (!stringp(idle_type) || idle_type == ""
        || !stringp(settler_ref) || settler_ref == "")
        return ([ "ok": 0, "error": "invalid_idle_registration" ]);
    err = catch(settler = load_object(settler_ref));
    if (err || !objectp(settler) || !function_exists("settle_idle", settler))
        return ([ "ok": 0, "error": "invalid_idle_settler" ]);
    _idle_settlers[idle_type] = settler_ref;
    return ([
        "ok": 1,
        "schema_version": 1,
        "idle_type": idle_type,
        "settler": settler_ref,
        "max_seconds": IDLE_MAX_SECONDS,
    ]);
}

private string query_idle_settler(string idle_type)
{
    return stringp(idle_type) ? _idle_settlers[idle_type] : "";
}

/* ============ 会话生命周期 ============ */

/* 挂机会话与重复点计数都是玩家持久状态：统一经玩家状态 owner 提交，
 * 失败返回 0，调用方不得伪报会话已建立/已取消。 */
private int commit_idle_state(object player, mapping changes, string *deletes,
    string operation)
{
    mapping mutation;
    mapping response;

    if (!objectp(player))
        return 0;
    if (!mapp(changes))
        changes = ([ ]);
    if (!arrayp(deletes))
        deletes = ({ });
    if (sizeof(changes) == 0 && sizeof(deletes) == 0)
        return 1;
    mutation = ([ "changes": changes, "deletes": deletes ]);
    response = PLAYER_STATE_D->commit_state_mutations(player, changes, deletes,
        PLAYER_STATE_D->build_state_idempotency_key(player,
            operation || "idle/state", mutation));
    return mapp(response) && response["ok"];
}

private mapping rollback_idle_effects(object player, mapping before_data,
    string operation)
{
    mapping restore;
    mapping save_phase;
    mixed err;

    if (!objectp(player) || !mapp(before_data))
        return ([ "ok": 0, "error": "idle_snapshot_unavailable" ]);
    restore = PLAYER_STATE_D->restore_state_snapshot_if_generation(player,
        before_data, PLAYER_STATE_D->query_state_value(player,
            "persistence/player_state_generation"), operation);
    if (!mapp(restore) || !restore["ok"])
        return ([ "ok": 0, "error": "idle_snapshot_restore_failed",
            "restore": restore ]);
    if (!function_exists("save_phases", player))
        save_phase = ([ "outcome": "not_written", "persisted": 0,
            "save_result": 0, "error": "player save phases unavailable" ]);
    else
        err = catch(save_phase = player->save_phases());
    if (err || !mapp(save_phase)
        || save_phase["outcome"] != "committed")
        return ([ "ok": 0, "error": "idle_snapshot_save_failed",
            "state": err ? "unknown" : save_phase["outcome"],
            "persisted": err ? -1 : save_phase["persisted"],
            "restore": restore ]);
    return ([ "ok": 1, "restore": restore ]);
}

/*
 * 发起挂机会话。校验：挂机点类型、当前无会话、每日上限。
 * 返回 ([ "ok":1, "session":([...]) ]) 或失败。
 */
mapping start_idle_session(object player, string idle_type, string room_path,
    mapping context)
{
    mapping session;
    mapping changes;
    string existing;
    string settlement_id;
    int now;

    if (!objectp(player))
        return ([ "ok": 0, "error": "invalid_player" ]);
    if (query_idle_settler(idle_type) == "")
        return ([ "ok": 0, "error": "unknown_idle_type", "idle_type": idle_type ]);

    existing = player->query(IDLE_SESSION_PATH);
    if (stringp(existing) && existing != "")
        return ([ "ok": 0, "error": "idle_already_active" ]);

    now = time();
    settlement_id = hash("sha256", sprintf("xkx-idle:%s:%s:%d",
        PLAYER_STATE_D->query_player_id(player), idle_type, now));
    session = ([
        "idle_type": idle_type,
        "room_path": stringp(room_path) ? room_path : "",
        "started_at": now,
        "settlement_id": settlement_id,
        "hours": 0,
        "context": mapp(context) ? copy(context) : ([]),
    ]);
    /* S20/L-192：挂机收益固定，不按挂机点重复递减。 */
    changes = ([ IDLE_SESSION_PATH: save_variable(session) ]);

    if (!commit_idle_state(player, changes, ({ }), "idle/start"))
        return ([ "ok": 0, "error": "idle_state_commit_failed" ]);
    return ([ "ok": 1, "session": session ]);
}

/*
 * 取消挂机会话（玩家在线时主动取消，不结算）。
 */
int cancel_idle_session(object player)
{
    if (!objectp(player))
        return 0;
    if (!commit_idle_state(player, ([ ]), ({ IDLE_SESSION_PATH }),
        "idle/cancel"))
        return 0;
    return 1;
}

/*
 * 结算离线挂机会话（登录恢复时调用）。
 * 按真实离线时长折算，受 24 小时单次上限与每日收益池约束；不做重复递减。
 */
mapping settle_offline_session(object player)
{
    mapping applied;
    mapping before_data;
    mapping result;
    mapping rollback;
    mapping session;
    mapping settlements;
    mapping receipt;
    string session_raw;
    string settler_ref;
    int elapsed;
    int hours;

    if (!objectp(player))
        return ([ "ok": 0, "error": "invalid_player" ]);

    session_raw = player->query(IDLE_SESSION_PATH);
    if (!stringp(session_raw) || session_raw == "")
        return ([ "ok": 0, "error": "no_idle_session" ]);

    session = restore_variable(session_raw);
    if (!mapp(session) || !stringp(session["idle_type"]) || session["idle_type"] == "")
        return ([ "ok": 0, "error": "invalid_idle_session" ]);
    if (!stringp(session["settlement_id"]) || session["settlement_id"] == "")
        session["settlement_id"] = hash("sha256", sprintf("xkx-idle:%s:%s:%d",
            PLAYER_STATE_D->query_player_id(player), session["idle_type"],
            intp(session["started_at"]) ? session["started_at"] : time()));

    settlements = player->query(IDLE_SETTLEMENTS_PATH);
    settlements = mapp(settlements)
        ? restore_variable(save_variable(settlements)) : ([]);
    receipt = settlements[session["settlement_id"]];
    if (mapp(receipt) && receipt["status"] == "applied") {
        if (!commit_idle_state(player, ([ ]), ({ IDLE_SESSION_PATH }),
            "idle/settle_duplicate"))
            return ([ "ok": 0, "error": "idle_state_commit_failed" ]);
        return ([ "ok": 1, "duplicate": 1, "hours": receipt["hours"] || 0,
            "applied": receipt["applied"] || ([]),
            "message": "这次挂机收益已经结算过了。" ]);
    }

    elapsed = time() - (intp(session["started_at"]) ? session["started_at"] : time());
    if (elapsed < 60)
        return ([ "ok": 0, "error": "idle_too_short", "message": "你才刚坐下就回来了，还没什么收获。" ]);

    hours = elapsed / 3600;
    if (hours > IDLE_MAX_HOURS)
        hours = IDLE_MAX_HOURS;

    settler_ref = query_idle_settler(session["idle_type"]);
    if (settler_ref == "")
        return ([ "ok": 0, "error": "unknown_idle_type" ]);
    if (!function_exists("query_data", player))
        return ([ "ok": 0, "error": "idle_snapshot_unavailable",
            "retryable": 1 ]);
    before_data = restore_variable(save_variable(player->query_data()));
    if (!mapp(before_data))
        return ([ "ok": 0, "error": "idle_snapshot_unavailable",
            "retryable": 1 ]);

    /* 进度分量半小时粒度（不满半小时不算，向下取整）。
     * elapsed_seconds 由本函数单点计算并钳到 24 小时上限后随 session 下发；
     * 结算器据此独立计算进度，恢复分量使用整小时语义。 */
    session["elapsed_seconds"] = min(elapsed, IDLE_MAX_SECONDS);

    result = call_other(settler_ref, "settle_idle", player, hours, session);
    if (!mapp(result) || !result["ok"]) {
        rollback = rollback_idle_effects(player, before_data,
            "idle/settle_failure_rollback");
        return mapp(result) ? result + ([
            "rollback_ok": rollback["ok"]
        ]) : ([ "ok": 0, "error": "settle_failed",
            "rollback_ok": rollback["ok"] ]);
    }

    applied = mapp(result["applied"]) ? result["applied"] : ([]);
    settlements[session["settlement_id"]] = ([
        "status": "applied",
        "idle_type": session["idle_type"],
        "hours": hours,
        "applied": applied,
        "settled_at": time(),
    ]);
    if (!commit_idle_state(player,
        ([ IDLE_SETTLEMENTS_PATH: settlements ]), ({ IDLE_SESSION_PATH }),
        "idle/settle_commit")) {
        rollback = rollback_idle_effects(player, before_data,
            "idle/settle_commit_rollback");
        return ([ "ok": 0, "error": "idle_state_commit_failed",
            "retryable": 1, "rollback_ok": rollback["ok"],
            "pending_recovery": rollback["ok"] ? 0 : 1 ]);
    }
    result["applied"] = applied;
    result["session"] = session;
    result["hours"] = hours;

    if (stringp(result["message"]) && result["message"] != "")
        call_other("/kernel/framework/xk_protocol_core", "xk_send", player,
            "IDLE", ([
                "action": "idle_settled",
                "idle_type": session["idle_type"],
                "hours": hours,
                "message": result["message"],
                "applied": applied,
            ]));
    return result;
}

/* ============ 状态查询 ============ */

mapping query_idle_status(object player)
{
    mapping session;
    string session_raw;

    if (!objectp(player))
        return ([ "ok": 0, "error": "invalid_player" ]);
    session_raw = player->query(IDLE_SESSION_PATH);
    if (!stringp(session_raw) || session_raw == "")
        return ([ "ok": 1, "active": 0 ]);
    session = restore_variable(session_raw);
    return ([
        "ok": 1,
        "active": 1,
        "session": mapp(session) ? session : ([]),
    ]);
}

mapping query_idle_config()
{
    return ([
        "schema_version": 1,
        "types": keys(_idle_settlers),
        "max_hours": IDLE_MAX_HOURS,
        "max_seconds": IDLE_MAX_SECONDS,
        "reward_decay": ([ "enabled": 0, "scope": "none" ]),
        "registration": ([
            "public": 1,
            "owner": "/daemons/services/gameplay/idle/*",
            "settler_contract": "mapping settle_idle(object player, int hours, mapping session)",
        ]),
    ]);
}

void create()
{
    ::create();
    _idle_settlers = ([
        "dazuo": "/daemons/services/gameplay/idle/idle_dazuo",
        "practice": "/daemons/services/gameplay/idle/idle_practice",
    ]);
}
