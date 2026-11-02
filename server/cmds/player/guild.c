#pragma strict_types

#include <system/ansi.h>
#include <system/services.h>
#include <framework/message_facade.h>

inherit "/lib/std/command_utils";
inherit "/lib/framework/network";

int main(object me, string arg);
int help(object me);

private string query_rank_label(string rank);
private string normalize_rank_input(string rank);
private string resolve_member_id(string token);
private string query_player_member_id(object who);
private mapping build_guild_dialog_extra(string dialog_id, mapping *actions);
private mapping *build_guild_entry_actions();
private mapping *build_guild_status_actions();
private mapping *build_guild_skills_actions();
private int show_guild_join_candidates(object me);
private int show_guild_appoint_candidates(object me);
private int show_guild_appoint_ranks(object me, string member_id);
private int show_guild_donate_options(object me);
private int show_guild_learn_offerings(object me, string rest);
private int compare_guild_learn_offering(mapping left, mapping right);
private void emit_guild_dialog(object me, string title, string content,
    string dialog_id, mapping *actions);
private void emit_guild_list_dialog(object me, string prefix);
private void emit_guild_status_dialog(object me, string prefix);
private void emit_guild_skills_dialog(object me, int page, string prefix);
private int show_guild_list(object me);
private int show_guild_status(object me);
private int show_guild_skills(object me, string rest);
private int do_create(object me, string rest);
private int do_join(object me, string rest);
private int do_leave(object me);
private int do_learn(object me, string rest);
private int do_donate(object me, string rest);
private int do_salary(object me);
private int do_appoint(object me, string rest);
private int dispatch_action(object me, string action, string rest);

private string format_skill_learn_failure(mapping result)
{
    string error;

    if (!mapp(result))
        return "当前无法学习这门帮派武学。\n";

    error = result["error"] || "";
    switch (error) {
        case "organization_missing":
        case "organization_not_allowed":
            return "你当前不在可授业的帮派体系里。\n";
        case "rank_too_low":
            return "你在帮派中的身份还不够。\n";
        case "contribution_too_low":
        case "insufficient_guild_contribution":
            return "你的帮派贡献不足。\n";
        case "insufficient_potential":
            return "你的潜能不足。\n";
        case "insufficient_combat_exp":
            return "你的实战经验不足。\n";
        case "missing_required_skills":
            return "你的前置技能还未达标。\n";
        case "already_learned":
            return "这门帮派武学你已经掌握。\n";
        default:
            return "当前无法学习这门帮派武学。\n";
    }
}

private string query_rank_label(string rank)
{
    switch (rank) {
        case "leader":
            return "帮主";
        case "deputy":
            return "副帮主";
        case "elder":
            return "长老";
        case "chef":
            return "堂主";
        case "elite":
            return "精英";
        case "member":
            return "帮众";
        case "trial":
            return "见习";
    }

    return "未知";
}

private string normalize_rank_input(string rank)
{
    string lowered;

    if (!stringp(rank) || rank == "")
        return "";

    lowered = lower_case(rank);
    switch (lowered) {
        case "leader":
        case "帮主":
            return "leader";
        case "deputy":
        case "副帮主":
            return "deputy";
        case "elder":
        case "长老":
            return "elder";
        case "chef":
        case "堂主":
            return "chef";
        case "elite":
        case "精英":
            return "elite";
        case "member":
        case "帮众":
            return "member";
        case "trial":
        case "见习":
            return "trial";
    }

    return "";
}

private string resolve_member_id(string token)
{
    object target;

    if (!stringp(token) || token == "")
        return "";

    target = find_player_online(token);
    if (objectp(target) && function_exists("query_account_id", target))
        return target->query_account_id() || token;

    return token;
}

private string query_player_member_id(object who)
{
    string player_id;

    if (!objectp(who))
        return "";
    if (function_exists("query_account_id", who)) {
        player_id = who->query_account_id() || "";
        if (player_id != "")
            return player_id;
    }

    player_id = get_object_id(who);
    return stringp(player_id) ? player_id : "";
}

