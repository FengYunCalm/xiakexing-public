/**
 * @file    skill_source_d.c
 * @brief   技能来源守护进程,导师学习/练习来源的判定与执行真源。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

mapping query_source_organization_status(object player, mapping source);
mapping learn_from_teacher(object player, object teacher, string skill_id);
mapping learn_from_teacher_levels(object player, object teacher, string skill_id, mixed level_count);
string format_teacher_offering_error(string error);
mapping can_learn_from_teacher(object player, object teacher, string skill_id);
mapping can_study_from_manual(object player, object manual);
mapping can_practice_skill(object player, string skill_id);
private int is_onboarding_outer_skill(object player, string skill_id);
private void record_onboarding_training_events(object player, string skill_id);

#define DEFAULT_TEACH_SKILL_LIMIT 20

private string query_teacher_provider_id(object teacher)
{
    string base_path;
    string provider_id;
    string *parts;

    if (!objectp(teacher))
        return "";

    provider_id = teacher->query("provider_id");
    if (stringp(provider_id) && provider_id != "")
        return provider_id;

    base_path = file_name(teacher);
    if (strsrch(base_path, "#") != -1)
        base_path = explode(base_path, "#")[0];

    parts = explode(base_path, "/");
    if (arrayp(parts) && sizeof(parts) > 0)
        return parts[<1];

    return "";
}

private string query_skill_display_name(string skill_id)
{
    mapping def;

    if (!stringp(skill_id) || skill_id == "")
        return "";
    def = SKILL_D->get_skill_definition(skill_id);
    if (mapp(def) && stringp(def["name"]) && def["name"] != "")
        return def["name"];
    return skill_id;
}

private string *query_configured_teacher_skills(object teacher)
{
    string *result;
    mixed skills;
    mixed skill_id;

    if (!objectp(teacher))
        return ({ });
    skills = teacher->query("teach_skills");
    if (stringp(skills) && skills != "")
        return ({ skills });
    if (arrayp(skills)) {
        result = ({ });
        foreach (skill_id in skills) {
            if (stringp(skill_id) && skill_id != "")
                result += ({ skill_id });
        }
        return result;
    }
    return ({ });
}

private string *dedupe_skill_ids(string *skills)
{
    mapping seen;
    string skill_id;

    if (!arrayp(skills) || sizeof(skills) == 0)
        return ({ });
    seen = ([ ]);
    foreach (skill_id in skills) {
        if (stringp(skill_id) && skill_id != "")
            seen[skill_id] = 1;
    }
    return sort_array(keys(seen), 1);
}

private int query_teacher_skill_limit(object teacher, mapping source, string skill_id)
{
    mapping limits;
    int limit;

    if (mapp(source) && intp(source["max_skill"]) && source["max_skill"] > 0)
        return source["max_skill"];
    if (mapp(source) && intp(source["max_level"]) && source["max_level"] > 0)
        return source["max_level"];
    if (objectp(teacher) && function_exists("query_teach_skill_limit", teacher)) {
        limit = teacher->query_teach_skill_limit(skill_id);
        if (limit > 0)
            return limit;
    }
    if (objectp(teacher)) {
        limits = teacher->query("teach_skill_limits");
        if (mapp(limits) && intp(limits[skill_id]) && limits[skill_id] > 0)
            return limits[skill_id];
        limit = teacher->query("teach_skill_max") || 0;
        if (limit > 0)
            return limit;
    }
    return DEFAULT_TEACH_SKILL_LIMIT;
}

/*
 * Trial lessons are a provider policy, not a second skill progression model.
 * They grant the first level while the provider's route gate is still unmet.
 * Once a player already meets that gate, the first formal lesson uses the
 * normal source requirements and costs instead of granting an extra free level.
 */
private mapping apply_teacher_trial_policy(mapping source, int current_level,
    int normal_requirements_met)
{
    mapping trial_source;
    string trial_policy;
    int trial_limit;

    if (!mapp(source))
        return source;
    trial_limit = intp(source["trial_skill_limit"])
        ? source["trial_skill_limit"] : 0;
    if (trial_limit <= 0 || current_level >= trial_limit)
        return source;

    /* A completed route enters paid instruction directly, even at level zero. */
    if (source["trial_bypass_requirements"] && normal_requirements_met)
        return source;

    trial_source = copy(source);
    trial_source["trial_active"] = 1;
    if (trial_source["trial_bypass_requirements"]) {
        trial_source["required_skills"] = ([ ]);
        trial_source["required_quests"] = ({ });
        trial_source["required_chests"] = ({ });
    }
    trial_policy = trial_source["trial_cost_policy"];
    if (!stringp(trial_policy) || trial_policy == "")
        trial_policy = "none";
    trial_source["cost_policy"] = trial_policy;
    if (trial_policy == "none")
        trial_source["cost_kind"] = "none";
    return trial_source;
}

private mapping query_configured_teacher_source(object teacher, string skill_id)
{
    string provider_id;

    if (!objectp(teacher) || !stringp(skill_id) || skill_id == "")
        return 0;
    if (member_array(skill_id, query_configured_teacher_skills(teacher)) == -1)
        return 0;

    provider_id = query_teacher_provider_id(teacher);
    return ([
        "id": "teacher_" + provider_id + "_" + skill_id,
        "source_type": "teacher",
        "provider_id": provider_id,
        "provider_path": base_name(teacher),
        "organization_type": teacher->query("teach_organization_type") || "none",
        "allowed_sects": teacher->query("teach_allowed_sects") || ({ "*" }),
        "allowed_guilds": teacher->query("teach_allowed_guilds") || ({ "*" }),
        "required_rank": teacher->query("teach_required_rank") || 0,
        "required_contribution": teacher->query("teach_required_contribution") || 0,
        "required_skills": teacher->query("teach_required_skills") || ([ ]),
        "required_quests": teacher->query("teach_required_quests") || ({ }),
        "required_chests": teacher->query("teach_required_chests") || ({ }),
        "trial_skill_limit": teacher->query("teach_trial_skill_limit") || 0,
        "trial_bypass_requirements": teacher->query("teach_trial_bypass_requirements") ? 1 : 0,
        "trial_cost_policy": teacher->query("teach_trial_cost_policy") || "",
        "cost_kind": teacher->query("teach_cost_kind") || "potential",
        "cost_policy": teacher->query("teach_cost_policy") || "progression_default",
        "teach_skills": ({ skill_id }),
        "max_skill": query_teacher_skill_limit(teacher, 0, skill_id),
    ]);
}

