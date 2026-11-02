/**
 * @file    message_d.c
 * @brief   XK Message Runtime 服务端消息事件门面



 *
 * @par Description:
 * 将服务端业务输出归一为带语义、可靠性和展示意图的玩家可感知事件。
 * 本服务只构建 `MSGE` envelope 并发送，不取代现有状态包、文本包或战斗包。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>
#include <system/xk_protocol.h>

inherit "/lib/framework/daemon";

#define MESSAGE_SCHEMA_VERSION 1
#define ROOM_MESSAGE_EVENT_BATCH_LIMIT 128
#define ROOM_MESSAGE_EVENT_RECIPIENT_BUDGET 64
#define ROOM_MESSAGE_EVENT_FLUSH_WALL_BUDGET_NS 8000000
#define ROOM_MESSAGE_EVENT_COALESCE_WINDOW_US 6000
#define ROOM_MESSAGE_EVENT_CONTINUATION_DELAY_US 1000
#define ROOM_MESSAGE_EVENT_PRESSURE_RETRY_US 6000
#define ROOM_MESSAGE_EVENT_PROMOTION_SCAN_BUDGET 64
#define ROOM_MESSAGE_EVENT_MAX_HOLD_US 2000000
#define ROOM_MESSAGE_EVENT_MAX_HOLD_NS 2000000000

nosave mapping _room_message_event_entries;
nosave mapping _room_message_event_partial_entries;
nosave mapping _room_message_event_partial_head_by_target;
nosave mapping _room_message_event_partial_tail_by_target;
nosave int _room_message_event_partial_scan_head;
nosave int _room_message_event_partial_scan_tail;
nosave int _room_message_event_partial_scan_cursor;
nosave int _room_message_event_pressure_deferred;
nosave mapping _room_message_event_queue;
nosave int _room_message_event_queue_head;
nosave int _room_message_event_queue_tail;
nosave int _room_message_event_entry_serial;
nosave int _room_message_event_flush_call_out;
nosave int _room_message_event_flush_generation;
nosave int _room_message_event_flush_scheduled_at_ns;
nosave int _room_message_event_flush_due_at_ns;
nosave mapping _room_message_event_diagnostics;
nosave mapping _message_seq_by_player;
nosave mapping _message_seq_dirty_players;
nosave mapping _room_message_scope_ids;

int emit_message_event(object player, string text, mapping options);
private void send_room_message_event_entry(object player, mapping entry);
private int room_message_reservation_can_reuse(mapping entry);
private void drop_room_message_entry(mapping entry, int noninteractive);
private int release_room_message_entry_reservation(object player, mapping entry);
private int refresh_room_message_event_entry_native_state(object player,
    mapping entry);
private int schedule_room_message_event_flush(int immediate);

private string normalize_text(mixed value, string fallback)
{
    return stringp(value) && value != "" ? value : fallback;
}

private string query_player_scope_id(object player)
{
    string player_id;

    if (!objectp(player))
        return "";

    player_id = get_object_id(player);
    if (stringp(player_id) && player_id != "")
        return player_id;

    player_id = player->query("account");
    if (stringp(player_id) && player_id != "")
        return player_id;

    return file_name(player);
}

private int allocate_player_message_seq(object player, int sync_to_player)
{
    mixed seq;

    if (!objectp(player))
        return 0;

    if (!mapp(_message_seq_by_player))
        _message_seq_by_player = ([]);
    if (!mapp(_message_seq_dirty_players))
        _message_seq_dirty_players = ([]);
    seq = _message_seq_by_player[player];
    if (!intp(seq) || seq < 0)
        seq = player->query_temp("protocol/message_seq");
    if (!intp(seq) || seq < 0)
        seq = 0;
    seq += 1;
    _message_seq_by_player[player] = seq;
    if (sync_to_player) {
        player->set_temp("protocol/message_seq", seq);
        map_delete(_message_seq_dirty_players, player);
        map_delete(_message_seq_by_player, player);
    } else {
        _message_seq_dirty_players[player] = 1;
    }
    return seq;
}

int query_next_player_seq(object player)
{
    return allocate_player_message_seq(player, 1);
}

private mapping build_scope(object player, mapping options)
{
    mapping scope;

    if (mapp(options["scope"]))
        return copy(options["scope"]);

    scope = ([
        "type": normalize_text(options["scope_type"], "player"),
        "id": query_player_scope_id(player),
    ]);
    return scope;
}

private mapping build_payload_from_options(mapping options)
{
    mapping payload;
    string key;

    payload = mapp(options["payload"]) ? copy(options["payload"]) : ([]);
    foreach (key in ({ "command", "reason", "request", "code" })) {
        if (!undefinedp(options[key]))
            payload[key] = options[key];
    }
    return payload;
}

private string query_owner_output_id(object player)
{
    mapping metadata;
    string raw_id;
    mixed cached_id;
    mixed err;

    if (!objectp(player))
        return "";

    err = catch(cached_id = player->query_temp("owner/output_id"));
    if (!err && stringp(cached_id) && cached_id != "")
        return cached_id;

    err = catch(metadata = OWNER_RUNTIME_D->query_owner_metadata(player));
    if (!err && mapp(metadata) && stringp(metadata["owner_id"]) && metadata["owner_id"] != "") {
        catch(player->set_temp("owner/output_id", metadata["owner_id"]));
        return metadata["owner_id"];
    }

    raw_id = get_object_id(player);
    if (stringp(raw_id) && raw_id != "") {
        catch(player->set_temp("owner/output_id", "owner/player/" + raw_id));
        return "owner/player/" + raw_id;
    }

    catch(player->set_temp("owner/output_id", "owner/output/" + file_name(player)));
    return "owner/output/" + file_name(player);
}

private void record_owner_fifo_boundary(object player, string packet_type, string text, mapping options)
{
    mapping meta;
    string channel;
    string owner_id;
    mixed err;

    if (!objectp(player) || !stringp(text) || text == "")
        return;

    options = mapp(options) ? options : ([]);
    channel = stringp(options["channel"]) && options["channel"] != "" ? options["channel"] : "main";
    owner_id = query_owner_output_id(player);
    if (!stringp(owner_id) || owner_id == "")
        return;

    meta = ([
        "stream": channel,
        "payload_contract": "frozen_text_metadata",
        "text_length": strlen(text),
        "message_runtime_fifo_boundary": 1,
        "business_output": 1,
        "direct_socket_write": 0,
    ]);
    err = catch(OWNER_SESSION_FIFO_D->validate_owner_output_boundary(
        player, owner_id, packet_type, meta));
}

private void record_owner_fifo_batch_boundary(object player, mapping entry)
{
    mapping meta;
    string owner_id;
    mixed err;
    int count;
    int text_length;

    if (!objectp(player) || !mapp(entry))
        return;
    count = intp(entry["event_count"]) ? entry["event_count"] : 0;
    text_length = intp(entry["text_length_total"])
        ? entry["text_length_total"] : 0;
    if (count <= 0 || text_length <= 0)
        return;
    owner_id = query_owner_output_id(player);
    if (!stringp(owner_id) || owner_id == "")
        return;
    meta = ([
        "stream": "message",
        "payload_contract": "preencoded_room_message_batch",
        "message_count": count,
        "text_length": text_length,
        "message_runtime_fifo_boundary": 1,
        "business_output": 1,
        "direct_socket_write": 0,
    ]);
    err = catch(OWNER_SESSION_FIFO_D->validate_owner_output_boundary(
        player, owner_id, "message_event_batch", meta));
}

/*
 * 内联动作解析：文本中的 [标签|cmd] 提取为 inline_actions，
 * 文本原位替换为标签（纯文本客户端直接显示标签，协议客户端渲染为按钮）。
 * 语法： [偷听|listen tea_drinkers]  —— 标签在前，命令在后，竖线分隔。
 */
private mapping parse_inline_actions(string text)
{
    mapping *actions;
    string *parts;
    string result;
    string label;
    string cmd;
    string rest;
    string after;
    int i;
    int leading_delim;

    actions = ({ });
    if (!stringp(text) || strsrch(text, "[") == -1)
        return ([ "text": stringp(text) ? text : "", "actions": actions ]);

    /* explode 跳过空段：文本以 "[" 开头时 parts[0] 已是首个 "[…]" 的内容，
     * 不能当作前缀，否则行首 "[" 被永久吞掉、行首的内联动作也永远不会被
     * 解析。 */
    leading_delim = strsrch(text, "[") == 0;
    parts = explode(text, "[");
    result = leading_delim ? "" : parts[0];
    for (i = leading_delim ? 0 : 1; i < sizeof(parts); i++) {
        string segment;

        segment = parts[i];
        if (sscanf(segment, "%s|%s", label, rest) == 2
            && sscanf(rest, "%s]%s", cmd, after) == 2
            && label != "" && cmd != "") {
            actions += ({ ([ "label": label, "cmd": cmd ]) });
            result += label + after;
        } else {
            result += "[" + segment;
        }
    }
    return ([ "text": result, "actions": actions ]);
}

private mapping build_stable_envelope(string text, mapping options)
{
    mapping envelope;
    mapping parsed;

    options = mapp(options) ? options : ([]);
    parsed = parse_inline_actions(text);
    envelope = ([
        "schema_version": MESSAGE_SCHEMA_VERSION,
        "channel": normalize_text(options["channel"], "main"),
        "intent": normalize_text(options["intent"], "append"),
        "priority": normalize_text(options["priority"], "normal"),
        "reliability": normalize_text(options["reliability"], "important"),
        "display_mode": normalize_text(options["display_mode"], "paced"),
        "ttl_ms": intp(options["ttl_ms"]) && options["ttl_ms"] > 0 ? options["ttl_ms"] : 30000,
        "collapse_key": stringp(options["collapse_key"]) ? options["collapse_key"] : "",
        "text": parsed["text"],
        "payload": build_payload_from_options(options),
    ]);
    if (sizeof(parsed["actions"]) > 0)
        envelope["inline_actions"] = parsed["actions"];
    if (stringp(options["id"]) && options["id"] != "")
        envelope["id"] = options["id"];
    if (mapp(options["scope"]))
        envelope["scope"] = copy(options["scope"]);
    if (stringp(options["causation_id"]) && options["causation_id"] != "")
        envelope["causation_id"] = options["causation_id"];
    if (stringp(options["correlation_id"]) && options["correlation_id"] != "")
        envelope["correlation_id"] = options["correlation_id"];
    return envelope;
}

private mapping build_envelope(object player, string text, mapping options)
{
    mapping envelope;
    int seq;
    int timestamp;

    options = mapp(options) ? options : ([]);
    seq = intp(options["seq"]) && options["seq"] > 0
        ? options["seq"]
        : query_next_player_seq(player);
    timestamp = intp(options["timestamp"]) && options["timestamp"] > 0
        ? options["timestamp"] : time();
    envelope = build_stable_envelope(text, options);
    if (!stringp(envelope["id"]) || envelope["id"] == "")
        envelope["id"] = sprintf("msg_%d_%d", timestamp, seq);
    envelope["seq"] = seq;
    if (!mapp(envelope["scope"]))
        envelope["scope"] = build_scope(player, options);
    if (!stringp(envelope["causation_id"]) || envelope["causation_id"] == "")
        envelope["causation_id"] = sprintf("cmd_%d", seq);
    if (!stringp(envelope["correlation_id"]) || envelope["correlation_id"] == "")
        envelope["correlation_id"] = sprintf("txn_%d", seq);
    envelope["timestamp"] = timestamp;
    return envelope;
}

