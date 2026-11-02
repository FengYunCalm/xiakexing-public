/**
 * @file    skill_source_registry_d.c
 * @brief   技能来源注册表,维护来源类型、练习来源与导师技能索引。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

nosave mapping _sources;
/* L-153：来源加载期的技能引用校验结果（内容发布/运维入口可读）。 */
nosave mapping _source_skill_validation;
nosave mapping _practice_sources_by_skill;
nosave mapping _source_types_by_skill;
nosave mapping _teacher_skills_by_provider;
nosave mapping _teacher_source_by_provider_skill;
nosave mapping _teacher_sources_by_skill;
nosave int _source_config_generation;

private void rebuild_source_indexes();

private mapping query_source_config()
{
    mapping config;

    config = CONFIG_D->query_full_config("skill_catalog");
    return mapp(config) ? config : ([]);
}

private string *coerce_content_string_array(mixed value)
{
    string *result;
    mixed entry;

    result = ({ });
    if (stringp(value) && value != "")
        return ({ value });
    if (!arrayp(value))
        return result;
    foreach (entry in value) {
        if (stringp(entry) && entry != "")
            result += ({ entry });
    }
    return result;
}

private string query_provider_id_from_path(string provider_path)
{
    string *parts;

    if (!stringp(provider_path) || provider_path == "")
        return "";
    if (strsrch(provider_path, "#") != -1)
        provider_path = explode(provider_path, "#")[0];
    parts = explode(provider_path, "/");
    return sizeof(parts) > 0 ? parts[<1] : "";
}

private mapping query_content_teacher_sources()
{
    mapping content_teachers;
    mapping npc_templates;
    mapping teacher;
    mapping template;
    mapping limits;
    mapping source;
    mapping sources;
    string teacher_id;
    string source_id;
    string provider_id;
    string provider_path;
    string template_id;
    string skill_id;
    string *teach_skills;
    int max_skill;

    content_teachers = CONFIG_D->query_config("content", "skill_teachers");
    npc_templates = CONFIG_D->query_config("content", "npc_templates");
    sources = ([ ]);
    if (!mapp(content_teachers))
        return sources;

    foreach (teacher_id, teacher in content_teachers) {
        if (!mapp(teacher))
            continue;
        template_id = teacher["teacher_template_id"] || "";
        template = mapp(npc_templates) ? npc_templates[template_id] : 0;
        provider_path = teacher["provider_path"] || (mapp(template) ? (template["path"] || "") : "");
        provider_id = teacher["provider_id"] || query_provider_id_from_path(provider_path);
        if (provider_path == "" || provider_id == "")
            continue;
        teach_skills = coerce_content_string_array(teacher["teach_skills"]);
        limits = mapp(teacher["teach_skill_limits"]) ? teacher["teach_skill_limits"] : ([ ]);
        foreach (skill_id in teach_skills) {
            if (!stringp(skill_id) || skill_id == "")
                continue;
            source_id = "content_" + teacher_id + "_" + skill_id;
            max_skill = intp(limits[skill_id]) && limits[skill_id] > 0
                ? limits[skill_id]
                : (teacher["max_skill"] || teacher["max_level"] || 0);
            source = ([
                "id": source_id,
                "source_type": "teacher",
                "provider_id": provider_id,
                "provider_path": provider_path,
                "organization_type": teacher["organization_type"] || "none",
                "allowed_guilds": teacher["allowed_guilds"] || ({ "*" }),
                "allowed_sects": teacher["allowed_sects"] || ({ "*" }),
                "required_rank": teacher["required_rank"] || 0,
                "required_rank_key": teacher["required_rank_key"] || "",
                "required_rank_keys": teacher["required_rank_keys"] || ({ }),
                "required_contribution": teacher["required_contribution"] || 0,
                "required_skills": teacher["required_skills"] || ([ ]),
                "required_quests": teacher["required_quests"] || ({ }),
                "required_chests": teacher["required_chests"] || ({ }),
                "trial_skill_limit": teacher["trial_skill_limit"] || 0,
                "trial_bypass_requirements": teacher["trial_bypass_requirements"] ? 1 : 0,
                "trial_cost_policy": teacher["trial_cost_policy"] || "",
                "cost_kind": teacher["cost_kind"] || "potential",
                "cost_policy": teacher["cost_policy"] || "progression_default",
                "cost_amount": teacher["cost_amount"] || 0,
                "teach_skills": ({ skill_id }),
            ]);
            if (max_skill > 0)
                source["max_skill"] = max_skill;
            sources[source_id] = source;
        }
    }

    return sources;
}

