/**
 * @file    team.c
 * @brief   组队命令 - 创建和管理队伍



 *
 * @par Description:
 * 创建和管理队伍，邀请/踢出队员，显示队伍状态，发送XK协议消息。
 *
 * @par Usage:
 *   team                    - 显示队伍状态
 *   team with <玩家>        - 邀请玩家组队
 *   team accept             - 接受当前组队邀请
 *   team reject             - 拒绝当前组队邀请
 *   team kick <玩家>        - 踢出队员
 *   team dismiss            - 解散队伍
 *   team leave              - 离开队伍
 *
 */

#pragma strict_types

#include <system/ansi.h>
#include <system/services.h>
#include <framework/message_facade.h>
#include <game/command.h>

inherit "/lib/framework/network";
inherit "/lib/std/command_utils";

private mapping build_team_dialog_extra(string dialog_id, mapping *actions);
private mapping *build_team_empty_actions(int has_pending_invite);
private mapping *build_team_member_actions(int is_leader);
private int show_team_invite_candidates(object me, mapping response_context);
private int show_team_kick_candidates(object me, mapping response_context);
private void emit_team_dialog(object me, string title, string content,
    mapping *actions, mapping response_context);
private string find_team_member_id(object me, string target_name)
{
    string *member_ids;
    string member_id;
    object member;
    string member_name;

    if (!objectp(me) || !stringp(target_name) || target_name == "")
        return 0;

    member_ids = TEAM_D->query_team_member_ids(me);
    foreach (member_id in member_ids) {
        if (target_name == member_id)
            return member_id;

        member = TEAM_D->query_team_member_object(member_id);
        if (!objectp(member))
            continue;

        member_name = get_object_display_name(member, "对方");
        if (target_name == member_name || target_name == get_object_display_id(member, "未知"))
            return member_id;
    }

    return 0;
}

private mapping build_team_dialog_extra(string dialog_id, mapping *actions)
{
    return DIALOG_PROJECTION_D->build_dialog_extra("team", dialog_id, ([
        "replace_mode": "replace",
        "actions": arrayp(actions) ? actions : ({ }),
    ]));
}

private mapping *build_team_empty_actions(int has_pending_invite)
{
    if (has_pending_invite) {
        return ({
            (["label":"接受邀请", "cmd":"team accept"]),
            (["label":"拒绝邀请", "cmd":"team reject"]),
            (["label":"刷新状态", "cmd":"team"]),
            (["label":"在线玩家", "cmd":"who"]),
        });
    }

    return ({
        (["label":"刷新状态", "cmd":"team"]),
        (["label":"邀请组队", "cmd":"team with-select"]),
        (["label":"在线玩家", "cmd":"who"]),
        (["label":"查看帮助", "cmd":"help team"]),
    });
}

private mapping *build_team_member_actions(int is_leader)
{
    if (is_leader) {
        return ({
            (["label":"队伍状态", "cmd":"team"]),
            (["label":"解散队伍", "cmd":"team dismiss"]),
            (["label":"踢出队员", "cmd":"team kick-select"]),
            (["label":"在线玩家", "cmd":"who"]),
        });
    }

    return ({
        (["label":"队伍状态", "cmd":"team"]),
        (["label":"离开队伍", "cmd":"team leave"]),
        (["label":"在线玩家", "cmd":"who"]),
        (["label":"解除跟随", "cmd":"follow none"]),
    });
}

private void emit_team_dialog(object me, string title, string content,
    mapping *actions, mapping response_context)
{
    if (!objectp(me) || !stringp(content) || content == "")
        return;

    xk_send_dialog(me, title, content,
        build_team_dialog_extra("team_status", actions),
        response_context);
}

