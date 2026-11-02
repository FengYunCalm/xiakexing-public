/**
 * @file    simul_efun_message.c
 * @brief   消息聊天辅助子服务
 * @author  XiaKeXing Development Team
 * @date    2026-03-30
 * @version 1.0.0
 */

#pragma strict_types

#include <system/ansi.h>
#include <system/services.h>

private string resolve_display_name(object speaker)
{
    string name;

    if (!objectp(speaker))
        return "有人";

    name = stringp(speaker->query_name()) ? speaker->query_name() :
        (stringp(speaker->query("name")) ? speaker->query("name") : "");
    if (!stringp(name) || name == "")
        name = "有人";

    return name;
}

private string channel_display_name(string type)
{
    switch (type) {
    case "say":
        return "附近";
    case "shout":
        return "世界";
    case "tell":
    case "whisper":
        return "私聊";
    default:
        return "消息";
    }
}

int xk_send_chat_packet_ex(object me, string type, string msg, mapping extra);
int xk_send_chat_packet_with_origin_ex(object me, string type, string msg,
    mapping extra, int chat_origin_ns);

/* Request-path self acknowledgement uses the same transaction packet as the
 * queued CHAT path, but it must not merge with and synchronously drain the
 * recipient's unrelated incoming fanout backlog.  Any validation/delivery
 * failure returns to the historical queue+flush path. */
private int send_room_chat_self_ack(object me, string type, string msg,
    mapping extra, int chat_origin_ns)
{
    mapping tx;
    mapping payload;
    string channel;

    if (!objectp(me) || !interactive(me) || !stringp(msg) || msg == "")
        return 0;

    channel = stringp(type) && type != "" ? type : "say";
    payload = mapp(extra) ? copy(extra) : ([]);
    if (!stringp(payload["channel_name"]) || payload["channel_name"] == "")
        payload["channel_name"] = channel_display_name(channel);
    if (!stringp(payload["sender_name"]) || payload["sender_name"] == "")
        payload["sender_name"] = resolve_display_name(me);
    payload["direction"] = "self";
    if (!intp(payload["timestamp"]) || payload["timestamp"] <= 0)
        payload["timestamp"] = time();

    tx = CLIENT_SYNC_D->tx_add_chat_message_ex(0, msg, channel, payload);
    tx = CLIENT_SYNC_D->tx_with_response_context(tx,
        CLIENT_SYNC_D->capture_command_response_context(me));
    if (chat_origin_ns > 0 && arrayp(tx["immediate"])
        && sizeof(tx["immediate"]) > 0 && mapp(tx["immediate"][0]))
        tx["immediate"][0]["chat_origin_ns"] = chat_origin_ns;
    if (CLIENT_SYNC_D->submit_immediate_chat_ack(me, tx))
        return 1;

    CLIENT_SYNC_D->submit_transaction(me, tx);
    CLIENT_SYNC_D->flush_player_sync(me);
    return 0;
}

void xk_send_chat_packet(object me, string type, string msg)
{
    xk_send_chat_packet_ex(me, type, msg, ([]));
}

private object query_loaded_message_daemon()
{
    object daemon;
    mixed err;

    err = catch(daemon = find_object(MESSAGE_D));
    if (err || !objectp(daemon))
        return 0;
    return daemon;
}

private int try_emit_message_event(object ob, string str, mapping options)
{
    object daemon;
    mixed err;
    int result;

    if (!objectp(ob) || !stringp(str) || str == "")
        return 0;

    daemon = query_loaded_message_daemon();
    if (!objectp(daemon))
        return 0;

    err = catch(result = daemon->emit_message_event(ob, str,
        mapp(options) ? copy(options) : ([])));
    if (err)
        return 0;
    return result ? 1 : 0;
}

int xk_send_chat_packet_ex(object me, string type, string msg, mapping extra)
{
    return xk_send_chat_packet_with_origin_ex(me, type, msg, extra, 0);
}

