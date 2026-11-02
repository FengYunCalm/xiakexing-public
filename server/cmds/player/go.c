/**
 * @file    go.c
 * @brief   移动命令 - 按方向在房间间移动



 */

#pragma strict_types

#include <framework/message_facade.h>
#include <system/xk_protocol.h>

inherit "/lib/framework/command";

private void send_go_error(object me, string message, int code);
inherit "/lib/std/command_utils";

int help(object me);
int main(object me, string arg);

private void send_go_error(object me, string message, int code)
{
    if (!objectp(me) || !interactive(me))
        return;

    xk_send_error(me, message, code,
        CLIENT_SYNC_D->capture_command_response_context(me));
}

int main(object me, string arg)
{
    mapping result;

    if (!objectp(me))
        return 0;

    if (!stringp(arg) || arg == "") {
        msg_error(me, "你要往哪个方向走？\n", "go", "missing_direction", "",
            XK_ERR_INVALID_PARAM);
        return 1;
    }

    result = move_player_by_direction(me, arg);
    if (!mapp(result) || !result["ok"]) {
        if (stringp(result["error_message"]) && result["error_message"] != "")
            msg_error(me, result["error_message"], "go", result["error_type"], arg,
                result["error_type"] == "invalid_param" ? XK_ERR_INVALID_PARAM : XK_ERR_NOT_FOUND);
        if (result["error_type"] == "invalid_param")
            send_go_error(me, result["error_message"], XK_ERR_INVALID_PARAM);
        else if (result["error_type"] == "not_found")
            send_go_error(me, result["error_message"], XK_ERR_NOT_FOUND);
        return 1;
    }

    return 1;
}

int help(object me)
{
    msg_send(me, @HELP
命令：go（移动）

按方向在房间间移动。

用法：
  go <方向>

支持方向：
  n / s / e / w / ne / nw / se / sw / u / d / enter / out
  nu / nd / su / sd / eu / ed / wu / wd
  northup / northdown / southup / southdown / eastup / eastdown / westup / westdown
  也支持当前房间列出的其他出口名，例如 side / back / water / stable

示例：
  go north
  go enter

注意：
  - 战斗中使用 go 会改为尝试向该方向逃跑

相关命令：
  flee    - 逃跑
  look    - 查看当前房间

HELP
    );
    return 1;
}
