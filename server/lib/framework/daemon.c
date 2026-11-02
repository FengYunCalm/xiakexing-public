/**
 * @file    daemon.c
 * @brief   守护进程框架基类


 *
 * @note
 * 统一对外继承入口为 /lib/framework/daemon。
 * 旧路径 /daemons/daemon_base 仅保留为 compatibility shell。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

#include "daemon_lifecycle.c"
private int message_facade_uses_xk(object ob)
{
    mixed version;

    if (!objectp(ob) || !interactive(ob))
        return 0;
    if (ob->query_temp("client/plain_text_only"))
        return 0;
    version = ob->query_temp("xk_protocol_version");
    return stringp(version) && version != "";
}
private int message_facade_has_visible_text(string msg)
{
    return stringp(msg) && trim(msg) != "";
}

void msg_send_legacy(object ob, string msg)
{
    /* daemon 基类负责 daemon/object 外的系统服务消息入口，业务层不再各自重复桥接。 */
    call_other("/kernel/simul_efun", "msg_send_raw", ob, msg);
}
varargs void msg_send(object ob, string msg, mapping response_context)
{
    mixed err;
    int result;
    mapping options;

    if (message_facade_uses_xk(ob) && message_facade_has_visible_text(msg)) {
        options = ([
            "channel": "main",
            "intent": "append",
            "priority": "normal",
            "reliability": "important",
            "display_mode": "paced",
            "payload": ([ "kind": "plain_text" ]),
        ]);
        response_context = resolve_xk_response_context(ob, response_context);
        if (mapp(response_context))
            options["response_context"] = copy(response_context);
        err = catch(result = MESSAGE_D->emit_message_event(ob, msg, options));
        /* emit 返回 0 或抛异常时继续回退 legacy 文本，保证玩家可见。 */
        if (!err && result)
            return;
    }
    msg_send_legacy(ob, msg);
}
int msg_event(object ob, string msg, mapping options)
{
    if (!objectp(ob) || !stringp(msg) || msg == "")
        return 0;
    if (message_facade_uses_xk(ob)) {
        options = mapp(options) ? copy(options) : ([]);
        /* daemon 的后台/延迟消息没有显式冻结上下文时保持无关联；
         * 不在发送时读取玩家最新命令。 */
        return MESSAGE_D->emit_message_event(ob, msg, options);
    }
    msg_send_legacy(ob, msg);
    return 1;
}
varargs int msg_error(object ob, string msg, string command, string reason,
    string request, int code, mapping response_context)
{
    mapping options;

    options = ([
        "channel": "error",
        "intent": "notify",
        "priority": "high",
        "reliability": "critical",
        "display_mode": "instant",
        "payload": ([
            "kind": "command_error",
            "command": stringp(command) ? command : "",
            "reason": stringp(reason) ? reason : "",
            "request": stringp(request) ? request : "",
            "code": intp(code) ? code : 0,
        ]),
    ]);
    options["response_context"] = resolve_xk_response_context(ob, response_context);
    return msg_event(ob, msg, options);
}

varargs int msg_system(object ob, string msg, string kind,
    mapping response_context)
{
    mapping options;

    options = ([
        "channel": "system",
        "intent": "notify",
        "priority": "normal",
        "reliability": "important",
        "display_mode": "instant",
        "payload": ([ "kind": stringp(kind) && kind != "" ? kind : "system_feedback" ]),
    ]);
    options["response_context"] = resolve_xk_response_context(ob, response_context);
    return msg_event(ob, msg, options);
}

varargs int msg_dialog(object ob, string title, string msg, string dialog_id,
    string kind, mapping response_context)
{
    mapping options;

    if (!objectp(ob) || !stringp(msg) || msg == "")
        return 0;

    if (!message_facade_uses_xk(ob)) {
        msg_send_legacy(ob, msg);
        return 1;
    }
    options = ([
        "channel": "dialog",
        "intent": "replace",
        "priority": "normal",
        "reliability": "important",
        "display_mode": "dialog",
        "collapse_key": stringp(dialog_id) && dialog_id != "" ? dialog_id : "dialog/message",
        "payload": ([
            "kind": stringp(kind) && kind != "" ? kind : "dialog_message",
            "title": stringp(title) && title != "" ? title : "消息",
            "dialog_id": stringp(dialog_id) && dialog_id != "" ? dialog_id : "message_dialog",
        ]),
    ]);
    response_context = resolve_xk_response_context(ob, response_context);
    if (mapp(response_context))
        options["response_context"] = copy(response_context);
    return MESSAGE_D->emit_message_event(ob, msg, options);
}

