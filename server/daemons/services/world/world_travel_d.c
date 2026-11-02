/**
 * @file    world_travel_d.c
 * @brief   登录、方向移动和传送的统一目标解析边界。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

#undef is_player

inherit "/lib/framework/daemon";

nosave mapping _health_status;
nosave mapping _travel_receipts;
nosave int _travel_receipt_sequence;
nosave mapping _move_proposals;
/* L-149：位置回滚失败恢复队列上限（超出丢最旧，避免无界增长）。 */
#define MAX_ROLLBACK_RECOVERY 32
nosave mapping *_rollback_recovery;

void enqueue_move_rollback_recovery(mapping entry, mapping failure);
int recover_pending_move_rollbacks();
int query_pending_move_rollback_count();
nosave int _move_proposal_sequence;

mapping move_transfer(object actor, mapping transition, mapping options);

private mapping failure(string code, string message)
{
    return ([
        "ok": 0,
        "error_code": code || "travel_failed",
        "error": message || "world travel failed",
    ]);
}

private int is_player_actor(object actor)
{
    return objectp(actor) && function_exists("is_player", actor)
        && actor->is_player();
}

int query_location_generation(object actor)
{
    mixed value;

    if (!objectp(actor))
        return 0;
    value = actor->query_temp("world/location_generation");
    return intp(value) && value >= 0 ? value : 0;
}

private string query_actor_id(object actor)
{
    return objectp(actor) ? get_object_id(actor) : "";
}

private int commit_public_room(object actor, string room_path, string operation)
{
    mapping change;
    mapping response;

    if (!is_player_actor(actor) || !stringp(room_path) || room_path == "")
        return 0;
    change = ([ "world/public_room": room_path ]);
    response = PLAYER_STATE_D->commit_state_fields(actor, change,
        PLAYER_STATE_D->build_state_idempotency_key(actor,
            operation || "world/travel_public_room", change));
    return mapp(response) && response["ok"];
}

private int is_world_path(string path)
{
    return stringp(path) && strsrch(path, "/world/") == 0;
}

private mapping validate_region_target(object room, string template_path)
{
    mapping context;

    if (objectp(room) && room->query("dungeon/runtime"))
        return ([ "ok": 1, "scope_key": "instance" ]);
    if (!is_world_path(template_path))
        return ([ "ok": 1, "scope_key": "global/login" ]);
    context = REGION_D->resolve_region_context(template_path);
    if (!mapp(context) || !context["ok"])
        return failure("region_unresolved", "travel target has no registered region");
    return ([ "ok": 1, "scope_key": context["scope_key"],
        "region_id": context["region_id"] ]);
}

private string travel_receipt_key(object actor, mixed target, mapping options)
{
    string requested;
    string actor_id;
    string target_id;

    requested = mapp(options) ? options["idempotency_key"] : "";
    if (!stringp(requested) || requested == "")
        return "";
    actor_id = query_actor_id(actor);
    target_id = objectp(target) ? file_name(target)
        : (stringp(target) ? target : "");
    return actor_id + "|" + target_id + "|" + requested;
}

private void retain_travel_receipt(string key, mapping receipt)
{
    string oldest_key;
    int oldest_sequence;
    string candidate;
    mapping entry;

    if (!stringp(key) || key == "" || !mapp(receipt))
        return;
    _travel_receipt_sequence++;
    receipt["receipt_sequence"] = _travel_receipt_sequence;
    _travel_receipts[key] = receipt;
    if (sizeof(_travel_receipts) <= 2048)
        return;
    oldest_sequence = 0x7fffffff;
    foreach (candidate, entry in _travel_receipts) {
        if (mapp(entry) && intp(entry["receipt_sequence"])
            && entry["receipt_sequence"] < oldest_sequence) {
            oldest_key = candidate;
            oldest_sequence = entry["receipt_sequence"];
        }
    }
    if (oldest_key != "")
        map_delete(_travel_receipts, oldest_key);
}

