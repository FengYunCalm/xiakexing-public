/**
 * @file    quest_escort_runtime.c
 * @brief   护送任务运行时,基于 step machine 与 transition 推进护送流程。
 */

#pragma strict_types
#pragma save_types

#include <game/quest.h>
#include <system/ansi.h>
#include <system/services.h>

#define QUEST_D                  "/daemons/services/gameplay/quest_d"
#define WORLD_ENTITY_LIFECYCLE_D "/daemons/services/world/world_entity_lifecycle_d"
#define WORLD_TRAVEL_D           "/daemons/services/world/world_travel_d"
#define QUEST_CATALOG_HELPER     "/daemons/services/gameplay/quest/quest_catalog"

#define QUEST_STEP_MACHINE_HELPER "/daemons/services/gameplay/quest/quest_step_machine"
#define QUEST_TRANSITION_HELPER   "/daemons/services/gameplay/quest/quest_transition_d"
#define QUEST_EVENT_ROUTER_HELPER "/daemons/services/gameplay/quest/quest_event_router"

inherit "/lib/framework/daemon";

private string query_escort_target(mapping quest_def)
{
    mapping escort;

    if (!mapp(quest_def))
        return "";
    escort = quest_def["escort"];
    if (mapp(escort) && stringp(escort["target"]) && escort["target"] != "")
        return escort["target"];
    if (stringp(quest_def["escort_target"]) && quest_def["escort_target"] != "")
        return quest_def["escort_target"];
    return "";
}

private string query_player_id(object player)
{
    string player_id;

    if (!objectp(player))
        return "unknown";
    player_id = get_object_id(player);
    return stringp(player_id) && player_id != "" ? player_id : "unknown";
}

private string query_escort_destination(mapping quest_def)
{
    mapping escort;

    if (!mapp(quest_def))
        return "";
    escort = quest_def["escort"];
    if (mapp(escort) && stringp(escort["destination"]) && escort["destination"] != "")
        return escort["destination"];
    if (stringp(quest_def["escort_destination"]) && quest_def["escort_destination"] != "")
        return quest_def["escort_destination"];
    return "";
}

private string *query_object_ids(object target)
{
    /* L-147：对象 id 解析合并到 quest_event_router 的唯一 owner——同域不再
     * 维护第二套字段集（router 版是 escort 版的超集，额外含
     * world_lifecycle/content_entity_id 与 template_id）。 */
    return call_other(QUEST_EVENT_ROUTER_HELPER, "query_object_ids", target);
}

private object find_escort_target(object room, string target_id)
{
    object item;

    if (!objectp(room) || !stringp(target_id) || target_id == "")
        return 0;

    foreach (item in all_inventory(room)) {
        if (!objectp(item))
            continue;
        if (userp(item))
            continue;
        if (member_array(target_id, query_object_ids(item)) != -1)
            return item;
    }
    return 0;
}

private object resolve_escort_target(object old_env, object new_env, string target_id)
{
    object target;

    target = find_escort_target(old_env, target_id);
    if (objectp(target))
        return target;
    return find_escort_target(new_env, target_id);
}

private object resolve_leased_target(object player, mapping lease);

/* lease token 构造——escort:<player>:<quest>:<lease_seq>:<epoch>。
 * seq 为玩家持久单调计数器（move 前分配），epoch 为 driver 运行时
 * （daemon reload 不变、重启变化）；禁止 time() 承担唯一性。 */
private string build_lease_token(string player_id, string quest_id,
    int lease_seq, string epoch)
{
    return sprintf("escort:%s:%s:%d:%s", player_id, quest_id,
        lease_seq, epoch);
}

private int commit_escort_leases(object player, mapping leases,
    string operation)
{
    mapping change;
    mapping response;

    if (!objectp(player) || !mapp(leases))
        return 0;
    change = ([ "escort/leases": leases ]);
    response = PLAYER_STATE_D->commit_state_fields(player, change,
        PLAYER_STATE_D->build_state_idempotency_key(player,
            operation || "quest/escort_leases", change));
    return mapp(response) && response["ok"];
}

private int commit_lease_compensations(object player, mapping compensations,
    string operation)
{
    mapping change;
    mapping response;

    if (!objectp(player) || !mapp(compensations))
        return 0;
    change = ([ "escort/lease_compensations": compensations ]);
    response = PLAYER_STATE_D->commit_state_fields(player, change,
        PLAYER_STATE_D->build_state_idempotency_key(player,
            operation || "quest/escort_compensations", change));
    return mapp(response) && response["ok"];
}

private int record_lease_compensation(object player, mapping compensation)
{
    mapping compensations;
    string operation_id;

    if (!objectp(player) || !mapp(compensation))
        return 0;
    operation_id = compensation["operation_id"] || "";
    if (operation_id == "")
        return 0;
    compensations = player->query("escort/lease_compensations");
    compensations = mapp(compensations) ? copy(compensations) : ([ ]);
    compensations[operation_id] = compensation;
    return commit_lease_compensations(player, compensations,
        "quest/escort_compensation/" + operation_id);
}

private int record_bind_lease_compensation(object player, object target,
    mapping lease, string quest_id)
{
    mapping compensation;
    string operation_id;

    if (!objectp(player) || !objectp(target) || !mapp(lease))
        return 0;
    operation_id = "escort-bind/" + query_player_id(player) + "/"
        + quest_id + "/" + (lease["lease_token"] || "");
    compensation = ([
        "operation_id": operation_id,
        "player_id": query_player_id(player),
        "quest_id": quest_id,
        "lease_token": lease["lease_token"] || "",
        "entity_key": lease["entity_key"] || "",
        "target_file": file_name(target),
        "lease_restore": ([ "present": 1, "value": copy(lease) ]),
        "spawn_room": target->query_temp("escort/spawn_room") || "",
        "preserve_existing_lease": 1,
        "rebind_required": 1,
        "attempts": 0,
        "state": "binding_restore_pending",
        "created_at": time(),
    ]);
    if (record_lease_compensation(player, compensation))
        return 1;
    log_file("system/quest", sprintf(
        "[%s] escort bind compensation persistence failed: player=%s quest=%s\n",
        ctime(time()), query_player_id(player), quest_id));
    target->set_temp("escort/lease_pending", compensation);
    return 0;
}