/* L-153：来源只能引用技能目录里存在的技能——报价（can_learn）与实做
 * （learn_skill）必须同源。引用未定义技能的来源在加载期剔除并留证据，
 * 不再出现“报价可学、实做失败”。 */
private string *query_source_skill_refs(mapping source)
{
    string *refs;
    string skill_id;

    refs = coerce_content_string_array(source["teach_skills"]);
    if (stringp(source["guild_skill_id"]) && source["guild_skill_id"] != "")
        refs += ({ source["guild_skill_id"] });
    if (stringp(source["sect_skill_id"]) && source["sect_skill_id"] != "")
        refs += ({ source["sect_skill_id"] });
    if (source["source_type"] == "practice"
        && stringp(source["skill_id"]) && source["skill_id"] != "")
        refs += ({ source["skill_id"] });
    return refs;
}

private mapping validate_source_skills(mapping sources)
{
    mapping kept;
    mapping rejected;
    mapping source;
    string *refs;
    string source_id;
    string skill_id;

    kept = ([]);
    rejected = ([]);
    /* L-153：技能目录不可信（未加载/读取失败）时不据此剔除任何来源——
     * 与 L-67 的分态原则一致，避免内容未就绪时把所有来源判为未定义。 */
    if (!SKILL_D->is_skill_catalog_trusted())
        return ([ "ok": 1, "rejected": rejected,
            "accepted": sizeof(sources), "sources": sources,
            "catalog_trusted": 0 ]);
    foreach (source_id, source in sources || ([])) {
        if (!mapp(source)) {
            rejected[source_id] = ([ "reason": "invalid_source" ]);
            continue;
        }
        refs = query_source_skill_refs(source);
        foreach (skill_id in refs) {
            if (!SKILL_D->skill_exists(skill_id)) {
                rejected[source_id] = ([ "reason": "undefined_skill",
                    "skill_id": skill_id ]);
                break;
            }
        }
        if (!undefinedp(rejected[source_id]))
            continue;
        kept[source_id] = source;
    }
    if (sizeof(rejected) > 0)
        log_file("system/skill", sprintf(
            "[%s] skill sources rejected for undefined skills: %O\n",
            ctime(time()), rejected));
    return ([ "ok": sizeof(rejected) == 0, "rejected": rejected,
        "accepted": sizeof(kept), "sources": kept, "catalog_trusted": 1 ]);
}

/* L-153：内容/运维校验入口（只读），返回被拒来源与原因。 */
mapping query_source_validation_report()
{
    if (!mapp(_source_skill_validation))
        return ([ "ok": 1, "rejected": ([]), "accepted": 0,
            "catalog_trusted": 0 ]);
    return ([ "ok": _source_skill_validation["ok"],
        "rejected": copy(_source_skill_validation["rejected"] || ([])),
        "accepted": _source_skill_validation["accepted"],
        "catalog_trusted": _source_skill_validation["catalog_trusted"] ? 1 : 0 ]);
}

private void load_source_config()
{
    mapping config;
    mapping sources;
    mapping validation;

    config = query_source_config();
    sources = mapp(config["sources"]) ? copy(config["sources"]) : ([]);
    sources += query_content_teacher_sources();
    validation = validate_source_skills(sources);
    _sources = mapp(validation["sources"]) ? validation["sources"] : ([]);
    _source_skill_validation = ([ "ok": validation["ok"],
        "rejected": validation["rejected"], "accepted": validation["accepted"],
        "catalog_trusted": validation["catalog_trusted"] ]);
    rebuild_source_indexes();
    _source_config_generation++;
}

