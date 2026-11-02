/**
 * @file    xk_protocol/meta.c
 * @brief   XK 协议核心 - 协议默认元数据派生与对话框分组
 *
 * @par Description:
 * 含 {attach_protocol_meta, attach_protocol_meta_with_batch_epoch, attach_batch_protocol_meta, attach_default_chat_batch_protocol_meta, uses_default_chat_batch_meta, query_default_stream, query_payload_text, query_default_reliability, query_default_priority, query_default_collapse_key, query_default_ttl_ms, query_dialog_action_group_id, query_dialog_action_group_title, normalize_dialog_action_group, normalize_dialog_actions, normalize_dialog_action_groups, normalize_dialog_action_groups_from_actions}。
 *
 * 本模块是 meta 域协议片段，由 `xk_protocol_core.c` 继承；
 * 只有本簇声明的 nosave 状态归属本簇，跨簇调用走 public 原型。
 */

#pragma strict_types
#pragma save_types

#include <system/globals.h>
#include <system/constants.h>
#include <system/errors.h>
#include <system/services.h>
#include <system/xk_protocol.h>
#include <framework/message_facade.h>

/* 跨簇调用原型 */
int query_next_server_seq();
int reserve_batch_server_seq_block(int child_count);
int query_target_stream_epoch(object ob, string stream);
int should_auto_bump_stream(string stream);
int xk_bump_stream_epoch(object ob, string stream);
/* 本模块前向原型 */
private mixed *attach_batch_protocol_meta(object ob, mixed raw_messages, mapping reserved_meta);