private mapping build_guild_dialog_extra(string dialog_id, mapping *actions)
{
    return DIALOG_PROJECTION_D->build_dialog_extra("guild", dialog_id, ([
        "replace_mode": "replace",
        "actions": arrayp(actions) ? actions : ({ }),
    ]));
}

private mapping *build_guild_entry_actions()
{
    return ({
        (["label":"帮派列表", "cmd":"guild list"]),
        (["label":"帮派状态", "cmd":"guild status"]),
        (["label":"帮派武学", "cmd":"guild skills"]),
        (["label":"加入帮派", "cmd":"guild join-select"]),
        (["label":"查看帮助", "cmd":"help guild"]),
    });
}

private mapping *build_guild_status_actions()
{
    return ({
        (["label":"帮派状态", "cmd":"guild status"]),
        (["label":"帮派武学", "cmd":"guild skills"]),
        (["label":"领取俸禄", "cmd":"guild salary"]),
        (["label":"退出帮派", "cmd":"guild leave"]),
        (["label":"捐献帮派", "cmd":"guild donate-select"]),
        (["label":"学习武学", "cmd":"guild learn-select"]),
        (["label":"任命职位", "cmd":"guild appoint-select"]),
    });
}

private mapping *build_guild_skills_actions()
{
    return ({
        (["label":"帮派状态", "cmd":"guild status"]),
        (["label":"帮派武学", "cmd":"guild skills"]),
        (["label":"领取俸禄", "cmd":"guild salary"]),
        (["label":"捐献帮派", "cmd":"guild donate-select"]),
        (["label":"学习武学", "cmd":"guild learn-select"]),
        (["label":"任命职位", "cmd":"guild appoint-select"]),
        (["label":"查看帮助", "cmd":"help guild"]),
    });
}

private void emit_guild_dialog(object me, string title, string content,
    string dialog_id, mapping *actions)
{
    if (!objectp(me) || !stringp(content) || content == "")
        return;

    xk_send_dialog(me, title, content,
        build_guild_dialog_extra(dialog_id, actions),
        CLIENT_SYNC_D->capture_command_response_context(me));
}

private int show_guild_join_candidates(object me)
{
    string *guild_ids;
    mapping guild_info;
    mapping *actions;
    string content;
    int member_count;

    if (!objectp(me))
        return 0;

    guild_ids = sort_array(GUILD_D->query_all_guilds(), 1);
    actions = ({ (["label":"返回帮派列表", "cmd":"guild list"]) });
    content = "请选择要加入的帮派：\n";

    if (!arrayp(guild_ids) || sizeof(guild_ids) == 0) {
        content += "当前没有可加入的帮派。";
    } else {
        foreach (string guild_id in guild_ids) {
            string guild_name;

            guild_info = GUILD_D->query_guild_info(guild_id);
            guild_name = mapp(guild_info) && stringp(guild_info["name"])
                ? guild_info["name"]
                : guild_id;
            member_count = mapp(guild_info) && mapp(guild_info["members"])
                ? sizeof(keys(guild_info["members"]))
                : 0;
            content += sprintf("- %s（%s） %d人\n", guild_name, guild_id, member_count);
            actions += ({ (["label": guild_name, "cmd": "guild join " + guild_id]) });
        }
    }

    emit_guild_dialog(me, "加入帮派", content, "guild_join_select", actions);
    return 1;
}

