#pragma strict_types

#include <system/ansi.h>
#include <game_constants.h>
#include <system/constants.h>
#include <system/services.h>

inherit "/lib/framework/command";
inherit "/lib/std/command_utils";

int main(object me, string arg);
int help(object me);

private int show_dungeon_list(object me);
private int show_dungeon_info(object me, string rest);
private int show_dungeon_status(object me);
private int do_enter(object me, string rest);
private int do_leave(object me);
private int do_claim(object me);
private int dispatch_action(object me, string action, string rest);

private object *resolve_enter_party(object me, string dungeon_type)
{
    mapping def;
    object room;
    string *member_ids;
    string member_id;
    object member;
    object *players;

    if (!objectp(me) || !stringp(dungeon_type) || dungeon_type == "")
        return 0;

    def = DUNGEON_D->query_dungeon_definition_for_actor(me, dungeon_type);
    if (!mapp(def))
        return 0;
    if ((def["max_players"] || MAX_TEAM_SIZE) <= 1)
        return ({ me });

    member_ids = TEAM_D->query_team_member_ids(me);
    if (!arrayp(member_ids) || !sizeof(member_ids))
        return ({ me });

    if (!TEAM_D->is_team_leader(me)) {
        msg_send(me, "只有队长才能带队进入副本。\n");
        return 0;
    }

    room = environment(me);
    players = ({ });
    foreach (member_id in member_ids) {
        member = TEAM_D->query_team_member_object(member_id);
        if (!objectp(member)) {
            msg_send(me, sprintf("队员 %s 当前不在线，无法进入副本。\n", member_id));
            return 0;
        }
        if (environment(member) != room) {
            msg_send(me, sprintf("队员 %s 不在副本入口处，无法一起进入副本。\n",
                member->query_name() || member_id));
            return 0;
        }
        if (mapp(DUNGEON_D->get_player_dungeon(member))) {
            msg_send(me, sprintf("队员 %s 已经在其他副本中。\n",
                member->query_name() || member_id));
            return 0;
        }
        if (DUNGEON_D->query_cooldown(member, dungeon_type) > 0) {
            msg_send(me, sprintf("队员 %s 当前副本仍在冷却中。\n",
                member->query_name() || member_id));
            return 0;
        }
        players += ({ member });
    }

    return players;
}

private string resolve_dungeon_state_label(mapping dungeon)
{
    if (!mapp(dungeon))
        return "未知";

    switch (dungeon["state"]) {
        case "active":
            return "进行中";
        case "completed":
            return "已完成";
        case "failed":
            if (dungeon["fail_reason"] == "timeout")
                return "已超时";
            if (dungeon["fail_reason"] == "wipe")
                return "已团灭";
            if (dungeon["fail_reason"] == "team_invalid")
                return "队伍失效";
            return "已失败";
    }

    return "未知";
}

private int can_enter_from_gate(object me, mapping def)
{
    object room;
    string room_path;
    string entry_gate_path;

    if (!objectp(me) || !mapp(def))
        return 0;

    room = environment(me);
    room_path = objectp(room) ? base_name(room) : "";
    entry_gate_path = def["public_entry_room"];
    if (!stringp(entry_gate_path) || entry_gate_path == "")
        entry_gate_path = CONFIG_D->query_start_room();

    if (room_path == entry_gate_path)
        return 1;

    msg_send(me, "当前只能在对应副本入口处进入副本。\n");
    return 0;
}

private int show_dungeon_list(object me)
{
    string *types;
    string dungeon_type;
    mapping def;
    string output;
    string status_label;
    int cooldown;

    types = sort_array(DUNGEON_D->query_all_dungeon_types(), 1);
    READ_SUMMARY_D->query_named_summary(me, "dungeon/list", ([
        "type": "dungeon_list_summary",
        "dungeon_count": arrayp(types) ? sizeof(types) : 0,
        "dungeon_types": arrayp(types) ? types : ({ }),
    ]));
    output = "可进入的副本：\n";
    if (!arrayp(types) || !sizeof(types)) {
        output += "当前没有可进入的副本。\n";
    } else {
        foreach (dungeon_type in types) {
            def = DUNGEON_D->query_dungeon_definition_for_actor(me,
                dungeon_type);
            if (!mapp(def))
                continue;
            cooldown = DUNGEON_D->query_cooldown(me, dungeon_type);
            status_label = cooldown > 0 ? sprintf("冷却 %d 秒", cooldown) : "可进入";
            output += sprintf("- %s（%s）：%s\n",
                dungeon_type,
                mapp(def) && stringp(def["name"]) ? def["name"] : dungeon_type,
                status_label);
        }
    }
    msg_send(me, output);
    return 1;
}

private int show_dungeon_status(object me)
{
    mapping dungeon;
    string output;

    dungeon = DUNGEON_D->get_player_dungeon(me);
    READ_SUMMARY_D->query_named_summary(me, "dungeon/status", ([
        "type": "dungeon_status_summary",
        "in_dungeon": mapp(dungeon) ? 1 : 0,
        "dungeon_type": mapp(dungeon) ? (dungeon["type"] || "") : "",
        "state": mapp(dungeon) ? (dungeon["state"] || "") : "",
        "stage": mapp(dungeon) ? (dungeon["stage"] || 0) : 0,
        "kills": mapp(dungeon) ? (dungeon["kills"] || 0) : 0,
    ]));
    if (!mapp(dungeon)) {
        msg_send(me, "当前没有处于任何副本中。\n");
        return 1;
    }

    output = sprintf("名称：%s\n", dungeon["name"] || dungeon["type"] || "未知");
    output += sprintf("类型：%s\n", dungeon["type"] || "未知");
    output += sprintf("状态：%s\n", resolve_dungeon_state_label(dungeon));
    output += sprintf("模式：%s\n", dungeon["mode"] == "team" ? "队伍" : "单人");
    output += sprintf("成员：%d\n", sizeof(dungeon["players"] || ({ })));
    output += sprintf("宝库领取：%d/%d\n",
        sizeof(keys(dungeon["treasury_claimed"] || ([]))),
        sizeof(dungeon["players"] || ({ })));
    output += sprintf("阶段：%d\n", dungeon["stage"] || 0);
    output += sprintf("击杀：%d\n", dungeon["kills"] || 0);
    output += sprintf("首领：%s\n", dungeon["boss_defeated"] ? "已击败" : "未击败");
    msg_send(me, output);
    return 1;
}

