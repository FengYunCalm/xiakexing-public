/**
 * @file    client_sync_plan.c
 * @brief   客户端同步计划与事务构造模块
 */

#pragma strict_types
#pragma save_types

#include <system/xk_protocol.h>

mapping normalize_sync_plan(mapping plan);
mapping normalize_sync_transaction(mapping tx);

mapping tx_add_chat_message_ex(mapping tx, string text, string channel, mapping extra);

mapping create_sync_plan()
{
    return ([
        "projection_state": 0,
        "status": 0,
        "inventory": 0,
        "objectives": 0,
        "score_refresh": 0,
        "room_snapshot": 0,
        "room": 0,
        "projection": 0,
        "response_context": 0,
    ]);
}

mapping create_sync_transaction()
{
    return ([
        "refresh": create_sync_plan(),
        "immediate": ({ }),
    ]);
}

mapping capture_command_response_context(object player)
{
    mixed response_id;

    if (!objectp(player)
        || !player->query_temp("gateway/command_context_active"))
        return 0;
    response_id = player->query_temp("gateway/command_request_id");
    if (!stringp(response_id) || response_id == "")
        return 0;
    return ([ "command_request_id": response_id ]);
}

mapping plan_with_response_context(mapping plan, mapping response_context)
{
    plan = normalize_sync_plan(plan);
    if (mapp(response_context)
        && stringp(response_context["command_request_id"])
        && response_context["command_request_id"] != "")
        plan["response_context"] = copy(response_context);
    return plan;
}

mapping tx_with_response_context(mapping tx, mapping response_context)
{
    mixed *immediate;
    int index;

    tx = normalize_sync_transaction(tx);
    if (!mapp(response_context)
        || !stringp(response_context["command_request_id"])
        || response_context["command_request_id"] == "")
        return tx;

    tx["response_context"] = copy(response_context);
    tx["refresh"]["response_context"] = copy(response_context);
    immediate = tx["immediate"];
    if (arrayp(immediate))
        for (index = 0; index < sizeof(immediate); index++)
            if (mapp(immediate[index])
                && !mapp(immediate[index]["response_context"]))
                immediate[index]["response_context"] = copy(response_context);
    return tx;
}

varargs mapping tx_with_player_response_context(object player, mapping tx,
    mapping response_context)
{
    /* response_context 必须在 producer 创建/入队时显式冻结；
     * flush/transaction 层不能从玩家当前作用域补读。 */
    return tx_with_response_context(tx, response_context);
}

mapping normalize_sync_plan(mapping plan)
{
    mapping normalized;

    normalized = create_sync_plan();
    if (!mapp(plan))
        return normalized;

    normalized["projection_state"] = plan["projection_state"] ? 1 : 0;
    normalized["status"] = plan["status"] ? 1 : 0;
    normalized["inventory"] = plan["inventory"] ? 1 : 0;
    normalized["objectives"] = plan["objectives"] ? 1 : 0;
    normalized["score_refresh"] = plan["score_refresh"] ? 1 : 0;
    normalized["room_snapshot"] = plan["room_snapshot"] ? 1 : 0;
    if (objectp(plan["room"])) {
        normalized["room"] = plan["room"];
        normalized["room_snapshot"] = 1;
    }
    if (mapp(plan["projection"]))
        normalized["projection"] = copy(plan["projection"]);
    if (mapp(plan["response_context"])
        && stringp(plan["response_context"]["command_request_id"])
        && plan["response_context"]["command_request_id"] != "")
        normalized["response_context"] = copy(plan["response_context"]);

    return normalized;
}

mapping normalize_sync_transaction(mapping tx)
{
    mapping normalized;

    normalized = create_sync_transaction();
    if (!mapp(tx))
        return normalized;

    normalized["refresh"] = normalize_sync_plan(tx["refresh"]);
    normalized["immediate"] = arrayp(tx["immediate"]) ? tx["immediate"] : ({ });
    if (mapp(tx["response_context"])
        && stringp(tx["response_context"]["command_request_id"])
        && tx["response_context"]["command_request_id"] != "")
        normalized["response_context"] = copy(tx["response_context"]);
    if (stringp(tx["reason"]) && tx["reason"] != "")
        normalized["reason"] = tx["reason"];

    return normalized;
}

