/**
 * @file    util_owner_projection.c
 * @brief   Owner 线程可执行的冻结快照投影工具。
 *
 * @par Description:
 * 这里只接受冻结 mapping/array/string/int，并返回冻结结果。
 * 运行时事实读取必须由主线程调用方在进入这里前完成。
 */

#pragma strict_types
#pragma save_types

private string owner_projection_compact_line(string text)
{
    string *parts;
    string *words;
    string part;
    int word_count;

    if (!stringp(text) || text == "")
        return "";

    parts = explode(replace_string(text, "\t", " "), " ");
    words = allocate(sizeof(parts));
    foreach (part in parts) {
        if (stringp(part) && part != "")
            words[word_count++] = part;
    }
    return word_count > 0 ? implode(words[0..word_count - 1], " ") : "";
}

private string owner_projection_normalize_text(string text)
{
    string *lines;
    string *normalized;
    string line;
    int line_count;

    if (!stringp(text) || text == "")
        return "";

    lines = explode(replace_string(text, "\r", ""), "\n");
    normalized = allocate(sizeof(lines));
    foreach (line in lines) {
        line = owner_projection_compact_line(line);
        if (line != "")
            normalized[line_count++] = line;
    }
    return line_count > 0 ? implode(normalized[0..line_count - 1], " ") : "";
}

private string owner_projection_summary(string normalized)
{
    string sentence;
    string remainder;

    if (!stringp(normalized) || normalized == "")
        return "";
    if (sscanf(normalized, "%s。%s", sentence, remainder) == 2
        && strlen(sentence) <= 32)
        return sentence + "。";
    if (strlen(normalized) > 36)
        return normalized[0..35] + "…";
    return normalized;
}

private string owner_projection_item_type_label(string item_type,
    string consume_action)
{
    switch (item_type) {
    case "consumable":
        return consume_action == "drink" ? "饮品" : "药品";
    case "weapon":
        return "武器";
    case "armor":
        return "装备";
    case "scene":
    case "scenery":
    case "prop":
    case "object":
    case "furniture":
    case "environment":
        return "场景";
    default:
        return "物品";
    }
}

private string owner_projection_worker_state(mixed state)
{
    if (!stringp(state) || state == "")
        return "pending";
    switch (state) {
    case "pending":
    case "pending_cached":
    case "ready":
    case "ready_cached":
    case "failed":
    case "main_thread_native":
        return state;
    default:
        return "pending";
    }
}