varargs int msg_combat(object ob, string msg, string kind,
    mapping response_context)
{
    mapping options;

    options = ([
        "channel": "combat",
        "intent": "append",
        "priority": "normal",
        "reliability": "important",
        "display_mode": "instant",
        "payload": ([ "kind": stringp(kind) && kind != "" ? kind : "combat_text" ]),
    ]);
    options["response_context"] = resolve_xk_response_context(ob, response_context);
    return msg_event(ob, msg, options);
}

/*
 * 沙箱专用故障注入门面与 owner 保存阶段逻辑下沉到 fragment（见文件内注释），
 * 本文件只保留入口；由本文件文本包含，编译与运行仍是同一翻译单元。
 */
#include "daemon_test_faults.c"
#include "daemon_save_stages.c"

int owner_save_object(string file, string owner_id, string snapshot_type, mapping payload, string idempotency_key)
{
    mapping detail;

    detail = owner_save_object_detail(file, owner_id, snapshot_type, payload, idempotency_key);
    if (!mapp(detail))
        return 0;
    if (detail["ok"] == 1)
        return 1;
    if (detail["persisted"] == 1
        && detail["outcome"] == "persisted_receipt_pending")
        return 2;
    return 0;
}

/* 阶段感知入口：需要区分“没写”“已落盘待收据”“完整提交”的调用者用它，
 * 再决定是否回滚内存副本（见 daemon_save_stages.c 的 persisted/outcome）。 */
mapping owner_save_object_phases(string file, string owner_id, string snapshot_type,
    mapping payload, string idempotency_key)
{
    return owner_save_object_detail(file, owner_id, snapshot_type, payload, idempotency_key);
}

varargs void msg_to_room(mixed room, string msg, mixed exclude)
{
    object *targets;
    object *excluded;
    object target;

    if (!room || !stringp(msg) || msg == "")
        return;
    if (stringp(room))
        room = load_object(room);
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
        if (!objectp(target) || member_array(target, excluded) != -1)
            continue;
        if (!interactive(target) && !living(target) && !function_exists("catch_tell", target))
            continue;
        msg_send(target, msg);
    }
}

varargs void msg_to_room_raw(mixed room, string msg, mixed exclude)
{
    call_other("/kernel/simul_efun", "msg_to_room_raw", room, msg, exclude);
}

void msg_broadcast(string msg)
{
    object target;

    if (!stringp(msg) || msg == "")
        return;
    foreach (target in users()) {
        if (objectp(target) && environment(target))
            msg_send(target, msg);
    }
}

varargs void send_xk_system(object me, string text, string level,
    mapping response_context)
{
    mapping tx;

    if (!objectp(me))
        return;

    tx = CLIENT_SYNC_D->tx_add_system(0, text, level);
    tx = CLIENT_SYNC_D->tx_with_player_response_context(me, tx, response_context);
    CLIENT_SYNC_D->submit_transaction(me, tx);
}

varargs void send_xk_combat_packet(object me, string type, mapping payload,
    mapping response_context)
{
    mapping tx;

    /*
     * 战斗包目前仍由 xk_protocol_core 负责最终线协议封装。
     * 守护进程侧统一经 daemon 基类转发，避免业务服务直接依赖协议核心。
     */
    if (!objectp(me) || !stringp(type))
        return;

    tx = CLIENT_SYNC_D->tx_add_combat_packet(0, type,
        mapp(payload) ? payload : ([]));
    tx = CLIENT_SYNC_D->tx_with_player_response_context(me, tx, response_context);
    CLIENT_SYNC_D->submit_transaction(me, tx);
}

