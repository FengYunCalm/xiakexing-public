/**
 * @file    client_sync_flush/delivery.c
 * @brief   pending 入队与目标投递判定
 *
 * @par Description:
 * 含 {scheduled_chat_flush_wall_budget_exhausted, store_pending_entry, queue_pending_entry_without_schedule, discard_non_chat_projection, queue_chat_pending_wave_entry_without_schedule, queue_pending_entry, queue_pending_room_snapshot}。
 *
 * 本模块是 `client_sync_flush.c` 继承链的一环，由上层模块继承；
 * 跨模块调用经前向声明 + 运行时动态分派（this_object() 解析到最终对象）。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>
#include <system/xk_protocol.h>

inherit "/daemons/services/system/sync/client_sync_flush/schedule";

#define CHAT_ENTRY_LANE_CHAT 1
#define CHAT_ENTRY_LANE_NORMAL 2
#define CHAT_FLUSH_WALL_BUDGET_NS 8000000
#define CLIENT_SYNC_DROP_PENDING 3

void append_entry_chat_packet_buffer(mapping entry, mixed *packets,
    int packet_count);
void append_entry_immediate_packets();
int can_deliver_to_target();
mapping create_sync_plan();
void discard_non_chat_projection();
void enqueue_pending_chat_key(string key);
int entry_immediate_packet_count(mapping entry);
mixed *entry_immediate_packets(mapping entry);
int entry_is_chat_only(mapping entry);
int entry_projection_is_current(object player, mapping entry);
int has_refresh_work(mapping plan);
mapping merge_sync_plan(mapping base, mapping extra);
mixed *normalize_immediate_packets(mixed packets);
mapping normalize_sync_plan(mapping plan);
int packets_are_only_chat_messages();
int packets_have_chat_messages(mixed packets);
mapping partition_immediate_packets(mixed packets);
int pending_entry_matches_player(string key, mapping entry,
    object player, string session_id);
mapping plan_projection(mapping plan, mapping projection);
string query_object_session_id(object player);
mapping query_pending_entry_service_route();
string query_plan_key();
string query_session_key();
int queue_chat_pending_wave_entry_without_schedule();
int queue_pending_entry();
int queue_pending_entry_without_schedule();
int queue_pending_room_snapshot();
void record_chat_entry_lane_diagnostics();
void record_scheduled_chat_flush_turnover_diagnostics();
void record_undeliverable_delivery_drop();
mapping refresh_plan_projection_for_current(object player, mapping plan);
void remove_pending_chat_entry(string key);
void replace_entry_immediate_packets(mapping entry, mixed packets);
void reset_score_refresh_queue_if_idle();
int retain_entry_chat_for_projection(object player, mapping entry,
    mapping projection);
void schedule_initial_chat_sync_flush();
varargs void schedule_sync_flush();
int scheduled_chat_flush_wall_budget_exhausted();
int score_refresh_entry_matches_player();
void stamp_chat_packet_pending_age();
void store_pending_entry(string key, mapping entry);

int scheduled_chat_flush_wall_budget_exhausted(int wall_elapsed_ns)
{
    if (wall_elapsed_ns < 0 || wall_elapsed_ns >= CHAT_FLUSH_WALL_BUDGET_NS) {
        record_scheduled_chat_flush_turnover_diagnostics(
            "wall_budget_yield");
        return 1;
    }
    return 0;
}

void store_pending_entry(string key, mapping entry)
{
    if (!stringp(key) || key == "" || !mapp(entry))
        return;
    if (entry_is_chat_only(entry)) {
        _pending_chat_sync_plans[key] = entry;
        enqueue_pending_chat_key(key);
        map_delete(_pending_sync_plans, key);
        return;
    }
    _pending_sync_plans[key] = entry;
    remove_pending_chat_entry(key);
}

private int queue_pending_entry_without_schedule(object player, mapping plan,
    mixed packets, int force)
{
    mapping entry;
    string key;
    string lane;
    string session_id;
    mixed *incoming;
    int chat_only;
    int chat_pending_since_ns;
    int plan_has_refresh;
    mapping current_projection;
    mapping partition;

    if (!objectp(player))
        return 0;
    /* 房间快照入队（force=1）绕过投递 gate：不可达窗口也须留存，恢复后重投；
     * 其余路径保持 gate 复查。 */
    if (!force && !can_deliver_to_target(player)) {
        record_undeliverable_delivery_drop(CLIENT_SYNC_DROP_PENDING);
        return 0;
    }
    session_id = query_object_session_id(player);
    key = session_id != "" ? query_session_key(session_id) : file_name(player);
    if (key == "")
        return 0;

    entry = mapp(_pending_sync_plans[key]) ? _pending_sync_plans[key]
        : mapp(_pending_chat_sync_plans[key]) ? _pending_chat_sync_plans[key] : ([]);
    incoming = normalize_immediate_packets(packets);
    current_projection = LOGIN_PROJECTION_D->query_projection_identity(player);
    if (!entry_projection_is_current(player, entry))
        retain_entry_chat_for_projection(player, entry, current_projection);
    plan = normalize_sync_plan(plan);
    if (mapp(plan["projection"])) {
        if (!LOGIN_PROJECTION_D->projection_identity_matches(player,
            plan["projection"])) {
            /*
             * plan 投影过期（玩家对象重建后 advance_projection 推进）：refresh
             * 内容（projection_state/room_snapshot 等）均为服务端现算，刷新投影继续
             * 保留，保证客户端 REAL 同步链不断；immediate 缓存包只保 chat。
             */
            plan = refresh_plan_projection_for_current(player, plan);
            partition = partition_immediate_packets(incoming);
            incoming = partition["chat"];
        }
    } else {
        plan = plan_projection(plan, current_projection);
    }
    plan_has_refresh = has_refresh_work(plan);
    chat_only = entry_is_chat_only(entry)
        && !plan_has_refresh
        && (sizeof(incoming) == 0 || packets_are_only_chat_messages(incoming));
    stamp_chat_packet_pending_age(incoming);
    chat_pending_since_ns = intp(entry["chat_pending_since_ns"])
        ? entry["chat_pending_since_ns"] : 0;
    if (chat_pending_since_ns <= 0 && packets_have_chat_messages(incoming))
        chat_pending_since_ns = perf_counter_ns();
    entry["service_route"] = query_pending_entry_service_route(
        entry, player, session_id);
    entry["player"] = player;
    entry["session_id"] = session_id;
    entry["projection"] = copy(current_projection);
    if (plan_has_refresh)
        entry["plan"] = merge_sync_plan(entry["plan"], plan);
    else if (!mapp(entry["plan"]))
        entry["plan"] = create_sync_plan();
    entry["chat_pending_since_ns"] = chat_pending_since_ns;
    append_entry_immediate_packets(entry, incoming);
    if (!has_refresh_work(entry["plan"])
        && entry_immediate_packet_count(entry) == 0) {
        map_delete(_pending_sync_plans, key);
        remove_pending_chat_entry(key);
        return 1;
    }
    if (chat_only)
        entry["chat_only"] = 1;
    else
        map_delete(entry, "chat_only");
    store_pending_entry(key, entry);
    lane = mapp(_pending_chat_sync_plans[key]) ? "chat" : "normal";
    record_chat_entry_lane_diagnostics(lane, incoming);
    return 1;
}