private mapping preencode_room_message_event_template(string text,
    mapping options)
{
    mapping stable_envelope;
    mixed encode_error;
    string stable_json;

    stable_envelope = build_stable_envelope(text, options);
    encode_error = catch(stable_json =
        json_encode_frozen_or_zero(stable_envelope));
    if (encode_error || !stringp(stable_json) || stable_json == "") {
        encode_error = catch(stable_json = json_encode(stable_envelope));
        if (encode_error || !stringp(stable_json) || stable_json == "")
            return 0;
    }
    return ([
        "stable_json": stable_json,
        "scope_type": normalize_text(options["scope_type"], "player"),
    ]);
}

private mapping freeze_room_message_event_options(mapping options)
{
    mapping frozen;

    frozen = mapp(options) ? copy(options) : ([]);
    if (mapp(frozen["payload"]))
        frozen["payload"] = copy(frozen["payload"]);
    if (mapp(frozen["scope"]))
        frozen["scope"] = copy(frozen["scope"]);
    return frozen;
}

private mapping new_room_message_event_wave(string text, mapping options,
    mapping preencoded, int sent_at)
{
    if (!stringp(text) || text == ""
        || !mapp(options) || !mapp(preencoded)
        || !stringp(preencoded["stable_json"])
        || preencoded["stable_json"] == ""
        || !stringp(preencoded["scope_type"])
        || preencoded["scope_type"] == ""
        || sent_at <= 0)
        return 0;
    return ([
        "text": text,
        "options": options,
        "stable_json": preencoded["stable_json"],
        "scope_type": preencoded["scope_type"],
        "sent_at": sent_at,
    ]);
}

private int room_message_event_diagnostic_int(string key)
{
    return mapp(_room_message_event_diagnostics)
        && intp(_room_message_event_diagnostics[key])
            ? _room_message_event_diagnostics[key] : 0;
}

private void add_room_message_event_diagnostic(string key, int amount)
{
    if (!stringp(key) || key == "" || amount <= 0)
        return;
    if (!mapp(_room_message_event_diagnostics))
        _room_message_event_diagnostics = ([]);
    _room_message_event_diagnostics[key] =
        room_message_event_diagnostic_int(key) + amount;
}

private void set_room_message_event_diagnostic_max(string key, int value)
{
    if (!stringp(key) || key == "" || value <= 0)
        return;
    if (!mapp(_room_message_event_diagnostics))
        _room_message_event_diagnostics = ([]);
    if (value > room_message_event_diagnostic_int(key))
        _room_message_event_diagnostics[key] = value;
}

private int query_next_room_message_seq(object player)
{
    if (mapp(_message_seq_by_player)
        && intp(_message_seq_by_player[player])
        && _message_seq_by_player[player] >= 0)
        add_room_message_event_diagnostic(
            "room_message_event_player_seq_cache_hits", 1);
    else
        add_room_message_event_diagnostic(
            "room_message_event_player_seq_cache_misses", 1);
    return allocate_player_message_seq(player, 0);
}

private int sync_player_message_seq(object player)
{
    int seq;

    if (!mapp(_message_seq_dirty_players)
        || !_message_seq_dirty_players[player])
        return 1;
    if (!objectp(player) || !mapp(_message_seq_by_player)
        || !intp(_message_seq_by_player[player])
        || _message_seq_by_player[player] <= 0) {
        if (mapp(_message_seq_dirty_players))
            map_delete(_message_seq_dirty_players, player);
        if (mapp(_message_seq_by_player))
            map_delete(_message_seq_by_player, player);
        return 0;
    }
    seq = _message_seq_by_player[player];
    player->set_temp("protocol/message_seq", seq);
    map_delete(_message_seq_dirty_players, player);
    map_delete(_message_seq_by_player, player);
    add_room_message_event_diagnostic(
        "room_message_event_player_seq_syncs", 1);
    return 1;
}

private string query_cached_room_message_scope_id(object player)
{
    string scope_id;

    if (!objectp(player))
        return "";
    if (!mapp(_room_message_scope_ids))
        _room_message_scope_ids = ([]);
    scope_id = _room_message_scope_ids[player];
    if (stringp(scope_id) && scope_id != "")
        return scope_id;
    scope_id = query_player_scope_id(player);
    if (stringp(scope_id) && scope_id != "")
        _room_message_scope_ids[player] = scope_id;
    return stringp(scope_id) ? scope_id : "";
}

private void ensure_room_message_event_queue()
{
    if (!mapp(_room_message_event_entries))
        _room_message_event_entries = ([]);
    if (!mapp(_room_message_event_partial_entries))
        _room_message_event_partial_entries = ([]);
    if (!mapp(_room_message_event_partial_head_by_target))
        _room_message_event_partial_head_by_target = ([]);
    if (!mapp(_room_message_event_partial_tail_by_target))
        _room_message_event_partial_tail_by_target = ([]);
    if (!mapp(_room_message_event_queue))
        _room_message_event_queue = ([]);
    if (!mapp(_message_seq_by_player))
        _message_seq_by_player = ([]);
    if (!mapp(_message_seq_dirty_players))
        _message_seq_dirty_players = ([]);
    if (!mapp(_room_message_scope_ids))
        _room_message_scope_ids = ([]);
    if (_room_message_event_queue_head <= 0)
        _room_message_event_queue_head = 1;
    if (_room_message_event_queue_tail <= 0)
        _room_message_event_queue_tail = 1;
}

private void reset_room_message_event_queue_if_empty()
{
    if (_room_message_event_queue_head < _room_message_event_queue_tail)
        return;
    _room_message_event_queue = ([]);
    _room_message_event_queue_head = 1;
    _room_message_event_queue_tail = 1;
    if (mapp(_room_message_event_partial_entries)
        && sizeof(_room_message_event_partial_entries) > 0)
        return;
    _room_message_event_entries = ([]);
    _room_message_event_partial_entries = ([]);
    _room_message_event_partial_head_by_target = ([]);
    _room_message_event_partial_tail_by_target = ([]);
    _room_message_event_partial_scan_head = 0;
    _room_message_event_partial_scan_tail = 0;
    _room_message_event_partial_scan_cursor = 0;
    _room_message_event_pressure_deferred = 0;
    _room_message_scope_ids = ([]);
}

private void link_room_message_event_partial_entry(mapping entry)
{
    mapping previous;
    mapping reservation;
    mapping scan_previous;
    string target_path;
    int previous_serial;
    int scan_previous_serial;
    int serial;

    if (!mapp(entry) || !intp(entry["serial"]) || entry["serial"] <= 0
        || !mapp(entry["reservation"]))
        return;
    ensure_room_message_event_queue();
    serial = entry["serial"];
    reservation = entry["reservation"];
    target_path = reservation["target_path"];
    if (!stringp(target_path) || target_path == "")
        return;
    previous_serial = intp(_room_message_event_partial_tail_by_target[target_path])
        ? _room_message_event_partial_tail_by_target[target_path] : 0;
    previous = previous_serial > 0
        ? _room_message_event_partial_entries[previous_serial] : 0;
    entry["partial_target_path"] = target_path;
    entry["partial_previous_serial"] = mapp(previous) ? previous_serial : 0;
    entry["partial_next_serial"] = 0;
    if (mapp(previous))
        previous["partial_next_serial"] = serial;
    else
        _room_message_event_partial_head_by_target[target_path] = serial;
    _room_message_event_partial_tail_by_target[target_path] = serial;

    scan_previous_serial = _room_message_event_partial_scan_tail;
    scan_previous = scan_previous_serial > 0
        ? _room_message_event_partial_entries[scan_previous_serial] : 0;
    entry["partial_scan_previous_serial"] = mapp(scan_previous)
        ? scan_previous_serial : 0;
    entry["partial_scan_next_serial"] = 0;
    if (mapp(scan_previous))
        scan_previous["partial_scan_next_serial"] = serial;
    else
        _room_message_event_partial_scan_head = serial;
    _room_message_event_partial_scan_tail = serial;
    if (_room_message_event_partial_scan_cursor <= 0)
        _room_message_event_partial_scan_cursor = serial;
    _room_message_event_partial_entries[serial] = entry;
}

private void unlink_room_message_event_partial_entry(mapping entry)
{
    mapping next_entry;
    mapping previous_entry;
    mapping scan_next_entry;
    mapping scan_previous_entry;
    string target_path;
    int next_serial;
    int previous_serial;
    int scan_next_serial;
    int scan_previous_serial;
    int serial;

    if (!mapp(entry) || !intp(entry["serial"]) || entry["serial"] <= 0)
        return;
    serial = entry["serial"];
    if (!mapp(_room_message_event_partial_entries)
        || _room_message_event_partial_entries[serial] != entry)
        return;
    target_path = entry["partial_target_path"];
    previous_serial = intp(entry["partial_previous_serial"])
        ? entry["partial_previous_serial"] : 0;
    next_serial = intp(entry["partial_next_serial"])
        ? entry["partial_next_serial"] : 0;
    previous_entry = previous_serial > 0
        ? _room_message_event_partial_entries[previous_serial] : 0;
    next_entry = next_serial > 0
        ? _room_message_event_partial_entries[next_serial] : 0;
    scan_previous_serial = intp(entry["partial_scan_previous_serial"])
        ? entry["partial_scan_previous_serial"] : 0;
    scan_next_serial = intp(entry["partial_scan_next_serial"])
        ? entry["partial_scan_next_serial"] : 0;
    scan_previous_entry = scan_previous_serial > 0
        ? _room_message_event_partial_entries[scan_previous_serial] : 0;
    scan_next_entry = scan_next_serial > 0
        ? _room_message_event_partial_entries[scan_next_serial] : 0;
    map_delete(_room_message_event_partial_entries, serial);
    if (mapp(previous_entry))
        previous_entry["partial_next_serial"] = next_serial;
    else if (stringp(target_path)
        && _room_message_event_partial_head_by_target[target_path] == serial) {
        if (next_serial > 0)
            _room_message_event_partial_head_by_target[target_path] = next_serial;
        else
            map_delete(_room_message_event_partial_head_by_target, target_path);
    }
    if (mapp(next_entry))
        next_entry["partial_previous_serial"] = previous_serial;
    else if (stringp(target_path)
        && _room_message_event_partial_tail_by_target[target_path] == serial) {
        if (previous_serial > 0)
            _room_message_event_partial_tail_by_target[target_path] = previous_serial;
        else
            map_delete(_room_message_event_partial_tail_by_target, target_path);
    }
    if (stringp(target_path)
        && undefinedp(_room_message_event_partial_head_by_target[target_path]))
        map_delete(_room_message_event_partial_tail_by_target, target_path);

    if (mapp(scan_previous_entry))
        scan_previous_entry["partial_scan_next_serial"] = scan_next_serial;
    else if (_room_message_event_partial_scan_head == serial)
        _room_message_event_partial_scan_head = scan_next_serial;
    if (mapp(scan_next_entry))
        scan_next_entry["partial_scan_previous_serial"] = scan_previous_serial;
    else if (_room_message_event_partial_scan_tail == serial)
        _room_message_event_partial_scan_tail = scan_previous_serial;
    if (_room_message_event_partial_scan_cursor == serial)
        _room_message_event_partial_scan_cursor = scan_next_serial > 0
            ? scan_next_serial : _room_message_event_partial_scan_head;
    if (_room_message_event_partial_scan_head <= 0) {
        _room_message_event_partial_scan_tail = 0;
        _room_message_event_partial_scan_cursor = 0;
    } else if (_room_message_event_partial_scan_cursor <= 0
        || !mapp(_room_message_event_partial_entries[
            _room_message_event_partial_scan_cursor]))
        _room_message_event_partial_scan_cursor =
            _room_message_event_partial_scan_head;
    map_delete(entry, "partial_target_path");
    map_delete(entry, "partial_previous_serial");
    map_delete(entry, "partial_next_serial");
    map_delete(entry, "partial_scan_previous_serial");
    map_delete(entry, "partial_scan_next_serial");
}

