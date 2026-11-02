#pragma strict_types

#include <framework/message_facade.h>
#include <system/services.h>

inherit "/lib/framework/command";
inherit "/lib/std/command_utils";

int main(object me, string arg);
int help(object me);

int main(object me, string arg)
{
    mapping result;
    mapping state;
    string action;
    string winner;
    object target;

    if (!objectp(me))
        return 0;

    if (!stringp(arg) || arg == "") {
        msg_send(me, "你想和谁文斗？\n");
        return 1;
    }

    if (arg == "status") {
        state = DEBATE_D->query_player_debate_state(me);
        if (!mapp(state)) {
            msg_send(me, "你当前没有文斗状态。\n");
            return 1;
        }

        if (state["status"] == "finished") {
            winner = state["winner"] || "";
            msg_send(me, sprintf("文斗已结束，胜者：%s。\n", winner != "" ? winner : "未定"));
            return 1;
        }

        msg_send(me, sprintf("文斗状态：第 %d 回合，双方出招后立即结算；未出招者超时还剩 %d 秒。\n",
            state["round"] || 1,
            (state["action_deadline"] || time()) > time() ? (state["action_deadline"] - time()) : 0));
        return 1;
    }

    if (member_array(arg, ({ "attack", "defend", "focus", "probe" })) != -1) {
        action = arg;
        result = DEBATE_D->choose_action(me, action);
        if (!mapp(result) || result["error"]) {
            msg_send(me, "文斗行动失败。\n");
            return 1;
        }

        msg_send(me, sprintf("你选择了%s。\n", action));
        if (result["status"] == "finished")
            msg_send(me, "这场文斗已经分出胜负。\n");
        return 1;
    }

    target = find_living_in_room(environment(me), arg, me);
    if (!objectp(target)) {
        msg_send(me, "这里没有这个文斗对象。\n");
        return 1;
    }

    result = DEBATE_D->start_debate(me, target);
    if (!mapp(result) || !result["id"]) {
        msg_send(me, "文斗发起失败。\n");
        return 1;
    }

    msg_send(me, sprintf("文斗开始，当前回合：%d。\n", result["round"] || 1));
    return 1;
}

int help(object me)
{
    msg_send(me, "用法：debate <目标>\n");
    return 1;
}
