/**
 * @file    breakthrough.c
 * @brief   技能突破玩家命令门面
 *
 * @par Description:
 * 命令层只负责参数解析、文本反馈和 XK 技能信息刷新；突破状态读侧回到
 * NUMERIC_RULE_D，选择写入只回到 SKILL_BREAKTHROUGH_D。
 */

#pragma strict_types

#include <system/services.h>

inherit "/lib/std/command_utils";
inherit "/lib/framework/network";

int main(object me, string arg);
int help(object me);

private int do_show(object me, string skill_id, mapping response_context)
{
    mapping state;
    mapping entry;
    mapping choice;
    mapping *available;
    string output;
    int i;

    if (!stringp(skill_id) || skill_id == "") {
        msg_send(me, "用法：breakthrough <技能ID>\n");
        return 1;
    }

    state = NUMERIC_RULE_D->query_skill_breakthrough_state(me, skill_id);
    if (!mapp(state) || !state["ok"]) {
        msg_send(me, "当前无法查看该技能突破。\n");
        return 1;
    }

    output = sprintf("技能：%s\n等级：%d\n已解锁层数：%d/%d\n下一级解锁：%d\n待选层数：%d\n",
        skill_id,
        state["level"] || 0,
        state["unlocked_tier"] || 0,
        state["max_tier"] || 0,
        state["next_level"] || 0,
        state["pending_count"] || 0);

    available = state["available"];
    if (!arrayp(available) || !sizeof(available)) {
        output += "当前没有可选突破。\n";
    } else {
        for (i = 0; i < sizeof(available); i++) {
            choice = available[i];
            if (!mapp(choice))
                continue;
            output += sprintf("- 第%d层（%d级）\n", choice["tier"] || 0, choice["level"] || 0);
            foreach (entry in choice["candidates"] || ({ })) {
                if (!mapp(entry))
                    continue;
                output += sprintf("  * %s：%s\n",
                    stringp(entry["id"]) ? entry["id"] : "unknown",
                    stringp(entry["description"]) ? entry["description"] : "");
            }
        }
    }

    msg_send(me, output);
    return 1;
}

private int do_choose(object me, string rest, mapping response_context)
{
    mapping parsed;
    mapping result;

    parsed = parse_command_args(rest, "%s %d %s");
    if (!parsed["valid"]) {
        msg_send(me, "用法：breakthrough <技能ID> <层数> <词条ID>\n");
        return 1;
    }

    result = SKILL_BREAKTHROUGH_D->choose_breakthrough(me, parsed["args"][0], parsed["args"][1], parsed["args"][2]);
    if (!mapp(result) || !result["ok"]) {
        msg_send(me, sprintf("突破选择失败：%s\n", mapp(result) && stringp(result["error"]) ? result["error"] : "unknown"));
        return 1;
    }

    msg_send(me, sprintf("已为技能 %s 选择第%d层突破：%s\n",
        parsed["args"][0], parsed["args"][1], parsed["args"][2]));
    if (is_xk_client(me))
        send_xk_skill_info(me, parsed["args"][0], response_context);
    return 1;
}

private string format_reset_failure(mapping result)
{
    string error;

    error = mapp(result) && stringp(result["error"]) ? result["error"] : "";
    switch (error) {
    case "skill_not_public":
        return "这门武学当前没有可重选的公开突破。";
    case "no_choice":
        return "这门武学尚未选择首层突破。";
    case "in_combat":
        return "战斗中不能重选突破。";
    case "teacher_not_present":
        return "需前往对应导师身边才能重选突破。";
    default:
        return "暂时无法重选这门武学的突破。";
    }
}

private int do_reset(object me, string skill_id, mapping response_context)
{
    mapping result;

    if (!stringp(skill_id) || skill_id == "") {
        msg_send(me, "用法：breakthrough reset <技能ID>\n");
        return 1;
    }

    result = SKILL_BREAKTHROUGH_D->reset_breakthrough(me, skill_id);
    if (!mapp(result) || !result["ok"]) {
        msg_send(me, "突破重选失败：" + format_reset_failure(result) + "\n");
        return 1;
    }

    msg_send(me, sprintf("已重选技能 %s 的首层突破，可重新作出选择。\n", skill_id));
    if (is_xk_client(me))
        send_xk_skill_info(me, skill_id, response_context);
    return 1;
}

int main(object me, string arg)
{
    mapping parsed;
    mapping direct;
    mapping response_context;

    if (!objectp(me))
        return 0;
    if (!stringp(arg) || arg == "")
        return help(me);

    response_context = CLIENT_SYNC_D->capture_command_response_context(me);
    parsed = parse_subcommand_args(arg);
    if (mapp(parsed) && parsed["valid"]) {
        if (parsed["action"] == "choose")
            return do_choose(me, parsed["rest"], response_context);
        if (parsed["action"] == "show")
            return do_show(me, parsed["rest"], response_context);
        if (parsed["action"] == "reset")
            return do_reset(me, parsed["rest"], response_context);
    }

    direct = parse_command_args(arg, "%s %d %s");
    if (direct["valid"])
        return do_choose(me, arg, response_context);

    return do_show(me, arg, response_context);
}

int help(object me)
{
    msg_send(me,
        "命令：breakthrough\n"
        "用法：breakthrough <技能ID>、breakthrough <技能ID> <层数> <词条ID>、breakthrough reset <技能ID>\n"
        "说明：查看技能突破候选、为已解锁层选择词条，或在对应导师身边脱战重选首层词条。\n");
    return 1;
}
