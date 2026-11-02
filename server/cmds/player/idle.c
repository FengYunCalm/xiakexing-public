#pragma strict_types

#include <system/services.h>
#include <framework/message_facade.h>

inherit "/lib/framework/command";
inherit "/lib/std/command_utils";

int help(object me);

/*
 * 挂机命令（按钮 cmd，玩家不手输）：
 *   idle stop      —— 结束当前挂机
 *   idle status    —— 查看挂机状态（弹窗由挂机点提供，此处兜底）
 */
int main(object me, string arg)
{
    mapping status;
    string action;

    if (!objectp(me))
        return 0;

    action = stringp(arg) ? trim(arg) : "";
    switch (action) {
    case "stop":
        IDLE_RUNTIME_D->cancel_idle_session(me);
        msg_send(me, "你收势站定，歇了口气。\n");
        return 1;
    case "status":
        status = IDLE_RUNTIME_D->query_idle_status(me);
        if (mapp(status) && status["active"]) {
            msg_send(me, "你还在打坐/练功，人不在也照常进行，下次上线领取。\n");
        } else {
            msg_send(me, "你现在没有在打坐或练功。\n");
        }
        return 1;
    default:
        return help(me);
    }
}

int help(object me)
{
    msg_send(me, "用法：idle stop | idle status\n"
        "说明：挂机通过挂机点弹窗操作，这里是底层命令。\n");
    return 1;
}