private int teacher_source_allows_sect(mapping source, mapping sect_status)
{
    mixed allowed_sects;
    string sect_id;

    allowed_sects = source["allowed_sects"];
    if (!arrayp(allowed_sects) || member_array("*", allowed_sects) != -1)
        return 1;

    sect_id = mapp(sect_status) ? (sect_status["sect_id"] || "") : "";
    return member_array(sect_id, allowed_sects) != -1;
}

private int teacher_source_meets_required_skills(object player, mixed required_skills)
{
    mixed raw_level;
    string required_skill;
    int required_level;

    if (!required_skills)
        return 1;

    if (stringp(required_skills))
        return SKILL_D->query_player_skill_level(player, required_skills) > 0;

    if (arrayp(required_skills)) {
        foreach (required_skill in required_skills) {
            if (SKILL_D->query_player_skill_level(player, required_skill) <= 0)
                return 0;
        }
        return 1;
    }

    if (!mapp(required_skills))
        return 1;

    foreach (required_skill, raw_level in required_skills) {
        required_level = (intp(raw_level) && raw_level > 0) ? raw_level : 1;
        if (SKILL_D->query_player_skill_level(player, required_skill) < required_level)
            return 0;
    }

    return 1;
}

private string format_required_skill_hint(object player, mixed required_skills)
{
    mixed raw_level;
    string required_skill;
    int required_level;
    int current_level;
    string skill_name;

    if (!objectp(player) || !required_skills)
        return "";
    if (stringp(required_skills) && required_skills != "") {
        current_level = SKILL_D->query_player_skill_level(player, required_skills);
        if (current_level > 0)
            return "";
        skill_name = query_skill_display_name(required_skills);
        return sprintf("需要%s 1 级，当前%d级", skill_name, current_level);
    }
    if (arrayp(required_skills)) {
        foreach (required_skill in required_skills) {
            if (!stringp(required_skill) || required_skill == "")
                continue;
            current_level = SKILL_D->query_player_skill_level(player, required_skill);
            if (current_level <= 0)
                return sprintf("需要%s 1 级，当前%d级", query_skill_display_name(required_skill), current_level);
        }
        return "";
    }
    if (!mapp(required_skills))
        return "";
    foreach (required_skill, raw_level in required_skills) {
        if (!stringp(required_skill) || required_skill == "")
            continue;
        required_level = intp(raw_level) && raw_level > 0 ? raw_level : 1;
        current_level = SKILL_D->query_player_skill_level(player, required_skill);
        if (current_level < required_level)
            return sprintf("需要%s %d 级，当前%d级", query_skill_display_name(required_skill), required_level, current_level);
    }
    return "";
}

private string format_required_chest_hint(object player, mixed required_chests)
{
    string chest_id;

    if (!objectp(player) || !required_chests)
        return "";
    if (stringp(required_chests) && required_chests != "")
        return EXPLORE_REWARD_D->query_chest_open_count(player, required_chests) > 0 ? "" : "需要先开启探索宝箱";
    if (!arrayp(required_chests))
        return "";
    foreach (chest_id in required_chests) {
        if (stringp(chest_id) && chest_id != ""
            && EXPLORE_REWARD_D->query_chest_open_count(player, chest_id) <= 0)
            return "需要先开启相关探索宝箱";
    }
    return "";
}

private string format_requirement_hint(object player, mapping source, string error)
{
    if (!mapp(source) || !stringp(error) || error == "")
        return "";
    switch (error) {
        case "missing_required_skills":
            return format_required_skill_hint(player, source["required_skills"]);
        case "missing_required_quests":
            return "需要先完成相关支线任务";
        case "missing_required_chests":
            return format_required_chest_hint(player, source["required_chests"]);
    }
    return "";
}

private int teacher_source_meets_required_quests(object player, mixed required_quests)
{
    string quest_id;

    if (!required_quests)
        return 1;
    if (stringp(required_quests) && required_quests != "")
        return QUEST_D->is_quest_completed(player, required_quests);
    if (!arrayp(required_quests))
        return 1;
    foreach (quest_id in required_quests) {
        if (stringp(quest_id) && quest_id != ""
            && !QUEST_D->is_quest_completed(player, quest_id))
            return 0;
    }
    return 1;
}

private int teacher_source_meets_required_chests(object player, mixed required_chests)
{
    string chest_id;
    int required_count;
    mixed raw_count;

    if (!required_chests)
        return 1;
    if (stringp(required_chests) && required_chests != "")
        return EXPLORE_REWARD_D->query_chest_open_count(player, required_chests) > 0;
    if (arrayp(required_chests)) {
        foreach (chest_id in required_chests) {
            if (stringp(chest_id) && chest_id != ""
                && EXPLORE_REWARD_D->query_chest_open_count(player, chest_id) <= 0)
                return 0;
        }
        return 1;
    }
    if (!mapp(required_chests))
        return 1;
    foreach (chest_id, raw_count in required_chests) {
        required_count = intp(raw_count) && raw_count > 0 ? raw_count : 1;
        if (EXPLORE_REWARD_D->query_chest_open_count(player, chest_id) < required_count)
            return 0;
    }
    return 1;
}

private int teacher_source_meets_normal_requirements(object player,
    mapping source)
{
    if (!objectp(player) || !mapp(source))
        return 0;
    return teacher_source_meets_required_skills(player,
        source["required_skills"])
        && teacher_source_meets_required_quests(player,
            source["required_quests"])
        && teacher_source_meets_required_chests(player,
            source["required_chests"]);
}

private mapping resolve_source_definition(mapping source, mapping org_status)
{
    mapping sect_skill_source;
    mapping guild_skill_source;
    mapping resolved;
    string sect_id;
    string sect_skill_id;
    string guild_id;
    string guild_skill_id;
    string key;

    if (!mapp(source))
        return 0;

    resolved = copy(source);

    sect_skill_id = stringp(source["sect_skill_id"]) ? source["sect_skill_id"] : "";
    if (sect_skill_id != "") {
        sect_id = stringp(source["sect_id"]) ? source["sect_id"] : "";
        if (sect_id == "" && mapp(org_status))
            sect_id = org_status["organization_id"] || "";
        if (sect_id != "") {
            sect_skill_source = SECT_SKILL_D->resolve_sect_skill_source(sect_id, sect_skill_id);
            if (mapp(sect_skill_source)) {
                foreach (key in keys(sect_skill_source)) {
                    if (member_array(key, keys(resolved)) == -1)
                        resolved[key] = sect_skill_source[key];
                }
            }
        }
    }

    guild_skill_id = stringp(source["guild_skill_id"]) ? source["guild_skill_id"] : "";
    if (guild_skill_id == "")
        return resolved;

    guild_id = stringp(source["guild_id"]) ? source["guild_id"] : "";
    if (guild_id == "" && mapp(org_status))
        guild_id = org_status["organization_id"] || "";
    if (guild_id == "")
        return resolved;

    guild_skill_source = GUILD_SKILL_D->resolve_guild_skill_source(guild_id, guild_skill_id);
    if (!mapp(guild_skill_source))
        return resolved;

    foreach (key in keys(guild_skill_source)) {
        if (member_array(key, keys(resolved)) == -1)
            resolved[key] = guild_skill_source[key];
    }

    return resolved;
}