private int show_guild_appoint_candidates(object me)
{
    string guild_id;
    mapping guild_info;
    string *members;
    mapping *actions;
    string self_id;
    string content;
    string member_id;
    mapping member_data;

    if (!objectp(me))
        return 0;

    guild_id = GUILD_D->query_player_guild(me);
    guild_info = GUILD_D->query_guild_info(guild_id);
    members = GUILD_D->query_guild_members(guild_id);
    self_id = query_player_member_id(me);

    if (!stringp(guild_id) || guild_id == "" || !mapp(guild_info)) {
        emit_guild_dialog(me, "任命职位", "当前没有可任命的帮派成员。",
            "guild_appoint_select", ({ (["label":"帮派状态", "cmd":"guild status"]) }));
        return 1;
    }

    actions = ({ (["label":"返回帮派状态", "cmd":"guild status"]) });
    content = "请选择要任命职位的成员：\n";
    foreach (member_id in sort_array(members, 1)) {
        string rank_label;

        if (!stringp(member_id) || member_id == "" || member_id == self_id)
            continue;
        member_data = guild_info["members"][member_id];
        rank_label = mapp(member_data) ? query_rank_label(member_data["rank"]) : "未知";
        content += sprintf("- %s（当前：%s）\n", member_id, rank_label);
        actions += ({ (["label": member_id, "cmd": "guild appoint-rank " + member_id]) });
    }

    if (sizeof(actions) == 1)
        content += "当前没有可任命的成员。";

    emit_guild_dialog(me, "任命职位", content, "guild_appoint_select", actions);
    return 1;
}

private int show_guild_appoint_ranks(object me, string member_id)
{
    string guild_id;
    string *members;
    mapping *actions;
    string self_id;
    string content;
    string rank;
    string label;

    if (!objectp(me) || !stringp(member_id) || member_id == "")
        return 0;

    guild_id = GUILD_D->query_player_guild(me);
    members = GUILD_D->query_guild_members(guild_id);
    self_id = query_player_member_id(me);
    if (!stringp(guild_id) || guild_id == "" || member_array(member_id, members) == -1 || member_id == self_id) {
        emit_guild_dialog(me, "任命职位", "当前无法为该成员设置职位。",
            "guild_appoint_rank", ({ (["label":"返回任命列表", "cmd":"guild appoint-select"]) }));
        return 1;
    }

    actions = ({ (["label":"返回任命列表", "cmd":"guild appoint-select"]) });
    content = sprintf("请选择要赋予 %s 的职位：\n", member_id);
    foreach (rank in ({ "deputy", "elder", "chef", "elite", "member", "trial" })) {
        label = query_rank_label(rank);
        content += sprintf("- %s\n", label);
        actions += ({ (["label": label, "cmd": "guild appoint " + member_id + " " + rank]) });
    }

    emit_guild_dialog(me, "任命职位", content,
        "guild_appoint_rank:" + member_id, actions);
    return 1;
}

private int show_guild_donate_options(object me)
{
    mapping *actions;
    string content;
    int amount;

    if (!objectp(me))
        return 0;

    actions = ({
        (["label":"返回帮派状态", "cmd":"guild status"]),
    });
    content = "请选择要捐献的金额：\n";
    foreach (amount in ({ 100, 500, 1000, 5000 })) {
        content += sprintf("- %d 两\n", amount);
        actions += ({ (["label": sprintf("%d 两", amount), "cmd": "guild donate " + amount]) });
    }

    emit_guild_dialog(me, "帮派捐献", content, "guild_donate_select", actions);
    return 1;
}

private int compare_guild_learn_offering(mapping left, mapping right)
{
    string left_key;
    string right_key;

    left_key = lower_case(left["display_skill_id"] || left["skill_id"] || left["source_id"] || "");
    right_key = lower_case(right["display_skill_id"] || right["skill_id"] || right["source_id"] || "");
    if (left_key == right_key)
        return 0;
    return left_key < right_key ? -1 : 1;
}

