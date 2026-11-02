/**
 * @file    whisper.c
 * @brief   私聊命令 - 向指定玩家发送私密消息



 *
 * @par Description:
 * 向指定玩家发送私密消息，只有目标玩家能看到，发送XK协议消息。
 *
 * @par Usage:
 *   whisper <玩家> <消息>    - 向玩家私聊
 *
 */

#pragma strict_types

#include <system/ansi.h>
#include <game/command.h>
#include <system/services.h>
#include <framework/message_facade.h>
#include <game_constants.h>

inherit "/lib/std/command_utils";
inherit "/lib/framework/network";

/**
 * 命令主入口
 */
int main(object me, string arg)
{
    mapping parsed;
    string target_name, message;
    string self_text;
    string target_text;
    string me_name;
    string target_display_name;
    object target;
    int self_sent;
    int target_sent;
    mapping response_context;

    if (!me) return 0;
    response_context = CLIENT_SYNC_D->capture_command_response_context(me);

    parsed = parse_whisper_args(arg);
    if (!parsed["valid"]) {
        msg_send(me, parsed["error"]);
        return 1;
    }

    target_name = parsed["target"];
    message = parsed["message"];
    message = CHAT_D->sanitize_chat_message(message);

    if (!validate_message_input(message, MAX_TELL_LENGTH))
        return 1;

    target = find_player_in_room(me, target_name);

    if (!target) {
        msg_send(me, HIR "这里没有这个玩家。" NOR "\n");
        return 1;
    }

    if (target == me) {
        msg_send(me, HIR "你不能自言自语。" NOR "\n");
        return 1;
    }

    if (!CHAT_D->can_send_message(me)) {
        msg_send(me, HIR "你说话太快了，稍候再试。" NOR "\n");
        return 1;
    }

    /* 发送私聊消息 */
    me_name = get_object_display_name(me, "有人");
    target_display_name = get_object_display_name(target, "对方");
    self_text = "你悄悄对" + target_display_name + "说道：" + message;
    target_text = me_name + "悄悄对你说道：" + message;

    msg_send_legacy(me, HIC + self_text + NOR + "\n");
    msg_send_legacy(target, HIC + target_text + NOR + "\n");
    self_sent = send_xk_chat_packet_ex(me, "whisper", message, ([
        "sender_name": get_object_display_name(me, "我"),
        "target_name": target_display_name,
        "direction": "self",
    ]));
    target_sent = send_xk_chat_packet_ex(target, "whisper", message, ([
        "sender_name": me_name,
        "target_name": target_display_name,
        "direction": "incoming",
    ]));
    if (self_sent && target_sent)
        CHAT_D->arm_message_cooldown(me);

    /* 发送快捷操作 */
    send_xk_quick_cmds(me,
        "交流|回复:whisper " + target_name + "|公开说话:say",
        1, response_context);

    return 1;
}

/**
 * 帮助信息
 */
int help(object me)
{
    msg_send(me, @HELP
命令：whisper（私聊）

向指定玩家发送私密消息。

用法：
  whisper <玩家> <消息>    向玩家私聊

示例：
  whisper xiaoming 你好    对小明私聊“你好”

注意：
  - 目标玩家必须与你在同一房间
  - 私聊内容只有双方能看到

相关命令：
  tell    - 远程私聊
  say     - 同房公开说话
  shout   - 全服大喊

HELP
    );
    return 1;
}