mapping project_inventory_item_snapshot(mapping snapshot)
{
    mapping payload;
    string consume_action;
    string description;
    string item_type;
    int has_food;
    int has_liquid;
    int quantity;

    if (!mapp(snapshot))
        return 0;

    item_type = stringp(snapshot["type"]) && snapshot["type"] != ""
        ? snapshot["type"] : "misc";
    has_food = (snapshot["food_remaining"] || 0) > 0
        || (snapshot["food_supply"] || 0) > 0;
    has_liquid = (snapshot["liquid_remaining"] || 0) > 0
        || (snapshot["liquid_supply"] || 0) > 0;
    consume_action = stringp(snapshot["consume_action"])
        ? snapshot["consume_action"] : "";
    if (consume_action == "") {
        if (has_liquid)
            consume_action = "drink";
        else if (has_food)
            consume_action = "eat";
    }
    quantity = intp(snapshot["quantity"]) && snapshot["quantity"] > 0
        ? snapshot["quantity"] : 1;
    description = owner_projection_normalize_text(
        stringp(snapshot["description_raw"]) ? snapshot["description_raw"] : "");

    payload = ([
        "id": stringp(snapshot["id"]) ? snapshot["id"] : "",
        "name": stringp(snapshot["name"]) ? snapshot["name"] : "",
        "template_id": stringp(snapshot["template_id"]) ? snapshot["template_id"] : "",
        "instance_id": stringp(snapshot["instance_id"]) ? snapshot["instance_id"] : "",
        "owner_id": stringp(snapshot["owner_id"]) ? snapshot["owner_id"] : "",
        "binding_policy": stringp(snapshot["binding_policy"])
            ? snapshot["binding_policy"] : "",
        "provenance": mapp(snapshot["provenance"])
            ? snapshot["provenance"] : ([]),
        "usage_modes": arrayp(snapshot["usage_modes"])
            ? snapshot["usage_modes"] : ({ }),
        "type": item_type,
        "sub_type": stringp(snapshot["sub_type"]) ? snapshot["sub_type"] : "",
        "quality": snapshot["quality"] || 1,
        "required_skill_cap": snapshot["required_skill_cap"] || 0,
        "durability": snapshot["durability"] || 0,
        "max_durability": snapshot["max_durability"] || 0,
        "equipped": snapshot["equipped"] ? 1 : 0,
        "enhance_level": snapshot["enhance_level"] || 0,
        "description": description,
        "summary": owner_projection_summary(description),
        "quantity": quantity,
        "location": "inventory",
        "consume_action": consume_action,
        "type_label": owner_projection_item_type_label(item_type, consume_action),
        "can_use": item_type == "consumable" || has_food || has_liquid ? 1 : 0,
        "can_equip": item_type == "weapon" || item_type == "armor"
            || snapshot["supports_wield"] || snapshot["supports_wear"] ? 1 : 0,
        "can_give": !snapshot["equipped"] && !snapshot["no_give"] ? 1 : 0,
    ]);
    return payload;
}

mapping project_inventory_payload_snapshot(mapping snapshot)
{
    mixed *facts;
    mixed *items;
    mapping item;
    mapping payload;
    int item_count;

    facts = mapp(snapshot) && arrayp(snapshot["items"])
        ? snapshot["items"] : ({ });
    items = allocate(sizeof(facts));
    foreach (item in facts) {
        payload = project_inventory_item_snapshot(item);
        if (mapp(payload))
            items[item_count++] = payload;
    }

    return ([
        "items": item_count > 0 ? items[0..item_count - 1] : ({ }),
        "capacity": mapp(snapshot) && intp(snapshot["capacity"])
            ? snapshot["capacity"] : 0,
        "used_slots": mapp(snapshot) && intp(snapshot["used_slots"])
            ? snapshot["used_slots"] : 0,
        "total_count": mapp(snapshot) && intp(snapshot["total_count"])
            ? snapshot["total_count"] : 0,
        "read_side_worker": mapp(snapshot) && intp(snapshot["read_side_worker"])
            ? snapshot["read_side_worker"] : 1,
        "read_side_worker_state": owner_projection_worker_state(
            mapp(snapshot) ? snapshot["read_side_worker_state"] : 0),
    ]);
}

string hp_dialog_signature_from_snapshot(mapping snapshot)
{
    if (!mapp(snapshot))
        snapshot = ([]);
    return sprintf("%d|%d|%d|%d",
        snapshot["hp"] || 0,
        snapshot["max_hp"] || 0,
        snapshot["mp"] || 0,
        snapshot["max_mp"] || 0);
}

string score_dialog_signature_from_snapshot(mapping snapshot)
{
    mapping analysis;

    if (!mapp(snapshot))
        snapshot = ([]);
    analysis = mapp(snapshot["actor_analysis"])
        ? snapshot["actor_analysis"] : ([]);
    return sprintf("%s|%s|%d|%d|%d|%d|%d|%d|%d|%s|%d|%d|%d|%d|%d|%s|%d",
        snapshot["name"] || "", snapshot["title"] || "",
        snapshot["hp"] || 0, snapshot["max_hp"] || 0,
        snapshot["mp"] || 0, snapshot["max_mp"] || 0,
        snapshot["combat_exp"] || 0, snapshot["progression_rank"] || 0,
        snapshot["skill_cap"] || 0, snapshot["weapon_name"] || "",
        snapshot["strength"] || 0, snapshot["dexterity"] || 0,
        snapshot["constitution"] || 0, snapshot["intelligence"] || 0,
        analysis["async_input_hash"] || 0,
        analysis["async_state"] || "", analysis["completed_at"] || 0);
}

