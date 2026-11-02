/**
 * @file    client_sync_packets.c
 * @brief   客户端同步即时包与刷新包发送模块
 */

#pragma strict_types
#pragma save_types

#include <system/xk_protocol.h>
#include <system/services.h>

object resolve_sync_target(object player);
int can_deliver_to_target(object player);
string query_object_session_id(object player);
mixed *normalize_immediate_packets(mixed packets);
mapping refresh_plan_projection_for_current(object player, mapping plan);
void record_chat_batch_flush_diagnostics(int message_count, int build_wall_ns,
    int build_cpu_ns, int send_wall_ns, int send_cpu_ns, int preencoded_success);
void record_chat_singleton_diagnostics(mapping packet, int delivered,
    int send_wall_ns, int send_cpu_ns);
void record_pending_chat_packet_age_summary(int *samples, int *totals_us,
    int *max_us, int *unavailable, int *origin_metrics);
private mapping chat_message_protocol_payload(mapping packet);
private mapping chat_message_protocol_batch_child(mapping packet);
private string chat_message_stable_child_json(mapping packet);
private int send_chat_message_batch_fallback(object player, mixed *packets,
    int packet_count, mapping response_context);
void queue_deferred_score_refresh(object player, mapping response_context);

private string packet_response_id(mapping packet)
{
    mapping context;
    mixed value;

    if (!mapp(packet))
        return "";
    context = mapp(packet["response_context"])
        ? packet["response_context"] : 0;
    value = mapp(context) ? context["command_request_id"]
        : packet["command_request_id"];
    return stringp(value) ? value : "";
}

private mixed install_response_context(object player, string response_id)
{
    mapping previous;

    if (!objectp(player))
        return 0;
    previous = ([
        "response_id": player->query_temp("gateway/command_request_id"),
        "context_active": player->query_temp("gateway/command_context_active"),
    ]);
    if (stringp(response_id) && response_id != "")
        player->set_temp("gateway/command_request_id", response_id);
    else
        player->delete_temp("gateway/command_request_id");
    /* Explicit sync/immediate delivery is a synchronous producer scope. */
    player->set_temp("gateway/command_context_active", 1);
    return previous;
}

private void restore_response_context(object player, mixed previous)
{
    mixed previous_response_id;

    if (!objectp(player))
        return;
    if (mapp(previous)) {
        previous_response_id = previous["response_id"];
        if (stringp(previous_response_id) && previous_response_id != "")
            player->set_temp("gateway/command_request_id",
                previous_response_id);
        else
            player->delete_temp("gateway/command_request_id");
        if (previous["context_active"])
            player->set_temp("gateway/command_context_active", 1);
        else
            player->delete_temp("gateway/command_context_active");
        return;
    }
    if (stringp(previous) && previous != "")
        player->set_temp("gateway/command_request_id", previous);
    else
        player->delete_temp("gateway/command_request_id");
    player->delete_temp("gateway/command_context_active");
}

private mapping chat_message_protocol_payload(mapping packet)
{
    mapping payload;

    if (!mapp(packet))
        return ([]);
    if (mapp(packet["protocol_payload"]))
        return packet["protocol_payload"];

    payload = mapp(packet["extra"]) ? copy(packet["extra"]) : ([]);
    payload["content"] = packet["text"] || "";
    payload["channel"] = packet["channel"] || "main";
    return payload;
}

private mapping chat_message_protocol_batch_child(mapping packet)
{
    mapping child;

    if (!mapp(packet))
        return ([]);
    child = mapp(packet["protocol_batch_child"])
        ? packet["protocol_batch_child"] : 0;
    if (mapp(child) && child["type"] == XK_CHAT && mapp(child["payload"]))
        return child;
    return ([
        "type": XK_CHAT,
        "payload": chat_message_protocol_payload(packet),
    ]);
}