private mapping resolve_teacher_source_context(object player, string source_id)
{
    mapping org_status;
    mapping source;
    string provider_path;
    string skill_id;
    object teacher;

    source = SKILL_SOURCE_REGISTRY_D->query_source(source_id);
    if (!mapp(source) || source["source_type"] != "teacher")
        return 0;

    org_status = query_source_organization_status(player, source);
    source = resolve_source_definition(source, org_status);
    if (!CONTENT_MODE_POLICY_D->is_content_acquirable(player, "skill", source))
        return ([ "ok": 0, "error": "skill_unavailable_in_world",
            "source_id": source["id"] || "" ]);
    provider_path = stringp(source["provider_path"]) ? source["provider_path"] : "";
    if (provider_path == "")
        return 0;

    teacher = load_object(provider_path);
    if (!objectp(teacher))
        return 0;

    skill_id = arrayp(source["teach_skills"]) && sizeof(source["teach_skills"]) > 0
        ? source["teach_skills"][0]
        : "";
    if (skill_id == "")
        return 0;

    return ([
        "source": source,
        "teacher": teacher,
        "skill_id": skill_id,
        "display_skill_id": stringp(source["guild_skill_id"]) && source["guild_skill_id"] != ""
            ? source["guild_skill_id"]
            : skill_id,
        "display_name": stringp(source["name"]) && source["name"] != ""
            ? source["name"]
            : ((mapp(SKILL_D->get_skill_definition(skill_id)) && stringp(SKILL_D->get_skill_definition(skill_id)["name"]))
                ? SKILL_D->get_skill_definition(skill_id)["name"]
                : skill_id),
        "organization": org_status,
    ]);
}

private mapping query_source_organization_status(object player, mapping source)
{
    mapping guild_status;
    mapping sect_status;
    string guild_id;
    string organization_type;

    organization_type = mapp(source) && stringp(source["organization_type"])
        ? lower_case(source["organization_type"])
        : "none";
    if (!objectp(player))
        return ([ "organization_type": organization_type, "joined": 0 ]);

    switch (organization_type) {
        case "sect":
            sect_status = SECT_D->query_player_sect_status(player);
            return ([
                "organization_type": "sect",
                "joined": mapp(sect_status) && sect_status["joined"] ? 1 : 0,
                "organization_id": mapp(sect_status) ? (sect_status["sect_id"] || "") : "",
                "rank": mapp(sect_status) ? (sect_status["rank"] || 0) : 0,
                "rank_key": "",
                "contribution": mapp(sect_status) ? (sect_status["contribution"] || 0) : 0,
            ]);

        case "guild":
            guild_status = GUILD_D->query_player_guild_status(player);
            guild_id = "";
            if (mapp(guild_status))
                guild_id = guild_status["guild_id"] || "";
            return ([
                "organization_type": "guild",
                "joined": mapp(guild_status) && guild_status["joined"] ? 1 : 0,
                "organization_id": guild_id,
                "rank": 0,
                "rank_key": mapp(guild_status) ? (guild_status["rank_key"] || "") : "",
                "contribution": mapp(guild_status) ? (guild_status["contribution"] || 0) : 0,
            ]);
    }

    return ([
        "organization_type": "none",
        "joined": 1,
        "organization_id": "",
        "rank": 0,
        "rank_key": "",
        "contribution": 0,
    ]);
}

private int source_allows_organization(mapping source, mapping org_status)
{
    mixed allowed;
    string organization_id;
    string organization_type;

    if (!mapp(source) || !mapp(org_status))
        return 0;

    organization_type = org_status["organization_type"] || "none";
    organization_id = org_status["organization_id"] || "";
    switch (organization_type) {
        case "sect":
            allowed = source["allowed_sects"];
            break;
        case "guild":
            allowed = source["allowed_guilds"];
            break;
        default:
            return 1;
    }

    if (!arrayp(allowed) || member_array("*", allowed) != -1)
        return 1;

    return member_array(organization_id, allowed) != -1;
}

private int source_meets_rank_requirement(mapping source, mapping org_status)
{
    string organization_type;
    string required_rank_key;
    mixed allowed_rank_keys;
    int required_rank;

    if (!mapp(source) || !mapp(org_status))
        return 0;

    organization_type = org_status["organization_type"] || "none";
    switch (organization_type) {
        case "sect":
            required_rank = intp(source["required_rank"]) ? source["required_rank"] : 0;
            return (org_status["rank"] || 0) >= required_rank;

        case "guild":
            required_rank_key = stringp(source["required_rank_key"]) ? source["required_rank_key"] : "";
            allowed_rank_keys = source["required_rank_keys"];
            if (required_rank_key != "")
                return org_status["rank_key"] == required_rank_key;
            if (arrayp(allowed_rank_keys) && sizeof(allowed_rank_keys) > 0)
                return member_array(org_status["rank_key"] || "", allowed_rank_keys) != -1;
            return 1;
    }

    return 1;
}

private string normalize_cost_error(string error)
{
    if (!stringp(error) || error == "")
        return "insufficient_cost";

    return error;
}

private mapping append_skill_cost_quote(object player, string action, string skill_id,
    mapping source, mapping result, mixed known_context)
{
    mapping cost_check;
    mapping cost_spec;

    if (!mapp(result) || !result["ok"])
        return result;

    cost_spec = PROGRESSION_D->query_skill_action_cost_spec(player, action, skill_id,
        source, known_context);
    if (!mapp(cost_spec) || !cost_spec["ok"])
        return ([
            "ok": 0,
            "error": "invalid_cost_spec",
            "source_id": mapp(source) ? (source["id"] || "") : "",
        ]);

    cost_check = PROGRESSION_D->can_pay_skill_cost(player, cost_spec);
    result["cost_spec"] = cost_spec;
    result["can_pay_cost"] = mapp(cost_check) && cost_check["ok"] ? 1 : 0;
    result["cost_error"] = mapp(cost_check) ? (cost_check["error"] || "") : "invalid_cost_spec";
    result["cost_current"] = mapp(cost_check) ? (cost_check["current"] || 0) : 0;
    return result;
}

