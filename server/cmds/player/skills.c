/**
 * @file    skills.c
 * @brief   技能命令 - 显示和管理技能



 *
 * @par Description:
 * 显示玩家已学习的技能，显示技能等级和熟练度。
 * 发送XK协议技能列表(SKLS)和技能详情(SKLI)，集成技能系统守护进程。
 *
 * @par Usage:
 *   skills              - 查看自己的技能
 *   skills <技能名>     - 查看特定技能详情
 *   skills <玩家>       - 查看他人技能（巫师）
 *
 */

#pragma strict_types

#include <system/ansi.h>
#include <game/command.h>
#include <system/services.h>
#include <framework/message_facade.h>

inherit "/lib/std/command_utils";
inherit "/lib/framework/network";

private mapping query_visible_life_skills(object target);
private string resolve_life_skill_id(string arg);
private void show_life_skill_info(object me, object target, string skill_id);

/**
 * 获取技能等级名称
 */
private string get_skill_level_name(int level)
{
    if (level >= 1000) return HIY "宗师" NOR;
    if (level >= 700) return HIW "大成" NOR;
    /* L-208：原 400 档与 150 档输出完全相同（死分支）；重复档已删除。
     * 是否需要 150–699 之间的独立档位文案属产品口径，另议。 */
    if (level >= 150) return HIR "大师" NOR;
    if (level >= 100) return MAG "专家" NOR;
    if (level >= 80)  return HIC "精通" NOR;
    if (level >= 60)  return HIG "熟练" NOR;
    if (level >= 40)  return HIY "入门" NOR;
    if (level >= 20)  return YEL "初学" NOR;
    return CYN "粗通" NOR;
}

private mapping query_visible_life_skills(object target)
{
    mapping result;
    mapping def;
    string skill_id;
    int level;
    int exp;

    result = ([]);
    if (!objectp(target))
        return result;

    foreach (skill_id in LIFE_SKILL_D->query_all_life_skills()) {
        def = LIFE_SKILL_D->query_skill_definition(skill_id);
        level = LIFE_SKILL_D->get_skill_level(target, skill_id);
        exp = target->query("life_skills_exp/" + skill_id) || 0;
        if (level <= 0 && exp <= 0)
            continue;

        result[skill_id] = ([
            "id": skill_id,
            "name": mapp(def) && stringp(def["name"]) ? def["name"] : skill_id,
            "type": mapp(def) && stringp(def["type"]) ? def["type"] : "",
            "level": level,
            "exp": exp,
            "max_level": mapp(def) && intp(def["max_level"]) ? def["max_level"] : 100,
        ]);
    }

    return result;
}

private string resolve_life_skill_id(string arg)
{
    mapping def;
    string skill_id;

    if (!stringp(arg) || arg == "")
        return "";

    foreach (skill_id in LIFE_SKILL_D->query_all_life_skills()) {
        def = LIFE_SKILL_D->query_skill_definition(skill_id);
        if (arg == skill_id)
            return skill_id;
        if (mapp(def) && stringp(def["name"]) && def["name"] == arg)
            return skill_id;
    }

    return "";
}

/**
 * 显示技能列表（文本模式）
 */
private void show_skills(object me, object target)
{
    mapping life_skills;
    mapping skills;
    string result;
    string *skill_names;
    string *life_skill_ids;
    mapping life_info;
    string target_name;

    skills = SKILL_D->query_player_skills(target);
    life_skills = query_visible_life_skills(target);
    target_name = target->name();
    if (!stringp(target_name) || target_name == "")
        target_name = target->query("name") || "未知";

    result = sprintf("%s的技能：\n", target_name);

    if (!mapp(skills) || sizeof(skills) == 0) {
        result += "暂无战斗技能。\n";
    } else {
        skill_names = keys(skills);
        foreach (string skill_name in skill_names) {
            int level = intp(skills[skill_name]["level"]) ? skills[skill_name]["level"] : 0;
            string level_name = get_skill_level_name(level);

            result += sprintf("- %s %d级 %s\n",
                skill_name, level, level_name);
        }
    }

    result += "\n生活技能：\n";
    if (!mapp(life_skills) || sizeof(life_skills) == 0) {
        result += "暂无生活技能进展。\n";
    } else {
        life_skill_ids = sort_array(keys(life_skills), 1);
        foreach (string life_skill_id in life_skill_ids) {
            life_info = life_skills[life_skill_id];
            result += sprintf("- %s %d级 经验 %d/10\n",
                life_info["name"],
                life_info["level"],
                life_info["exp"]);
        }
    }

    msg_send(me, result);
}

