#pragma strict_types

#include <system/services.h>
#include <system/xk_protocol.h>
#include <framework/message_facade.h>

inherit "/lib/framework/command";
inherit "/lib/std/command_utils";

int main(object me, string arg);
int help(object me);

private string format_practice_failure(mapping result)
{
    string error;

    if (!mapp(result))
        return "你现在还没法练这门技能。\n";

    error = result["error"];
    switch (error) {
        case "missing_practice_source":
            return "这门技能目前没有可用的练习路径。\n";
        case "not_enabled":
            return "你还没有把这门技能当作当前练习目标。\n";
        case "missing_basic_skill":
            return "你的基础技能还没到位。\n";
        case "exp_too_low":
            return "你的实战经验不足，暂时练不好这门技能。\n";
        case "practice_limit_reached":
            return "你靠单纯练习已经很难再提升这门技能了。\n";
        case "practice_failed":
            return "你反复揣摩动作，却没抓住要领。\n";
        case "insufficient_potential":
            return "你的潜能不足，暂时付不起这次练习消耗。\n";
        case "insufficient_combat_exp":
            return "你的实战经验不足，暂时付不起这次练习消耗。\n";
        case "insufficient_sect_contribution":
            return "你的门派贡献不足，暂时付不起这次练习消耗。\n";
        case "insufficient_guild_contribution":
            return "你的帮派贡献不足，暂时付不起这次练习消耗。\n";
        case "spend_failed":
            return "练习消耗结算失败，请稍后再试。\n";
        default:
            return "你现在还没法练这门技能。\n";
    }
}

private string query_practice_failure_reason(mapping result)
{
    if (mapp(result) && stringp(result["error"]) && result["error"] != "")
        return result["error"];
    return "practice_failed";
}

private void send_practice_error(object me, string skill_id, mapping result)
{
    string message;

    message = format_practice_failure(result);
    xk_send_command_error(me, message, XK_ERR_SKILL_NOT_FOUND, "practice",
        query_practice_failure_reason(result), skill_id,
        CLIENT_SYNC_D->capture_command_response_context(me));
    msg_error(me, message, "practice", query_practice_failure_reason(result), skill_id,
        XK_ERR_SKILL_NOT_FOUND);
}

/*
 * 离线练功：校验练功房挂机点 + 技能可练，然后记录挂机会话。
 * 挂机点声明由房间配置 idle_points 提供（见 rooms.c 扩展）。
 */
private int is_practice_idle_point(object me)
{
    object room;
    mixed points;

    room = environment(me);
    if (!objectp(room))
        return 0;
    points = room->query("idle_points");
    if (!arrayp(points))
        return 0;
    return member_array("practice", points) != -1;
}

private int do_offline_practice(object me, string skill_id)
{
    mapping quote;
    mapping session;
    mixed previous_enabled;

    if (!is_practice_idle_point(me)) {
        msg_send(me, "这里不是练功的地方，静不下心来。\n");
        return 1;
    }

    /* L-211：只用查询型入口（can_practice + 报价）做资格校验。原实现把结算
     * 入口 practice_skill 当校验，会先真实练一次并扣消耗，挂机会话启动失败
     * 时这部分消耗无法回退。requires_enable 的练习来源要求
     * practice/enabled 与技能一致，故先登记并在失败分支恢复。 */
    previous_enabled = me->query_temp("practice/enabled");
    me->set_temp("practice/enabled", skill_id);
    quote = SKILL_SOURCE_D->query_practice_quote(me, skill_id);
    if (!mapp(quote) || !quote["ok"]) {
        if (undefinedp(previous_enabled))
            me->delete_temp("practice/enabled");
        else
            me->set_temp("practice/enabled", previous_enabled);
        send_practice_error(me, skill_id, quote);
        return 1;
    }

    session = IDLE_RUNTIME_D->start_idle_session(me, "practice",
        base_name(environment(me)), ([ "skill_id": skill_id ]));
    if (!mapp(session) || !session["ok"]) {
        /* 启动失败：技能与消耗都未动，启用技能也回到原值，不残留。 */
        if (undefinedp(previous_enabled))
            me->delete_temp("practice/enabled");
        else
            me->set_temp("practice/enabled", previous_enabled);
        msg_send(me, mapp(session) && stringp(session["error"])
            ? "练功没能开始：" + session["error"] + "\n"
            : "练功没能开始。\n");
        return 1;
    }

    msg_send(me, sprintf("你沉下心，把「%s」从头到尾练了一遍又一遍。\n"
        "你不在的时候，这功夫也会自己走下去。\n"
        "（想收功，就点练功桩上的结束按钮）\n", skill_id));
    return 1;
}

int do_idle_stop(object me)
{
    if (!objectp(me))
        return 0;
    IDLE_RUNTIME_D->cancel_idle_session(me);
    msg_send(me, "你收势站定，歇了口气。\n");
    return 1;
}

int main(object me, string arg)
{
    mapping result;
    string skill_id;

    if (!objectp(me))
        return 0;
    if (!stringp(arg) || arg == "")
        return help(me);

    /* 离线练功：practice idle，使用当前已启用的练习技能 */
    if (trim(arg) == "idle") {
        skill_id = me->query_temp("practice/enabled") || "";
        if (skill_id == "")
            return help(me);
        return do_offline_practice(me, skill_id);
    }

    /* practice idle <skill_id>——指定技能直接开离线练功
     * （客户端选技弹窗入口；结算链读 session.context.skill_id）。 */
    if (sscanf(arg, "idle %s", skill_id) == 1 && trim(skill_id) != "") {
        return do_offline_practice(me, trim(skill_id));
    }

    /* 取消离线练功 */
    if (trim(arg) == "stop")
        return do_idle_stop(me);

    skill_id = arg;
    me->set_temp("practice/enabled", skill_id);
    result = SKILL_SOURCE_D->practice_skill(me, skill_id);
    if (mapp(result) && result["ok"]) {
        msg_system(me, sprintf("你反复揣摩，感觉%s又精进了一层。\n", skill_id),
            "skill_practice");
        return 1;
    }

    send_practice_error(me, skill_id, result);
    return 1;
}

int help(object me)
{
    msg_send(me, "用法：practice <skill_id>\n"
        "      practice idle  （在挂机点发起离线练功，练当前启用技能）\n");
    return 1;
}