private void register_room_message_event_entry(object player, mapping entry)
{
    int serial;

    if (!objectp(player) || !mapp(entry))
        return;
    ensure_room_message_event_queue();
    serial = ++_room_message_event_entry_serial;
    entry["player"] = player;
    entry["serial"] = serial;
    entry["ticket"] = 0;
    entry["active"] = 1;
    entry["ready"] = 0;
    _room_message_event_entries[player] = entry;
    link_room_message_event_partial_entry(entry);
}

private int enqueue_room_message_event_entry(object player, mapping entry,
    string reason)
{
    int ticket;

    if (!mapp(entry) || !entry["active"] || entry["ready"])
        return 0;
    ensure_room_message_event_queue();
    unlink_room_message_event_partial_entry(entry);
    if (objectp(player) && _room_message_event_entries[player] == entry)
        map_delete(_room_message_event_entries, player);
    ticket = _room_message_event_queue_tail++;
    _room_message_event_queue[ticket] = ({ player, entry });
    entry["ticket"] = ticket;
    entry["ready"] = 1;
    entry["ready_reason"] = reason;
    if (reason == "batch_full")
        add_room_message_event_diagnostic(
            "room_message_event_full_ready_recipients", 1);
    else if (reason == "producer_idle")
        add_room_message_event_diagnostic(
            "room_message_event_idle_ready_recipients", 1);
    else if (reason == "max_hold")
        add_room_message_event_diagnostic(
            "room_message_event_max_hold_ready_recipients", 1);
    else if (reason == "actor_fifo_ready_successor")
        add_room_message_event_diagnostic(
            "room_message_event_fifo_ready_successor_recipients", 1);
    else if (reason == "schedule_failed")
        add_room_message_event_diagnostic(
            "room_message_event_schedule_failure_forced_recipients", 1);
    else if (reason == "native_state_missing")
        add_room_message_event_diagnostic(
            "room_message_event_native_state_missing_recipients", 1);
    else if (reason == "native_recovery")
        add_room_message_event_diagnostic(
            "room_message_event_native_recovery_recipients", 1);
    return 1;
}

private void remove_room_message_event_entry_from_queue(mapping entry)
{
    mixed *queued;
    object player;
    int ticket;

    if (!mapp(entry))
        return;
    player = entry["player"];
    ticket = intp(entry["ticket"]) ? entry["ticket"] : 0;
    unlink_room_message_event_partial_entry(entry);
    entry["active"] = 0;
    entry["ready"] = 0;
    if (objectp(player) && mapp(_room_message_event_entries)
        && _room_message_event_entries[player] == entry)
        map_delete(_room_message_event_entries, player);
    if (ticket > 0 && mapp(_room_message_event_queue)) {
        queued = _room_message_event_queue[ticket];
        if (arrayp(queued) && sizeof(queued) >= 2 && queued[1] == entry)
            map_delete(_room_message_event_queue, ticket);
    }
    entry["ticket"] = 0;
    while (_room_message_event_queue_tail > _room_message_event_queue_head
        && !arrayp(_room_message_event_queue[
            _room_message_event_queue_tail - 1]))
        _room_message_event_queue_tail--;
    while (_room_message_event_queue_head < _room_message_event_queue_tail
        && !arrayp(_room_message_event_queue[
            _room_message_event_queue_head]))
        _room_message_event_queue_head++;
    reset_room_message_event_queue_if_empty();
}

private mapping new_native_room_message_event_entry(object player,
    int reservation_id, int slot_server_seq, int sent_at, int text_length,
    int created_at_ns)
{
    mapping entry;
    string scope_id;

    if (!objectp(player) || reservation_id <= 0 || slot_server_seq <= 0
        || sent_at <= 0 || text_length <= 0 || created_at_ns <= 0)
        return 0;
    scope_id = query_cached_room_message_scope_id(player);
    if (!stringp(scope_id) || scope_id == "")
        return 0;
    entry = ([
        "event_count": 1,
        "text_length_total": text_length,
        "scope_id": scope_id,
        "created_at_ns": created_at_ns,
        "last_append_at_ns": created_at_ns,
        "reservation": ([
            "ok": 1,
            "reservation_id": reservation_id,
            "target_path": file_name(player),
            "target_owner_epoch": vm_owner_epoch(player),
            "slot_server_seq": slot_server_seq,
            "slot_sent_at": sent_at,
            "raw_gateway_fifo_reserved": 1,
        ]),
    ]);
    register_room_message_event_entry(player, entry);
    if (!mapp(_room_message_event_partial_entries)
        || _room_message_event_partial_entries[entry["serial"]] != entry) {
        remove_room_message_event_entry_from_queue(entry);
        return 0;
    }
    return entry;
}

private int room_message_reservation_can_reuse(mapping entry)
{
    mapping reservation;
    int count;

    if (!mapp(entry) || !entry["active"] || entry["ready"])
        return 0;
    count = intp(entry["event_count"]) ? entry["event_count"] : 0;
    reservation = entry["reservation"];
    return count > 0 && count < ROOM_MESSAGE_EVENT_BATCH_LIMIT
        && intp(entry["serial"]) && entry["serial"] > 0
        && mapp(_room_message_event_partial_entries)
        && _room_message_event_partial_entries[entry["serial"]] == entry
        && mapp(reservation) && reservation["ok"] == 1
        && intp(reservation["reservation_id"])
        && reservation["reservation_id"] > 0
        && !reservation["terminal"];
}

private int room_message_entry_has_earlier_partial(mapping entry)
{
    string target_path;
    int head_serial;
    int serial;

    if (!mapp(entry) || !intp(entry["serial"]) || entry["serial"] <= 0)
        return 0;
    serial = entry["serial"];
    target_path = entry["partial_target_path"];
    if (!stringp(target_path) || target_path == "")
        return 0;
    head_serial = intp(_room_message_event_partial_head_by_target[target_path])
        ? _room_message_event_partial_head_by_target[target_path] : 0;
    return head_serial > 0 && head_serial != serial;
}

private mixed *take_room_message_event_recipient()
{
    mapping entry;
    mixed *queued;
    object player;
    int ticket;

    ensure_room_message_event_queue();
    while (_room_message_event_queue_head < _room_message_event_queue_tail) {
        ticket = _room_message_event_queue_head++;
        queued = _room_message_event_queue[ticket];
        map_delete(_room_message_event_queue, ticket);
        if (!arrayp(queued) || sizeof(queued) < 2 || !mapp(queued[1]))
            continue;
        player = queued[0];
        entry = queued[1];
        if (!entry["active"] || !entry["ready"]
            || entry["ticket"] != ticket)
            continue;
        entry["active"] = 0;
        entry["ready"] = 0;
        if (!objectp(player)) {
            reset_room_message_event_queue_if_empty();
            return ({ 0, entry });
        }
        if (mapp(_room_message_event_entries)
            && _room_message_event_entries[player] == entry)
            map_delete(_room_message_event_entries, player);
        reset_room_message_event_queue_if_empty();
        return ({ player, entry });
    }
    reset_room_message_event_queue_if_empty();
    return ({ });
}

private int room_message_event_queue_pending()
{
    return mapp(_room_message_event_queue)
        && _room_message_event_queue_head < _room_message_event_queue_tail;
}

private int room_message_event_partial_pending()
{
    return mapp(_room_message_event_partial_entries)
        && sizeof(_room_message_event_partial_entries) > 0;
}

private int room_message_event_pressure_pending()
{
    int buffered_input_pending;
    int command_pending;
    int main_queue_pending;

    command_pending = gateway_command_pending();
    main_queue_pending = gateway_main_queue_pending();
    buffered_input_pending = gateway_buffered_input_pending();
    if (command_pending <= 0 && main_queue_pending <= 0
        && buffered_input_pending <= 0)
        return 0;
    add_room_message_event_diagnostic(
        "room_message_event_pressure_deferrals", 1);
    if (command_pending > 0)
        add_room_message_event_diagnostic(
            "room_message_event_command_pressure_deferrals", 1);
    if (main_queue_pending > 0)
        add_room_message_event_diagnostic(
            "room_message_event_main_queue_pressure_deferrals", 1);
    if (buffered_input_pending > 0)
        add_room_message_event_diagnostic(
            "room_message_event_buffered_input_pressure_deferrals", 1);
    return 1;
}

