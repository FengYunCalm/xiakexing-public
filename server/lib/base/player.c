/**
 * @file    player.c
 * @brief   玩家角色对象



 *
 * @par Description:
 * 玩家角色对象，在 `living.c` 之上补充会话、持久化、初始化与别名能力。
 */

#pragma strict_types

#include <system/globals.h>
#include <system/daemon.h>
#include <system/services.h>
#include <system/ansi.h>
#include <framework/heartbeat_module.h>

inherit "/lib/base/living";
inherit "/lib/base/player_session";
inherit "/lib/base/player_persistence";
inherit "/lib/base/player_init";
inherit "/lib/base/player_runtime";
inherit "/lib/base/player_lifecycle";
inherit "/lib/base/player_alias";

int set_temp(string path, mixed value);
mixed query_temp(string path);
int delete_temp(string path);
mixed query(string path);
int set(string path, mixed value);
void start_busy(int duration);
void interrupt_busy();
void on_die(object killer);
void wake_up_from_unconscious();
void set_leader(object leader);
object query_leader();
mapping query_follow_snapshot();
private object *query_followers();
private string resolve_follow_direction(object old_env, object new_env);
private string query_follow_guard_reason(object follower, object new_env);
private void stop_following(object leader, object follower, string reason);
mapping refresh_player_owner_route(string reason);
void prepare_player_persistence_snapshot(object player);
mapping restore_player_inventory_snapshot(object player);
void on_move_complete(object old_env, object new_env);
private void record_move_completion_phase(string phase, int wall_started_at,
    int cpu_started_at);

#define UNCONSCIOUS_DURATION_SECONDS  20

nosave string _account_id;
nosave int _login_time;
nosave int _last_command_time;  // 最后命令时间

private void record_move_completion_phase(string phase, int wall_started_at,
    int cpu_started_at)
{
    int cpu_finished_at;

    cpu_finished_at = thread_cpu_time_ns();
    GATEWAY_TELEMETRY_D->record_gateway_movement_phase_timing(
        this_object(), phase, perf_counter_ns() - wall_started_at,
        cpu_started_at >= 0 && cpu_finished_at >= cpu_started_at
            ? cpu_finished_at - cpu_started_at : -1);
}
nosave int _net_dead;
nosave int _login_fail_count;
nosave int _locked;

#undef is_player
#undef is_living
#undef is_npc
#undef is_character

private string query_stable_id(object actor)
{
    string player_id;

    if (!objectp(actor))
        return "unknown";
    player_id = get_object_id(actor);
    return stringp(player_id) && player_id != "" ? player_id : file_name(actor);
}

private string query_display_identity(object actor)
{
    mixed name;

    if (!objectp(actor))
        return "对方";
    name = actor->query("name");
    return stringp(name) && name != "" ? name : query_stable_id(actor);
}

/*
 * 玩家对象在能力判断上应与 NPC 保持同一语义：
 * 交互层统一通过 is_character() 判断“可被当作角色处理的对象”，
 * 避免命令层再分叉出一套只识别 player/NPC 的条件树。
 */
int is_character()
{
    return 1;
}

mapping query_data_schema_contract()
{
    return PLAYER_STATE_D->query_state_contract();
}

int validate_data_snapshot(mapping data)
{
    return PLAYER_STATE_D->validate_state_snapshot(data);
}

mapping migrate_data_snapshot(mapping data, int from_version)
{
    return PLAYER_STATE_D->migrate_state_snapshot(data, from_version);
}

mapping query_player_owner_route()
{
    mapping route;
    string player_id;
    mixed err;

    player_id = query_stable_id(this_object());
    if (!stringp(player_id) || player_id == "")
        player_id = geteuid(this_object()) || file_name(this_object());

    err = catch(route = OWNER_RUNTIME_D->query_service_owner_route("player", player_id));
    if (err || !mapp(route) || route["ok"] != 1)
        return ([
            "ok": 0,
            "service": "player",
            "key": player_id,
            "player_id": player_id,
            "failure_reason": err ? sprintf("owner route query failed: %O", err)
                : (mapp(route) ? route["failure_reason"] : "owner route unavailable"),
            "readiness": mapp(route) ? route["readiness"] : ([ "ok": 0 ]),
            "dependencies": mapp(route) ? route["dependencies"] : ([ ]),
            "direct_cross_owner_write": 0,
        ]);

    route["player_id"] = player_id;
    route["same_owner_direct_only"] = 1;
    route["direct_cross_owner_write"] = 0;
    return route;
}