/* 目标 temp 标记快照/还原：lease 提交失败时，目标已被 move_actor 移动，
 * 必须把位置、owner/quest/token 标记还原到本次移动前的状态，不能留下
 * “目标跟随了但租约没落盘”的半提交。 */
private mapping snapshot_escort_target_temp(object target)
{
    mapping snapshot;

    if (!objectp(target))
        return 0;
    snapshot = ([
        "escort/owner": target->query_temp("escort/owner"),
        "escort/quest": target->query_temp("escort/quest"),
        "escort/acquired_at": target->query_temp("escort/acquired_at"),
        "escort/origin_room": target->query_temp("escort/origin_room"),
        "escort/spawn_room": target->query_temp("escort/spawn_room"),
        "escort/lease_token": target->query_temp("escort/lease_token"),
        "escort/lease_epoch": target->query_temp("escort/lease_epoch"),
        "escort/lease_pending": target->query_temp("escort/lease_pending"),
    ]);
    return snapshot;
}

private void restore_escort_target_temp(object target, mapping snapshot)
{
    string key;

    if (!objectp(target) || !mapp(snapshot))
        return;
    foreach (key in keys(snapshot)) {
        if (undefinedp(snapshot[key]))
            target->delete_temp(key);
        else
            target->set_temp(key, snapshot[key]);
    }
}

/* lease 提交失败补偿：位置回滚 + temp 标记还原。
 * 位置回滚失败时保留显式 pending 标记（含 quest id 与原 spawn），
 * 由后续登录/房间生命周期路径重试，不静默丢弃。 */
private int compensate_failed_lease_commit(object player, object target,
    object old_env, mapping temp_before, mapping lease,
    mapping lease_restore, string quest_id)
{
    mapping moved;
    mapping compensation;
    string lease_token;
    string entity_key;
    int position_restored;

    if (!objectp(target))
        return 0;
    lease_token = target->query_temp("escort/lease_token") || "";
    entity_key = mapp(lease) && stringp(lease["entity_key"])
        ? lease["entity_key"] : "";
    position_restored = 1;
    if (objectp(old_env) && environment(target) != old_env) {
        moved = WORLD_TRAVEL_D->move_actor(target, old_env, ([
            "reason": "quest_escort_lease_rollback",
            "expected_location_generation":
                target->query_temp("world/location_generation") || 0,
        ]));
        position_restored = mapp(moved) && moved["ok"];
    }
    restore_escort_target_temp(target, temp_before);
    if (!position_restored) {
        compensation = ([
            "operation_id": "escort-compensation/" + query_player_id(player)
                + "/" + quest_id + "/" + lease_token,
            "player_id": query_player_id(player),
            "quest_id": quest_id,
            "lease_token": lease_token,
            "entity_key": entity_key,
            "target_file": file_name(target),
            "lease_restore": lease_restore,
            "spawn_room": objectp(old_env) ? base_name(old_env) : "",
            "preserve_existing_lease": mapp(temp_before)
                && stringp(temp_before["escort/lease_token"])
                && temp_before["escort/lease_token"] != "" ? 1 : 0,
            "attempts": 0,
            "state": "position_rollback_pending",
            "created_at": time(),
        ]);
        if (!record_lease_compensation(player, compensation))
            log_file("system/quest", sprintf(
                "[%s] escort compensation persistence failed: player=%s quest=%s\n",
                ctime(time()), query_player_id(player), quest_id));
        target->set_temp("escort/lease_pending", compensation);
        return 0;
    }
    return 1;
}

private object resolve_lease_compensation_target(object player,
    mapping compensation)
{
    mapping resolution;
    object target;

    if (!objectp(player) || !mapp(compensation)
        || !stringp(compensation["target_file"])
        || compensation["target_file"] == "")
        return 0;
    resolution = WORLD_TRAVEL_D->resolve_target(player,
        compensation["target_file"], ([ "resolve_exact": 1 ]));
    target = mapp(resolution) ? resolution["room"] : 0;
    if (!objectp(target) || file_name(target) != compensation["target_file"])
        return 0;
    return target;
}

private void clear_lease_compensation_temp(object target, mapping compensation)
{
    string current_token;

    if (!objectp(target) || !mapp(compensation))
        return;
    current_token = target->query_temp("escort/lease_token") || "";
    if (!compensation["preserve_existing_lease"]
        || current_token == compensation["lease_token"]) {
        target->delete_temp("escort/owner");
        target->delete_temp("escort/quest");
        target->delete_temp("escort/acquired_at");
        target->delete_temp("escort/origin_room");
        target->delete_temp("escort/lease_token");
        target->delete_temp("escort/lease_epoch");
    }
    target->delete_temp("escort/lease_pending");
}

/* 持久补偿消费者：提交失败后的回位不能只靠 target temp。
 * 记录保留到目标回位、能力撤销和玩家状态删除都成功；任一步失败都留
 * 在同一 mapping 供下一次登录/登出重试。 */
