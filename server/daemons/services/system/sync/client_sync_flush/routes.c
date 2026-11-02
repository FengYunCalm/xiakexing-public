/**
 * @file    client_sync_flush/routes.c
 * @brief   客户端同步 owner 路由与 handoff
 *
 * @par Description:
 * 含 {query_client_sync_owner_route, query_pending_entry_service_route, query_session_key, query_object_session_id, query_plan_key, handoff_player_sync, resolve_sync_target, resolve_sync_target_key}。
 * 持有 pending 账本与 score refresh 队列状态（_pending_sync_plans、_pending_chat_sync_plans、_pending_score_refreshes、_score_refresh_queue 系列）；本模块是 handoff 状态汇聚点（会话键迁移需在同一处重排全部账本），上层模块经继承直接访问。
 *
 * 本模块是 `client_sync_flush.c` 继承链的一环，由上层模块继承；
 * 跨模块调用经前向声明 + 运行时动态分派（this_object() 解析到最终对象）。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>
#include <system/xk_protocol.h>

inherit "/daemons/services/system/sync/client_sync_flush/diagnostics";

nosave mapping _pending_sync_plans;
nosave mapping _pending_chat_sync_plans;
nosave mapping _pending_score_refreshes;
nosave mapping _score_refresh_queue;
nosave int _score_refresh_queue_head;
nosave int _score_refresh_queue_tail;

mixed *entry_immediate_packets(mapping entry);
int entry_projection_is_current(object player, mapping entry);
void handoff_player_sync();
int has_refresh_work(mapping plan);
mapping merge_score_refresh_handoff_state(mixed current, mixed incoming);
mapping merge_sync_plan(mapping base, mapping extra);
mapping normalize_score_refresh_state(mixed state);
int packets_have_chat_messages(mixed packets);
mapping query_client_sync_owner_route();
string query_object_session_id(object player);
mapping query_pending_entry_service_route();
string query_plan_key();
string query_session_key();
mapping refresh_plan_projection_for_current(object player, mapping plan);
void remove_pending_chat_entry(string key);
void replace_entry_immediate_packets(mapping entry, mixed packets);
object resolve_sync_target();
object resolve_sync_target_key();
int retain_entry_chat_for_projection(object player, mapping entry,
    mapping projection);
int score_refresh_projection_is_current(object player, mapping state);
void store_pending_entry(string key, mapping entry);

mapping query_client_sync_owner_route(object player)
{
    mapping route;
    string session_id;
    string route_key;
    mixed err;

    if (!objectp(player))
        return ([ "ok": 0, "reason": "player is required" ]);

    session_id = query_object_session_id(player);
    route_key = session_id != "" ? session_id : file_name(player);
    err = catch(route = OWNER_RUNTIME_D->query_service_owner_route("client_sync", route_key));
    if (err || !mapp(route) || route["ok"] != 1)
        return ([
            "ok": 0,
            "service": "client_sync",
            "key": route_key,
            "session_id": session_id,
            "failure_reason": err ? sprintf("owner route query failed: %O", err)
                : (mapp(route) ? route["failure_reason"] : "owner route unavailable"),
            "readiness": mapp(route) ? route["readiness"] : ([ "ok": 0 ]),
            "dependencies": mapp(route) ? route["dependencies"] : ([ ]),
            "direct_cross_owner_write": 0,
        ]);

    route["session_id"] = session_id;
    route["owner_output_boundary"] = 1;
    route["direct_cross_owner_write"] = 0;
    return route;
}

mapping query_pending_entry_service_route(mapping entry, object player,
    string session_id)
{
    mapping route;

    route = mapp(entry) && mapp(entry["service_route"])
        ? entry["service_route"] : 0;
    if (mapp(route) && route["session_id"] == session_id
        && (session_id != "" || entry["player"] == player)
        && route["owner_output_boundary"] == 1
        && route["direct_cross_owner_write"] == 0)
        return route;

    return query_client_sync_owner_route(player);
}

string query_session_key(string session_id)
{
    if (!stringp(session_id) || session_id == "")
        return "";

    return "session:" + session_id;
}

string query_object_session_id(object player)
{
    if (!objectp(player))
        return "";

    return GATEWAY_SESSION_D->query_object_session_id(player) || "";
}

string query_plan_key(object player)
{
    string session_id;

    if (!objectp(player))
        return "";

    session_id = query_object_session_id(player);
    if (session_id != "")
        return query_session_key(session_id);

    return file_name(player);
}

void handoff_player_sync(object player)
{
    mapping current_projection;
    mapping old_entry;
    mapping new_entry;
    mapping merged_plan;
    string old_key;
    string new_key;
    string session_id;
    mixed *immediate;
    int chat_pending_since_ns;
    int old_chat_pending_since_ns;
    mapping old_score_refresh_state;

    if (!objectp(player))
        return;

    session_id = query_object_session_id(player);
    if (!stringp(session_id) || session_id == "")
        return;

    old_key = file_name(player);
    new_key = query_session_key(session_id);
    if (old_key == "" || new_key == "" || old_key == new_key)
        return;

    current_projection = LOGIN_PROJECTION_D->query_projection_identity(player);

    if (mapp(_pending_score_refreshes)
        && _pending_score_refreshes[new_key]
        && !score_refresh_projection_is_current(player,
            _pending_score_refreshes[new_key]))
        map_delete(_pending_score_refreshes, new_key);

    if (mapp(_pending_score_refreshes)
        && _pending_score_refreshes[old_key]) {
        old_score_refresh_state = normalize_score_refresh_state(
            _pending_score_refreshes[old_key]);
        if (!score_refresh_projection_is_current(player, old_score_refresh_state))
            map_delete(_pending_score_refreshes, old_key);
        else if (!_pending_score_refreshes[new_key]) {
            old_score_refresh_state["projection"] = copy(current_projection);
            old_score_refresh_state["player"] = player;
            old_score_refresh_state["session_id"] = session_id;
            _pending_score_refreshes[new_key] = old_score_refresh_state;
            if (!mapp(_score_refresh_queue)) {
                _score_refresh_queue = ([]);
                _score_refresh_queue_head = 0;
                _score_refresh_queue_tail = 0;
            }
            _score_refresh_queue[_score_refresh_queue_tail++] = new_key;
        } else if (score_refresh_projection_is_current(player,
            _pending_score_refreshes[new_key])) {
            _pending_score_refreshes[new_key] = merge_score_refresh_handoff_state(
                _pending_score_refreshes[new_key], old_score_refresh_state);
            _pending_score_refreshes[new_key]["player"] = player;
            _pending_score_refreshes[new_key]["session_id"] = session_id;
        } else {
            map_delete(_pending_score_refreshes, new_key);
            _pending_score_refreshes[new_key] = ([
                "queued_at_ns": old_score_refresh_state["queued_at_ns"],
                "pressure_deferrals": old_score_refresh_state["pressure_deferrals"],
                "projection": copy(current_projection),
                "player": player,
                "session_id": session_id,
            ]);
        }
        map_delete(_pending_score_refreshes, old_key);
    }

    old_entry = mapp(_pending_sync_plans[old_key]) ? _pending_sync_plans[old_key]
        : mapp(_pending_chat_sync_plans[old_key]) ? _pending_chat_sync_plans[old_key] : 0;
    if (!mapp(old_entry))
        return;

    new_entry = mapp(_pending_sync_plans[new_key]) ? _pending_sync_plans[new_key]
        : mapp(_pending_chat_sync_plans[new_key]) ? _pending_chat_sync_plans[new_key] : ([]);
    /*
     * handoff（玩家对象重建/会话迁移）时，旧 entry 的 plan 可能含
     * projection_state/room_snapshot：若因投影过期被 retain_entry_chat_for_projection
     * 清空，客户端将收不到新 REAL，gate 以旧投影拒绝后续房间包。
     * 正确做法：plan 内容保留并刷新投影后合并；immediate 才按投影过滤 chat。
     * 注意：只刷新 entry["plan"]["projection"]，顶层 entry["projection"]
     * 保持旧值，retain 分支靠顶层旧值触发；刷新顶层会让 chat 保留静默失效。
     */
    if (!entry_projection_is_current(player, old_entry))
        old_entry["plan"] = refresh_plan_projection_for_current(player,
            mapp(old_entry["plan"]) ? old_entry["plan"] : ([]));
    if (!entry_projection_is_current(player, new_entry))
        new_entry["plan"] = refresh_plan_projection_for_current(player,
            mapp(new_entry["plan"]) ? new_entry["plan"] : ([]));
    merged_plan = merge_sync_plan(new_entry["plan"], old_entry["plan"]);
    if (!entry_projection_is_current(player, old_entry))
        retain_entry_chat_for_projection(player, old_entry,
            current_projection);
    if (!entry_projection_is_current(player, new_entry))
        retain_entry_chat_for_projection(player, new_entry,
            current_projection);
    /* L-181：旧对象键（handoff 前积压）在前，新会话键（重连窗口内入队）
     * 在后；chat 只按数组顺序发送，反转会造成“新在前、旧在后”的阅读错乱。 */
    immediate = entry_immediate_packets(old_entry) +
        entry_immediate_packets(new_entry);
    chat_pending_since_ns = intp(new_entry["chat_pending_since_ns"])
        ? new_entry["chat_pending_since_ns"] : 0;
    old_chat_pending_since_ns = intp(old_entry["chat_pending_since_ns"])
        ? old_entry["chat_pending_since_ns"] : 0;
    if (old_chat_pending_since_ns > 0
        && (chat_pending_since_ns <= 0
            || old_chat_pending_since_ns < chat_pending_since_ns))
        chat_pending_since_ns = old_chat_pending_since_ns;
    if (chat_pending_since_ns <= 0 && packets_have_chat_messages(immediate))
        chat_pending_since_ns = perf_counter_ns();

    new_entry = ([
        "player": player,
        "session_id": session_id,
        "plan": merged_plan,
        "projection": copy(current_projection),
        "chat_pending_since_ns": chat_pending_since_ns,
    ]);
    replace_entry_immediate_packets(new_entry, immediate);
    map_delete(_pending_sync_plans, old_key);
    remove_pending_chat_entry(old_key);
    if (!has_refresh_work(merged_plan) && sizeof(immediate) == 0) {
        map_delete(_pending_sync_plans, new_key);
        remove_pending_chat_entry(new_key);
        return;
    }
    store_pending_entry(new_key, new_entry);
}

object resolve_sync_target(object player)
{
    string session_id;
    object target;

    if (objectp(player) && interactive(player))
        return player;

    session_id = query_object_session_id(player);
    if (session_id == "")
        return player;

    target = GATEWAY_SESSION_D->query_session_target(session_id);
    if (objectp(target))
        return target;

    return player;
}

object resolve_sync_target_key(string key)
{
    string session_id;
    object target;

    if (!stringp(key) || key == "")
        return 0;

    if (sscanf(key, "session:%s", session_id) == 1 && session_id != "") {
        target = GATEWAY_SESSION_D->query_session_target(session_id);
        return objectp(target) ? target : 0;
    }

    target = find_object(key);
    return objectp(target) ? target : 0;
}