mapping merge_sync_plan(mapping base, mapping extra)
{
    mapping merged;
    mapping normalized_extra;

    merged = normalize_sync_plan(base);
    normalized_extra = normalize_sync_plan(extra);

    if (mapp(normalized_extra["projection"])) {
        if (mapp(merged["projection"])
            && (merged["projection"]["projection_id"]
                    != normalized_extra["projection"]["projection_id"]
                || merged["projection"]["projection_revision"]
                    != normalized_extra["projection"]["projection_revision"]))
            merged = create_sync_plan();
        merged["projection"] = copy(normalized_extra["projection"]);
    }

    if (mapp(merged["response_context"])
        && mapp(normalized_extra["response_context"])) {
        if (merged["response_context"]["command_request_id"]
            != normalized_extra["response_context"]["command_request_id"])
            map_delete(merged, "response_context");
    } else if (mapp(merged["response_context"])
        && !mapp(normalized_extra["response_context"])) {
        /* A background refresh merged into a command transaction must not
         * inherit the command's response identity. */
        map_delete(merged, "response_context");
    } else if (mapp(normalized_extra["response_context"]))
        merged["response_context"] = copy(normalized_extra["response_context"]);

    if (normalized_extra["projection_state"])
        merged["projection_state"] = 1;
    if (normalized_extra["status"])
        merged["status"] = 1;
    if (normalized_extra["inventory"])
        merged["inventory"] = 1;
    if (normalized_extra["objectives"])
        merged["objectives"] = 1;
    if (normalized_extra["score_refresh"])
        merged["score_refresh"] = 1;
    if (normalized_extra["room_snapshot"])
        merged["room_snapshot"] = 1;
    if (objectp(normalized_extra["room"]))
        merged["room"] = normalized_extra["room"];

    return merged;
}

mapping plan_status(mapping plan)
{
    plan = normalize_sync_plan(plan);
    plan["status"] = 1;
    return plan;
}

mapping plan_projection_state(mapping plan)
{
    plan = normalize_sync_plan(plan);
    plan["projection_state"] = 1;
    return plan;
}

mapping plan_objectives(mapping plan)
{
    plan = normalize_sync_plan(plan);
    plan["objectives"] = 1;
    return plan;
}

mapping plan_projection(mapping plan, mapping projection)
{
    plan = normalize_sync_plan(plan);
    if (mapp(projection))
        plan["projection"] = copy(projection);
    return plan;
}

mapping plan_inventory(mapping plan)
{
    plan = normalize_sync_plan(plan);
    plan["inventory"] = 1;
    return plan;
}

mapping plan_score_refresh(mapping plan)
{
    plan = normalize_sync_plan(plan);
    plan["score_refresh"] = 1;
    return plan;
}

mapping plan_room_snapshot(mapping plan, object room)
{
    plan = normalize_sync_plan(plan);
    plan["room_snapshot"] = 1;
    if (objectp(room))
        plan["room"] = room;
    return plan;
}

mapping tx_plan_status(mapping tx)
{
    tx = normalize_sync_transaction(tx);
    tx["refresh"] = plan_status(tx["refresh"]);
    return tx;
}

mapping tx_plan_inventory(mapping tx)
{
    tx = normalize_sync_transaction(tx);
    tx["refresh"] = plan_inventory(tx["refresh"]);
    return tx;
}

mapping tx_plan_status_inventory(mapping tx)
{
    tx = normalize_sync_transaction(tx);
    tx["refresh"] = plan_inventory(plan_status(tx["refresh"]));
    return tx;
}

mapping tx_plan_status_inventory_score_refresh(mapping tx)
{
    tx = normalize_sync_transaction(tx);
    tx["refresh"] = plan_score_refresh(
        plan_inventory(plan_status(tx["refresh"])));
    return tx;
}

mapping tx_plan_room_snapshot(mapping tx, object room)
{
    tx = normalize_sync_transaction(tx);
    tx["refresh"] = plan_room_snapshot(tx["refresh"], room);
    return tx;
}

mapping tx_plan_status_room_snapshot(mapping tx, object room)
{
    tx = normalize_sync_transaction(tx);
    tx["refresh"] = plan_room_snapshot(plan_status(tx["refresh"]), room);
    return tx;
}

mapping tx_plan_inventory_room(mapping tx, object room)
{
    tx = normalize_sync_transaction(tx);
    tx["refresh"] = plan_room_snapshot(plan_inventory(tx["refresh"]), room);
    return tx;
}

mapping tx_plan_status_inventory_room(mapping tx, object room)
{
    tx = normalize_sync_transaction(tx);
    tx["refresh"] = plan_room_snapshot(plan_inventory(plan_status(tx["refresh"])), room);
    return tx;
}

mapping tx_plan_full_player_snapshot(mapping tx, object room)
{
    tx = normalize_sync_transaction(tx);
    tx["refresh"] = plan_objectives(plan_room_snapshot(
        plan_inventory(plan_status(plan_projection_state(tx["refresh"]))), room));
    return tx;
}

mapping tx_add_trade_result(mapping tx, string action, string item_name,
    int price, int success, mapping extra_payload)
{
    tx = normalize_sync_transaction(tx);
    tx["immediate"] += ({ ([
        "kind": "trade_result",
        "action": stringp(action) ? action : "",
        "item_name": stringp(item_name) ? item_name : "",
        "price": intp(price) ? price : 0,
        "success": success ? 1 : 0,
        "extra_payload": mapp(extra_payload) ? extra_payload : ([]),
    ]) });
    return tx;
}