mapping recover_player_escort_compensations(object player)
{
    mapping compensations;
    mapping compensation;
    mapping leases;
    object target;
    mapping moved;
    string operation_id;
    string spawn_room;
    int recovered;
    int pending;
    int attempts;
    int rebound;

    if (!objectp(player))
        return ([ "ok": 0, "recovered": 0, "pending": 0,
            "error": "invalid player" ]);
    compensations = player->query("escort/lease_compensations");
    if (!mapp(compensations) || sizeof(compensations) == 0)
        return ([ "ok": 1, "recovered": 0, "pending": 0 ]);
    compensations = copy(compensations);
    foreach (operation_id, compensation in copy(compensations)) {
        if (!mapp(compensation)) {
            map_delete(compensations, operation_id);
            continue;
        }
        if (mapp(compensation["lease_restore"])) {
            leases = player->query("escort/leases");
            leases = mapp(leases) ? copy(leases) : ([ ]);
            if (compensation["lease_restore"]["present"])
                leases[compensation["quest_id"]] = copy(
                    compensation["lease_restore"]["value"]);
            else
                map_delete(leases, compensation["quest_id"]);
            if (!commit_escort_leases(player, leases,
                "quest/escort_compensation_restore/" + operation_id)) {
                pending++;
                continue;
            }
        }
        target = resolve_lease_compensation_target(player, compensation);
        if (!objectp(target)) {
            pending++;
            continue;
        }
        if (compensation["rebind_required"]) {
            rebound = call_other(WORLD_ENTITY_LIFECYCLE_D,
                "bind_entity_lease", compensation["lease_token"] || "",
                target, ([ "entity_key": compensation["entity_key"] || "" ]));
            if (!rebound) {
                attempts = (compensation["attempts"] || 0) + 1;
                compensation["attempts"] = attempts;
                compensation["state"] = "binding_restore_pending";
                compensations[operation_id] = compensation;
                commit_lease_compensations(player, compensations,
                    "quest/escort_compensation_rebind_retry/" + operation_id);
                pending++;
                continue;
            }
            map_delete(compensations, operation_id);
            if (!commit_lease_compensations(player, compensations,
                "quest/escort_compensation_rebind_clear/" + operation_id)) {
                compensations[operation_id] = compensation;
                pending++;
                continue;
            }
            target->delete_temp("escort/lease_pending");
            recovered++;
            continue;
        }
        spawn_room = compensation["spawn_room"] || "";
        if (spawn_room == ""
            || (objectp(environment(target))
                && base_name(environment(target)) == spawn_room)) {
            /* 已在原槽：补偿目标不存在也视为位置阶段完成，随后撤销
             * candidate capability；旧 lease（rebind）不受影响。 */
        } else {
            moved = WORLD_TRAVEL_D->move_actor(target, spawn_room, ([
                "reason": "quest_escort_compensation_retry",
                "expected_location_generation":
                    target->query_temp("world/location_generation") || 0,
            ]));
            if (!mapp(moved) || !moved["ok"]) {
                attempts = (compensation["attempts"] || 0) + 1;
                compensation["attempts"] = attempts;
                compensation["state"] = "position_rollback_pending";
                compensation["last_error"] = "move to spawn failed";
                compensations[operation_id] = compensation;
                commit_lease_compensations(player, compensations,
                    "quest/escort_compensation_retry/" + operation_id);
                pending++;
                continue;
            }
        }
        call_other(WORLD_ENTITY_LIFECYCLE_D, "discard_entity_lease",
            compensation["lease_token"] || "", compensation["entity_key"] || "");
        clear_lease_compensation_temp(target, compensation);
        map_delete(compensations, operation_id);
        if (!commit_lease_compensations(player, compensations,
            "quest/escort_compensation_clear/" + operation_id)) {
            compensations[operation_id] = compensation;
            pending++;
            continue;
        }
        recovered++;
    }
    return ([ "ok": pending == 0, "recovered": recovered,
        "pending": pending ]);
}

/* 分配下一个持久 lease seq（调用方在 move 前调用并 set 落盘，
 * 保证同秒重获 token 不同、崩溃不重号）。 */
private int next_lease_seq(object player)
{
    mapping change;
    mapping response;
    int seq;

    seq = (player->query("escort/lease_seq") || 0) + 1;
    change = ([ "escort/lease_seq": seq ]);
    response = PLAYER_STATE_D->commit_state_fields(player, change,
        PLAYER_STATE_D->build_state_idempotency_key(player,
            "quest/escort_lease_seq", change));
    return mapp(response) && response["ok"] ? seq : 0;
}