private string chat_message_stable_child_json(mapping packet)
{
    string stable_child_json;

    if (!mapp(packet))
        return "";
    stable_child_json = packet["protocol_stable_child_json"];
    return stringp(stable_child_json) && stable_child_json != ""
        ? stable_child_json : "";
}

/* The preencoded path is deliberately optional.  Its failure must retain the
 * historical mapping/BACH sender so a bad artifact cannot drop a room chat. */
private int send_chat_message_batch_fallback(object player, mixed *packets,
    int packet_count, mapping response_context)
{
    mixed *messages;
    mapping packet;
    int index;

    if (!arrayp(packets) || packet_count <= 0 || packet_count > sizeof(packets))
        return 0;
    messages = allocate(packet_count);
    for (index = 0; index < packet_count; index++) {
        packet = packets[index];
        if (!mapp(packet) || packet["kind"] != "chat_message")
            return 0;
        messages[index] = chat_message_protocol_batch_child(packet);
    }
    return call_other("/kernel/simul_efun", "xk_send_batch", player, messages,
        response_context);
}

/* Keep the exact pre-send timestamp captured by the flush orchestration, but
 * accumulate it while the sender already walks CHAT packets to build BACH. */
private void accumulate_pending_chat_packet_age(int *samples, int *totals_us,
    int *max_us, int *unavailable, mapping packet, int now,
    int *origin_metrics)
{
    mapping extra;
    int direction_index;
    int elapsed_ns;
    int elapsed_us;
    int origin_ns;
    int queued_at;

    if (!arrayp(samples) || !arrayp(totals_us) || !arrayp(max_us)
        || !arrayp(unavailable) || !arrayp(origin_metrics)
        || sizeof(samples) < 3
        || sizeof(totals_us) < 3 || sizeof(max_us) < 3
        || sizeof(unavailable) < 3 || sizeof(origin_metrics) < 4
        || !mapp(packet) || packet["kind"] != "chat_message" || now <= 0)
        return;

    extra = mapp(packet["extra"]) ? packet["extra"] : ([]);
    direction_index = extra["direction"] == "self" ? 0
        : extra["direction"] == "incoming" ? 1 : 2;
    queued_at = intp(packet["chat_pending_since_ns"])
        ? packet["chat_pending_since_ns"] : 0;
    elapsed_ns = queued_at > 0 ? now - queued_at : -1;
    if (elapsed_ns < 0)
        unavailable[direction_index]++;
    else {
        elapsed_us = elapsed_ns / 1000;
        samples[direction_index]++;
        totals_us[direction_index] += elapsed_us;
        if (elapsed_us > max_us[direction_index])
            max_us[direction_index] = elapsed_us;
    }

    origin_ns = intp(packet["chat_origin_ns"]) ? packet["chat_origin_ns"] : 0;
    elapsed_ns = origin_ns > 0 ? now - origin_ns : -1;
    if (elapsed_ns < 0)
        origin_metrics[3]++;
    else {
        elapsed_us = elapsed_ns / 1000;
        origin_metrics[0]++;
        origin_metrics[1] += elapsed_us;
        if (elapsed_us > origin_metrics[2])
            origin_metrics[2] = elapsed_us;
    }
}

/* Delivery can fail before the normal sender walk.  Preserve the prior
 * measurement contract by using the same accumulator only on that rare path. */
private void record_pending_chat_packet_age_from_buffer(mixed *packets,
    int packet_count, int now)
{
    int *max_us;
    int *origin_metrics;
    int *samples;
    int *totals_us;
    int *unavailable;
    int index;

    if (!arrayp(packets) || packet_count <= 0 || now <= 0)
        return;
    if (packet_count > sizeof(packets))
        packet_count = sizeof(packets);
    samples = allocate(3);
    totals_us = allocate(3);
    max_us = allocate(3);
    unavailable = allocate(3);
    origin_metrics = allocate(4);
    for (index = 0; index < packet_count; index++)
        accumulate_pending_chat_packet_age(samples, totals_us, max_us,
            unavailable, packets[index], now, origin_metrics);
    record_pending_chat_packet_age_summary(samples, totals_us, max_us,
        unavailable, origin_metrics);
}

