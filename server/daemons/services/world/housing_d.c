/**
 * @file    housing_d.c
 * @brief   住房守护进程,管理玩家房产与产权归属。
 */

#pragma strict_types
#pragma save_types

#include <system/globals.h>
#include <system/services.h>

inherit "/lib/framework/daemon";

mapping _players;
mapping _property_owners;
mapping _pending_rent_operations;
nosave mapping _property_rooms;
nosave int _restore_failed;
nosave string _last_persist_result;
nosave string _last_persist_error;
nosave mapping _last_housing_save_phase;

private string ensure_player_id(object player)
{
    string player_id;

    if (!objectp(player))
        return 0;

    player_id = get_object_id(player);
    return stringp(player_id) && player_id != "" ? player_id : 0;
}

private mapping query_housing_config()
{
    mapping config;

    config = CONFIG_D->query_full_config("housing");
    return mapp(config) ? config : ([]);
}

private mapping query_property_catalog()
{
    mapping config;

    config = query_housing_config();
    return mapp(config["properties"]) ? config["properties"] : ([]);
}

private string get_housing_save_file()
{
    return DATA_DIR + "runtime/housing_d";
}

private string query_room_template_path()
{
    return "/lib/base/housing_room";
}

private mixed deep_copy_value(mixed value)
{
    mixed restored;

    if (catch(restored = restore_variable(save_variable(value))))
        return 0;
    return restored;
}

private void restore_housing_data()
{
    string file;
    mixed err;
    int restored;

    _restore_failed = 0;
    _last_persist_result = "not_attempted";
    _last_persist_error = "";
    file = get_housing_save_file();
    if (file_size(file + ".o") <= 0) {
        _players = ([]);
        _property_owners = ([]);
        _pending_rent_operations = ([]);
        return;
    }

    err = catch(restored = restore_object(file));
    if (err || !restored || !mapp(_players) || !mapp(_property_owners)) {
        _players = ([]);
        _property_owners = ([]);
        _pending_rent_operations = ([]);
        _restore_failed = 1;
        _last_persist_result = "restore_failed";
        _last_persist_error = err ? sprintf("%O", err)
            : !restored ? "restore_object returned failure"
            : "restored housing state is incomplete";
        log_file("system/housing", sprintf(
            "housing restore failed; save frozen: %s\n", _last_persist_error));
        return;
    }
    if (!mapp(_pending_rent_operations))
        _pending_rent_operations = ([]);
}

private int persist_housing_data()
{
    string file;
    string runtime_dir;
    mixed err;
    mapping detail;

    if (_restore_failed) {
        _last_persist_result = "restore_failed";
        _last_persist_error = "housing restore failed; refusing to overwrite save";
        return 0;
    }

    file = get_housing_save_file();
    runtime_dir = DATA_DIR + "runtime";
    if (file_size(runtime_dir) == -1)
        mkdir(runtime_dir);
    if (!mapp(_players))
        _players = ([]);
    if (!mapp(_property_owners))
        _property_owners = ([]);
    if (!mapp(_pending_rent_operations))
        _pending_rent_operations = ([]);
    if (!mapp(_property_rooms))
        _property_rooms = ([]);

    err = catch(detail = owner_save_object_phases(file,
        "owner/global/service/world/housing", "housing_state", ([
            "player_count": sizeof(_players),
            "property_owner_count": sizeof(_property_owners),
            "pending_rent_count": sizeof(_pending_rent_operations),
            "property_room_count": sizeof(_property_rooms),
            "state_digest": hash("sha256", save_variable(_players)
                + save_variable(_property_owners)
                + save_variable(_pending_rent_operations)
                + save_variable(_property_rooms)),
        ]), "housing/save/" + hash("sha256", save_variable(_players)
            + save_variable(_property_owners)
            + save_variable(_pending_rent_operations)
            + save_variable(_property_rooms))));
    if (err || !mapp(detail)) {
        _last_housing_save_phase = ([ "outcome": "unknown", "persisted": -1,
            "save_result": 0, "error": err ? sprintf("%O", err)
                : "housing owner save result unavailable" ]);
        _last_persist_result = "unknown";
        _last_persist_error = _last_housing_save_phase["error"];
        return 0;
    }
    _last_housing_save_phase = copy(detail);
    if (detail["ok"] == 1) {
        _last_persist_result = "success";
        _last_persist_error = "";
        return 1;
    }
    _last_persist_result = detail["outcome"] || "unknown";
    _last_persist_error = detail["error"] || "housing save incomplete";
    return detail["persisted"] == 1 ? 2 : 0;
}

private int save_housing_data()
{
    return persist_housing_data();
}

private mapping snapshot_housing_state()
{
    return ([
        "players": deep_copy_value(_players || ([])),
        "property_owners": deep_copy_value(_property_owners || ([])),
        "pending_rent_operations": deep_copy_value(
            _pending_rent_operations || ([])),
    ]);
}

private void restore_housing_state(mapping snapshot)
{
    _players = mapp(snapshot) && mapp(snapshot["players"])
        ? snapshot["players"] : ([]);
    _property_owners = mapp(snapshot) && mapp(snapshot["property_owners"])
        ? snapshot["property_owners"] : ([]);
    _pending_rent_operations = mapp(snapshot)
        && mapp(snapshot["pending_rent_operations"])
        ? snapshot["pending_rent_operations"] : ([]);
}

private int commit_housing_state(mapping snapshot, string operation)
{
    int save_result;

    save_result = save_housing_data();
    if (save_result == 1)
        return 1;
    /* A durable candidate or an unknown publication must not be reverted as
     * if no file write occurred.  The phase owner keeps the retry identity. */
    if (mapp(_last_housing_save_phase)
        && _last_housing_save_phase["outcome"] != "not_written") {
        log_file("system/housing", sprintf(
            "%s retained after persistence phase %s: %s\n",
            stringp(operation) ? operation : "mutation",
            _last_housing_save_phase["outcome"] || "unknown",
            _last_persist_error));
        return 0;
    }
    restore_housing_state(snapshot);
    log_file("system/housing", sprintf(
        "%s rejected after persistence failure: %s\n",
        stringp(operation) ? operation : "mutation", _last_persist_error));
    return 0;
}