private int show_guild_learn_offerings(object me, string rest)
{
    mixed *offerings;
    mixed *page_offerings;
    mapping offering;
    mapping cost_spec;
    mapping *actions;
    string content;
    string token;
    string label;
    int page;
    int page_size;
    int total_pages;
    int start;
    int end;

    if (!objectp(me))
        return 0;

    offerings = SKILL_SOURCE_D->query_organization_skill_offerings(me, "guild");
    actions = ({ (["label":"返回帮派武学", "cmd":"guild skills"]) });
    if (!arrayp(offerings) || sizeof(offerings) == 0) {
        emit_guild_dialog(me, "学习武学", "当前没有可展示的帮派武学。",
            "guild_learn_select", actions);
        return 1;
    }

    offerings = sort_array(offerings, (: compare_guild_learn_offering :));
    page = 1;
    if (stringp(rest) && sscanf(rest, "%d", page) != 1)
        page = 1;
    page_size = 3;
    total_pages = (sizeof(offerings) + page_size - 1) / page_size;
    if (page <= 0)
        page = 1;
    if (page > total_pages)
        page = total_pages;
    start = (page - 1) * page_size;
    end = start + page_size - 1;
    if (end >= sizeof(offerings))
        end = sizeof(offerings) - 1;
    page_offerings = offerings[start..end];

    content = sprintf("请选择要学习的帮派武学（第 %d/%d 页）：\n", page, total_pages);
    foreach (offering in page_offerings) {
        if (!mapp(offering))
            continue;
        token = offering["display_skill_id"] || offering["skill_id"] || offering["source_id"] || "";
        label = offering["display_name"] || token;
        cost_spec = mapp(offering["cost_spec"]) ? offering["cost_spec"] : ([]);
        content += sprintf("- %s（%s）", token, label);
        if (offering["can_learn"] && offering["can_pay_cost"]) {
            if (mapp(cost_spec) && intp(cost_spec["amount"]) && cost_spec["amount"] > 0)
                content += sprintf(" 可学（%s:%d）", cost_spec["kind"] || "none", cost_spec["amount"] || 0);
            else
                content += " 可学";
            actions += ({ (["label": token, "cmd": "guild learn " + token]) });
        } else {
            content += " " + SKILL_SOURCE_D->format_teacher_offering_error(offering["error"] || "");
        }
        content += "\n";
    }

    if (page > 1)
        actions += ({ (["label":"上一页", "cmd":"guild learn-select " + (page - 1)]) });
    if (page < total_pages)
        actions += ({ (["label":"下一页", "cmd":"guild learn-select " + (page + 1)]) });
    emit_guild_dialog(me, "学习武学", content, "guild_learn_select", actions);
    return 1;
}

private void emit_guild_list_dialog(object me, string prefix)
{
    string *guild_ids;
    mapping guild_info;
    string content;
    int member_count;

    guild_ids = sort_array(GUILD_D->query_all_guilds(), 1);
    content = stringp(prefix) && prefix != "" ? prefix + "\n\n" : "";
    if (!arrayp(guild_ids) || sizeof(guild_ids) == 0) {
        content += "当前还没有任何帮派。";
    } else {
        content += "当前帮派列表：\n";
        foreach (string guild_id in guild_ids) {
            guild_info = GUILD_D->query_guild_info(guild_id);
            member_count = mapp(guild_info) && mapp(guild_info["members"])
                ? sizeof(keys(guild_info["members"]))
                : 0;
            content += sprintf("- %s（%s） %d人\n",
                guild_id,
                mapp(guild_info) && stringp(guild_info["name"]) ? guild_info["name"] : guild_id,
                member_count);
        }
    }

    emit_guild_dialog(me, "帮派列表", content, "guild_list", build_guild_entry_actions());
}