int xk_send_chat_packet_with_origin_ex(object me, string type, string msg,
    mapping extra, int chat_origin_ns)
{
    mapping tx;
    mapping payload;
    string channel;

    if (!me || !interactive(me))
        return 0;

    channel = stringp(type) && type != "" ? type : "say";
    payload = mapp(extra) ? copy(extra) : ([]);
    if (!stringp(payload["channel_name"]) || payload["channel_name"] == "")
        payload["channel_name"] = channel_display_name(channel);
    if (!stringp(payload["sender_name"]) || payload["sender_name"] == "")
        payload["sender_name"] = resolve_display_name(me);
    if (!stringp(payload["direction"]) || payload["direction"] == "")
        payload["direction"] = "incoming";
    if (!intp(payload["timestamp"]) || payload["timestamp"] <= 0)
        payload["timestamp"] = time();

    tx = CLIENT_SYNC_D->tx_add_chat_message_ex(0,
        msg || "",
        channel,
        payload);
    if (!mapp(tx))
        return 0;
    tx = CLIENT_SYNC_D->tx_with_response_context(tx,
        payload["direction"] == "self"
            ? CLIENT_SYNC_D->capture_command_response_context(me) : 0);
    if (chat_origin_ns > 0 && arrayp(tx["immediate"])
        && sizeof(tx["immediate"]) > 0 && mapp(tx["immediate"][0]))
        tx["immediate"][0]["chat_origin_ns"] = chat_origin_ns;
    return CLIENT_SYNC_D->submit_transaction(me, tx);
}

void xk_send_room_chat_packets(object speaker, object *targets, string type,
    string msg, mapping extra, int chat_origin_ns)
{
    mapping payload;
    string channel;

    if (!objectp(speaker) || !arrayp(targets) || sizeof(targets) == 0
        || !stringp(msg) || msg == "")
        return;

    channel = stringp(type) && type != "" ? type : "say";
    payload = mapp(extra) ? copy(extra) : ([]);
    if (!stringp(payload["channel_name"]) || payload["channel_name"] == "")
        payload["channel_name"] = channel_display_name(channel);
    if (!stringp(payload["sender_name"]) || payload["sender_name"] == "")
        payload["sender_name"] = resolve_display_name(speaker);
    if (!stringp(payload["direction"]) || payload["direction"] == "")
        payload["direction"] = "incoming";
    if (!intp(payload["timestamp"]) || payload["timestamp"] <= 0)
        payload["timestamp"] = time();

    CLIENT_SYNC_D->submit_chat_message_batch(targets, msg, channel, payload,
        chat_origin_ns);
}

private int message_runtime_uses_xk(object ob)
{
    mixed version;

    if (!objectp(ob) || !interactive(ob))
        return 0;
    if (ob->query_temp("client/plain_text_only"))
        return 0;
    version = ob->query_temp("xk_protocol_version");
    return stringp(version) && version != "";
}

private int message_runtime_has_visible_text(string str)
{
    return stringp(str) && trim(str) != "";
}

private string query_raw_output_owner_id(object ob)
{
    mapping metadata;
    string object_id;
    mixed err;

    if (!objectp(ob))
        return "";

    err = catch(metadata = OWNER_RUNTIME_D->query_owner_metadata(ob));
    if (!err && mapp(metadata) && stringp(metadata["owner_id"]) && metadata["owner_id"] != "")
        return metadata["owner_id"];

    object_id = call_other("/kernel/simul_efun", "get_object_id", ob);
    if (stringp(object_id) && object_id != "")
        return "owner/player/" + object_id;

    return "owner/output/" + file_name(ob);
}

private mapping build_raw_owner_fifo_meta(string str)
{
    return ([
        "stream": "main",
        "payload_contract": "frozen_text_metadata",
        "text_length": strlen(str),
        "raw_text_fifo_boundary": 1,
        "business_output": 1,
        "direct_socket_write": 0,
    ]);
}

private int validate_raw_owner_fifo_boundary(object ob, string str)
{
    mapping result;
    mixed err;
    string owner_id;

    if (!objectp(ob) || !stringp(str) || str == "")
        return 0;
    owner_id = query_raw_output_owner_id(ob);
    if (!stringp(owner_id) || owner_id == "")
        return 0;
    if (GATEWAY_SESSION_D->query_object_session_id(ob) == "")
        return 1;
    err = catch(result = OWNER_SESSION_FIFO_D->validate_owner_output_boundary(
        ob, owner_id, "raw_text", build_raw_owner_fifo_meta(str)));
    return !err && mapp(result) && result["ok"] == 1;
}

/**
 * 原样透传文本（不包 XKMSGE 信封）。行终止符契约：gateway 侧以 bufio.Scanner
 * 按行消费，压平后的多字段大帧若不以 \n 终结将永不吐帧（静默滞留）；
 * 调用方必须保证 str 以换行结尾（见 measurement_snapshot_d.c send_raw_flattened）。
 */