private mapping spend_skill_action_cost(object player, mapping result)
{
    mapping cost_spec;
    mapping spend_result;

    cost_spec = mapp(result) ? result["cost_spec"] : 0;
    if (!mapp(cost_spec) || !cost_spec["ok"])
        return ([ "ok": 1, "kind": "none", "amount": 0 ]);

    spend_result = PROGRESSION_D->spend_skill_cost(player, cost_spec);
    if (!mapp(spend_result) || !spend_result["ok"]) {
        return ([
            "ok": 0,
            "error": normalize_cost_error(mapp(spend_result) ? spend_result["error"] : "spend_failed"),
        ]);
    }

    return spend_result;
}

private void refund_skill_action_cost(object player, mapping result)
{
    mapping cost_spec;

    cost_spec = mapp(result) ? result["cost_spec"] : 0;
    if (!mapp(cost_spec) || !cost_spec["ok"])
        return;

    PROGRESSION_D->refund_skill_cost(player, cost_spec);
}

mapping query_teacher_learn_quote(object player, object teacher, string skill_id)
{
    mapping result;

    result = can_learn_from_teacher(player, teacher, skill_id);
    return append_skill_cost_quote(player, "learn", skill_id,
        mapp(result) ? result["source"] : 0, result,
        mapp(result) && intp(result["current_level"])
            ? ([ "current_level": result["current_level"] ]) : 0);
}

mapping query_teacher_source_learn_quote(object player, string source_id)
{
    mapping context;
    mapping result;
    mapping source;
    mapping org_status;
    int current_level;
    int required_contribution;

    context = resolve_teacher_source_context(player, source_id);
    if (!mapp(context))
        return ([ "ok": 0, "error": "missing_skill_source", "source_id": source_id ]);

    source = context["source"];
    current_level = SKILL_D->query_player_skill_level(player, context["skill_id"]);
    source = apply_teacher_trial_policy(source, current_level,
        teacher_source_meets_normal_requirements(player, source));
    org_status = context["organization"];
    if (current_level > 0) {
        result = ([ "ok": 0, "error": "already_learned", "source_id": source_id ]);
    } else if (!org_status["joined"]) {
        if (org_status["organization_type"] == "sect")
            result = ([ "ok": 0, "error": "sect_not_allowed", "source_id": source_id ]);
        else
            result = ([ "ok": 0, "error": "organization_missing", "source_id": source_id ]);
    } else if (!source_allows_organization(source, org_status)) {
        if (org_status["organization_type"] == "sect")
            result = ([ "ok": 0, "error": "sect_not_allowed", "source_id": source_id ]);
        else
            result = ([ "ok": 0, "error": "organization_not_allowed", "source_id": source_id ]);
    } else if (!source_meets_rank_requirement(source, org_status)) {
        result = ([ "ok": 0, "error": "rank_too_low", "source_id": source_id ]);
    } else {
        required_contribution = intp(source["required_contribution"]) ? source["required_contribution"] : 0;
        if ((org_status["contribution"] || 0) < required_contribution)
            result = ([ "ok": 0, "error": "contribution_too_low", "source_id": source_id ]);
        else if (!teacher_source_meets_required_skills(player, source["required_skills"]))
            result = ([ "ok": 0, "error": "missing_required_skills", "source_id": source_id, "source": source ]);
        else if (!teacher_source_meets_required_quests(player, source["required_quests"]))
            result = ([ "ok": 0, "error": "missing_required_quests", "source_id": source_id, "source": source ]);
        else if (!teacher_source_meets_required_chests(player, source["required_chests"]))
            result = ([ "ok": 0, "error": "missing_required_chests", "source_id": source_id, "source": source ]);
        else
            result = ([ "ok": 1, "source_id": source_id, "source": source, "organization": org_status ]);
    }

    result = append_skill_cost_quote(player, "learn", context["skill_id"], source,
        result, 0);
    result["skill_id"] = context["skill_id"];
    result["display_skill_id"] = context["display_skill_id"];
    result["display_name"] = context["display_name"];
    result["requirement_hint"] = format_requirement_hint(player, source, result["error"] || "");
    return result;
}

mapping learn_from_teacher_source(object player, string source_id)
{
    mapping context;
    mapping result;
    mapping spend_result;

    context = resolve_teacher_source_context(player, source_id);
    if (!mapp(context))
        return ([ "ok": 0, "error": "missing_skill_source", "source_id": source_id ]);

    result = query_teacher_source_learn_quote(player, source_id);
    if (!mapp(result) || !result["ok"])
        return result;
    if (!result["can_pay_cost"])
        return ([ "ok": 0, "error": normalize_cost_error(result["cost_error"]), "source_id": source_id, "cost_spec": result["cost_spec"] ]);

    spend_result = spend_skill_action_cost(player, result);
    if (!mapp(spend_result) || !spend_result["ok"])
        return ([ "ok": 0, "error": normalize_cost_error(mapp(spend_result) ? spend_result["error"] : "spend_failed"), "source_id": source_id ]);

    if (!SKILL_D->learn_skill(player, context["skill_id"])) {
        refund_skill_action_cost(player, result);
        return ([ "ok": 0, "error": "learn_skill_failed", "source_id": source_id ]);
    }
    record_onboarding_training_events(player, context["skill_id"]);

    result = ([
        "ok": 1,
        "action": "learn",
        "skill_id": context["skill_id"],
        "source_id": source_id,
        "cost_spec": result["cost_spec"],
    ]);
    result["display_skill_id"] = context["display_skill_id"];
    result["display_name"] = context["display_name"];
    return result;
}

mapping query_manual_study_quote(object player, object manual)
{
    mapping result;

    result = can_study_from_manual(player, manual);
    return append_skill_cost_quote(player, "study",
        mapp(result) ? (result["skill_id"] || "") : "",
        mapp(result) ? result["source"] : 0, result, 0);
}

mapping query_practice_quote(object player, string skill_id)
{
    mapping result;

    result = can_practice_skill(player, skill_id);
    return append_skill_cost_quote(player, "practice", skill_id,
        mapp(result) ? result["source"] : 0, result, 0);
}