/* Exact resolution never canonicalizes a clone path. A missing exact object
 * is a hard miss and callers must decide whether recovery is allowed. */
object resolve_exact(string path, mapping context)
{
    object room;

    if (!stringp(path) || path == "")
        return 0;
    room = find_object(path);
    if (!objectp(room)) {
        _health_status["last_error"] = sprintf(
            "resolve_exact: object unavailable path=%s", path);
        _health_status["last_resolution_mode"] = "exact";
        _health_status["last_resolution_path"] = path;
        return 0;
    }
    _health_status["last_resolution_mode"] = "exact";
    _health_status["last_resolution_path"] = path;
    return room;
}

/* Public resolution may load a stable ordinary room. If an actor is inside a
 * dungeon, the same template path is resolved through the active instance;
 * the caller never reaches into the instance registry directly. */
object resolve_public_room(string path, mapping context)
{
    object actor;
    object current;
    object room;
    string dungeon_id;

    if (!stringp(path) || path == "")
        return 0;
    actor = mapp(context) && objectp(context["actor"])
        ? context["actor"] : 0;
    current = objectp(actor) ? environment(actor) : 0;
    if (strsrch(path, "#") != -1)
        return resolve_exact(path, context);
    if (objectp(current) && current->query("dungeon/runtime")) {
        dungeon_id = current->query("dungeon/id") || "";
        room = DUNGEON_INSTANCE_D->query_instance_room(dungeon_id, path);
        if (objectp(room)) {
            _health_status["last_resolution_mode"] = "public_instance";
            _health_status["last_resolution_path"] = path;
            return room;
        }
    }
    room = find_object(path);
    if (!objectp(room))
        catch(room = load_object(path));
    if (!objectp(room))
        _health_status["last_error"] = sprintf(
            "resolve_public_room: object unavailable path=%s", path);
    _health_status["last_resolution_mode"] = "public";
    _health_status["last_resolution_path"] = path;
    return room;
}

mapping validate_dungeon_runtime_target(object actor, object room)
{
    mapping context;
    mapping dungeon;
    object registered_room;
    string actor_id;
    string dungeon_id;
    string template_path;

    if (!objectp(actor) || !objectp(room)
        || !(room->query("dungeon/runtime") || 0))
        return failure("dungeon_target_rejected",
            "travel target is not an active dungeon room");
    dungeon = DUNGEON_D->get_player_dungeon(actor);
    dungeon_id = room->query("dungeon/id") || "";
    if (!mapp(dungeon) || dungeon["state"] != "active"
        || dungeon["id"] != dungeon_id)
        return failure("dungeon_target_rejected",
            "actor is not a member of the target dungeon");
    context = room->query("instance/context");
    if (!mapp(context) || context["dungeon_id"] != dungeon_id
        || context["owner_id"] != dungeon["owner_id"]
        || context["state_scope"] != dungeon["state_scope"]
        || context["instance_scope"] != dungeon["instance_scope"])
        return failure("dungeon_target_rejected",
            "dungeon runtime identity does not match the active run");
    if (dungeon["instance_scope"] == "character") {
        actor_id = query_actor_id(actor);
        if (actor_id == "" || dungeon["owner_id"] != actor_id)
            return failure("dungeon_target_rejected",
                "actor identity does not own the character dungeon run");
    }
    template_path = INSTANCE_ROOM_D->query_template_room_path(room);
    registered_room = DUNGEON_INSTANCE_D->query_instance_room(
        dungeon_id, template_path);
    if (!objectp(registered_room) || registered_room != room)
        return failure("dungeon_target_rejected",
            "dungeon runtime room is not registered for this actor");
    return ([
        "ok": 1,
        "dungeon_id": dungeon_id,
        "template_room": template_path,
        "runtime_room": file_name(room),
    ]);
}

