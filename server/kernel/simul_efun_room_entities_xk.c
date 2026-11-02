/**
 * @file    simul_efun_room_entities_xk.c
 * @brief   房间实体列表 XK 协议发送辅助子服务
 * @author  XiaKeXing Development Team
 * @date    2026-03-30
 * @version 1.0.0
 */

#pragma strict_types

#include <system/services.h>
#include <system/constants.h>
#include <system/xk_protocol.h>

#define XK_PROTOCOL_CORE "/kernel/framework/xk_protocol_core"
#define XK_INVENTORY_PAYLOAD_CACHE_TTL 5
#define XK_INVENTORY_INLINE_ARTIFACT_MAX_ITEMS 8

private mapping serialize_named_entity(object ob)
{
    string object_id;
    string object_name;
    mixed object_id_raw;
    mixed object_name_raw;

    if (!objectp(ob))
        return 0;

    object_id_raw = call_other("/kernel/simul_efun", "get_object_id", ob);
    object_name_raw = ob->name() || ob->query("name") || "";
    if (arrayp(object_id_raw) && sizeof(object_id_raw) > 0)
        object_id = object_id_raw[0];
    else if (stringp(object_id_raw))
        object_id = object_id_raw;
    else
        object_id = file_name(ob);

    if (arrayp(object_name_raw) && sizeof(object_name_raw) > 0)
        object_name = object_name_raw[0];
    else if (stringp(object_name_raw))
        object_name = object_name_raw;
    else
        object_name = "";

    return ([
        "id": object_id,
        "name": object_name,
    ]);
}

private mapping serialize_inventory_item(object item, int inventory_mode, object me);
private mapping query_inventory_owner_snapshot(object player, object *items,
    string worker_state);
private int should_serialize_npc(object npc);
varargs mapping xk_build_objects_payload(object *items, object me);
varargs mapping xk_build_inventory_payload(object *items, object me);
varargs mapping xk_build_npcs_payload(mixed *npcs, object me);
varargs void xk_prime_inventory_payload_cache(object player, int force_refresh);
void xk_refresh_inventory_payload_cache(object player);
void invalidate_inventory_payload_cache(object player);
void cancel_inventory_read_model_refresh(object player);
private string normalize_inventory_worker_state(mixed worker_state);

int query_xk_inventory_generation(object player)
{
    mixed generation;

    if (!objectp(player))
        return 0;
    generation = player->query_temp("xk/inventory_generation");
    return intp(generation) && generation >= 0 ? generation : 0;
}

private mixed *copy_mapping_array(mixed *values)
{
    mixed *result;
    int index;

    if (!arrayp(values))
        return ({ });

    result = allocate(sizeof(values));
    for (index = 0; index < sizeof(values); index++)
        result[index] = mapp(values[index]) ? copy(values[index]) : values[index];
    return result;
}

private mapping copy_inventory_payload(mapping payload)
{
    if (!mapp(payload))
        return ([ ]);

    return ([
        "items": copy_mapping_array(payload["items"]),
        "capacity": intp(payload["capacity"]) ? payload["capacity"] : 0,
        "used_slots": intp(payload["used_slots"]) ? payload["used_slots"] : 0,
        "total_count": intp(payload["total_count"]) ? payload["total_count"] : 0,
        "read_side_worker": intp(payload["read_side_worker"])
            ? payload["read_side_worker"]
            : 1,
        "read_side_worker_state": normalize_inventory_worker_state(
            payload["read_side_worker_state"]),
    ]);
}

private string normalize_inventory_worker_state(mixed worker_state)
{
    return stringp(worker_state) && worker_state != ""
        ? worker_state
        : "pending";
}

private string promote_cached_inventory_worker_state(mixed worker_state)
{
    string normalized;

    normalized = normalize_inventory_worker_state(worker_state);
    if (normalized == "ready" || normalized == "ready_cached")
        return "ready_cached";
    if (normalized == "pending" || normalized == "pending_cached")
        return "pending_cached";
    return normalized;
}

private mapping query_cached_inventory_payload(object player)
{
    mapping entry;

    if (!objectp(player))
        return 0;

    entry = player->query_temp("xk/inventory_payload_cache");
    if (!mapp(entry))
        return 0;
    if (!intp(entry["expires_at"]) || entry["expires_at"] < time()) {
        player->delete_temp("xk/inventory_payload_cache");
        return 0;
    }
    if (!intp(entry["generation"])
        || entry["generation"] != query_xk_inventory_generation(player)) {
        player->delete_temp("xk/inventory_payload_cache");
        return 0;
    }
    if (!mapp(entry["payload"])
        || !intp(entry["payload"]["read_side_worker"])
        || !stringp(entry["payload"]["read_side_worker_state"])
        || entry["payload"]["read_side_worker_state"] == "") {
        player->delete_temp("xk/inventory_payload_cache");
        return 0;
    }
    entry = copy_inventory_payload(entry["payload"]);
    entry["read_side_worker_state"] = promote_cached_inventory_worker_state(
        entry["read_side_worker_state"]);
    return entry;
}