private int apply_immediate_packet_unscoped(object player, mapping packet)
{
    string kind;
    mapping response_context;
    mapping extra;

    player = resolve_sync_target(player);
    if (!objectp(player) || !can_deliver_to_target(player) || !mapp(packet))
        return 0;

    kind = stringp(packet["kind"]) ? packet["kind"] : "";
    response_context = mapp(packet["response_context"])
        ? packet["response_context"] : 0;
    if (!mapp(response_context) && packet_response_id(packet) != "")
        response_context = ([
            "command_request_id": packet_response_id(packet),
        ]);
    switch (kind) {
        case "trade_result":
            TRADE_D->xk_send_trade_result_packet(player,
                packet["action"] || "",
                packet["item_name"] || "",
                packet["price"] || 0,
                packet["success"] || 0,
                packet["extra_payload"] || ([]), response_context);
            return 1;
        case "shop_snapshot":
            if (objectp(packet["vendor"]))
                SHOP_D->xk_emit_shop_snapshot(player,
                    packet["vendor"], response_context);
            return 1;
        case "dialog":
            extra = mapp(packet["extra"]) ? copy(packet["extra"]) : ([]);
            if (mapp(response_context))
                extra["response_context"] = response_context;
            call_other("/kernel/framework/xk_protocol_core", "xk_send_dialog", player,
                packet["title"] || "",
                packet["content"] || "", extra);
            return 1;
        case "quick_cmds":
            call_other("/kernel/simul_efun", "xk_send_quick_cmds", player,
                packet["cmds"], packet["priority"] || 1, response_context);
            return 1;
        case "quick_cmds_secondary":
            call_other("/kernel/simul_efun", "xk_send_secondary_cmds", player,
                packet["cmds"], packet["title"] || "", packet["priority"] || 1,
                response_context);
            return 1;
        case "dynamic_buttons":
            call_other("/kernel/simul_efun", "xk_send_dynamic_buttons", player,
                packet["cmds"], packet["title"] || "", packet["priority"] || 1,
                response_context);
            return 1;
        case "report":
            call_other("/kernel/framework/xk_protocol_core", "xk_send_report", player,
                packet["command"] || "",
                packet["data"] || ([]), response_context);
            return 1;
        case "error":
            call_other("/kernel/framework/xk_protocol_core", "xk_send_error", player,
                packet["message"] || "",
                packet["code"] || 0, response_context);
            return 1;
        case "system":
            call_other("/kernel/framework/xk_protocol_core", "xk_send_system", player,
                packet["text"] || "",
                packet["level"] || "info", response_context);
            return 1;
        case "skill_event":
            call_other("/kernel/framework/xk_protocol_core", "xk_send_combat", player,
                XK_CBSK,
                packet["payload"] || ([]), response_context);
            return 1;
        case "combat_packet":
            call_other("/kernel/framework/xk_protocol_core", "xk_send_combat", player,
                packet["type"] || "",
                packet["payload"] || ([]), response_context);
            return 1;
        case "chat_message":
            return call_other("/kernel/framework/xk_protocol_core", "xk_send",
                player, XK_CHAT, chat_message_protocol_payload(packet),
                response_context) ? 1 : 0;
    }

    return 0;
}

int apply_immediate_packet(object player, mapping packet)
{
    string response_id;
    mixed previous;
    int result;

    response_id = packet_response_id(packet);
    previous = install_response_context(player, response_id);
    result = 1;
    if (catch(result = apply_immediate_packet_unscoped(player, packet)))
        result = 0;
    restore_response_context(player, previous);
    return result;
}

