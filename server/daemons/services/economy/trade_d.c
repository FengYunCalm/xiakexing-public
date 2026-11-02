/**
 * @file    trade_d.c
 * @brief   玩家交易守护进程,管理交易会话、玩家会话索引与序列。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>
#include <system/xk_protocol.h>

inherit "/lib/framework/daemon";

nosave mapping _sessions;
nosave mapping _player_sessions;
nosave int _trade_seq;
nosave int _owner_operation_seq;

varargs void xk_send_trade_result_packet(object me, string action,
    string item_name, int price, int success, mapping extra_payload,
    mapping response_context);

private string ensure_player_id(object player)
{
    string player_id;

    if (!objectp(player))
        return 0;

    if (player->query("trade_proxy") == 1) {
        player_id = player->query("trade_proxy_id");
        if (stringp(player_id) && player_id != "")
            return player_id;
    }
    player_id = get_object_id(player);
    return stringp(player_id) && player_id != "" ? player_id : 0;
}

private string query_player_display_name(object player)
{
    string name;

    if (!objectp(player))
        return "对方";

    name = player->query_name() || player->query("name") || ensure_player_id(player) || "对方";
    return name;
}

private mapping ensure_session(string session_id)
{
    if (!stringp(session_id) || !mapp(_sessions[session_id]))
        return 0;
    return _sessions[session_id];
}

private void clear_session_indexes(mapping session)
{
    if (!mapp(session))
        return;

    map_delete(_player_sessions, session["left_id"] || "");
    map_delete(_player_sessions, session["right_id"] || "");
}

private void cancel_session_timer(mapping session)
{
    int timer_handle;
    string timer_mode;

    if (!mapp(session))
        return;

    timer_handle = session["timer_handle"] || 0;
    timer_mode = session["timer_mode"] || "owner_timer";
    if (intp(timer_handle) && timer_handle > 0) {
        if (timer_mode == "local_fallback")
            remove_call_out(timer_handle);
        else
            TIMER_D->cancel_timer(timer_handle);
    }
    session["timer_handle"] = 0;
    session["timer_mode"] = "";
}

private void reset_session_confirm(mapping session)
{
    mapping offers;

    if (!mapp(session) || !mapp(session["offers"]))
        return;

    offers = session["offers"];
    if (mapp(offers[session["left_id"]]))
        offers[session["left_id"]]["confirmed"] = 0;
    if (mapp(offers[session["right_id"]]))
        offers[session["right_id"]]["confirmed"] = 0;
}

private void remove_session(mapping session)
{
    string session_id;

    if (!mapp(session))
        return;

    cancel_session_timer(session);
    clear_session_indexes(session);
    session_id = session["id"] || "";
    if (session_id != "")
        map_delete(_sessions, session_id);
}

private void notify_trade_pair(mapping session, string left_msg, string right_msg)
{
    object left;
    object right;

    if (!mapp(session))
        return;

    left = session["left_player"];
    right = session["right_player"];
    if (objectp(left) && (userp(left) || interactive(left))
        && stringp(left_msg) && left_msg != "")
        msg_send(left, left_msg);
    if (objectp(right) && (userp(right) || interactive(right))
        && stringp(right_msg) && right_msg != "")
        msg_send(right, right_msg);
}

private string query_trade_pair_error(object left, object right)
{
    int min_skill_cap;

    if (!objectp(left) || !objectp(right) || left == right) {
        return "交易对象无效。\n";
    }
    if (environment(left) != environment(right)) {
        return "双方必须在同一地点才能交易。\n";
    }
    min_skill_cap = ECONOMY_SETTLEMENT_D->query_trade_min_skill_cap();
    if (!NUMERIC_RULE_D->player_meets_skill_cap_gate(left, min_skill_cap)
        || !NUMERIC_RULE_D->player_meets_skill_cap_gate(right, min_skill_cap)) {
        return "任一方技能上限不足，无法交易。\n";
    }

    return "";
}

private string generate_session_id()
{
    _trade_seq += 1;
    return sprintf("trade_%d_%d", time(), _trade_seq);
}

private void schedule_session_timeout(mapping session)
{
    int timeout;
    int timer_handle;
    string timer_mode;

    if (!mapp(session))
        return;

    cancel_session_timer(session);
    timeout = ECONOMY_SETTLEMENT_D->query_trade_timeout();
    if (timeout <= 0)
        timeout = 60;
    session["expires_at"] = time() + timeout;
    timer_mode = "owner_timer";
    timer_handle = TIMER_D->call_later("expire_session", timeout,
        this_object(), session["id"]);
    if (!intp(timer_handle) || timer_handle <= 0) {
        timer_handle = call_out("expire_session", timeout, session["id"]);
        timer_mode = "local_fallback";
    }
    session["timer_handle"] = timer_handle;
    session["timer_mode"] = timer_mode;
    if (!intp(timer_handle) || timer_handle <= 0) {
        session["timer_mode"] = "unavailable";
        session["timer_error"] = "owner_route_unavailable";
        notify_trade_pair(session,
            "交易会话定时器当前不可用，系统不会自动结束本次会话。\n",
            "交易会话定时器当前不可用，系统不会自动结束本次会话。\n");
    } else {
        map_delete(session, "timer_error");
    }
}

private mapping create_session(object left, object right)
{
    mapping session;
    string left_id;
    string right_id;
    string session_id;

    left_id = ensure_player_id(left);
    right_id = ensure_player_id(right);
    session_id = generate_session_id();
    session = ([
        "id": session_id,
        "status": "requested",
        "left_id": left_id,
        "right_id": right_id,
        "left_player": left,
        "right_player": right,
        "created_at": time(),
        "revision": 1,
        "expires_at": 0,
        "timer_handle": 0,
        "offers": ([
            left_id: ([ "items": ({ }), "money": 0, "confirmed": 0 ]),
            right_id: ([ "items": ({ }), "money": 0, "confirmed": 0 ]),
        ]),
    ]);

    _sessions[session_id] = session;
    _player_sessions[left_id] = session_id;
    _player_sessions[right_id] = session_id;
    schedule_session_timeout(session);
    return session;
}

private mapping query_player_offer(mapping session, object player)
{
    string player_id;

    player_id = ensure_player_id(player);
    if (!mapp(session) || !stringp(player_id) || !mapp(session["offers"]))
        return 0;

    return session["offers"][player_id];
}

private object query_trade_partner(mapping session, object player)
{
    string player_id;

    player_id = ensure_player_id(player);
    if (!mapp(session) || !stringp(player_id))
        return 0;

    if (session["left_id"] == player_id)
        return session["right_player"];
    if (session["right_id"] == player_id)
        return session["left_player"];

    return 0;
}

private string format_trade_item_list(mixed *items)
{
    object item;
    string output;

    if (!arrayp(items) || sizeof(items) == 0)
        return "无";

    output = "";
    foreach (item in items) {
        if (!objectp(item))
            continue;
        if (output != "")
            output += ", ";
        output += item->query_name() || item->query("name") || item->query("template_id") || "物品";
    }

    return output == "" ? "无" : output;
}

private string format_trade_status_line(mapping session, string player_id)
{
    mapping offer;

    offer = mapp(session["offers"]) ? session["offers"][player_id] : 0;
    if (!mapp(offer))
        return "无报价";

    return sprintf("物品[%s] 现金[%s] %s",
        format_trade_item_list(offer["items"]),
        MONEY_D->format_money(offer["money"] || 0),
        offer["confirmed"] ? "已确认" : "未确认");
}

private void touch_trade_session(mapping session)
{
    if (!mapp(session))
        return;
    session["revision"] = intp(session["revision"]) && session["revision"] > 0
        ? session["revision"] + 1 : 1;
}

private mapping build_trade_offer_payload(mapping offer)
{
    object item;
    mapping *items;
    string item_id;
    string item_name;
    string template_id;

    items = ({ });
    foreach (item in mapp(offer) && arrayp(offer["items"])
        ? offer["items"] : ({ })) {
        if (!objectp(item))
            continue;
        item_id = get_object_id(item);
        item_name = item->query_name() || item->query("name") || "物品";
        template_id = item->query("template_id") || "";
        items += ({ ([
            "id": stringp(item_id) ? item_id : "",
            "name": item_name,
            "template_id": template_id,
            "quantity": ITEM_INVENTORY_D->query_item_stack_amount(item),
        ]) });
    }
    return ([
        "items": items,
        "money": mapp(offer) && intp(offer["money"]) ? offer["money"] : 0,
        "confirmed": mapp(offer) && offer["confirmed"] ? 1 : 0,
    ]);
}

private mapping build_trade_session_payload(mapping session, object player,
    string terminal_result, int terminal_success)
{
    string player_id;
    string partner_id;
    object partner;
    mapping self_offer;
    mapping partner_offer;

    if (!mapp(session) || !objectp(player))
        return 0;
    player_id = ensure_player_id(player);
    partner = query_trade_partner(session, player);
    partner_id = objectp(partner) ? ensure_player_id(partner) : "";
    self_offer = mapp(session["offers"]) ? session["offers"][player_id] : 0;
    partner_offer = mapp(session["offers"]) ? session["offers"][partner_id] : 0;
    return ([
        "kind": "trade_session",
        "session_id": session["id"] || "",
        "revision": intp(session["revision"]) ? session["revision"] : 1,
        "status": session["status"] || "",
        "counterparty_id": partner_id,
        "counterparty": query_player_display_name(partner),
        "self_offer": build_trade_offer_payload(self_offer),
        "counterparty_offer": build_trade_offer_payload(partner_offer),
        "available_actions": session["status"] == "requested"
            ? (player_id == session["right_id"]
                ? ({ "accept", "reject", "cancel" })
                : ({ "cancel" }))
            : session["status"] == "active"
                ? ({ "add", "remove", "money", "unmoney", "confirm", "cancel" })
                : ({ }),
        "expires_at": intp(session["expires_at"]) ? session["expires_at"] : 0,
        "terminal_result": terminal_result || "",
        "terminal_success": terminal_success ? 1 : 0,
    ]);
}

private void send_trade_session_packet(mapping session, object player,
    string terminal_result, int terminal_success, mapping response_context)
{
    mapping payload;

    if (!mapp(session) || !objectp(player) || !interactive(player))
        return;
    payload = build_trade_session_payload(session, player, terminal_result,
        terminal_success);
    if (!mapp(payload))
        return;
    xk_send_trade_result_packet(player, "session", "", 0,
        terminal_success ? 1 : 0, payload, response_context);
}

private void send_trade_session_pair(mapping session, object response_player,
    mapping response_context)
{
    if (!mapp(session))
        return;
    send_trade_session_packet(session, session["left_player"], "", 0,
        response_player == session["left_player"] ? response_context : 0);
    send_trade_session_packet(session, session["right_player"], "", 0,
        response_player == session["right_player"] ? response_context : 0);
}

private void send_trade_terminal_pair(mapping session, string status,
    string message, int success, object response_player,
    mapping response_context)
{
    if (!mapp(session))
        return;
    touch_trade_session(session);
    session["status"] = status;
    send_trade_session_packet(session, session["left_player"], message, success,
        response_player == session["left_player"] ? response_context : 0);
    send_trade_session_packet(session, session["right_player"], message, success,
        response_player == session["right_player"] ? response_context : 0);
}

void expire_session(string session_id)
{
    mapping session;

    session = ensure_session(session_id);
    if (!mapp(session))
        return;

    notify_trade_pair(session,
        "当前交易会话已超时，系统自动取消。\n",
        "当前交易会话已超时，系统自动取消。\n");
    send_trade_terminal_pair(session, "expired", "当前交易会话已超时，系统自动取消。", 0,
        0, 0);
    remove_session(session);
}

mapping query_trade_session(object player)
{
    mapping session;
    string player_id;
    string session_id;

    player_id = ensure_player_id(player);
    session_id = stringp(player_id) ? (_player_sessions[player_id] || "") : "";
    session = ensure_session(session_id);
    return mapp(session) ? copy(session) : 0;
}

string render_trade_session(object player)
{
    mapping session;
    object partner;
    string player_id;

    session = query_trade_session(player);
    if (!mapp(session))
        return "当前没有进行中的交易会话。\n";

    player_id = ensure_player_id(player);
    partner = query_trade_partner(session, player);
    return sprintf("交易状态：%s\n对方：%s\n你的报价：%s\n对方报价：%s\n剩余：%d秒\n",
        session["status"] || "",
        query_player_display_name(partner),
        format_trade_status_line(session, player_id),
        format_trade_status_line(session, partner == session["left_player"] ? session["left_id"] : session["right_id"]),
        (session["expires_at"] || time()) - time());
}

mapping query_runtime_snapshot()
{
    return ([
        "session_count": sizeof(keys(_sessions || ([]))),
        "player_binding_count": sizeof(keys(_player_sessions || ([]))),
        "runtime": ECONOMY_SETTLEMENT_D->query_trade_runtime_snapshot(),
        "session_ids": keys(_sessions || ([])),
    ]);
}

private mapping request_trade_internal(object actor, object target,
    mapping response_context)
{
    mapping session;
    string error;

    error = query_trade_pair_error(actor, target);
    if (error != "")
        return ([ "success": 0, "message": error || "无法发起交易。\n" ]);
    if (stringp(_player_sessions[ensure_player_id(actor)]) || stringp(_player_sessions[ensure_player_id(target)]))
        return ([ "success": 0, "message": "任一方已有进行中的交易会话。\n" ]);

    session = create_session(actor, target);
    notify_trade_pair(session,
        sprintf("你向%s发起了交易请求。\n", query_player_display_name(target)),
        sprintf("%s向你发起了交易请求。可用 trade accept / trade reject。\n", query_player_display_name(actor)));
    send_trade_session_pair(session, actor, response_context);
    return ([ "success": 1, "session_id": session["id"] ]);
}

private int is_trade_proxy(object target)
{
    return objectp(target) && target->query("trade_proxy") == 1;
}

private void seed_trade_proxy_offer(mapping session, object proxy)
{
    mapping offer;
    object item;
    object *items;

    if (!mapp(session) || !is_trade_proxy(proxy))
        return;
    offer = query_player_offer(session, proxy);
    if (!mapp(offer) || arrayp(offer["items"]) && sizeof(offer["items"]) > 0)
        return;
    items = all_inventory(proxy);
    foreach (item in items) {
        if (!objectp(item) || item->query("no_give") || item->query("no_drop"))
            continue;
        if (function_exists("query_equipped", item) && item->query_equipped())
            continue;
        offer["items"] += ({ item });
        break;
    }
}

private mapping request_trade_proxy_internal(object actor, object target,
    mapping response_context)
{
    mapping session;
    string actor_id;
    string target_id;
    string error;

    if (!userp(actor) || !is_trade_proxy(target))
        return ([ "success": 0, "message": "交易对象无效。\n" ]);
    error = query_trade_pair_error(actor, target);
    if (error != "")
        return ([ "success": 0, "message": error ]);
    actor_id = ensure_player_id(actor);
    target_id = ensure_player_id(target);
    if (!stringp(actor_id) || !stringp(target_id))
        return ([ "success": 0, "message": "交易对象无效。\n" ]);
    if (stringp(_player_sessions[actor_id]) || stringp(_player_sessions[target_id]))
        return ([ "success": 0, "message": "任一方已有进行中的交易会话。\n" ]);

    session = create_session(actor, target);
    session["status"] = "active";
    reset_session_confirm(session);
    seed_trade_proxy_offer(session, target);
    schedule_session_timeout(session);
    notify_trade_pair(session,
        sprintf("你向%s发起了交易，请加入报价后确认。\n",
            query_player_display_name(target)), "");
    send_trade_session_packet(session, actor, "", 0, response_context);
    return ([ "success": 1, "session_id": session["id"] ]);
}

private mapping accept_trade_internal(object player,
    mapping response_context)
{
    mapping session;
    string player_id;

    player_id = ensure_player_id(player);
    session = ensure_session(_player_sessions[player_id] || "");
    if (!mapp(session) || session["status"] != "requested" || session["right_id"] != player_id)
        return ([ "success": 0, "message": "当前没有待接受的交易请求。\n" ]);

    session["status"] = "active";
    reset_session_confirm(session);
    touch_trade_session(session);
    schedule_session_timeout(session);
    notify_trade_pair(session, "交易请求已被接受，可开始报价。\n", "你接受了交易请求，可开始报价。\n");
    send_trade_session_pair(session, player, response_context);
    return ([ "success": 1, "session_id": session["id"] ]);
}

mapping request_trade(object actor, object target)
{
    mapping created;
    mapping prepared;
    mapping committed;
    mapping transaction;
    mapping applied;
    mapping payload;
    mapping response_context;
    string actor_id;
    string target_id;
    string idempotency_key;

    if (objectp(actor) && actor->query_temp("owner/internal/trade") == 1) {
        response_context = actor->query_temp("owner/trade_response_context");
        return request_trade_internal(actor, target,
            mapp(response_context) ? copy(response_context) : 0);
    }
    response_context = CLIENT_SYNC_D->capture_command_response_context(actor);
    if (is_trade_proxy(target))
        return request_trade_proxy_internal(actor, target, response_context);
    actor_id = ensure_player_id(actor);
    target_id = ensure_player_id(target);
    if (!stringp(actor_id) || !stringp(target_id))
        return ([ "success": 0, "message": "交易对象无效。\n" ]);
    _owner_operation_seq++;
    idempotency_key = sprintf("trade/request/%s/%s/%d/%d", actor_id,
        target_id, time(), _owner_operation_seq);
    payload = ([
        "schema_version": 1,
        "trade": ([
            "operation": "request",
            "actor_player_id": actor_id,
            "target_player_id": target_id,
            "response_context": response_context,
        ]),
    ]);
    created = OWNER_COMMIT_D->create_domain_transaction("trade",
        idempotency_key, ({ "owner/player/" + actor_id,
            "owner/global/service/trade" }), payload);
    if (!mapp(created) || created["ok"] != 1
        || !mapp(created["transaction"]))
        return ([ "success": 0, "message": "交易提交失败，请稍后重试。\n" ]);
    prepared = OWNER_COMMIT_D->prepare_transaction(
        created["transaction"]["id"]);
    if (!mapp(prepared) || prepared["ok"] != 1)
        return ([ "success": 0, "message": "交易准备失败，请稍后重试。\n" ]);
    committed = OWNER_COMMIT_D->commit_transaction(
        created["transaction"]["id"]);
    if (!mapp(committed) || committed["ok"] != 1)
        return ([ "success": 0, "message": "交易提交失败，请稍后重试。\n",
            "owner_transaction": committed ]);
    transaction = committed["transaction"];
    applied = mapp(transaction) ? transaction["apply_result"] : 0;
    if (!mapp(applied) || applied["ok"] != 1)
        return ([ "success": 0, "message": "交易提交回执缺失。\n" ]);
    return ([ "success": 1, "session_id": applied["session_id"],
        "owner_transaction": committed ]);
}

mapping accept_trade(object player)
{
    mapping created;
    mapping prepared;
    mapping committed;
    mapping transaction;
    mapping applied;
    mapping payload;
    mapping response_context;
    string player_id;
    string idempotency_key;

    if (objectp(player) && player->query_temp("owner/internal/trade") == 1) {
        response_context = player->query_temp("owner/trade_response_context");
        return accept_trade_internal(player,
            mapp(response_context) ? copy(response_context) : 0);
    }
    response_context = CLIENT_SYNC_D->capture_command_response_context(player);
    player_id = ensure_player_id(player);
    if (!stringp(player_id))
        return ([ "success": 0, "message": "玩家身份无效。\n" ]);
    _owner_operation_seq++;
    idempotency_key = sprintf("trade/accept/%s/%d/%d", player_id,
        time(), _owner_operation_seq);
    payload = ([
        "schema_version": 1,
        "trade": ([
            "operation": "accept",
            "actor_player_id": player_id,
            "response_context": response_context,
        ]),
    ]);
    created = OWNER_COMMIT_D->create_domain_transaction("trade",
        idempotency_key, ({ "owner/player/" + player_id,
            "owner/global/service/trade" }), payload);
    if (!mapp(created) || created["ok"] != 1
        || !mapp(created["transaction"]))
        return ([ "success": 0, "message": "交易提交失败，请稍后重试。\n" ]);
    prepared = OWNER_COMMIT_D->prepare_transaction(
        created["transaction"]["id"]);
    if (!mapp(prepared) || prepared["ok"] != 1)
        return ([ "success": 0, "message": "交易准备失败，请稍后重试。\n" ]);
    committed = OWNER_COMMIT_D->commit_transaction(
        created["transaction"]["id"]);
    if (!mapp(committed) || committed["ok"] != 1)
        return ([ "success": 0, "message": "交易提交失败，请稍后重试。\n",
            "owner_transaction": committed ]);
    transaction = committed["transaction"];
    applied = mapp(transaction) ? transaction["apply_result"] : 0;
    if (!mapp(applied) || applied["ok"] != 1)
        return ([ "success": 0, "message": "交易提交回执缺失。\n" ]);
    return ([ "success": 1, "session_id": applied["session_id"],
        "owner_transaction": committed ]);
}

mapping reject_trade(object player)
{
    mapping session;
    string player_id;

    player_id = ensure_player_id(player);
    session = ensure_session(_player_sessions[player_id] || "");
    if (!mapp(session) || session["status"] != "requested" || session["right_id"] != player_id)
        return ([ "success": 0, "message": "当前没有待拒绝的交易请求。\n" ]);

    notify_trade_pair(session, "对方拒绝了你的交易请求。\n", "你拒绝了交易请求。\n");
    send_trade_terminal_pair(session, "rejected", "交易请求已拒绝。", 0,
        player, CLIENT_SYNC_D->capture_command_response_context(player));
    remove_session(session);
    return ([ "success": 1 ]);
}

mapping cancel_trade(object player)
{
    mapping session;
    string player_id;

    player_id = ensure_player_id(player);
    session = ensure_session(_player_sessions[player_id] || "");
    if (!mapp(session))
        return ([ "success": 0, "message": "当前没有进行中的交易会话。\n" ]);

    notify_trade_pair(session,
        player_id == session["left_id"] ? "你取消了交易。\n" : "对方取消了交易。\n",
        player_id == session["right_id"] ? "你取消了交易。\n" : "对方取消了交易。\n");
    send_trade_terminal_pair(session, "cancelled", "交易已取消。", 0,
        player, CLIENT_SYNC_D->capture_command_response_context(player));
    remove_session(session);
    return ([ "success": 1 ]);
}

mapping add_item_offer(object player, string item_name)
{
    mapping offer;
    mapping session;
    object item;

    session = ensure_session(_player_sessions[ensure_player_id(player)] || "");
    if (!mapp(session) || session["status"] != "active")
        return ([ "success": 0, "message": "当前没有可报价的交易会话。\n" ]);

    item = ITEM_INVENTORY_D->query_transferable_item(player, item_name);
    if (!objectp(item))
        return ([ "success": 0, "message": "这件物品当前无法用于交易。\n" ]);

    offer = query_player_offer(session, player);
    if (!mapp(offer))
        return ([ "success": 0, "message": "交易会话异常。\n" ]);
    if (member_array(item, offer["items"] || ({ })) != -1)
        return ([ "success": 0, "message": "该物品已在报价中。\n" ]);

    offer["items"] += ({ item });
    reset_session_confirm(session);
    touch_trade_session(session);
    schedule_session_timeout(session);
    send_trade_session_pair(session, player,
        CLIENT_SYNC_D->capture_command_response_context(player));
    return ([ "success": 1, "message": "已将物品加入交易报价。\n" ]);
}

mapping remove_item_offer(object player, string item_name)
{
    mapping offer;
    mapping session;
    mixed *items;
    object item;
    int index;

    session = ensure_session(_player_sessions[ensure_player_id(player)] || "");
    if (!mapp(session) || session["status"] != "active")
        return ([ "success": 0, "message": "当前没有可修改的交易会话。\n" ]);

    offer = query_player_offer(session, player);
    items = arrayp(offer["items"]) ? offer["items"] : ({ });
    for (index = 0; index < sizeof(items); index++) {
        item = items[index];
        if (!objectp(item))
            continue;
        if (item_name == (item->query_name() || item->query("name") || "")
            || item_name == (item->query("template_id") || "")) {
            offer["items"] = items[0..index - 1] + items[index + 1..];
            reset_session_confirm(session);
            touch_trade_session(session);
            schedule_session_timeout(session);
            send_trade_session_pair(session, player,
                CLIENT_SYNC_D->capture_command_response_context(player));
            return ([ "success": 1, "message": "已从交易报价中移除该物品。\n" ]);
        }
    }

    return ([ "success": 0, "message": "报价中没有这件物品。\n" ]);
}

mapping set_money_offer(object player, int amount)
{
    mapping offer;
    mapping session;

    session = ensure_session(_player_sessions[ensure_player_id(player)] || "");
    if (!mapp(session) || session["status"] != "active")
        return ([ "success": 0, "message": "当前没有可修改的交易会话。\n" ]);
    if (!intp(amount) || amount < 0)
        return ([ "success": 0, "message": "请输入有效金额。\n" ]);

    offer = query_player_offer(session, player);
    offer["money"] = amount;
    reset_session_confirm(session);
    touch_trade_session(session);
    schedule_session_timeout(session);
    send_trade_session_pair(session, player,
        CLIENT_SYNC_D->capture_command_response_context(player));
    return ([ "success": 1, "message": "已更新现金报价。\n" ]);
}

mapping clear_money_offer(object player)
{
    return set_money_offer(player, 0);
}

mapping confirm_trade(object player)
{
    mapping other_offer;
    mapping offer;
    mapping result;
    mapping session;
    mapping response_context;
    object partner;

    response_context = CLIENT_SYNC_D->capture_command_response_context(player);
    session = ensure_session(_player_sessions[ensure_player_id(player)] || "");
    if (!mapp(session) || session["status"] != "active")
        return ([ "success": 0, "message": "当前没有可确认的交易会话。\n" ]);
    if (query_trade_pair_error(session["left_player"], session["right_player"]) != "")
        return ([ "success": 0, "message": "双方已不满足交易条件。\n" ]);

    offer = query_player_offer(session, player);
    offer["confirmed"] = 1;
    touch_trade_session(session);
    schedule_session_timeout(session);

    partner = query_trade_partner(session, player);
    other_offer = query_player_offer(session, partner);
    if (is_trade_proxy(partner) && mapp(other_offer))
        other_offer["confirmed"] = 1;
    if (!mapp(other_offer) || !other_offer["confirmed"]) {
        notify_trade_pair(session,
            player == session["left_player"] ? "你已确认当前报价，等待对方确认。\n" : "对方已确认当前报价，等待你确认。\n",
            player == session["right_player"] ? "你已确认当前报价，等待对方确认。\n" : "对方已确认当前报价，等待你确认。\n");
        send_trade_session_pair(session, player, response_context);
        return ([ "success": 1, "pending": 1 ]);
    }

    result = ECONOMY_SETTLEMENT_D->settle_player_trade(session);
    if (!mapp(result) || !result["success"]) {
        notify_trade_pair(session,
            stringp(result["message"]) ? result["message"] : "交易提交失败。\n",
            stringp(result["message"]) ? result["message"] : "交易提交失败。\n");
        if (mapp(result) && result["pending_recovery"]) {
            /* 阶段未完成时保留会话和原 operation 身份，允许同一
             * 报价恢复；不能把待收据发送成终态失败并丢失责任。 */
            send_trade_session_pair(session, player, response_context);
            return result;
        }
        send_trade_terminal_pair(session, "failed",
            stringp(result["message"]) ? result["message"] : "交易未能完成。", 0,
            player, response_context);
        remove_session(session);
        return result;
    }

    notify_trade_pair(session,
        sprintf("交易完成！你支付%s，收到%s。\n",
            MONEY_D->format_money((result["left_money"] || 0) + (result["left_tax"] || 0)),
            MONEY_D->format_money(result["right_money"] || 0)),
        sprintf("交易完成！你支付%s，收到%s。\n",
            MONEY_D->format_money((result["right_money"] || 0) + (result["right_tax"] || 0)),
            MONEY_D->format_money(result["left_money"] || 0)));
    send_trade_terminal_pair(session, "completed", "交易已完成。", 1,
        player, response_context);
    if (objectp(player))
        CLIENT_SYNC_D->request_status_inventory(player, response_context);
    remove_session(session);
    return result;
}