private void emit_guild_status_dialog(object me, string prefix)
{
    string guild_id;
    string player_id;
    mapping guild_info;
    mapping member_data;
    string *members;
    string content;
    int member_count;

    guild_id = GUILD_D->query_player_guild(me);
    if (!stringp(guild_id) || guild_id == "") {
        content = stringp(prefix) && prefix != "" ? prefix + "\n\n" : "";
        content += "当前角色没有加入任何帮派。";
        emit_guild_dialog(me, "帮派", content, "guild_status", build_guild_entry_actions());
        return;
    }

    guild_info = GUILD_D->query_guild_info(guild_id);
    members = GUILD_D->query_guild_members(guild_id);
    player_id = query_player_member_id(me);
    member_data = mapp(guild_info) && mapp(guild_info["members"])
        ? guild_info["members"][player_id]
        : 0;
    member_count = arrayp(members) ? sizeof(members) : 0;
    content = stringp(prefix) && prefix != "" ? prefix + "\n\n" : "";
    content += sprintf("帮派ID：%s\n", guild_id);
    content += sprintf("帮派名：%s\n",
        mapp(guild_info) && stringp(guild_info["name"]) ? guild_info["name"] : guild_id);
    content += sprintf("成员数：%d\n", member_count);
    content += sprintf("你的职位：%s\n",
        mapp(member_data) ? query_rank_label(member_data["rank"]) : "未知");
    content += sprintf("当前贡献：%d\n",
        mapp(member_data) ? (member_data["contribution"] || 0) : 0);
    content += "\n成员列表：\n";
    if (!arrayp(members) || sizeof(members) == 0) {
        content += "当前没有成员\n";
    } else {
        foreach (string member_id in sort_array(members, 1)) {
            member_data = guild_info["members"][member_id];
            content += sprintf("- %s %s %d贡\n",
                member_id,
                mapp(member_data) ? query_rank_label(member_data["rank"]) : "未知",
                mapp(member_data) ? (member_data["contribution"] || 0) : 0);
        }
    }

    emit_guild_dialog(me, "帮派状态", content, "guild_status", build_guild_status_actions());
}

private void emit_guild_skills_dialog(object me, int page, string prefix)
{
    string guild_id;
    string content;

    guild_id = GUILD_D->query_player_guild(me);
    if (!stringp(guild_id) || guild_id == "") {
        content = stringp(prefix) && prefix != "" ? prefix + "\n\n" : "";
        content += "当前角色没有加入任何帮派。";
        emit_guild_dialog(me, "帮派武学", content, "guild_skills", build_guild_entry_actions());
        return;
    }

    content = stringp(prefix) && prefix != "" ? prefix + "\n\n" : "";
    content += SKILL_SOURCE_D->render_organization_skill_offerings_page(me, "guild", page);
    emit_guild_dialog(me, "帮派武学", content, "guild_skills", build_guild_skills_actions());
}

private int show_guild_list(object me)
{
    string *guild_ids;
    mapping guild_info;
    mapping response_context;
    string output;
    int member_count;

    response_context = CLIENT_SYNC_D->capture_command_response_context(me);
    guild_ids = sort_array(GUILD_D->query_all_guilds(), 1);
    READ_SUMMARY_D->query_named_summary(me, "guild/list", ([
        "type": "guild_list_summary",
        "guild_count": arrayp(guild_ids) ? sizeof(guild_ids) : 0,
        "guild_ids": arrayp(guild_ids) ? guild_ids : ({ }),
    ]));
    output = "帮派列表：\n";
    if (!arrayp(guild_ids) || sizeof(guild_ids) == 0) {
        output += "当前还没有任何帮派。\n";
    } else {
        foreach (string guild_id in guild_ids) {
            guild_info = GUILD_D->query_guild_info(guild_id);
            member_count = mapp(guild_info) && mapp(guild_info["members"]) ? sizeof(keys(guild_info["members"])) : 0;
            output += sprintf("- %s（%s） %d人\n",
                guild_id,
                mapp(guild_info) && stringp(guild_info["name"]) ? guild_info["name"] : guild_id,
                member_count);
        }
    }
    if (!is_xk_client(me))
        msg_send(me, output);
    send_xk_quick_cmds(me,
        ({
            (["label":"帮派列表", "cmd":"guild list"]),
            (["label":"帮派状态", "cmd":"guild status"]),
            (["label":"帮派武学", "cmd":"guild skills"]),
        }),
        1, response_context);
    send_xk_secondary_cmds(me,
        ({
            (["label":"加入帮派", "cmd":"guild join-select"]),
            (["label":"查看帮助", "cmd":"help guild"]),
        }),
        "帮派入口",
        1, response_context);
    emit_guild_list_dialog(me, "");
    return 1;
}

