#pragma strict_types

#include <system/ansi.h>
#include <system/services.h>

inherit "/lib/std/command_utils";
inherit "/lib/framework/network";

int main(object me, string arg);
int help(object me);

private mapping build_rank_dialog_extra(mapping *actions);
private mapping *build_rank_dialog_actions(string rank_type);
private void emit_rank_dialog(object me, string title, string content,
    string rank_type, string prefix, mapping response_context);
private int do_list(object me, string rest, mapping response_context);
private int do_me(object me, string rest, mapping response_context);
private int do_claim(object me, string rest, mapping response_context);
private int dispatch_action(object me, string action, string rest,
    mapping response_context);

private mapping build_rank_dialog_extra(mapping *actions)
{
    return DIALOG_PROJECTION_D->build_dialog_extra("rank", "rank_panel", ([
        "replace_mode": "replace",
        "actions": arrayp(actions) ? actions : ({ }),
    ]));
}

private mapping *build_rank_dialog_actions(string rank_type)
{
    string resolved_rank_type;

    resolved_rank_type = stringp(rank_type) && rank_type != "" ? rank_type : "power";
    return ({
        (["label":"实力榜", "cmd":"rank list power 10"]),
        (["label":"我的名次", "cmd":"rank me " + resolved_rank_type]),
        (["label":"领取奖励", "cmd":"rank claim " + resolved_rank_type]),
    });
}

private void emit_rank_dialog(object me, string title, string content,
    string rank_type, string prefix, mapping response_context)
{
    string dialog_content;

    if (!objectp(me) || !stringp(content) || content == "")
        return;

    dialog_content = stringp(prefix) && prefix != "" ? prefix + "\n\n" : "";
    dialog_content += content;
    xk_send_dialog(me, title, dialog_content,
        build_rank_dialog_extra(build_rank_dialog_actions(rank_type)),
        response_context);
}

private int do_list(object me, string rest, mapping response_context)
{
    mapping parsed;
    mapping view;
    mixed *entries;
    mapping entry;
    string dialog_content;
    string rank_type;
    string output;
    int count;
    int index;

    parsed = parse_command_args(rest, "%s %d");
    if (parsed["valid"]) {
        rank_type = parsed["args"][0];
        count = parsed["args"][1];
    } else {
        parsed = parse_command_args(rest, "%s");
        if (!parsed["valid"]) {
            msg_send(me, "用法：rank list <榜单类型> [数量]\n", response_context);
            return 1;
        }
        rank_type = parsed["args"][0];
        count = 10;
    }

    view = RANK_D->query_async_rank_view(rank_type, count);
    entries = mapp(view) && arrayp(view["entries"]) ? view["entries"] : ({ });
    READ_SUMMARY_D->query_named_summary(me, "rank/list/" + rank_type, ([
        "type": "rank_list_summary",
        "rank_type": rank_type,
        "limit": count,
        "entry_count": sizeof(entries),
        "async_state": mapp(view) ? (view["async_state"] || "") : "",
    ]));
    output = sprintf("%s 排行榜\n", rank_type);
    dialog_content = output;
    if (mapp(view) && stringp(view["async_state"]) && view["async_state"] != "ready") {
        output += sprintf("后台重算：%s\n", view["async_state"]);
        dialog_content += sprintf("后台重算：%s\n", view["async_state"]);
    }
    if (!arrayp(entries) || !sizeof(entries)) {
        output += "当前没有榜单数据。\n";
        dialog_content += "当前没有榜单数据。\n";
    } else {
        for (index = 0; index < sizeof(entries); index++) {
            entry = entries[index];
            if (!mapp(entry))
                continue;
            output += sprintf("%d. %s 分数：%d\n",
                index + 1,
                stringp(entry["player_id"]) ? entry["player_id"] : "未知",
                intp(entry["score"]) ? entry["score"] : 0);
            dialog_content += sprintf("%d. %s 分数：%d\n",
                index + 1,
                stringp(entry["player_id"]) ? entry["player_id"] : "未知",
                intp(entry["score"]) ? entry["score"] : 0);
        }
    }
    if (!is_xk_client(me))
        msg_send(me, output, response_context);
    send_xk_quick_cmds(me,
        ({
            (["label":"实力榜", "cmd":"rank list power 10"]),
            (["label":"我的名次", "cmd":"rank me power"]),
            (["label":"领取奖励", "cmd":"rank claim power"]),
        }),
        1, response_context);
    send_xk_secondary_cmds(me,
        ({
            (["label":"实力榜", "cmd":"rank list power 10"]),
            (["label":"我的名次", "cmd":"rank me power"]),
            (["label":"领取奖励", "cmd":"rank claim power"]),
        }),
        "排行扩展",
        1, response_context);
    emit_rank_dialog(me, "排行榜", dialog_content, rank_type, "",
        response_context);
    return 1;
}