void create()
{
    ::create();
    if (!mapp(_sessions))
        _sessions = ([]);
    if (!mapp(_player_sessions))
        _player_sessions = ([]);
    _trade_seq = 0;
    _owner_operation_seq = 0;
}


/* ===== 领域协议发送器（自 kernel/simul_efun_trade_xk.c 迁入，逐字保留） ===== */

#define XK_PROTOCOL_CORE "/kernel/framework/xk_protocol_core"

varargs void xk_send_trade_result_packet(object me, string action, string item_name, int price, int success, mapping extra_payload, mapping response_context)
{
    int balance;
    mapping payload;
    string safe_action;
    string safe_item_name;

    if (!me || !interactive(me))
        return;
    /* Settlement already supplies an authoritative same-command balance. */
    if (mapp(extra_payload) && intp(extra_payload["balance"]))
        balance = extra_payload["balance"];
    else
        balance = MONEY_D->query_balance(me);
    if (!stringp(action))
        action = "";
    if (!stringp(item_name))
        item_name = "";

    safe_action = action;
    safe_item_name = item_name;
    payload = ([
        "action": safe_action,
        "item": safe_item_name,
        "price": price,
        "success": success ? 1 : 0,
        "balance": balance,
        "enhance_level": 0,
    ]);
    if (mapp(extra_payload)) {
        payload += extra_payload;
        if (!intp(payload["enhance_level"]) || payload["enhance_level"] < 0)
            payload["enhance_level"] = 0;
    }

    call_other(XK_PROTOCOL_CORE, "xk_send", me, XK_TRADE, payload,
        response_context);
}