private int show_guild_status(object me)
{
    string guild_id;
    string player_id;
    mapping response_context;
    mapping guild_info;
    mapping member_data;
    string *members;
    string output;
    int member_count;

    response_context = CLIENT_SYNC_D->capture_command_response_context(me);
    guild_id = GUILD_D->query_player_guild(me);
    if (!stringp(guild_id) || guild_id == "") {
        READ_SUMMARY_D->query_named_summary(me, "guild/status", ([
            "type": "guild_status_summary",
            "guild_id": "",
            "member_count": 0,
        ]));
        if (!is_xk_client(me))
            msg_send(me, "当前角色没有加入任何帮派。\n");
        send_xk_quick_cmds(me,
            ({
                (["label":"帮派列表", "cmd":"guild list"]),
                (["label":"帮派武学", "cmd":"guild skills"]),
            }),
            1, response_context);
        send_xk_secondary_cmds(me,
        ({
            (["label":"加入帮派", "cmd":"guild join-select"]),
            (["label":"查看帮助", "cmd":"help guild"]),
        }),
            "帮派入口",
            1, response_context);
        emit_guild_status_dialog(me, "");
        return 1;
    }

    guild_info = GUILD_D->query_guild_info(guild_id);
    members = GUILD_D->query_guild_members(guild_id);
    player_id = query_player_member_id(me);
    member_data = mapp(guild_info) && mapp(guild_info["members"]) ? guild_info["members"][player_id] : 0;
    member_count = arrayp(members) ? sizeof(members) : 0;
    READ_SUMMARY_D->query_named_summary(me, "guild/status", ([
        "type": "guild_status_summary",
        "guild_id": guild_id,
        "member_count": member_count,
        "rank": mapp(member_data) ? (member_data["rank"] || "") : "",
        "contribution": mapp(member_data) ? (member_data["contribution"] || 0) : 0,
    ]));

    output = sprintf("帮派ID：%s\n", guild_id);
    output += sprintf("帮派名：%s\n", mapp(guild_info) && stringp(guild_info["name"]) ? guild_info["name"] : guild_id);
    output += sprintf("成员数：%d\n", member_count);
    output += sprintf("你的职位：%s\n", mapp(member_data) ? query_rank_label(member_data["rank"]) : "未知");
    output += sprintf("当前贡献：%d\n", mapp(member_data) ? (member_data["contribution"] || 0) : 0);
    output += "成员列表：\n";
    if (!arrayp(members) || sizeof(members) == 0) {
        output += "当前没有成员。\n";
    } else {
        foreach (string member_id in sort_array(members, 1)) {
            member_data = guild_info["members"][member_id];
            output += sprintf("- %s %s %d贡\n",
                member_id,
                mapp(member_data) ? query_rank_label(member_data["rank"]) : "未知",
                mapp(member_data) ? (member_data["contribution"] || 0) : 0);
        }
    }
    if (!is_xk_client(me))
        msg_send(me, output);
    send_xk_quick_cmds(me,
        ({
            (["label":"帮派状态", "cmd":"guild status"]),
            (["label":"帮派武学", "cmd":"guild skills"]),
            (["label":"领取俸禄", "cmd":"guild salary"]),
            (["label":"退出帮派", "cmd":"guild leave"]),
        }),
        1, response_context);
    send_xk_secondary_cmds(me,
        ({
            (["label":"捐献帮派", "cmd":"guild donate-select"]),
            (["label":"学习武学", "cmd":"guild learn-select"]),
            (["label":"任命职位", "cmd":"guild appoint-select"]),
        }),
        "帮派操作",
        1, response_context);
    emit_guild_status_dialog(me, "");
    return 1;
}