private void cache_inventory_payload(object player, mapping payload)
{
    if (!objectp(player) || !mapp(payload))
        return;

    player->set_temp("xk/inventory_payload_cache", ([
        "expires_at": time() + XK_INVENTORY_PAYLOAD_CACHE_TTL,
        "generation": query_xk_inventory_generation(player),
        "payload": copy_inventory_payload(payload),
    ]));
}

void xk_cache_inventory_payload(object player, mapping payload)
{
    cache_inventory_payload(player, payload);
}

void invalidate_inventory_payload_cache(object player)
{
    mixed handle;

    if (!objectp(player))
        return;

    player->delete_temp("xk/inventory_payload_cache");
    player->set_temp("xk/inventory_generation",
        query_xk_inventory_generation(player) + 1);
    if (!function_exists("claim_owner_read_model_refresh", player)
        || !player->claim_owner_read_model_refresh("inventory", 0))
        return;
    handle = player->query_temp("xk/inventory_read_model_refresh_handle");
    if (intp(handle) && handle > 0)
        return;
    handle = call_out_walltime_background(
        "xk_refresh_inventory_payload_cache", 0, player);
    player->set_temp("xk/inventory_read_model_refresh_handle", handle);
}

void cancel_inventory_read_model_refresh(object player)
{
    mixed handle;

    if (!objectp(player))
        return;
    handle = player->query_temp("xk/inventory_read_model_refresh_handle");
    if (intp(handle) && handle > 0)
        remove_call_out(handle);
    player->delete_temp("xk/inventory_read_model_refresh_handle");
}

varargs void xk_prime_inventory_payload_cache(object player, int force_refresh)
{
    if (!objectp(player) || !interactive(player))
        return;
    if (!function_exists("claim_owner_read_model_refresh", player)
        || !player->claim_owner_read_model_refresh("inventory", force_refresh))
        return;
    xk_refresh_inventory_payload_cache(player);
}

void xk_refresh_inventory_payload_cache(object player)
{
    mapping payload;
    mapping snapshot;
    object *items;
    string input_token;
    string worker_state;
    int snapshot_started_at;
    int fallback_started_at;

    if (!objectp(player))
        return;
    player->delete_temp("xk/inventory_read_model_refresh_handle");
    if (!interactive(player))
        return;

    snapshot_started_at = perf_counter_ns();
    worker_state = "ready";
    call_other(ITEM_INVENTORY_D, "normalize_unique_inventory_items", player);
    items = all_inventory(player);
    snapshot = query_inventory_owner_snapshot(player, items, worker_state);
    if (function_exists("record_owner_protocol_projection_timing", player))
        player->record_owner_protocol_projection_timing("snapshot", snapshot,
            perf_counter_ns() - snapshot_started_at);
    input_token = sprintf("%d", snapshot["generation"]);
    if (function_exists("publish_read_model_artifact", player)
        && player->publish_read_model_artifact("inventory", snapshot,
            input_token))
        return;
    fallback_started_at = perf_counter_ns();
    payload = CLIENT_PROJECTION_D->project_inventory_payload_snapshot(snapshot);
    payload["read_side_worker"] = 0;
    payload["read_side_worker_state"] = "main_thread";
    cache_inventory_payload(player, payload);
    if (function_exists("record_owner_protocol_projection_timing", player))
        player->record_owner_protocol_projection_timing("sync_fallback", snapshot,
            perf_counter_ns() - fallback_started_at);
}

private mixed *serialize_inventory_payload(object *items, int inventory_mode, object me)
{
    mixed *items_buffer;
    mixed *items_payload;
    object item;
    mapping item_payload;
    int item_count;

    if (!arrayp(items) || sizeof(items) == 0)
        return ({ });

    items_buffer = allocate(sizeof(items));
    item_count = 0;

    foreach (item in items) {
        if (!objectp(item))
            continue;

        item_payload = serialize_inventory_item(item, inventory_mode, me);
        if (mapp(item_payload))
            items_buffer[item_count++] = item_payload;
    }

    items_payload = item_count > 0 ? items_buffer[0..item_count - 1] : ({ });
    return items_payload;
}

