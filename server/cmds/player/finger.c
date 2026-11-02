/**
 * @file    finger.c
 * @brief   查看玩家信息命令 - 显示玩家详细信息



 *
 * @par Description:
 * 查看指定玩家的详细信息，显示在线状态、成长档位、经验等，发送XK协议消息。
 *
 * @par Usage:
 *   finger              - 查看自己
 *   finger <玩家>       - 查看指定玩家
 *
 */

#pragma strict_types

#include <system/ansi.h>
#include <game/command.h>
#include <system/globals.h>
#include <system/constants.h>
#include <system/services.h>
#include <framework/message_facade.h>

inherit "/lib/std/command_utils";
inherit "/lib/framework/network";

/**
 * 显示玩家信息
 */
private void show_finger(object me, object target, int is_online,
    mapping response_context)
{
    string result;
    mapping snapshot;
    string name, id;
    int progression_rank, exp;
    int idle_sec;
    int last_on;
    string idle_label;
    string last_on_str;

    snapshot = query_player_public_snapshot(target);
    READ_SUMMARY_D->query_named_summary(me, "finger/" + (snapshot["id"] || "unknown"), ([
        "type": "finger_summary",
        "target_id": snapshot["id"] || "",
        "target_name": snapshot["name"] || "",
        "progression_rank": snapshot["progression_rank"] || 0,
        "exp": snapshot["exp"] || 0,
        "online": is_online ? 1 : 0,
    ]));
    name = snapshot["name"];
    id = snapshot["id"];
    progression_rank = snapshot["progression_rank"];
    exp = snapshot["exp"];
    idle_sec = snapshot["idle"];
    last_on = snapshot["last_on"];

    last_on_str = format_player_online_label(is_online, last_on);
    idle_label = is_online ? format_player_idle_label(idle_sec) : "";
    if (stringp(idle_label) && idle_label != "")
        last_on_str += " " + idle_label;

    result = HIC "╔════════════════════════════════════════╗" NOR "\n";
    result += sprintf(HIC "║" HIG " 玩家信息" HIC "                              ║" NOR "\n");
    result += HIC "╠════════════════════════════════════════╣" NOR "\n";
    result += sprintf(HIC "║" NOR " 姓名：%-32s" HIC "║" NOR "\n", name);
    result += sprintf(HIC "║" NOR " ID：%-33s" HIC "║" NOR "\n", id);
    result += sprintf(HIC "║" NOR " 成长档位：%-27d" HIC "║" NOR "\n", progression_rank);
    result += sprintf(HIC "║" NOR " 经验：%-31d" HIC "║" NOR "\n", exp);
    result += HIC "╠════════════════════════════════════════╣" NOR "\n";
    result += sprintf(HIC "║" NOR " 状态：%-31s" HIC "║" NOR "\n", last_on_str);
    result += HIC "╚════════════════════════════════════════╝" NOR "\n";

    msg_send(me, result, response_context);

    /* 发送快捷操作 */
    if (is_online && target != me) {
        send_xk_quick_cmds(me,
            "互动|私聊:whisper " + id + "|再次查看:finger " + id,
            1, response_context);
    }
}

/**
 * 命令主入口
 */
int main(object me, string arg)
{
    object target;
    mapping response_context;

    if (!me) return 0;

    response_context = CLIENT_SYNC_D->capture_command_response_context(me);
    if (!arg || arg == "") {
        show_finger(me, me, 1, response_context);
        return 1;
    }

    /* 查找在线玩家 */
    target = find_player_online(arg);
    if (target) {
        show_finger(me, target, 1, response_context);
        return 1;
    }

    msg_send(me, HIR "没有找到玩家 " + arg + "。" NOR "\n", response_context);
    return 1;
}

/**
 * 帮助信息
 */
int help(object me)
{
    msg_send(me, @HELP
命令：finger（查看玩家）

查看玩家的详细信息。

用法：
  finger              查看自己
  finger <玩家>       查看指定玩家

显示内容：
  姓名  - 玩家名称
  ID    - 玩家ID
  成长档位 - 技能上限驱动的成长档位
  经验  - 战斗经验
  状态  - 在线/最后登录时间

说明：
  - 只能查看在线玩家或自己

示例：
  finger              查看自己
  finger xiaoming     查看小明

相关命令：
  who    - 在线玩家列表
  hp     - 生命值状态

HELP
    );
    return 1;
}