private int promote_due_room_message_event_entries(int now_ns)
{
    mapping entry;
    object player;
    int count;
    int created_at;
    int elapsed;
    int examined;
    int last_append_at;
    int next_serial;
    int pressure_pending;
    int promoted;
    int scan_cursor_wraps;
    int scan_limit;
    int scan_started_at;
    int serial;

    if (now_ns <= 0 || !room_message_event_partial_pending())
        return 0;
    pressure_pending = room_message_event_pressure_pending();
    _room_message_event_pressure_deferred = pressure_pending;
    serial = _room_message_event_partial_scan_head;
    entry = serial > 0 ? _room_message_event_partial_entries[serial] : 0;
    created_at = mapp(entry) && intp(entry["created_at_ns"])
        ? entry["created_at_ns"] : 0;
    if (pressure_pending && created_at > 0
        && now_ns - created_at < ROOM_MESSAGE_EVENT_MAX_HOLD_NS)
        return 0;
    scan_limit = sizeof(_room_message_event_partial_entries);
    if (scan_limit > ROOM_MESSAGE_EVENT_PROMOTION_SCAN_BUDGET)
        scan_limit = ROOM_MESSAGE_EVENT_PROMOTION_SCAN_BUDGET;
    scan_started_at = perf_counter_ns();
    serial = _room_message_event_partial_scan_cursor;
    if (serial <= 0 || !mapp(_room_message_event_partial_entries[serial]))
        serial = _room_message_event_partial_scan_head;
    while (examined < ROOM_MESSAGE_EVENT_PROMOTION_SCAN_BUDGET
        && examined < scan_limit && serial > 0) {
        entry = _room_message_event_partial_entries[serial];
        if (!mapp(entry))
            break;
        next_serial = intp(entry["partial_scan_next_serial"])
            ? entry["partial_scan_next_serial"] : 0;
        if (next_serial <= 0) {
            next_serial = _room_message_event_partial_scan_head;
            scan_cursor_wraps++;
        }
        _room_message_event_partial_scan_cursor = next_serial;
        examined++;
        if (!entry["active"] || entry["ready"]) {
            unlink_room_message_event_partial_entry(entry);
            serial = _room_message_event_partial_scan_cursor;
            continue;
        }
        player = entry["player"];
        if (!objectp(player)
            || !refresh_room_message_event_entry_native_state(player, entry)) {
            promoted += enqueue_room_message_event_entry(
                player, entry, "native_state_missing");
            serial = _room_message_event_partial_scan_cursor;
            continue;
        }
        count = intp(entry["event_count"]) ? entry["event_count"] : 0;
        created_at = intp(entry["created_at_ns"])
            ? entry["created_at_ns"] : 0;
        last_append_at = intp(entry["last_append_at_ns"])
            ? entry["last_append_at_ns"] : 0;
        if (count >= ROOM_MESSAGE_EVENT_BATCH_LIMIT) {
            if (room_message_entry_has_earlier_partial(entry))
                add_room_message_event_diagnostic(
                    "room_message_event_full_fifo_blocked_recipients", 1);
            promoted += enqueue_room_message_event_entry(
                player, entry, "batch_full");
        } else if (created_at > 0
            && now_ns - created_at >= ROOM_MESSAGE_EVENT_MAX_HOLD_NS) {
            promoted += enqueue_room_message_event_entry(
                player, entry, "max_hold");
        } else if (!pressure_pending && last_append_at > 0
            && now_ns - last_append_at
                >= ROOM_MESSAGE_EVENT_COALESCE_WINDOW_US * 1000) {
            promoted += enqueue_room_message_event_entry(
                player, entry, "producer_idle");
        }
        serial = _room_message_event_partial_scan_cursor;
    }
    elapsed = (perf_counter_ns() - scan_started_at) / 1000;
    add_room_message_event_diagnostic(
        "room_message_event_promotion_scan_callbacks", 1);
    add_room_message_event_diagnostic(
        "room_message_event_promotion_scan_entries_total", examined);
    set_room_message_event_diagnostic_max(
        "room_message_event_promotion_scan_entries_max", examined);
    add_room_message_event_diagnostic(
        "room_message_event_promotion_scan_cpu_total_us", elapsed);
    set_room_message_event_diagnostic_max(
        "room_message_event_promotion_scan_cpu_max_us", elapsed);
    if (scan_cursor_wraps > 0)
        add_room_message_event_diagnostic(
            "room_message_event_promotion_scan_cursor_wraps",
            scan_cursor_wraps);
    return promoted;
}

int promote_actor_room_message_fifo_debt(object player)
{
    mapping entry;
    mapping reservation;
    mixed check_error;
    string target_path;
    int checked;
    int has_ready_successor;
    int next_serial;
    int promoted;
    int serial;

    if (!objectp(player))
        return 0;
    ensure_room_message_event_queue();
    target_path = file_name(player);
    serial = intp(_room_message_event_partial_head_by_target[target_path])
        ? _room_message_event_partial_head_by_target[target_path] : 0;
    while (serial > 0 && checked < ROOM_MESSAGE_EVENT_PROMOTION_SCAN_BUDGET) {
        entry = _room_message_event_partial_entries[serial];
        if (!mapp(entry))
            break;
        next_serial = intp(entry["partial_next_serial"])
            ? entry["partial_next_serial"] : 0;
        checked++;
        if (entry["active"] && !entry["ready"]
            && entry["player"] == player) {
            reservation = entry["reservation"];
            if (mapp(reservation)
                && intp(reservation["reservation_id"])
                && reservation["reservation_id"] > 0) {
                add_room_message_event_diagnostic(
                    "room_message_event_fifo_ready_successor_checks", 1);
                check_error = catch(has_ready_successor =
                    gateway_session_pending_reservation_has_ready_successor(
                        player, reservation["reservation_id"]));
                if (!check_error && has_ready_successor)
                    promoted += enqueue_room_message_event_entry(
                        player, entry, "actor_fifo_ready_successor");
            }
        }
        serial = next_serial;
    }
    if (promoted > 0)
        schedule_room_message_event_flush(1);
    return promoted;
}

private void promote_all_room_message_event_entries(string reason)
{
    mapping entry;
    object player;
    int serial;

    if (!room_message_event_partial_pending())
        return;
    while (room_message_event_partial_pending()) {
        serial = _room_message_event_partial_scan_head;
        entry = _room_message_event_partial_entries[serial];
        if (!mapp(entry))
            break;
        if (!entry["active"] || entry["ready"]) {
            unlink_room_message_event_partial_entry(entry);
            continue;
        }
        player = entry["player"];
        if (!enqueue_room_message_event_entry(player, entry, reason))
            unlink_room_message_event_partial_entry(entry);
    }
}

private int query_room_message_event_next_due_ns(int now_ns)
{
    mapping entry;
    int created_at_ns;
    int max_hold_due_at_ns;
    int serial;
    int target_ns;

    if (!room_message_event_partial_pending())
        return 0;
    target_ns = now_ns + ROOM_MESSAGE_EVENT_PRESSURE_RETRY_US * 1000;
    serial = _room_message_event_partial_scan_head;
    entry = serial > 0 ? _room_message_event_partial_entries[serial] : 0;
    created_at_ns = mapp(entry) && intp(entry["created_at_ns"])
        ? entry["created_at_ns"] : 0;
    if (created_at_ns > 0) {
        max_hold_due_at_ns = created_at_ns + ROOM_MESSAGE_EVENT_MAX_HOLD_NS;
        if (_room_message_event_pressure_deferred
            || max_hold_due_at_ns < target_ns)
            target_ns = max_hold_due_at_ns;
    }
    return target_ns;
}

private void drain_room_message_event_queue_now()
{
    mapping entry;
    mixed *pending;
    object player;

    while (room_message_event_queue_pending()) {
        pending = take_room_message_event_recipient();
        if (!arrayp(pending) || sizeof(pending) < 2)
            break;
        player = pending[0];
        entry = pending[1];
        send_room_message_event_entry(player, entry);
        add_room_message_event_diagnostic(
            "room_message_event_recipients_processed", 1);
    }
}

private int schedule_room_message_event_flush(int immediate)
{
    mixed err;
    float delay_seconds;
    int now_ns;
    int scheduled;
    int scheduled_at;
    int target_ns;

    ensure_room_message_event_queue();
    if (!room_message_event_queue_pending()
        && !room_message_event_partial_pending())
        return 1;
    now_ns = perf_counter_ns();
    if (room_message_event_queue_pending())
        target_ns = now_ns + (immediate
            ? 0 : ROOM_MESSAGE_EVENT_CONTINUATION_DELAY_US * 1000);
    else
        target_ns = query_room_message_event_next_due_ns(now_ns);
    if (target_ns <= 0)
        target_ns = now_ns;
    if (_room_message_event_flush_call_out) {
        if (_room_message_event_flush_due_at_ns > 0
            && _room_message_event_flush_due_at_ns <= target_ns)
            return 1;
        catch(remove_call_out(_room_message_event_flush_call_out));
        _room_message_event_flush_call_out = 0;
    }
    _room_message_event_flush_generation++;
    scheduled_at = now_ns;
    _room_message_event_flush_scheduled_at_ns = scheduled_at;
    _room_message_event_flush_due_at_ns = target_ns;
    delay_seconds = target_ns > now_ns
        ? (target_ns - now_ns) / 1000000000.0 : 0.0;
    err = catch(scheduled = call_out_walltime_gateway(
        "flush_room_message_event_batches",
        delay_seconds,
        _room_message_event_flush_generation));
    if (!err && scheduled > 0) {
        _room_message_event_flush_call_out = scheduled;
        return 1;
    }

    _room_message_event_flush_call_out = 0;
    _room_message_event_flush_scheduled_at_ns = 0;
    _room_message_event_flush_due_at_ns = 0;
    promote_all_room_message_event_entries("schedule_failed");
    drain_room_message_event_queue_now();
    return !room_message_event_queue_pending()
        && !room_message_event_partial_pending();
}

private int fill_room_message_event_entry(object player, mapping entry)
{
    mapping reservation;
    mapping result;
    mixed send_error;
    string scope_id;
    string outcome;
    int delivered;
    int elapsed;
    int prepared_at;
    int started_at;
    int count;

    if (!objectp(player) || !interactive(player) || !mapp(entry)
        || !refresh_room_message_event_entry_native_state(player, entry))
        return 0;
    started_at = perf_counter_ns();
    count = intp(entry["event_count"]) ? entry["event_count"] : 0;
    reservation = entry["reservation"];
    if (!mapp(reservation) || count <= 0
        || !intp(entry["text_length_total"])
        || entry["text_length_total"] <= 0) {
        add_room_message_event_diagnostic(
            "room_message_event_invalid_entry_recipients", 1);
        return 0;
    }
    scope_id = query_player_scope_id(player);
    if (!stringp(scope_id) || scope_id == "") {
        add_room_message_event_diagnostic(
            "room_message_event_invalid_entry_recipients", 1);
        return 0;
    }
    prepared_at = perf_counter_ns();
    elapsed = (prepared_at - started_at) / 1000;
    add_room_message_event_diagnostic(
        "room_message_event_native_prepare_samples", 1);
    add_room_message_event_diagnostic(
        "room_message_event_native_prepare_total_us", elapsed);
    set_room_message_event_diagnostic_max(
        "room_message_event_native_prepare_max_us", elapsed);

    send_error = catch(result = call_other(
        "/kernel/framework/xk_protocol_core",
        "xk_fill_reserved_native_room_message_events", player,
        scope_id, count, reservation));
    elapsed = (perf_counter_ns() - prepared_at) / 1000;
    add_room_message_event_diagnostic(
        "room_message_event_native_fill_samples", 1);
    add_room_message_event_diagnostic(
        "room_message_event_native_fill_total_us", elapsed);
    set_room_message_event_diagnostic_max(
        "room_message_event_native_fill_max_us", elapsed);
    add_room_message_event_diagnostic(
        "room_message_event_native_fill_batch_samples", 1);
    add_room_message_event_diagnostic(
        "room_message_event_native_fill_batch_total_us", elapsed);
    set_room_message_event_diagnostic_max(
        "room_message_event_native_fill_batch_max_us", elapsed);
    add_room_message_event_diagnostic(
        "room_message_event_native_fill_batch_recipients_total", 1);
    set_room_message_event_diagnostic_max(
        "room_message_event_native_fill_batch_recipients_max", 1);
    if (send_error || !mapp(result)) {
        outcome = "native_fill_failed";
    } else {
        outcome = stringp(result["outcome"])
            ? result["outcome"] : "native_fill_failed";
        if (result["terminal"])
            reservation["terminal"] = 1;
    }
    if (member_array(outcome, ({
            "invalid_entry", "target_identity_changed", "promotion_failed",
            "native_fill_failed", "completion_failed"
        })) == -1 && outcome != "filled")
        outcome = "native_fill_failed";
    if (outcome != "filled")
        add_room_message_event_diagnostic(
            "room_message_event_" + outcome + "_recipients", 1);
    if (!send_error && mapp(result) && result["released"])
        add_room_message_event_diagnostic(
            "room_message_event_released_recipients", 1);
    delivered = !send_error && mapp(result) && result["ok"] == 1;
    if (delivered && count == 1) {
        add_room_message_event_diagnostic(
            "room_message_event_singleton_messages", 1);
    } else if (delivered) {
        add_room_message_event_diagnostic("room_message_event_batch_samples", 1);
        add_room_message_event_diagnostic(
            "room_message_event_batch_messages_total", count);
        set_room_message_event_diagnostic_max(
            "room_message_event_batch_messages_max", count);
    }
    if (delivered) {
        record_owner_fifo_batch_boundary(player, entry);
        add_room_message_event_diagnostic(
            "room_message_event_filled_recipients", 1);
        add_room_message_event_diagnostic(
            "room_message_event_native_filled_messages", count);
    }
    return delivered;
}

