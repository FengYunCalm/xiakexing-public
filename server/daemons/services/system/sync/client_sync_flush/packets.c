/**
 * @file    client_sync_flush/packets.c
 * @brief   即时包分区与 chat 队列 FIFO
 *
 * @par Description:
 * 含 {count_chat_message_packets, entry_immediate_packet_count, entry_immediate_chat_count, replace_entry_immediate_packets, append_entry_immediate_packets, append_entry_chat_packet_buffer, transaction_has_only_chat_messages, transaction_carries_room_snapshot, entry_has_chat_messages, entry_has_only_chat_messages, packets_are_only_chat_messages, entry_is_chat_only, reset_pending_chat_queue, reset_pending_chat_queue_if_idle, enqueue_pending_chat_key, remove_pending_chat_entry, take_pending_chat_entry, packets_have_chat_messages, stamp_chat_packet_pending_age, partition_immediate_packets}。
 *
 * 本模块是 `client_sync_flush.c` 继承链的一环，由上层模块继承；
 * 跨模块调用经前向声明 + 运行时动态分派（this_object() 解析到最终对象）。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>
#include <system/xk_protocol.h>

inherit "/daemons/services/system/sync/client_sync_flush/delivery";

void append_entry_chat_packet_buffer(mapping entry, mixed *packets,
    int packet_count);
void append_entry_immediate_packets();
mixed *apply_chat_only_packet_buffer(object player, mixed *packets,
    int packet_count, int pending_chat_age_now);
mixed *apply_entry_immediate_packets();
mixed *apply_immediate_packet_buffer(object player, mixed *packets,
    int packet_count, int pending_chat_age_now);
int count_chat_message_packets();
void enqueue_pending_chat_key(string key);
int entry_has_chat_messages(mapping entry);
int entry_has_only_chat_messages();
private int entry_immediate_chat_count(mapping entry);
int entry_immediate_packet_count(mapping entry);
mixed *entry_immediate_packets(mapping entry);
int entry_is_chat_only(mapping entry);
int has_refresh_work(mapping plan);
mixed *normalize_immediate_packets(mixed packets);
int packets_are_only_chat_messages();
int packets_have_chat_messages(mixed packets);
mapping partition_immediate_packets(mixed packets);
void remove_pending_chat_entry(string key);
void replace_entry_immediate_packets(mapping entry, mixed packets);
void reset_pending_chat_queue();
private void reset_pending_chat_queue_if_idle();
void stamp_chat_packet_pending_age();
mixed *take_pending_chat_entry();
int transaction_carries_room_snapshot();
int transaction_has_only_chat_messages();

mixed *normalize_immediate_packets(mixed packets)
{
    if (!arrayp(packets) || sizeof(packets) == 0)
        return ({ });

    return filter_array(packets, (: mapp($1) :));
}

private int count_chat_message_packets(mixed *packets, int packet_count)
{
    mixed packet;
    int chat_count;
    int index;

    if (!arrayp(packets) || packet_count <= 0)
        return 0;
    if (packet_count > sizeof(packets))
        packet_count = sizeof(packets);
    for (index = 0; index < packet_count; index++) {
        packet = packets[index];
        if (mapp(packet) && packet["kind"] == "chat_message")
            chat_count++;
    }
    return chat_count;
}

int entry_immediate_packet_count(mapping entry)
{
    mixed *normalized;
    mixed *storage;
    int count;

    if (!mapp(entry))
        return 0;
    storage = arrayp(entry["immediate"]) ? entry["immediate"] : ({ });
    count = intp(entry["immediate_count"]) ? entry["immediate_count"] : -1;
    if (count >= 0 && count <= sizeof(storage))
        return count;

    normalized = normalize_immediate_packets(storage);
    count = sizeof(normalized);
    entry["immediate"] = normalized;
    entry["immediate_count"] = count;
    entry["immediate_chat_count"] = count_chat_message_packets(normalized, count);
    map_delete(entry, "chat_only");
    return count;
}

private int entry_immediate_chat_count(mapping entry)
{
    mixed *storage;
    int chat_count;
    int count;

    if (!mapp(entry))
        return 0;
    count = entry_immediate_packet_count(entry);
    chat_count = intp(entry["immediate_chat_count"])
        ? entry["immediate_chat_count"] : -1;
    if (chat_count >= 0 && chat_count <= count)
        return chat_count;

    storage = arrayp(entry["immediate"]) ? entry["immediate"] : ({ });
    chat_count = count_chat_message_packets(storage, count);
    entry["immediate_chat_count"] = chat_count;
    map_delete(entry, "chat_only");
    return chat_count;
}

mixed *entry_immediate_packets(mapping entry)
{
    mixed *storage;
    int count;

    if (!mapp(entry))
        return ({ });
    count = entry_immediate_packet_count(entry);
    storage = entry["immediate"];
    if (count == 0)
        return ({ });
    if (count == sizeof(storage))
        return storage;
    return storage[0..count - 1];
}

mixed *apply_entry_immediate_packets(object player, mapping entry,
    int pending_chat_age_now)
{
    mapping partition;
    mixed *storage;
    int count;

    if (!mapp(entry))
        return ({ });
    if (mapp(entry["projection"])
        && !LOGIN_PROJECTION_D->projection_identity_matches(player,
            entry["projection"])) {
        partition = partition_immediate_packets(entry_immediate_packets(entry));
        storage = partition["chat"];
        count = arrayp(storage) ? sizeof(storage) : 0;
        if (count == 0)
            return ({ });
        return apply_chat_only_packet_buffer(player, storage, count,
            pending_chat_age_now);
    }
    count = entry_immediate_packet_count(entry);
    if (count == 0)
        return ({ });
    storage = entry["immediate"];
    if (pending_chat_age_now <= 0 || !entry_has_chat_messages(entry))
        pending_chat_age_now = 0;
    if (entry_is_chat_only(entry))
        return apply_chat_only_packet_buffer(player, storage, count,
            pending_chat_age_now);
    return apply_immediate_packet_buffer(player, storage, count,
        pending_chat_age_now);
}

void replace_entry_immediate_packets(mapping entry, mixed packets)
{
    mixed *normalized;

    if (!mapp(entry))
        return;
    normalized = normalize_immediate_packets(packets);
    entry["immediate"] = normalized;
    entry["immediate_count"] = sizeof(normalized);
    entry["immediate_chat_count"] = count_chat_message_packets(
        normalized, sizeof(normalized));
    map_delete(entry, "chat_only");
}

void append_entry_immediate_packets(mapping entry, mixed packets)
{
    mixed *incoming;
    mixed *storage;
    mixed *grown;
    int capacity;
    int chat_count;
    int count;
    int incoming_chat_count;
    int index;
    int required;

    if (!mapp(entry) || !arrayp(packets))
        return;
    incoming = packets;
    if (sizeof(incoming) == 0)
        return;

    count = entry_immediate_packet_count(entry);
    chat_count = entry_immediate_chat_count(entry);
    storage = arrayp(entry["immediate"]) ? entry["immediate"] : ({ });
    incoming_chat_count = count_chat_message_packets(incoming, sizeof(incoming));

    required = count + sizeof(incoming);
    capacity = sizeof(storage);
    if (capacity < required) {
        if (capacity < 8)
            capacity = 8;
        while (capacity < required)
            capacity *= 2;
        grown = allocate(capacity);
        for (index = 0; index < count; index++)
            grown[index] = storage[index];
        storage = grown;
    }
    foreach (mixed packet in incoming)
        storage[count++] = packet;
    entry["immediate"] = storage;
    entry["immediate_count"] = count;
    entry["immediate_chat_count"] = chat_count + incoming_chat_count;
}

void append_entry_chat_packet_buffer(mapping entry, mixed *packets,
    int packet_count)
{
    mixed *storage;
    mixed *grown;
    int capacity;
    int chat_count;
    int count;
    int index;
    int required;

    if (!mapp(entry) || !arrayp(packets) || packet_count <= 0)
        return;
    if (packet_count > sizeof(packets))
        packet_count = sizeof(packets);

    count = entry_immediate_packet_count(entry);
    chat_count = entry_immediate_chat_count(entry);
    storage = arrayp(entry["immediate"]) ? entry["immediate"] : ({ });
    required = count + packet_count;
    capacity = sizeof(storage);
    if (capacity < required) {
        if (capacity < 8)
            capacity = 8;
        while (capacity < required)
            capacity *= 2;
        grown = allocate(capacity);
        for (index = 0; index < count; index++)
            grown[index] = storage[index];
        storage = grown;
    }
    for (index = 0; index < packet_count; index++)
        storage[count + index] = packets[index];
    entry["immediate"] = storage;
    entry["immediate_count"] = required;
    entry["immediate_chat_count"] = chat_count + packet_count;
}

int transaction_has_only_chat_messages(mapping tx)
{
    mixed *immediate;
    mixed packet;

    if (!mapp(tx) || has_refresh_work(tx["refresh"]))
        return 0;
    immediate = normalize_immediate_packets(tx["immediate"]);
    if (sizeof(immediate) == 0)
        return 0;
    foreach (packet in immediate) {
        if (!mapp(packet) || packet["kind"] != "chat_message")
            return 0;
    }
    return 1;
}

int transaction_carries_room_snapshot(mapping tx)
{
    return mapp(tx) && mapp(tx["refresh"])
        && tx["refresh"]["room_snapshot"] ? 1 : 0;
}

int entry_has_chat_messages(mapping entry)
{
    return entry_immediate_chat_count(entry) > 0;
}

int entry_has_only_chat_messages(mapping entry)
{
    int chat_count;
    int packet_count;

    if (!mapp(entry) || has_refresh_work(entry["plan"]))
        return 0;
    packet_count = entry_immediate_packet_count(entry);
    chat_count = entry_immediate_chat_count(entry);
    return packet_count > 0 && chat_count == packet_count;
}

int packets_are_only_chat_messages(mixed packets)
{
    mixed packet;

    if (!arrayp(packets) || sizeof(packets) == 0)
        return 0;
    foreach (packet in packets) {
        if (!mapp(packet) || packet["kind"] != "chat_message")
            return 0;
    }
    return 1;
}

int entry_is_chat_only(mapping entry)
{
    if (!mapp(entry))
        return 0;
    if (entry["chat_only"] == 1 && entry_has_only_chat_messages(entry))
        return 1;
    if (!entry_has_only_chat_messages(entry)) {
        map_delete(entry, "chat_only");
        return 0;
    }
    entry["chat_only"] = 1;
    return 1;
}

void reset_pending_chat_queue()
{
    _pending_chat_sync_queue = ([]);
    _pending_chat_sync_queue_ticket = ([]);
    _pending_chat_sync_queue_head = 1;
    _pending_chat_sync_queue_tail = 1;
    _pending_chat_sync_active_wave_tail = 0;
}

private void reset_pending_chat_queue_if_idle()
{
    if (mapp(_pending_chat_sync_plans)
        && sizeof(_pending_chat_sync_plans) > 0)
        return;
    /* The active flush owns its ticket ceiling until the wave closes. */
    if (_pending_chat_sync_active_wave_tail > 0)
        return;
    reset_pending_chat_queue();
}