mapping resolve_target(object actor, mixed target, mapping options)
{
    mapping dungeon_target;
    mapping region_validation;
    mapping resolved;
    object room;
    string runtime_path;
    string template_path;

    if (!objectp(actor))
        return failure("invalid_actor", "travel requires an actor");
    if (objectp(target)) {
        template_path = INSTANCE_ROOM_D->query_template_room_path(target);
        if (target->query("dungeon/runtime")) {
            dungeon_target = validate_dungeon_runtime_target(actor, target);
            if (!mapp(dungeon_target) || !dungeon_target["ok"]) {
                _health_status["resolution_failures"]++;
                return dungeon_target;
            }
            room = target;
        } else
            room = target;
    } else if (stringp(target) && target != "") {
        runtime_path = target;
        template_path = runtime_path;
        if (is_world_path(template_path) && strsrch(template_path, "#") == -1
            && !(objectp(environment(actor))
                && environment(actor)->query("dungeon/runtime"))) {
            region_validation = validate_region_target(0, template_path);
            if (!mapp(region_validation) || !region_validation["ok"])
                return region_validation;
        }
        if (mapp(options) && options["resolve_exact"])
            room = resolve_exact(runtime_path, ([
                "actor": actor,
                "reason": options["reason"] || "travel_exact",
            ]));
        else
            room = resolve_public_room(runtime_path, ([
                "actor": actor,
                "reason": mapp(options) ? options["reason"] : "travel_public",
            ]));
        template_path = objectp(room)
            ? INSTANCE_ROOM_D->query_template_room_path(room) : runtime_path;
        if (objectp(room) && room->query("dungeon/runtime")) {
            dungeon_target = validate_dungeon_runtime_target(actor, room);
            if (!mapp(dungeon_target) || !dungeon_target["ok"]) {
                _health_status["resolution_failures"]++;
                return dungeon_target;
            }
        }
    }
    if (!objectp(room)) {
        _health_status["resolution_failures"]++;
        return failure("target_not_found", "travel target could not be resolved");
    }
    resolved = ([
        "ok": 1,
        "template_room": template_path,
        "runtime_room": file_name(room),
        "room": room,
    ]);
    region_validation = validate_region_target(room, template_path);
    if (!mapp(region_validation) || !region_validation["ok"])
        return region_validation;
    resolved += region_validation;
    if (!resolved["ok"]) {
        _health_status["resolution_failures"]++;
        return resolved;
    }
    return resolved;
}

private string infer_travel_direction(object old_room, object room)
{
    mapping exits;
    string direction;
    string target_path;

    if (!objectp(old_room) || !objectp(room))
        return "";
    exits = old_room->query("exits");
    target_path = INSTANCE_ROOM_D->query_template_room_path(room);
    if (!mapp(exits) || !stringp(target_path) || target_path == "")
        return "";
    foreach (direction in keys(exits)) {
        if (stringp(exits[direction]) && exits[direction] == target_path)
            return direction;
    }
    return "";
}

private mapping validate_travel_preflight(object actor, object old_room,
    object room, mapping options)
{
    string direction;

    if (!objectp(actor) || !objectp(room))
        return failure("invalid_travel_target", "travel actor or target is invalid");
    if (old_room == room)
        return ([ "ok": 1, "same_room": 1 ]);
    if (function_exists("can_move", actor) && !actor->can_move(room))
        return failure("move_rejected", "actor cannot enter travel target");
    direction = mapp(options) && stringp(options["direction"])
        ? options["direction"] : "";
    if (direction == "")
        direction = infer_travel_direction(old_room, room);
    if (objectp(old_room) && direction != ""
        && function_exists("valid_leave", old_room)
        && !old_room->valid_leave(actor, direction))
        return failure("leave_rejected", "source room rejected travel");
    if (function_exists("valid_enter", room) && !room->valid_enter(actor))
        return failure("enter_rejected", "destination rejected travel");
    return ([ "ok": 1, "direction": direction, "preflight": 1 ]);
}