mapping refresh_player_owner_route(string reason)
{
    mapping route;

    route = query_player_owner_route();
    set_temp("owner/player_route", route);
    set_temp("owner/player_route_reason", stringp(reason) ? reason : "");
    return route;
}

void setup()
{
    string my_id;

    seteuid(getuid(this_object()));
    my_id = init_identity();
    init_attributes(my_id);
    init_combat_stats();
    init_skills();
    if (!geteuid()) {
        seteuid(my_id);
    }
    set_temp("player/login_time", time());
    set_temp("player/last_command_time", time());
    refresh_player_owner_route("setup");
    set_heartbeat_batch_name(HEARTBEAT_BATCH_PLAYER);
    set_heartbeat_profile(HEARTBEAT_PROFILE_PLAYER, ([
        "batch": HEARTBEAT_BATCH_PLAYER,
        "interval": 1,
    ]));
    register_batched_heartbeat("buffs", "update_buffs", 1);
    register_batched_heartbeat("skill_passives", "update_skill_passives", 1);
    enable_commands();
    /* Setup/restore may legitimately seed state before the player is online.
     * Once the runtime owner is bound, the object facade upgrades managed
     * writes to PLAYER_STATE_D owner transactions. */
    set_temp("player/state_owner_ready", 1);
}

int save()
{
    if (objectp(environment(this_object())))
        WORLD_TRAVEL_D->capture_actor_location(this_object());
    /* 背包恢复未完成时保留待恢复条目快照，不用部分运行态覆写存档。 */
    if (!query("persistence/inventory_restore_pending"))
        prepare_player_persistence_snapshot(this_object());
    return ::save();
}

int restore()
{
    int ok;

    ok = ::restore();
    if (ok)
        restore_player_inventory_snapshot(this_object());
    return ok;
}

void net_dead()
{
    mapping result;

    /* 登出生命周期统一收口——handler、保存、销毁及失败重试均由
     * PLAYER_LIFECYCLE_D 负责，player 对象不再维护第二套清理状态机。 */
    set_net_dead(1);
    result = PLAYER_LIFECYCLE_D->terminate_player(this_object(), "net_dead");
    if (!mapp(result) || !result["ok"])
        log_file("system/error", sprintf("[%s] player termination pending for %s: %O\n",
            ctime(time()), query_stable_id(this_object()), result));
}

private string resolve_follow_direction(object old_env, object new_env)
{
    mapping exits;
    string dir;
    string dest_path;
    string room_path;

    if (!objectp(old_env) || !objectp(new_env))
        return 0;

    if (function_exists("query_exits", old_env))
        exits = old_env->query_exits();
    if (!mapp(exits))
        exits = old_env->query("exits");
    if (!mapp(exits))
        return 0;

    room_path = base_name(new_env);
    foreach (dir, dest_path in exits) {
        if (!stringp(dest_path) || dest_path == "")
            continue;
        if (dest_path == room_path)
            return dir;
    }

    return 0;
}

private void stop_following(object leader, object follower, string reason)
{
    string leader_name;

    if (!objectp(follower))
        return;

    follower->set_leader(0);

    if (!stringp(reason) || reason == "") {
        leader_name = query_display_identity(leader);
        reason = HIC "你没能继续跟上" + leader_name + "，跟随中断。" NOR "\n";
    }

    msg_send(follower, reason);
}