private mapping fill_room_message_event_entries(object *players,
    mapping *entries)
{
    mapping entry;
    mapping meta;
    mapping reservation;
    mapping result;
    mapping *boundary_metas;
    mixed fill_error;
    object player;
    object *boundary_players;
    string outcome;
    string *boundary_owner_ids;
    string *outcomes;
    int *event_counts;
    int *released;
    int *text_length_totals;
    int boundary_count;
    int count;
    int delivered;
    int elapsed;
    int index;
    int started_at;

    if (!arrayp(players) || !arrayp(entries) || sizeof(players) == 0
        || sizeof(entries) != sizeof(players))
        return ([ "ok": 0, "delivered": 0 ]);
    count = sizeof(players);
    started_at = perf_counter_ns();
    fill_error = catch(result = call_other(
        "/kernel/framework/xk_protocol_core",
        "xk_fill_reserved_native_room_message_event_entries",
        players, entries));
    elapsed = (perf_counter_ns() - started_at) / 1000;
    add_room_message_event_diagnostic(
        "room_message_event_native_fill_samples", count);
    add_room_message_event_diagnostic(
        "room_message_event_native_fill_total_us", elapsed);
    set_room_message_event_diagnostic_max(
        "room_message_event_native_fill_max_us", elapsed);
    add_room_message_event_diagnostic(
        "room_message_event_native_fill_batch_samples", 1);
    add_room_message_event_diagnostic(
        "room_message_event_native_fill_batch_total_us", elapsed);
    set_room_message_event_diagnostic_max(
        "room_message_event_native_fill_batch_max_us", elapsed);
    add_room_message_event_diagnostic(
        "room_message_event_native_fill_batch_recipients_total", count);
    set_room_message_event_diagnostic_max(
        "room_message_event_native_fill_batch_recipients_max", count);

    outcomes = !fill_error && mapp(result) && arrayp(result["outcomes"])
        && sizeof(result["outcomes"]) == count
            ? result["outcomes"] : allocate(count);
    event_counts = !fill_error && mapp(result)
        && arrayp(result["event_counts"])
        && sizeof(result["event_counts"]) == count
            ? result["event_counts"] : allocate(count);
    text_length_totals = !fill_error && mapp(result)
        && arrayp(result["text_length_totals"])
        && sizeof(result["text_length_totals"]) == count
            ? result["text_length_totals"] : allocate(count);
    released = !fill_error && mapp(result) && arrayp(result["released"])
        && sizeof(result["released"]) == count
            ? result["released"] : allocate(count);
    boundary_players = allocate(count);
    boundary_owner_ids = allocate(count);
    boundary_metas = allocate(count);

    for (index = 0; index < count; index++) {
        player = players[index];
        entry = entries[index];
        reservation = mapp(entry) ? entry["reservation"] : 0;
        outcome = stringp(outcomes[index])
            ? outcomes[index] : "native_fill_failed";
        if (member_array(outcome, ({
                "filled", "submitted", "completion_failed",
                "invalid_entry", "target_identity_changed",
                "promotion_failed", "native_fill_failed"
            })) == -1)
            outcome = "native_fill_failed";
        if ((outcome == "filled" || outcome == "completion_failed")
            && mapp(reservation))
            reservation["terminal"] = 1;
        if (released[index] && mapp(reservation)) {
            reservation["terminal"] = 1;
            add_room_message_event_diagnostic(
                "room_message_event_released_recipients", 1);
        }

        if (outcome != "filled")
            add_room_message_event_diagnostic(
                "room_message_event_" + outcome + "_recipients", 1);
        if (outcome != "filled" && outcome != "submitted"
            && outcome != "completion_failed") {
            if (!released[index])
                release_room_message_entry_reservation(player, entry);
            drop_room_message_entry(entry, 0);
            continue;
        }

        if (event_counts[index] <= 0)
            event_counts[index] = mapp(entry) && intp(entry["event_count"])
                ? entry["event_count"] : 0;
        if (text_length_totals[index] <= 0)
            text_length_totals[index] = mapp(entry)
                && intp(entry["text_length_total"])
                    ? entry["text_length_total"] : 0;
        if (event_counts[index] == 1) {
            add_room_message_event_diagnostic(
                "room_message_event_singleton_messages", 1);
        } else {
            add_room_message_event_diagnostic(
                "room_message_event_batch_samples", 1);
            add_room_message_event_diagnostic(
                "room_message_event_batch_messages_total", event_counts[index]);
            set_room_message_event_diagnostic_max(
                "room_message_event_batch_messages_max", event_counts[index]);
        }
        if (outcome == "submitted") {
            add_room_message_event_diagnostic(
                "room_message_event_owner_submitted_recipients", 1);
            add_room_message_event_diagnostic(
                "room_message_event_owner_submitted_messages",
                event_counts[index]);
        } else {
            add_room_message_event_diagnostic(
                "room_message_event_filled_recipients", 1);
            add_room_message_event_diagnostic(
                "room_message_event_native_filled_messages",
                event_counts[index]);
        }
        delivered++;

        boundary_players[boundary_count] = player;
        boundary_owner_ids[boundary_count] = query_owner_output_id(player);
        meta = ([
            "stream": "message",
            "payload_contract": "preencoded_room_message_batch",
            "message_count": event_counts[index],
            "text_length": text_length_totals[index],
            "message_runtime_fifo_boundary": 1,
            "business_output": 1,
            "direct_socket_write": 0,
        ]);
        boundary_metas[boundary_count] = meta;
        boundary_count++;
    }

    if (boundary_count > 0) {
        boundary_players = boundary_players[0..boundary_count - 1];
        boundary_owner_ids = boundary_owner_ids[0..boundary_count - 1];
        boundary_metas = boundary_metas[0..boundary_count - 1];
        catch(OWNER_SESSION_FIFO_D->record_owner_output_boundaries_batch(
            boundary_players, boundary_owner_ids,
            "message_event_batch", boundary_metas));
    }
    return ([ "ok": 1, "delivered": delivered ]);
}

int record_owner_room_output_completion(string state, int event_count)
{
    if ((state != "completed" && state != "released")
        || event_count <= 0)
        return 0;
    if (state == "completed") {
        add_room_message_event_diagnostic(
            "room_message_event_owner_completed_recipients", 1);
        add_room_message_event_diagnostic(
            "room_message_event_owner_completed_messages", event_count);
        add_room_message_event_diagnostic(
            "room_message_event_filled_recipients", 1);
        add_room_message_event_diagnostic(
            "room_message_event_native_filled_messages", event_count);
    } else {
        add_room_message_event_diagnostic(
            "room_message_event_owner_released_recipients", 1);
        add_room_message_event_diagnostic(
            "room_message_event_owner_released_messages", event_count);
        add_room_message_event_diagnostic(
            "room_message_event_released_recipients", 1);
        add_room_message_event_diagnostic(
            "room_message_event_dropped_recipients", 1);
        add_room_message_event_diagnostic(
            "room_message_event_dropped_messages", event_count);
    }
    return 1;
}

private void fallback_room_message_event(object player, mapping wave,
    int message_seq)
{
    mapping envelope;
    mapping options;
    mixed send_error;
    int elapsed;
    int result;
    int started_at;

    if (!objectp(player) || !mapp(wave)
        || !stringp(wave["text"]) || wave["text"] == ""
        || !mapp(wave["options"]) || message_seq <= 0
        || !intp(wave["sent_at"]) || wave["sent_at"] <= 0)
        return;
    started_at = perf_counter_ns();
    options = copy(wave["options"]);
    options["seq"] = message_seq;
    options["timestamp"] = wave["sent_at"];
    record_owner_fifo_boundary(player, "message_event",
        wave["text"], options);
    envelope = build_envelope(player, wave["text"], options);
    elapsed = (perf_counter_ns() - started_at) / 1000;
    add_room_message_event_diagnostic(
        "room_message_event_fallback_materialize_samples", 1);
    add_room_message_event_diagnostic(
        "room_message_event_fallback_materialize_total_us", elapsed);
    set_room_message_event_diagnostic_max(
        "room_message_event_fallback_materialize_max_us", elapsed);
    send_error = catch(result = call_other(
        "/kernel/framework/xk_protocol_core", "xk_send_message_event",
        player, envelope, options["response_context"]));
    if (send_error || !result)
        catch(call_other("/kernel/simul_efun", "msg_send_raw",
            player, wave["text"]));
    add_room_message_event_diagnostic(
        "room_message_event_fallback_messages", 1);
}

private void drop_room_message_entry(mapping entry, int noninteractive)
{
    int count;

    if (!mapp(entry))
        return;
    count = intp(entry["event_count"]) ? entry["event_count"] : 0;
    if (count > 0) {
        add_room_message_event_diagnostic(
            "room_message_event_dropped_recipients", 1);
        add_room_message_event_diagnostic(
            "room_message_event_dropped_messages", count);
        if (noninteractive)
            add_room_message_event_diagnostic(
                "room_message_event_noninteractive_dropped_messages", count);
    }
}

private int release_room_message_entry_reservation(object player, mapping entry)
{
    mixed err;
    int result;

    if (!objectp(player) || !mapp(entry) || !mapp(entry["reservation"]))
        return 0;
    if (entry["reservation"]["terminal"])
        return 1;
    err = catch(result = call_other("/kernel/framework/xk_protocol_core",
        "xk_release_reserved_protocol", player, entry["reservation"]));
    if (!err && result) {
        entry["reservation"]["terminal"] = 1;
        add_room_message_event_diagnostic(
            "room_message_event_released_recipients", 1);
    }
    return !err && result;
}

mapping discard_room_message_events_for_target(object player)
{
    mapping entry;
    mapping seen;
    mapping *entries;
    mixed *queued;
    int count;
    int index;
    int messages;
    int release_failed;
    int released;
    int serial;
    int ticket;

    if (!objectp(player))
        return ([ "detached": 0, "messages": 0,
            "released": 0, "release_failed": 0 ]);
    ensure_room_message_event_queue();
    entries = ({});
    seen = ([]);
    foreach (serial, entry in _room_message_event_partial_entries) {
        if (!mapp(entry) || entry["player"] != player)
            continue;
        entries += ({ entry });
        seen[serial] = 1;
    }
    foreach (ticket, queued in _room_message_event_queue) {
        if (!arrayp(queued) || sizeof(queued) < 2 || queued[0] != player
            || !mapp(queued[1]))
            continue;
        entry = queued[1];
        serial = intp(entry["serial"]) ? entry["serial"] : 0;
        if (serial > 0 && seen[serial])
            continue;
        entries += ({ entry });
        if (serial > 0)
            seen[serial] = 1;
    }

    count = sizeof(entries);
    for (index = 0; index < count; index++) {
        entry = entries[index];
        messages += intp(entry["event_count"]) ? entry["event_count"] : 0;
        remove_room_message_event_entry_from_queue(entry);
        if (release_room_message_entry_reservation(player, entry))
            released++;
        else
            release_failed++;
        drop_room_message_entry(entry, 1);
    }
    if (count > 0) {
        add_room_message_event_diagnostic(
            "room_message_event_disconnect_detached_recipients", count);
        add_room_message_event_diagnostic(
            "room_message_event_disconnect_detached_messages", messages);
        add_room_message_event_diagnostic(
            "room_message_event_disconnect_released_recipients", released);
        add_room_message_event_diagnostic(
            "room_message_event_disconnect_release_failed_recipients",
            release_failed);
    }
    if (!room_message_event_queue_pending()
        && !room_message_event_partial_pending()
        && _room_message_event_flush_call_out) {
        catch(remove_call_out(_room_message_event_flush_call_out));
        _room_message_event_flush_call_out = 0;
        _room_message_event_flush_scheduled_at_ns = 0;
        _room_message_event_flush_due_at_ns = 0;
        _room_message_event_flush_generation++;
    }
    reset_room_message_event_queue_if_empty();
    return ([
        "detached": count,
        "messages": messages,
        "released": released,
        "release_failed": release_failed,
    ]);
}