string render_hp_dialog_snapshot(mapping snapshot)
{
    if (!mapp(snapshot))
        snapshot = ([]);
    return sprintf("【气血】 %d/%d\n【内力】 %d/%d\n【多核】 %s",
        snapshot["hp"] || 0,
        snapshot["max_hp"] || 0,
        snapshot["mp"] || 0,
        snapshot["max_mp"] || 0,
        owner_projection_worker_state(snapshot["read_side_worker_state"]));
}

string render_score_dialog_snapshot(mapping snapshot)
{
    mapping analysis;
    string analysis_text;

    if (!mapp(snapshot))
        snapshot = ([]);
    analysis = mapp(snapshot["actor_analysis"])
        ? snapshot["actor_analysis"] : ([]);
    if (analysis["ok"]) {
        analysis_text = sprintf("【多核评估】 生存 %d%%  资源 %d%%  综合 %d%%  状态 %s",
            (analysis["hp_pct_bp"] || 0) / 100,
            (analysis["resource_score"] || 0) / 100,
            (analysis["total_score"] || 0) / 100,
            analysis["state"] || "unknown");
    } else {
        analysis_text = "【多核评估】 后台状态评估: "
            + (analysis["async_state"] == "pending" ? "计算中" : "暂不可用");
    }
    return sprintf("【战斗状态】 气血 %d/%d  内力 %d/%d\n"
        "【先天属性】 臂力 %d  身法 %d  根骨 %d  悟性 %d\n"
        "【成长概览】 阅历 %d  档位 %d  上限 %d\n%s\n"
        "【装备状态】 武器 %s",
        snapshot["hp"] || 0, snapshot["max_hp"] || 0,
        snapshot["mp"] || 0, snapshot["max_mp"] || 0,
        snapshot["strength"] || 10, snapshot["dexterity"] || 10,
        snapshot["constitution"] || 10, snapshot["intelligence"] || 10,
        snapshot["combat_exp"] || 0, snapshot["progression_rank"] || 0,
        snapshot["skill_cap"] || 0, analysis_text,
        snapshot["weapon_name"] || "空手");
}

private string *owner_projection_append_unique_strings(string *base,
    string *values)
{
    string value;

    if (!arrayp(base))
        base = ({ });
    if (!arrayp(values))
        return base;
    foreach (value in values) {
        if (stringp(value) && value != "" && member_array(value, base) == -1)
            base += ({ value });
    }
    return base;
}

private string *owner_projection_provider_quest_ids(mapping quest_ids_by_provider,
    string provider_id)
{
    string *quest_ids;

    if (!mapp(quest_ids_by_provider) || !stringp(provider_id)
        || provider_id == "")
        return ({ });
    quest_ids = quest_ids_by_provider[provider_id];
    return arrayp(quest_ids) ? quest_ids + ({ }) : ({ });
}

private string *owner_projection_room_quest_ids(string room_path,
    mapping room_provider_ids, mapping quest_ids_by_provider)
{
    string *provider_ids;
    string *quest_ids;
    string provider_id;

    if (!stringp(room_path) || room_path == "" || !mapp(room_provider_ids))
        return ({ });
    provider_ids = room_provider_ids[room_path];
    if (!arrayp(provider_ids))
        return ({ });
    quest_ids = ({ });
    foreach (provider_id in provider_ids) {
        quest_ids = owner_projection_append_unique_strings(quest_ids,
            owner_projection_provider_quest_ids(quest_ids_by_provider,
                provider_id));
    }
    return quest_ids;
}