private int flush_chat_message_batch(object player, mixed *packets,
    int packet_count, int *pending_age_samples, int *pending_age_totals_us,
    int *pending_age_max_us, int *pending_age_unavailable,
    int pending_chat_age_now, int *origin_age_metrics)
{
    mapping packet;
    mixed fallback_error;
    mixed previous_response_context;
    string response_id;
    string packet_id;
    string stable_child_json;
    string *stable_children_json;
    int count;
    int build_started_at;
    int build_cpu_started_at;
    int build_cpu_finished_at;
    int index;
    int preencoded_ready;
    int send_started_at;
    int send_cpu_started_at;
    int send_cpu_finished_at;
    int result;
    int preencoded_success;
    int same_response_context;

    if (!arrayp(packets) || packet_count <= 0)
        return 1;
    if (packet_count > sizeof(packets))
        return 0;
    if (packet_count == 1) {
        if (!mapp(packets[0]))
            return 0;
        accumulate_pending_chat_packet_age(pending_age_samples,
            pending_age_totals_us, pending_age_max_us,
            pending_age_unavailable, packets[0], pending_chat_age_now,
            origin_age_metrics);
        send_started_at = perf_counter_ns();
        send_cpu_started_at = thread_cpu_time_ns();
        result = apply_immediate_packet(player, packets[0]);
        send_cpu_finished_at = thread_cpu_time_ns();
        record_chat_singleton_diagnostics(packets[0], result,
            perf_counter_ns() - send_started_at,
            send_cpu_started_at >= 0 && send_cpu_finished_at >= send_cpu_started_at
                ? send_cpu_finished_at - send_cpu_started_at : -1);
        return result;
    }

    build_started_at = perf_counter_ns();
    build_cpu_started_at = thread_cpu_time_ns();
    stable_children_json = allocate(packet_count);
    preencoded_ready = 1;
    same_response_context = 1;
    response_id = packet_response_id(packets[0]);
    for (index = 0; index < packet_count; index++) {
        packet = packets[index];
        if (!mapp(packet) || packet["kind"] != "chat_message")
            return 0;
        accumulate_pending_chat_packet_age(pending_age_samples,
            pending_age_totals_us, pending_age_max_us,
            pending_age_unavailable, packet, pending_chat_age_now,
            origin_age_metrics);
        packet_id = packet_response_id(packet);
        if (packet_id != response_id)
            same_response_context = 0;
        stable_child_json = chat_message_stable_child_json(packet);
        if (stable_child_json == "")
            preencoded_ready = 0;
        stable_children_json[count++] = stable_child_json;
    }
    build_cpu_finished_at = thread_cpu_time_ns();
    if (!same_response_context) {
        result = 1;
        for (index = 0; index < packet_count; index++)
            if (!apply_immediate_packet(player, packets[index]))
                result = 0;
        return result;
    }
    previous_response_context = install_response_context(player, response_id);
    send_started_at = perf_counter_ns();
    send_cpu_started_at = thread_cpu_time_ns();
    result = 0;
    if (preencoded_ready) {
        if (catch(result = call_other(
            "/kernel/framework/xk_protocol_core",
            "xk_send_preencoded_chat_batch", player, stable_children_json,
            response_id != ""
                ? ([ "command_request_id": response_id ]) : 0)))
            result = 0;
    }
    /* L-36：-1（已填充待对账）不得触发回退重发；指标只把完整完成记为成功。 */
    preencoded_success = result == 1 ? 1 : 0;
    if (!result) {
        fallback_error = catch(result = send_chat_message_batch_fallback(
            player, packets, packet_count,
            response_id != "" ? ([ "command_request_id": response_id ]) : 0));
        if (fallback_error)
            result = 0;
    }
    send_cpu_finished_at = thread_cpu_time_ns();
    restore_response_context(player, previous_response_context);
    record_chat_batch_flush_diagnostics(count, send_started_at - build_started_at,
        build_cpu_started_at >= 0 && build_cpu_finished_at >= build_cpu_started_at
            ? build_cpu_finished_at - build_cpu_started_at : -1,
        perf_counter_ns() - send_started_at,
        send_cpu_started_at >= 0 && send_cpu_finished_at >= send_cpu_started_at
            ? send_cpu_finished_at - send_cpu_started_at : -1,
        preencoded_success);
    return result;
}

