#pragma strict_types

#include <system/ansi.h>
#include <system/services.h>
#include <framework/message_facade.h>

inherit "/lib/framework/command";
inherit "/lib/std/command_utils";

int main(object me, string arg);
int help(object me);

private string format_skill_learn_failure(mapping result)
{
    string error;

    if (!mapp(result))
        return "当前无法学习这门门派武学。\n";

    error = result["error"] || "";
    switch (error) {
        case "sect_not_allowed":
        case "organization_missing":
        case "organization_not_allowed":
            return "你当前不在可授业的门派体系里。\n";
        case "rank_too_low":
            return "你在门派中的身份还不够。\n";
        case "contribution_too_low":
        case "insufficient_sect_contribution":
            return "你的门派贡献不足。\n";
        case "insufficient_potential":
            return "你的潜能不足。\n";
        case "insufficient_combat_exp":
            return "你的实战经验不足。\n";
        case "missing_required_skills":
            return "你的前置技能还未达标。\n";
        case "already_learned":
            return "这门门派武学你已经掌握。\n";
        default:
            return "当前无法学习这门门派武学。\n";
    }
}

private int show_sect_list(object me)
{
    string output;
    string *sects;
    mapping def;

    sects = sort_array(SECT_D->query_all_sects(), 1);
    READ_SUMMARY_D->query_named_summary(me, "sect/list", ([
        "type": "sect_list_summary",
        "sect_count": arrayp(sects) ? sizeof(sects) : 0,
        "sect_ids": arrayp(sects) ? sects : ({ }),
    ]));
    output = "可加入的门派：\n";
    foreach (string sect_id in sects) {
        def = SECT_D->query_sect_definition(sect_id);
        output += sprintf("- %s（%s）：%s\n",
            sect_id,
            def["name"],
            def["description"]);
    }
    msg_send(me, output);
    return 1;
}

private int show_sect_status(object me)
{
    mapping status;
    string output;
    string next_rank_line;

    status = SECT_D->query_player_sect_status(me);
    READ_SUMMARY_D->query_named_summary(me, "sect/status", ([
        "type": "sect_status_summary",
        "joined": mapp(status) && status["joined"] ? 1 : 0,
        "sect_id": mapp(status) ? (status["sect_id"] || "") : "",
        "rank_name": mapp(status) ? (status["rank_name"] || "") : "",
        "contribution": mapp(status) ? (status["contribution"] || 0) : 0,
    ]));
    if (!mapp(status) || !status["joined"]) {
        msg_send(me, "当前没有所属门派。\n");
        return 1;
    }

    if (stringp(status["next_rank_name"]) && status["next_rank_name"] != "") {
        next_rank_line = sprintf("下一身份：%s（还需 %d 点贡献）\n",
            status["next_rank_name"],
            status["next_rank_gap"] || 0);
    } else {
        next_rank_line = "下一身份：当前已达到本门最高身份\n";
    }

    output = "门派状态\n";
    output += sprintf("所属门派：%s（%s）\n",
        status["sect_name"] || "",
        status["sect_id"] || "");
    output += sprintf("当前身份：%s\n", status["rank_name"] || "初入山门");
    output += sprintf("当前贡献：%d\n", status["contribution"] || 0);
    output += sprintf("接入范围：%s\n", status["provider_scope_name"] || "门派接引");
    output += next_rank_line;
    msg_send(me, output);
    return 1;
}

private int show_sect_skills(object me, string rest)
{
    int page;
    mapping status;

    status = SECT_D->query_player_sect_status(me);
    if (!mapp(status) || !status["joined"]) {
        msg_send(me, "当前没有所属门派。\n");
        return 1;
    }

    page = 1;
    if (stringp(rest) && sscanf(rest, "%d", page) != 1)
        page = 1;
    READ_SUMMARY_D->query_named_summary(me, "sect/skills", ([
        "type": "sect_skills_summary",
        "sect_id": status["sect_id"] || "",
        "page": page,
    ]));

    msg_send(me, SKILL_SOURCE_D->render_organization_skill_offerings_page(me, "sect", page));
    return 1;
}

private int do_join(object me, string rest)
{
    mapping result;

    if (!stringp(rest) || rest == "") {
        msg_send(me, "用法：sect join <门派名/ID>\n");
        return 1;
    }
    result = SECT_D->command_join_sect(me, rest);
    msg_send(me, result["message"]);
    return 1;
}

private int do_learn(object me, string rest)
{
    mapping result;

    if (!stringp(rest) || rest == "") {
        msg_send(me, "用法：sect learn <武学标识>\n");
        return 1;
    }

    result = SKILL_SOURCE_D->learn_from_organization_source(me, "sect", rest);
    if (mapp(result) && result["ok"]) {
        msg_send(me, sprintf("你学会了门派武学 %s。\n",
            result["display_name"] || result["display_skill_id"] || result["skill_id"] || rest));
        return 1;
    }

    msg_send(me, format_skill_learn_failure(result));
    return 1;
}

int main(object me, string arg)
{
    mapping parsed;

    if (!objectp(me))
        return 0;
    if (!stringp(arg) || arg == "")
        return help(me);

    parsed = parse_subcommand_args(arg);
    if (!parsed["valid"])
        return help(me);

    switch (parsed["action"]) {
        case "list":
            return show_sect_list(me);
        case "status":
            return show_sect_status(me);
        case "skills":
            return show_sect_skills(me, parsed["rest"]);
        case "join":
            return do_join(me, parsed["rest"]);
        case "learn":
            return do_learn(me, parsed["rest"]);
        default:
            return help(me);
    }
}

int help(object me)
{
    msg_send(me,
        "命令：sect\n"
        "用法：sect list、sect status、sect skills [页码]、sect join <门派名/ID>、sect learn <武学标识>\n");
    return 1;
}