/**
 * 显示技能详情（文本模式）
 */
private void show_skill_info(object me, string skill_id)
{
    int level;
    string result;

    /* L-208：删除未使用的技能表读取（明细只消费单技能事实）。 */
    level = SKILL_D->query_player_skill_level(me, skill_id);

    result = sprintf("技能：%s\n等级：%d\n评价：%s\n",
        skill_id,
        level,
        get_skill_level_name(level));

    msg_send(me, result);
}

private void show_life_skill_info(object me, object target, string skill_id)
{
    mapping def;
    int level;
    int exp;
    int next_gap;
    string result;
    string skill_name;
    string skill_type;

    def = LIFE_SKILL_D->query_skill_definition(skill_id);
    level = LIFE_SKILL_D->get_skill_level(target, skill_id);
    /* L-208/L-66：经验与阈值都取生活技能 owner，命令层不再写死每级 10。 */
    exp = LIFE_SKILL_D->get_skill_exp(target, skill_id);
    skill_name = mapp(def) && stringp(def["name"]) ? def["name"] : skill_id;
    skill_type = mapp(def) && stringp(def["type"]) ? def["type"] : "未知";
    next_gap = LIFE_SKILL_D->query_skill_level_exp_threshold() - exp;
    if (next_gap < 0)
        next_gap = 0;

    result = sprintf("生活技能：%s\n标识：%s\n类型：%s\n等级：%d\n当前经验：%d\n距离下一级：%d\n评价：%s\n",
        skill_name,
        skill_id,
        skill_type,
        level,
        exp,
        next_gap,
        get_skill_level_name(level));

    msg_send(me, result);
}

/**
 * 命令主入口
 */
int main(object me, string arg)
{
    object target;
    string life_skill_id;
    int use_xk = 1;  /* 默认启用XK协议 */
    int xk_client;
    mapping response_context;

    if (!me) return 0;
    response_context = CLIENT_SYNC_D->capture_command_response_context(me);
    xk_client = use_xk && is_xk_client(me);

    /* 无参数：发送技能列表 */
    if (!arg || arg == "") {
        /* 发送XK协议技能列表 */
        if (xk_client) {
            send_xk_skill_list(me, response_context);
            send_xk_quick_cmds(me,
                ({ (["label": "查看状态", "cmd": "score"]),
                   (["label": "查看装备", "cmd": "equip"]) }),
                1, response_context);
        } else {
            show_skills(me, me);
        }

        return 1;
    }

    life_skill_id = resolve_life_skill_id(arg);
    if (life_skill_id != "") {
        if (xk_client)
            send_xk_skill_info(me, life_skill_id, response_context);
        else
            show_life_skill_info(me, me, life_skill_id);
        return 1;
    }

    /* 检查是否是查看特定技能 */
    if (member_array(arg, keys(me->query("skills") || ([]))) != -1) {
        /* 发送XK协议技能详情 */
        if (xk_client) {
            send_xk_skill_info(me, arg, response_context);
        } else {
            show_skill_info(me, arg);
        }
        return 1;
    }

    /* 巫师查看其他玩家 */
    if (wizardp(me)) {
        target = find_player_online(arg);
        if (!target) {
            target = present(arg, environment(me));
        }

        if (!target) {
            msg_send(me, HIR "没有找到 " + arg + "。" NOR + "\n");
            return 1;
        }

        show_skills(me, target);
        return 1;
    }

    /* 未知参数，显示帮助 */
    msg_send(me, "没有找到这个技能。先用 skills 看看列表。\n");
    return 1;
}

/**
 * 帮助信息
 */
int help(object me)
{
    msg_send(me,
        "命令：skills\n"
        "用法：skills、skills <技能名>、skills <生活技能名>\n"
        "说明：直接输入 skills 查看全部技能；输入技能名可看详情。巫师也可查看他人技能。\n");
    return 1;
}