private void apply_committed_leave_behavior(object actor, object old_room,
    string direction)
{
    mixed err;

    if (!objectp(actor) || !objectp(old_room) || !stringp(direction)
        || direction == "")
        return;
    err = catch(call_other("/daemons/services/world/room_behavior_d",
        "dispatch_room_leave", actor, old_room, direction));
    if (err)
        log_file("world/travel", sprintf("[%s] leave behavior failed: %O\n",
            ctime(time()), err));
}

mapping validate_travel(object actor, mixed target, mapping options)
{
    mapping resolved;
    mapping preflight;
    object old_room;
    object room;

    old_room = objectp(actor) ? environment(actor) : 0;
    resolved = resolve_target(actor, target, options);
    if (!mapp(resolved) || !resolved["ok"])
        return resolved;
    room = resolved["room"];
    preflight = validate_travel_preflight(actor, old_room, room, options);
    if (!mapp(preflight) || !preflight["ok"])
        return preflight;
    map_delete(resolved, "room");
    resolved += preflight;
    resolved["old_room"] = objectp(old_room) ? file_name(old_room) : "";
    return resolved;
}

mapping move_actor(object actor, mixed target, mapping options)
{
    mapping resolved;
    mapping cached;
    mapping preflight;
    mapping automatic_transition;
    mapping automatic_result;
    object old_room;
    object room;
    string previous_public_room;
    string receipt_key;
    int quiet;
    mixed err;
    int moved;
    int location_generation;
    int expected_generation;

    receipt_key = travel_receipt_key(actor, target, options);
    if (receipt_key != "" && mapp(_travel_receipts[receipt_key])) {
        mapping cached_receipt;
        string receipt_room;
        object receipt_env;

        /* 幂等回执是对“已到达目标房间”的声明，只有玩家确实在回执目标
         * 房间时才可复用。位置代数随登录重置，跨会话会生成相同键；直接
         * 回放旧回执会返回成功而玩家没移动（自动转移会因此卡在出口）。 */
        cached_receipt = _travel_receipts[receipt_key];
        receipt_room = stringp(cached_receipt["template_room"])
            ? cached_receipt["template_room"] : "";
        receipt_env = objectp(actor) ? environment(actor) : 0;
        if (receipt_room != "" && objectp(receipt_env)
            && INSTANCE_ROOM_D->query_template_room_path(receipt_env) == receipt_room)
            return copy(cached_receipt);
        map_delete(_travel_receipts, receipt_key);
    }

    old_room = objectp(actor) ? environment(actor) : 0;
    if (mapp(options) && !undefinedp(options["expected_location_generation"])) {
        expected_generation = options["expected_location_generation"];
        location_generation = query_location_generation(actor);
        if (!intp(expected_generation) || expected_generation != location_generation)
            return failure("stale_location", "travel location generation is stale");
    }
    resolved = resolve_target(actor, target, options);
    if (!mapp(resolved) || !resolved["ok"])
        return resolved;
    room = resolved["room"];
    preflight = validate_travel_preflight(actor, old_room, room, options);
    if (!mapp(preflight) || !preflight["ok"])
        return preflight;
    if (old_room == room) {
        resolved["old_room"] = objectp(old_room) ? file_name(old_room) : "";
        resolved["same_room"] = 1;
        map_delete(resolved, "room");
        if (receipt_key != "")
            retain_travel_receipt(receipt_key, copy(resolved));
        return resolved;
    }

    /* Commit owns the only placement call. The preflight above is deliberately
     * side-effect free; room behavior is dispatched only after placement. */
    quiet = mapp(options) && options["quiet"] ? 1 : 0;
    previous_public_room = is_player_actor(actor)
        ? actor->query("world/public_room") : "";
    err = catch(moved = actor->move_to(room, quiet));
    if (err || !moved || environment(actor) != room) {
        mixed rollback_err;
        int rollback_ok;
        mapping result;

        /* L-26：回滚必须被**校验**，不能假设 move_to 一定成功。回滚后
         * 位置与代数联合确认；未回到原房间即登记恢复并显式上报。 */
        rollback_ok = 1;
        if (objectp(old_room) && environment(actor) != old_room) {
            rollback_err = catch(actor->move_to(old_room, 1));
            rollback_ok = !rollback_err && environment(actor) == old_room;
        }
        if (rollback_ok && objectp(actor)) {
            /* 位置代数推进：客户端据此判定位置已变化并重取投影。 */
            location_generation = query_location_generation(actor);
            actor->set_temp("world/location_generation", location_generation + 1);
        }
        _health_status["move_failures"]++;
        _health_status["last_error"] = sprintf("travel move failed: %O", err);
        result = failure("move_rejected", "travel destination rejected movement");
        result["rollback_ok"] = rollback_ok;
        result["location_generation"] = objectp(actor)
            ? actor->query_temp("world/location_generation") : 0;
        if (!rollback_ok) {
            enqueue_move_rollback_recovery(([
                "actor": actor,
                "from_path": objectp(old_room) ? base_name(old_room) : "",
            ]), ([ "error": sprintf("travel move rollback failed: %O", rollback_err) ]));
            result["recovery_required"] = 1;
            result["recovery_queued"] = 1;
        }
        return result;
    }
    if (is_player_actor(actor) && !room->query("dungeon/runtime")) {
        if (!commit_public_room(actor, resolved["template_room"],
            "world/travel_public_room")) {
            mixed rollback_err;
            int rollback_ok;
            mapping result;

            /* L-26：位置回滚与公共房间回滚都要校验到位，联合确认位置/代数。 */
            rollback_ok = 1;
            if (objectp(old_room) && environment(actor) != old_room) {
                rollback_err = catch(actor->move_to(old_room, 1));
                rollback_ok = !rollback_err && environment(actor) == old_room;
            }
            if (previous_public_room != "")
                catch(commit_public_room(actor, previous_public_room,
                    "world/travel_public_room_rollback"));
            if (rollback_ok && objectp(actor)) {
                location_generation = query_location_generation(actor);
                actor->set_temp("world/location_generation", location_generation + 1);
            }
            _health_status["move_failures"]++;
            _health_status["last_error"] = "public location commit failed";
            result = failure("location_commit_failed",
                "public location could not be committed");
            result["rollback_ok"] = rollback_ok;
            if (!rollback_ok) {
                enqueue_move_rollback_recovery(([
                    "actor": actor,
                    "from_path": objectp(old_room) ? base_name(old_room) : "",
                ]), ([ "error": "public location commit rollback failed" ]));
                result["recovery_required"] = 1;
                result["recovery_queued"] = 1;
            }
            return result;
        }
    }
    location_generation = query_location_generation(actor);
    if (objectp(actor))
        actor->set_temp("world/location_generation", location_generation + 1);
    apply_committed_leave_behavior(actor, old_room,
        mapp(preflight) && stringp(preflight["direction"])
            ? preflight["direction"] : "");

    /* 地图声明的 automatic 转移必须在普通移动进入转移口后执行，
     * 不能只由 route 的 BFS 消费。两镇出口方向相反（中枢镇向北出、
     * 试炼镇向南出），落点是对方镇门而不是对方出口，因此客户端在跨区
     * 快照切换瞬间重放同一方向也只会走回镇内，不需要抑制值。 */
    automatic_transition = WORLD_MAP_D->query_automatic_transition_for_room(
        resolved["template_room"]);
    if (mapp(automatic_transition)) {
        automatic_result = move_transfer(actor, automatic_transition, ([
            "expected_location_generation": location_generation + 1,
            "expected_transfer_generation":
                automatic_transition["transfer_generation"] || 0,
            "idempotency_key": sprintf("automatic-transfer|%s|%d",
                automatic_transition["id"] || "", location_generation + 1),
        ]));
        if (mapp(automatic_result) && automatic_result["ok"]) {
            automatic_result["automatic_trigger"] = 1;
            return automatic_result;
        }
        log_file("world/travel", sprintf(
            "[%s] automatic transfer failed source=%s transition=%O result=%O\n",
            ctime(time()), resolved["template_room"],
            automatic_transition, automatic_result));
        resolved["automatic_transfer"] = automatic_result;
    }

    _health_status["moves"]++;
    resolved["old_room"] = objectp(old_room) ? file_name(old_room) : "";
    resolved["reason"] = mapp(options) && stringp(options["reason"])
        ? options["reason"] : "travel";
    resolved["location_generation"] = location_generation + 1;
    resolved["committed"] = 1;
    map_delete(resolved, "room");
    cached = copy(resolved);
    if (receipt_key != "")
        retain_travel_receipt(receipt_key, cached);
    return resolved;
}