private void owner_projection_merge_map_marker(mapping marker_details,
    string key, string entry_room_id, string target_room_id,
    string entry_direction, string *quest_ids, string label)
{
    mapping detail;
    string *existing_quest_ids;

    if (!mapp(marker_details) || !stringp(key) || key == ""
        || !arrayp(quest_ids) || sizeof(quest_ids) == 0)
        return;
    detail = marker_details[key];
    if (!mapp(detail)) {
        detail = ([
            "coord": key,
            "entry_room_id": stringp(entry_room_id) ? entry_room_id : "",
            "target_room_id": stringp(target_room_id) ? target_room_id : "",
            "entry_direction": stringp(entry_direction) ? entry_direction : "",
            "entry_command": stringp(entry_direction) && entry_direction != ""
                ? sprintf("go %s", entry_direction) : "",
            "label": stringp(label) && label != "" ? label : "任务地点",
            "quest_ids": ({ }),
        ]);
    }
    existing_quest_ids = arrayp(detail["quest_ids"])
        ? detail["quest_ids"] : ({ });
    detail["quest_ids"] = owner_projection_append_unique_strings(
        existing_quest_ids, quest_ids);
    marker_details[key] = detail;
}

private void owner_projection_record_map_marker(mapping markers,
    mapping marker_details, string key, string entry_room_id,
    string target_room_id, string entry_direction, string *quest_ids,
    string label)
{
    if (!mapp(markers) || !arrayp(quest_ids) || sizeof(quest_ids) == 0
        || !stringp(key) || key == "")
        return;
    markers[key] = 1;
    owner_projection_merge_map_marker(marker_details, key, entry_room_id,
        target_room_id, entry_direction, quest_ids, label);
}

private mapping owner_projection_indoor_marker(mapping candidate,
    mapping quest_ids_by_provider)
{
    mixed *room_candidates;
    mapping room_candidate;
    string *provider_ids;
    string *room_quest_ids;
    string *quest_ids;
    string provider_id;
    string target_room_id;

    if (!mapp(candidate) || !mapp(quest_ids_by_provider))
        return ([ "target_room_id": "", "quest_ids": ({ }) ]);
    room_candidates = arrayp(candidate["room_candidates"])
        ? candidate["room_candidates"] : ({ });
    quest_ids = ({ });
    target_room_id = "";
    foreach (room_candidate in room_candidates) {
        if (!mapp(room_candidate))
            continue;
        provider_ids = arrayp(room_candidate["provider_ids"])
            ? room_candidate["provider_ids"] : ({ });
        room_quest_ids = ({ });
        foreach (provider_id in provider_ids) {
            room_quest_ids = owner_projection_append_unique_strings(
                room_quest_ids, owner_projection_provider_quest_ids(
                    quest_ids_by_provider, provider_id));
        }
        if (sizeof(room_quest_ids) == 0)
            continue;
        if (target_room_id == "" && stringp(room_candidate["room_id"]))
            target_room_id = room_candidate["room_id"];
        quest_ids = owner_projection_append_unique_strings(quest_ids,
            room_quest_ids);
    }
    return ([
        "target_room_id": target_room_id == ""
            ? candidate["target_room_id"] : target_room_id,
        "quest_ids": quest_ids,
    ]);
}

private mixed *owner_projection_map_marker_details(mapping marker_details)
{
    string *detail_keys;
    mixed *details;
    int index;

    if (!mapp(marker_details) || sizeof(marker_details) == 0)
        return ({ });
    detail_keys = keys(marker_details);
    details = allocate(sizeof(detail_keys));
    for (index = 0; index < sizeof(detail_keys); index++)
        details[index] = marker_details[detail_keys[index]];
    return details;
}