mixed *query_organization_skill_offerings(object player, string organization_type)
{
    mapping context;
    mapping result;
    mapping source;
    mixed *offerings;
    string source_id;
    string wanted_type;

    offerings = ({ });
    wanted_type = stringp(organization_type) ? lower_case(organization_type) : "";
    foreach (source_id, source in SKILL_SOURCE_REGISTRY_D->query_all_sources() || ([])) {
        if (!mapp(source) || source["source_type"] != "teacher")
            continue;
        if ((stringp(source["organization_type"]) ? lower_case(source["organization_type"]) : "none") != wanted_type)
            continue;

        context = resolve_teacher_source_context(player, source_id);
        if (!mapp(context))
            continue;

        result = query_teacher_source_learn_quote(player, source_id);
        offerings += ({ ([
            "source_id": source_id,
            "display_skill_id": context["display_skill_id"],
            "display_name": context["display_name"],
            "skill_id": context["skill_id"],
            "skill_type": mapp(context["source"]) ? (context["source"]["skill_type"] || "basic") : "basic",
            "can_learn": mapp(result) && result["ok"] ? 1 : 0,
            "can_pay_cost": mapp(result) && result["ok"] && result["can_pay_cost"] ? 1 : 0,
            "error": mapp(result) && result["ok"]
                ? (result["can_pay_cost"] ? "" : (result["cost_error"] || ""))
                : (mapp(result) ? (result["error"] || "") : "missing_skill_source"),
            "cost_spec": mapp(result) ? (result["cost_spec"] || ([])) : ([]),
        ]) });
    }

    return offerings;
}

private mapping resolve_organization_skill_offering(object player, string organization_type,
    string token)
{
    mapping offering;
    string normalized;

    normalized = stringp(token) ? lower_case(token) : "";
    foreach (offering in query_organization_skill_offerings(player, organization_type)) {
        if (!mapp(offering))
            continue;
        if (normalized == ""
            || lower_case(offering["source_id"] || "") == normalized
            || lower_case(offering["display_skill_id"] || "") == normalized
            || lower_case(offering["skill_id"] || "") == normalized) {
            return offering;
        }
    }

    return 0;
}

mapping learn_from_organization_source(object player, string organization_type, string token)
{
    mapping offering;

    offering = resolve_organization_skill_offering(player, organization_type, token);
    if (!mapp(offering))
        return ([ "ok": 0, "error": "missing_skill_source" ]);

    return learn_from_teacher_source(player, offering["source_id"]);
}

private int query_organization_skill_page_size(string organization_type)
{
    return 3;
}

private string query_skill_group_label(string skill_type)
{
    switch (skill_type) {
        case "force": return "内功";
        case "sword": return "剑法";
        case "blade": return "刀法";
        case "spear": return "枪法";
        case "staff": return "杖法";
        case "hand": return "手法";
        case "dodge": return "轻功";
        case "parry": return "招架";
        case "unarmed": return "拳掌";
        case "basic": return "基础";
        default: return "其他";
    }
}

private int query_skill_group_order(string skill_type)
{
    switch (skill_type) {
        case "basic": return 0;
        case "force": return 1;
        case "unarmed": return 2;
        case "blade": return 3;
        case "sword": return 4;
        case "spear": return 5;
        case "staff": return 6;
        case "hand": return 7;
        case "dodge": return 8;
        case "parry": return 9;
        default: return 99;
    }
}

private int compare_organization_offering(mapping left, mapping right)
{
    int left_order;
    int right_order;
    string left_name;
    string right_name;

    left_order = query_skill_group_order(left["skill_type"] || "basic");
    right_order = query_skill_group_order(right["skill_type"] || "basic");
    if (left_order != right_order)
        return left_order - right_order;

    left_name = lower_case(left["display_skill_id"] || left["skill_id"] || left["source_id"] || "");
    right_name = lower_case(right["display_skill_id"] || right["skill_id"] || right["source_id"] || "");
    if (left_name == right_name)
        return 0;
    return left_name < right_name ? -1 : 1;
}

string render_organization_skill_offerings_page(object player, string organization_type,
    int page)
{
    mapping cost_spec;
    mapping offering;
    mixed *offerings;
    mixed *page_offerings;
    string current_group;
    string group_label;
    string output;
    string page_hint;
    string title;
    int end;
    int page_size;
    int start;
    int total_pages;

    offerings = query_organization_skill_offerings(player, organization_type);
    if (!arrayp(offerings) || sizeof(offerings) == 0)
        return "当前没有可公开展示的组织武学。\n";

    offerings = sort_array(offerings, (: compare_organization_offering :));
    page_size = query_organization_skill_page_size(organization_type);
    if (page_size <= 0)
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

    title = lower_case(organization_type) == "guild" ? "帮派武学" : "门派武学";
    output = sprintf("%s（第 %d/%d 页）：\n", title, page, total_pages);
    current_group = "";
    foreach (offering in page_offerings) {
        cost_spec = mapp(offering) ? (offering["cost_spec"] || ([])) : ([]);
        if (!mapp(offering))
            continue;

        group_label = query_skill_group_label(offering["skill_type"] || "basic");
        if (group_label != current_group) {
            current_group = group_label;
            output += sprintf("[%s]\n", current_group);
        }

        output += sprintf("- %s（%s）：%s",
            offering["display_skill_id"] || offering["skill_id"] || offering["source_id"] || "",
            offering["display_name"] || offering["skill_id"] || "",
            offering["can_learn"] && offering["can_pay_cost"] ? "可学" : format_teacher_offering_error(offering["error"]));
        if (mapp(cost_spec) && intp(cost_spec["amount"]) && cost_spec["amount"] > 0) {
            output += sprintf("（%s:%d）",
                cost_spec["kind"] || "none",
                cost_spec["amount"] || 0);
        }
        output += "\n";
    }

    page_hint = lower_case(organization_type) == "guild"
        ? "帮派武学下一页"
        : "门派武学下一页";
    output += "看中哪门武学，向负责传授的人说明即可。\n";
    if (total_pages > 1)
        output += sprintf("还可继续查看%s。\n", page_hint);
    return output;
}

string render_organization_skill_offerings(object player, string organization_type)
{
    return render_organization_skill_offerings_page(player, organization_type, 1);
}

