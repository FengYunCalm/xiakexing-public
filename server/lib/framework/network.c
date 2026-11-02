/**
 * @file    network.c
 * @brief   网络命令基类



 *
 * @par Description:
 * 需要网络能力的命令基类，提供消息发送与 XK 协议包装函数。
 * 这里是命令层 convenience wrapper，不是协议真源。
 */

#pragma strict_types
#pragma save_types

#include <system/ansi.h>
#include <system/services.h>
#include <framework/message_facade.h>

#define DAEMON_FRAMEWORK "/lib/framework/daemon"

private mapping resolve_xk_response_context(object me, mapping response_context)
{
    /* network facade 只传播已在同步命令入口冻结的上下文；异步/后台默认无关联。 */
    if (undefinedp(response_context))
        return 0;
    return mapp(response_context) ? copy(response_context) : 0;
}

inherit "/lib/framework/command";

/**
 * @brief 向玩家发送消息
 * @param msg 消息内容
 */
void send_message(string msg)
{
    object me;

    if (!msg || msg == "")
        return;

    /* 默认目标固定为 this_player()，避免命令层 helper 越权向任意对象发送协议消息。 */
    me = this_player();
    if (objectp(me))
        msg_send(me, msg);
}

/**
 * @brief 发送格式化消息
 * @param fmt 格式字符串
 * @param args 参数
 */
varargs void send_formatted(string fmt, mixed args...)
{
    string msg;
    object me;

    if (!fmt || fmt == "")
        return;

    /* 只负责格式化后转交统一消息入口，不附带额外前缀或协议类型。 */
    msg = sprintf(fmt, args...);
    me = this_player();
    if (objectp(me))
        msg_send(me, msg);
}

/**
 * @brief 发送系统消息
 * @param msg 消息内容
 */
void send_system(string msg)
{
    object me;

    if (!msg || msg == "")
        return;

    me = this_player();
    if (objectp(me))
        msg_send(me, HIY "【系统】" NOR + msg + "\n");
}

/**
 * @brief 发送错误消息
 * @param msg 错误内容
 */
void send_error(string msg)
{
    object me;

    if (!msg || msg == "")
        return;

    me = this_player();
    if (objectp(me))
        msg_send(me, HIR "【错误】" NOR + msg + "\n");
}

/**
 * @brief 发送成功消息
 * @param msg 成功内容
 */
void send_success(string msg)
{
    object me;

    if (!msg || msg == "")
        return;

    me = this_player();
    if (objectp(me))
        msg_send(me, HIG "【成功】" NOR + msg + "\n");
}

/**
 * @brief 发送提示消息
 * @param msg 提示内容
 */
void send_notice(string msg)
{
    object me;

    if (!msg || msg == "")
        return;

    me = this_player();
    if (objectp(me))
        msg_send(me, HIC "【提示】" NOR + msg + "\n");
}

/**
 * @brief 向房间内广播消息
 * @param room 房间对象
 * @param msg 消息内容
 * @param exclude 排除的对象（可选）
 */
varargs void broadcast_room(object room, string msg, object exclude)
{
    if (!room || !msg || msg == "")
        return;

    msg_to_room(room, msg, exclude);
}

varargs void send_xk_status(object me, mapping response_context)
{
    if (objectp(me)) {
        response_context = resolve_xk_response_context(me, response_context);
        CLIENT_SYNC_D->request_status(me, response_context);
    }
}

varargs void send_xk_quick_cmds(object me, mixed cmds, int priority,
    mapping response_context)
{
    mapping tx;

    if (!objectp(me))
        return;

    response_context = resolve_xk_response_context(me, response_context);
    tx = CLIENT_SYNC_D->tx_add_quick_cmds(0, cmds, priority);
    if (mapp(response_context))
        tx = CLIENT_SYNC_D->tx_with_response_context(tx, response_context);
    CLIENT_SYNC_D->submit_transaction(me, tx);
}

varargs void send_xk_secondary_cmds(object me, mixed cmds, string title,
    int priority, mapping response_context)
{
    mapping tx;

    if (!objectp(me))
        return;

    response_context = resolve_xk_response_context(me, response_context);
    tx = CLIENT_SYNC_D->tx_add_secondary_quick_cmds(0, cmds,
        stringp(title) ? title : "", priority);
    if (mapp(response_context))
        tx = CLIENT_SYNC_D->tx_with_response_context(tx, response_context);
    CLIENT_SYNC_D->submit_transaction(me, tx);
}

varargs void send_xk_dynamic_buttons(object me, mixed cmds, string title,
    int priority, mapping response_context)
{
    mapping tx;

    if (!objectp(me))
        return;

    response_context = resolve_xk_response_context(me, response_context);
    tx = CLIENT_SYNC_D->tx_add_dynamic_buttons(0, cmds,
        stringp(title) ? title : "", priority);
    if (mapp(response_context))
        tx = CLIENT_SYNC_D->tx_with_response_context(tx, response_context);
    CLIENT_SYNC_D->submit_transaction(me, tx);
}

void send_xk_chat_packet(object me, string chat_type, string msg)
{
    if (objectp(me))
        call_other("/kernel/simul_efun", "xk_send_chat_packet", me, chat_type, msg);
}

int send_xk_chat_packet_ex(object me, string chat_type, string msg, mapping extra)
{
    if (objectp(me))
        return call_other("/kernel/simul_efun", "xk_send_chat_packet_ex", me,
            chat_type, msg, mapp(extra) ? extra : ([]));
    return 0;
}

void emit_room_say_ui(object me, string msg)
{
    if (objectp(me))
        call_other("/kernel/simul_efun", "emit_room_say_ui", me, msg);
}

int emit_world_shout_ui(object me, string msg)
{
    if (objectp(me))
        return call_other("/kernel/simul_efun", "emit_world_shout_ui", me, msg);
    return 0;
}

varargs void send_xk_skill_list(object me, mapping response_context)
{
    if (objectp(me)) {
        response_context = resolve_xk_response_context(me, response_context);
        call_other("/kernel/simul_efun", "xk_send_skill_list", me,
            response_context);
    }
}

varargs void send_xk_skill_info(object me, string skill_id,
    mapping response_context)
{
    if (objectp(me) && stringp(skill_id)) {
        response_context = resolve_xk_response_context(me, response_context);
        call_other("/kernel/simul_efun", "xk_send_skill_info", me, skill_id,
            response_context);
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
        call_other(DAEMON_FRAMEWORK, "send_xk_trade_result_packet", me,
            action, item_name, price, success, extra_payload, response_context);
    }
}

varargs void emit_xk_shop_snapshot(object me, object vendor,
    mapping response_context)
{
    if (objectp(me) && objectp(vendor)) {
        response_context = resolve_xk_response_context(me, response_context);
        call_other(DAEMON_FRAMEWORK, "emit_xk_shop_snapshot", me, vendor,
            response_context);
    }
}