private string query_default_stream(string type)
{
    switch (type) {
    case XK_TITL:
    case XK_EXIT:
    case XK_NPCS:
    case XK_ITEM:
    case XK_MAPS:
        return "room";
    case XK_STAT:
    case XK_HPBR:
    case XK_MPBR:
        return "status";
    case XK_IVTY:
        return "inventory";
    case XK_DLOG:
    case XK_ACT1:
    case XK_ACT2:
    case XK_BTNS:
        return "dialog";
    case XK_CBST:
    case XK_CBDM:
    case XK_CBED:
    case XK_CBRD:
    case XK_CBSK:
    case XK_CBRT:
    case XK_CBEN:
        return "combat";
    case XK_REAL:
        return "projection";
    case XK_QUEST:
    case XK_QUEST_UPDATE:
    case XK_ROUT:
        return "objectives";
    case XK_CHAT:
    case XK_TEXT:
    case XK_MSGE:
        return "message";
    default:
        return "system";
    }
}
private string query_payload_text(mapping payload, string key)
{
    mixed value;

    if (!mapp(payload))
        return "";

    value = payload[key];
    return stringp(value) ? value : "";
}
private string query_default_reliability(string type, mapping payload)
{
    string reliability;

    reliability = query_payload_text(payload, "reliability");
    if (reliability != "")
        return reliability;

    switch (type) {
    case XK_LOGN:
    case XK_REGI:
    case XK_ERRO:
    case XK_TRADE:
        return "critical";
    case XK_TITL:
    case XK_EXIT:
    case XK_NPCS:
    case XK_ITEM:
    case XK_STAT:
    case XK_HPBR:
    case XK_MPBR:
    case XK_IVTY:
    case XK_ACT1:
    case XK_ACT2:
    case XK_BTNS:
    case XK_MAPS:
    case XK_ROUT:
    case XK_QUEST:
    case XK_QUEST_UPDATE:
    case XK_CBRT:
    case XK_CBEN:
        return "state";
    case XK_PING:
    case XK_PONG:
        return "ephemeral";
    default:
        return "important";
    }
}
private string query_default_priority(string reliability, mapping payload)
{
    string priority;

    priority = query_payload_text(payload, "priority");
    if (priority != "")
        return priority;

    switch (reliability) {
    case "critical":
        return "critical";
    case "ephemeral":
        return "low";
    default:
        return "normal";
    }
}
private string query_default_collapse_key(string type, string stream, string reliability, mapping payload)
{
    string collapse_key;

    collapse_key = query_payload_text(payload, "collapse_key");
    if (collapse_key != "")
        return collapse_key;
    if (reliability != "state")
        return "";

    return stream + "/" + type;
}
private int query_default_ttl_ms(string reliability, mapping payload)
{
    mixed ttl_ms;

    if (!mapp(payload))
        return reliability == "ephemeral" ? 3000 : 0;

    ttl_ms = payload["ttl_ms"];
    if (intp(ttl_ms) && ttl_ms > 0)
        return ttl_ms;

    return reliability == "ephemeral" ? 3000 : 0;
}
varargs private mapping attach_protocol_meta_with_batch_epoch(object ob,
    string type, mapping raw_payload, mixed batch_epoch_by_stream,
    mapping reserved_meta)
{
    mapping payload;
    mapping meta;
    string command_request_id;
    mixed *prepared_messages;
    string stream;
    string reliability;
    string priority;
    string collapse_key;
    int ttl_ms;
    int server_seq;
    int first_server_seq;

    payload = mapp(raw_payload) ? copy(raw_payload) : ([]);
    meta = mapp(payload["meta"]) ? copy(payload["meta"]) : ([]);

    stream = query_payload_text(meta, "stream");
    if (stream == "")
        stream = query_default_stream(type);
    reliability = query_payload_text(meta, "reliability");
    if (reliability == "")
        reliability = query_default_reliability(type, payload);
    priority = query_payload_text(meta, "priority");
    if (priority == "")
        priority = query_default_priority(reliability, payload);
    collapse_key = query_payload_text(meta, "collapse_key");
    if (collapse_key == "")
        collapse_key = query_default_collapse_key(type, stream, reliability, payload);
    ttl_ms = !undefinedp(meta["ttl_ms"]) && intp(meta["ttl_ms"])
        ? meta["ttl_ms"] : query_default_ttl_ms(reliability, payload);

    if (type == XK_BACH && arrayp(payload["messages"])) {
        /* 批包在编码前一次性预留“外层 + 全部子帧”的连续段；失败整批拒绝。 */
        if (!mapp(reserved_meta)) {
            first_server_seq = reserve_batch_server_seq_block(sizeof(payload["messages"]));
            if (first_server_seq <= 0) {
                debug_message(sprintf(
                    "[xk_protocol_core] sequence_unassigned type=%s ob=%s",
                    stringp(type) ? type : "(null)",
                    objectp(ob) ? file_name(ob) : "(null)"));
                GATEWAY_SESSION_D->record_owner_protocol_output("reserve_failed");
                return 0;
            }
            reserved_meta = ([ "server_seq": first_server_seq ]);
        }
        server_seq = intp(reserved_meta["server_seq"])
            ? reserved_meta["server_seq"] : 0;
        if (server_seq > 0)
            reserved_meta["server_seq"] = server_seq + 1;   /* 顺序消费预留段 */
    } else if (mapp(reserved_meta)) {
        server_seq = intp(reserved_meta["server_seq"])
            ? reserved_meta["server_seq"] : 0;
        if (server_seq > 0)
            reserved_meta["server_seq"] = server_seq + 1;   /* 顺序消费预留段 */
    } else {
        server_seq = query_next_server_seq();
    }
    if (server_seq <= 0) {
        /* durable 序号不可得：不得编码零序号帧。 */
        debug_message(sprintf("[xk_protocol_core] sequence_unassigned type=%s ob=%s",
            stringp(type) ? type : "(null)",
            objectp(ob) ? file_name(ob) : "(null)"));
        GATEWAY_SESSION_D->record_owner_protocol_output("reserve_failed");
        return 0;
    }
    meta["server_seq"] = server_seq;
    meta["stream"] = stream;
    if (undefinedp(meta["epoch"]) || !intp(meta["epoch"])) {
        if (mapp(reserved_meta) && intp(reserved_meta["epoch"]))
            meta["epoch"] = reserved_meta["epoch"];
        else if (mapp(batch_epoch_by_stream)) {
            if (undefinedp(batch_epoch_by_stream[stream])
                || !intp(batch_epoch_by_stream[stream]))
                batch_epoch_by_stream[stream] = should_auto_bump_stream(stream)
                    ? xk_bump_stream_epoch(ob, stream)
                    : query_target_stream_epoch(ob, stream);
            meta["epoch"] = batch_epoch_by_stream[stream];
        } else if (should_auto_bump_stream(stream))
            meta["epoch"] = xk_bump_stream_epoch(ob, stream);
    }
    meta["epoch"] = !undefinedp(meta["epoch"]) && intp(meta["epoch"])
        ? meta["epoch"] : query_target_stream_epoch(ob, stream);
    meta["reliability"] = reliability;
    meta["priority"] = priority;
    meta["sent_at"] = mapp(reserved_meta)
        && intp(reserved_meta["sent_at"])
        && reserved_meta["sent_at"] > 0
            ? reserved_meta["sent_at"] : time();
    if (collapse_key != "")
        meta["collapse_key"] = collapse_key;
    if (ttl_ms > 0)
        meta["ttl_ms"] = ttl_ms;

    if (stringp(payload["command_request_id"]))
        command_request_id = payload["command_request_id"];
    else
        command_request_id = "";
    map_delete(payload, "suppress_command_request_id");
    if (command_request_id != "") {
        payload["command_request_id"] = command_request_id;
        meta["command_request_id"] = command_request_id;
    } else
        map_delete(payload, "command_request_id");
    payload["meta"] = meta;

    if (type == XK_BACH && arrayp(payload["messages"])) {
        prepared_messages = attach_batch_protocol_meta(ob, payload["messages"],
            reserved_meta);
        /* 任一子帧拿不到正数序号时整批拒绝：零序号帧不得进入编码。 */
        if (!arrayp(prepared_messages)) {
            GATEWAY_SESSION_D->record_owner_protocol_output("reserve_failed");
            return 0;
        }
        payload["messages"] = prepared_messages;
    }

    return payload;
}
int uses_default_chat_batch_meta(mapping payload)
{
    if (!mapp(payload))
        return 0;
    if (mapp(payload["meta"]) && sizeof(payload["meta"]) > 0)
        return 0;
    if (stringp(payload["reliability"]) && payload["reliability"] != "")
        return 0;
    if (stringp(payload["priority"]) && payload["priority"] != "")
        return 0;
    if (stringp(payload["collapse_key"]) && payload["collapse_key"] != "")
        return 0;
    if (intp(payload["ttl_ms"]) && payload["ttl_ms"] > 0)
        return 0;
    return 1;
}
private mapping attach_default_chat_batch_protocol_meta(object ob,
    mapping raw_payload, mapping batch_epoch_by_stream, mapping reserved_meta)
{
    mapping payload;
    int server_seq;

    payload = copy(raw_payload);
    if (mapp(reserved_meta)) {
        server_seq = intp(reserved_meta["server_seq"])
            ? reserved_meta["server_seq"] : 0;
        if (server_seq > 0)
            reserved_meta["server_seq"] = server_seq + 1;   /* 顺序消费预留段 */
    } else {
        server_seq = query_next_server_seq();
    }
    if (server_seq <= 0) {
        debug_message(sprintf("[xk_protocol_core] sequence_unassigned type=%s ob=%s",
            XK_CHAT, objectp(ob) ? file_name(ob) : "(null)"));
        return 0;
    }
    if (undefinedp(batch_epoch_by_stream["message"])
        || !intp(batch_epoch_by_stream["message"]))
        batch_epoch_by_stream["message"] = query_target_stream_epoch(ob, "message");
    payload["meta"] = ([
        "server_seq": server_seq,
        "stream": "message",
        "epoch": batch_epoch_by_stream["message"],
        "reliability": "important",
        "priority": "normal",
        "sent_at": time(),
    ]);
    return payload;
}
private mixed *attach_batch_protocol_meta(object ob, mixed raw_messages, mapping reserved_meta)
{
    mixed *children;
    mixed child;
    mapping child_packet;
    mapping child_payload;
    mapping prepared;
    mapping batch_epoch_by_stream;
    string child_type;
    int index;

    if (!arrayp(raw_messages) || sizeof(raw_messages) == 0)
        return ({ });

    children = allocate(sizeof(raw_messages));
    batch_epoch_by_stream = ([]);
    foreach (child in raw_messages) {
        if (!mapp(child)) {
            /* L-35：批子帧必须是帧对象；原始值不得绕过元数据附着混入批。 */
            debug_message(sprintf("[xk_protocol_core] batch_child_invalid ob=%s",
                objectp(ob) ? file_name(ob) : "(null)"));
            return 0;
        }
        child_packet = copy(child);
        child_type = stringp(child_packet["type"]) ? child_packet["type"] : "";
        if (strlen(child_type) != 4 || child_type == XK_BACH) {
            /* L-35：嵌套批没有独立预留段，非帧类型无法附着元数据；
             * 一律整批拒绝，禁止在预留段外取号或发送无元数据子帧。 */
            debug_message(sprintf(
                "[xk_protocol_core] batch_child_rejected type=%s ob=%s",
                child_type == "" ? "(none)" : child_type,
                objectp(ob) ? file_name(ob) : "(null)"));
            return 0;
        }
        child_payload = mapp(child_packet["payload"])
            ? child_packet["payload"] : ([]);
        if (child_type == XK_CHAT
            && uses_default_chat_batch_meta(child_payload)) {
            prepared = attach_default_chat_batch_protocol_meta(
                ob, child_payload, batch_epoch_by_stream, reserved_meta);
            if (!mapp(prepared))
                return 0;
            child_packet["payload"] = prepared;
        } else {
            prepared = attach_protocol_meta_with_batch_epoch(ob,
                child_type, child_payload, batch_epoch_by_stream,
                reserved_meta);
            if (!mapp(prepared))
                return 0;
            child_packet["payload"] = prepared;
        }
        children[index] = child_packet;
        index++;
    }
    return children;
}
mapping attach_protocol_meta(object ob, string type, mapping raw_payload)
{
    return attach_protocol_meta_with_batch_epoch(ob, type, raw_payload, 0);
}
private mapping *normalize_dialog_actions(mixed raw_actions)
{
    mapping *actions;
    mixed raw_action;

    actions = ({ });
    if (!arrayp(raw_actions))
        return actions;

    foreach (raw_action in raw_actions) {
        mapping action;
        string label;
        string cmd;

        if (!mapp(raw_action))
            continue;

        label = stringp(raw_action["label"]) ? raw_action["label"] : "";
        cmd = stringp(raw_action["cmd"]) ? raw_action["cmd"] : "";
        if (label == "" || cmd == "")
            continue;

        action = ([
            "label": label,
            "cmd": cmd,
        ]);
        if (stringp(raw_action["type"]) && raw_action["type"] != "")
            action["type"] = raw_action["type"];
        if (stringp(raw_action["kind"]) && raw_action["kind"] != "")
            action["kind"] = raw_action["kind"];
        if (stringp(raw_action["role"]) && raw_action["role"] != "")
            action["role"] = raw_action["role"];
        if (stringp(raw_action["surface"]) && raw_action["surface"] != "")
            action["surface"] = raw_action["surface"];
        if (stringp(raw_action["target_group"]) && raw_action["target_group"] != "")
            action["target_group"] = raw_action["target_group"];
        if (stringp(raw_action["style"]) && raw_action["style"] != "")
            action["style"] = raw_action["style"];
        if (intp(raw_action["priority"]) && raw_action["priority"] > 0)
            action["priority"] = raw_action["priority"];
        /* L-38：guided 是「需要保留的任务入口」标记，对话归一化白名单必须
         * 与房间投影路一致保留它，否则同一动作在不同路径上指引属性不一致。 */
        if (raw_action["guided"])
            action["guided"] = 1;

        actions += ({ action });
    }

    return actions;
}
private string query_dialog_action_group_id(mapping action)
{
    string surface;
    string cmd;

    if (!mapp(action))
        return "main";

    surface = stringp(action["surface"]) ? action["surface"] : "";
    if (surface != "")
        return surface;

    cmd = stringp(action["cmd"]) ? lower_case(action["cmd"]) : "";
    if (cmd == "" || cmd == "ui://interaction/root")
        return "nav";
    if (strsrch(cmd, "quest") == 0)
        return "quest";
    if (strsrch(cmd, "ask ") == 0 || strsrch(cmd, "talk ") == 0 || strsrch(cmd, "look ") == 0)
        return "conversation";
    if (strsrch(cmd, "list") == 0 || strsrch(cmd, "buy ") == 0 || strsrch(cmd, "sell ") == 0)
        return "trade";
    if (strsrch(cmd, "give ") == 0 || strsrch(cmd, "eat ") == 0 || strsrch(cmd, "drink ") == 0)
        return "item";
    if (strsrch(cmd, "fight ") == 0 || strsrch(cmd, "kill ") == 0)
        return "danger";
    return "main";
}
private string query_dialog_action_group_title(string group_id)
{
    switch (group_id) {
    case "root":
        return "主要操作";
    case "conversation":
        return "话题";   /* 话题具名化，组标题不再叫“交谈” */
    case "quest":
        return "任务";
    case "trade":
        return "交易";
    case "item":
        return "物品";
    case "danger":
        return "危险操作";
    case "nav":
        return "导航";
    default:
        return "操作";
    }
}
private mapping normalize_dialog_action_group(mapping raw_group)
{
    mapping group;
    mapping *actions;
    string group_id;

    if (!mapp(raw_group))
        return 0;

    actions = normalize_dialog_actions(raw_group["actions"]);
    if (!sizeof(actions))
        return 0;

    group_id = stringp(raw_group["id"]) && raw_group["id"] != ""
        ? raw_group["id"]
        : (stringp(raw_group["surface"]) && raw_group["surface"] != "" ? raw_group["surface"] : "main");
    group = ([
        "id": group_id,
        "title": stringp(raw_group["title"]) && raw_group["title"] != ""
            ? raw_group["title"]
            : query_dialog_action_group_title(group_id),
        "surface": stringp(raw_group["surface"]) && raw_group["surface"] != ""
            ? raw_group["surface"]
            : group_id,
        "actions": actions,
    ]);
    if (stringp(raw_group["role"]) && raw_group["role"] != "")
        group["role"] = raw_group["role"];
    return group;
}
mapping *normalize_dialog_action_groups(mixed raw_groups)
{
    mapping *groups;
    mapping group;
    mixed raw_group;

    groups = ({ });
    if (arrayp(raw_groups)) {
        foreach (raw_group in raw_groups) {
            group = normalize_dialog_action_group(raw_group);
            if (mapp(group))
                groups += ({ group });
        }
    }
    return groups;
}
mapping *normalize_dialog_action_groups_from_actions(mixed raw_actions)
{
    mapping *groups;
    mapping *actions;
    mapping action;
    mapping buckets;
    string group_id;

    actions = normalize_dialog_actions(raw_actions);
    if (!sizeof(actions))
        return ({ });

    buckets = ([]);
    foreach (action in actions) {
        group_id = query_dialog_action_group_id(action);
        if (!arrayp(buckets[group_id]))
            buckets[group_id] = ({ });
        buckets[group_id] += ({ action });
    }

    groups = ({ });
    foreach (group_id in keys(buckets)) {
        groups += ({
            ([
                "id": group_id,
                "title": query_dialog_action_group_title(group_id),
                "surface": group_id,
                "actions": buckets[group_id],
            ])
        });
    }
    return groups;
}