mapping move_transfer(object actor, mapping transition, mapping options)
{
    mapping target_context;
    mapping moved;
    mapping transfer_options;
    object current_room;
    string source_path;
    string target_path;
    string current_path;
    string transfer_direction;
    int expected_transfer_generation;

    if (!objectp(actor) || !mapp(transition))
        return failure("invalid_transfer", "region transfer requires a transition snapshot");
    source_path = stringp(transition["source_path"])
        ? transition["source_path"] : transition["source_room"];
    target_path = stringp(transition["target_path"])
        ? transition["target_path"] : transition["target_room"];
    if (!is_world_path(source_path) || !is_world_path(target_path))
        return failure("invalid_transfer", "transition paths must be stable world paths");
    current_room = environment(actor);
    current_path = objectp(current_room)
        ? INSTANCE_ROOM_D->query_template_room_path(current_room) : "";
    if (current_path != source_path)
        return failure("transfer_source_mismatch",
            "actor is no longer at the transition source");
    if (!undefinedp(transition["source_region"]) && stringp(transition["source_region"])
        && REGION_D->resolve_room_region(source_path) != transition["source_region"])
        return failure("transfer_source_mismatch", "transition source region is invalid");
    target_context = REGION_D->resolve_region_context(target_path);
    if (!mapp(target_context) || !target_context["ok"])
        return failure("region_unresolved", "transfer target region is unavailable");
    if (mapp(options) && !undefinedp(options["expected_transfer_generation"])) {
        expected_transfer_generation = options["expected_transfer_generation"];
        if (!intp(expected_transfer_generation)
            || expected_transfer_generation != transition["transfer_generation"])
            return failure("stale_transfer", "transition generation is stale");
    }
    transfer_options = mapp(options) ? copy(options) : ([]);
    transfer_options["reason"] = "region_transfer";
    transfer_direction = stringp(transition["direction"])
        ? transition["direction"] : "";
    if (transfer_direction == "" && stringp(transition["exit"]))
        transfer_direction = transition["exit"];
    if (transfer_direction != "")
        transfer_options["direction"] = transfer_direction;
    moved = move_actor(actor, target_path, transfer_options);
    if (!mapp(moved) || !moved["ok"])
        return moved;
    moved["transfer_id"] = transition["id"] || "";
    moved["transfer_generation"] = transition["transfer_generation"] || 0;
    moved["source_path"] = source_path;
    moved["target_path"] = target_path;
    moved["arrival_policy"] = transition["arrival_policy"] || "direct_arrival";
    return moved;
}