private mapping query_inventory_owner_snapshot(object player, object *items,
    string worker_state)
{
    mixed *facts;
    object item;
    mapping fact;
    int fact_count;
    int total_count;

    if (!arrayp(items))
        items = ({ });
    facts = allocate(sizeof(items));
    total_count = 0;
    foreach (item in items) {
        if (!objectp(item))
            continue;
        fact = CLIENT_PROJECTION_D->query_inventory_item_projection_snapshot(item);
        if (mapp(fact))
            facts[fact_count++] = fact;
        total_count += call_other(ITEM_INVENTORY_D,
            "query_item_stack_amount", item);
    }

    return ([
        "payload_key": "xk/inventory-owner-projection/v1",
        "projection_kind": "inventory",
        "generation": query_xk_inventory_generation(player),
        "items": fact_count > 0 ? facts[0..fact_count - 1] : ({ }),
        "capacity": INVENTORY_CAPACITY,
        "used_slots": call_other(ITEM_INVENTORY_D,
            "query_inventory_kind_count", player),
        "total_count": total_count,
        "read_side_worker": 1,
        "read_side_worker_state": normalize_inventory_worker_state(worker_state),
    ]);
}

private mixed *serialize_npc_payload(mixed *npcs, object me)
{
    mixed *npcs_buffer;
    mixed *npcs_payload;
    mixed npc;
    mapping npc_payload;
    int npc_count;

    if (!arrayp(npcs) || sizeof(npcs) == 0)
        return ({ });

    npcs_buffer = allocate(sizeof(npcs));
    npc_count = 0;

    foreach (npc in npcs) {
        if (mapp(npc))
            npc_payload = copy(npc);
        else {
            if (!should_serialize_npc(npc))
                continue;
            npc_payload = CLIENT_PROJECTION_D->build_room_npc_payload(npc, me);
        }

        if (mapp(npc_payload))
            npcs_buffer[npc_count++] = npc_payload;
    }

    npcs_payload = npc_count > 0 ? npcs_buffer[0..npc_count - 1] : ({ });
    return npcs_payload;
}

private int send_inventory_packet(object me, string type, object *items,
    int inventory_mode, mapping response_context)
{
    mapping payload;
    mapping read_model;
    mapping inline_read_model;
    mapping snapshot;
    string input_token;
    string worker_state;
    int artifact_result;
    int snapshot_started_at;
    int fallback_started_at;

    if (!me || !interactive(me) || !stringp(type) || strlen(type) != 4)
        return 0;

    if (inventory_mode) {
        input_token = sprintf("%d", query_xk_inventory_generation(me));
        if (function_exists("query_owner_read_model", me)) {
            read_model = me->query_owner_read_model(
                "inventory", input_token, 0);
            if (mapp(read_model) && read_model["state"] == "ready"
                && mapp(read_model["preencoded"])) {
                artifact_result = call_other(XK_PROTOCOL_CORE,
                    "xk_send_preencoded_artifact", me, type,
                    read_model["preencoded"], response_context);
                /* L-36：1/-1（已填充待对账）都已投递，不得回退重发。 */
                if (intp(artifact_result) && artifact_result != 0)
                    return artifact_result;
            }
        }

        payload = query_cached_inventory_payload(me);
        if (mapp(payload)) {
            return call_other(XK_PROTOCOL_CORE, "xk_send", me, type,
                payload, response_context);
        }

        snapshot_started_at = perf_counter_ns();
        call_other(ITEM_INVENTORY_D, "normalize_unique_inventory_items", me);
        items = all_inventory(me);
        worker_state = "ready";
        snapshot = query_inventory_owner_snapshot(me, items, worker_state);
        if (function_exists("record_owner_protocol_projection_timing", me))
            me->record_owner_protocol_projection_timing("snapshot", snapshot,
                perf_counter_ns() - snapshot_started_at);
        input_token = sprintf("%d", snapshot["generation"]);
        if (sizeof(snapshot["items"]) <= XK_INVENTORY_INLINE_ARTIFACT_MAX_ITEMS) {
            if (function_exists("publish_inline_inventory_read_model", me)
                && mapp(inline_read_model = me->publish_inline_inventory_read_model(
                    snapshot, input_token))) {
                if (inline_read_model["state"] == "ready"
                    && mapp(inline_read_model["preencoded"])) {
                    artifact_result = call_other(XK_PROTOCOL_CORE,
                        "xk_send_preencoded_artifact", me, type,
                        inline_read_model["preencoded"], response_context);
                    /* L-36：-1（已填充待对账）不得回退重发。 */
                    if (intp(artifact_result) && artifact_result != 0)
                        return artifact_result;
                }
            }
        } else if (function_exists("submit_owner_read_model_output", me)
            && me->submit_owner_read_model_output(
                "inventory", snapshot, input_token, type))
            return 1;

        if (function_exists("record_owner_protocol_sync_fallback", me))
            me->record_owner_protocol_sync_fallback("inventory",
                "owner read model not current");
        fallback_started_at = perf_counter_ns();
        payload = CLIENT_PROJECTION_D->project_inventory_payload_snapshot(snapshot);
        payload["read_side_worker"] = 0;
        payload["read_side_worker_state"] = "main_thread";
            if (function_exists("record_owner_protocol_projection_timing", me))
            me->record_owner_protocol_projection_timing("sync_fallback", snapshot,
                perf_counter_ns() - fallback_started_at);
        cache_inventory_payload(me, payload);
    } else
        payload = xk_build_objects_payload(items, me);

    return call_other(XK_PROTOCOL_CORE, "xk_send", me, type, payload,
        response_context);
}

