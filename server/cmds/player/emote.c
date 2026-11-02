/**
 * @file    emote.c
 * @brief   表情命令 - 显示各种表情动作



 *
 * @par Description:
 * 显示各种表情动作，支持自定义表情，发送XK协议消息。
 *
 * @par Usage:
 *   emote <动作>      - 显示表情动作
 *   emote list        - 显示所有表情
 *
 */

#pragma strict_types

#include <system/ansi.h>
#include <game/command.h>
#include <framework/message_facade.h>
#include <system/services.h>
#include <game_constants.h>

inherit "/lib/framework/network";
inherit "/lib/std/command_utils";

/* 预定义表情 */
private mapping emotes = ([
    "hi": "向大家挥手打招呼",
    "bye": "向大家挥手告别",
    "nod": "点了点头",
    "shake": "摇了摇头",
    "smile": "露出了微笑",
    "laugh": "哈哈大笑",
    "cry": "伤心地哭了",
    "angry": "愤怒地瞪大了眼睛",
    "think": "陷入沉思",
    "happy": "开心地跳了起来",
    "sad": "一脸沮丧",
    "sleep": "困得直打瞌睡",
    "kiss": "飞吻",
    "hug": "给大家一个大大的拥抱",
    "bow": "向大家鞠躬行礼",
    "pat": "拍了拍手",
    "jump": "高兴地跳了起来",
    "run": "飞快地跑开了",
    "look": "四处张望",
    "sweat": "流下了汗水",
]);

/**
 * 显示表情列表
 */
private void show_emote_list(object me)
{
    string result;
    string *emote_list;

    emote_list = keys(emotes);

    result = HIC "╔════════════════════════════════════════╗" NOR "\n";
    result += HIC "║" HIG "           表情列表" HIC "                    ║" NOR "\n";
    result += HIC "╠════════════════════════════════════════╣" NOR "\n";

    /* 分列显示 */
    int i, size = sizeof(emote_list);
    for (i = 0; i < size; i += 4) {
        string line = "";
        int j;
        for (j = 0; j < 4 && (i + j) < size; j++) {
            line += sprintf("%-10s", emote_list[i + j]);
        }
        result += sprintf(HIC "║" NOR " %-38s" HIC "║" NOR "\n", line);
    }

    result += HIC "╠════════════════════════════════════════╣" NOR "\n";
    result += sprintf(HIC "║" NOR " 使用: emote <表情名>" HIC "                 ║" NOR "\n");
    result += HIC "╚════════════════════════════════════════╝" NOR "\n";

    msg_send(me, result);
}

/**
 * 命令主入口
 */
int main(object me, string arg)
{
    string name;
    string action;
    string message;

    if (!me) return 0;

    name = me->query_name() || me->query("name") || "有人";

    if (!arg || arg == "") {
        show_emote_list(me);
        return 1;
    }

    if (arg == "list") {
        show_emote_list(me);
        return 1;
    }

    /* 查找预定义表情 */
    if (emotes[arg]) {
        action = emotes[arg];
        message = HIY + name + action + NOR + "\n";
    } else {
        /* 自定义表情：与 say/chat 同一套用户文本边界——
         * 去掉换行/制表（防止伪造额外行）、按字符数限制长度、并复用聊天冷却。 */
        action = normalize_input_text(arg);
        if (action == "") {
            msg_send(me, "这个表情是空的。\n");
            return 1;
        }
        if (strlen(action) > MAX_SAY_LENGTH) {
            msg_send(me, sprintf("表情太长了，最多%d个字符。\n", MAX_SAY_LENGTH));
            return 1;
        }
        if (!CHAT_D->can_send_message(me)) {
            msg_send(me, "你说话太快了，稍等片刻。\n");
            return 1;
        }
        message = HIY + name + " " + action + NOR + "\n";
    }

    /* 显示给房间内的所有人 */
    if (environment(me)) {
        msg_to_room(environment(me), message, ({ me }));
    }
    msg_send(me, message);
    if (!emotes[arg])
        CHAT_D->arm_message_cooldown(me);

    return 1;
}

/**
 * 帮助信息
 */
int help(object me)
{
    msg_send(me, @HELP
命令：emote（表情）

显示表情动作。

用法：
  emote <表情>      显示预定义表情
  emote <自定义>    显示自定义动作
  emote list        显示所有表情列表

内置表情：
  hi, bye, nod, shake, smile, laugh
  cry, angry, think, happy, sad, sleep
  kiss, hug, bow, pat, jump, run
  look, sweat

示例：
  emote smile       你露出了微笑
  emote 做了个鬼脸  你做了个鬼脸

相关命令：
  say     - 说话
  whisper - 私聊
  shout   - 大喊

HELP
    );
    return 1;
}