private int do_me(object me, string rest, mapping response_context)
{
    string player_id;
    int rank;

    if (!stringp(rest) || rest == "") {
        msg_send(me, "用法：rank me <榜单类型>\n", response_context);
        return 1;
    }

    if (function_exists("query_account_id", me))
        player_id = me->query_account_id() || get_object_id(me);
    else
        player_id = get_object_id(me);
    rank = RANK_D->query_player_rank(player_id, rest);
    READ_SUMMARY_D->query_named_summary(me, "rank/me/" + rest, ([
        "type": "rank_me_summary",
        "rank_type": rest,
        "player_id": player_id,
        "rank": rank,
    ]));
    if (rank <= 0) {
        if (!is_xk_client(me))
            msg_send(me, "当前未进入该榜单。\n", response_context);
        emit_rank_dialog(me, "我的名次", "当前未进入该榜单。", rest, "",
            response_context);
        return 1;
    }

    if (!is_xk_client(me))
        msg_send(me, sprintf("你在榜单 %s 中当前排名第 %d。\n", rest, rank), response_context);
    send_xk_quick_cmds(me,
        ({
            (["label":"刷新名次", "cmd":"rank me " + rest]),
            (["label":"榜单列表", "cmd":"rank list " + rest + " 10"]),
            (["label":"领取奖励", "cmd":"rank claim " + rest]),
        }),
        1, response_context);
    send_xk_secondary_cmds(me,
        ({
            (["label":"实力榜", "cmd":"rank list power 10"]),
            (["label":"查看帮助", "cmd":"help rank"]),
        }),
        "排行扩展",
        1, response_context);
    emit_rank_dialog(me, "我的名次",
        sprintf("你在榜单 %s 中当前排名第 %d。", rest, rank),
        rest,
        "",
        response_context);
    return 1;
}

private int do_claim(object me, string rest, mapping response_context)
{
    mapping result;

    if (!stringp(rest) || rest == "") {
        msg_send(me, "用法：rank claim <榜单类型>\n", response_context);
        return 1;
    }

    result = RANK_D->command_claim_rank_reward(me, rest, response_context);
    if (!is_xk_client(me))
        msg_send(me, mapp(result) ? result["message"] : "排行奖励领取失败。\n", response_context);
    send_xk_quick_cmds(me,
        ({
            (["label":"榜单列表", "cmd":"rank list " + rest + " 10"]),
            (["label":"我的名次", "cmd":"rank me " + rest]),
        }),
        1, response_context);
    send_xk_secondary_cmds(me,
        ({
            (["label":"实力榜", "cmd":"rank list power 10"]),
            (["label":"查看帮助", "cmd":"help rank"]),
        }),
        "排行扩展",
        1, response_context);
    emit_rank_dialog(me,
        "排行奖励",
        mapp(result) ? (result["message"] || "排行奖励领取失败。") : "排行奖励领取失败。",
        rest,
        "",
        response_context);
    return 1;
}

private int dispatch_action(object me, string action, string rest,
    mapping response_context)
{
    switch (action) {
        case "list":
            return do_list(me, rest, response_context);
        case "me":
            return do_me(me, rest, response_context);
        case "claim":
            return do_claim(me, rest, response_context);
        default:
            return help(me, response_context);
    }
}

int main(object me, string arg)
{
    mapping parsed;
    mapping response_context;

    if (!objectp(me))
        return 0;
    response_context = CLIENT_SYNC_D->capture_command_response_context(me);
    if (!stringp(arg) || arg == "")
        return help(me, response_context);

    parsed = parse_subcommand_args(arg);
    if (!parsed["valid"])
        return help(me, response_context);

    return dispatch_action(me, parsed["action"], parsed["rest"],
        response_context);
}

varargs int help(object me, mapping response_context)
{
    msg_send(me,
        "命令：rank\n"
        "用法：rank list <榜单类型> [数量]、rank me <榜单类型>、rank claim <榜单类型>\n"
        "说明：list 查看榜单，me 查看自己的名次，claim 领取该榜单今日可领的排行奖励。\n",
        response_context);
    return 1;
}