mapping can_learn_from_teacher(object player, object teacher, string skill_id)
{
    mapping org_status;
    mapping source;
    int current_level;
    int max_skill;
    int required_contribution;
    int skill_cap;
    string provider_id;

    if (!objectp(player) || !objectp(teacher) || !stringp(skill_id) || skill_id == "")
        return ([ "ok": 0, "error": "invalid_arguments" ]);

    provider_id = query_teacher_provider_id(teacher);
    if (provider_id != "")
        source = SKILL_SOURCE_REGISTRY_D->query_teacher_source(provider_id, skill_id);
    if (!mapp(source))
        source = query_configured_teacher_source(teacher, skill_id);
    if (!mapp(source))
        return ([ "ok": 0, "error": "missing_skill_source" ]);

    org_status = query_source_organization_status(player, source);
    source = resolve_source_definition(source, org_status);
    if (!CONTENT_MODE_POLICY_D->is_content_acquirable(player, "skill", source))
        return ([ "ok": 0, "error": "skill_unavailable_in_world",
            "source_id": source["id"] || "" ]);
    max_skill = query_teacher_skill_limit(teacher, source, skill_id);
    current_level = SKILL_D->query_player_skill_level(player, skill_id);
    source = apply_teacher_trial_policy(source, current_level,
        teacher_source_meets_normal_requirements(player, source));
    if (max_skill > 0 && current_level >= max_skill)
        return ([ "ok": 0, "error": "teacher_limit_reached", "source_id": source["id"], "max_skill": max_skill ]);

    if (!org_status["joined"]) {
        if (org_status["organization_type"] == "sect")
            return ([ "ok": 0, "error": "sect_not_allowed", "source_id": source["id"] ]);
        return ([ "ok": 0, "error": "organization_missing", "source_id": source["id"] ]);
    }
    if (!source_allows_organization(source, org_status)) {
        if (org_status["organization_type"] == "sect")
            return ([ "ok": 0, "error": "sect_not_allowed", "source_id": source["id"] ]);
        return ([ "ok": 0, "error": "organization_not_allowed", "source_id": source["id"] ]);
    }

    if (!source_meets_rank_requirement(source, org_status))
        return ([ "ok": 0, "error": "rank_too_low", "source_id": source["id"] ]);

    required_contribution = intp(source["required_contribution"]) ? source["required_contribution"] : 0;
    if ((org_status["contribution"] || 0) < required_contribution)
        return ([ "ok": 0, "error": "contribution_too_low", "source_id": source["id"] ]);

    if (!teacher_source_meets_required_skills(player, source["required_skills"]))
        return ([ "ok": 0, "error": "missing_required_skills", "source_id": source["id"], "source": source,
            "requirement_hint": format_requirement_hint(player, source, "missing_required_skills") ]);
    if (!teacher_source_meets_required_quests(player, source["required_quests"]))
        return ([ "ok": 0, "error": "missing_required_quests", "source_id": source["id"], "source": source,
            "requirement_hint": format_requirement_hint(player, source, "missing_required_quests") ]);
    if (!teacher_source_meets_required_chests(player, source["required_chests"]))
        return ([ "ok": 0, "error": "missing_required_chests", "source_id": source["id"], "source": source,
            "requirement_hint": format_requirement_hint(player, source, "missing_required_chests") ]);

    skill_cap = SKILL_D->query_player_skill_cap(player, skill_id);
    if (current_level >= skill_cap)
        return ([ "ok": 0, "error": "skill_cap_reached", "source_id": source["id"], "skill_id": skill_id, "skill_cap": skill_cap, "max_skill": max_skill ]);

    return ([
        "ok": 1,
        "source_id": source["id"],
        "source": source,
        "organization": org_status,
        "current_level": current_level,
        "max_skill": max_skill,
    ]);
}

mapping learn_from_teacher(object player, object teacher, string skill_id)
{
    return learn_from_teacher_levels(player, teacher, skill_id, 1);
}

private int apply_teacher_skill_level(object player, string skill_id,
    int current_level)
{
    if (current_level <= 0)
        return SKILL_D->learn_skill(player, skill_id);
    return SKILL_D->improve_skill(player, skill_id, 10);
}

private int onboarding_foundation_ready(object player)
{
    string skill_id;

    if (!objectp(player)
        || SKILL_D->query_player_skill_level(player, "force") < 5
        || SKILL_D->query_player_skill_level(player, "dodge") < 5
        || SKILL_D->query_player_skill_level(player, "parry") < 5)
        return 0;

    foreach (skill_id in ({ "unarmed", "hand", "sword", "blade", "axe" }))
        if (SKILL_D->query_player_skill_level(player, skill_id) >= 5)
            return 1;
    return 0;
}

private int is_onboarding_outer_skill(object player, string skill_id)
{
    mapping options;

    if (!objectp(player) || !stringp(skill_id) || skill_id == "")
        return 0;
    options = SKILL_BEHAVIOR_D->query_skill_activation_options(player, skill_id);
    return mapp(options) && mapp(options["outer"]);
}

private void record_onboarding_training_events(object player, string skill_id)
{
    int level;

    if (!objectp(player) || !stringp(skill_id) || skill_id == "")
        return;
    level = SKILL_D->query_player_skill_level(player, skill_id);
    if (onboarding_foundation_ready(player))
        QUEST_D->record_action_progress(player,
            "onboarding_foundation_ready", skill_id, "success");
    if (level == 1 && SKILL_BREAKTHROUGH_D->is_public_skill(skill_id)
        && is_onboarding_outer_skill(player, skill_id))
        QUEST_D->record_action_progress(player,
            "onboarding_outer_skill_trialled", skill_id, "success");
    if (level >= 100 && SKILL_BREAKTHROUGH_D->is_public_skill(skill_id))
        QUEST_D->record_action_progress(player,
            "first_breakthrough_skill_ready", skill_id, "success");
}