mapping query_persistence_status()
{
    return ([
        "restore_failed": _restore_failed,
        "last_persist_result": _last_persist_result,
        "last_persist_error": _last_persist_error,
        "pending_rent_count": sizeof(_pending_rent_operations || ([])),
        "save_phase": mapp(_last_housing_save_phase)
            ? copy(_last_housing_save_phase) : ([ "outcome": "not_attempted" ]),
    ]);
}

private mapping ensure_player_bucket(string player_id)
{
    if (!stringp(player_id) || player_id == "")
        return 0;
    if (!mapp(_players[player_id]))
        _players[player_id] = ([ "properties": ([ ]) ]);
    if (!mapp(_players[player_id]["properties"]))
        _players[player_id]["properties"] = ([ ]);
    return _players[player_id];
}

private mapping query_player_bucket(string player_id)
{
    return mapp(_players[player_id]) ? _players[player_id] : 0;
}

private mapping query_player_property_state_by_id(string player_id, string property_id)
{
    mapping bucket;

    bucket = query_player_bucket(player_id);
    if (!mapp(bucket) || !mapp(bucket["properties"]))
        return 0;
    if (!mapp(bucket["properties"][property_id]))
        return 0;
    if (!arrayp(bucket["properties"][property_id]["storage"]))
        bucket["properties"][property_id]["storage"] = ({ });
    if (!arrayp(bucket["properties"][property_id]["guests"]))
        bucket["properties"][property_id]["guests"] = ({ });
    return bucket["properties"][property_id];
}

private mapping peek_player_property_state_by_id(string player_id, string property_id)
{
    mapping bucket;

    bucket = query_player_bucket(player_id);
    if (!mapp(bucket) || !mapp(bucket["properties"]))
        return 0;
    return mapp(bucket["properties"][property_id])
        ? bucket["properties"][property_id] : 0;
}

private mapping query_property_definition(string property_id)
{
    mapping catalog;

    catalog = query_property_catalog();
    return mapp(catalog[property_id]) ? copy(catalog[property_id]) : 0;
}

private int player_can_access_property(string player_id, mapping property_state)
{
    if (!stringp(player_id) || player_id == "" || !mapp(property_state))
        return 0;
    if (player_id == (property_state["owner_id"] || ""))
        return 1;
    return member_array(player_id, property_state["guests"] || ({ })) != -1;
}

private object ensure_property_room(string property_id)
{
    mapping property_def;
    mapping property_state;
    object room;
    string owner_id;

    if (!stringp(property_id) || property_id == "")
        return 0;
    if (objectp(_property_rooms[property_id]))
        return _property_rooms[property_id];

    owner_id = _property_owners[property_id] || "";
    property_def = query_property_definition(property_id);
    property_state = query_player_property_state_by_id(owner_id, property_id);
    if (!mapp(property_def) || !mapp(property_state))
        return 0;

    room = new(query_room_template_path());
    if (!objectp(room))
        return 0;
    if (!room->configure_housing_room(property_def, property_id, owner_id)) {
        destruct(room);
        return 0;
    }
    _property_rooms[property_id] = room;
    return room;
}

private int evict_room_occupants(string property_id, string reason)
{
    mixed occupant;
    mixed *occupants;
    mapping moved;
    mapping property_def;
    object room;
    object target_room;
    string anchor_room;
    int all_moved;

    room = objectp(_property_rooms[property_id]) ? _property_rooms[property_id] : 0;
    if (!objectp(room))
        return 1;

    property_def = query_property_definition(property_id);
    anchor_room = mapp(property_def) ? (property_def["anchor_room"] || CONFIG_D->query_start_room()) : CONFIG_D->query_start_room();
    target_room = load_object(anchor_room);
    if (!objectp(target_room))
        return 0;

    occupants = all_inventory(room);
    all_moved = 1;
    foreach (occupant in occupants) {
        if (!objectp(occupant))
            continue;
        if (userp(occupant) || (function_exists("is_character", occupant) && occupant->is_character())) {
            moved = WORLD_TRAVEL_D->move_actor(occupant, target_room, ([
                "reason": "housing_evict",
            ]));
            if (!mapp(moved) || !moved["ok"]) {
                all_moved = 0;
                continue;
            }
            if (userp(occupant))
                msg_send(occupant, sprintf("你离开了房屋室内空间。原因：%s。\n", reason));
        }
    }
    return all_moved;
}

private int cleanup_property_room(string property_id)
{
    object room;

    room = objectp(_property_rooms[property_id]) ? _property_rooms[property_id] : 0;
    if (!objectp(room))
        return 1;

    if (!evict_room_occupants(property_id, "房屋状态已失效"))
        return 0;
    destruct(room);
    map_delete(_property_rooms, property_id);
    return 1;
}

private void reconcile_property_rooms()
{
    mapping property_def;
    object room;
    string property_id;
    string owner_id;
    string *property_ids;

    property_ids = keys(_property_rooms || ([]));
    foreach (property_id in property_ids) {
        room = _property_rooms[property_id];
        property_def = query_property_definition(property_id);
        if (!mapp(property_def)) {
            cleanup_property_room(property_id);
            continue;
        }
        if (!objectp(room)) {
            map_delete(_property_rooms, property_id);
            continue;
        }
        owner_id = _property_owners[property_id] || "";
        if (!room->configure_housing_room(property_def, property_id, owner_id))
            log_file("system/housing", sprintf(
                "housing room reconfigure failed: property=%s\n", property_id));
    }
}

private int query_seconds_from_days(int days, int fallback)
{
    if (!intp(days) || days <= 0)
        days = fallback;
    return days * 86400;
}

private int query_property_rent_seconds()
{
    return query_seconds_from_days(ECONOMY_SETTLEMENT_D->query_housing_default_rent_days(), 7);
}

private int query_property_tax_seconds()
{
    return query_seconds_from_days(ECONOMY_SETTLEMENT_D->query_housing_default_tax_days(), 1);
}

private int query_property_tax_grace_seconds()
{
    return query_seconds_from_days(ECONOMY_SETTLEMENT_D->query_housing_tax_grace_days(), 3);
}

private int query_storage_slot_limit(mapping property_def)
{
    if (!mapp(property_def))
        return 0;
    return intp(property_def["storage_slots"]) && property_def["storage_slots"] > 0
        ? property_def["storage_slots"]
        : 8;
}