private int show_guild_skills(object me, string rest)
{
    string guild_id;
    mapping response_context;
    int page;

    response_context = CLIENT_SYNC_D->capture_command_response_context(me);
    guild_id = GUILD_D->query_player_guild(me);
    if (!stringp(guild_id) || guild_id == "") {
        if (!is_xk_client(me))
            msg_send(me, "当前角色没有加入任何帮派。\n");
        return 1;
    }

    page = 1;
    if (stringp(rest) && sscanf(rest, "%d", page) != 1)
        page = 1;
    READ_SUMMARY_D->query_named_summary(me, "guild/skills", ([
        "type": "guild_skills_summary",
        "guild_id": guild_id,
        "page": page,
    ]));

    if (!is_xk_client(me))
        msg_send(me, SKILL_SOURCE_D->render_organization_skill_offerings_page(me, "guild", page));
    send_xk_quick_cmds(me,
        ({
            (["label":"帮派状态", "cmd":"guild status"]),
            (["label":"帮派武学", "cmd":"guild skills"]),
            (["label":"领取俸禄", "cmd":"guild salary"]),
        }),
        1, response_context);
    send_xk_secondary_cmds(me,
        ({
            (["label":"学习武学", "cmd":"guild learn-select"]),
            (["label":"捐献帮派", "cmd":"guild donate-select"]),
            (["label":"查看帮助", "cmd":"help guild"]),
        }),
        "帮派操作",
        1, response_context);
    emit_guild_skills_dialog(me, page, "");
    return 1;
}

private int do_create(object me, string rest)
{
    if (!stringp(rest) || rest == "") {
        msg_send(me, "用法：guild create <帮派名>\n");
        return 1;
    }

    if (GUILD_D->create_guild(me, rest)) {
        if (!is_xk_client(me))
            msg_send(me, "帮派创建成功。\n");
        emit_guild_status_dialog(me, "帮派创建成功。");
    } else {
        if (!is_xk_client(me))
            msg_send(me, "帮派创建失败。\n");
        emit_guild_list_dialog(me, "帮派创建失败。");
    }
    return 1;
}

private int do_join(object me, string rest)
{
    if (!stringp(rest) || rest == "") {
        msg_send(me, "用法：guild join <帮派ID>\n");
        return 1;
    }

    if (GUILD_D->join_guild(me, rest)) {
        if (!is_xk_client(me))
            msg_send(me, "加入帮派成功。\n");
        emit_guild_status_dialog(me, "加入帮派成功。");
    } else {
        if (!is_xk_client(me))
            msg_send(me, "加入帮派失败。\n");
        emit_guild_list_dialog(me, "加入帮派失败。");
    }
    return 1;
}

private int do_leave(object me)
{
    if (GUILD_D->leave_guild(me)) {
        if (!is_xk_client(me))
            msg_send(me, "退出帮派成功。\n");
        emit_guild_list_dialog(me, "退出帮派成功。");
    } else {
        if (!is_xk_client(me))
            msg_send(me, "退出帮派失败。\n");
        emit_guild_status_dialog(me, "退出帮派失败。");
    }
    return 1;
}

private int do_learn(object me, string rest)
{
    mapping result;
    string message;

    if (!stringp(rest) || rest == "") {
        msg_send(me, "用法：guild learn <武学标识>\n");
        return 1;
    }

    result = SKILL_SOURCE_D->learn_from_organization_source(me, "guild", rest);
    if (mapp(result) && result["ok"]) {
        message = sprintf("你学会了帮派武学 %s。",
            result["display_name"] || result["display_skill_id"] || result["skill_id"] || rest);
        if (!is_xk_client(me))
            msg_send(me, message + "\n");
        emit_guild_skills_dialog(me, 1, message);
        return 1;
    }

    if (!is_xk_client(me))
        msg_send(me, format_skill_learn_failure(result));
    emit_guild_skills_dialog(me, 1,
        replace_string(format_skill_learn_failure(result), "\n", ""));
    return 1;
}