private int show_team_invite_candidates(object me, mapping response_context)
{
    object env;
    object *room_inventory;
    string *member_ids;
    mapping *actions;
    object target;
    string target_id;
    string label;
    string content;

    if (!objectp(me))
        return 0;

    if (sizeof(TEAM_D->query_team_member_ids(me)) > 0 && !TEAM_D->is_team_leader(me)) {
        emit_team_dialog(me, "邀请组队",
            "只有队长才能邀请新成员。",
            ({ (["label":"返回队伍", "cmd":"team"]) }), response_context);
        return 1;
    }

    env = environment(me);
    if (!objectp(env)) {
        emit_team_dialog(me, "邀请组队",
            "当前场景无法邀请其他玩家。",
            ({ (["label":"返回队伍", "cmd":"team"]) }), response_context);
        return 1;
    }

    room_inventory = all_inventory(env);
    member_ids = TEAM_D->query_team_member_ids(me);
    actions = ({ (["label":"返回队伍", "cmd":"team"]) });
    content = "请选择当前房间中要邀请的玩家：\n";

    foreach (target in room_inventory) {
        if (!objectp(target) || target == me || !userp(target))
            continue;

        target_id = get_object_display_id(target, "");
        if (!stringp(target_id) || target_id == "")
            continue;
        if (member_array(target_id, member_ids) != -1)
            continue;

        label = get_object_display_name(target, target_id);
        content += sprintf("- %s（%s）\n", label, target_id);
        actions += ({ (["label": label, "cmd": "team with " + target_id]) });
    }

    if (sizeof(actions) == 1)
        content += "当前没有可邀请的玩家。";

    xk_send_dialog(me, "邀请组队", content,
        build_team_dialog_extra("team_with_select", actions),
        response_context);
    return 1;
}

private int show_team_kick_candidates(object me, mapping response_context)
{
    string *member_ids;
    mapping *actions;
    string self_id;
    string member_id;
    object member;
    string label;
    string content;

    if (!objectp(me))
        return 0;

    if (!TEAM_D->is_team_leader(me)) {
        emit_team_dialog(me, "踢出队员",
            "只有队长才能踢出队员。",
            ({ (["label":"返回队伍", "cmd":"team"]) }), response_context);
        return 1;
    }

    self_id = get_object_display_id(me, "");
    member_ids = TEAM_D->query_team_member_ids(me);
    actions = ({ (["label":"返回队伍", "cmd":"team"]) });
    content = "请选择要移出队伍的成员：\n";

    foreach (member_id in member_ids) {
        if (!stringp(member_id) || member_id == "" || member_id == self_id)
            continue;

        member = TEAM_D->query_team_member_object(member_id);
        label = objectp(member) ? get_object_display_name(member, member_id) : member_id + "（离线）";
        content += sprintf("- %s\n", label);
        actions += ({ (["label": label, "cmd": "team kick " + member_id]) });
    }

    if (sizeof(actions) == 1)
        content += "当前没有可踢出的队员。";

    xk_send_dialog(me, "踢出队员", content,
        build_team_dialog_extra("team_kick_select", actions),
        response_context);
    return 1;
}

private string query_pending_leader_label(mapping invite)
{
    object leader;

    if (!mapp(invite) || !stringp(invite["leader_id"]) || invite["leader_id"] == "")
        return "对方";

    leader = TEAM_D->query_team_member_object(invite["leader_id"]);
    if (objectp(leader))
        return get_object_display_name(leader, invite["leader_id"]);

    return invite["leader_id"];
}

/**
 * 显示队伍状态
 */
