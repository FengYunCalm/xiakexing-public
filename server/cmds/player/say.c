/**
 * @file    say.c
 * @brief   说话命令 - 向当前房间发言



 */

#pragma strict_types

#include <framework/message_facade.h>

inherit "/lib/framework/network";
inherit "/lib/std/command_utils";

int help(object me);
int main(object me, string arg);

int main(object me, string arg)
{
    mapping parsed;

    if (!objectp(me))
        return 0;

    parsed = parse_say_args(arg);
    if (!parsed["valid"]) {
        msg_send(me, parsed["error"]);
        return 1;
    }

    emit_room_say_ui(me, parsed["message"]);
    return 1;
}

int help(object me)
{
    msg_send(me, @HELP
命令：say（说话）

向当前房间内的人公开说话。

用法：
  say <内容>       向当前房间发言

说明：
  - 只有同一房间内的人能看到
  - 发言内容会同步到客户端聊天界面

相关命令：
  whisper  - 私聊
  tell     - 远程私聊
  shout    - 全服广播
  emote    - 表情动作

HELP
    );
    return 1;
}
