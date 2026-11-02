/**
 * @file    util_message.c
 * @brief   消息发送工具 - Message Sending Utilities



 *
 * @par Description:
 * 提供消息发送和快捷命令生成功能。
 */

#pragma strict_types
#pragma save_types

#include <system/globals.h>
#include <system/ansi.h>
#include <system/constants.h>
#include <framework/message_facade.h>
#include <game_constants.h>

/**
 * @brief 发送成功消息
 * @param player 玩家对象
 * @param msg 消息内容
 */
void send_success_msg(object player, string msg)
{
    if (!player || !msg)
        return;

    /* 统一经由消息 facade 发送，颜色包装归这个轻量 helper 所有。 */
    msg_send(player, HIG "【成功】" NOR + msg + "\n");
}

/**
 * @brief 发送错误消息
 * @param player 玩家对象
 * @param msg 消息内容
 */
void send_error_msg(object player, string msg)
{
    if (!player || !msg)
        return;

    /* 保持和 send_success_msg 相同的 ownership，业务层不必重复前缀样式。 */
    msg_send(player, HIR "【错误】" NOR + msg + "\n");
}

/**
 * @brief 发送提示消息
 * @param player 玩家对象
 * @param msg 消息内容
 */
void send_notice_msg(object player, string msg)
{
    if (!player || !msg)
        return;

    /* 这里只负责展示包装，不承担协议分发或消息持久化。 */
    msg_send(player, HIC "【提示】" NOR + msg + "\n");
}

/**
 * @brief 广播消息到房间
 * @param room 房间对象
 * @param msg 消息内容
 * @param exclude 排除的对象
 */
varargs void broadcast_to_room(object room, string msg, object exclude)
{
    if (!room || !msg)
        return;

    /*
     * 房间广播继续委托给统一 facade，exclude 语义由底层实现裁决，
     * 这里不复制筛选规则。
     */
    msg_to_room(room, msg, exclude);
}