private string format_time_line(int timestamp)
{
    if (!intp(timestamp) || timestamp <= 0)
        return "未设置";
    return ctime(timestamp)[4..15];
}

private string query_attachment_display_name(mapping attachment)
{
    string template_id;

    if (!mapp(attachment))
        return "物品";
    if (stringp(attachment["item_name"]) && attachment["item_name"] != "")
        return attachment["item_name"];
    template_id = attachment["template_id"] || attachment["lookup_id"] || "";
    if (template_id != "" && stringp(ITEM_D->get_item_name(template_id)))
        return ITEM_D->get_item_name(template_id);
    return "物品";
}

private mapping build_release_mail_reward(mapping property_state)
{
    mixed *attachments;

    attachments = arrayp(property_state["storage"]) ? copy(property_state["storage"]) : ({ });
    return ([ "attachments": attachments ]);
}

private int deliver_release_storage(string player_id, object player, mapping property_state,
    mapping property_def, string reason)
{
    mixed *attachments;
    mapping rewards;

    attachments = arrayp(property_state["storage"]) ? copy(property_state["storage"]) : ({ });
    if (!sizeof(attachments))
        return 1;

    rewards = build_release_mail_reward(property_state);
    if (objectp(player)) {
        if (REWARD_APPLY_D->apply_reward_package(player, rewards,
                ([
                    "drop_on_full": 1,
                    "source": "housing_release",
                    "persist": 1,
                    "idempotent": 1,
                    "target_key": sprintf("housing/release/%s/%s/%d",
                        player_id, property_state["property_id"] || "",
                        property_state["rented_at"] || 0),
                ]))["ok"]) {
            property_state["storage"] = ({ });
            return 1;
        }
    }

    if (MAIL_D->send_system_mail(player_id,
            sprintf("房屋释放：%s", property_def["name"] || property_def["id"] || "房屋"),
            sprintf("你的房屋已被释放，仓储物品已退还。原因：%s。", reason),
            attachments, 0)) {
        property_state["storage"] = ({ });
        return 1;
    }

    return 0;
}

private void forget_property_owner(string property_id)
{
    if (!stringp(property_id) || property_id == "")
        return;
    map_delete(_property_owners, property_id);
}

private mapping release_property_internal(string player_id, object player,
    string property_id, mapping property_state, mapping property_def, string reason)
{
    mapping bucket;
    mapping snapshot;

    bucket = ensure_player_bucket(player_id);
    if (!mapp(bucket) || !mapp(property_state) || !mapp(property_def))
        return ([ "success": 0, "message": "房屋释放失败。\n" ]);

    snapshot = snapshot_housing_state();
    if (!cleanup_property_room(property_id))
        return ([
            "success": 0,
            "message": "房屋内仍有成员未能驱逐，暂不释放房屋归属。\n",
            "property_id": property_id,
            "reason": reason,
        ]);

    if (!deliver_release_storage(player_id, player, property_state, property_def, reason))
        return ([ "success": 0, "message": "仓储物品退还失败，当前无法释放房屋。\n" ]);

    map_delete(bucket["properties"], property_id);
    forget_property_owner(property_id);
    if (!commit_housing_state(snapshot, "release_property")) {
        return ([
            "success": 0,
            "message": "房屋释放未能持久化，已保留原房屋账本。\n",
            "property_id": property_id,
            "reason": reason,
        ]);
    }
    return ([
        "success": 1,
        "message": sprintf("房屋 %s 已释放。\n", property_def["name"] || property_id),
        "property_id": property_id,
        "reason": reason,
    ]);
}

private mapping prepare_rent_operation(string action, string player_id,
    string property_id, mapping property_def, mapping candidate);
private mapping apply_rent_operation(object player, mapping record);

private mapping settle_property_state(object player, string player_id, string property_id)
{
    int guard;
    int now;
    int grace_seconds;
    int rent_seconds;
    int tax_seconds;
    mapping property_def;
    mapping property_state;
    mapping result;
    mapping snapshot;
    mapping tax_candidate;
    mapping prepared;
    mapping applied;

    property_state = query_player_property_state_by_id(player_id, property_id);
    property_def = query_property_definition(property_id);
    if (!mapp(property_state) || !mapp(property_def))
        return ([ "ok": 0, "state": 0, "released": 0 ]);
    snapshot = snapshot_housing_state();

    now = time();
    rent_seconds = query_property_rent_seconds();
    tax_seconds = query_property_tax_seconds();
    grace_seconds = query_property_tax_grace_seconds();

    if ((property_state["rent_expires_at"] || 0) > 0 && now > property_state["rent_expires_at"] + grace_seconds) {
        result = release_property_internal(player_id, player, property_id, property_state, property_def, "rent_expired");
        if (!result["success"]) {
            restore_housing_state(snapshot);
            return ([ "ok": 0, "released": 0, "result": result ]);
        }
        return ([ "ok": 1, "released": 1, "result": result ]);
    }

    if ((property_state["rent_expires_at"] || 0) > 0 && now > property_state["rent_expires_at"])
        property_state["status"] = "overdue";

    guard = 0;
    while ((property_state["tax_due_at"] || 0) > 0
        && property_state["tax_due_at"] <= now
        && guard < 16) {
        guard += 1;
        tax_candidate = deep_copy_value(property_state);
        if (!mapp(tax_candidate))
            return ([ "ok": 0, "released": 0 ]);
        tax_candidate["status"] = "overdue";
        if (!intp(tax_candidate["overdue_since"])
            || tax_candidate["overdue_since"] <= 0)
            tax_candidate["overdue_since"] = tax_candidate["tax_due_at"] || now;
        if (now > tax_candidate["overdue_since"] + grace_seconds) {
            result = release_property_internal(player_id, player,
                property_id, property_state, property_def, "tax_default");
            if (!result["success"]) {
                restore_housing_state(snapshot);
                return ([ "ok": 0, "released": 0, "result": result ]);
            }
            return ([ "ok": 1, "released": 1, "result": result ]);
        }

        tax_candidate["last_tax_settled_at"] = now;
        tax_candidate["tax_due_at"] = (tax_candidate["tax_due_at"] || now)
            + tax_seconds;
        tax_candidate["overdue_since"] = 0;
        if (now <= (tax_candidate["rent_expires_at"] || 0))
            tax_candidate["status"] = "active";

        prepared = prepare_rent_operation("tax", player_id, property_id,
            property_def, tax_candidate);
        if (!mapp(prepared) || !prepared["success"])
            return ([ "ok": 0, "released": 0, "result": prepared ]);
        applied = apply_rent_operation(player, prepared["record"]);
        if (!mapp(applied) || !applied["success"]) {
            if (mapp(applied) && applied["pending_recovery"])
                return ([ "ok": 0, "released": 0, "result": applied ]);
            property_state["status"] = "overdue";
            if (!intp(property_state["overdue_since"])
                || property_state["overdue_since"] <= 0)
                property_state["overdue_since"] = property_state["tax_due_at"] || now;
            if (!commit_housing_state(snapshot, "settle_tax_failure"))
                return ([ "ok": 0, "released": 0, "result": applied ]);
            return ([ "ok": 1, "state": copy(property_state),
                "released": 0, "result": applied ]);
        }
        property_state = query_player_property_state_by_id(player_id, property_id);
        if (!mapp(property_state))
            return ([ "ok": 0, "released": 0, "result": applied ]);
    }

    if ((property_state["status"] || "") != "overdue")
        property_state["status"] = "active";
    if (!commit_housing_state(snapshot, "settle_property_state"))
        return ([ "ok": 0, "released": 0 ]);
    return ([ "ok": 1, "state": copy(property_state), "released": 0 ]);
}

