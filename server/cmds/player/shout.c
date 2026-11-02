/**
 * @file    shout.c
 * @brief   大喊命令 - 向全服发送消息



 *
 * @par Description:
 * 向全服发送消息，消耗精力，发送XK协议消息。
 *
 * @par Usage:
 *   shout <消息>    - 向全服大喊
 *
 */

#pragma strict_types

#include <system/ansi.h>
#include <game/command.h>
#include <system/daemon.h>
#include <system/services.h>
#include <framework/message_facade.h>

inherit "/lib/std/command_utils";
inherit "/lib/framework/network";

/**
 * 命令主入口
 */
int main(object me, string arg)
{
    string name;
    string msg;
    int mp_cost;
    int sent;
    mapping response_context;

    if (!me) return 0;
    response_context = CLIENT_SYNC_D->capture_command_response_context(me);

    // 使用公共库解析参数
    mapping args = parse_say_args(arg);
    if (!args["valid"]) {
        msg_send(me, args["error"]);
        return 1;
    }

    msg = args["message"];
    msg = CHAT_D->sanitize_chat_message(msg);
    name = me->name();
    mp_cost = CONFIG_D->query_shout_mp_cost();

    if (!CHAT_D->can_send_message(me)) {
        msg_send(me, HIR "你说话太快了，稍候再试。" NOR "\n");
        return 1;
    }

    if (!wizardp(me)) {
        if (!function_exists("consume_mp", me) || !me->consume_mp(mp_cost)) {
            msg_send(me, HIR "你内力不足，无法大喊。" NOR "\n");
            return 1;
        }
    }

    sent = emit_world_shout_ui(me, msg);
    if (sent)
        CHAT_D->arm_message_cooldown(me);

    CLIENT_SYNC_D->request_status(me, response_context);

    return 1;
}

/**
 * 帮助信息
 */
int help(object me)
{
    msg_send(me, @HELP
命令：shout（大喊）

向全服玩家发送消息。

用法：
  shout <消息>    向全服大喊

消耗：
  内力按配置真源扣减（默认 10 点，巫师免费）

示例：
  shout 大家好！   全服广播"大家好！"

注意：
  - 大喊会消耗内力
  - 请勿滥用，以免打扰他人

相关命令：
  say      - 同房公开说话
  whisper  - 私聊指定玩家
  tell     - 远程私聊

HELP
    );
    return 1;
}