mapping capture_actor_location(object actor)
{
    object room;
    string current_public_room;
    string template_path;

    if (!is_player_actor(actor) || !objectp(environment(actor)))
        return failure("missing_location", "actor has no stable room");
    room = environment(actor);
    template_path = INSTANCE_ROOM_D->query_template_room_path(room);
    if (room->query("dungeon/runtime"))
        return ([ "ok": 1, "instance": 1,
            "template_room": template_path, "runtime_room": file_name(room) ]);
    current_public_room = actor->query("world/public_room");
    if (current_public_room != template_path) {
        if (!commit_public_room(actor, template_path,
            "world/capture_public_room"))
            return failure("location_commit_failed",
                "public location could not be captured");
        _health_status["public_location_writes"]++;
    }
    return ([ "ok": 1,
        "template_room": template_path, "runtime_room": file_name(room) ]);
}

mapping restore_login_location(object actor)
{
    string room_path;
    string requested_path;
    mapping moved;

    if (!objectp(actor))
        return failure("invalid_actor", "login location restore requires an actor");
    requested_path = actor->query("world/public_room") || "";
    room_path = requested_path;
    if (room_path == "")
        room_path = CONFIG_D->query_start_room();
    moved = move_actor(actor, room_path, ([
        "reason": "public_login_restore",
    ]));
    if (!mapp(moved) || !moved["ok"])
        return moved;
    /* 结构化 reason：区分存档路径恢复与配置出生点回退，供登录日志定位。 */
    moved["source"] = requested_path == ""
        ? "start_room_fallback" : "saved_location";
    moved["requested_path"] = requested_path;
    moved["projection_reason"] = moved["reason"] || "public_login_restore";
    moved["final_room"] = moved["runtime_room"] || "";
    return moved;
}