void enqueue_pending_chat_key(string key)
{
    int ticket;

    if (!stringp(key) || key == "")
        return;
    if (!mapp(_pending_chat_sync_queue)
        || !mapp(_pending_chat_sync_queue_ticket))
        reset_pending_chat_queue();
    ticket = intp(_pending_chat_sync_queue_ticket[key])
        ? _pending_chat_sync_queue_ticket[key] : 0;
    if (ticket > 0)
        return;

    ticket = _pending_chat_sync_queue_tail++;
    _pending_chat_sync_queue[ticket] = key;
    _pending_chat_sync_queue_ticket[key] = ticket;
}

void remove_pending_chat_entry(string key)
{
    int ticket;

    if (!stringp(key) || key == "")
        return;
    ticket = mapp(_pending_chat_sync_queue_ticket)
        && intp(_pending_chat_sync_queue_ticket[key])
            ? _pending_chat_sync_queue_ticket[key] : 0;
    if (!mapp(_pending_chat_sync_plans[key]) && ticket <= 0)
        return;
    map_delete(_pending_chat_sync_plans, key);
    map_delete(_pending_chat_sync_queue_ticket, key);
    reset_pending_chat_queue_if_idle();
}

mixed *take_pending_chat_entry()
{
    mapping entry;
    string key;
    int queue_ticket;
    int ticket;

    if (!mapp(_pending_chat_sync_queue)
        || !mapp(_pending_chat_sync_queue_ticket))
        return ({ });

    if (_pending_chat_sync_queue_head >= _pending_chat_sync_queue_tail)
        return ({ });

    queue_ticket = _pending_chat_sync_queue_head++;
    key = _pending_chat_sync_queue[queue_ticket];
    map_delete(_pending_chat_sync_queue, queue_ticket);
    if (!stringp(key) || key == "")
        return ({ "", ([]) });

    ticket = intp(_pending_chat_sync_queue_ticket[key])
        ? _pending_chat_sync_queue_ticket[key] : 0;
    if (ticket != queue_ticket)
        return ({ "", ([]) });

    entry = mapp(_pending_chat_sync_plans[key])
        ? _pending_chat_sync_plans[key] : 0;
    if (!mapp(entry)) {
        map_delete(_pending_chat_sync_queue_ticket, key);
        return ({ "", ([]) });
    }

    remove_pending_chat_entry(key);
    return ({ key, entry });
}