mapping learn_from_teacher_levels(object player, object teacher, string skill_id, mixed level_count)
{
    mapping result;
    mapping spend_result;
    mapping total_cost_spec;
    int learned;
    int requested;
    int current_level;
    int final_level;
    int max_skill;
    int total_cost_amount;
    string last_error;

    if (!objectp(player) || !objectp(teacher) || !stringp(skill_id) || skill_id == "")
        return ([ "ok": 0, "error": "invalid_arguments" ]);

    result = query_teacher_learn_quote(player, teacher, skill_id);
    if (!mapp(result) || !result["ok"])
        return result;

    current_level = intp(result["current_level"])
        ? result["current_level"]
        : SKILL_D->query_player_skill_level(player, skill_id);
    max_skill = result["max_skill"] || query_teacher_skill_limit(teacher, result["source"], skill_id);
    if (stringp(level_count) && lower_case(level_count) == "all") {
        requested = max_skill > current_level ? max_skill - current_level : 0;
        if (requested <= 0 && current_level <= 0)
            requested = 1;
    } else if (intp(level_count)) {
        requested = level_count;
    } else {
        requested = 1;
    }
    if (requested <= 0)
        return ([ "ok": 0, "error": "teacher_limit_reached", "skill_id": skill_id, "max_skill": max_skill ]);

    total_cost_spec = ([]);
    while (requested > 0) {
        if (learned > 0)
            result = query_teacher_learn_quote(player, teacher, skill_id);
        if (!mapp(result) || !result["ok"]) {
            last_error = mapp(result) ? (result["error"] || "learn_skill_failed") : "learn_skill_failed";
            break;
        }
        if (!result["can_pay_cost"]) {
            last_error = normalize_cost_error(result["cost_error"]);
            break;
        }
        if (intp(result["current_level"]))
            current_level = result["current_level"];

        spend_result = spend_skill_action_cost(player, result);
        if (!mapp(spend_result) || !spend_result["ok"]) {
            last_error = normalize_cost_error(mapp(spend_result) ? spend_result["error"] : "spend_failed");
            break;
        }

        if (!apply_teacher_skill_level(player, skill_id, current_level)) {
            refund_skill_action_cost(player, result);
            last_error = "learn_skill_failed";
            break;
        }
        record_onboarding_training_events(player, skill_id);
        final_level = current_level + 1;
        if (mapp(result["cost_spec"])) {
            total_cost_amount += result["cost_spec"]["amount"] || 0;
            total_cost_spec = copy(result["cost_spec"]);
        }
        learned++;
        requested--;
    }

    if (learned <= 0)
        return ([
            "ok": 0,
            "error": stringp(last_error) && last_error != ""
                ? last_error
                : (mapp(result) && result["can_pay_cost"] == 0
                ? normalize_cost_error(result["cost_error"])
                : (mapp(result) ? (result["error"] || "learn_skill_failed") : "learn_skill_failed")),
            "skill_id": skill_id,
            "cost_spec": mapp(result) ? (result["cost_spec"] || ([])) : ([]),
            "cost_current": mapp(result) ? (result["cost_current"] || 0) : 0,
        ]);

    if (stringp(last_error) && last_error != "") {
        total_cost_spec["stop_error"] = last_error;
        total_cost_spec["stop_cost_spec"] = mapp(result) && mapp(result["cost_spec"]) ? copy(result["cost_spec"]) : ([]);
        total_cost_spec["stop_cost_current"] = mapp(result) ? (result["cost_current"] || 0) : 0;
    }

    if (mapp(total_cost_spec) && total_cost_amount > 0)
        total_cost_spec["amount"] = total_cost_amount;

    return ([
        "ok": 1,
        "action": "learn",
        "skill_id": skill_id,
        "display_name": query_skill_display_name(skill_id),
        "levels": learned,
        "level": final_level,
        "source_id": result["source_id"],
        "cost_spec": total_cost_amount > 0 ? total_cost_spec : result["cost_spec"],
    ]);
}

private string format_teacher_offering_error(string error)
{
    switch (error) {
        case "sect_not_allowed":
            return "门派不符";
        case "organization_not_allowed":
            return "门派不符";
        case "organization_missing":
            return "未加入组织";
        case "rank_too_low":
            return "身份不足";
        case "contribution_too_low":
            return "贡献不足";
        case "insufficient_potential":
            return "潜能不足";
        case "insufficient_combat_exp":
            return "经验不足";
        case "insufficient_sect_contribution":
            return "门派贡献不足";
        case "insufficient_guild_contribution":
            return "帮派贡献不足";
        case "already_learned":
            return "已经掌握";
        case "teacher_limit_reached":
            return "已到上限";
        case "skill_cap_reached":
            return "修为不足";
        case "missing_required_skills":
            return "前置不足";
        case "missing_required_quests":
            return "支线不足";
        case "missing_required_chests":
            return "探索不足";
        case "missing_skill_source":
            return "暂无来源";
        default:
            return "暂不可学";
    }
}

mixed *query_teacher_skill_offerings(object player, object teacher)
{
    mapping result;
    mixed *offerings;
    string provider_id;
    string skill_id;
    string *skills;

    provider_id = query_teacher_provider_id(teacher);
    if (provider_id == "")
        return ({ });

    skills = SKILL_SOURCE_REGISTRY_D->query_teacher_teach_skills(provider_id);
    skills += query_configured_teacher_skills(teacher);
    if (!arrayp(skills) || sizeof(skills) == 0)
        return ({ });
    skills = dedupe_skill_ids(skills);

    offerings = ({ });
    foreach (skill_id in skills) {
        result = query_teacher_learn_quote(player, teacher, skill_id);
        offerings += ({ ([
            "skill_id": skill_id,
            "display_name": query_skill_display_name(skill_id),
            "level": SKILL_D->query_player_skill_level(player, skill_id),
            "max_skill": mapp(result) ? (result["max_skill"] || query_teacher_skill_limit(teacher, result["source"], skill_id)) : query_teacher_skill_limit(teacher, 0, skill_id),
            "can_learn": result["ok"] ? 1 : 0,
            "can_pay_cost": result["ok"] && result["can_pay_cost"] ? 1 : 0,
            "error": result["ok"] ? "" : (result["error"] || ""),
            "display_error": result["ok"]
                ? (result["can_pay_cost"] ? "" : (result["cost_error"] || ""))
                : (result["error"] || ""),
            "display_status": result["ok"]
                ? (result["can_pay_cost"] ? "" : format_teacher_offering_error(result["cost_error"] || ""))
                : (stringp(result["requirement_hint"]) && result["requirement_hint"] != ""
                    ? result["requirement_hint"]
                    : format_teacher_offering_error(result["error"] || "")),
            "requirement_hint": result["requirement_hint"] || "",
            "source_id": result["source_id"] || "",
            "cost_spec": result["cost_spec"] || ([]),
            "cost_current": result["cost_current"] || 0,
        ]) });
    }

    return offerings;
}

string render_teacher_skill_offerings(object player, object teacher)
{
    mixed *offerings;
    mapping offering;
    string output;

    offerings = query_teacher_skill_offerings(player, teacher);
    if (!arrayp(offerings) || sizeof(offerings) == 0)
        return "此人当前没有可公开展示的授业内容。\n";

    output = "可授武学：\n";
    foreach (offering in offerings) {
        if (!mapp(offering))
            continue;

        if (offering["can_learn"] && offering["can_pay_cost"])
            output += sprintf("- %s：可学（%d/%d）\n",
                offering["display_name"] || offering["skill_id"],
                offering["level"] || 0, offering["max_skill"] || 0);
        else
            output += sprintf("- %s：%s\n", offering["display_name"] || offering["skill_id"], format_teacher_offering_error(offering["display_error"] || offering["error"]));
    }

    return output;
}