private void emit_team_status_ui(object me, mapping response_context)
{
    mapping *dialog_actions;
    string dialog_content;
    int is_leader;
    int show_plain;
    mapping pending_invite;
    string *member_ids;
    string leader_id;
    string result;
    int total_count;
    int online_count;
    string member_id;

    member_ids = TEAM_D->query_team_member_ids(me);
    leader_id = TEAM_D->query_team_leader_id(me);
    show_plain = !is_xk_client(me);

    if (!arrayp(member_ids) || sizeof(member_ids) == 0) {
        pending_invite = TEAM_D->query_pending_invite_snapshot(me);
        READ_SUMMARY_D->query_named_summary(me, "team/status", ([
            "type": "team_status_summary",
            "member_count": 0,
            "online_count": 0,
            "has_pending_invite": mapp(pending_invite) ? 1 : 0,
        ]));
        dialog_actions = build_team_empty_actions(mapp(pending_invite));
        result = show_plain ? HIC "╔════════════════════════════════════════╗" NOR "\n" : "";
        if (mapp(pending_invite)) {
            if (show_plain) {
                result += HIC "║" HIY "      你收到了一份待处理的组队邀请" HIC "      ║" NOR "\n";
                result += HIC "╠════════════════════════════════════════╣" NOR "\n";
                result += sprintf(HIC "║" NOR " %-38s" HIC "║" NOR "\n",
                    "邀请来自：" + query_pending_leader_label(pending_invite));
                result += sprintf(HIC "║" NOR " %-38s" HIC "║" NOR "\n",
                    "输入 team accept / team reject 处理邀请");
            }
            dialog_content = "你收到了一份待处理的组队邀请。\n"
                + "邀请来自：" + query_pending_leader_label(pending_invite) + "\n"
                + "可直接在当前面板中接受或拒绝邀请。";
        } else {
            if (show_plain)
                result += HIC "║" YEL "        你目前没有加入任何队伍" HIC "           ║" NOR "\n";
            dialog_content = "你目前没有加入任何队伍。\n"
                + "可以邀请房间内玩家组队，或先查看当前在线玩家。";
        }
        if (show_plain)
            result += HIC "╚════════════════════════════════════════╝" NOR "\n";
        if (mapp(pending_invite)) {
            send_xk_quick_cmds(me,
                "组队|接受邀请:team accept|拒绝邀请:team reject",
                1, response_context);
            send_xk_secondary_cmds(me,
                ({
                    (["label":"查看邀请", "cmd":"team"]),
                    (["label":"在线玩家", "cmd":"who"]),
                    (["label":"查看帮助", "cmd":"help team"]),
                }),
                "组队辅助",
                1, response_context);
        } else {
            if (show_plain)
                result += HIC "使用 team with <玩家> 来邀请他人组队。" NOR "\n";
            send_xk_quick_cmds(me,
                ({
                    (["label":"刷新状态", "cmd":"team"]),
                    (["label":"在线玩家", "cmd":"who"]),
                    (["label":"查看帮助", "cmd":"help team"]),
                }),
                1, response_context);
            send_xk_secondary_cmds(me,
                ({
                    (["label":"邀请组队", "cmd":"team with-select"]),
                    (["label":"查看属性", "cmd":"score"]),
                    (["label":"解除跟随", "cmd":"follow none"]),
                }),
                "组队辅助",
                1, response_context);
        }
        if (show_plain)
            msg_send(me, result, response_context);
        emit_team_dialog(me, "队伍", dialog_content, dialog_actions, response_context);
        return;
    }

    result = HIC "╔════════════════════════════════════════╗" NOR "\n";
    result += HIC "║" HIG "           队伍成员" HIC "                    ║" NOR "\n";
    result += HIC "╠════════════════════════════════════════╣" NOR "\n";

    total_count = sizeof(member_ids);
    online_count = 0;
    foreach (member_id in member_ids) {
        object member;
        string status;

        member = TEAM_D->query_team_member_object(member_id);
        status = (member_id == leader_id) ? HIY "【队长】" NOR : "【队员】";
        if (objectp(member)) {
            mapping snapshot;
            online_count++;
            snapshot = query_player_public_snapshot(member);
            result += sprintf(HIC "║" NOR " %-38s" HIC "║" NOR "\n",
                status + format_player_brief_line(snapshot));
        } else {
            result += sprintf(HIC "║" NOR " %-38s" HIC "║" NOR "\n",
                status + "【离线】" + member_id);
        }
    }
    READ_SUMMARY_D->query_named_summary(me, "team/status", ([
        "type": "team_status_summary",
        "leader_id": leader_id || "",
        "member_count": total_count,
        "online_count": online_count,
        "member_ids": member_ids,
    ]));

    result += HIC "╠════════════════════════════════════════╣" NOR "\n";
    result += sprintf(HIC "║" NOR " 队伍人数：%d 人（在线 %d 人）" HIC "               ║" NOR "\n", total_count, online_count);
    result += HIC "╚════════════════════════════════════════╝" NOR "\n";
    dialog_content = "当前队伍成员：\n";
    foreach (member_id in member_ids) {
        object member;
        string status;

        member = TEAM_D->query_team_member_object(member_id);
        status = (member_id == leader_id) ? "【队长】" : "【队员】";
        if (objectp(member)) {
            mapping snapshot;

            snapshot = query_player_public_snapshot(member);
            dialog_content += status + format_player_brief_line(snapshot) + "\n";
        } else {
            dialog_content += status + "【离线】" + member_id + "\n";
        }
    }
    dialog_content += sprintf("\n队伍人数：%d 人（在线 %d 人）", total_count, online_count);

    if (show_plain)
        msg_send(me, result, response_context);

    is_leader = me == TEAM_D->query_leader(me);
    dialog_actions = build_team_member_actions(is_leader);

    if (is_leader) {
        send_xk_quick_cmds(me,
            ({
                (["label":"队伍状态", "cmd":"team"]),
                (["label":"解散队伍", "cmd":"team dismiss"]),
            }),
            1, response_context);
        send_xk_secondary_cmds(me,
                ({
                    (["label":"踢出队员", "cmd":"team kick-select"]),
                    (["label":"在线玩家", "cmd":"who"]),
                    (["label":"解除跟随", "cmd":"follow none"]),
                    (["label":"查看帮助", "cmd":"help team"]),
            }),
            "队长操作",
            1, response_context);
    } else {
        send_xk_quick_cmds(me,
            ({
                (["label":"队伍状态", "cmd":"team"]),
                (["label":"离开队伍", "cmd":"team leave"]),
            }),
            1, response_context);
        send_xk_secondary_cmds(me,
            ({
                (["label":"在线玩家", "cmd":"who"]),
                (["label":"解除跟随", "cmd":"follow none"]),
                (["label":"查看帮助", "cmd":"help team"]),
            }),
            "队伍辅助",
                1, response_context);
    }

    emit_team_dialog(me, "队伍", dialog_content, dialog_actions, response_context);
}