/* Pending entries have already validated this buffer as pure CHAT.  Avoid the
 * generic partition buffers, but preserve its delivery guard and exact retry
 * prefix when the existing singleton/BACH sender cannot complete. */
mixed *apply_chat_only_packet_buffer(object player, mixed *packets,
    int packet_count, int pending_chat_age_now)
{
    int *max_us;
    int *origin_metrics;
    int *samples;
    int *totals_us;
    int *unavailable;
    int result;

    if (!arrayp(packets) || packet_count <= 0)
        return ({ });
    if (packet_count > sizeof(packets))
        packet_count = sizeof(packets);

    if (!can_deliver_to_target(player)) {
        if (pending_chat_age_now > 0)
            record_pending_chat_packet_age_from_buffer(packets, packet_count,
                pending_chat_age_now);
        return packet_count == sizeof(packets)
            ? packets : packets[0..packet_count - 1];
    }
    if (pending_chat_age_now > 0) {
        samples = allocate(3);
        totals_us = allocate(3);
        max_us = allocate(3);
        unavailable = allocate(3);
        origin_metrics = allocate(4);
    }
    result = flush_chat_message_batch(player, packets, packet_count, samples,
        totals_us, max_us, unavailable, pending_chat_age_now, origin_metrics);
    if (pending_chat_age_now > 0)
        record_pending_chat_packet_age_summary(samples, totals_us, max_us,
            unavailable, origin_metrics);
    if (result)
        return ({ });
    return packet_count == sizeof(packets)
        ? packets : packets[0..packet_count - 1];
}

mixed *apply_immediate_packet_buffer(object player, mixed *packets,
    int packet_count, int pending_chat_age_now)
{
    mixed packet;
    mixed *chat_batch;
    mixed *undelivered;
    int *max_us;
    int *origin_metrics;
    int *samples;
    int *totals_us;
    int *unavailable;
    int chat_count;
    int chat_index;
    int index;
    int undelivered_count;

    if (!arrayp(packets) || packet_count <= 0)
        return ({ });
    if (packet_count > sizeof(packets))
        packet_count = sizeof(packets);

    if (!can_deliver_to_target(player)) {
        if (pending_chat_age_now > 0)
            record_pending_chat_packet_age_from_buffer(packets, packet_count,
                pending_chat_age_now);
        return packet_count == sizeof(packets)
            ? packets : packets[0..packet_count - 1];
    }

    if (pending_chat_age_now > 0) {
        samples = allocate(3);
        totals_us = allocate(3);
        max_us = allocate(3);
        unavailable = allocate(3);
        origin_metrics = allocate(4);
    }
    undelivered = allocate(packet_count);
    chat_batch = allocate(packet_count);
    for (index = 0; index < packet_count; index++) {
        packet = packets[index];
        if (mapp(packet) && packet["kind"] == "chat_message") {
            chat_batch[chat_count++] = packet;
            continue;
        }
        if (chat_count > 0) {
            if (!flush_chat_message_batch(player, chat_batch, chat_count,
                samples, totals_us, max_us, unavailable, pending_chat_age_now,
                origin_metrics)) {
                for (chat_index = 0; chat_index < chat_count; chat_index++)
                    undelivered[undelivered_count++] = chat_batch[chat_index];
            }
            chat_count = 0;
        }
        if (mapp(packet) && !apply_immediate_packet(player, packet))
            undelivered[undelivered_count++] = packet;
    }
    if (chat_count > 0) {
        if (!flush_chat_message_batch(player, chat_batch, chat_count, samples,
            totals_us, max_us, unavailable, pending_chat_age_now,
            origin_metrics)) {
            for (chat_index = 0; chat_index < chat_count; chat_index++)
                undelivered[undelivered_count++] = chat_batch[chat_index];
        }
    }

    if (pending_chat_age_now > 0)
        record_pending_chat_packet_age_summary(samples, totals_us, max_us,
            unavailable, origin_metrics);

    return undelivered_count > 0
        ? undelivered[0..undelivered_count - 1] : ({ });
}