private void send_room_message_event_entry(object player, mapping entry)
{
    if (objectp(player))
        sync_player_message_seq(player);
    if (!objectp(player) || !interactive(player)) {
        release_room_message_entry_reservation(player, entry);
        drop_room_message_entry(entry, 1);
    } else if (!fill_room_message_event_entry(player, entry)) {
        release_room_message_entry_reservation(player, entry);
        drop_room_message_entry(entry, 0);
    }
}

void flush_room_message_event_batches(int generation)
{
    mapping entry;
    mapping *entries;
    mixed *pending;
    object player;
    object *players;
    int batch_count;
    int due_at;
    int elapsed;
    int handled;
    int scheduled_at;
    int started_at;
    int wall_budget_yielded;

    if (generation != _room_message_event_flush_generation)
        return;
    _room_message_event_flush_call_out = 0;
    scheduled_at = _room_message_event_flush_scheduled_at_ns;
    due_at = _room_message_event_flush_due_at_ns;
    _room_message_event_flush_scheduled_at_ns = 0;
    _room_message_event_flush_due_at_ns = 0;
    started_at = perf_counter_ns();
    if (scheduled_at > 0 && started_at >= scheduled_at) {
        elapsed = (started_at - scheduled_at) / 1000;
        add_room_message_event_diagnostic(
            "room_message_event_flush_delay_samples", 1);
        add_room_message_event_diagnostic(
            "room_message_event_flush_delay_total_us", elapsed);
        set_room_message_event_diagnostic_max(
            "room_message_event_flush_delay_max_us", elapsed);
    }
    if (due_at > 0 && started_at >= due_at) {
        elapsed = (started_at - due_at) / 1000;
        add_room_message_event_diagnostic(
            "room_message_event_flush_lateness_samples", 1);
        add_room_message_event_diagnostic(
            "room_message_event_flush_lateness_total_us", elapsed);
        set_room_message_event_diagnostic_max(
            "room_message_event_flush_lateness_max_us", elapsed);
    }
    add_room_message_event_diagnostic("room_message_event_flush_callbacks", 1);
    promote_due_room_message_event_entries(started_at);
    players = allocate(ROOM_MESSAGE_EVENT_RECIPIENT_BUDGET);
    entries = allocate(ROOM_MESSAGE_EVENT_RECIPIENT_BUDGET);

    /* These unresolved reservations are foreground FIFO debt: every later
     * command response for the same session waits behind them.  Retire a
     * useful cross-session wave in one callback, but keep the existing hard
     * wall budget so room fanout cannot monopolize the gateway lane. */
    while (handled < ROOM_MESSAGE_EVENT_RECIPIENT_BUDGET) {
        pending = take_room_message_event_recipient();
        if (!arrayp(pending) || sizeof(pending) < 2)
            break;
        player = pending[0];
        entry = pending[1];
        if (!objectp(player) || !interactive(player)) {
            release_room_message_entry_reservation(player, entry);
            drop_room_message_entry(entry, 1);
        } else {
            sync_player_message_seq(player);
            players[batch_count] = player;
            entries[batch_count] = entry;
            batch_count++;
        }
        handled++;
        add_room_message_event_diagnostic(
            "room_message_event_recipients_processed", 1);
        if (perf_counter_ns() - started_at
            >= ROOM_MESSAGE_EVENT_FLUSH_WALL_BUDGET_NS) {
            wall_budget_yielded = 1;
            break;
        }
    }
    if (batch_count > 0) {
        players = players[0..batch_count - 1];
        entries = entries[0..batch_count - 1];
        fill_room_message_event_entries(players, entries);
    }

    elapsed = perf_counter_ns() - started_at;
    add_room_message_event_diagnostic(
        "room_message_event_flush_wall_samples", 1);
    add_room_message_event_diagnostic(
        "room_message_event_flush_wall_total_us", elapsed / 1000);
    set_room_message_event_diagnostic_max(
        "room_message_event_flush_wall_max_us", elapsed / 1000);
    set_room_message_event_diagnostic_max(
        "room_message_event_flush_recipients_max", handled);
    if (room_message_event_queue_pending()
        && handled >= ROOM_MESSAGE_EVENT_RECIPIENT_BUDGET)
        add_room_message_event_diagnostic(
            "room_message_event_flush_recipient_budget_yields", 1);
    if (room_message_event_queue_pending() && wall_budget_yielded)
        add_room_message_event_diagnostic(
            "room_message_event_flush_wall_budget_yields", 1);
    schedule_room_message_event_flush(0);
}

private mapping reserve_room_message_event_sequence_block(object *players)
{
    mapping reserved;
    mixed err;

    err = catch(reserved = call_other(
        "/kernel/framework/xk_protocol_core",
        "xk_reserve_room_message_event_sequence_block", players));
    return !err && mapp(reserved) ? reserved : ([]);
}

private void fallback_unreserved_room_message_event(object player,
    string text, mapping options)
{
    if (!emit_message_event(player, text, options))
        catch(call_other("/kernel/simul_efun", "msg_send_raw", player, text));
    add_room_message_event_diagnostic(
        "room_message_event_fallback_messages", 1);
    add_room_message_event_diagnostic(
        "room_message_event_fallback_recipients", 1);
}

private void fallback_unreserved_room_message_wave(object *players,
    string text, mapping options)
{
    object player;

    foreach (player in players)
        fallback_unreserved_room_message_event(player, text, options);
}

private void fallback_reserved_room_message_wave(object *players,
    mapping message_wave, mapping reservation_wave)
{
    int *message_seqs;
    int index;

    message_seqs = mapp(reservation_wave)
        && arrayp(reservation_wave["message_seqs"])
        ? reservation_wave["message_seqs"] : ({ });
    if (sizeof(message_seqs) != sizeof(players)) {
        if (mapp(message_wave) && stringp(message_wave["text"])
            && message_wave["text"] != "" && mapp(message_wave["options"]))
            fallback_unreserved_room_message_wave(players,
                message_wave["text"], message_wave["options"]);
        return;
    }
    for (index = 0; index < sizeof(players); index++) {
        sync_player_message_seq(players[index]);
        fallback_room_message_event(
            players[index], message_wave, message_seqs[index]);
        add_room_message_event_diagnostic(
            "room_message_event_fallback_recipients", 1);
    }
}

private mapping reserve_room_message_sequence_wave(object *players)
{
    object player;
    int *message_seqs;
    int count;
    int index;

    count = sizeof(players);
    message_seqs = allocate(count);
    for (index = 0; index < count; index++) {
        player = players[index];
        message_seqs[index] = query_next_room_message_seq(player);
    }
    return ([ "message_seqs": message_seqs ]);
}

private int rollback_room_message_compact_wave(object *players, mapping wave)
{
    mixed err;
    int rolled_back;

    if (!arrayp(players) || !mapp(wave)
        || !intp(wave["wave_id"]) || wave["wave_id"] <= 0)
        return 0;
    err = catch(rolled_back =
        gateway_sessions_rollback_compact_preencoded_message_event_wave(
            players, wave["wave_id"]));
    return !err && rolled_back;
}

private int refresh_room_message_event_entry_native_state(object player,
    mapping entry)
{
    mapping reservation;
    mapping status;
    mixed err;

    reservation = mapp(entry) ? entry["reservation"] : 0;
    if (!objectp(player) || !mapp(entry) || !mapp(reservation)
        || !intp(reservation["reservation_id"])
        || reservation["reservation_id"] <= 0)
        return 0;
    err = catch(status = gateway_session_pending_message_event_batch_status(
        player, reservation["reservation_id"]));
    if (err || !mapp(status) || status["ok"] != 1
        || !intp(status["event_count"]) || status["event_count"] <= 0
        || status["event_count"] > ROOM_MESSAGE_EVENT_BATCH_LIMIT
        || !intp(status["text_length_total"])
        || status["text_length_total"] <= 0
        || !intp(status["created_at_ns"])
        || status["created_at_ns"] <= 0
        || !intp(status["last_append_at_ns"])
        || status["last_append_at_ns"] < status["created_at_ns"])
        return 0;
    entry["event_count"] = status["event_count"];
    entry["text_length_total"] = status["text_length_total"];
    entry["created_at_ns"] = status["created_at_ns"];
    entry["last_append_at_ns"] = status["last_append_at_ns"];
    return 1;
}

private int register_room_message_compact_created_entries(mapping wave)
{
    object *targets;
    mixed err;
    int registered;

    if (!mapp(wave) || !arrayp(wave["created_targets"])
        || !arrayp(wave["created_reservation_ids"])
        || !arrayp(wave["created_slot_server_seqs"])
        || sizeof(wave["created_targets"])
            != sizeof(wave["created_reservation_ids"])
        || sizeof(wave["created_targets"])
            != sizeof(wave["created_slot_server_seqs"]))
        return 0;
    targets = wave["created_targets"];
    if (sizeof(targets) == 0)
        return 1;
    err = catch(registered = call_other(
        "/kernel/framework/xk_protocol_core",
        "xk_register_sparse_room_message_reservations", targets,
        wave["created_reservation_ids"],
        wave["created_slot_server_seqs"]));
    return !err && registered;
}

private void remove_room_message_compact_created_entries(mapping wave)
{
    mapping entry;
    mapping previous;
    mapping *created_entries;
    mapping *previous_entries;
    object player;
    object *targets;
    int index;

    if (!mapp(wave) || !arrayp(wave["created_entries"])
        || !arrayp(wave["created_previous_entries"])
        || !arrayp(wave["created_targets"]))
        return;
    created_entries = wave["created_entries"];
    previous_entries = wave["created_previous_entries"];
    targets = wave["created_targets"];
    for (index = 0; index < sizeof(created_entries); index++) {
        entry = created_entries[index];
        player = index < sizeof(targets) ? targets[index] : 0;
        if (mapp(entry))
            remove_room_message_event_entry_from_queue(entry);
        previous = index < sizeof(previous_entries)
            ? previous_entries[index] : 0;
        if (objectp(player) && mapp(previous)
            && previous["active"] && !previous["ready"])
            _room_message_event_entries[player] = previous;
    }
}