private int move_escort_target(object player, object target, object old_env, object new_env,
    string quest_id)
{
    string existing_owner;
    string target_name;
    string spawn_room;
    mapping leases;
    mapping temp_before;
    mapping lease_restore;
    string epoch;
    int lease_seq;
    int rebind;
    object incumbent;
    mapping moved;

    if (!objectp(target) || !objectp(new_env))
        return 0;
    if (environment(target) != old_env)
        return 0;

    /* 独占租约：目标已被其他玩家护送时拒绝（不带走他人目标）；
     * 同一玩家同一 quest 的重复移动（重入）仍允许，同人不同 quest 拒绝
     * （防止一个玩家的另一护送任务覆盖目标 lease）。
     * 校验必须先于"目标已在 new_env"快捷返回——否则玩家 B 进入目标所在
     * 房间可绕过租约推进自己的护送。 */
    existing_owner = target->query_temp("escort/owner");
    if (stringp(existing_owner) && existing_owner != ""
        && (existing_owner != query_player_id(player)
            || target->query_temp("escort/quest") != quest_id))
        return 0;
    if (environment(target) == new_env)
        return 1;

    /* move 前分配持久 seq 并落盘（防同秒碰撞/崩溃重号），
     * lease 表项在 move 成功后写入（含已分配 seq 的 token）。 */
    leases = player->query("escort/leases");
    leases = mapp(leases) ? copy(leases) : ([]);
    lease_restore = ([
        "present": mapp(leases[quest_id]) ? 1 : 0,
        "value": mapp(leases[quest_id]) ? copy(leases[quest_id]) : 0,
    ]);
    if (!mapp(leases[quest_id])) {
        lease_seq = next_lease_seq(player);
        if (lease_seq <= 0)
            return 0;
    } else if (stringp(leases[quest_id]["target_file"])
        && leases[quest_id]["target_file"] != file_name(target)) {
        /* 只有原 token 的 clone 已经不存在、候选仍在原 spawn slot 时才换绑。
         * next seq 必须在 move 前完成；失败不得移动目标或伪造 seq=0 token。 */
        incumbent = resolve_leased_target(player, leases[quest_id]);
        if (objectp(incumbent) && incumbent != target)
            return 0;
        if (leases[quest_id]["entity_key"] != call_other(
            WORLD_ENTITY_LIFECYCLE_D, "query_entity_index_key_for_object",
            old_env, target))
            return 0;
        lease_seq = next_lease_seq(player);
        if (lease_seq <= 0)
            return 0;
        rebind = 1;
    } else
        lease_seq = 0;

    moved = WORLD_TRAVEL_D->move_actor(target, new_env, ([
        "reason": "quest_escort",
        "expected_location_generation":
            target->query_temp("world/location_generation") || 0,
    ]));
    if (!mapp(moved) || !moved["ok"])
        return 0;

    /* 目标生命周期标记——归还/释放时按此回到原 spawn。
     * old_env 是目标移动前所在房间（首次移动即其原 spawn 房间）。
     * target temp 只存 token/epoch capability（不存 active 列表），
     * 持久 lease 是唯一 owner 记录。写前先快照，lease 提交失败要还原。 */
    temp_before = snapshot_escort_target_temp(target);
    spawn_room = target->query_temp("escort/spawn_room");
    if (!stringp(spawn_room) || spawn_room == "")
        target->set_temp("escort/spawn_room", base_name(old_env));
    target->set_temp("escort/owner", query_player_id(player));
    target->set_temp("escort/quest", quest_id);
    target->set_temp("escort/acquired_at", time());
    target->set_temp("escort/origin_room", base_name(old_env));

    /* 持久 lease 表（player 持久数据）——登出/断线后唯一可重试路径。
     * 记录 stable content entity id（WORLD_ENTITY_LIFECYCLE_D 解析真源）与当次
     * clone file_name（诊断用）。fresh clone 移动时刷新身份（旧 lease
     * 不指向已销毁实例）。 */
    epoch = call_other(WORLD_ENTITY_LIFECYCLE_D, "query_world_runtime_epoch");
    if (!mapp(leases[quest_id])) {
        /* spawn key 只在首次 acquire 用 old_env（移动前房间）写一次，
         * 不可变——move 后用 environment(target) 构造会指向目标房间而非
         * spawn slot，resolver 必然 miss。lease_token 唯一标识本次租约
         * 拥有的 clone incarnation（防 fresh clone 误认），由持久 seq +
         * driver epoch 构成，同秒重获必然不同。 */
        leases[quest_id] = ([
            "player_id": query_player_id(player),
            "quest_id": quest_id,
            "entity_key": call_other(WORLD_ENTITY_LIFECYCLE_D,
                "query_entity_index_key_for_object", old_env, target),
            "content_generation": target->query_temp(
                "world_lifecycle/content_entity_generation")
                || CONTENT_REGISTRY_D->query_content_generation(),
            "lease_token": build_lease_token(query_player_id(player),
                quest_id, lease_seq, epoch),
            "lease_seq": lease_seq,
            "target_file": file_name(target),
            "target_path": base_name(target),
            "spawn_room": target->query_temp("escort/spawn_room"),
            "state": "active",
            "acquired_at": time(),
            "attempts": 0,
        ]);
        target->set_temp("escort/lease_token",
            leases[quest_id]["lease_token"]);
        target->set_temp("escort/lease_epoch", epoch);
        if (!commit_escort_leases(player, leases,
            "quest/escort_acquire")) {
            compensate_failed_lease_commit(player, target, old_env,
                temp_before, leases[quest_id], lease_restore, quest_id);
            return 0;
        }
        if (!call_other(WORLD_ENTITY_LIFECYCLE_D, "bind_entity_lease",
            leases[quest_id]["lease_token"], target, ([
                "entity_key": leases[quest_id]["entity_key"],
            ]))) {
            leases[quest_id]["state"] = "bind_pending";
            leases[quest_id]["updated_at"] = time();
            commit_escort_leases(player, leases, "quest/escort_bind_retry");
            record_bind_lease_compensation(player, target, leases[quest_id],
                quest_id);
            target->set_temp("escort/lease_pending", ([
                "quest_id": quest_id,
                "state": "bind_pending",
                "lease_token": leases[quest_id]["lease_token"],
            ]));
            return 0;
        }
    } else if (rebind) {
        /* fresh clone（旧实例销毁/补刷）：刷新 clone 身份并换发新 token
         * （旧 token 只属于已销毁实例）；spawn key 不变（同 spawn slot）。
         * seq 已在 move 前成功分配。 */
        leases[quest_id]["lease_token"] = build_lease_token(
            query_player_id(player), quest_id, lease_seq, epoch);
        leases[quest_id]["lease_seq"] = lease_seq;
        leases[quest_id]["target_file"] = file_name(target);
        leases[quest_id]["target_path"] = base_name(target);
        leases[quest_id]["spawn_room"] = target->query_temp("escort/spawn_room");
        leases[quest_id]["content_generation"] = target->query_temp(
            "world_lifecycle/content_entity_generation")
            || CONTENT_REGISTRY_D->query_content_generation();
        leases[quest_id]["state"] = "active";
        target->set_temp("escort/lease_token",
            leases[quest_id]["lease_token"]);
        target->set_temp("escort/lease_epoch", epoch);
        if (!commit_escort_leases(player, leases,
            "quest/escort_rebind")) {
            compensate_failed_lease_commit(player, target, old_env,
                temp_before, leases[quest_id], lease_restore, quest_id);
            return 0;
        }
        if (!call_other(WORLD_ENTITY_LIFECYCLE_D, "bind_entity_lease",
            leases[quest_id]["lease_token"], target, ([
                "entity_key": leases[quest_id]["entity_key"],
            ]))) {
            leases[quest_id]["state"] = "bind_pending";
            leases[quest_id]["updated_at"] = time();
            commit_escort_leases(player, leases, "quest/escort_bind_retry");
            record_bind_lease_compensation(player, target, leases[quest_id],
                quest_id);
            target->set_temp("escort/lease_pending", ([
                "quest_id": quest_id,
                "state": "bind_pending",
                "lease_token": leases[quest_id]["lease_token"],
            ]));
            return 0;
        }
    }

    target_name = function_exists("query_name", target) ? target->query_name() : 0;
    if (!stringp(target_name) || target_name == "")
        target_name = target->query("name") || "护送目标";
    msg_send(player, HIC + target_name + "跟着你移动了。" NOR "\n");
    return 1;
}