void on_config_reloaded(string key, string config_name)
{
    if (key != "skill_catalog" && config_name != "skill_catalog"
        && key != "content" && config_name != "content")
        return;

    load_source_config();
}

void on_config_reload_rollback(string key, string config_name)
{
    if (key != "skill_catalog" && config_name != "skill_catalog"
        && key != "content" && config_name != "content")
        return;

    load_source_config();
    if (_source_config_generation > 1)
        _source_config_generation -= 2;
    if (_source_config_generation < 1)
        _source_config_generation = 1;
}

private string *dedupe_strings(mixed *values)
{
    string *result;
    string value;

    if (!arrayp(values) || sizeof(values) == 0)
        return ({ });

    result = ({ });
    foreach (value in values) {
        if (!stringp(value) || value == "")
            continue;
        if (member_array(value, result) != -1)
            continue;
        result += ({ value });
    }

    return result;
}

private void add_source_type_for_skill(string skill_id, string source_type)
{
    if (!stringp(skill_id) || skill_id == "" || !stringp(source_type) || source_type == "")
        return;
    if (!arrayp(_source_types_by_skill[skill_id]))
        _source_types_by_skill[skill_id] = ({ });
    if (member_array(source_type, _source_types_by_skill[skill_id]) == -1)
        _source_types_by_skill[skill_id] += ({ source_type });
}

private void add_teacher_source_for_skill(string skill_id, mapping source)
{
    if (!stringp(skill_id) || skill_id == "" || !mapp(source))
        return;
    if (!arrayp(_teacher_sources_by_skill[skill_id]))
        _teacher_sources_by_skill[skill_id] = ({ });
    _teacher_sources_by_skill[skill_id] += ({ source });
}

private void add_teacher_skill_for_provider(string provider_id, string skill_id)
{
    if (!stringp(provider_id) || provider_id == "" || !stringp(skill_id) || skill_id == "")
        return;
    if (!arrayp(_teacher_skills_by_provider[provider_id]))
        _teacher_skills_by_provider[provider_id] = ({ });
    if (member_array(skill_id, _teacher_skills_by_provider[provider_id]) == -1)
        _teacher_skills_by_provider[provider_id] += ({ skill_id });
}

private void rebuild_source_indexes()
{
    mapping source;
    mixed teach_skills;
    string source_id;
    string skill_id;
    string provider_id;
    string key;

    _practice_sources_by_skill = ([ ]);
    _source_types_by_skill = ([ ]);
    _teacher_skills_by_provider = ([ ]);
    _teacher_source_by_provider_skill = ([ ]);
    _teacher_sources_by_skill = ([ ]);

    foreach (source_id, source in _sources || ([ ])) {
        if (!mapp(source))
            continue;
        switch (source["source_type"]) {
        case "teacher":
            provider_id = source["provider_id"] || "";
            teach_skills = source["teach_skills"];
            if (!arrayp(teach_skills))
                break;
            foreach (skill_id in teach_skills) {
                if (!stringp(skill_id) || skill_id == "")
                    continue;
                add_source_type_for_skill(skill_id, "teacher");
                add_teacher_source_for_skill(skill_id, source);
                add_teacher_skill_for_provider(provider_id, skill_id);
                if (provider_id != "") {
                    key = provider_id + ":" + skill_id;
                    if (!mapp(_teacher_source_by_provider_skill[key]))
                        _teacher_source_by_provider_skill[key] = source;
                }
            }
            break;
        case "manual":
            teach_skills = source["teach_skills"];
            if (!arrayp(teach_skills))
                break;
            foreach (skill_id in teach_skills)
                add_source_type_for_skill(skill_id, "manual");
            break;
        case "practice":
            skill_id = source["skill_id"] || "";
            if (skill_id == "")
                break;
            add_source_type_for_skill(skill_id, "practice");
            if (!mapp(_practice_sources_by_skill[skill_id]))
                _practice_sources_by_skill[skill_id] = source;
            break;
        }
    }
}