private int should_hide_room_item_for_player(object item, object me)
{
    string chest_id;

    if (!objectp(item) || !objectp(me))
        return 0;
    chest_id = item->query("exploration_chest_id") || item->query("chest_id") || "";
    if (chest_id == "")
        return 0;
    return EXPLORE_REWARD_D->query_chest_open_count(me, chest_id) > 0;
}

private mapping serialize_inventory_item(object item, int inventory_mode, object me)
{
    if (!inventory_mode && should_hide_room_item_for_player(item, me))
        return 0;
    if (inventory_mode)
        return CLIENT_PROJECTION_D->build_inventory_item_payload(item);
    return CLIENT_PROJECTION_D->build_item_list_payload(item, me);
}

private int should_serialize_npc(object npc)
{
    int hp;
    int max_hp;

    if (!objectp(npc))
        return 0;

    if (npc->query_temp("is_dying"))
        return 0;

    hp = npc->query("combat/hp");
    max_hp = npc->query("combat/max_hp");
    if (intp(max_hp) && max_hp > 0 && intp(hp) && hp <= 0)
        return 0;

    return 1;
}

varargs int xk_send_objects(object me, object *items,
    mapping response_context)
{
    return send_inventory_packet(me, XK_ITEM, items, 0, response_context);
}

varargs int xk_send_inventory(object me, object *items,
    mapping response_context)
{
    return send_inventory_packet(me, XK_IVTY, items, 1, response_context);
}

varargs void xk_send_npcs(object me, mixed *npcs,
    mapping response_context)
{
    if (!me || !interactive(me))
        return;
    call_other(XK_PROTOCOL_CORE, "xk_send", me, XK_NPCS,
        xk_build_npcs_payload(npcs, me), response_context);
}

varargs mapping xk_build_objects_payload(object *items, object me)
{
    return ([
        "items": serialize_inventory_payload(items, 0, me),
    ]);
}

varargs mapping xk_build_inventory_payload(object *items, object me)
{
    mapping payload;
    mapping snapshot;
    string worker_state;
    int snapshot_started_at;
    int fallback_started_at;

    payload = query_cached_inventory_payload(me);
    if (mapp(payload))
        return payload;

    worker_state = "main_thread";
    if (objectp(me)) {
        snapshot_started_at = perf_counter_ns();
        call_other(ITEM_INVENTORY_D, "normalize_unique_inventory_items", me);
        items = all_inventory(me);
    } else if (!arrayp(items))
        items = ({ });

    snapshot = query_inventory_owner_snapshot(me, items, worker_state);
    if (objectp(me)
        && function_exists("record_owner_protocol_projection_timing", me))
        me->record_owner_protocol_projection_timing("snapshot", snapshot,
            perf_counter_ns() - snapshot_started_at);
    fallback_started_at = perf_counter_ns();
    payload = CLIENT_PROJECTION_D->project_inventory_payload_snapshot(snapshot);
    payload["read_side_worker"] = 0;
    payload["read_side_worker_state"] = "main_thread";
    if (objectp(me)
        && function_exists("record_owner_protocol_projection_timing", me))
        me->record_owner_protocol_projection_timing("sync_fallback", snapshot,
            perf_counter_ns() - fallback_started_at);
    cache_inventory_payload(me, payload);
    return payload;
}

varargs mapping xk_build_npcs_payload(mixed *npcs, object me)
{
    return ([
        "npcs": serialize_npc_payload(npcs, me),
    ]);
}