/* ============ move 域事务 proposal（owner handler 委托入口） ============ */
/* 域事务 payload 必须可序列化（不能携带对象引用）：这里由真源持有对象
 * 句柄，对外只发布 token。owner_domain_handler_d 的 move 域 apply/rollback
 * 通过 token 委托本守护进程执行，事务失败时按 from_path 移回。 */

string register_move_proposal(object actor, mixed target, mapping options)
{
    string token;

    /* L-25：_move_proposals 是 mapping，objectp 恒假导致提案注册永远失败。 */
    if (!objectp(actor))
        return "";
    if (!mapp(_move_proposals))
        _move_proposals = ([]);
    _move_proposal_sequence++;
    token = sprintf("move_proposal/%d", _move_proposal_sequence);
    _move_proposals[token] = ([
        "actor": actor,
        "target": target,
        "options": mapp(options) ? copy(options) : ([]),
        "from_path": objectp(environment(actor))
            ? base_name(environment(actor)) : "",
        "state": "registered",
        "created_at": time(),
    ]);
    return token;
}

mapping apply_move_proposal(string token)
{
    mapping entry;
    mapping moved;

    if (!stringp(token) || token == "" || !mapp(_move_proposals[token]))
        return failure("move_proposal_unknown", "move proposal token is unknown");
    entry = _move_proposals[token];
    if (entry["state"] == "applied")
        return failure("move_proposal_state", "move proposal was already applied");
    moved = move_actor(entry["actor"], entry["target"], entry["options"]);
    if (!mapp(moved) || moved["ok"] != 1) {
        /* move 未发生：proposal 作废，无回滚需求。 */
        map_delete(_move_proposals, token);
        return moved;
    }
    entry["state"] = "applied";
    entry["applied_at"] = time();
    return moved;
}

mapping rollback_move_proposal(string token)
{
    mapping entry;
    mapping moved;

    if (!stringp(token) || token == "" || !mapp(_move_proposals[token]))
        return ([ "ok": 1, "rolled_back": 1, "skipped": 1 ]);
    entry = _move_proposals[token];
    if (entry["state"] != "applied") {
        map_delete(_move_proposals, token);
        return ([ "ok": 1, "rolled_back": 1, "skipped": 1 ]);
    }
    if (!stringp(entry["from_path"]) || entry["from_path"] == "") {
        map_delete(_move_proposals, token);
        return ([ "ok": 0, "rolled_back": 0,
            "error_code": "move_proposal_no_origin",
            "error": "move proposal has no recorded origin room",
            "compensation_required": 1 ]);
    }
    moved = move_actor(entry["actor"], entry["from_path"], ([
        "reason": "owner_domain_move_rollback",
    ]));
    map_delete(_move_proposals, token);
    if (!mapp(moved) || moved["ok"] != 1) {
        /* L-149：回滚失败不能只写日志——进入有界恢复队列，由消费者重试，
         * 调用方据 compensation_required 阻断，健康快照可见待恢复条目。 */
        enqueue_move_rollback_recovery(entry, moved);
        return ([ "ok": 0, "rolled_back": 0,
            "error_code": "move_proposal_rollback_failed",
            "error": "position rollback failed",
            "handler_result": moved, "compensation_required": 1,
            "recovery_queued": 1 ]);
    }
    return ([ "ok": 1, "rolled_back": 1, "compensated": 1,
        "from_path": entry["from_path"] ]);
}