private string query_follow_guard_reason(object follower, object new_env)
{
    mapping dungeon;
    mapping access;
    string dungeon_id;
    string follower_id;
    string property_id;

    if (!objectp(follower) || !objectp(new_env))
        return HIC "你没能继续跟上对方，跟随中断。" NOR "\n";

    if (function_exists("valid_enter", new_env) && !new_env->valid_enter(follower))
        return HIC "对方进入的地方不允许你直接跟随，跟随中断。" NOR "\n";

    property_id = function_exists("query_housing_property_id", new_env)
        ? new_env->query_housing_property_id()
        : (new_env->query("housing/property_id") || "");
    if (stringp(property_id) && property_id != "") {
        follower_id = query_stable_id(follower);
        access = HOUSING_D->query_access_snapshot_by_id(follower_id, property_id);
        if (!mapp(access) || !access["ok"] || !access["can_access"]) {
            return HIC "这处房屋需要单独验证进入权限，你没能继续跟上，跟随中断。" NOR "\n";
        }
    }

    dungeon_id = new_env->query("dungeon/id") || "";
    if (stringp(dungeon_id) && dungeon_id != "") {
        dungeon = DUNGEON_D->get_player_dungeon(follower);
        if (!mapp(dungeon) || (dungeon["id"] || "") != dungeon_id)
            return HIC "副本实例需要通过正式入口进入，你没能继续跟上，跟随中断。" NOR "\n";
    }

    return "";
}

void on_move_complete(object old_env, object new_env)
{
    object *followers;
    object follower;
    string follow_dir;
    string follow_guard_reason;
    int bootstrap_room_sync;
    int move_quest_updated;
    mixed refresh_err;
    mixed move_quest_err;
    mixed dungeon_err;
    mapping follow_move;
    int phase_started_at;
    int phase_cpu_started_at;
    int total_started_at;
    int total_cpu_started_at;

    if (!objectp(new_env))
        return;

    total_started_at = perf_counter_ns();
    total_cpu_started_at = thread_cpu_time_ns();

    bootstrap_room_sync = query_temp("login/bootstrap_room_sync") ? 1 : 0;

    if (!bootstrap_room_sync) {
        phase_started_at = perf_counter_ns();
        phase_cpu_started_at = thread_cpu_time_ns();
        refresh_err = catch(call_other("/lib/std/utils/util_view", "refresh_room_entities", new_env));
        if (refresh_err) {
            log_file("system/session", sprintf("[%s] on_move_complete: room refresh failed for %s -> %s: %O\n",
                ctime(time()), query_stable_id(this_object()), file_name(new_env), refresh_err));
        }
        record_move_completion_phase("completion_entities", phase_started_at,
            phase_cpu_started_at);
    }

    if (!bootstrap_room_sync && objectp(old_env)) {
        phase_started_at = perf_counter_ns();
        phase_cpu_started_at = thread_cpu_time_ns();
        WORLD_OBSERVER_D->sync_player_room_transition(this_object(), old_env, new_env);
        record_move_completion_phase("completion_world", phase_started_at,
            phase_cpu_started_at);
    }

    if (!bootstrap_room_sync && objectp(old_env)) {
        phase_started_at = perf_counter_ns();
        phase_cpu_started_at = thread_cpu_time_ns();
        move_quest_updated = 0;
        move_quest_err = catch(move_quest_updated = QUEST_D->record_move_progress(
            this_object(), old_env, new_env));
        if (move_quest_err) {
            log_file("system/session", sprintf("[%s] on_move_complete: quest move progress failed for %s -> %s: %O\n",
                ctime(time()), query_stable_id(this_object()), file_name(new_env), move_quest_err));
        }
        record_move_completion_phase("completion_quest", phase_started_at,
            phase_cpu_started_at);
    }

    if (!bootstrap_room_sync) {
        phase_started_at = perf_counter_ns();
        phase_cpu_started_at = thread_cpu_time_ns();
        dungeon_err = catch(DUNGEON_D->track_player_room(this_object(), new_env));
        if (dungeon_err) {
            log_file("system/session", sprintf("[%s] on_move_complete: dungeon room track failed for %s -> %s: %O\n",
                ctime(time()), query_stable_id(this_object()), file_name(new_env), dungeon_err));
        }
        record_move_completion_phase("completion_dungeon", phase_started_at,
            phase_cpu_started_at);
    }

    /*
     * 移动事务在全部房间阶段（实体补刷、投影重建、护送/到达进度、副本、同伴）
     * 结束后只发送一次带完整四段的收敛快照；导航快照不再前置发送，避免客户端
     * 把导航子包当成完整换房快照而清空实体。发送 helper 统一携带
     * snapshot_kind/room_id/投影身份/房间快照 revision。
     */
    if (!bootstrap_room_sync) {
        phase_started_at = perf_counter_ns();
        phase_cpu_started_at = thread_cpu_time_ns();
        call_other("/kernel/simul_efun", "xk_emit_room_snapshot",
            this_object(), new_env);
        record_move_completion_phase("completion_snapshot", phase_started_at,
            phase_cpu_started_at);
    }

    if (!objectp(old_env)) {
        record_move_completion_phase("completion_total", total_started_at,
            total_cpu_started_at);
        return;
    }

    phase_started_at = perf_counter_ns();
    phase_cpu_started_at = thread_cpu_time_ns();
    follow_dir = resolve_follow_direction(old_env, new_env);

    followers = query_followers();
    foreach (follower in followers) {
        if (follower->query_leader() != this_object())
            continue;
        if (environment(follower) != old_env)
            continue;
        if ((follower->query("combat/hp") || 0) <= 0 || follower->query_temp("is_dying"))
            continue;

        if (!stringp(follow_dir) || follow_dir == "") {
            stop_following(this_object(), follower,
                HIC "对方不是通过正常出口离开的，你没能继续跟上，跟随中断。" NOR "\n");
            continue;
        }

        if (follower->is_fighting()) {
            stop_following(this_object(), follower,
                HIC "你正陷于战斗，没能继续跟上对方，跟随中断。" NOR "\n");
            continue;
        }

        follow_guard_reason = query_follow_guard_reason(follower, new_env);
        if (follow_guard_reason != "") {
            stop_following(this_object(), follower, follow_guard_reason);
            continue;
        }

        follow_move = WORLD_TRAVEL_D->move_actor(follower, new_env, ([
            "reason": "follow",
            "direction": follow_dir,
            "expected_location_generation":
                follower->query_temp("world/location_generation") || 0,
        ]));
        if (!mapp(follow_move) || !follow_move["ok"]) {
            stop_following(this_object(), follower, 0);
            continue;
        }

        msg_send(follower, HIC "你跟着" + query_display_identity(this_object()) + "移动了。" NOR "\n");
    }
    record_move_completion_phase("completion_followers", phase_started_at,
        phase_cpu_started_at);
    record_move_completion_phase("completion_total", total_started_at,
        total_cpu_started_at);
}