void discard_non_chat_projection(object player, mapping next_projection)
{
    mapping entry;
    mapping partition;
    mapping seen;
    mapping state;
    mixed *chat;
    string *candidate_keys;
    string destination_key;
    string session_id;
    string object_key;
    string key;

    if (function_exists("reset_owner_read_model_projection", player))
        player->reset_owner_read_model_projection();
    destination_key = query_plan_key(player);
    if (destination_key == "")
        return;
    session_id = query_object_session_id(player);
    object_key = file_name(player);
    candidate_keys = ({ destination_key });
    if (object_key != "" && member_array(object_key, candidate_keys) == -1)
        candidate_keys += ({ object_key });
    if (session_id != ""
        && member_array(query_session_key(session_id), candidate_keys) == -1)
        candidate_keys += ({ query_session_key(session_id) });
    if (mapp(_pending_sync_plans))
        candidate_keys += keys(_pending_sync_plans);
    if (mapp(_pending_chat_sync_plans))
        candidate_keys += keys(_pending_chat_sync_plans);
    seen = ([]);
    chat = ({ });
    foreach (key in candidate_keys) {
        if (seen[key])
            continue;
        seen[key] = 1;
        entry = mapp(_pending_sync_plans[key]) ? _pending_sync_plans[key]
            : mapp(_pending_chat_sync_plans[key]) ? _pending_chat_sync_plans[key] : 0;
        if (!pending_entry_matches_player(key, entry, player, session_id))
            continue;
        if (mapp(entry)) {
            partition = partition_immediate_packets(
                entry_immediate_packets(entry));
            if (arrayp(partition["chat"]))
                chat += partition["chat"];
        }
        map_delete(_pending_sync_plans, key);
        remove_pending_chat_entry(key);
    }
    if (mapp(_pending_score_refreshes))
        foreach (key in keys(_pending_score_refreshes)) {
            state = _pending_score_refreshes[key];
            if (!score_refresh_entry_matches_player(key, state, player,
                session_id))
                continue;
            map_delete(_pending_score_refreshes, key);
        }
    reset_score_refresh_queue_if_idle();
    if (sizeof(chat) == 0)
        return;
    entry = ([
        "player": player,
        "session_id": session_id,
        "projection": mapp(next_projection)
        ? copy(next_projection) : LOGIN_PROJECTION_D->query_projection_identity(player),
    ]);
    entry["plan"] = create_sync_plan();
    entry["chat_only"] = 1;
    replace_entry_immediate_packets(entry, chat);
    entry["chat_only"] = 1;
    store_pending_entry(destination_key, entry);
    schedule_initial_chat_sync_flush();
}