int packets_have_chat_messages(mixed packets)
{
    mixed packet;

    if (!arrayp(packets))
        return 0;
    foreach (packet in packets) {
        if (mapp(packet) && packet["kind"] == "chat_message")
            return 1;
    }
    return 0;
}

void stamp_chat_packet_pending_age(mixed packets)
{
    mixed packet;
    int queued_at;

    if (!arrayp(packets))
        return;
    queued_at = perf_counter_ns();
    foreach (packet in packets) {
        if (mapp(packet) && packet["kind"] == "chat_message"
            && (!intp(packet["chat_pending_since_ns"])
                || packet["chat_pending_since_ns"] <= 0))
            packet["chat_pending_since_ns"] = queued_at;
    }
}

mapping partition_immediate_packets(mixed packets)
{
    mixed *chat;
    mixed *normal;
    mixed *normalized;
    mixed packet;
    int chat_count;
    int normal_count;

    normalized = normalize_immediate_packets(packets);
    chat = allocate(sizeof(normalized));
    normal = allocate(sizeof(normalized));
    foreach (packet in normalized) {
        if (mapp(packet) && packet["kind"] == "chat_message")
            chat[chat_count++] = packet;
        else
            normal[normal_count++] = packet;
    }
    return ([
        "chat": chat_count > 0 ? chat[0..chat_count - 1] : ({ }),
        "normal": normal_count > 0 ? normal[0..normal_count - 1] : ({ }),
    ]);
}