/**
 * 邀请玩家组队
 */
private int do_team_with(object me, string target_name, mapping response_context)
{
    mapping result;
    object target;

    if (!target_name || target_name == "") {
        msg_send(me, HIR "请指定要邀请的玩家。" NOR "\n", response_context);
        return 1;
    }

    target = find_player_in_room(me, target_name);
    if (!target) {
        msg_send(me, HIR "这里没有这个玩家。" NOR "\n", response_context);
        return 1;
    }

    if (target == me) {
        msg_send(me, HIR "你不能和自己组队。" NOR "\n", response_context);
        return 1;
    }

    result = TEAM_D->request_team_invite(me, target);
    if (!mapp(result) || !result["ok"]) {
        msg_send(me, HIR + (mapp(result) ? (result["message"] || "组队邀请失败。\n") : "组队邀请失败。\n") + NOR, response_context);
        return 1;
    }

    msg_send(me, HIG "你向 " + get_object_display_name(target, "对方") + " 发出了组队邀请。" NOR "\n", response_context);
    msg_send(target, HIY + get_object_display_name(me, "有人") + " 邀请你组队，输入 team accept 接受，或输入 team reject 拒绝。" NOR "\n");
    send_xk_quick_cmds(target,
        "组队|接受邀请:team accept|拒绝邀请:team reject",
        1);

    return 1;
}

private int do_team_accept(object me, mapping response_context)
{
    mapping result;
    object leader;
    string leader_label;

    result = TEAM_D->accept_team_invite(me);
    if (!mapp(result) || !result["ok"]) {
        msg_send(me, HIR + (mapp(result) ? (result["message"] || "接受组队邀请失败。\n") : "接受组队邀请失败。\n") + NOR, response_context);
        return 1;
    }

    leader = TEAM_D->query_team_member_object(result["leader_id"] || "");
    leader_label = objectp(leader) ? get_object_display_name(leader, result["leader_id"]) : (result["leader_id"] || "队长");
    if (!is_xk_client(me))
        msg_send(me, HIG "你加入了 " + leader_label + " 的队伍。" NOR "\n", response_context);
    if (objectp(leader))
        msg_send(leader, HIG + get_object_display_name(me, "有人") + " 接受了你的组队邀请。" NOR "\n");
    emit_team_status_ui(me, response_context);
    return 1;
}

private int do_team_reject(object me, mapping response_context)
{
    mapping result;
    object leader;

    result = TEAM_D->reject_team_invite(me);
    if (!mapp(result) || !result["ok"]) {
        msg_send(me, HIR + (mapp(result) ? (result["message"] || "拒绝组队邀请失败。\n") : "拒绝组队邀请失败。\n") + NOR, response_context);
        return 1;
    }

    if (!is_xk_client(me))
        msg_send(me, HIG "你拒绝了这份组队邀请。" NOR "\n", response_context);
    leader = TEAM_D->query_team_member_object(result["leader_id"] || "");
    if (objectp(leader))
        msg_send(leader, HIY + get_object_display_name(me, "有人") + " 拒绝了你的组队邀请。" NOR "\n");
    emit_team_status_ui(me, response_context);
    return 1;
}

/**
 * 踢出队员
 */