varargs void send_xk_dialog(object me, string title, string content,
    mapping extra, mapping response_context)
{
    mapping tx;

    if (!objectp(me))
        return;

    tx = CLIENT_SYNC_D->tx_add_dialog(0, title, content,
        mapp(extra) ? extra : ([]));
    tx = CLIENT_SYNC_D->tx_with_player_response_context(me, tx, response_context);
    CLIENT_SYNC_D->submit_transaction(me, tx);
}

varargs void send_xk_report(object me, string command, mapping data,
    mapping response_context)
{
    mapping tx;

    if (!objectp(me))
        return;

    tx = CLIENT_SYNC_D->tx_add_report(0, command,
        mapp(data) ? data : ([]));
    tx = CLIENT_SYNC_D->tx_with_player_response_context(me, tx, response_context);
    CLIENT_SYNC_D->submit_transaction(me, tx);
}

varargs void send_xk_batch(object me, mixed messages,
    mapping response_context)
{
    if (objectp(me)) {
        response_context = resolve_xk_response_context(me, response_context);
        call_other("/kernel/simul_efun", "xk_send_batch", me, messages,
            response_context);
    }
}

varargs void send_xk_quick_cmds(object me, mixed cmds, int priority,
    mapping response_context)
{
    mapping tx;

    if (!objectp(me))
        return;

    tx = CLIENT_SYNC_D->tx_add_quick_cmds(0, cmds, priority);
    tx = CLIENT_SYNC_D->tx_with_player_response_context(me, tx, response_context);
    CLIENT_SYNC_D->submit_transaction(me, tx);
}

varargs void send_xk_secondary_cmds(object me, mixed cmds, string title,
    int priority, mapping response_context)
{
    mapping tx;

    if (!objectp(me))
        return;

    tx = CLIENT_SYNC_D->tx_add_secondary_quick_cmds(0, cmds,
        stringp(title) ? title : "", priority);
    tx = CLIENT_SYNC_D->tx_with_player_response_context(me, tx, response_context);
    CLIENT_SYNC_D->submit_transaction(me, tx);
}

varargs void send_xk_dynamic_buttons(object me, mixed cmds, string title,
    int priority, mapping response_context)
{
    mapping tx;

    if (!objectp(me))
        return;

    tx = CLIENT_SYNC_D->tx_add_dynamic_buttons(0, cmds,
        stringp(title) ? title : "", priority);
    tx = CLIENT_SYNC_D->tx_with_player_response_context(me, tx, response_context);
    CLIENT_SYNC_D->submit_transaction(me, tx);
}

varargs void send_xk_status(object me, mapping response_context)
{
    if (objectp(me)) {
        response_context = resolve_xk_response_context(me, response_context);
        CLIENT_SYNC_D->request_status(me, response_context);
    }
}

varargs void send_xk_objects(object me, object *obs,
    mapping response_context)
{
    if (objectp(me)) {
        response_context = resolve_xk_response_context(me, response_context);
        call_other("/kernel/simul_efun", "xk_send_objects", me,
            obs || ({ }), response_context);
    }
}

varargs void send_xk_inventory(object me, object *obs,
    mapping response_context)
{
    if (objectp(me)) {
        /* obs 保留兼容签名；实际同步由 client_sync_d 读取当前背包快照。 */
        response_context = resolve_xk_response_context(me, response_context);
        CLIENT_SYNC_D->request_inventory(me, response_context);
    }
}

varargs void send_xk_trade_result_packet(object me, string action,
    string item_name, int price, int success, mapping extra_payload,
    mapping response_context)
{
    if (objectp(me)) {
        response_context = resolve_xk_response_context(me, response_context);
        TRADE_D->xk_send_trade_result_packet(me, action, item_name, price,
            success, extra_payload, response_context);
    }
}

varargs void emit_xk_shop_snapshot(object me, object vendor,
    mapping response_context)
{
    if (objectp(me) && objectp(vendor)) {
        response_context = resolve_xk_response_context(me, response_context);
        SHOP_D->xk_emit_shop_snapshot(me, vendor, response_context);
    }
}