private int complete_legacy_escort(mapping quest_definitions, mapping player_data,
    string quest_id, mapping quest_def, mapping quest_info, object target, object new_env)
{
    string destination;

    if (!mapp(quest_info)
        || (arrayp(quest_info["steps"]) && sizeof(quest_info["steps"]) > 0))
        return 0;

    destination = query_escort_destination(quest_def);
    if (!stringp(destination) || destination == "")
        return 0;
    if (base_name(new_env) != destination || environment(target) != new_env)
        return 0;

    quest_info["progress"] = 1;
    call_other(QUEST_TRANSITION_HELPER, "set_completed_if_ready", player_data, quest_id, 1);
    return 1;
}

private mapping query_quest_record_across_buckets(mapping data, string quest_id);
int record_escort_progress(mapping quest_definitions, mapping player_data,
    object player, object old_env, object new_env)
{
    mapping quest_def;
    mapping quest_info;
    object target;
    string quest_id;
    string target_id;
    int updated;

    if (!mapp(quest_definitions) || !mapp(player_data) || !mapp(player_data["active"])
        || !objectp(player) || !objectp(old_env) || !objectp(new_env))
        return 0;

    foreach (quest_id, quest_info in player_data["active"]) {
        if (!mapp(quest_info) || quest_info["status"] != QUEST_STATUS_ACCEPTED)
            continue;
        quest_def = quest_definitions[quest_id];
        if (!mapp(quest_def) || quest_def["type"] != QUEST_TYPE_ESCORT)
            continue;

        target_id = query_escort_target(quest_def);
        target = resolve_escort_target(old_env, new_env, target_id);
        if (!objectp(target))
            continue;
        if (!move_escort_target(player, target, old_env, new_env, quest_id))
            continue;

        if (call_other(QUEST_STEP_MACHINE_HELPER, "record_event_progress",
            quest_definitions, player_data, "escort", ([
                "target_ids": query_object_ids(target),
                "room": base_name(new_env),
                "count": 1,
            ]))) {
            updated = 1;
            continue;
        }

        if (complete_legacy_escort(quest_definitions, player_data, quest_id,
            quest_def, quest_info, target, new_env))
            updated = 1;
    }
    return updated;
}

/* Room unload can destroy a leased clone before logout/login cleanup runs.
 * Only accept a replacement at the original stable spawn slot when it has no
 * lease capability; a replacement elsewhere is never treated as the lease. */
private object query_recreated_spawn_target(object player, mapping lease)
{
    object room;
    object target;
    mapping quest_definitions;
    mapping quest_def;
    string target_id;
    string entity_key;
    mapping resolution;

    if (!objectp(player) || !mapp(lease) || !stringp(lease["spawn_room"])
        || lease["spawn_room"] == "")
        return 0;
    quest_definitions = call_other(QUEST_D, "query_quest_definitions");
    quest_def = mapp(quest_definitions) ? quest_definitions[lease["quest_id"]] : 0;
    target_id = query_escort_target(quest_def);
    if (target_id == "")
        return 0;
    resolution = call_other(WORLD_TRAVEL_D, "resolve_target", player,
        lease["spawn_room"], ([ "reason": "escort_recreated_spawn" ]));
    room = mapp(resolution) ? resolution["room"] : 0;
    target = find_escort_target(room, target_id);
    if (!objectp(target)
        || (stringp(target->query_temp("escort/lease_token"))
            && target->query_temp("escort/lease_token") != ""))
        return 0;
    entity_key = call_other(WORLD_ENTITY_LIFECYCLE_D,
        "query_entity_index_key_for_object", room, target);
    if (!stringp(lease["entity_key"]) || lease["entity_key"] == ""
        || entity_key != lease["entity_key"])
        return 0;
    return target;
}

/* 从持久 lease 解析护送目标（唯一 owner 记录）。
 * 顺序：① lease token 经 WORLD_ENTITY_LIFECYCLE_D 派生索引（含 reload 后
 * lazy 回退重建）；② entity_key 全键经 query_live_entity_object 回退；
 * ③ spawn_room 枚举真实 clone（daemon reload 缺口最后手段）。
 * 每步候选都要强 token 校验（temp lease_token 与 lease 表一致），
 * 绝不凭 file_name 弱绑定。 */
private object resolve_leased_target(object player, mapping lease)
{
    object target;
    string token;
    string entity_key;
    string target_file;
    string spawn_room;
    string target_id;
    mapping resolution;
    mapping quest_definitions;
    mapping quest_def;

    if (!mapp(lease) || !objectp(player))
        return 0;
    token = lease["lease_token"];
    entity_key = lease["entity_key"];
    target_file = lease["target_file"];
    /* The exact clone may still be loaded outside the player's room while
     * the derived lifecycle index is being rebuilt after disconnect. Keep
     * path resolution behind the shared travel owner. */
    if (stringp(target_file) && target_file != "") {
        resolution = call_other(WORLD_TRAVEL_D, "resolve_target", player,
            target_file, ([ "resolve_exact": 1 ]));
        target = mapp(resolution) ? resolution["room"] : 0;
        if (objectp(target) && (!stringp(token) || token == ""
            || target->query_temp("escort/lease_token") != token))
            target = 0;
    }
    if (!objectp(target) && stringp(token) && token != "")
        target = call_other(WORLD_ENTITY_LIFECYCLE_D, "query_leased_entity_object",
            token, stringp(entity_key) ? entity_key : "");
    if (!objectp(target) && stringp(entity_key) && entity_key != "") {
        target = call_other(WORLD_ENTITY_LIFECYCLE_D, "query_live_entity_object",
            entity_key);
        if (objectp(target) && (!stringp(token) || token == ""
            || target->query_temp("escort/lease_token") != token))
            target = 0;
    }
    if (!objectp(target) && stringp(lease["spawn_room"]) && lease["spawn_room"] != ""
        && stringp(lease["quest_id"])) {
        quest_definitions = call_other(QUEST_D, "query_quest_definitions");
        quest_def = mapp(quest_definitions) ? quest_definitions[lease["quest_id"]] : 0;
        target_id = query_escort_target(quest_def);
        if (target_id != "") {
            spawn_room = lease["spawn_room"];
            resolution = call_other(WORLD_TRAVEL_D, "resolve_target", player,
                spawn_room, ([ "reason": "escort_lease_recovery" ]));
            target = mapp(resolution) ? resolution["room"] : 0;
            if (objectp(target)) {
                target = find_escort_target(target, target_id);
                if (objectp(target) && (!stringp(token) || token == ""
                    || target->query_temp("escort/lease_token") != token))
                    target = 0;
            }
        }
    }
    return target;
}

