/**
 * @file    common_utils.c
 * @brief   公共工具函数实现



 *
 * @par Description:
 * 公共工具函数的具体实现。
 * 可通过 `COMMON_UTILS` 宏访问：`call_other(COMMON_UTILS, "函数名", 参数...)`
 */

#pragma strict_types
#pragma save_types

#include <system/globals.h>
#include <system/ansi.h>
#include <system/daemon.h>
#include <system/errors.h>
#include <framework/message_facade.h>
#include "common_utils.h"

int check_object(object ob, string msg)
{
    object receiver;

    if (!objectp(ob)) {
        if (stringp(msg) && msg != "") {
            receiver = this_player();
            if (objectp(receiver))
                msg_send(receiver, msg + "\n");
            call_other(ERROR_D, "log_error", ERR_NULL_POINTER, msg);
        }
        return 0;
    }
    return 1;
}

int check_player(object me, string cmd_name)
{
    object receiver;

    if (!objectp(me)) {
        return 0;
    }
    /*
     * 这里把 interactive() 视为玩家命令边界。
     * NPC 或守护对象即使复用命令代码，也不应被这个 helper 误判为合法调用者。
     */
    if (!interactive(me)) {
        if (stringp(cmd_name) && cmd_name != "") {
            receiver = this_player();
            if (objectp(receiver))
                msg_send(receiver, "该命令只能由玩家使用。\n");
            call_other(ERROR_D, "log_error", ERR_AUTH_DENIED, cmd_name);
        }
        return 0;
    }
    return 1;
}

int check_objects(object ob1, object ob2)
{
    return objectp(ob1) && objectp(ob2);
}

void msg_success(object me, string msg)
{
    if (!objectp(me) || !stringp(msg))
        return;
    msg_send(me, HIG + msg + NOR + "\n");
}

void msg_warning(object me, string msg)
{
    if (!objectp(me) || !stringp(msg))
        return;
    msg_send(me, HIY + msg + NOR + "\n");
}

void msg_info(object me, string msg)
{
    if (!objectp(me) || !stringp(msg))
        return;
    msg_send(me, HIC + msg + NOR + "\n");
}

void msg_highlight(object me, string msg)
{
    if (!objectp(me) || !stringp(msg))
        return;
    msg_send(me, HIW + msg + NOR + "\n");
}

int fail_msg(string msg)
{
    /*
     * 兼容老命令习惯，统一返回 0 方便直接 `return fail_msg(...)`。
     */
    if (stringp(msg) && msg != "") {
        call_other(ERROR_D, "log_error", ERR_UNKNOWN, msg);
        notify_fail(msg + "\n");
    }
    return 0;
}

void print_title(object me, string title)
{
    if (!objectp(me) || !stringp(title))
        return;
    msg_send(me, sprintf(HIC "\n══════════ %s ══════════" NOR "\n", title));
}

void print_separator(object me)
{
    if (!objectp(me))
        return;
    msg_send(me, HIC "────────────────────────────" NOR "\n");
}

void print_kv(object me, string key, string value, string key_color, string value_color)
{
    string kc, vc;

    if (!objectp(me) || !stringp(key) || !stringp(value))
        return;

    kc = stringp(key_color) ? key_color : HIY;
    vc = stringp(value_color) ? value_color : HIG;

    msg_send(me, sprintf("  " + kc + "%-12s" NOR " : " + vc + "%s" NOR "\n",
        key, value));
}

int validate_string(string str, string msg)
{
    if (!stringp(str) || str == "") {
        if (stringp(msg) && msg != "")
            fail_msg(msg);
        return 0;
    }
    return 1;
}

int validate_range(int value, int min_value, int max_value, string msg)
{
    if (value < min_value || value > max_value) {
        if (stringp(msg) && msg != "")
            fail_msg(msg);
        return 0;
    }
    return 1;
}

object validate_player_online(string player_id, string msg)
{
    object player;

    if (!stringp(player_id) || player_id == "") {
        if (stringp(msg) && msg != "")
            fail_msg(msg);
        return 0;
    }

    /*
     * 这里只保证当前驱动还能找到在线玩家对象。
     * 权限、可见性和业务可交互性仍由调用方继续检查。
     */
    player = find_player(player_id);
    if (!objectp(player)) {
        if (stringp(msg) && msg != "")
            fail_msg(msg);
        return 0;
    }

    return player;
}

string format_percent(int value, int max_value)
{
    if (max_value <= 0)
        return "0%";

    return sprintf("%d%%", (value * 100) / max_value);
}

string format_progress_bar(int current, int max_value, int width)
{
    int filled;
    string bar;

    if (max_value <= 0 || width <= 0)
        return "";

    if (current < 0)
        current = 0;
    if (current > max_value)
        current = max_value;

    filled = (current * width) / max_value;
    if (filled > width)
        filled = width;

    bar = sprintf("[%s%s]",
        repeat_string("█", filled),
        repeat_string("░", width - filled));

    return bar;
}

private string repeat_string(string str, int count)
{
    string result;

    /*
     * 保持私有 helper，避免外层把它当成通用高性能字符串构建接口来依赖。
     */
    if (!stringp(str) || count <= 0)
        return "";

    result = implode(allocate(count, str), "");
    return result;
}