void set_leader(object leader)
{
    mixed followers_raw;
    object old_leader;
    object *followers;

    old_leader = query_temp("follow/leader");
    if (objectp(old_leader)) {
        int index;

        followers_raw = old_leader->query_temp("follow/followers");
        if (arrayp(followers_raw))
            followers = followers_raw;
        else
            followers = ({ });
        index = member_array(this_object(), followers);
        if (index != -1)
            followers = followers[0..index-1] + followers[index+1..];
        old_leader->set_temp("follow/followers", followers);
    }

    if (objectp(leader)) {
        set_temp("follow/leader", leader);
        followers_raw = leader->query_temp("follow/followers");
        if (arrayp(followers_raw))
            followers = followers_raw;
        else
            followers = ({ });
        if (member_array(this_object(), followers) == -1)
            followers += ({ this_object() });
        leader->set_temp("follow/followers", followers);
    } else {
        delete_temp("follow/leader");
    }
}

object query_leader()
{
    object leader;

    leader = query_temp("follow/leader");
    return objectp(leader) ? leader : 0;
}

mapping query_follow_snapshot()
{
    object leader;
    object follower;
    object *followers;
    string *follower_ids;
    string player_id;

    leader = query_leader();
    followers = query_followers();
    follower_ids = ({ });

    foreach (follower in followers) {
        if (!objectp(follower))
            continue;
        player_id = query_stable_id(follower);
        if (!stringp(player_id) || player_id == "")
            player_id = file_name(follower);
        follower_ids += ({ player_id });
    }

    return ([
        "leader_id": objectp(leader) ? query_stable_id(leader) : "",
        "leader_name": objectp(leader) ? query_display_identity(leader) : "",
        "follower_ids": follower_ids,
        "follower_count": sizeof(follower_ids),
    ]);
}

private object *query_followers()
{
    mixed followers;

    followers = query_temp("follow/followers");
    if (!arrayp(followers))
        return ({ });

    return filter_array(followers, (: objectp($1) :));
}