int queue_chat_pending_wave_entry_without_schedule(object player,
    mapping wave_entry, string session_id)
{
    mapping entry;
    mixed *packets;
    string key;
    int chat_pending_since_ns;
    int has_chat_lane;
    int has_normal_lane;
    int lane;
    int packet_count;
    int queued_at;

    if (!objectp(player) || !mapp(wave_entry))
        return 0;
    packets = wave_entry["packets"];
    packet_count = intp(wave_entry["packet_count"])
        ? wave_entry["packet_count"] : 0;
    queued_at = intp(wave_entry["queued_at_ns"])
        ? wave_entry["queued_at_ns"] : 0;
    if (!arrayp(packets) || packet_count <= 0 || packet_count > sizeof(packets))
        return 0;
    key = session_id != "" ? query_session_key(session_id) : file_name(player);
    if (key == "")
        return 0;

    /* Generic lane transitions establish this invariant: a pure CHAT append
     * is lane-stable, while a mixed/normal entry stays normal so its
     * state-before-chat ordering is retained. */
    has_normal_lane = mapp(_pending_sync_plans[key]);
    has_chat_lane = mapp(_pending_chat_sync_plans[key]);
    if (has_normal_lane) {
        entry = _pending_sync_plans[key];
        lane = CHAT_ENTRY_LANE_NORMAL;
    } else if (has_chat_lane) {
        entry = _pending_chat_sync_plans[key];
        lane = CHAT_ENTRY_LANE_CHAT;
    } else {
        entry = ([]);
        lane = CHAT_ENTRY_LANE_CHAT;
    }
    chat_pending_since_ns = intp(entry["chat_pending_since_ns"])
        ? entry["chat_pending_since_ns"] : 0;
    if (chat_pending_since_ns <= 0)
        chat_pending_since_ns = queued_at > 0 ? queued_at : perf_counter_ns();
    if (mapp(entry["service_route"])
        && entry["service_route"]["session_id"] != session_id)
        map_delete(entry, "service_route");
    entry["player"] = player;
    entry["session_id"] = session_id;
    entry["chat_pending_since_ns"] = chat_pending_since_ns;
    if (entry_immediate_packet_count(entry) == 0) {
        entry["immediate"] = packets;
        entry["immediate_count"] = packet_count;
        entry["immediate_chat_count"] = packet_count;
    } else
        append_entry_chat_packet_buffer(entry, packets, packet_count);
    if (lane == CHAT_ENTRY_LANE_CHAT) {
        entry["chat_only"] = 1;
        store_pending_entry(key, entry);
    } else {
        map_delete(entry, "chat_only");
        _pending_sync_plans[key] = entry;
        if (has_chat_lane)
            remove_pending_chat_entry(key);
    }
    return lane;
}

int queue_pending_entry(object player, mapping plan, mixed packets, int delay)
{
    string key;

    if (!queue_pending_entry_without_schedule(player, plan, packets, 0))
        return 0;
    key = query_plan_key(player);
    if (mapp(_pending_chat_sync_plans[key]))
        schedule_initial_chat_sync_flush();
    else
        schedule_sync_flush(delay, 0);
    return 1;
}

/* 登录交接窗口内的事务不能丢失：目标路由可能正在从 login 对象切换到
 * player/session，先以强制入队保留完整事务，下一次 flush 再按会话路由投递。 */
int queue_pending_transaction(object player, mapping tx)
{
    string key;

    if (!objectp(player) || !mapp(tx))
        return 0;
    if (!queue_pending_entry_without_schedule(player, tx["refresh"],
        tx["immediate"], 1))
        return 0;
    key = query_plan_key(player);
    if (mapp(_pending_chat_sync_plans[key]))
        schedule_initial_chat_sync_flush();
    else
        schedule_sync_flush(0, 0);
    return 1;
}

int queue_pending_room_snapshot(object player, mapping plan)
{
    string key;

    if (!objectp(player))
        return 0;
    if (!queue_pending_entry_without_schedule(player, plan, ({ }), 1))
        return 0;
    key = query_plan_key(player);
    if (mapp(_pending_chat_sync_plans[key]))
        schedule_initial_chat_sync_flush();
    else
        schedule_sync_flush(1, 0);
    return 1;
}