private int property_available_for_player(string player_id, string property_id)
{
    string owner_id;

    owner_id = _property_owners[property_id] || "";
    return owner_id == "" || owner_id == player_id;
}

mapping query_runtime_snapshot()
{
    return ([
        "player_count": sizeof(keys(_players || ([]))),
        "owned_property_count": sizeof(keys(_property_owners || ([]))),
        "property_count": sizeof(keys(query_property_catalog() || ([]))),
        "restore_failed": _restore_failed,
        "last_persist_result": _last_persist_result,
        "last_persist_error": _last_persist_error,
        "save_phase": mapp(_last_housing_save_phase)
            ? copy(_last_housing_save_phase) : ([ "outcome": "not_attempted" ]),
        "runtime": ECONOMY_SETTLEMENT_D->query_housing_runtime_snapshot(),
    ]);
}

mapping query_property_catalog_snapshot()
{
    return copy(query_property_catalog() || ([]));
}

mapping query_player_housing_snapshot(object player)
{
    mapping bucket;
    mapping properties;
    mapping snapshot;
    mapping property_state;
    string player_id;
    string property_id;

    player_id = ensure_player_id(player);
    if (!stringp(player_id) || player_id == "")
        return ([ "player_id": "", "properties": ([ ]) ]);

    bucket = query_player_bucket(player_id);
    properties = mapp(bucket) && mapp(bucket["properties"]) ? bucket["properties"] : ([ ]);
    snapshot = ([ ]);
    foreach (property_id in keys(properties)) {
        property_state = properties[property_id];
        if (mapp(property_state))
            snapshot[property_id] = copy(property_state);
    }

    return ([
        "player_id": player_id,
        "properties": snapshot,
    ]);
}

private mapping settle_property_for_mutation(object player, string property_id)
{
    mapping result;
    mapping property_state;
    string player_id;

    player_id = ensure_player_id(player);
    if (!stringp(player_id) || player_id == "")
        return 0;

    result = settle_property_state(player, player_id, property_id);
    if (!mapp(result) || !result["ok"] || result["released"])
        return 0;
    property_state = query_player_property_state_by_id(player_id, property_id);
    return mapp(property_state) ? property_state : 0;
}

private mapping settle_property_for_access(object actor, string property_id)
{
    mapping result;
    mapping property_state;
    object owner_player;
    string actor_id;
    string owner_id;

    actor_id = ensure_player_id(actor);
    if (!stringp(actor_id) || actor_id == "")
        return 0;

    owner_id = _property_owners[property_id] || actor_id;
    owner_player = owner_id == actor_id ? actor : find_player_online(owner_id);
    if (!objectp(owner_player))
        return 0;

    result = settle_property_state(owner_player, owner_id, property_id);
    if (!mapp(result) || !result["ok"] || result["released"])
        return 0;
    property_state = query_player_property_state_by_id(owner_id, property_id);
    return mapp(property_state) ? property_state : 0;
}

mapping query_property_status(object player, string property_id)
{
    mapping property_state;
    string player_id;

    player_id = ensure_player_id(player);
    if (!stringp(player_id) || player_id == "")
        return 0;

    property_state = peek_player_property_state_by_id(player_id, property_id);
    return mapp(property_state) ? copy(property_state) : 0;
}

string render_catalog(object player, int page)
{
    int end;
    int page_size;
    int start;
    int total_pages;
    mapping property_def;
    mixed *property_ids;
    string output;
    string player_id;

    property_ids = sort_array(keys(query_property_catalog() || ([])), 1);
    if (!arrayp(property_ids) || sizeof(property_ids) == 0)
        return "当前没有可租赁的房屋目录。\n";

    page_size = 4;
    total_pages = (sizeof(property_ids) + page_size - 1) / page_size;
    if (page <= 0)
        page = 1;
    if (page > total_pages)
        page = total_pages;
    start = (page - 1) * page_size;
    end = start + page_size - 1;
    if (end >= sizeof(property_ids))
        end = sizeof(property_ids) - 1;

    player_id = ensure_player_id(player);
    output = sprintf("房屋目录（第 %d/%d 页）：\n", page, total_pages);
    foreach (string property_id in property_ids[start..end]) {
        property_def = query_property_definition(property_id);
        output += sprintf("  - %s [%s] %s / 仓储:%d / 房租:%s / 房税:%s / %s\n",
            property_id,
            property_def["name"] || property_id,
            property_def["grade"] || "starter",
            query_storage_slot_limit(property_def),
            MONEY_D->format_money(ECONOMY_SETTLEMENT_D->query_housing_rent(property_def)),
            MONEY_D->format_money(ECONOMY_SETTLEMENT_D->query_housing_tax(property_def)),
            property_available_for_player(player_id, property_id) ? "可租" : "已被占用");
    }
    if (total_pages > 1)
        output += "用 housing list <页码> 翻页。\n";
    return output;
}