mapping can_study_from_manual(object player, object manual)
{
    mapping source;
    string manual_id;
    string skill_id;
    int exp_required;
    int level;
    int max_skill;
    int min_skill;

    if (!objectp(player) || !objectp(manual))
        return ([ "ok": 0, "error": "invalid_arguments" ]);

    manual_id = manual->query("manual_id") || "";
    source = SKILL_SOURCE_REGISTRY_D->query_manual_source(manual_id);
    if (!mapp(source))
        return ([ "ok": 0, "error": "missing_manual_source" ]);

    if (!arrayp(source["teach_skills"]) || sizeof(source["teach_skills"]) == 0)
        return ([ "ok": 0, "error": "missing_manual_skill", "source_id": source["id"] ]);

    skill_id = source["teach_skills"][0];
    exp_required = intp(source["exp_required"]) ? source["exp_required"] : 0;
    if ((player->query("combat_exp") || 0) < exp_required)
        return ([ "ok": 0, "error": "exp_too_low", "source_id": source["id"] ]);

    level = SKILL_D->query_player_skill_level(player, skill_id);
    min_skill = intp(source["min_skill"]) ? source["min_skill"] : 0;
    max_skill = intp(source["max_skill"]) ? source["max_skill"] : 0;
    if (level < min_skill)
        return ([ "ok": 0, "error": "min_skill_not_met", "source_id": source["id"], "skill_id": skill_id ]);
    if (max_skill > 0 && level >= max_skill)
        return ([ "ok": 0, "error": "manual_limit_reached", "source_id": source["id"], "skill_id": skill_id ]);
    if (level >= SKILL_D->query_player_skill_cap(player, skill_id))
        return ([ "ok": 0, "error": "skill_cap_reached", "source_id": source["id"], "skill_id": skill_id ]);

    return ([ "ok": 1, "source_id": source["id"], "source": source, "skill_id": skill_id ]);
}

mapping study_from_manual(object player, object manual)
{
    mapping result;
    mapping source;
    mapping spend_result;
    int current_level;

    result = query_manual_study_quote(player, manual);
    if (!mapp(result) || !result["ok"])
        return result;
    if (!result["can_pay_cost"])
        return ([ "ok": 0, "error": normalize_cost_error(result["cost_error"]), "source_id": result["source_id"], "skill_id": result["skill_id"], "cost_spec": result["cost_spec"] ]);

    spend_result = spend_skill_action_cost(player, result);
    if (!mapp(spend_result) || !spend_result["ok"])
        return ([ "ok": 0, "error": normalize_cost_error(mapp(spend_result) ? spend_result["error"] : "spend_failed"), "source_id": result["source_id"], "skill_id": result["skill_id"] ]);

    source = result["source"];
    current_level = SKILL_D->query_player_skill_level(player, result["skill_id"]);
    if (current_level > 0) {
        if (!SKILL_D->improve_skill(player, result["skill_id"], (source["difficulty"] || 1) * 10)) {
            refund_skill_action_cost(player, result);
            return ([ "ok": 0, "error": "study_failed", "source_id": result["source_id"] ]);
        }
    } else {
        if (!SKILL_D->learn_skill(player, result["skill_id"])) {
            refund_skill_action_cost(player, result);
            return ([ "ok": 0, "error": "study_failed", "source_id": result["source_id"] ]);
        }
    }

    return ([
        "ok": 1,
        "action": "study",
        "skill_id": result["skill_id"],
        "source_id": result["source_id"],
        "cost_spec": result["cost_spec"],
    ]);
}

mapping can_practice_skill(object player, string skill_id)
{
    mapping source;
    mixed required_basic;
    string basic_skill;
    int current_level;
    int max_skill;
    int required_level;

    if (!objectp(player) || !stringp(skill_id) || skill_id == "")
        return ([ "ok": 0, "error": "invalid_arguments" ]);

    source = SKILL_SOURCE_REGISTRY_D->query_practice_source(skill_id);
    if (!mapp(source))
        return ([ "ok": 0, "error": "missing_practice_source" ]);

    if (source["requires_enable"] && player->query_temp("practice/enabled") != skill_id)
        return ([ "ok": 0, "error": "not_enabled", "source_id": source["id"] ]);

    required_basic = source["requires_basic_skill"];
    if (mapp(required_basic)) {
        foreach (basic_skill, required_level in required_basic) {
            if (SKILL_D->query_player_skill_level(player, basic_skill) < required_level)
                return ([ "ok": 0, "error": "missing_basic_skill", "source_id": source["id"] ]);
        }
    }

    if ((player->query("combat_exp") || 0) < (source["required_combat_exp"] || 0))
        return ([ "ok": 0, "error": "exp_too_low", "source_id": source["id"] ]);

    current_level = SKILL_D->query_player_skill_level(player, skill_id);
    max_skill = source["max_skill"] || 0;
    if (max_skill > 0 && current_level >= max_skill)
        return ([ "ok": 0, "error": "practice_limit_reached", "source_id": source["id"] ]);
    if (current_level >= SKILL_D->query_player_skill_cap(player, skill_id))
        return ([ "ok": 0, "error": "skill_cap_reached", "source_id": source["id"] ]);

    return ([ "ok": 1, "source_id": source["id"], "source": source, "skill_id": skill_id ]);
}

mapping practice_skill(object player, string skill_id)
{
    mapping result;
    mapping source;
    mapping spend_result;

    result = query_practice_quote(player, skill_id);
    if (!mapp(result) || !result["ok"])
        return result;
    if (!result["can_pay_cost"])
        return ([ "ok": 0, "error": normalize_cost_error(result["cost_error"]), "source_id": result["source_id"], "skill_id": skill_id, "cost_spec": result["cost_spec"] ]);

    spend_result = spend_skill_action_cost(player, result);
    if (!mapp(spend_result) || !spend_result["ok"])
        return ([ "ok": 0, "error": normalize_cost_error(mapp(spend_result) ? spend_result["error"] : "spend_failed"), "source_id": result["source_id"], "skill_id": skill_id ]);

    source = result["source"];
    if (!SKILL_D->improve_skill(player, skill_id, (source["difficulty"] || 1) * 10)) {
        refund_skill_action_cost(player, result);
        return ([ "ok": 0, "error": "practice_failed", "source_id": result["source_id"] ]);
    }

    return ([
        "ok": 1,
        "action": "practice",
        "skill_id": skill_id,
        "source_id": result["source_id"],
        "cost_spec": result["cost_spec"],
    ]);
}

int learn_skill(object player, string skill_id)
{
    return SKILL_D->learn_skill(player, skill_id);
}