void msg_send_raw(object ob, string str)
{
    mapping result;
    mixed err;
    string owner_id;

    if (!objectp(ob) || !stringp(str) || str == "")
        return;

    owner_id = query_raw_output_owner_id(ob);
    if (!validate_raw_owner_fifo_boundary(ob, str)) {
        OWNER_SESSION_FIFO_D->reject_direct_socket_write(owner_id, "raw_text",
            "raw output boundary validation failed");
        return;
    }
    err = catch(result = OWNER_SESSION_FIFO_D->enqueue_owner_output_for_target(
        ob, owner_id, "raw_text", build_raw_owner_fifo_meta(str), str));
    if (err || !mapp(result) || result["ok"] != 1)
        OWNER_SESSION_FIFO_D->reject_direct_socket_write(owner_id, "raw_text",
            err ? sprintf("raw output enqueue failed: %O", err)
                : "raw output enqueue failed");
}

varargs void msg_send(object ob, string str, mapping response_context)
{
    mapping options;

    options = ([
        "channel": "main",
        "intent": "append",
        "priority": "normal",
        "reliability": "important",
        "display_mode": "paced",
        "payload": ([ "kind": "plain_text" ]),
    ]);
    if (mapp(response_context))
        options["response_context"] = copy(response_context);

    if (message_runtime_uses_xk(ob) && message_runtime_has_visible_text(str)) {
        if (try_emit_message_event(ob, str, options))
            return;
    }
    msg_send_raw(ob, str);
}

private varargs void emit_room_text(mixed room, string str, mixed exclude, int raw_only)
{
    object *targets;
    object target;
    object *excluded;

    if (!room || !stringp(str) || str == "")
        return;

    if (stringp(room)) {
        room = load_object(room);
        if (!room)
            return;
    }

    if (!objectp(room))
        return;

    if (objectp(exclude))
        excluded = ({ exclude });
    else if (arrayp(exclude))
        excluded = filter(exclude, (: objectp($1) :));
    else
        excluded = ({ });

    targets = all_inventory(room);
    foreach (target in targets) {
        if (!objectp(target))
            continue;
        if (member_array(target, excluded) != -1)
            continue;
        if (!interactive(target)
            && !living(target)
            && !function_exists("catch_tell", target))
            continue;

        if (raw_only)
            msg_send_raw(target, str);
        else
            msg_send(target, str);
    }
}

varargs void msg_to_room_raw(mixed room, string str, mixed exclude)
{
    emit_room_text(room, str, exclude, 1);
}

varargs void msg_to_room(mixed room, string str, mixed exclude)
{
    emit_room_text(room, str, exclude, 0);
}

varargs void msg_to_room_event(mixed room, string str, mixed exclude, mapping options)
{
    object *targets;
    object *xk_targets;
    object target;
    object daemon;
    object *excluded;
    mapping event_options;
    mixed batch_error;
    mixed emit_error;
    int batch_result;
    int emitted;
    int xk_target_count;

    if (!room || !stringp(str) || str == "")
        return;

    if (stringp(room)) {
        room = load_object(room);
        if (!room)
            return;
    }

    if (!objectp(room))
        return;

    if (objectp(exclude))
        excluded = ({ exclude });
    else if (arrayp(exclude))
        excluded = filter(exclude, (: objectp($1) :));
    else
        excluded = ({ });

    event_options = mapp(options) ? copy(options) : ([]);
    targets = all_inventory(room);

    xk_targets = allocate(sizeof(targets));
    foreach (target in targets) {
        if (!objectp(target))
            continue;
        if (member_array(target, excluded) != -1)
            continue;
        if (!interactive(target)
            && !living(target)
            && !function_exists("catch_tell", target))
            continue;

        if (message_runtime_uses_xk(target))
            xk_targets[xk_target_count++] = target;
        else
            msg_send_raw(target, str);
    }

    if (xk_target_count <= 0)
        return;
    xk_targets = xk_targets[0..xk_target_count - 1];
    daemon = query_loaded_message_daemon();
    if (objectp(daemon))
        batch_error = catch(batch_result =
            daemon->emit_room_message_event_batch(xk_targets, str, event_options));
    if (!objectp(daemon) || batch_error || !batch_result) {
        /* 逐目标回退保留事件语义；只有 emit 本身失败才退回 raw 文本保证可见。 */
        foreach (target in xk_targets) {
            emit_error = catch(emitted = MESSAGE_D->emit_message_event(target, str,
                event_options));
            if (emit_error || !emitted)
                msg_send_raw(target, str);
        }
    }
}