string render_status(object player)
{
    mapping properties;
    mapping property_def;
    mapping property_state;
    mapping snapshot;
    string output;
    string property_id;

    snapshot = query_player_housing_snapshot(player);
    properties = snapshot["properties"] || ([ ]);
    if (!sizeof(keys(properties)))
        return "当前没有持有中的房屋。\n";

    output = "房屋状态：\n";
    foreach (property_id in sort_array(keys(properties), 1)) {
        property_state = properties[property_id];
        property_def = query_property_definition(property_id);
        output += sprintf("  - %s [%s] 状态:%s / 到期:%s / 下次房税:%s / 仓储:%d/%d\n",
            property_id,
            property_def["name"] || property_id,
            property_state["status"] || "active",
            format_time_line(property_state["rent_expires_at"] || 0),
            format_time_line(property_state["tax_due_at"] || 0),
            sizeof(property_state["storage"] || ({ })),
            query_storage_slot_limit(property_def));
        if (sizeof(property_state["guests"] || ({ })) > 0)
            output += sprintf("    访客：%d 人\n", sizeof(property_state["guests"] || ({ })));
    }
    return output;
}

string render_storage(object player, string property_id, int page)
{
    int end;
    int index;
    int page_size;
    int start;
    int total_pages;
    mapping property_def;
    mapping property_state;
    mixed *storage;
    string output;

    property_state = query_property_status(player, property_id);
    property_def = query_property_definition(property_id);
    if (!mapp(property_state) || !mapp(property_def))
        return "当前没有该房屋的有效仓储。\n";

    storage = arrayp(property_state["storage"]) ? property_state["storage"] : ({ });
    if (!sizeof(storage))
        return sprintf("%s 当前仓储为空。\n", property_def["name"] || property_id);

    page_size = 5;
    total_pages = (sizeof(storage) + page_size - 1) / page_size;
    if (page <= 0)
        page = 1;
    if (page > total_pages)
        page = total_pages;
    start = (page - 1) * page_size;
    end = start + page_size - 1;
    if (end >= sizeof(storage))
        end = sizeof(storage) - 1;

    output = sprintf("%s 仓储（第 %d/%d 页）：\n", property_def["name"] || property_id, page, total_pages);
    for (index = start; index <= end; index++) {
        output += sprintf("  %d. %s x%d\n",
            index + 1,
            query_attachment_display_name(storage[index]),
            storage[index]["count"] || 1);
    }
    if (total_pages > 1)
        output += sprintf("用 housing storage %s <页码> 翻页。\n", property_id);
    return output;
}

private string build_rent_operation_key(string action, string player_id,
    string property_id)
{
    return sprintf("%s:%s:%s", action || "rent", player_id || "",
        property_id || "");
}

private mapping prepare_rent_operation(string action, string player_id,
    string property_id, mapping property_def, mapping candidate)
{
    mapping record;
    mapping previous_pending;
    int save_result;
    string key;

    if (!mapp(_pending_rent_operations))
        _pending_rent_operations = ([]);
    key = build_rent_operation_key(action, player_id, property_id);
    record = _pending_rent_operations[key];
    if (mapp(record)) {
        if ((record["player_id"] || "") != player_id
            || (record["property_id"] || "") != property_id
            || (record["action"] || "") != action
            || !stringp(record["operation_id"])
            || !mapp(record["candidate"]))
            return ([ "success": 0, "message": "房屋已有未完成的租赁操作，请稍后重试。\n",
                "pending_recovery": 1 ]);
        return ([ "success": 1, "key": key, "record": record ]);
    }

    previous_pending = deep_copy_value(_pending_rent_operations);
    record = ([
        "action": action,
        "player_id": player_id,
        "property_id": property_id,
        "operation_id": ECONOMY_SETTLEMENT_D->issue_money_operation_id(
            "housing", action, player_id, property_id),
        "amount": action == "tax"
            ? ECONOMY_SETTLEMENT_D->query_housing_tax(property_def)
            : ECONOMY_SETTLEMENT_D->query_housing_rent(property_def),
        "candidate": deep_copy_value(candidate),
        "created_at": time(),
    ]);
    if (!mapp(record["candidate"]))
        return ([ "success": 0, "message": "无法建立房屋租赁恢复记录。\n" ]);
    _pending_rent_operations[key] = record;
    save_result = save_housing_data();
    if (save_result == 1)
        return ([ "success": 1, "key": key, "record": record ]);
    if (!mapp(_last_housing_save_phase)
        || _last_housing_save_phase["outcome"] == "not_written")
        _pending_rent_operations = previous_pending;
    return ([ "success": 0, "message": "房屋租赁恢复记录保存失败，请稍后重试。\n",
        "pending_recovery": 1, "state": mapp(_last_housing_save_phase)
            ? _last_housing_save_phase["outcome"] : "unknown" ]);
}