mapping tx_add_shop_snapshot(mapping tx, object vendor)
{
    tx = normalize_sync_transaction(tx);
    if (!objectp(vendor))
        return tx;

    tx["immediate"] += ({ ([
        "kind": "shop_snapshot",
        "vendor": vendor,
    ]) });
    return tx;
}

mapping tx_add_dialog(mapping tx, string title, string content, mapping extra)
{
    tx = normalize_sync_transaction(tx);
    tx["immediate"] += ({ ([
        "kind": "dialog",
        "title": stringp(title) ? title : "",
        "content": stringp(content) ? content : "",
        "extra": mapp(extra) ? copy(extra) : ([]),
    ]) });
    return tx;
}

mapping tx_add_quick_cmds(mapping tx, mixed cmds, int priority)
{
    tx = normalize_sync_transaction(tx);
    if (!stringp(cmds) && !arrayp(cmds))
        return tx;

    tx["immediate"] += ({ ([
        "kind": "quick_cmds",
        "cmds": cmds,
        "priority": intp(priority) && priority > 0 ? priority : 1,
    ]) });
    return tx;
}

mapping tx_add_secondary_quick_cmds(mapping tx, mixed cmds, string title, int priority)
{
    tx = normalize_sync_transaction(tx);
    if (!stringp(cmds) && !arrayp(cmds))
        return tx;

    tx["immediate"] += ({ ([
        "kind": "quick_cmds_secondary",
        "cmds": cmds,
        "title": stringp(title) ? title : "",
        "priority": intp(priority) && priority > 0 ? priority : 1,
    ]) });
    return tx;
}

mapping tx_add_dynamic_buttons(mapping tx, mixed cmds, string title, int priority)
{
    tx = normalize_sync_transaction(tx);
    if (!stringp(cmds) && !arrayp(cmds))
        return tx;

    tx["immediate"] += ({ ([
        "kind": "dynamic_buttons",
        "cmds": cmds,
        "title": stringp(title) ? title : "",
        "priority": intp(priority) && priority > 0 ? priority : 1,
    ]) });
    return tx;
}

mapping tx_add_report(mapping tx, string command, mapping data)
{
    tx = normalize_sync_transaction(tx);
    if (!stringp(command) || command == "")
        return tx;

    tx["immediate"] += ({ ([
        "kind": "report",
        "command": command,
        "data": mapp(data) ? copy(data) : ([]),
    ]) });
    return tx;
}

mapping tx_add_error(mapping tx, string message, int code)
{
    tx = normalize_sync_transaction(tx);
    if (!stringp(message) || message == "")
        return tx;

    tx["immediate"] += ({ ([
        "kind": "error",
        "message": message,
        "code": intp(code) ? code : 0,
    ]) });
    return tx;
}

mapping tx_add_system(mapping tx, string text, string level)
{
    tx = normalize_sync_transaction(tx);
    if (!stringp(text) || text == "")
        return tx;

    tx["immediate"] += ({ ([
        "kind": "system",
        "text": text,
        "level": stringp(level) && level != "" ? level : "info",
    ]) });
    return tx;
}

mapping tx_add_skill_event(mapping tx, mapping payload)
{
    tx = normalize_sync_transaction(tx);
    if (!mapp(payload) || sizeof(payload) == 0)
        return tx;

    tx["immediate"] += ({ ([
        "kind": "skill_event",
        "payload": copy(payload),
    ]) });
    return tx;
}

mapping tx_add_combat_packet(mapping tx, string type, mapping payload)
{
    tx = normalize_sync_transaction(tx);
    if (!stringp(type) || type == "")
        return tx;

    tx["immediate"] += ({ ([
        "kind": "combat_packet",
        "type": type,
        "payload": mapp(payload) ? copy(payload) : ([]),
    ]) });
    return tx;
}

mapping tx_add_chat_message(mapping tx, string text, string channel)
{
    return tx_add_chat_message_ex(tx, text, channel, ([]));
}

mapping tx_add_chat_message_ex(mapping tx, string text, string channel, mapping extra)
{
    mapping payload;
    string normalized_channel;

    tx = normalize_sync_transaction(tx);
    if (!stringp(text) || text == "")
        return tx;

    normalized_channel = stringp(channel) && channel != "" ? channel : "main";
    payload = mapp(extra) ? copy(extra) : ([]);
    payload["content"] = text;
    payload["channel"] = normalized_channel;
    tx["immediate"] += ({ ([
        "kind": "chat_message",
        "text": text,
        "channel": normalized_channel,
        "extra": payload,
        "protocol_payload": payload,
        "protocol_batch_child": ([
            "type": XK_CHAT,
            "payload": payload,
        ]),
    ]) });
    return tx;
}

int has_refresh_work(mapping plan)
{
    if (!mapp(plan))
        return 0;

    return plan["projection_state"] || plan["status"] || plan["inventory"]
        || plan["objectives"] || plan["score_refresh"]
        || plan["room_snapshot"]
        || objectp(plan["room"]);
}