/* 护送目标生命周期释放——complete/submit/fail/abandon/turn_in 后
 * 目标回到原 spawn 房间。按 quest_id 隔离：多护送并存时只释放对应目标。
 * move 失败（spawn 房间已删/不可入）时保留标记，供后续兜底定位。 */
/* 持久 lease 表辅助——删除表项（归还完成/目标销毁）并解绑
 * WORLD_ENTITY_LIFECYCLE_D 派生索引。 */
private int delete_persistent_lease(object player, string quest_id, object target)
{
    mapping leases;
    mapping previous_leases;
    mapping lease;
    mapping compensation;
    string token;
    string entity_key;
    int unbound;

    if (!objectp(player) || !stringp(quest_id) || quest_id == "")
        return 0;
    leases = player->query("escort/leases");
    if (!mapp(leases) || !mapp(leases[quest_id]))
        return 0;
    leases = copy(leases);
    previous_leases = copy(leases);
    lease = copy(leases[quest_id]);
    token = lease["lease_token"];
    entity_key = stringp(lease["entity_key"]) ? lease["entity_key"] : "";
    map_delete(leases, quest_id);
    if (!commit_escort_leases(player, leases, "quest/escort_release"))
        return 0;

    if (stringp(token) && token != "" && objectp(target)) {
        unbound = call_other(WORLD_ENTITY_LIFECYCLE_D,
            "unbind_entity_lease", token, target);
        if (!unbound) {
            /* 解绑失败不能被 release 汇总吞掉；先恢复玩家 lease，失败时
             * 留一笔只允许按 expected object 重绑的持久补偿。 */
            if (commit_escort_leases(player, previous_leases,
                "quest/escort_release_restore"))
                return 0;
            compensation = ([
                "operation_id": "escort-rebind/" + query_player_id(player)
                    + "/" + quest_id + "/" + token,
                "player_id": query_player_id(player),
                "quest_id": quest_id,
                "lease_token": token,
                "entity_key": entity_key,
                "target_file": file_name(target),
                "lease_restore": ([
                    "present": 1,
                    "value": copy(lease),
                ]),
                "spawn_room": objectp(environment(target))
                    ? base_name(environment(target)) : "",
                "preserve_existing_lease": 1,
                "rebind_required": 1,
                "attempts": 0,
                "state": "binding_restore_pending",
                "created_at": time(),
            ]);
            if (!record_lease_compensation(player, compensation))
                log_file("system/quest", sprintf(
                    "[%s] escort rebind compensation persistence failed: player=%s quest=%s\n",
                    ctime(time()), query_player_id(player), quest_id));
            target->set_temp("escort/lease_pending", compensation);
            return 0;
        }
    } else if (stringp(token) && token != "") {
        call_other(WORLD_ENTITY_LIFECYCLE_D, "discard_entity_lease", token,
            entity_key);
    }
    return 1;
}


/* 统一 release 事务 owner——全树所有 escort release（terminal/
 * logout/login/reaccept/admin/worker）只经此函数，触发方只传 reason。
 * move 前在同一函数完成身份校验；任一不匹配保留 lease 返回
 * identity_mismatch。attempts 从持久 lease 递增（不读 target temp）。
 * 返回 ([ ok, released, pending, attempts, state, error, changed_domains ])。 */
private string *lease_identity_mismatch(object player, mapping lease, object target)
{
    string *mismatches;
    string world_part;
    string instance_part;
    string epoch;
    string token;

    mismatches = ({ });
    if (!objectp(player) || !mapp(lease) || !objectp(target))
        return ({ "no_target" });
    if (target->query_temp("escort/owner") != query_player_id(player))
        mismatches += ({ "owner" });
    if (target->query_temp("escort/quest") != lease["quest_id"])
        mismatches += ({ "quest" });
    token = lease["lease_token"];
    if (stringp(token) && sscanf(token, "%*s:%*s:%*s:%*d:%s", epoch) == 1
        && target->query_temp("escort/lease_epoch") != epoch)
        mismatches += ({ "epoch" });
    if (stringp(lease["entity_key"])
        && sscanf(lease["entity_key"], "%s|%s|%*s|%*s",
            world_part, instance_part) == 2) {
        if (target->query_temp("world_lifecycle/content_entity_world_id")
            != world_part)
            mismatches += ({ "world" });
        if (target->query_temp("world_lifecycle/content_entity_instance_id")
            != instance_part)
            mismatches += ({ "instance" });
    }
    if (intp(lease["content_generation"])
        && lease["content_generation"] > 0
        && target->query_temp("world_lifecycle/content_entity_generation")
            != lease["content_generation"])
        mismatches += ({ "generation" });
    return mismatches;
}