mapping project_map_grid_payload_snapshot(mapping snapshot)
{
    mixed *indoor_entry_candidates;
    mapping candidate;
    mapping indoor_marker;
    mapping map_marker_provider_ids;
    mapping marker_details;
    mapping markers;
    mapping payload;
    mapping quest_ids_by_provider;
    mapping room_provider_ids;
    mapping rooms_by_cell;
    string *provider_ids;
    string *quest_ids;
    string key;
    string provider_id;
    string room_path;

    if (!mapp(snapshot))
        return 0;
    rooms_by_cell = mapp(snapshot["rooms"]) ? snapshot["rooms"] : ([ ]);
    room_provider_ids = mapp(snapshot["room_provider_ids"])
        ? snapshot["room_provider_ids"] : ([ ]);
    map_marker_provider_ids = mapp(snapshot["map_marker_provider_ids"])
        ? snapshot["map_marker_provider_ids"] : ([ ]);
    indoor_entry_candidates = arrayp(snapshot["indoor_entry_candidates"])
        ? snapshot["indoor_entry_candidates"] : ({ });
    quest_ids_by_provider = mapp(snapshot["quest_ids_by_provider"])
        ? snapshot["quest_ids_by_provider"] : ([ ]);
    markers = ([ ]);
    marker_details = ([ ]);

    foreach (key, room_path in rooms_by_cell) {
        quest_ids = owner_projection_room_quest_ids(room_path,
            room_provider_ids, quest_ids_by_provider);
        owner_projection_record_map_marker(markers, marker_details, key,
            room_path, room_path, "", quest_ids, "任务地点");
    }
    foreach (candidate in indoor_entry_candidates) {
        indoor_marker = owner_projection_indoor_marker(candidate,
            quest_ids_by_provider);
        quest_ids = arrayp(indoor_marker["quest_ids"])
            ? indoor_marker["quest_ids"] : ({ });
        owner_projection_record_map_marker(markers, marker_details,
            candidate["cell"], candidate["entry_room_id"],
            stringp(indoor_marker["target_room_id"])
                ? indoor_marker["target_room_id"] : candidate["target_room_id"],
            candidate["entry_direction"], quest_ids, "任务入口");
    }
    foreach (key, room_path in rooms_by_cell) {
        provider_ids = map_marker_provider_ids[room_path];
        if (!arrayp(provider_ids))
            continue;
        foreach (provider_id in provider_ids) {
            quest_ids = owner_projection_provider_quest_ids(
                quest_ids_by_provider, provider_id);
            owner_projection_record_map_marker(markers, marker_details, key,
                room_path, room_path, "", quest_ids, "任务发布点");
        }
    }

    payload = ([
        "rows": arrayp(snapshot["rows"]) ? snapshot["rows"] + ({ }) : ({ }),
        "paths": mapp(snapshot["paths"]) ? copy(snapshot["paths"]) : ([ ]),
        "rooms": copy(rooms_by_cell),
        "quest_markers": keys(markers),
        "quest_marker_details": owner_projection_map_marker_details(marker_details),
        "width": snapshot["width"],
        "height": snapshot["height"],
        "read_side_worker": snapshot["read_side_worker"] ? 1 : 0,
        "read_side_worker_state": owner_projection_worker_state(
            snapshot["read_side_worker_state"]),
    ]);
    if (stringp(snapshot["region_id"]) && snapshot["region_id"] != "")
        payload["region_id"] = snapshot["region_id"];
    if (stringp(snapshot["map_context_id"]) && snapshot["map_context_id"] != "")
        payload["map_context_id"] = snapshot["map_context_id"];
    if (stringp(snapshot["map_layer"]) && snapshot["map_layer"] != "")
        payload["map_layer"] = snapshot["map_layer"];
    if (stringp(snapshot["submap_id"]) && snapshot["submap_id"] != "")
        payload["submap_id"] = snapshot["submap_id"];
    if (stringp(snapshot["title"]) && snapshot["title"] != "")
        payload["title"] = snapshot["title"];
    if (stringp(snapshot["zone"]) && snapshot["zone"] != "")
        payload["zone"] = snapshot["zone"];
    if (arrayp(snapshot["legend"]) && sizeof(snapshot["legend"]) > 0)
        payload["legend"] = snapshot["legend"] + ({ });
    return payload;
}
