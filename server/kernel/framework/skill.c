/**
 * @file    skill.c
 * @brief   技能系统框架
 * @author  XiaKeXing Development Team
 * @date    2026-03-20
 * @version 1.0.1
 *
 * @par Description:
 * 提供技能数据查询、技能等级提升和技能加成计算基础能力。
 */

#pragma strict_types
#pragma save_types

#include <system/globals.h>
#include <system/services.h>

mixed query(string path);
int set(string path, mixed value);
int delete_temp(string path);

/**
 * @brief 查询技能等级
 * @param skill 技能名称
 * @return 技能等级
 */
int query_skill(string skill)
{
    if (!stringp(skill) || skill == "")
        return 0;

    return SKILL_D->query_player_skill_level(this_object(), skill);
}

/**
 * @brief 设置技能等级
 * @param skill 技能名称
 * @param level 技能等级
 */
void set_skill(string skill, int level)
{
    mapping skill_data;
    int cap;

    if (!stringp(skill) || skill == "")
        return;

    if (level < 0)
        level = 0;
    cap = SKILL_D->query_player_skill_cap(this_object(), skill);
    if (cap > 0 && level > cap)
        level = cap;

    skill_data = SKILL_D->get_player_skill_data(this_object(), skill);
    if (!mapp(skill_data))
        skill_data = ([ "level": 0, "exp": 0 ]);
    skill_data["level"] = level;
    set("skills/" + skill, skill_data);
    SKILL_D->invalidate_player_skill_projection(this_object(), skill);
}

/**
 * @brief 提升技能
 * @param skill 技能名称
 * @param amount 提升量
 */
void improve_skill(string skill, int amount)
{
    int current;

    if (!stringp(skill) || skill == "" || amount <= 0)
        return;

    current = query_skill(skill);
    set_skill(skill, current + amount);
}

/**
 * @brief 检查是否拥有某技能
 * @param skill 技能名称
 * @return 1有, 0无
 */
int has_skill(string skill)
{
    return (query_skill(skill) > 0);
}

/**
 * @brief 获取所有技能
 * @return 技能映射表 ([技能名:等级])
 */
mapping query_all_skills()
{
    mapping skills;

    skills = SKILL_D->query_player_skills(this_object());
    if (!mapp(skills))
        return ([]);
    return skills;
}

/**
 * @brief 计算技能加成
 * @param base_value 基础值
 * @param skill 技能名称
 * @return 加成后的值
 */
int apply_skill_bonus(int base_value, string skill)
{
    int level;

    if (!stringp(skill))
        return base_value;

    level = query_skill(skill);
    if (level <= 0)
        return base_value;

    return base_value * (100 + level * 2) / 100;
}

/**
 * @brief 获取技能名称列表
 * @return 技能名称数组
 */
string *query_skill_names()
{
    mapping skills;

    skills = query_all_skills();
    return keys(skills);
}