mapping release_escort_lease(object player, string quest_id, string reason)
{
    mapping lease;
    mapping leases;
    mapping result;
    object target;
    string *mismatches;
    string spawn_room;
    mapping moved;
    int attempts;

    result = ([ "ok": 0, "released": 0, "pending": 0, "attempts": 0,
        "state": "", "error": "", "changed_domains": ({ }) ]);
    if (!objectp(player) || !stringp(quest_id) || quest_id == "")
        return result + ([ "error": "invalid_args" ]);
    /* 持久 lease 是唯一 owner 记录——release 从 lease 解析目标，
     * 不再依赖 temp active_targets（temp 只存 capability）。 */
    leases = player->query("escort/leases");
    if (!mapp(leases) || !mapp(leases[quest_id]))
        return result + ([ "error": "no_lease" ]);
    leases = copy(leases);
    lease = copy(leases[quest_id]);
    if (lease["player_id"] != query_player_id(player))
        return result + ([ "error": "lease_owner_mismatch" ]);
    if (!stringp(lease["lease_token"]) || lease["lease_token"] == "") {
        /* L-148：历史无 token 租约不得永久 pending（否则 accept_quest 因
         * “租约释放未完成”永久拒绝重接）。若能在既有解析路径定位目标且
         * 身份校验（owner/quest/world/instance/generation）通过，继续走
         * 正常释放流程；定位不到或身份不匹配时删除持久租约记录（此时目标
         * 无活跃绑定可破坏）并写审计日志，放行重接。 */
        target = resolve_leased_target(player, lease);
        if (!objectp(target)
            && (reason == "login" || reason == "logout"))
            target = query_recreated_spawn_target(player, lease);
        mismatches = objectp(target)
            ? lease_identity_mismatch(player, lease, target)
            : ({ "unresolved" });
        if (!objectp(target) || sizeof(mismatches) > 0) {
            if (!delete_persistent_lease(player, quest_id, 0))
                return result + ([ "state": "orphaned_legacy", "pending": 1,
                    "error": "lease_persist_failed" ]);
            log_file("system/quest", sprintf(
                "[%s] legacy escort lease dropped: player=%s quest=%s reason=%s mismatches=%O\n",
                ctime(time()), query_player_id(player), quest_id, reason,
                mismatches));
            return result + ([ "ok": 1, "released": 1,
                "state": "legacy_dropped",
                "changed_domains": ({ "quest" }) ]);
        }
        /* 身份校验通过：落回下方统一释放流程（token 已在解析内跳过，
         * owner/quest 标记由 lease_identity_mismatch 校验）。 */
        lease["state"] = "legacy_verified";
        lease["updated_at"] = time();
        leases[quest_id] = lease;
    }
    target = resolve_leased_target(player, lease);
    if (!objectp(target)
        && (reason == "login" || reason == "logout")) {
        target = query_recreated_spawn_target(player, lease);
        if (objectp(target)) {
            if (!delete_persistent_lease(player, quest_id, 0))
                return result + ([ "pending": 1,
                    "error": "lease_persist_failed" ]);
            return result + ([ "ok": 1, "released": 1,
                "state": "recreated_spawn",
                "changed_domains": ({ "quest" }) ]);
        }
    }
    if (!objectp(target))
        return result + ([ "error": "target_not_found", "pending": 1,
            "state": lease["state"] || "active" ]);
    /* move 前统一身份校验（token 强校验已在 resolve 内完成，
     * 此处补 owner/quest/epoch/world/instance/generation）。任一不匹配
     * 保留 lease，不移动不清理。 */
    mismatches = lease_identity_mismatch(player, lease, target);
    if (sizeof(mismatches) > 0)
        return result + ([ "error": "identity_mismatch",
            "mismatch_fields": mismatches, "pending": 1,
            "state": lease["state"] || "active" ]);

    spawn_room = target->query_temp("escort/spawn_room");
    if ((!stringp(spawn_room) || spawn_room == "")
        && stringp(lease["spawn_room"]))
        spawn_room = lease["spawn_room"];

    /* 目标已在原 spawn（含未移动过）：归还完成，清 target 标记与持久表项。 */
    if (!stringp(spawn_room) || spawn_room == ""
        || environment(target) == 0
        || base_name(environment(target)) == spawn_room) {
        if (!delete_persistent_lease(player, quest_id, target))
            return result + ([ "pending": 1,
                "error": "lease_persist_failed" ]);
        target->delete_temp("escort/owner");
        target->delete_temp("escort/quest");
        target->delete_temp("escort/acquired_at");
        target->delete_temp("escort/origin_room");
        target->delete_temp("escort/spawn_room");
        target->delete_temp("escort/lease_token");
        target->delete_temp("escort/lease_epoch");
        target->delete_temp("escort/lease_pending");
        return result + ([ "ok": 1, "released": 1, "state": "released",
            "changed_domains": ({ "quest" }) ]);
    }
    if (!function_exists("move", target))
        return result + ([ "error": "no_move", "pending": 1 ]);

    /* 回 spawn 失败：attempts 从持久 lease 递增（不读 target temp），
     * 保留完整 lease（player 记录 + 持久表项），不静默删除唯一可重试路径。 */
    moved = WORLD_TRAVEL_D->move_actor(target, spawn_room, ([
        "reason": "quest_escort_release",
        "expected_location_generation":
            target->query_temp("world/location_generation") || 0,
    ]));
    if (!mapp(moved) || !moved["ok"]) {
        attempts = (lease["attempts"] || 0) + 1;
        lease["state"] = "release_pending";
        lease["attempts"] = attempts;
        lease["last_error"] = "move to spawn failed";
        lease["updated_at"] = time();
        leases[quest_id] = lease;
        if (!commit_escort_leases(player, leases,
            "quest/escort_release_retry"))
            return result + ([ "pending": 1, "attempts": attempts,
                "state": "release_pending", "error": "lease_persist_failed" ]);
        return result + ([ "pending": 1, "attempts": attempts,
            "state": "release_pending", "error": "move_to_spawn_failed" ]);
    }
    if (!delete_persistent_lease(player, quest_id, target))
        return result + ([ "pending": 1,
            "error": "lease_persist_failed" ]);
    target->delete_temp("escort/owner");
    target->delete_temp("escort/quest");
    target->delete_temp("escort/acquired_at");
    target->delete_temp("escort/origin_room");
    target->delete_temp("escort/spawn_room");
    target->delete_temp("escort/lease_token");
    target->delete_temp("escort/lease_epoch");
    target->delete_temp("escort/lease_pending");
    target->delete_temp("escort/release_attempts");
    target->delete_temp("escort/release_pending");
    target->delete_temp("escort/last_error");
    return result + ([ "ok": 1, "released": 1, "state": "released",
        "changed_domains": ({ "quest" }) ]);
}

