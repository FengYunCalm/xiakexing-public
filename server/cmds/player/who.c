/**
 * @file    who.c
 * @brief   在线玩家命令



 *
 * @par Description:
 * 显示当前在线的所有玩家，支持分页显示并发送 XK 协议消息。
 */

#pragma strict_types

#include <system/ansi.h>
#include <game/command.h>
#include <system/globals.h>
#include <system/constants.h>
#include <system/services.h>

#include <framework/message_facade.h>

inherit "/lib/framework/network";

#define ROSTER_DIGEST_REPEAT 1

private int get_page_size();
private int parse_page(string arg, int total, int page_size);
private string build_player_list(mixed *entries, int start, int end);
private string build_dialog_player_list(mixed *entries, int start, int end);
private string build_header(mapping roster_snapshot);
private string build_footer(int page, int total, int page_size);
private string build_dialog_content(mapping roster_snapshot, mixed *entries,
    int start, int end, int page, int page_size);
private mapping build_who_dialog_extra(mapping *actions);
private mapping *build_who_dialog_actions(int page, int end, int total, int page_size);
private void emit_who_dialog(object me, mapping roster_snapshot, mixed *entries,
    int start, int end, int page, int total, int page_size);
private void send_quick_commands(object me, int page, int end, int total,
    int page_size, mapping response_context);

private int get_page_size()
{
    return CONFIG_D->query_who_page_size();
}

private string format_entry_brief_line(mapping entry)
{
    string name;
    string id;
    int progression_rank;

    if (!mapp(entry))
        return "无名(未知) 档位1";
    name = stringp(entry["name"]) && entry["name"] != "" ? entry["name"] : "无名";
    id = stringp(entry["id"]) && entry["id"] != "" ? entry["id"] : "未知";
    progression_rank = intp(entry["progression_rank"]) ? entry["progression_rank"] : 1;
    return sprintf("%s(%s) 档位%d", name, id, progression_rank);
}

private string format_tagged_entry_brief_line(string tag, mapping entry)
{
    return (stringp(tag) ? tag : "") + format_entry_brief_line(entry);
}

private string build_dialog_player_list(mixed *entries, int start, int end)
{
    string result;
    int i;

    result = "";
    for (i = start; i < end; i++) {
        mapping entry;
        string line;

        entry = entries[i];
        if (!mapp(entry))
            continue;
        line = entry["is_wizard"]
            ? format_tagged_entry_brief_line("【巫师】", entry)
            : format_tagged_entry_brief_line("【玩家】", entry);
        result += sprintf("%d. %s\n", i + 1, line);
    }

    return result;
}

private string build_dialog_content(mapping roster_snapshot, mixed *entries,
    int start, int end, int page, int page_size)
{
    string result;
    int total;
    int wizard_count;
    int player_count;
    int total_pages;

    total = mapp(roster_snapshot) ? (roster_snapshot["total"] || 0) : 0;
    wizard_count = mapp(roster_snapshot) ? (roster_snapshot["wizard_count"] || 0) : 0;
    player_count = mapp(roster_snapshot) ? (roster_snapshot["player_count"] || 0) : 0;
    total_pages = (total + page_size - 1) / page_size;

    result = sprintf("当前在线：%d 人\n", total);
    result += sprintf("巫师：%d  玩家：%d\n", wizard_count, player_count);
    if (total <= 0)
        return result + "\n当前没有在线玩家。";

    result += "\n";
    result += build_dialog_player_list(entries, start, end);
    result += sprintf("\n第 %d/%d 页", page, total_pages);
    return result;
}

private mapping build_who_dialog_extra(mapping *actions)
{
    return DIALOG_PROJECTION_D->build_dialog_extra("who", "who_panel", ([
        "replace_mode": "replace",
        "actions": arrayp(actions) ? actions : ({ }),
    ]));
}

private mapping *build_who_dialog_actions(int page, int end, int total, int page_size)
{
    mapping *actions;

    actions = ({
        (["label":"刷新列表", "cmd":"who" + (page > 1 ? " " + page : "")]),
        (["label":"查看时间", "cmd":"time"]),
        (["label":"服务器状态", "cmd":"status"]),
        (["label":"查看帮助", "cmd":"help who"]),
    });

    if (page > 1)
        actions = ({ (["label":"上一页", "cmd":"who " + (page - 1)]) }) + actions;
    if (end < total)
        actions += ({ (["label":"下一页", "cmd":"who " + (page + 1)]) });

    return actions;
}

private void emit_who_dialog(object me, mapping roster_snapshot, mixed *entries,
    int start, int end, int page, int total, int page_size,
    mapping response_context)
{
    xk_send_dialog(me,
        "在线玩家",
        build_dialog_content(roster_snapshot, entries, start, end, page, page_size),
        build_who_dialog_extra(build_who_dialog_actions(page, end, total, page_size)),
        response_context);
}

/**
 * @brief 命令主入口
 */