private int finalize_room_message_compact_wave(object *players, mapping wave)
{
    mapping entry;
    int *full_indices;
    int index;
    int player_index;
    int promoted;

    if (!arrayp(players) || !mapp(wave)
        || !arrayp(wave["full_indices"]))
        return 0;
    full_indices = wave["full_indices"];
    for (index = 0; index < sizeof(full_indices); index++) {
        player_index = full_indices[index];
        if (player_index < 0 || player_index >= sizeof(players))
            continue;
        entry = _room_message_event_entries[players[player_index]];
        if (!mapp(entry) || !entry["active"] || entry["ready"]
            || !refresh_room_message_event_entry_native_state(
                players[player_index], entry))
            continue;
        if (room_message_entry_has_earlier_partial(entry))
            add_room_message_event_diagnostic(
                "room_message_event_full_fifo_blocked_recipients", 1);
        promoted += enqueue_room_message_event_entry(
            players[player_index], entry, "batch_full");
    }
    return promoted;
}

private int recover_room_message_compact_wave(object *players, mapping wave,
    mapping message_wave)
{
    mapping entry;
    object player;
    mapping *created_entries;
    int *created_indices;
    int *created_reservation_ids;
    int *message_seqs;
    int index;
    int player_index;
    int promoted;
    int released;
    mixed release_error;

    if (!arrayp(players) || !mapp(wave) || !mapp(message_wave)
        || !arrayp(wave["created_entries"])
        || !arrayp(wave["created_indices"])
        || !arrayp(wave["created_reservation_ids"])
        || !arrayp(wave["message_seqs"]))
        return 0;
    created_entries = wave["created_entries"];
    created_indices = wave["created_indices"];
    created_reservation_ids = wave["created_reservation_ids"];
    message_seqs = wave["message_seqs"];
    for (index = 0; index < sizeof(created_indices); index++) {
        if (index < sizeof(created_entries) && mapp(created_entries[index]))
            continue;
        player_index = created_indices[index];
        if (player_index < 0 || player_index >= sizeof(players)
            || index >= sizeof(created_reservation_ids)
            || created_reservation_ids[index] <= 0
            || player_index >= sizeof(message_seqs))
            continue;
        player = players[player_index];
        release_error = catch(released = call_other(
            "/kernel/framework/xk_protocol_core",
            "xk_release_reserved_protocol_id", player,
            created_reservation_ids[index]));
        if (release_error || !released)
            continue;
        add_room_message_event_diagnostic(
            "room_message_event_released_recipients", 1);
        sync_player_message_seq(player);
        fallback_room_message_event(
            player, message_wave, message_seqs[player_index]);
        add_room_message_event_diagnostic(
            "room_message_event_fallback_recipients", 1);
    }
    foreach (player in players) {
        entry = _room_message_event_entries[player];
        if (!mapp(entry) || !entry["active"] || entry["ready"])
            continue;
        refresh_room_message_event_entry_native_state(player, entry);
        promoted += enqueue_room_message_event_entry(
            player, entry, "native_recovery");
    }
    return promoted;
}

private mapping queue_room_message_compact_wave(object *players,
    mapping sequence_wave, mapping message_wave, mapping reserved_state)
{
    mapping entry;
    mapping previous;
    mapping result;
    mapping wave;
    mapping *created_entries;
    mapping *previous_entries;
    object player;
    object *created_targets;
    int *created_indices;
    int *created_reservation_ids;
    int *created_slot_server_seqs;
    int *full_indices;
    int *message_epochs;
    int *message_seqs;
    int count;
    int created_count;
    int index;
    int player_index;
    int previous_index;
    int ready_count;
    int reused_count;
    int text_length;
    int wave_id;
    mixed native_error;

    count = sizeof(players);
    if (count <= 0 || !mapp(sequence_wave)
        || !arrayp(sequence_wave["message_seqs"])
        || sizeof(sequence_wave["message_seqs"]) != count
        || !mapp(message_wave) || !stringp(message_wave["stable_json"])
        || message_wave["stable_json"] == ""
        || !stringp(message_wave["scope_type"])
        || message_wave["scope_type"] == ""
        || !stringp(message_wave["text"]) || message_wave["text"] == ""
        || !intp(message_wave["sent_at"]) || message_wave["sent_at"] <= 0
        || !mapp(reserved_state)
        || !intp(reserved_state["first_server_seq"])
        || reserved_state["first_server_seq"] <= 0
        || !arrayp(reserved_state["message_epochs"])
        || sizeof(reserved_state["message_epochs"]) != count)
        return ([ "ok": 0, "rolled_back": 1 ]);
    message_seqs = sequence_wave["message_seqs"];
    message_epochs = reserved_state["message_epochs"];
    text_length = strlen(message_wave["text"]);
    native_error = catch(result =
        gateway_sessions_reserve_and_append_compact_preencoded_message_event_wave(
            players, reserved_state["first_server_seq"],
            message_wave["stable_json"], message_wave["scope_type"],
            message_seqs, message_epochs, message_wave["sent_at"],
            ROOM_MESSAGE_EVENT_BATCH_LIMIT, text_length));
    if (native_error || !mapp(result) || result["ok"] != 1) {
        add_room_message_event_diagnostic(
            "room_message_event_native_append_failures", 1);
        return ([ "ok": 0, "rolled_back": 1 ]);
    }

    created_indices = arrayp(result["created_indices"])
        ? result["created_indices"] : ({ });
    created_reservation_ids = arrayp(result["created_reservation_ids"])
        ? result["created_reservation_ids"] : ({ });
    created_slot_server_seqs = arrayp(result["created_slot_server_seqs"])
        ? result["created_slot_server_seqs"] : ({ });
    full_indices = arrayp(result["full_indices"])
        ? result["full_indices"] : ({ });
    created_count = intp(result["created_count"])
        ? result["created_count"] : -1;
    reused_count = intp(result["reused_count"])
        ? result["reused_count"] : -1;
    wave_id = intp(result["wave_id"]) ? result["wave_id"] : 0;
    wave = ([
        "wave_id": wave_id,
        "message_seqs": message_seqs,
        "full_indices": full_indices,
        "created_indices": created_indices,
        "created_reservation_ids": created_reservation_ids,
        "created_slot_server_seqs": created_slot_server_seqs,
    ]);
    if (wave_id <= 0 || created_count < 0 || reused_count < 0
        || created_count + reused_count != count
        || sizeof(created_indices) != created_count
        || sizeof(created_reservation_ids) != created_count
        || sizeof(created_slot_server_seqs) != created_count) {
        if (!rollback_room_message_compact_wave(players, wave))
            wave["rollback_failed"] = 1;
        add_room_message_event_diagnostic(
            "room_message_event_native_append_failures", 1);
        return wave + ([ "ok": 0,
            "rolled_back": wave["rollback_failed"] ? 0 : 1 ]);
    }
    previous_index = -1;
    for (index = 0; index < created_count; index++) {
        player_index = created_indices[index];
        if (player_index <= previous_index || player_index < 0
            || player_index >= count
            || !intp(created_reservation_ids[index])
            || created_reservation_ids[index] <= 0
            || created_slot_server_seqs[index]
                != reserved_state["first_server_seq"] + player_index * 2) {
            if (!rollback_room_message_compact_wave(players, wave))
                wave["rollback_failed"] = 1;
            add_room_message_event_diagnostic(
                "room_message_event_native_append_failures", 1);
            return wave + ([ "ok": 0,
                "rolled_back": wave["rollback_failed"] ? 0 : 1 ]);
        }
        previous_index = player_index;
    }

    created_targets = allocate(created_count);
    created_entries = allocate(created_count);
    previous_entries = allocate(created_count);
    for (index = 0; index < created_count; index++) {
        player_index = created_indices[index];
        player = players[player_index];
        previous = _room_message_event_entries[player];
        previous_entries[index] = mapp(previous) ? previous : 0;
        entry = new_native_room_message_event_entry(player,
            created_reservation_ids[index], created_slot_server_seqs[index],
            message_wave["sent_at"], text_length, perf_counter_ns());
        created_targets[index] = player;
        created_entries[index] = entry;
        if (!mapp(entry)) {
            wave["created_targets"] = created_targets;
            wave["created_entries"] = created_entries;
            wave["created_previous_entries"] = previous_entries;
            if (rollback_room_message_compact_wave(players, wave)) {
                remove_room_message_compact_created_entries(wave);
                return wave + ([ "ok": 0, "rolled_back": 1 ]);
            }
            wave["rollback_failed"] = 1;
            ready_count = recover_room_message_compact_wave(
                players, wave, message_wave);
            return wave + ([ "ok": 1, "recovered": 1,
                "ready_count": ready_count ]);
        }
    }
    wave["created_targets"] = created_targets;
    wave["created_entries"] = created_entries;
    wave["created_previous_entries"] = previous_entries;

    native_error = catch(index =
        gateway_sessions_commit_compact_preencoded_message_event_wave(
            players, wave_id));
    if (native_error || !index
        || !register_room_message_compact_created_entries(wave)) {
        if (rollback_room_message_compact_wave(players, wave)) {
            remove_room_message_compact_created_entries(wave);
            return wave + ([ "ok": 0, "rolled_back": 1 ]);
        }
        wave["rollback_failed"] = 1;
        ready_count = recover_room_message_compact_wave(
            players, wave, message_wave);
        return wave + ([ "ok": 1, "recovered": 1,
            "ready_count": ready_count ]);
    }

    add_room_message_event_diagnostic(
        "room_message_event_reservation_reused_targets", reused_count);
    add_room_message_event_diagnostic(
        "room_message_event_reservation_new_targets", created_count);
    if (reused_count > 0)
        add_room_message_event_diagnostic(
            "room_message_event_reservation_reuse_waves", 1);
    ready_count = finalize_room_message_compact_wave(players, wave);
    return wave + ([ "ok": 1, "ready_count": ready_count ]);
}