private int show_dungeon_info(object me, string rest)
{
    mapping def;
    string output;
    int duration;
    int reward_count;

    if (!stringp(rest) || rest == "") {
        msg_send(me, "用法：dungeon info <副本类型>\n");
        return 1;
    }

    def = DUNGEON_D->query_dungeon_definition_for_actor(me, rest);
    if (!mapp(def)) {
        msg_send(me, "没有找到这个副本类型。\n");
        return 1;
    }

    duration = intp(def["duration"]) ? def["duration"] : 0;
    reward_count = sizeof(def["treasury_rewards"] || ({ }));
    READ_SUMMARY_D->query_named_summary(me, "dungeon/info/" + rest, ([
        "type": "dungeon_info_summary",
        "dungeon_type": rest,
        "duration": duration,
        "reward_count": reward_count,
    ]));

    output = "副本信息\n";
    output += sprintf("名称：%s\n", def["name"] || rest);
    output += sprintf("类型：%s\n", rest);
    output += sprintf("入口：%s\n", def["public_entry_room"] || "未知");
    output += sprintf("宝库：%s\n", def["treasury_room"] || "未知");
    output += sprintf("时长：%s\n", duration > 0 ? sprintf("%d 秒", duration) : "未知");
    output += sprintf("奖励：%d 项宝库奖励\n", reward_count);
    msg_send(me, output);
    return 1;
}

private int do_enter(object me, string rest)
{
    mapping def;
    mapping dungeon;
    object *players;
    object *entered_players;
    object player;

    if (!stringp(rest) || rest == "") {
        msg_send(me, "用法：dungeon enter <副本类型>\n");
        return 1;
    }
    if (mapp(DUNGEON_D->get_player_dungeon(me))) {
        msg_send(me, "你已经在副本中了。\n");
        return 1;
    }
    if (DUNGEON_D->query_cooldown(me, rest) > 0) {
        msg_send(me, "当前副本仍在冷却中。\n");
        return 1;
    }

    def = DUNGEON_D->query_dungeon_definition_for_actor(me, rest);
    if (!mapp(def)) {
        msg_send(me, "没有找到这个副本类型。\n");
        return 1;
    }
    if (!can_enter_from_gate(me, def))
        return 1;
    players = resolve_enter_party(me, rest);
    if (!arrayp(players) || !sizeof(players))
        return 1;
    if (!DUNGEON_D->create_dungeon(rest, players, DUNGEON_DIFFICULTY_NORMAL)) {
        msg_send(me, "副本创建失败。\n");
        return 1;
    }

    dungeon = DUNGEON_D->get_player_dungeon(me);
    if (!mapp(dungeon)) {
        msg_send(me, "副本进入失败。\n");
        return 1;
    }

    entered_players = ({ });
    foreach (player in players) {
        if (!DUNGEON_ROOM_D->enter_dungeon(player, dungeon["id"])) {
            DUNGEON_D->abort_dungeon_entry(dungeon["id"], entered_players);
            msg_send(me, "副本进入失败。\n");
            return 1;
        }
        entered_players += ({ player });
    }

    foreach (player in players)
        msg_send(player, sprintf("你进入了副本：%s。\n", def["name"] || rest));
    return 1;
}

private int do_leave(object me)
{
    mapping dungeon;

    dungeon = DUNGEON_D->get_player_dungeon(me);
    if (!mapp(dungeon)) {
        msg_send(me, "当前不在副本中。\n");
        return 1;
    }
    if (!DUNGEON_D->retry_pending_leave(me)) {
        msg_send(me, "上次离场尚未完成，请稍后重试。\n");
        return 1;
    }
    if (!DUNGEON_D->leave_dungeon(me)) {
        msg_send(me, "离开副本失败，状态已保留，请重试。\n");
        return 1;
    }
    msg_send(me, "你离开了当前副本。\n");
    return 1;
}

private int do_claim(object me)
{
    mapping result;
    mapping response_context;

    response_context = CLIENT_SYNC_D->capture_command_response_context(me);
    result = DUNGEON_D->command_claim_treasury(me, response_context);
    msg_send(me, mapp(result) ? result["message"] : "宝库奖励领取失败。\n",
        response_context);
    return 1;
}

private int dispatch_action(object me, string action, string rest)
{
    switch (action) {
        case "list":
            return show_dungeon_list(me);
        case "info":
            return show_dungeon_info(me, rest);
        case "status":
            return show_dungeon_status(me);
        case "enter":
            return do_enter(me, rest);
        case "leave":
            return do_leave(me);
        case "claim":
            return do_claim(me);
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
        "命令：dungeon\n"
        "用法：dungeon list、dungeon info <副本类型>、dungeon status、dungeon enter <副本类型>、dungeon leave、dungeon claim\n"
        "说明：当前开放旧渡沉船单人剧情副本。击败副本首领后，才可在藏宝库领取战利品。\n");
    return 1;
}
