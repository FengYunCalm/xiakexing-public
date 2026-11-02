/**
 * @file    guild_skill_d.c
 * @brief   帮派技能守护进程,从配置加载帮派技能定义。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

nosave mapping _guild_skill_defs;

private mapping query_guild_skill_config()
{
    mapping config;

    config = CONFIG_D->query_full_config("guild_skill");
    return mapp(config) ? config : ([]);
}

private int validate_guild_skill_def(mapping skill_def)
{
    mixed teach_skills;

    if (!mapp(skill_def))
        return 0;
    teach_skills = skill_def["teach_skills"];
    if (!arrayp(teach_skills) || sizeof(teach_skills) == 0)
        return 0;

    return 1;
}

private int load_guild_skill_config()
{
    mapping config;
    mapping guilds;
    int invalid;
    mapping guild_def;
    mapping skills;
    mapping skill_def;
    mapping normalized;
    string guild_id;
    string skill_id;

    config = query_guild_skill_config();
    if (!mapp(config["guilds"]))
        return 0;
    guilds = config["guilds"];
    normalized = ([]);
    foreach (guild_id, guild_def in guilds) {
        if (!stringp(guild_id) || guild_id == "" || !mapp(guild_def)) {
            invalid = 1;
            continue;
        }

        if (!mapp(guild_def["skills"])) {
            invalid = 1;
            continue;
        }
        skills = guild_def["skills"];
        normalized[guild_id] = ([ "skills": ([ ]) ]);
        foreach (skill_id, skill_def in skills) {
            if (!stringp(skill_id) || skill_id == "")
                continue;
            if (!validate_guild_skill_def(skill_def)) {
                invalid = 1;
                continue;
            }

            normalized[guild_id]["skills"][skill_id] = copy(skill_def);
            normalized[guild_id]["skills"][skill_id]["id"] = skill_id;
        }
    }

    if (invalid)
        return 0;
    _guild_skill_defs = normalized;
    return 1;
}

void on_config_reloaded(string key, string config_name)
{
    if (key != "guild_skill" && config_name != "guild_skill")
        return;

    if (!load_guild_skill_config())
        error("GUILD_SKILL_CONFIG_INVALID");
}

void on_config_reload_rollback(string key, string config_name)
{
    if (key != "guild_skill" && config_name != "guild_skill")
        return;

    if (!load_guild_skill_config())
        error("GUILD_SKILL_CONFIG_INVALID");
}

void create()
{
    ::create();
    if (!mapp(_guild_skill_defs))
        _guild_skill_defs = ([]);

    load_guild_skill_config();
    CONFIG_D->compat_register_config_callback("guild_skill", "on_config_reloaded", this_object());
}

mapping query_all_guild_skill_defs()
{
    return copy(_guild_skill_defs || ([]));
}

mapping query_guild_skill_catalog(string guild_id)
{
    mapping common_def;
    mapping guild_def;
    mapping skills;
    mapping merged;

    merged = ([]);
    common_def = _guild_skill_defs["*"];
    if (mapp(common_def) && mapp(common_def["skills"]))
        merged += copy(common_def["skills"]);

    if (!stringp(guild_id) || guild_id == "")
        return merged;

    guild_def = _guild_skill_defs[guild_id];
    skills = mapp(guild_def) && mapp(guild_def["skills"]) ? copy(guild_def["skills"]) : ([]);
    if (mapp(skills))
        merged += skills;

    return merged;
}

mapping query_guild_skill_definition(string guild_id, string guild_skill_id)
{
    mapping skills;

    if (!stringp(guild_id) || guild_id == "" || !stringp(guild_skill_id) || guild_skill_id == "")
        return 0;

    skills = query_guild_skill_catalog(guild_id);
    return mapp(skills[guild_skill_id]) ? copy(skills[guild_skill_id]) : 0;
}

mapping resolve_guild_skill_source(string guild_id, string guild_skill_id)
{
    mapping skill_def;

    skill_def = query_guild_skill_definition(guild_id, guild_skill_id);
    if (!mapp(skill_def))
        return 0;

    return ([
        "guild_id": guild_id,
        "guild_skill_id": guild_skill_id,
        "teach_skills": copy(skill_def["teach_skills"] || ({ })),
        "required_rank_keys": copy(skill_def["required_rank_keys"] || ({ })),
        "required_contribution": intp(skill_def["required_contribution"]) ? skill_def["required_contribution"] : 0,
        "required_skills": copy(skill_def["required_skills"] || ([])),
        "max_level": intp(skill_def["max_level"]) ? skill_def["max_level"] : 0,
        "skill_type": stringp(skill_def["skill_type"]) ? skill_def["skill_type"] : "basic",
        "name": skill_def["name"] || guild_skill_id,
        "description": skill_def["description"] || "",
    ]);
}
