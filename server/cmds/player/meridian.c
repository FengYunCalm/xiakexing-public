#pragma strict_types

#include <system/services.h>
#include <system/ansi.h>
#include <framework/message_facade.h>

inherit "/lib/framework/command";
inherit "/lib/std/command_utils";

int help(object me);

int main(object me, string arg)
{
    string rest;
    mapping def;
    mapping meridian;
    mapping bonuses;
    string *ids;
    int i;
    int result;

    if (!objectp(me))
        return 0;

    if (!stringp(arg) || arg == "" || arg == "list") {
        ids = sort_array(keys(MERIDIAN_D->query_meridians()), 1);
        msg_send(me, "经脉一览（进度/穴位）：\n");
        for (i = 0; i < sizeof(ids); i++) {
            meridian = MERIDIAN_D->query_meridian(ids[i]);
            if (!mapp(meridian))
                continue;
            msg_send(me, sprintf("  %s：%d/%d 通（内力 %d/穴）\n",
                meridian["name"], MERIDIAN_D->query_progress(me, ids[i]),
                meridian["points"], meridian["cost_mp_each"]));
        }
        if (intp(me->query("meridian/wind_fire")) && me->query("meridian/wind_fire") > 0)
            msg_send(me, HIY "你走火入魔，经脉受损（meridian repair 可修复）。\n" NOR);
        bonuses = MERIDIAN_D->query_meridian_bonuses(me);
        if (sizeof(bonuses) > 0)
            msg_send(me, "当前加成：" + sprintf("%O", bonuses) + "\n");
        return 1;
    }

    if (arg == "repair") {
        result = MERIDIAN_D->repair_wind_fire(me);
        if (result == 1)
            msg_send(me, "你调息半晌，紊乱的真气渐渐平复。\n");
        else if (result == -1)
            msg_send(me, "修复需要 200 文钱，你身上的钱不够。\n");
        else if (result == -2)
            msg_send(me, "修复结算未完成，状态未清除，请稍后重试。\n");
        else
            msg_send(me, "你没有走火，无需修复。\n");
        return 1;
    }

    if (sscanf(arg, "charge %s", rest) == 1 && rest != "") {
        result = MERIDIAN_D->attempt_charge(me, rest);
        def = MERIDIAN_D->query_meridian(rest);
        if (result > 0)
            msg_send(me, sprintf("你凝神运气，%s已通 %d 个穴位。\n",
                def["name"], result));
        else if (result == -1)
            msg_send(me, "没有这条经脉。\n");
        else if (result == -2)
            msg_send(me, "你经脉受损，先修复再冲穴（meridian repair）。\n");
        else if (result == -3)
            msg_send(me, "这条经脉已经全通了。\n");
        else if (result == -4)
            msg_send(me, "内力或潜能不足，冲穴失败。\n");
        else if (result == -5)
            msg_send(me, HIY "你冲穴太急，真气逆行——走火入魔了！\n" NOR
                "经脉受损，需修复（meridian repair）。\n");
        else if (result == -6)
            msg_send(me, "冲穴状态正在保存，请稍后查看，不要重复操作。\n");
        return 1;
    }

    return help(me);
}

int help(object me)
{
    msg_send(me, @HELP
命令：meridian（经脉）

查看与修炼经脉（时间型成长）。

用法：
  meridian                查看经脉进度与加成
  meridian charge <经脉>  尝试冲穴（消耗内力与潜能）
  meridian repair         修复走火损伤
HELP
    );
    return 1;
}