mixed *apply_immediate_packets(object player, mixed packets)
{
    mixed *normalized;

    normalized = normalize_immediate_packets(packets);
    return apply_immediate_packet_buffer(player, normalized, sizeof(normalized),
        0);
}

int apply_sync_plan(object player, mapping plan)
{
    mapping projection;
    mapping exits;
    object room;
    mapping response_context;
    mixed previous_response_context;
    mixed dispatch_error;
    string response_id;
    string title;
    string zone;
    int context_installed;
    int delivered;

    player = resolve_sync_target(player);
    if (!objectp(player))
        return 0;
    /* L-183：与 can_deliver_to_target 同一投递门——非 interactive 但持有
     * gateway session 的目标同样可投递，避免 refresh 反复入队后必然失败。 */
    if (!interactive(player) && query_object_session_id(player) == "")
        return 0;
    /*
     * plan 投影过期（玩家对象重建后 advance_projection 推进过）时，
     * 不得整包跳过：REAL/房间快照均为服务端现算内容，刷新投影后继续
     * 执行（统一 helper refresh_plan_projection_for_current），保证客户端
     * REAL 同步链不断（否则客户端 gate 以旧投影拒绝之后所有房间包，
     * 表现为“后端移动、前端永不刷新”）。
     */
    plan = refresh_plan_projection_for_current(player, plan);
    response_context = mapp(plan["response_context"])
        ? plan["response_context"] : 0;
    response_id = mapp(response_context)
        ? response_context["command_request_id"] : "";
    previous_response_context = install_response_context(player, response_id);
    context_installed = 1;

    dispatch_error = catch {
    delivered = 1;

    if (plan["projection_state"]) {
        projection = LOGIN_PROJECTION_D->query_projection_context(player);
        if (!LOGIN_PROJECTION_D->xk_send_projection_state(player,
            projection, response_context))
            delivered = 0;
    }

    if (plan["room_snapshot"]) {
        room = objectp(plan["room"]) ? plan["room"] : environment(player);
        if (objectp(room)) {
            if (!call_other("/kernel/simul_efun", "xk_emit_room_snapshot",
                player, room, response_context))
                delivered = 0;
            /* A projection reset clears the client's map cache. Force a
             * fresh MAPS packet instead of allowing the normal map ack cache
             * to short-circuit this authoritative bootstrap barrier. */
            if (room == environment(player)) {
                title = room->query("short") || "未知地点";
                zone = room->query("zone") || "";
                exits = room->query("exits");
                if (!call_other("/kernel/simul_efun", "xk_send_map_grid",
                    player, title, zone, exits, 1, response_context))
                    delivered = 0;
            }
        } else
            delivered = 0;
    }

    /*
     * 首屏与切房的体感优先级是“先看到房间，再补状态/背包”。
     * 这里保持 room snapshot 在前，避免登录成功后先刷 STAT/IVTY、房间标题却晚一拍。
     */
    if (plan["status"])
        if (!call_other("/kernel/simul_efun", "xk_send_status", player,
            response_context))
            delivered = 0;

    if (plan["inventory"])
        if (!call_other("/kernel/simul_efun", "xk_send_inventory", player,
            0, response_context))
            delivered = 0;

    if (plan["objectives"]) {
        projection = mapp(projection) ? projection
            : LOGIN_PROJECTION_D->query_projection_context(player);
        if (!QUEST_PROTOCOL->xk_send_quest_list(player, response_context))
            delivered = 0;
    }

    if (plan["score_refresh"])
        queue_deferred_score_refresh(player, response_context);
    };

    if (context_installed)
        restore_response_context(player, previous_response_context);
    if (dispatch_error)
        return 0;
    return delivered;
}
