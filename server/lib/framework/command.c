/**
 * @file    command.c
 * @brief   命令基类



 *
 * @par Description:
 * 所有玩家命令的基类，提供命令执行的基础框架和通用功能。
 * 所有具体命令都应该继承此类。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

/**
 * @brief 命令创建时的初始化
 */
void create()
{
}

/**
 * @brief 命令执行前的检查
 * @param me 执行命令的玩家
 * @param arg 命令参数
 * @return 1=允许执行, 0=拒绝执行
 */
int can_execute(object me, string arg)
{
    if (!me)
        return 0;

    return 1;
}

/**
 * @brief 获取命令名称
 * @return 命令名称
 */
string query_command_name()
{
    string fname;
    string *parts;

    fname = file_name(this_object());
    parts = explode(fname, "/");
    return parts[sizeof(parts) - 1];
}

/**
 * @brief 检查玩家是否可以执行此命令
 * @param me 玩家对象
 * @return 1=可以执行, 0=不可以
 */
int valid_execute(object me)
{
    // 检查玩家是否被禁止
    if (me->query_temp("command_blocked"))
        return 0;

    // 检查玩家是否处于特殊状态
    if (me->query_temp("no_command"))
        return 0;

    return 1;
}

int is_admin_operator(object me)
{
    return objectp(me) && CONFIG_D->is_admin_player(me);
}

int is_xk_client(object me)
{
    return objectp(me)
        && stringp(me->query_temp("xk_protocol_version"))
        && me->query_temp("xk_protocol_version") != "";
}

private mapping resolve_xk_response_context(object ob, mapping response_context)
{
    /* Command replies bind only the command actor.  A command may notify
     * another player, but that notification must not inherit the recipient's
     * latest request context. */
    if (undefinedp(response_context))
        return ob == this_player()
            ? CLIENT_SYNC_D->capture_command_response_context(ob) : 0;
    return mapp(response_context) ? copy(response_context) : 0;
}

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
    /*
     * command 基类保留这一层 raw wrapper，只为让命令文件不必自己直连 simul_efun。
     * 更通用的对象/daemon 场景应分别走 /lib/base/object 与 /lib/framework/daemon。
     */
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
        if (undefinedp(response_context)) {
            if (ob == this_player())
                response_context = CLIENT_SYNC_D->capture_command_response_context(ob);
            else
                response_context = 0;
        }
        if (!undefinedp(response_context))
            options["response_context"] = mapp(response_context)
                ? copy(response_context) : response_context;
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
        /* command producer 在入口冻结上下文；显式 0 表示无关联，不能重绑。 */
        if (undefinedp(options["response_context"])) {
            if (ob == this_player())
                options["response_context"] = CLIENT_SYNC_D->capture_command_response_context(ob);
            else
                options["response_context"] = 0;
        }
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
    if (!undefinedp(response_context))
        options["response_context"] = mapp(response_context)
            ? copy(response_context) : response_context;
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
    if (!undefinedp(response_context))
        options["response_context"] = mapp(response_context)
            ? copy(response_context) : response_context;
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
    if (undefinedp(response_context))
        response_context = ob == this_player()
            ? CLIENT_SYNC_D->capture_command_response_context(ob) : 0;
    if (!undefinedp(response_context))
        options["response_context"] = mapp(response_context)
            ? copy(response_context) : response_context;
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
    if (!undefinedp(response_context))
        options["response_context"] = mapp(response_context)
            ? copy(response_context) : response_context;
    return msg_event(ob, msg, options);
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

varargs int xk_send_report(object ob, string command, mapping data,
    mapping response_context)
{
    mapping tx;

    if (!objectp(ob))
        return 0;

    response_context = resolve_xk_response_context(ob, response_context);
    tx = CLIENT_SYNC_D->tx_add_report(0, command,
        mapp(data) ? data : ([]));
    if (!undefinedp(response_context))
        tx = CLIENT_SYNC_D->tx_with_response_context(tx, response_context);
    CLIENT_SYNC_D->submit_transaction(ob, tx);
    return 1;
}

varargs int xk_send_error(object ob, string error, int code,
    mapping response_context)
{
    mapping tx;

    if (!objectp(ob))
        return 0;

    response_context = resolve_xk_response_context(ob, response_context);
    tx = CLIENT_SYNC_D->tx_add_error(0, error, code);
    if (!undefinedp(response_context))
        tx = CLIENT_SYNC_D->tx_with_response_context(tx, response_context);
    CLIENT_SYNC_D->submit_transaction(ob, tx);
    return 1;
}

varargs int xk_send_command_error(object ob, string error, int code,
    string command, string reason, string request, mapping response_context)
{
    if (!objectp(ob))
        return 0;

    response_context = resolve_xk_response_context(ob, response_context);
    return call_other("/kernel/framework/xk_protocol_core", "xk_send_command_error",
        ob, error, code, command, reason, request,
        response_context);
}

int xk_emit_message_event(object ob, string text, mapping options)
{
    if (!objectp(ob))
        return 0;

    options = mapp(options) ? copy(options) : ([]);
    return MESSAGE_D->emit_message_event(ob, text, options);
}

varargs int xk_send_dialog(object ob, string title, string content,
    mapping extra, mapping response_context)
{
    mapping tx;

    if (!objectp(ob))
        return 0;

    response_context = resolve_xk_response_context(ob, response_context);
    tx = CLIENT_SYNC_D->tx_add_dialog(0, title, content,
        mapp(extra) ? extra : ([]));
    if (mapp(response_context))
        tx = CLIENT_SYNC_D->tx_with_response_context(tx, response_context);
    CLIENT_SYNC_D->submit_transaction(ob, tx);
    return 1;
}

varargs int xk_send_batch(object ob, mixed messages,
    mapping response_context)
{
    if (!objectp(ob))
        return 0;
    response_context = resolve_xk_response_context(ob, response_context);
    return call_other("/kernel/framework/xk_protocol_core", "xk_send_batch",
        ob, messages, response_context);
}