private int do_team_kick(object me, string target_name, mapping response_context)
{
    string target_id;
    object target;
    string target_label;

    if (!TEAM_D->is_team_leader(me)) {
        msg_send(me, HIR "只有队长才能踢出队员。" NOR "\n", response_context);
        return 1;
    }

    if (!stringp(target_name) || target_name == "") {
        msg_send(me, HIR "请指定要踢出的队员。" NOR "\n", response_context);
        return 1;
    }

    target_id = find_team_member_id(me, target_name);
    if (!stringp(target_id) || target_id == "") {
        msg_send(me, HIR "队伍中没有这个成员。" NOR "\n", response_context);
        return 1;
    }

    target = TEAM_D->query_team_member_object(target_id);
    target_label = objectp(target) ? get_object_display_name(target, target_id) : target_id;

    if (target_id == TEAM_D->query_team_leader_id(me)) {
        msg_send(me, HIR "不能踢出自己，请使用 team dismiss 或 team leave。" NOR "\n", response_context);
        return 1;
    }

    if (!TEAM_D->kick_team_member_id(me, target_id)) {
        msg_send(me, HIR "踢出队员失败。" NOR "\n", response_context);
        return 1;
    }

    if (!is_xk_client(me))
        msg_send(me, HIG "你将 " + target_label + " 移出了队伍。" NOR "\n",
        response_context);
    if (objectp(target))
        msg_send(target, HIY + get_object_display_name(me, "有人") + " 将你移出了队伍。" NOR "\n");
    emit_team_status_ui(me, response_context);
    return 1;
}

/**
 * 离开队伍
 */
private int do_team_leave(object me, mapping response_context)
{
    if (!TEAM_D->leave_team(me)) {
        msg_send(me, HIR "你当前不在队伍中。" NOR "\n", response_context);
        return 1;
    }

    if (!is_xk_client(me))
        msg_send(me, HIG "你离开了队伍。" NOR "\n", response_context);
    emit_team_status_ui(me, response_context);
    return 1;
}

/**
 * 解散队伍
 */
private int do_team_dismiss(object me, mapping response_context)
{
    object *members;
    object member;

    members = TEAM_D->query_team(me);
    if (!TEAM_D->dismiss_team(me)) {
        if (!sizeof(members))
            msg_send(me, HIR "你当前没有队伍可解散。" NOR "\n", response_context);
        else
            msg_send(me, HIR "只有队长才能解散队伍。" NOR "\n", response_context);
        return 1;
    }

    foreach (member in members) {
        if (!objectp(member) || member == me)
            continue;
        msg_send(member, HIY + get_object_display_name(me, "有人") + " 解散了队伍。" NOR "\n");
    }

    if (!is_xk_client(me))
        msg_send(me, HIG "队伍已解散。" NOR "\n", response_context);
    emit_team_status_ui(me, response_context);
    return 1;
}

/**
 * 命令主入口
 */
int main(object me, string arg)
{
    mapping parsed;
    mapping response_context;
    string action, target;

    if (!me) return 0;
    response_context = CLIENT_SYNC_D->capture_command_response_context(me);

    if (!arg) {
        emit_team_status_ui(me, response_context);
        return 1;
    }

    if (arg == "with-select")
        return show_team_invite_candidates(me, response_context);

    if (arg == "kick-select")
        return show_team_kick_candidates(me, response_context);

    parsed = parse_command_args(arg, "%s %s");
    if (parsed["valid"]) {
        action = parsed["args"][0];
        target = parsed["args"][1];

        switch (action) {
            case "with":
                return do_team_with(me, target, response_context);
            case "kick":
                return do_team_kick(me, target, response_context);
            default:
                msg_send(me, HIR "未知的子命令。" NOR "\n", response_context);
                return 1;
        }
    }

    if (arg == "dismiss") {
        return do_team_dismiss(me, response_context);
    }

    if (arg == "leave") {
        return do_team_leave(me, response_context);
    }

    if (arg == "accept") {
        return do_team_accept(me, response_context);
    }

    if (arg == "reject") {
        return do_team_reject(me, response_context);
    }

    emit_team_status_ui(me, response_context);
    return 1;
}

/**
 * 帮助信息
 */
int help(object me)
{
    msg_send(me, @HELP
命令：team（组队）

创建和管理队伍。

用法：
  team                    显示队伍状态
  team with <玩家>        向玩家发出组队邀请
  team accept             接受当前组队邀请
  team reject             拒绝当前组队邀请
  team kick <玩家>        踢出队员（队长专用）
  team dismiss            解散队伍（队长专用）
  team leave              离开队伍

组队优势：
  - 战斗经验共享
  - 可以互相救助
  - 团队配合更高效

注意：
  - 队伍最多5人
  - 队长可以踢出队员
  - 解散后所有队员离开
  - 当前 team with 会先发邀请，由对方用 accept / reject 处理
  - 副本进行中不能邀请、接受或拒绝队伍变更

相关命令：
  follow  - 跟随玩家
  who     - 查看在线玩家

HELP
    );
    return 1;
}