private mapping apply_rent_operation(object player, mapping record)
{
    mapping property_def;
    mapping settlement_def;
    mapping result;
    mapping snapshot;
    mapping bucket;
    mapping candidate;
    int save_result;
    string key;
    string player_id;
    string property_id;
    string action;

    if (!objectp(player) || !mapp(record))
        return ([ "success": 0, "message": "房屋租赁操作无效。\n" ]);
    player_id = record["player_id"];
    property_id = record["property_id"];
    action = record["action"] || "rent";
    if (!stringp(player_id) || !stringp(property_id)
        || player_id != ensure_player_id(player))
        return ([ "success": 0, "message": "房屋租赁操作身份不匹配。\n" ]);
    property_def = query_property_definition(property_id);
    candidate = deep_copy_value(record["candidate"]);
    if (!mapp(property_def) || !mapp(candidate))
        return ([ "success": 0, "message": "房屋租赁恢复记录已损坏。\n",
            "pending_recovery": 1 ]);
    settlement_def = deep_copy_value(property_def);
    if (action == "tax") {
        settlement_def["tax"] = record["amount"];
        result = ECONOMY_SETTLEMENT_D->settle_housing_tax_with_operation(
            player, candidate, settlement_def, record["operation_id"]);
    } else {
        settlement_def["rent"] = record["amount"];
        result = ECONOMY_SETTLEMENT_D->settle_housing_rent_with_operation(
            player, settlement_def, record["operation_id"]);
    }
    if (!mapp(result) || !result["success"]) {
        if (!mapp(result) || !result["pending_recovery"]) {
            log_file("system/housing", sprintf(
                "%s rent operation %s was not charged: %s\n", action,
                record["operation_id"], mapp(result) ? result["message"] : "unknown"));
            /* The intent was durable before the debit attempt.  A known
             * non-pending rejection (for example insufficient cash) must
             * remove that intent, otherwise every later rent command is
             * trapped replaying a permanently failed operation. */
            key = build_rent_operation_key(action, player_id, property_id);
            map_delete(_pending_rent_operations, key);
            save_result = save_housing_data();
            if (save_result != 1)
                _pending_rent_operations[key] = record;
        }
        return result || ([ "success": 0, "message": "房租扣款失败。\n" ]);
    }

    snapshot = snapshot_housing_state();
    bucket = ensure_player_bucket(player_id);
    if (!mapp(bucket))
        return ([ "success": 0, "message": "房屋账本不可用。\n",
            "pending_recovery": 1 ]);
    bucket["properties"][property_id] = candidate;
    _property_owners[property_id] = player_id;
    save_result = save_housing_data();
    if (save_result != 1) {
        if (!mapp(_last_housing_save_phase)
            || _last_housing_save_phase["outcome"] == "not_written")
            restore_housing_state(snapshot);
        return ([ "success": 0, "message": "房屋租赁已扣款，账本正在恢复，请稍后重试。\n",
            "pending_recovery": 1, "state": mapp(_last_housing_save_phase)
                ? _last_housing_save_phase["outcome"] : "unknown",
            "operation_id": record["operation_id"] ]);
    }

    key = build_rent_operation_key(action, player_id, property_id);
    map_delete(_pending_rent_operations, key);
    save_result = save_housing_data();
    if (save_result != 1) {
        _pending_rent_operations[key] = record;
        return ([ "success": 0, "message": "房屋租赁已保存，恢复收据尚未完成，请稍后重试。\n",
            "pending_recovery": 1, "state": mapp(_last_housing_save_phase)
                ? _last_housing_save_phase["outcome"] : "unknown",
            "operation_id": record["operation_id"] ]);
    }
    return ([ "success": 1,
        "message": action == "renew"
            ? sprintf("已续租 %s。\n", property_def["name"] || property_id)
            : sprintf("你租下了 %s。\n", property_def["name"] || property_id),
        "property_id": property_id, "operation_id": record["operation_id"] ]);
}

mapping recover_pending_rent_operations(object player)
{
    mapping result;
    mapping last_result;
    mapping record;
    string player_id;
    string key;
    int recovered;

    player_id = ensure_player_id(player);
    if (!stringp(player_id) || player_id == "")
        return ([ "success": 0, "message": "无法恢复房屋租赁身份。\n" ]);
    foreach (key in keys(_pending_rent_operations || ([]))) {
        record = _pending_rent_operations[key];
        if (!mapp(record) || record["player_id"] != player_id)
            continue;
        result = apply_rent_operation(player, record);
        if (!mapp(result) || !result["success"])
            return result || ([ "success": 0, "pending_recovery": 1 ]);
        recovered += 1;
        last_result = result;
    }
    return ([ "success": 1, "recovered": recovered,
        "last_result": last_result ]);
}

mapping rent_property(object player, string property_id)
{
    int now;
    int rent_seconds;
    int tax_seconds;
    mapping property_def;
    mapping property_state;
    mapping prepared;
    mapping recovery;
    string player_id;

    recovery = recover_pending_rent_operations(player);
    if (!mapp(recovery) || !recovery["success"])
        return recovery || ([ "success": 0, "pending_recovery": 1 ]);
    if (recovery["recovered"] > 0)
        return recovery["last_result"];

    player_id = ensure_player_id(player);
    property_def = query_property_definition(property_id);
    if (!stringp(player_id) || player_id == "" || !mapp(property_def))
        return ([ "success": 0, "message": "找不到该房屋。\n" ]);
    if (!ECONOMY_SETTLEMENT_D->query_housing_enabled())
        return ([ "success": 0, "message": "房屋系统当前未启用。\n" ]);
    if (!property_available_for_player(player_id, property_id))
        return ([ "success": 0, "message": "该房屋当前已被其他玩家租用。\n" ]);
    if (mapp(query_player_property_state_by_id(player_id, property_id)))
        return ([ "success": 0, "message": "你已经持有这处房屋。\n" ]);
    if (!NUMERIC_RULE_D->player_meets_skill_cap_gate(player, property_def["unlock"]["skill_cap"] || 0))
        return ([ "success": 0, "message": "你的技能上限还不足以租用这处房屋。\n" ]);

    now = time();
    rent_seconds = query_property_rent_seconds();
    tax_seconds = query_property_tax_seconds();
    property_state = ([
        "property_id": property_id,
        "owner_id": player_id,
        "status": "active",
        "rented_at": now,
        "rent_expires_at": now + rent_seconds,
        "tax_due_at": now + tax_seconds,
        "last_tax_settled_at": now,
        "overdue_since": 0,
        "storage": ({ }),
        "guests": ({ }),
    ]);
    prepared = prepare_rent_operation("rent", player_id, property_id,
        property_def, property_state);
    if (!mapp(prepared) || !prepared["success"])
        return prepared || ([ "success": 0, "pending_recovery": 1 ]);
    return apply_rent_operation(player, prepared["record"]);
}