/* terminal 统一释放——按 before/after 终态变化释放护送目标。 */
int release_escort_targets_for_terminal(object player, mapping before_data, mapping after_data)
{
    mapping after_record;
    mapping definition;
    mapping quest_definitions;
    mapping before_active;
    mapping before_info;
    string quest_id;

    if (!objectp(player) || !mapp(before_data) || !mapp(after_data))
        return 0;
    before_active = before_data["active"];
    if (!mapp(before_active))
        return 0;
    quest_definitions = call_other(QUEST_D, "query_quest_definitions");
    foreach (quest_id, before_info in before_active) {
        if (!mapp(before_info) || before_info["status"] != QUEST_STATUS_ACCEPTED)
            continue;
        definition = mapp(quest_definitions) ? quest_definitions[quest_id] : 0;
        if (!mapp(definition) || definition["type"] != QUEST_TYPE_ESCORT)
            continue;
        after_record = query_quest_record_across_buckets(after_data, quest_id);
        if (!mapp(after_record)
            || !call_other(QUEST_TRANSITION_HELPER, "is_terminal_quest_status",
                after_record["status"]))
            continue;
        /* 统一 owner——触发方只传 reason。 */
        release_escort_lease(player, quest_id, "terminal");
    }
    return 1;
}

/* §10.3：登录恢复消费者——遍历玩家持久 lease 表，对每个 escort 任务
 * 调统一 release（滞留目标回 spawn / 失败保留 pending）。
 * 返回聚合：([ ok, recovered, pending ])。
 * 由 quest_d 注册到 LOGIN_LIFECYCLE_D（login 完成时执行）。 */
mapping recover_player_escort_leases(object player)
{
    mapping leases;
    mapping result;
    mapping compensation_result;
    string *quest_ids;
    string quest_id;
    int recovered;
    int pending;
    int compensated;
    int all_ok;

    if (!objectp(player))
        return ([ "ok": 0, "error": "invalid player" ]);
    compensation_result = recover_player_escort_compensations(player);
    leases = player->query("escort/leases");
    if (!mapp(leases))
        return ([ "ok": compensation_result["ok"], "recovered": 0,
            "compensated": compensation_result["recovered"],
            "pending": compensation_result["pending"] ]);
    quest_ids = keys(leases);
    all_ok = compensation_result["ok"];
    compensated = compensation_result["recovered"];
    pending = compensation_result["pending"];
    foreach (quest_id in quest_ids) {
        if (!mapp(leases[quest_id])
            || leases[quest_id]["player_id"] != query_player_id(player))
            continue;
        result = release_escort_lease(player, quest_id, "login");
        if (mapp(result) && result["ok"] && result["released"])
            recovered++;
        else {
            all_ok = 0;
            if (mapp(result) && result["pending"])
                pending++;
        }
    }
    return ([ "ok": all_ok, "recovered": recovered,
        "compensated": compensated, "pending": pending ]);
}

/* 跨桶取任务记录（终态迁移后所在桶）。桶名经
 * QUEST_TRANSITION_HELPER->query_status_bucket 单一 owner 解析，
 * 不硬编码桶名表（新增终态桶只需改 transition_d 一处）。
 * 任务被整体删除（三桶均查不到）返回 0：调用方跳过释放，lease
 * 保留至登出清理（登出释放路径不依赖任务记录）。 */
private mapping query_quest_record_across_buckets(mapping data, string quest_id)
{
    int status;
    string bucket_name;

    if (!mapp(data))
        return 0;
    foreach (status in ({ QUEST_STATUS_COMPLETED, QUEST_STATUS_DELIVERED,
        QUEST_STATUS_FAILED })) {
        bucket_name = call_other(QUEST_TRANSITION_HELPER, "query_status_bucket",
            status);
        if (stringp(bucket_name) && bucket_name != ""
            && mapp(data[bucket_name]) && mapp(data[bucket_name][quest_id]))
            return data[bucket_name][quest_id];
    }
    return 0;
}

/* logout/断线时释放该玩家全部护送租约（目标回 spawn）。
 * 持久 lease 是唯一 owner 记录——遍历 lease 表调统一
 * release（触发方只传 reason），返回结构化释放结果供生命周期聚合。 */
mapping release_player_escort_targets(object player)
{
    mapping leases;
    mapping result;
    mapping compensation_result;
    string quest_id;
    int released;
    int pending;
    int all_ok;

    if (!objectp(player))
        return ([ "ok": 0, "error": "player unavailable" ]);
    compensation_result = recover_player_escort_compensations(player);
    all_ok = compensation_result["ok"];
    pending = compensation_result["pending"];
    leases = player->query("escort/leases");
    if (!mapp(leases))
        return ([ "ok": compensation_result["ok"],
            "state": "no_escort_lease", "released": 0,
            "compensated": compensation_result["recovered"],
            "pending": compensation_result["pending"] ]);
    foreach (quest_id in keys(leases)) {
        if (!mapp(leases[quest_id])
            || leases[quest_id]["player_id"] != query_player_id(player))
            continue;
        result = release_escort_lease(player, quest_id, "logout");
        if (mapp(result) && result["ok"] && result["released"])
            released++;
        else {
            all_ok = 0;
            if (mapp(result) && result["pending"])
                pending++;
        }
    }
    return ([ "ok": all_ok,
        "state": "escort_released", "released": released,
        "compensated": compensation_result["recovered"],
        "pending": pending ]);
}