void on_die(object killer)
{
    mapping budget;
    int duration;
    int timer_handle;
    string timer_mode;
    object room;

    budget = NUMERIC_RULE_D->query_combat_survival_budget_report(this_object(), ([
        "event": "player_down",
        "kill_mode": objectp(killer) && function_exists("is_player", killer) && call_other(killer, "is_player") ? 1 : 0,
    ]));
    duration = mapp(budget) ? (budget["unconscious_duration_seconds"] || UNCONSCIOUS_DURATION_SECONDS) : UNCONSCIOUS_DURATION_SECONDS;
    if (duration < 1)
        duration = UNCONSCIOUS_DURATION_SECONDS;
    set_temp("combat/survival_budget", budget);

    timer_handle = query_temp("player/unconscious_timer");
    timer_mode = query_temp("player/unconscious_timer_mode");
    if (intp(timer_handle) && timer_handle > 0) {
        if (timer_mode == "local_fallback")
            remove_call_out(timer_handle);
        else
            TIMER_D->cancel_timer(timer_handle);
    }

    set_temp("is_dying", 1);
    if (!mapp(budget) || budget["command_blocked_while_down"]) {
        set_temp("no_command", 1);
        set_temp("command_blocked", 1);
    }
    start_busy((duration + 1) / 2);

    msg_send(this_object(), HIR "你眼前一黑，失去了知觉。" NOR "\n");
    if (!mapp(budget) || budget["notify_dungeon_state"])
        DUNGEON_D->notify_player_down(this_object(), killer);
    room = environment(this_object());
    if (objectp(room))
        CLIENT_SYNC_D->request_status_room_snapshot(this_object(), room, 0);
    else
        send_xk_status(this_object(), 0);

    timer_mode = "owner_timer";
    timer_handle = TIMER_D->call_later("wake_up_from_unconscious", duration, this_object());
    if (!intp(timer_handle) || timer_handle <= 0) {
        /* owner runtime 未就绪时只降级到玩家对象自己的 call_out，不放宽跨 owner 路由。 */
        timer_handle = call_out("wake_up_from_unconscious", duration);
        timer_mode = "local_fallback";
    }
    if (!intp(timer_handle) || timer_handle <= 0) {
        set_temp("player/unconscious_timer", 0);
        set_temp("player/unconscious_timer_mode", "unavailable");
        msg_send(this_object(), "唤醒定时器当前不可用，请稍后由系统恢复。\n");
        return;
    }
    set_temp("player/unconscious_timer", timer_handle);
    set_temp("player/unconscious_timer_mode", timer_mode);
}

void wake_up_from_unconscious()
{
    mapping budget;
    int max_hp;
    int recover_hp;
    int recover_hp_bp;
    object room;

    budget = NUMERIC_RULE_D->query_combat_survival_budget_report(this_object(), ([
        "event": "wake_up",
    ]));
    recover_hp_bp = mapp(budget) ? (budget["wake_recover_hp_bp"] || 1000) : 1000;

    max_hp = query("combat/max_hp");
    if (!intp(max_hp) || max_hp <= 0)
        max_hp = 100;

    recover_hp = max_hp * recover_hp_bp / 10000;
    if (recover_hp_bp > 0 && recover_hp < 1)
        recover_hp = 1;

    set("combat/hp", recover_hp);
    delete_temp("is_dying");
    delete_temp("no_command");
    delete_temp("command_blocked");
    delete_temp("player/unconscious_timer");
    delete_temp("player/unconscious_timer_mode");
    delete_temp("combat/survival_budget");
    interrupt_busy();

    room = environment(this_object());
    if (objectp(room))
        ROOM_VIEW_D->invalidate_room_view_content(room);

    msg_send(this_object(), HIY "你悠悠醒来，勉强恢复了行动能力。" NOR "\n");
    if (!mapp(budget) || budget["notify_dungeon_state"])
        DUNGEON_D->notify_player_recovered(this_object());
    if (objectp(room))
        CLIENT_SYNC_D->request_status_room_snapshot(this_object(), room, 0);
    else
        send_xk_status(this_object(), 0);
}

void update_skill_passives()
{
    SKILL_BEHAVIOR_D->update_player_skill_passives(this_object());
}
