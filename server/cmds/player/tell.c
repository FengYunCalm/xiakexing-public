/**
 * @file    tell.c
 * @brief   私聊命令



 *
 * @par Description:
 * 向指定玩家发送私信，支持同场景与远程私聊消息。
 */

#pragma strict_types

#include <system/ansi.h>
#include <game/command.h>
#include <system/globals.h>
#include <system/constants.h>
#include <system/services.h>
#include <framework/message_facade.h>
#include <game_constants.h>

inherit "/lib/std/command_utils";
inherit "/lib/framework/network";

int help(object me);
int main(object me, string arg);

private string sanitize_message(string msg);
private int validate_tell_message_length(string msg);
private int can_send_tell(object me, object target);
private void record_reply_target(object me, object target);

/**
 * @brief 清理消息内容
 * @param msg 原始消息
 * @return string 清理后的消息
 */
private string sanitize_message(string msg)
{
    return CHAT_D->sanitize_chat_message(normalize_input_text(msg));
}

/**
 * @brief 验证消息长度
 * @param msg 消息内容
 * @return int 1=有效, 0=无效
 */
private int validate_tell_message_length(string msg)
{
    return validate_message_input(msg, MAX_TELL_LENGTH);
}

/**
 * @brief 验证是否可以发送消息
 * @param me 发送者
 * @param target_name 目标名称
 * @return int 1=可以, 0=不可以
 */
private int can_send_tell(object me, object target)
{
    if (!objectp(target))
        return 0;

    // 不能对自己说话
    if (target == me) {
        msg_send(me, "自言自语？用 say 命令吧。\n");
        return 0;
    }
    return 1;
}

/**
 * @brief 记录回复对象
 * @param me 发送者
 * @param target 接收者
 */
private void record_reply_target(object me, object target)
{
    me->set_temp("last_tell", get_object_id(target));
    target->set_temp("last_tell_from", get_object_id(me));
}

int main(object me, string arg)
{
    mapping parsed;
    string target_name, msg;
    object target;
    string self_text;
    string target_text;
    string me_name;
    string target_display_name;
    int self_sent;
    int target_sent;

    if (!me)
        return 0;

    parsed = parse_tell_args(arg);
    if (!parsed["valid"]) {
        msg_send(me, parsed["error"]);
        return 1;
    }

    target_name = parsed["target"];
    msg = parsed["message"];

    msg = sanitize_message(msg);
    if (!validate_tell_message_length(msg))
        return 1;

    target = find_player_in_room(me, target_name);
    if (!target)
        target = find_player_online(target_name);
    if (!target) {
        msg_send(me, "没有「" + target_name + "」这个玩家，或者他不在线。\n");
        return 1;
    }

    if (!can_send_tell(me, target))
        return 1;

    if (!CHAT_D->can_send_message(me)) {
        msg_send(me, "你说话太快了，稍候再试。\n");
        return 1;
    }

    if (environment(target) == environment(me)) {
        target_display_name = get_object_display_name(target, "对方");
        me_name = get_object_display_name(me, "有人");
        self_text = sprintf("你对%s说道：「%s」", target_display_name, msg);
        target_text = sprintf("%s对你说道：「%s」", me_name, msg);
        msg_send_legacy(me, HIC + self_text + NOR + "\n");
        msg_send_legacy(target, HIC + target_text + NOR + "\n");
    } else {
        target_display_name = get_object_display_name(target, "对方");
        me_name = get_object_display_name(me, "有人");
        self_text = sprintf("【千里传音】你对%s说道：「%s」", target_display_name, msg);
        target_text = sprintf("【千里传音】%s对你说道：「%s」", me_name, msg);
        msg_send_legacy(me, HIM + self_text + NOR + "\n");
        msg_send_legacy(target, HIM + target_text + NOR + "\n");
    }

    record_reply_target(me, target);
    self_sent = send_xk_chat_packet_ex(me, "tell", msg, ([
        "sender_name": get_object_display_name(me, "我"),
        "target_name": target_display_name,
        "direction": "self",
    ]));
    target_sent = send_xk_chat_packet_ex(target, "tell", msg, ([
        "sender_name": me_name,
        "target_name": target_display_name,
        "direction": "incoming",
    ]));
    if (self_sent && target_sent)
        CHAT_D->arm_message_cooldown(me);
    return 1;
}

int help(object me)
{
    msg_send(me, @HELP
命令：tell（私聊）

向指定玩家发送私信。

用法：
  tell <玩家名> <消息>  - 向指定玩家发送私信

示例：
  tell zhangsan 你好！  - 向张三发送私信
  tell 李四 一起组队吗？ - 向李四发送私信

注意：
  - 对方必须在线才能收到消息
  - 消息长度限制200字符
  - 频繁发送消息可能被限制

相关命令：
  say    - 对房间内所有人说话
  whisper - 私聊同房目标
  shout  - 喊话

HELP
    );
    return 1;
}
