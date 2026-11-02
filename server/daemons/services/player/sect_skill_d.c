/**
 * @file    sect_skill_d.c
 * @brief   门派技能守护进程,从配置加载门派技能定义。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

nosave mapping _sect_skill_defs;

private mapping query_sect_skill_config()
{
    mapping config;

    config = CONFIG_D->query_full_config("sect_skill");
    return mapp(config) ? config : ([]);
}

private int validate_sect_skill_def(mapping skill_def)
{
    mixed teach_skills;

    if (!mapp(skill_def))
        return 0;
    teach_skills = skill_def["teach_skills"];
    if (!arrayp(teach_skills) || sizeof(teach_skills) == 0)
        return 0;

    return 1;
}

private void load_sect_skill_config()
{
    mapping config;
    mapping normalized;
    mapping sect_def;
    mapping skill_def;
    mapping skills;
    mapping sects;
    string sect_id;
    string skill_id;

    config = query_sect_skill_config();
    sects = mapp(config["sects"]) ? config["sects"] : ([]);
    normalized = ([]);
    foreach (sect_id, sect_def in sects) {
        if (!stringp(sect_id) || sect_id == "" || !mapp(sect_def))
            continue;

        skills = mapp(sect_def["skills"]) ? sect_def["skills"] : ([]);
        normalized[sect_id] = ([ "skills": ([ ]) ]);
        foreach (skill_id, skill_def in skills) {
            if (!stringp(skill_id) || skill_id == "")
                continue;
            if (!validate_sect_skill_def(skill_def))
                continue;

            normalized[sect_id]["skills"][skill_id] = copy(skill_def);
            normalized[sect_id]["skills"][skill_id]["id"] = skill_id;
        }
    }

    _sect_skill_defs = normalized;
}

void on_config_reloaded(string key, string config_name)
{
    if (key != "sect_skill" && config_name != "sect_skill")
        return;

    load_sect_skill_config();
}

void on_config_reload_rollback(string key, string config_name)
{
    if (key != "sect_skill" && config_name != "sect_skill")
        return;

    load_sect_skill_config();
}

void create()
{
    ::create();
    if (!mapp(_sect_skill_defs))
        _sect_skill_defs = ([]);

    load_sect_skill_config();
    CONFIG_D->compat_register_config_callback("sect_skill", "on_config_reloaded", this_object());
}

mapping query_all_sect_skill_defs()
{
    return copy(_sect_skill_defs || ([]));
}

mapping query_sect_skill_catalog(string sect_id)
{
    mapping sect_def;

    if (!stringp(sect_id) || sect_id == "")
        return ([]);

    sect_def = _sect_skill_defs[sect_id];
    return mapp(sect_def) && mapp(sect_def["skills"]) ? copy(sect_def["skills"]) : ([]);
}

mapping query_sect_skill_definition(string sect_id, string sect_skill_id)
{
    mapping skills;

    if (!stringp(sect_id) || sect_id == "" || !stringp(sect_skill_id) || sect_skill_id == "")
        return 0;

    skills = query_sect_skill_catalog(sect_id);
    return mapp(skills[sect_skill_id]) ? copy(skills[sect_skill_id]) : 0;
}

mapping resolve_sect_skill_source(string sect_id, string sect_skill_id)
{
    mapping skill_def;

    skill_def = query_sect_skill_definition(sect_id, sect_skill_id);
    if (!mapp(skill_def))
        return 0;

    return ([
        "sect_id": sect_id,
        "sect_skill_id": sect_skill_id,
        "teach_skills": copy(skill_def["teach_skills"] || ({ })),
        "required_rank": intp(skill_def["required_rank"]) ? skill_def["required_rank"] : 0,
        "required_contribution": intp(skill_def["required_contribution"]) ? skill_def["required_contribution"] : 0,
        "required_skills": copy(skill_def["required_skills"] || ([])),
        "max_level": intp(skill_def["max_level"]) ? skill_def["max_level"] : 0,
        "skill_type": stringp(skill_def["skill_type"]) ? skill_def["skill_type"] : "basic",
        "name": skill_def["name"] || sect_skill_id,
        "description": skill_def["description"] || "",
    ]);
}