int main(object me, string arg)
{
    mapping roster_snapshot;
    mixed *entries;
    string result;
    mapping response_context;
    int total, page, start, end, page_size;

    if (!me) return 0;
    response_context = CLIENT_SYNC_D->capture_command_response_context(me);

    /* 第三个参数是摘要 worker 的 repeat，不是分页大小；分页仍由 page_size 控制。 */
    roster_snapshot = PLAYER_ACTOR_ANALYSIS_D->query_online_player_roster_for(
        me, 1, ROSTER_DIGEST_REPEAT);
    entries = mapp(roster_snapshot) && arrayp(roster_snapshot["entries"])
        ? roster_snapshot["entries"]
        : ({ });
    total = roster_snapshot["total"] || sizeof(entries);
    page_size = get_page_size();

    if (total == 0)
    {
        if (!is_xk_client(me)) {
            msg_send(me,
                HIC "╔════════════════════════════════════════╗" NOR "\n"
                + HIC "║" YEL "        当前没有在线玩家" HIC "               ║" NOR "\n"
                + HIC "╚════════════════════════════════════════╝" NOR "\n");
        }
        emit_who_dialog(me, roster_snapshot, entries, 0, 0, 1, total, page_size,
            response_context);
        return 1;
    }

    page = parse_page(arg, total, page_size);
    start = (page - 1) * page_size;
    end = start + page_size;
    if (end > total) end = total;

    result = build_header(roster_snapshot);
    result += build_player_list(entries, start, end);
    result += build_footer(page, total, page_size);

    if (!is_xk_client(me))
        msg_send(me, result);

    send_quick_commands(me, page, end, total, page_size, response_context);
    emit_who_dialog(me, roster_snapshot, entries, start, end, page, total, page_size,
        response_context);

    return 1;
}

/**
 * @brief 解析页码
 * @param arg 参数
 * @param total 总数
 * @return 页码
 */
private int parse_page(string arg, int total, int page_size)
{
    int page;

    if (!arg || sscanf(arg, "%d", page) != 1)
        return 1;

    if (page < 1 || page_size <= 0
        || page > (total + page_size - 1) / page_size)
        return 1;

    return page;
}

/**
 * @brief 构建玩家列表字符串
 * @param users_list 玩家列表
 * @param start 开始索引
 * @param end 结束索引
 * @param wiz_count 巫师计数指针
 * @param player_count 玩家计数指针
 * @return 列表字符串
 */
private string build_player_list(mixed *entries, int start, int end)
{
    string result = "";
    int i;

    for (i = start; i < end; i++)
    {
        mapping entry = entries[i];
        string line;

        if (!mapp(entry))
            continue;

        if (entry["is_wizard"])
        {
            line = HIW + format_tagged_entry_brief_line("【巫师】", entry) + NOR;
        }
        else
        {
            line = HIC + format_tagged_entry_brief_line("【玩家】", entry) + NOR;
        }

        result += sprintf(HIC "║" NOR " %-38s" HIC "║" NOR "\n", line);
    }

    return result;
}

/**
 * @brief 构建表头
 * @param total 总数
 * @return 表头字符串
 */
private string build_header(mapping roster_snapshot)
{
    string result = "";
    int total;
    int wizard_count;
    int player_count;

    total = mapp(roster_snapshot) ? (roster_snapshot["total"] || 0) : 0;
    wizard_count = mapp(roster_snapshot) ? (roster_snapshot["wizard_count"] || 0) : 0;
    player_count = mapp(roster_snapshot) ? (roster_snapshot["player_count"] || 0) : 0;

    result += HIC "╔════════════════════════════════════════╗" NOR "\n";
    result += sprintf(HIC "║" HIG "           在线玩家列表" HIC "                 ║" NOR "\n");
    result += sprintf(HIC "║" NOR " 当前在线: %d 人" HIC "                        ║" NOR "\n", total);
    result += sprintf(HIC "║" NOR " 巫师: %d  玩家: %d" HIC "                     ║" NOR "\n",
        wizard_count, player_count);
    result += HIC "╠════════════════════════════════════════╣" NOR "\n";

    return result;
}

/**
 * @brief 构建表尾
 * @param page 当前页
 * @param total 总数
 * @return 表尾字符串
 */
private string build_footer(int page, int total, int page_size)
{
    string result = "";
    int total_pages = (total + page_size - 1) / page_size;

    result += HIC "╠════════════════════════════════════════╣" NOR "\n";
    result += sprintf(HIC "║" NOR " 第 %d/%d 页" HIC "                           ║" NOR "\n",
        page, total_pages);
    result += HIC "╚════════════════════════════════════════╝" NOR "\n";

    return result;
}

/**
 * @brief 发送快捷命令
 * @param me 玩家对象
 * @param page 当前页
 * @param end 结束索引
 * @param total 总数
 */
private void send_quick_commands(object me, int page, int end, int total,
    int page_size, mapping response_context)
{
    mapping *cmds = ({});

    if (page > 1 || total > page_size)
    {
        if (page > 1)
            cmds += ({ (["label":"上一页", "cmd":"who " + (page - 1)]) });
        if (end < total)
            cmds += ({ (["label":"下一页", "cmd":"who " + (page + 1)]) });

        send_xk_quick_cmds(me, cmds, 1, response_context);
    }

    send_xk_secondary_cmds(me,
        ({
            (["label":"查看时间", "cmd":"time"]),
            (["label":"服务器状态", "cmd":"status"]),
            (["label":"查看帮助", "cmd":"help who"]),
        }),
        "在线辅助",
        1, response_context);
}

/**
 * @brief 帮助信息
 */
int help(object me)
{
    msg_send(me, @HELP
命令：who（在线玩家）

查看当前在线的玩家列表。

用法：
  who              显示所有在线玩家
  who <页码>       显示指定页

显示内容：
  【巫师】 - 游戏管理员
  【玩家】 - 普通玩家

显示格式：
  名称 (ID) 等级

说明：
  - 巫师会优先显示在列表前面
  - 人数较多时按页显示

示例：
  who              查看第一页
  who 2            查看第二页

相关命令：
  finger  - 查看玩家详细信息
  status  - 查看服务器概览

HELP
    );
    return 1;
}