private int do_donate(object me, string rest)
{
    int amount;

    if (!stringp(rest) || sscanf(rest, "%d", amount) != 1 || amount <= 0) {
        msg_send(me, "用法：guild donate <金额>\n");
        return 1;
    }

    if (GUILD_D->donate_guild(me, amount)) {
        if (!is_xk_client(me))
            msg_send(me, "帮派捐献成功。\n");
        emit_guild_status_dialog(me, "帮派捐献成功。");
    } else {
        if (!is_xk_client(me))
            msg_send(me, "帮派捐献失败。\n");
        emit_guild_status_dialog(me, "帮派捐献失败。");
    }
    return 1;
}

private int do_salary(object me)
{
    mapping result;
    string message;

    result = GUILD_D->command_receive_guild_salary(me);
    message = mapp(result) ? result["message"] : "帮派俸禄领取失败。\n";
    if (!is_xk_client(me))
        msg_send(me, message);
    emit_guild_status_dialog(me, replace_string(message, "\n", ""));
    return 1;
}

private int do_appoint(object me, string rest)
{
    mapping parsed;
    string member_id;
    string rank;

    parsed = parse_command_args(rest, "%s %s");
    if (!parsed["valid"] || !stringp(parsed["args"][0]) || !stringp(parsed["args"][1])) {
        msg_send(me, "用法：guild appoint <玩家ID> <职位>\n");
        return 1;
    }

    member_id = resolve_member_id(parsed["args"][0]);
    rank = normalize_rank_input(parsed["args"][1]);
    if (!stringp(member_id) || member_id == "" || rank == "") {
        msg_send(me, "职位必须是：副帮主/长老/堂主/精英/帮众/见习 或对应英文代号。\n");
        return 1;
    }

    if (GUILD_D->appoint_member(me, member_id, rank)) {
        if (!is_xk_client(me))
            msg_send(me, "职位任命成功。\n");
        emit_guild_status_dialog(me, "职位任命成功。");
    } else {
        if (!is_xk_client(me))
            msg_send(me, "职位任命失败。\n");
        emit_guild_status_dialog(me, "职位任命失败。");
    }
    return 1;
}

private int dispatch_action(object me, string action, string rest)
{
    switch (action) {
        case "list":
            return show_guild_list(me);
        case "join-select":
            return show_guild_join_candidates(me);
        case "status":
            return show_guild_status(me);
        case "skills":
            return show_guild_skills(me, rest);
        case "create":
            return do_create(me, rest);
        case "join":
            return do_join(me, rest);
        case "leave":
            return do_leave(me);
        case "learn-select":
            return show_guild_learn_offerings(me, rest);
        case "learn":
            return do_learn(me, rest);
        case "donate-select":
            return show_guild_donate_options(me);
        case "donate":
            return do_donate(me, rest);
        case "salary":
            return do_salary(me);
        case "appoint-select":
            return show_guild_appoint_candidates(me);
        case "appoint-rank":
            return show_guild_appoint_ranks(me, rest);
        case "appoint":
            return do_appoint(me, rest);
        default:
            return help(me);
    }
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

    return dispatch_action(me, parsed["action"], parsed["rest"]);
}

int help(object me)
{
    msg_send(me,
        "命令：guild\n"
        "用法：guild list、guild status、guild skills、guild create <帮派名>、guild join <帮派ID>、guild leave、guild learn <武学标识>、guild donate <金额>、guild salary、guild appoint <玩家ID> <职位>\n"
        "职位：副帮主、长老、堂主、精英、帮众、见习；也可用 deputy、elder、chef、elite、member、trial。\n");
    return 1;
}