int emit_room_message_event_batch(object *players, string text, mapping options)
{
    mapping frozen_options;
    mapping message_wave;
    mapping preencoded;
    mapping queue_result;
    mapping reserved_state;
    mapping sequence_wave;
    object player;
    int count;
    int elapsed;
    int preencode_started_at;
    int queue_started_at;
    int ready_count;
    int reserve_started_at;
    int sequence_started_at;
    int sent_at;

    if (!arrayp(players) || sizeof(players) == 0
        || !stringp(text) || text == "")
        return 0;
    foreach (player in players) {
        if (!objectp(player) || !interactive(player))
            return 0;
    }

    count = sizeof(players);
    ensure_room_message_event_queue();
    frozen_options = freeze_room_message_event_options(options);
    preencode_started_at = perf_counter_ns();
    preencoded = preencode_room_message_event_template(text, frozen_options);
    elapsed = (perf_counter_ns() - preencode_started_at) / 1000;
    add_room_message_event_diagnostic(
        "room_message_event_preencode_samples", 1);
    add_room_message_event_diagnostic(
        "room_message_event_preencode_total_us", elapsed);
    set_room_message_event_diagnostic_max(
        "room_message_event_preencode_max_us", elapsed);
    add_room_message_event_diagnostic("room_message_event_jobs_enqueued", 1);
    add_room_message_event_diagnostic(
        "room_message_event_targets_enqueued", count);
    if (!mapp(preencoded)) {
        add_room_message_event_diagnostic(
            "room_message_event_preencode_failures", 1);
        fallback_unreserved_room_message_wave(players, text, frozen_options);
        return 1;
    }
    sent_at = time();
    message_wave = new_room_message_event_wave(
        text, frozen_options, preencoded, sent_at);
    if (!mapp(message_wave)) {
        fallback_unreserved_room_message_wave(players, text, frozen_options);
        return 1;
    }

    /* Player message sequence remains player-private.  The native compact
     * wave owns reservation reuse and returns only newly-created/full sparse
     * indices, so the common reused path does not materialize N-wide LPC
     * reservation or previous-state columns. */
    reserve_started_at = perf_counter_ns();
    sequence_wave = reserve_room_message_sequence_wave(players);
    elapsed = (perf_counter_ns() - reserve_started_at) / 1000;
    add_room_message_event_diagnostic(
        "room_message_event_reserve_wave_samples", 1);
    add_room_message_event_diagnostic(
        "room_message_event_reserve_wave_total_us", elapsed);
    set_room_message_event_diagnostic_max(
        "room_message_event_reserve_wave_max_us", elapsed);
    if (!mapp(sequence_wave)) {
        fallback_unreserved_room_message_wave(players, text, frozen_options);
        return 1;
    }

    sequence_started_at = perf_counter_ns();
    reserved_state = reserve_room_message_event_sequence_block(players);
    elapsed = (perf_counter_ns() - sequence_started_at) / 1000;
    add_room_message_event_diagnostic(
        "room_message_event_sequence_block_samples", 1);
    add_room_message_event_diagnostic(
        "room_message_event_sequence_block_total_us", elapsed);
    set_room_message_event_diagnostic_max(
        "room_message_event_sequence_block_max_us", elapsed);
    queue_started_at = perf_counter_ns();
    queue_result = queue_room_message_compact_wave(
        players, sequence_wave, message_wave, reserved_state);
    elapsed = (perf_counter_ns() - queue_started_at) / 1000;
    add_room_message_event_diagnostic(
        "room_message_event_columnar_queue_samples", 1);
    add_room_message_event_diagnostic(
        "room_message_event_columnar_queue_total_us", elapsed);
    set_room_message_event_diagnostic_max(
        "room_message_event_columnar_queue_max_us", elapsed);

    if (!mapp(queue_result) || queue_result["ok"] != 1) {
        if (mapp(queue_result) && queue_result["rollback_failed"])
            add_room_message_event_diagnostic(
                "room_message_event_native_rollback_failures", 1);
        if (!mapp(queue_result) || queue_result["rolled_back"])
            fallback_reserved_room_message_wave(
                players, message_wave, sequence_wave);
        return 1;
    }
    ready_count = intp(queue_result["ready_count"])
        ? queue_result["ready_count"] : 0;
    schedule_room_message_event_flush(ready_count > 0);
    return 1;
}

mapping query_room_message_event_batch_status()
{
    mapping status;
    string key;
    int partial_depth;
    int ready_depth;

    status = ([]);
    foreach (key in ({
        "room_message_event_jobs_enqueued",
        "room_message_event_targets_enqueued",
        "room_message_event_flush_callbacks",
        "room_message_event_recipients_processed",
        "room_message_event_batch_samples",
        "room_message_event_batch_messages_total",
        "room_message_event_batch_messages_max",
        "room_message_event_singleton_messages",
        "room_message_event_fallback_messages",
        "room_message_event_filled_recipients",
        "room_message_event_fallback_recipients",
        "room_message_event_dropped_recipients",
        "room_message_event_released_recipients",
        "room_message_event_invalid_entry_recipients",
        "room_message_event_target_identity_changed_recipients",
        "room_message_event_promotion_failed_recipients",
        "room_message_event_native_fill_failed_recipients",
        "room_message_event_native_append_failures",
        "room_message_event_native_rollback_failures",
        "room_message_event_completion_failed_recipients",
        "room_message_event_dropped_messages",
        "room_message_event_noninteractive_dropped_messages",
        "room_message_event_native_filled_messages",
        "room_message_event_owner_submitted_recipients",
        "room_message_event_owner_submitted_messages",
        "room_message_event_owner_completed_recipients",
        "room_message_event_owner_completed_messages",
        "room_message_event_owner_released_recipients",
        "room_message_event_owner_released_messages",
        "room_message_event_preencode_samples",
        "room_message_event_preencode_total_us",
        "room_message_event_preencode_max_us",
        "room_message_event_preencode_failures",
        "room_message_event_reserve_wave_samples",
        "room_message_event_reserve_wave_total_us",
        "room_message_event_reserve_wave_max_us",
        "room_message_event_sequence_block_samples",
        "room_message_event_sequence_block_total_us",
        "room_message_event_sequence_block_max_us",
        "room_message_event_reservation_register_samples",
        "room_message_event_reservation_register_total_us",
        "room_message_event_reservation_register_max_us",
        "room_message_event_columnar_queue_samples",
        "room_message_event_columnar_queue_total_us",
        "room_message_event_columnar_queue_max_us",
        "room_message_event_reservation_reused_targets",
        "room_message_event_reservation_new_targets",
        "room_message_event_reservation_reuse_waves",
        "room_message_event_player_seq_cache_hits",
        "room_message_event_player_seq_cache_misses",
        "room_message_event_player_seq_syncs",
        "room_message_event_native_prepare_samples",
        "room_message_event_native_prepare_total_us",
        "room_message_event_native_prepare_max_us",
        "room_message_event_native_fill_samples",
        "room_message_event_native_fill_total_us",
        "room_message_event_native_fill_max_us",
        "room_message_event_native_fill_batch_samples",
        "room_message_event_native_fill_batch_total_us",
        "room_message_event_native_fill_batch_max_us",
        "room_message_event_native_fill_batch_recipients_total",
        "room_message_event_native_fill_batch_recipients_max",
        "room_message_event_fallback_materialize_samples",
        "room_message_event_fallback_materialize_total_us",
        "room_message_event_fallback_materialize_max_us",
        "room_message_event_flush_delay_samples",
        "room_message_event_flush_delay_total_us",
        "room_message_event_flush_delay_max_us",
        "room_message_event_flush_lateness_samples",
        "room_message_event_flush_lateness_total_us",
        "room_message_event_flush_lateness_max_us",
        "room_message_event_promotion_scan_callbacks",
        "room_message_event_promotion_scan_entries_total",
        "room_message_event_promotion_scan_entries_max",
        "room_message_event_promotion_scan_cpu_total_us",
        "room_message_event_promotion_scan_cpu_max_us",
        "room_message_event_promotion_scan_cursor_wraps",
        "room_message_event_flush_wall_samples",
        "room_message_event_flush_wall_total_us",
        "room_message_event_flush_wall_max_us",
        "room_message_event_flush_recipient_budget_yields",
        "room_message_event_flush_wall_budget_yields",
        "room_message_event_flush_recipients_max",
        "room_message_event_full_ready_recipients",
        "room_message_event_full_fifo_blocked_recipients",
        "room_message_event_idle_ready_recipients",
        "room_message_event_max_hold_ready_recipients",
        "room_message_event_fifo_ready_successor_checks",
        "room_message_event_fifo_ready_successor_recipients",
        "room_message_event_pressure_deferrals",
        "room_message_event_command_pressure_deferrals",
        "room_message_event_main_queue_pressure_deferrals",
        "room_message_event_buffered_input_pressure_deferrals",
        "room_message_event_schedule_failure_forced_recipients",
        "room_message_event_native_state_missing_recipients",
        "room_message_event_native_recovery_recipients",
        "room_message_event_session_boundary_yields",
    }))
        status[key] = room_message_event_diagnostic_int(key);
    status["room_message_event_flush_recipient_budget"] =
        ROOM_MESSAGE_EVENT_RECIPIENT_BUDGET;
    status["room_message_event_flush_wall_budget_us"] =
        ROOM_MESSAGE_EVENT_FLUSH_WALL_BUDGET_NS / 1000;
    status["room_message_event_columnar_layout"] = 1;
    status["room_message_event_native_pending_layout"] = 1;
    status["room_message_event_native_recipient_batch_fill"] = 1;
    status["room_message_event_flush_priority"] = 1;
    status["room_message_event_coalesce_window_us"] =
        ROOM_MESSAGE_EVENT_COALESCE_WINDOW_US;
    status["room_message_event_continuation_delay_us"] =
        ROOM_MESSAGE_EVENT_CONTINUATION_DELAY_US;
    status["room_message_event_pressure_retry_us"] =
        ROOM_MESSAGE_EVENT_PRESSURE_RETRY_US;
    status["room_message_event_promotion_scan_budget"] =
        ROOM_MESSAGE_EVENT_PROMOTION_SCAN_BUDGET;
    status["room_message_event_max_hold_us"] =
        ROOM_MESSAGE_EVENT_MAX_HOLD_US;
    partial_depth = mapp(_room_message_event_partial_entries)
        ? sizeof(_room_message_event_partial_entries) : 0;
    ready_depth = mapp(_room_message_event_queue)
        ? sizeof(_room_message_event_queue) : 0;
    status["room_message_event_partial_depth"] = partial_depth;
    status["room_message_event_ready_depth"] = ready_depth;
    status["room_message_event_pending_recipients"] =
        partial_depth + ready_depth;
    status["room_message_event_queue_depth"] = ready_depth;
    return status;
}

int reset_room_message_event_batch_diagnostics()
{
    if (room_message_event_queue_pending()
        || room_message_event_partial_pending())
        return 0;
    _room_message_event_diagnostics = ([]);
    return 1;
}

int emit_message_event(object player, string text, mapping options)
{
    mapping envelope;

    if (!objectp(player) || !stringp(text) || text == "")
        return 0;
    if (!mapp(options))
        options = ([]);
    else
        options = copy(options);

    /* A message event is not a command reply unless its producer freezes an
     * explicit response_context.  Background/daemon producers must never
     * inherit the player's latest command temp at flush time. */
    record_owner_fifo_boundary(player, "message_event", text, options);
    envelope = build_envelope(player, text, options);
    if (!undefinedp(options["response_context"])) {
        envelope["suppress_command_request_id"] = 1;
        if (mapp(options["response_context"])
            && stringp(options["response_context"]["command_request_id"])
            && options["response_context"]["command_request_id"] != "")
            envelope["command_request_id"] =
                options["response_context"]["command_request_id"];
    }
    return call_other("/kernel/framework/xk_protocol_core", "xk_send_message_event",
        player, envelope, options["response_context"]);
}

int emit_text(object player, string text, mapping options)
{
    options = mapp(options) ? copy(options) : ([]);
    if (!stringp(options["channel"]) || options["channel"] == "")
        options["channel"] = "main";
    if (!stringp(options["intent"]) || options["intent"] == "")
        options["intent"] = "append";
    return emit_message_event(player, text, options);
}

int emit_error(object player, string text, mapping options)
{
    options = mapp(options) ? copy(options) : ([]);
    options["channel"] = "error";
    options["intent"] = "notify";
    options["priority"] = "high";
    options["reliability"] = "critical";
    options["display_mode"] = "instant";
    return emit_message_event(player, text, options);
}

int emit_state_notice(object player, string text, mapping options)
{
    options = mapp(options) ? copy(options) : ([]);
    options["channel"] = "state";
    options["intent"] = "state_update";
    options["priority"] = "low";
    options["reliability"] = "state";
    options["display_mode"] = "silent_state";
    return emit_message_event(player, text, options);
}