/* L-149：位置回滚失败的恢复队列（有界）。条目保留 actor、目标房间与
 * 失败次数；消费者按原语义重试同一回滚，成功才出队。 */
void enqueue_move_rollback_recovery(mapping entry, mapping failure)
{
    mapping record;
    string key;

    if (!mapp(entry) || !objectp(entry["actor"]))
        return;
    if (!arrayp(_rollback_recovery))
        _rollback_recovery = ({ });
    key = entry["from_path"] || "";
    record = ([
        "actor": entry["actor"],
        "actor_id": get_object_id(entry["actor"]) || "",
        "from_path": key,
        "created_at": time(),
        "attempts": 0,
        "last_error": mapp(failure) && failure["error"]
            ? failure["error"] : "position rollback failed",
    ]);
    _rollback_recovery += ({ record });
    if (sizeof(_rollback_recovery) > MAX_ROLLBACK_RECOVERY)
        _rollback_recovery = _rollback_recovery[
            sizeof(_rollback_recovery) - MAX_ROLLBACK_RECOVERY ..];
    log_file("travel", sprintf("%s move rollback recovery queued for %s -> %s\n",
        ctime(time()), record["actor_id"] || "?", key));
}

int recover_pending_move_rollbacks()
{
    mapping record;
    mapping moved;
    mapping *kept;
    int recovered;

    if (!arrayp(_rollback_recovery) || sizeof(_rollback_recovery) == 0)
        return 0;
    kept = ({ });
    foreach (record in _rollback_recovery) {
        if (!mapp(record) || !objectp(record["actor"])) {
            recovered++;
            continue;
        }
        record["attempts"] = (record["attempts"] || 0) + 1;
        moved = move_actor(record["actor"], record["from_path"],
            ([ "reason": "owner_domain_move_rollback_retry" ]));
        if (mapp(moved) && moved["ok"] == 1)
            recovered++;
        else {
            record["last_error"] = mapp(moved) && moved["error"]
                ? moved["error"] : "position rollback failed";
            kept += ({ record });
        }
    }
    _rollback_recovery = kept;
    return recovered;
}

int query_pending_move_rollback_count()
{
    return arrayp(_rollback_recovery) ? sizeof(_rollback_recovery) : 0;
}

mapping query_travel_contract()
{
    return ([
        "resolve": "resolve_target",
        "preflight": "validate_travel",
        "commit": "move_actor",
        "placement_owner": "WORLD_TRAVEL_D",
        "location_generation": 1,
        "idempotency_receipts": 1,
        "region_fail_closed": 1,
        "public_location_commit": 1,
        "instance_public_location_isolated": 1,
        "region_transfer": "move_transfer",
        "transfer_direct_arrival": 1,
        "move_proposal_delegation": 1,
    ]);
}

mapping query_health_snapshot()
{
    mapping snapshot;

    snapshot = copy(_health_status || ([]));
    snapshot["travel_receipts"] = sizeof(_travel_receipts || ([]));
    snapshot["status"] = snapshot["last_error"] == "" ? "ok" : "degraded";
    return snapshot;
}

void create()
{
    ::create();
    _health_status = ([
        "moves": 0,
        "resolution_failures": 0,
        "move_failures": 0,
        "public_location_writes": 0,
        "last_error": "",
        "last_resolution_mode": "",
        "last_resolution_path": "",
    ]);
    _travel_receipts = ([]);
    _travel_receipt_sequence = 0;
    _move_proposals = ([]);
    _move_proposal_sequence = 0;
}