void create()
{
    ::create();

    if (!mapp(_sources))
        _sources = ([]);
    if (!mapp(_practice_sources_by_skill))
        _practice_sources_by_skill = ([]);
    if (!mapp(_source_types_by_skill))
        _source_types_by_skill = ([]);
    if (!mapp(_teacher_skills_by_provider))
        _teacher_skills_by_provider = ([]);
    if (!mapp(_teacher_source_by_provider_skill))
        _teacher_source_by_provider_skill = ([]);
    if (!mapp(_teacher_sources_by_skill))
        _teacher_sources_by_skill = ([]);

    load_source_config();
    CONFIG_D->compat_register_config_callback("skill_catalog", "on_config_reloaded", this_object());
    CONFIG_D->compat_register_config_callback("content", "on_config_reloaded", this_object());
}

mapping query_source(string source_id)
{
    if (!stringp(source_id) || source_id == "")
        return 0;

    return mapp(_sources[source_id]) ? copy(_sources[source_id]) : 0;
}

mapping query_all_sources()
{
    return copy(_sources || ([]));
}

int query_source_config_generation()
{
    return _source_config_generation > 0 ? _source_config_generation : 1;
}

mapping query_teacher_source(string provider_id, string skill_id)
{
    string key;

    if (!stringp(provider_id) || provider_id == "" || !stringp(skill_id) || skill_id == "")
        return 0;

    key = provider_id + ":" + skill_id;
    return mapp(_teacher_source_by_provider_skill[key]) ? copy(_teacher_source_by_provider_skill[key]) : 0;
}

mapping query_any_teacher_source(string skill_id)
{
    mixed *sources;

    if (!stringp(skill_id) || skill_id == "")
        return 0;

    sources = _teacher_sources_by_skill[skill_id];
    return arrayp(sources) && sizeof(sources) > 0 && mapp(sources[0]) ? copy(sources[0]) : 0;
}

string *query_teacher_teach_skills(string provider_id)
{
    if (!stringp(provider_id) || provider_id == "")
        return ({ });
    return arrayp(_teacher_skills_by_provider[provider_id]) ? copy(_teacher_skills_by_provider[provider_id]) : ({ });
}

mapping query_manual_source(string manual_id)
{
    mapping source;
    string source_id;

    if (!stringp(manual_id) || manual_id == "")
        return 0;

    foreach (source_id in keys(_sources || ([]))) {
        source = _sources[source_id];
        if (!mapp(source) || source["source_type"] != "manual")
            continue;
        if (source["manual_id"] == manual_id)
            return copy(source);
    }

    return 0;
}

mapping query_practice_source(string skill_id)
{
    if (!stringp(skill_id) || skill_id == "")
        return 0;
    return mapp(_practice_sources_by_skill[skill_id]) ? copy(_practice_sources_by_skill[skill_id]) : 0;
}

mixed *query_skill_source_types(string skill_id)
{
    mapping skill_def;
    string *source_types;
    mixed configured_types;
    mixed source_type;

    if (!stringp(skill_id) || skill_id == "")
        return ({ });

    source_types = ({ });
    skill_def = SKILL_D->get_skill_definition(skill_id);
    configured_types = mapp(skill_def) ? skill_def["source_types"] : 0;
    if (arrayp(configured_types)) {
        foreach (source_type in configured_types) {
            if (stringp(source_type) && source_type != "")
                source_types += ({ source_type });
        }
    } else if (mapp(skill_def) && stringp(skill_def["numeric_category"]) && skill_def["numeric_category"] != "") {
        source_types += ({ skill_def["numeric_category"] });
    }

    if (arrayp(_source_types_by_skill[skill_id]))
        source_types += _source_types_by_skill[skill_id];

    return dedupe_strings(source_types);
}
