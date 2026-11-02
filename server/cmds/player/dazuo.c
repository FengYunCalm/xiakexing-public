#pragma strict_types

#include <system/services.h>
#include <framework/message_facade.h>

inherit "/lib/framework/command";
inherit "/lib/std/command_utils";

/*
 * 打坐命令：在打坐挂机点发起离线吐纳。
 * 用法：dazuo | dazuo stop
 */
int main(object me, string arg)
{
    mapping result;
    mapping session;
    mapping context;
    object room;
    mixed points;

    if (!objectp(me))
        return 0;

    if (stringp(arg) && trim(arg) == "stop") {
        IDLE_RUNTIME_D->cancel_idle_session(me);
        msg_send(me, "你睁开眼，吐出一口浊气，站起身来。\n");
        return 1;
    }

    /* dazuo <skill_id>——指定内功开参悟挂机。
     * 门控谓词走 numeric_category 轴（基本内功 def.type=basic 但 numeric_category=force，
     * 按 def["type"] 判会错杀新手唯一内功）；skill_id 进 session.context，
     * 结算器据此发内功进度（每半小时 1 级）。 */
    if (stringp(arg) && trim(arg) != "") {
        string force_skill;
        mapping skills;

        force_skill = trim(arg);
        skills = me->query("skills") || ([]);
        if (!mapp(skills) || !mapp(skills[force_skill])) {
            msg_send(me, "你还没有学过这门内功，参悟无从谈起。\n");
            return 1;
        }
        if (!SKILL_BEHAVIOR_D->query_is_force_skill(force_skill)) {
            msg_send(me, "这不是可以打坐参悟的内功。\n");
            return 1;
        }
    }

    room = environment(me);
    if (!objectp(room)) {
        msg_send(me, "你无处安坐。\n");
        return 1;
    }

    points = room->query("idle_points");
    if (!arrayp(points) || member_array("dazuo", points) == -1) {
        msg_send(me, "这里不是打坐的好地方，心静不下来。\n");
        return 1;
    }

    context = ([ ]);
    if (stringp(arg) && trim(arg) != "")
        context["skill_id"] = trim(arg);
    session = IDLE_RUNTIME_D->start_idle_session(me, "dazuo",
        base_name(room), context);
    if (!mapp(session) || !session["ok"]) {
        msg_send(me, mapp(session) && stringp(session["error"])
            ? "打坐没能开始：" + session["error"] + "\n"
            : "打坐没能开始。\n");
        return 1;
    }

    msg_send(me, "你盘膝坐下，把气往丹田里沉。周围的声音渐渐远了。\n"
        "你不在的时候，这口气也会一直沉下去。\n"
        "（想收功，就点蒲团上的结束按钮）\n");
    return 1;
}

int help(object me)
{
    msg_send(me, "用法：dazuo        （在打坐点盘膝吐纳）\n"
        "      dazuo stop  （取消打坐）\n"
        "说明：打坐是离线吐纳，你不在时也会继续，下次上线领取。\n");
    return 1;
}