mapping renew_property(object player, string property_id)
{
    int rent_seconds;
    mapping property_def;
    mapping property_state;
    mapping candidate;
    mapping prepared;
    mapping recovery;
    string player_id;

    recovery = recover_pending_rent_operations(player);
    if (!mapp(recovery) || !recovery["success"])
        return recovery || ([ "success": 0, "pending_recovery": 1 ]);
    if (recovery["recovered"] > 0)
        return recovery["last_result"];

    player_id = ensure_player_id(player);
    property_state = settle_property_for_mutation(player, property_id);
    property_def = query_property_definition(property_id);
    if (!stringp(player_id) || player_id == "" || !mapp(property_state) || !mapp(property_def))
        return ([ "success": 0, "message": "当前没有这处房屋的有效租期。\n" ]);

    candidate = deep_copy_value(property_state);
    if (!mapp(candidate))
        return ([ "success": 0, "message": "无法建立续租恢复记录。\n" ]);
    rent_seconds = query_property_rent_seconds();
    if ((candidate["rent_expires_at"] || 0) < time())
        candidate["rent_expires_at"] = time() + rent_seconds;
    else
        candidate["rent_expires_at"] += rent_seconds;
    candidate["status"] = "active";
    prepared = prepare_rent_operation("renew", player_id, property_id,
        property_def, candidate);
    if (!mapp(prepared) || !prepared["success"])
        return prepared || ([ "success": 0, "pending_recovery": 1 ]);
    return apply_rent_operation(player, prepared["record"]);
}

mapping release_property(object player, string property_id)
{
    mapping property_def;
    mapping property_state;
    string player_id;

    player_id = ensure_player_id(player);
    property_state = settle_property_for_mutation(player, property_id);
    property_def = query_property_definition(property_id);
    if (!stringp(player_id) || player_id == "" || !mapp(property_state) || !mapp(property_def))
        return ([ "success": 0, "message": "当前没有这处房屋。\n" ]);

    return release_property_internal(player_id, player, property_id, property_state, property_def, "manual_release");
}

mapping store_item(object player, string property_id, string item_name)
{
    mapping attachment;
    mapping property_def;
    mapping property_state;
    mapping snapshot;
    object item;

    property_state = settle_property_for_mutation(player, property_id);
    property_def = query_property_definition(property_id);
    if (!mapp(property_state) || !mapp(property_def))
        return ([ "success": 0, "message": "当前没有这处房屋的有效仓储。\n" ]);
    if ((property_state["status"] || "") != "active")
        return ([ "success": 0, "message": "房屋当前不在可正常使用状态。\n" ]);
    if (sizeof(property_state["storage"] || ({ })) >= query_storage_slot_limit(property_def))
        return ([ "success": 0, "message": "仓储槽位已满。\n" ]);

    item = ITEM_INVENTORY_D->query_transferable_item(player, item_name);
    if (!objectp(item))
        return ([ "success": 0, "message": "这件物品当前无法存入房屋仓储。\n" ]);
    attachment = ITEM_INVENTORY_D->build_item_attachment_from_object(item);
    if (!mapp(attachment))
        return ([ "success": 0, "message": "这件物品当前无法存入房屋仓储。\n" ]);

    snapshot = snapshot_housing_state();
    property_state["storage_seq"] = (property_state["storage_seq"] || 0) + 1;
    attachment["storage_id"] = sprintf("%s:%d", property_id,
        property_state["storage_seq"]);
    if (!ITEM_INVENTORY_D->consume_attachment_item_from_player(player, attachment))
        return ([ "success": 0, "message": "无法从背包中扣除该物品。\n" ]);

    property_state["storage"] += ({ attachment });
    if (!commit_housing_state(snapshot, "store_item")) {
        if (!ITEM_INVENTORY_D->restore_attachment_to_player(player, attachment))
            log_file("system/housing", "store item rollback failed\n");
        return ([ "success": 0, "message": "房屋账本保存失败，物品未存入仓储。\n" ]);
    }
    return ([ "success": 1, "message": sprintf("已将 %s 存入 %s。\n", query_attachment_display_name(attachment), property_def["name"] || property_id) ]);
}

mapping take_item(object player, string property_id, int index)
{
    mapping applied;
    mapping attachment;
    mapping property_state;
    mapping snapshot;
    string player_id;
    string operation_id;
    mixed *storage;

    player_id = ensure_player_id(player);
    property_state = settle_property_for_mutation(player, property_id);
    if (!stringp(player_id) || player_id == "" || !mapp(property_state))
        return ([ "success": 0, "message": "当前没有这处房屋的有效仓储。\n" ]);

    storage = arrayp(property_state["storage"]) ? property_state["storage"] : ({ });
    if (index <= 0 || index > sizeof(storage))
        return ([ "success": 0, "message": "仓储序号无效。\n" ]);

    attachment = storage[index - 1];
    snapshot = snapshot_housing_state();
    operation_id = sprintf("housing/take/%s/%s/%s", player_id, property_id,
        attachment["storage_id"] || sprintf("legacy-%d-%s", index,
            attachment["template_id"] || attachment["lookup_id"] || "item"));
    applied = REWARD_APPLY_D->apply_reward_package(player,
        ([ "attachments": ({ attachment }) ]),
        ([
            "drop_on_full": 0,
            "source": "housing_take",
            "persist": 1,
            "idempotent": 1,
            "target_key": operation_id,
        ]));
    if (!mapp(applied) || !applied["ok"])
        return ([ "success": 0, "message": "背包空间不足，暂时无法取出该物品。\n" ]);

    property_state["storage"] = storage[0..index - 2] + storage[index..];
    if (!commit_housing_state(snapshot, "take_item"))
        return ([ "success": 0, "message": "房屋账本保存失败，物品领取可重试。\n" ]);
    return ([ "success": 1, "message": sprintf("已从房屋仓储中取出 %s。\n", query_attachment_display_name(attachment)) ]);
}

mapping query_access_snapshot_by_id(string player_id, string property_id)
{
    mapping property_state;
    string owner_id;

    owner_id = _property_owners[property_id] || "";
    property_state = peek_player_property_state_by_id(owner_id, property_id);
    if (!mapp(property_state))
        return ([ "ok": 0, "error": "missing_property" ]);

    return ([
        "ok": 1,
        "property_id": property_id,
        "owner_id": property_state["owner_id"] || owner_id,
        "guests": copy(property_state["guests"] || ({ })),
        "can_access": player_can_access_property(player_id, property_state) ? 1 : 0,
    ]);
}

