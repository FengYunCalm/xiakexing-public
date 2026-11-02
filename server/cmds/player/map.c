/**
 * @file    map.c
 * @brief   地图命令 - 显示当前位置周边地图



 *
 * @par Description:
 * 显示当前位置周边的地图，显示可移动的方向，发送XK协议消息。
 *
 * @par Usage:
 *   map              - 查看周边地图
 *
 */

#pragma strict_types

#include <system/ansi.h>
#include <game/command.h>
#include <system/services.h>
#include <system/xk_protocol.h>
#include <framework/message_facade.h>

inherit "/lib/framework/network";

#define XK_PROTOCOL_CORE "/kernel/framework/xk_protocol_core"

private string format_map_box_row(string content)
{
    return sprintf(HIC "║" NOR " %-38s" HIC "║" NOR "\n", content || "");
}

private string resolve_direction_label(object env, string dir)
{
    string label;

    if (!objectp(env) || !stringp(dir) || dir == "")
        return dir;

    if (function_exists("get_exit_direction_name", env)) {
        label = env->get_exit_direction_name(dir);
        if (stringp(label) && label != "")
            return label;
    }

    return dir;
}

private string resolve_exit_room_name(string dest)
{
    return call_other("/kernel/simul_efun", "resolve_room_display_name", dest);
}

/**
 * 命令主入口
 */
int main(object me, string arg)
{
    object env;
    mapping exits;
    string result;
    string *exit_keys;
    string *quick_cmds;
    int protocol_only;
    int map_ack_sent;
    mapping response_context;
    int i, count;

    if (!me) return 0;
    response_context = CLIENT_SYNC_D->capture_command_response_context(me);

    env = environment(me);
    if (!env) {
        msg_send(me, HIR "你不在任何地方。" NOR "\n");
        return 1;
    }

    exits = env->query("exits");
    protocol_only = stringp(arg) && lower_case(trim(arg)) == "xk";
    if (protocol_only) {
        if (mapp(exits) && sizeof(exits) > 0) {
            map_ack_sent = xk_send_map_grid(me, env->query("short") || "未知地点",
                env->query("zone") || "", exits, 1, response_context);
            if (!map_ack_sent) {
                call_other(XK_PROTOCOL_CORE, "xk_send", me, XK_SYST, ([
                    "content": "地图已刷新。",
                    "level": "info",
                    "reliability": "important",
                    "priority": "normal",
                ]), response_context);
            }
        }
        return 1;
    }

    result = HIC "╔════════════════════════════════════════╗" NOR "\n";
    result += format_map_box_row(HIG + " " + (env->query("short") || "未知地点") + NOR);
    result += HIC "╠════════════════════════════════════════╣" NOR "\n";

    if (!mapp(exits) || sizeof(exits) == 0) {
        result += format_map_box_row("这里没有明显的出口。");
    } else {
        result += format_map_box_row("可移动方向：");

        exit_keys = sort_array(keys(exits), 1);
        quick_cmds = ({ "移动" });
        count = sizeof(exit_keys);
        for (i = 0; i < count; i++) {
            string dir = exit_keys[i];
            string dir_cn = resolve_direction_label(env, dir);
            string dest = exits[dir];
            string dest_name = resolve_exit_room_name(dest);

            result += sprintf(HIC "║" NOR "   " HIY "%-6s" NOR " -> %-24s" HIC "║" NOR "\n",
                dir_cn, dest_name);
            quick_cmds += ({ dir_cn + ":go " + dir });
        }
    }

    result += HIC "╚════════════════════════════════════════╝" NOR "\n";
    if (!protocol_only)
        msg_send(me, result);

    /* 地图结构包只在玩家主动打开地图时发送，避免移动热路径携带大 MAPS 包。 */
    if (mapp(exits) && sizeof(exits) > 0) {
        xk_send_map_grid(me, env->query("short") || "未知地点", env->query("zone") || "", exits, 1,
            response_context);
        send_xk_quick_cmds(me,
            implode(quick_cmds, "|"),
            1, response_context);
    }

    return 1;
}

/**
 * 帮助信息
 */
int help(object me)
{
    msg_send(me, @HELP
命令：map（地图）

查看当前位置和可移动的方向。

用法：
  map              查看周边地图

显示内容：
  当前位置名称
  可移动方向及目标地点

说明：
  - 地图仅显示当前房间可见出口
  - 目标地点名称依赖目标房间 short

支持输入：
  n / s / e / w / ne / nw / se / sw / u / d / enter / out

地图显示：
  会把这些方向显示成中文标签（如北、南、东北、上、进、出）

相关命令：
  go     - 移动到指定方向
  look   - 查看当前位置详情

HELP
    );
    return 1;
}