varargs void msg_room(mixed room, string str, mixed exclude)
{
    msg_to_room(room, str, exclude);
}

varargs void say(string str, mixed exclude)
{
    object tp;
    object env;

    tp = this_player();
    if (!stringp(str) || !tp)
        return;

    env = environment(tp);
    if (!env)
        return;

    msg_to_room(env, str, exclude ? exclude : tp);
}

void emit_room_say_ui(object speaker, string msg)
{
    object env;
    object *targets;
    object target;
    object *xk_targets;
    string speaker_name;
    string room_packet_text;
    string room_text;
    int chat_origin_ns;
    int speaker_flush_started_at;
    int speaker_flush_cpu_started_at;
    int speaker_flush_cpu_finished_at;
    int xk_target_count;

    if (!objectp(speaker) || !stringp(msg) || msg == "")
        return;

    env = environment(speaker);
    chat_origin_ns = perf_counter_ns();
    speaker_name = resolve_display_name(speaker);
    room_packet_text = speaker_name + "说道：「" + msg + "」";

    if (!message_runtime_uses_xk(speaker))
        msg_send_raw(speaker, "你说道：「" + msg + "」\n");
    else {
        speaker_flush_started_at = perf_counter_ns();
        speaker_flush_cpu_started_at = thread_cpu_time_ns();
        send_room_chat_self_ack(speaker, "say", msg, ([
            "sender_name": speaker_name,
            "direction": "self",
        ]), chat_origin_ns);
        speaker_flush_cpu_finished_at = thread_cpu_time_ns();
        CLIENT_SYNC_D->record_room_chat_command_phase_diagnostics(
            "speaker_flush", perf_counter_ns() - speaker_flush_started_at,
            speaker_flush_cpu_started_at >= 0
                && speaker_flush_cpu_finished_at >= speaker_flush_cpu_started_at
                    ? speaker_flush_cpu_finished_at - speaker_flush_cpu_started_at
                    : -1);
    }

    if (!objectp(env))
        return;

    room_text = room_packet_text + "\n";
    targets = all_inventory(env);
    xk_targets = allocate(sizeof(targets));
    foreach (target in targets) {
        if (!objectp(target) || target == speaker)
            continue;
        if (!interactive(target)
            && !living(target)
            && !function_exists("catch_tell", target))
            continue;
        if (message_runtime_uses_xk(target))
            xk_targets[xk_target_count++] = target;
        else
            msg_send_raw(target, room_text);
    }
    if (xk_target_count > 0)
        xk_send_room_chat_packets(speaker, xk_targets[0..xk_target_count - 1], "say", msg, ([
            "sender_name": speaker_name,
            "direction": "incoming",
        ]), chat_origin_ns);
}

int emit_world_shout_ui(object speaker, string msg)
{
    object *targets;
    object target;
    string speaker_name;
    string self_text;
    string self_packet_text;
    string target_text;
    string target_packet_text;
    int sender_sent;

    if (!objectp(speaker) || !stringp(msg) || msg == "")
        return 0;

    speaker_name = resolve_display_name(speaker);

    self_packet_text = "你大喊道：" + msg;
    target_packet_text = "【大喊】" + speaker_name + "：" + msg;
    self_text = HIY + self_packet_text + NOR + "\n";
    target_text = HIY + target_packet_text + NOR + "\n";

    if (!message_runtime_uses_xk(speaker)) {
        msg_send_raw(speaker, self_text);
        sender_sent = 1;
    } else
        sender_sent = xk_send_chat_packet_ex(speaker, "shout", msg, ([
            "sender_name": speaker_name,
            "direction": "self",
        ]));

    targets = users();
    foreach (target in targets) {
        if (!objectp(target) || target == speaker || !environment(target))
            continue;

        if (message_runtime_uses_xk(target))
            xk_send_chat_packet_ex(target, "shout", msg, ([
                "sender_name": speaker_name,
                "direction": "incoming",
            ]));
        else
            msg_send_raw(target, target_text);
    }
    return sender_sent ? 1 : 0;
}

void msg_broadcast(string str)
{
    object *obs;
    int i;
    int sz;

    if (!stringp(str))
        return;

    obs = users();
    for (i = 0, sz = sizeof(obs); i < sz; i++) {
        if (obs[i] && environment(obs[i]))
            msg_send(obs[i], str);
    }
}