mapping enter_property(object player, string property_id)
{
    mapping moved;
    mapping property_def;
    mapping property_state;
    object room;
    string player_id;

    player_id = ensure_player_id(player);
    if (!stringp(player_id) || player_id == "" || !stringp(property_id) || property_id == "")
        return ([ "success": 0, "message": "无法进入该房屋。\n" ]);

    property_state = settle_property_for_access(player, property_id);
    property_def = query_property_definition(property_id);
    if (!mapp(property_state) || !mapp(property_def))
        return ([ "success": 0, "message": "当前没有这处房屋的有效室内空间。\n" ]);
    if ((property_state["status"] || "") != "active")
        return ([ "success": 0, "message": "房屋当前不在可进入状态。\n" ]);
    if (!player_can_access_property(player_id, property_state))
        return ([ "success": 0, "message": "你没有这处房屋的进入权限。\n" ]);

    room = ensure_property_room(property_id);
    moved = objectp(room) ? WORLD_TRAVEL_D->move_actor(player, room, ([
        "reason": "housing_enter",
    ])) : 0;
    if (!mapp(moved) || !moved["ok"])
        return ([ "success": 0, "message": "进入房屋失败。\n" ]);

    return ([ "success": 1, "message": sprintf("你进入了 %s。\n", property_def["name"] || property_id), "property_id": property_id ]);
}

mapping leave_property(object player)
{
    mapping moved;
    mapping property_def;
    object room;
    object target_room;
    string anchor_room;
    string property_id;

    room = environment(player);
    if (!objectp(room) || base_name(room) != query_room_template_path())
        return ([ "success": 0, "message": "你当前不在房屋室内空间中。\n" ]);

    if (!function_exists("query_housing_property_id", room))
        return ([ "success": 0, "message": "当前房间不是有效的房屋室内空间。\n" ]);
    property_id = room->query_housing_property_id();
    if (!stringp(property_id) || property_id == "")
        return ([ "success": 0, "message": "当前房间缺少房屋归属标识。\n" ]);
    property_def = query_property_definition(property_id);
    if (!mapp(property_def))
        return ([ "success": 0, "message": "当前房屋归属已失效。\n" ]);
    anchor_room = property_def["anchor_room"] || CONFIG_D->query_start_room();
    target_room = load_object(anchor_room);
    moved = objectp(target_room)
        ? WORLD_TRAVEL_D->move_actor(player, target_room, ([
            "reason": "housing_leave",
        ])) : 0;
    if (!mapp(moved) || !moved["ok"])
        return ([ "success": 0, "message": "离开房屋失败。\n" ]);

    return ([ "success": 1, "message": "你离开了房屋室内空间。\n", "property_id": property_id ]);
}

mapping set_guest_access(object owner, string property_id, string guest_id, int allow)
{
    mapping property_state;
    mapping snapshot;
    string owner_id;

    owner_id = ensure_player_id(owner);
    property_state = settle_property_for_mutation(owner, property_id);
    if (!stringp(owner_id) || owner_id == "" || !mapp(property_state))
        return ([ "success": 0, "message": "当前没有这处房屋。\n" ]);
    if (!stringp(guest_id) || guest_id == "" || guest_id == owner_id)
        return ([ "success": 0, "message": "访客账号无效。\n" ]);

    snapshot = snapshot_housing_state();
    if (allow) {
        if (member_array(guest_id, property_state["guests"] || ({ })) == -1)
            property_state["guests"] += ({ guest_id });
        if (!commit_housing_state(snapshot, "set_guest_access"))
            return ([ "success": 0, "message": "房屋账本保存失败，访客权限未变更。\n" ]);
        return ([ "success": 1, "message": sprintf("已为 %s 开通访客权限。\n", guest_id) ]);
    }

    property_state["guests"] = filter(property_state["guests"] || ({ }), (: $1 != $(guest_id) :));
    if (!commit_housing_state(snapshot, "set_guest_access"))
        return ([ "success": 0, "message": "房屋账本保存失败，访客权限未变更。\n" ]);
    return ([ "success": 1, "message": sprintf("已移除 %s 的访客权限。\n", guest_id) ]);
}

string render_guest_list(object owner, string property_id)
{
    mapping property_def;
    mapping property_state;
    string guest_id;
    string output;
    string owner_id;

    owner_id = ensure_player_id(owner);
    property_state = peek_player_property_state_by_id(owner_id, property_id);
    property_def = query_property_definition(property_id);
    if (!mapp(property_state) || !mapp(property_def))
        return "当前没有这处房屋。\n";

    output = sprintf("%s 访客名单：\n", property_def["name"] || property_id);
    if (!sizeof(property_state["guests"] || ({ })))
        return output + "  (空)\n";
    foreach (guest_id in property_state["guests"])
        output += sprintf("  - %s\n", guest_id);
    return output;
}

mapping admin_force_tax_due(object player, string property_id, int overdue)
{
    mapping property_state;
    mapping snapshot;
    mapping result;
    string player_id;

    player_id = ensure_player_id(player);
    property_state = query_player_property_state_by_id(player_id, property_id);
    if (!mapp(property_state))
        return ([ "success": 0, "message": "当前没有这处房屋。\n" ]);

    snapshot = snapshot_housing_state();
    property_state["tax_due_at"] = time() - 1;
    if (overdue)
        property_state["overdue_since"] = time() - query_property_tax_grace_seconds() - 1;
    if (!commit_housing_state(snapshot, "admin_force_tax_due"))
        return ([ "success": 0, "message": "房屋账本保存失败，未触发房税结算。\n" ]);
    result = settle_property_state(player, player_id, property_id);
    if (!mapp(result) || !result["ok"])
        return ([ "success": 0, "message": "房税结算未能持久化，请稍后重试。\n" ]);
    return ([ "success": 1, "message": overdue ? "已强制进入房税逾期结算。\n" : "已触发一次房税结算。\n" ]);
}

void on_config_reloaded(string key, string config_name)
{
    if (key != "housing" && config_name != "housing")
        return;

    reconcile_property_rooms();
    if (!save_housing_data())
        log_file("system/housing", "housing config reload persistence failed\n");
}

void on_config_reload_rollback(string key, string config_name)
{
    if (key != "housing" && config_name != "housing")
        return;

    reconcile_property_rooms();
}

void create()
{
    ::create();
    restore_housing_data();
    _property_rooms = ([]);
    CONFIG_D->compat_register_config_callback("housing", "on_config_reloaded", this_object());
}
