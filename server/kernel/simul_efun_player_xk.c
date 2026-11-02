/**
 * @file    simul_efun_player_xk.c
 * @brief   玩家 XK UI 协议发送辅助子服务
 * @author  XiaKeXing Development Team
 * @date    2026-03-30
 * @version 1.0.0
 */

#pragma strict_types

#include <system/services.h>
#include <system/xk_protocol.h>
#include <game/quest.h>

#define XK_PROTOCOL_CORE "/kernel/framework/xk_protocol_core"
nosave mapping _skill_list_static_entry_cache;
nosave mapping _skill_list_action_fact_cache;
nosave int _skill_list_static_entry_behavior_generation;
nosave int _skill_list_static_numeric_generation;
nosave int _skill_list_static_entry_source_generation;

private string _xk_sanitize_action_token(string value, int max_length);

private int query_skill_unlock_available(object me, string unlock_type, string skill_id);
private mapping query_action_profile_payload(object me, string action_type, string skill_id);
private varargs mapping *query_action_intro_payloads(object me, string skill_id,
    int compact, mapping skill_data);
private int query_player_potential(object me);
private int query_player_age(object me);
private mapping *query_skill_list_action_facts(string skill_id);
private void refresh_skill_list_static_caches();
private mapping query_skill_activation_payload(object me, string skill_id);
private int query_skill_can_practice(string skill_id);
private mixed *query_skill_source_types(string skill_id);
private mapping build_skill_list_entry(object me, string skill_id, mapping skill_data);
private mapping build_life_skill_list_entry(mapping life_info);
private int query_skill_list_resource(object me, string resource);
private varargs string query_skill_list_shape_signature(object me, mapping skills);
private string query_life_skill_list_shape_signature(mapping life_skills);
private varargs mapping build_skill_list_resource_thresholds(object me,
    mapping skills);
private varargs mapping query_skill_list_resource_thresholds(object me,
    mapping skills);
private int query_skill_list_resource_bucket(object me, string resource,
    int *thresholds);
private int query_skill_list_has_combat_target(object me);
private string query_skill_list_fast_signature(object me,
    mapping resource_thresholds);
private varargs string query_skill_list_runtime_signature(object me,
    mapping skills, mapping life_skills, mapping resource_thresholds);
private mapping query_fast_cached_skill_list_entry(object me);
private mapping query_cached_skill_list_entry(object me, string runtime_signature);
private mapping build_skill_list_payload(object me);
private mapping query_active_skill_slots_payload(object me);
void xk_prime_skill_list_cache(object me);

private int query_skill_action_configured(string action_type, string skill_id)
{
    mapping *actions;

    if (!stringp(action_type) || action_type == "" || !stringp(skill_id) || skill_id == "")
        return 0;

    actions = SKILL_BEHAVIOR_D->query_skill_actions(action_type, skill_id);
    return arrayp(actions) && sizeof(actions) > 0 ? 1 : 0;
}

private string query_skill_display_name(string skill_id)
{
    mapping def;

    def = NUMERIC_RULE_D->query_skill_definition(skill_id);
    if (mapp(def) && stringp(def["name"]) && def["name"] != "")
        return def["name"];
    return stringp(skill_id) ? skill_id : "";
}

private mapping query_active_skill_slots_payload(object me)
{
    mapping active_slots;
    mapping payload;
    mapping by_category;
    string slot;
    string category;
    string skill_id;

    active_slots = SKILL_BEHAVIOR_D->query_active_skill_slots(me);
    payload = ([ ]);
    by_category = ([ ]);
    foreach (slot in ({ "outer", "parry", "force", "dodge" })) {
        skill_id = mapp(active_slots) && stringp(active_slots[slot])
            ? active_slots[slot] : "";
        payload[slot] = ([
            "skill_id": skill_id,
            "skill_name": skill_id != "" ? query_skill_display_name(skill_id) : "",
        ]);
    }
    if (mapp(active_slots) && mapp(active_slots["outer_by_category"]))
        foreach (category, skill_id in active_slots["outer_by_category"])
            if (stringp(skill_id) && skill_id != "")
                by_category[category] = ([
                    "skill_id": skill_id,
                    "skill_name": query_skill_display_name(skill_id),
                ]);
    payload["active_by_category"] = by_category;
    return payload;
}

private string query_action_label(string action_type, mapping profile)
{
    string description;
    int colon;

    if (mapp(profile) && stringp(profile["label"]) && profile["label"] != "")
        return profile["label"];
    if (mapp(profile) && stringp(profile["description"]) && profile["description"] != "") {
        description = profile["description"];
        colon = strsrch(description, "：");
        if (colon <= 0)
            colon = strsrch(description, ":");
        if (colon > 0)
            return description[0..colon - 1];
    }
    switch (action_type) {
    case "perform":
        return "招式";
    case "exert":
        return "内功";
    default:
        return action_type;
    }
}

private string query_action_default_usage(string action_type, string target_policy)
{
    if (action_type == "perform")
        return target_policy == "combat_target"
            ? "战斗中对当前目标施展，强化本次武学动作。"
            : "施展这门武学的主动招式。";
    if (action_type == "exert")
        return "运起内功或心法，对自身产生即时或下一次防御效果。";
    return "执行这门武学的主动能力。";
}

private string query_action_unavailable_reason(mapping unlock_result, mapping budget,
    string target_policy, object me)
{
    string error;

    if (mapp(unlock_result) && !unlock_result["ok"])
        error = unlock_result["error"];
    else if (mapp(budget) && !budget["ok"])
        error = budget["error"];
    else if (target_policy == "combat_target" && objectp(me)
        && !objectp(me->query_temp("fight_ob"))
        && (!function_exists("query_fight_object", me) || !objectp(me->query_fight_object())))
        return "需要先与目标交手。";
    else
        return "";

    switch (error) {
    case "missing_unlock_rule":
        return "这门能力尚未配置解锁规则。";
    case "skill_level_too_low":
        return "当前武学等级不足。";
    case "breakthrough_entry_required":
        return "需要先选择对应突破词条。";
    case "missing_action":
        return "这一路动作尚未配置。";
    case "sect_not_allowed":
        return "当前门派不能使用。";
    case "action_cooldown":
        return sprintf("还需%d秒冷却。", mapp(budget) ? (budget["cooldown_remaining"] || 0) : 0);
    case "action_interval":
        return sprintf("出手间隔还需%d秒。", mapp(budget) ? (budget["interval_remaining"] || 0) : 0);
    case "insufficient_resource":
        return "当前资源不足。";
    default:
        return "当前不可使用。";
    }
}

private string query_action_requirements(string action_type, string skill_id, mapping action_def)
{
    string *requirements;
    mapping rule;
    int level;

    requirements = ({ });
    rule = mapp(action_def) && mapp(action_def["unlock"])
        ? action_def["unlock"] : SKILL_UNLOCK_REGISTRY_D->query_unlock_rule(action_type, skill_id);
    level = mapp(action_def) ? (action_def["required_skill_level"] || 0) : 0;
    if (level <= 0)
        level = mapp(rule) ? (rule["required_skill_level"] || 1) : 1;
    if (level > 0)
        requirements += ({ sprintf("%s等级达到%d", query_skill_display_name(skill_id), level) });
    if (mapp(action_def) && stringp(action_def["required_breakthrough_entry"])
        && action_def["required_breakthrough_entry"] != "")
        requirements += ({ sprintf("选择突破词条 %s", action_def["required_breakthrough_entry"]) });
    return implode(requirements, "；");
}

private mapping query_action_profile_payload(object me, string action_type, string skill_id)
{
    mapping budget;
    mapping profile;

    profile = SKILL_BEHAVIOR_D->query_skill_action_profile(action_type, skill_id);
    if (!mapp(profile) || sizeof(keys(profile)) == 0)
        return ([]);
    budget = objectp(me) ? NUMERIC_RULE_D->query_action_budget_state(me, action_type, skill_id, profile) : ([]);
    return ([
        "busy": mapp(budget) ? (budget["busy"] || 0) : (profile["busy"] || 0),
        "cooldown": mapp(budget) ? (budget["cooldown"] || 0) : (profile["cooldown"] || 0),
        "resource_costs": copy(mapp(budget) ? (budget["resource_costs"] || ([ ])) : (profile["resource_costs"] || ([ ]))),
        "target_policy": profile["target_policy"] || "",
        "effects": copy(profile["effects"] || ([ ])),
        "status_effects": profile["status_effects"] || ({ }),
        "pvp_factor_bp": profile["pvp_factor_bp"] || 10000,
        "pve_factor_bp": profile["pve_factor_bp"] || 10000,
    ]);
}

private mapping *query_skill_list_action_facts(string skill_id)
{
    mapping action;
    mapping budget_spec;
    mapping detail_payload;
    mapping fact;
    mapping profile;
    mapping static_payload;
    mapping *actions;
    mapping *facts;
    string action_id;
    string action_type;
    string target_policy;

    facts = ({ });
    if (!stringp(skill_id) || skill_id == "")
        return facts;

    refresh_skill_list_static_caches();
    if (arrayp(_skill_list_action_fact_cache[skill_id]))
        return _skill_list_action_fact_cache[skill_id] + ({ });

    foreach (action_type in ({ "perform", "exert" })) {
        actions = SKILL_BEHAVIOR_D->query_skill_actions(action_type, skill_id);
        foreach (action in actions || ({ })) {
            if (!mapp(action))
                continue;
            profile = action["behavior"];
            if (!mapp(profile) || sizeof(keys(profile)) == 0)
                continue;
            action_id = action["id"];
            if (!stringp(action_id) || action_id == "")
                action_id = skill_id;
            target_policy = profile["target_policy"] || "";
            static_payload = ([
                "type": action_type,
                "id": action_id,
                "label": query_action_label(action_type, profile),
                "command": action_id == skill_id
                    ? action_type + " " + skill_id
                    : action_type + " " + skill_id + " " + action_id,
                "requirements": query_action_requirements(action_type, skill_id, action),
            ]);
            detail_payload = ([
                "usage": stringp(profile["usage"]) && profile["usage"] != ""
                    ? profile["usage"] : query_action_default_usage(action_type, target_policy),
                "target_policy": target_policy,
                "effects": copy(profile["effects"] || ([ ])),
                "status_effects": arrayp(profile["status_effects"])
                    ? profile["status_effects"] + ({ }) : ({ }),
                "pvp_factor_bp": profile["pvp_factor_bp"] || 10000,
                "pve_factor_bp": profile["pve_factor_bp"] || 10000,
                "description": stringp(profile["description"])
                    ? profile["description"] : "",
            ]);
            budget_spec = NUMERIC_RULE_D->query_action_budget_spec(
                action_type, action_id, profile);
            fact = ([
                "action_type": action_type,
                "action_id": action_id,
                "action_def": action,
                "profile": profile,
                "target_policy": target_policy,
                "static_payload": static_payload,
                "detail_payload": detail_payload,
                "budget_spec": budget_spec,
            ]);
            facts += ({ fact });
        }
    }
    _skill_list_action_fact_cache[skill_id] = facts;
    return facts + ({ });
}

private mapping query_action_intro_payload_from_fact(object me,
    string skill_id, mapping fact, int compact, mapping skill_data)
{
    mapping action_def;
    mapping access;
    mapping budget;
    mapping payload;
    mapping profile;
    mapping unlock_result;
    string action_id;
    string action_type;
    string reason;
    string target_policy;
    int current_level;

    if (!mapp(fact))
        return 0;
    action_type = fact["action_type"] || "";
    action_id = fact["action_id"] || skill_id;
    action_def = fact["action_def"];
    profile = fact["profile"];
    target_policy = fact["target_policy"] || "";

    current_level = mapp(skill_data) && intp(skill_data["level"])
        ? skill_data["level"] : -1;
    access = SKILL_BEHAVIOR_D->query_skill_action_access(me, action_type,
        skill_id, action_id, action_def,
        current_level >= 0 ? ([ "level": current_level ]) : 0);
    unlock_result = mapp(access) ? access : ([ "ok": 0, "error": "missing_action" ]);
    budget = ([]);
    if (unlock_result["ok"]) {
        budget = NUMERIC_RULE_D->query_action_budget_state_from_spec(
            me, fact["budget_spec"]);
        if (!mapp(budget) || budget["error"] == "invalid_action_budget_spec")
            budget = NUMERIC_RULE_D->query_action_budget_state(me, action_type,
                action_id, profile);
    }
    reason = query_action_unavailable_reason(unlock_result, budget, target_policy, me);
    payload = copy(fact["static_payload"] || ([ ]));
    payload["available"] = reason == "" ? 1 : 0;
    payload["unlocked"] = mapp(unlock_result) && unlock_result["ok"] ? 1 : 0;
    payload["unavailable_reason"] = reason;
    if (compact)
        return payload;
    payload += copy(fact["detail_payload"] || ([ ]));
    payload["resource_costs"] = copy(mapp(budget)
        ? (budget["resource_costs"] || ([ ]))
        : (profile["resource_costs"] || ([ ])));
    payload["cooldown"] = mapp(budget)
        ? (budget["cooldown"] || 0) : (profile["cooldown"] || 0);
    payload["cooldown_remaining"] = mapp(budget)
        ? (budget["cooldown_remaining"] || 0) : 0;
    payload["interval_remaining"] = mapp(budget)
        ? (budget["interval_remaining"] || 0) : 0;
    payload["busy"] = mapp(budget)
        ? (budget["busy"] || 0) : (profile["busy"] || 0);
    return payload;
}

private varargs mapping *query_action_intro_payloads(object me, string skill_id,
    int compact, mapping skill_data)
{
    mapping fact;
    mapping intro;
    mapping *facts;
    mapping *intros;

    intros = ({ });
    if (!stringp(skill_id) || skill_id == "")
        return intros;

    facts = query_skill_list_action_facts(skill_id);
    foreach (fact in facts || ({ })) {
        intro = query_action_intro_payload_from_fact(me, skill_id, fact,
            compact, skill_data);
        if (mapp(intro))
            intros += ({ intro });
    }
    return intros;
}

private int query_skill_list_resource(object me, string resource)
{
    mixed value;

    if (!objectp(me) || !stringp(resource) || resource == "")
        return 0;

    switch (resource) {
    case "hp":
        value = function_exists("query_hp", me)
            ? me->query_hp()
            : me->query("combat/hp");
        break;
    case "mp":
        value = function_exists("query_mp", me)
            ? me->query_mp()
            : me->query("combat/mp");
        break;
    case "ep":
        value = function_exists("query_ep", me)
            ? me->query_ep()
            : me->query("combat/ep");
        break;
    case "neili":
        value = me->query("neili");
        break;
    default:
        value = 0;
        break;
    }

    return intp(value) ? value : 0;
}

private int query_skill_list_has_combat_target(object me)
{
    if (!objectp(me))
        return 0;

    if (function_exists("query_fight_object", me) && objectp(me->query_fight_object()))
        return 1;

    return objectp(me->query_temp("fight_ob"));
}

private int query_skill_resource_threshold_satisfied(string resource, int current,
    int amount)
{
    if (!stringp(resource) || resource == "" || amount <= 0)
        return 1;

    if (resource == "hp")
        return current > amount;
    return current >= amount;
}

private varargs string query_skill_list_shape_signature(object me, mapping skills)
{
    mixed value;
    string *parts;
    string skill_id;

    if (!objectp(me))
        return "invalid";

    if (!mapp(skills))
        skills = SKILL_D->query_player_skills(me);
    if (!mapp(skills) || sizeof(skills) == 0)
        return "empty";

    parts = ({ });
    foreach (skill_id in sort_array(keys(skills), 1)) {
        value = skills[skill_id];
        if (mapp(value))
            parts += ({ sprintf("%s:%d", skill_id, value["level"] || 0) });
        else
            parts += ({ sprintf("%s:%O", skill_id, value) });
    }
    return implode(parts, "|");
}

private string query_life_skill_list_shape_signature(mapping life_skills)
{
    mapping skill_data;
    string *parts;
    string skill_id;

    if (!mapp(life_skills) || sizeof(life_skills) == 0)
        return "empty";

    parts = ({ });
    foreach (skill_id in sort_array(keys(life_skills), 1)) {
        skill_data = mapp(life_skills[skill_id]) ? life_skills[skill_id] : ([]);
        parts += ({ sprintf("%s:%d:%d", skill_id,
            skill_data["level"] || 0, skill_data["exp"] || 0) });
    }
    return implode(parts, "|");
}

private varargs mapping build_skill_list_resource_thresholds(object me,
    mapping skills)
{
    if (!objectp(me))
        return ([]);

    if (!mapp(skills))
        skills = SKILL_D->query_player_skills(me);
    return SKILL_BEHAVIOR_D->query_shared_skill_resource_thresholds(
        keys(skills || ([ ])));
}

private varargs mapping query_skill_list_resource_thresholds(object me,
    mapping skills)
{
    mapping cache;
    mapping thresholds;
    string shape_signature;
    int behavior_generation;
    int numeric_generation;

    if (!objectp(me))
        return ([]);

    shape_signature = query_skill_list_shape_signature(me, skills);
    behavior_generation = SKILL_BEHAVIOR_D->query_behavior_config_generation();
    numeric_generation = NUMERIC_RULE_D->query_numeric_config_generation();
    cache = me->query_temp("xk/skill_list_threshold_cache");
    if (mapp(cache)
        && cache["shape_signature"] == shape_signature
        && cache["behavior_generation"] == behavior_generation
        && cache["numeric_generation"] == numeric_generation
        && mapp(cache["thresholds"]))
        return copy(cache["thresholds"]);

    thresholds = build_skill_list_resource_thresholds(me, skills);
    me->set_temp("xk/skill_list_threshold_cache", ([
        "shape_signature": shape_signature,
        "behavior_generation": behavior_generation,
        "numeric_generation": numeric_generation,
        "thresholds": copy(thresholds),
    ]));
    return thresholds;
}

private int query_skill_list_resource_bucket(object me, string resource,
    int *thresholds)
{
    int amount;
    int bucket;
    int current;

    if (!objectp(me) || !stringp(resource) || resource == ""
        || !arrayp(thresholds) || sizeof(thresholds) == 0)
        return 0;

    current = query_skill_list_resource(me, resource);
    foreach (amount in thresholds) {
        if (!query_skill_resource_threshold_satisfied(resource, current, amount))
            break;
        bucket++;
    }
    return bucket;
}

private string query_skill_list_fast_signature(object me,
    mapping resource_thresholds)
{
    int *thresholds;
    string *parts;
    string resource;

    if (!objectp(me) || !mapp(resource_thresholds)
        || !NUMERIC_RULE_D->query_action_budget_runtime_is_idle(me))
        return "";

    parts = ({
        sprintf("projection_generation=%d",
            me->query_xk_skill_projection_generation()),
        sprintf("behavior_generation=%d",
            SKILL_BEHAVIOR_D->query_behavior_config_generation()),
        sprintf("numeric_generation=%d",
            NUMERIC_RULE_D->query_numeric_config_generation()),
        sprintf("source_generation=%d",
            SKILL_SOURCE_REGISTRY_D->query_source_config_generation()),
        sprintf("skill_generation=%d",
            SKILL_D->query_skill_config_generation()),
        sprintf("combat_target=%d", query_skill_list_has_combat_target(me)),
    });
    foreach (resource in sort_array(keys(resource_thresholds), 1)) {
        thresholds = resource_thresholds[resource];
        if (!arrayp(thresholds) || sizeof(thresholds) == 0)
            continue;
        parts += ({
            sprintf("%s_bucket=%d", resource,
                query_skill_list_resource_bucket(me, resource, thresholds)),
        });
    }
    return implode(sort_array(parts, 1), "|");
}

private varargs string query_skill_list_runtime_signature(object me,
    mapping skills, mapping life_skills, mapping resource_thresholds)
{
    mapping cooldown;
    mapping thresholds;
    mixed *cooldowns;
    int *threshold_values;
    string *parts;
    string resource;

    if (!objectp(me))
        return "invalid";

    parts = ({
        sprintf("projection_generation=%d",
            me->query_xk_skill_projection_generation()),
        sprintf("skill_shape=%s", query_skill_list_shape_signature(me, skills)),
        sprintf("life_skill_shape=%s",
            query_life_skill_list_shape_signature(life_skills)),
        sprintf("behavior_generation=%d",
            SKILL_BEHAVIOR_D->query_behavior_config_generation()),
        sprintf("numeric_generation=%d",
            NUMERIC_RULE_D->query_numeric_config_generation()),
        sprintf("source_generation=%d",
            SKILL_SOURCE_REGISTRY_D->query_source_config_generation()),
        sprintf("skill_generation=%d",
            SKILL_D->query_skill_config_generation()),
        sprintf("combat_target=%d", query_skill_list_has_combat_target(me)),
    });
    thresholds = mapp(resource_thresholds) ? resource_thresholds
        : query_skill_list_resource_thresholds(me, skills);
    foreach (resource in sort_array(keys(thresholds || ([])), 1)) {
        threshold_values = thresholds[resource];
        if (!arrayp(threshold_values) || sizeof(threshold_values) == 0)
            continue;
        parts += ({
            sprintf("%s_bucket=%d",
                resource,
                query_skill_list_resource_bucket(me, resource, threshold_values)),
        });
    }

    cooldowns = NUMERIC_RULE_D->query_action_budget_runtime_cooldowns(me);
    foreach (cooldown in cooldowns || ({ })) {
        string action_type;
        string skill_id;
        string action_id;

        if (!mapp(cooldown))
            continue;
        action_type = stringp(cooldown["action_type"]) ? cooldown["action_type"] : "";
        skill_id = stringp(cooldown["skill_id"]) ? cooldown["skill_id"] : "";
        action_id = stringp(cooldown["action_id"]) ? cooldown["action_id"] : "";
        parts += ({
            sprintf("cooldown=%s:%s:%s:%d:%d",
                action_type,
                skill_id,
                action_id,
                cooldown["cooldown_remaining"] || 0,
                cooldown["interval_remaining"] || 0),
        });
    }

    return implode(sort_array(parts, 1), "|");
}

private string promote_cached_skill_list_worker_state(string worker_state)
{
    return "main_thread_cached";
}

private mapping query_fast_cached_skill_list_entry(object me)
{
    mapping cache;
    string fast_signature;

    if (!objectp(me))
        return 0;

    cache = me->query_temp("xk/skill_list_cache");
    if (!mapp(cache) || !arrayp(cache["skills"])
        || !mapp(cache["resource_thresholds"])
        || !stringp(cache["fast_signature"])
        || cache["fast_signature"] == ""
        || !stringp(cache["read_side_worker_state"])
        || cache["read_side_worker_state"] == "")
        return 0;

    fast_signature = query_skill_list_fast_signature(
        me, cache["resource_thresholds"]);
    if (fast_signature == "" || cache["fast_signature"] != fast_signature)
        return 0;
    return cache;
}

private mapping query_cached_skill_list_entry(object me, string runtime_signature)
{
    mapping cache;

    if (!objectp(me))
        return 0;

    cache = me->query_temp("xk/skill_list_cache");
    if (!mapp(cache)
        || !arrayp(cache["skills"])
        || !stringp(cache["runtime_signature"])
        || cache["runtime_signature"] != runtime_signature
        || !stringp(cache["read_side_worker_state"])
        || cache["read_side_worker_state"] == "") {
        me->delete_temp("xk/skill_list_cache");
        return 0;
    }

    return cache;
}

private mapping normalize_action_group_payload(mixed cmds, string title)
{
    mapping *action_list;
    string resolved_title;
    int i;

    resolved_title = stringp(title) ? title : "";
    action_list = ({ });

    if (arrayp(cmds)) {
        /* Never normalize in-place: callers may reuse this array for another
         * projection in the same sync transaction. */
        action_list = allocate(sizeof(cmds));
        for (i = 0; i < sizeof(cmds); i++) {
            if (!mapp(cmds[i]))
                return 0;
            action_list[i] = ([
                "label": cmds[i]["label"],
                "cmd": cmds[i]["cmd"],
            ]);
            if (stringp(cmds[i]["type"]) && cmds[i]["type"] != "")
                action_list[i]["type"] = cmds[i]["type"];
            if (stringp(cmds[i]["kind"]) && cmds[i]["kind"] != "")
                action_list[i]["kind"] = cmds[i]["kind"];
            if (stringp(cmds[i]["role"]) && cmds[i]["role"] != "")
                action_list[i]["role"] = cmds[i]["role"];
            if (stringp(cmds[i]["surface"]) && cmds[i]["surface"] != "")
                action_list[i]["surface"] = cmds[i]["surface"];
            if (stringp(cmds[i]["target_group"])
                && cmds[i]["target_group"] != "")
                action_list[i]["target_group"] = cmds[i]["target_group"];
            if (stringp(cmds[i]["style"]) && cmds[i]["style"] != "")
                action_list[i]["style"] = cmds[i]["style"];
            if (intp(cmds[i]["priority"]) && cmds[i]["priority"] > 0)
                action_list[i]["priority"] = cmds[i]["priority"];
            if (cmds[i]["guided"])
                action_list[i]["guided"] = 1;
        }
    } else if (stringp(cmds)) {
        string *parts;

        if (cmds == "")
            return ([
                "title": resolved_title,
                "actions": ({ }),
            ]);

        parts = explode(cmds, "|");
        if (!arrayp(parts) || sizeof(parts) < 2)
            return 0;
        if (resolved_title == "")
            resolved_title = parts[0];
        if (sizeof(parts) == 2 && parts[1] == "")
            return ([
                "title": resolved_title,
                "actions": ({ }),
            ]);
        for (i = 1; i < sizeof(parts); i++) {
            string label;
            string cmd;

            if (sscanf(parts[i], "%s:%s", label, cmd) != 2)
                return 0;
            action_list += ({ ([
                "label": label,
                "cmd": cmd,
            ]) });
        }
    } else {
        /* Missing/unsupported input is not an authoritative empty snapshot. */
        return 0;
    }

    for (i = 0; i < sizeof(action_list); i++) {
        string label;
        string cmd;
        mapping normalized_action;

        if (!mapp(action_list[i]))
            return 0;
        label = stringp(action_list[i]["label"]) ? action_list[i]["label"] : "";
        cmd = stringp(action_list[i]["cmd"]) ? action_list[i]["cmd"] : "";

        /* L-155：动作组是受信结构化数据，不能靠调用方自觉。
         * 去掉换行/控制字符并把长度收在协议可消费范围内，
         * 避免用户正文（或拼接进来的文本）在客户端被当成可执行动作/伪造额外行。 */
        label = _xk_sanitize_action_token(label, 48);
        cmd = _xk_sanitize_action_token(cmd, 160);
        if (label == "" || cmd == "")
            return 0;
        normalized_action = ([
            "label": label,
            "cmd": cmd,
        ]);
        foreach (string field in ({
            "type", "kind", "role", "surface", "target_group", "style"
        })) {
            if (stringp(action_list[i][field]) && action_list[i][field] != "")
                normalized_action[field] = action_list[i][field];
        }
        if (intp(action_list[i]["priority"]) && action_list[i]["priority"] > 0)
            normalized_action["priority"] = action_list[i]["priority"];
        if (action_list[i]["guided"])
            normalized_action["guided"] = 1;
        action_list[i] = normalized_action;
    }

    return ([
        "title": resolved_title,
        "actions": action_list,
    ]);
}

/* 动作组 token 只允许单行可打印文本；换行/制表/回车一律去掉。 */
private string _xk_sanitize_action_token(string value, int max_length)
{
    if (!stringp(value))
        return "";
    value = replace_string(value, "\n", "");
    value = replace_string(value, "\r", "");
    value = replace_string(value, "\t", " ");
    value = trim(value);
    if (max_length > 0 && strlen(value) > max_length)
        value = value[0 .. max_length - 1];
    return value;
}

private void xk_send_action_group(object me, string packet_type,
    string payload_key, mixed cmds, string title, int priority,
    mapping response_context)
{
    mapping group_payload;
    mapping payload;
    if (!objectp(me) || !interactive(me)
        || !stringp(packet_type) || strlen(packet_type) != 4)
        return;

    group_payload = normalize_action_group_payload(cmds, title);
    if (!mapp(group_payload))
        return;

    payload = ([
        "title": group_payload["title"],
        "cols": priority > 0 ? priority : 1,
        "priority": priority > 0 ? priority : 1,
    ]);
    payload[stringp(payload_key) && payload_key != "" ? payload_key : "actions"] =
        group_payload["actions"];

    call_other(XK_PROTOCOL_CORE, "xk_send", me, packet_type, payload,
        response_context);
}

private mapping query_visible_life_skills(object me)
{
    mapping result;
    mapping def;
    mapping exp_by_skill;
    mapping level_by_skill;
    mapping skill_ids;
    string skill_id;
    int level;
    int exp;

    result = ([]);
    if (!objectp(me))
        return result;

    level_by_skill = me->query("life_skills");
    exp_by_skill = me->query("life_skills_exp");
    skill_ids = ([ ]);
    foreach (skill_id in keys(level_by_skill || ([ ])))
        if (stringp(skill_id) && skill_id != "")
            skill_ids[skill_id] = 1;
    foreach (skill_id in keys(exp_by_skill || ([ ])))
        if (stringp(skill_id) && skill_id != "")
            skill_ids[skill_id] = 1;

    foreach (skill_id in sort_array(keys(skill_ids), 1)) {
        def = LIFE_SKILL_D->query_skill_definition(skill_id);
        if (!mapp(def))
            continue;
        level = LIFE_SKILL_D->get_skill_level(me, skill_id);
        exp = me->query("life_skills_exp/" + skill_id) || 0;
        if (level <= 0 && exp <= 0)
            continue;

        result[skill_id] = ([
            "id": skill_id,
            "name": mapp(def) && stringp(def["name"]) ? def["name"] : skill_id,
            "type": mapp(def) && stringp(def["type"]) ? def["type"] : "life",
            "level": level,
            "exp": exp,
            "max_level": mapp(def) && intp(def["max_level"]) ? def["max_level"] : 100,
        ]);
    }

    return result;
}

private mapping copy_skill_list_static_entry(mapping entry)
{
    mapping result;

    result = mapp(entry) ? copy(entry) : ([ ]);
    if (arrayp(result["source_types"]))
        result["source_types"] = result["source_types"] + ({ });
    return result;
}

private void refresh_skill_list_static_caches()
{
    int behavior_generation;
    int numeric_generation;
    int source_generation;

    behavior_generation = SKILL_BEHAVIOR_D->query_behavior_config_generation();
    numeric_generation = NUMERIC_RULE_D->query_numeric_config_generation();
    source_generation = SKILL_SOURCE_REGISTRY_D->query_source_config_generation();
    if (!mapp(_skill_list_static_entry_cache)
        || !mapp(_skill_list_action_fact_cache)
        || _skill_list_static_entry_behavior_generation != behavior_generation
        || _skill_list_static_numeric_generation != numeric_generation
        || _skill_list_static_entry_source_generation != source_generation) {
        _skill_list_static_entry_cache = ([ ]);
        _skill_list_action_fact_cache = ([ ]);
        _skill_list_static_entry_behavior_generation = behavior_generation;
        _skill_list_static_numeric_generation = numeric_generation;
        _skill_list_static_entry_source_generation = source_generation;
    }
}

private mapping query_skill_list_static_entry(string skill_id)
{
    mapping entry;
    mixed *source_types;
    string category;

    if (!stringp(skill_id) || skill_id == "")
        return ([ ]);

    refresh_skill_list_static_caches();
    entry = _skill_list_static_entry_cache[skill_id];
    if (mapp(entry))
        return copy_skill_list_static_entry(entry);

    entry = ([
        "id": skill_id,
        "name": query_skill_display_name(skill_id),
        "type": NUMERIC_RULE_D->query_skill_kind(skill_id),
    ]);
    category = NUMERIC_RULE_D->query_skill_category(skill_id);
    if (stringp(category) && category != "")
        entry["category"] = category;
    source_types = query_skill_source_types(skill_id);
    if (arrayp(source_types) && sizeof(source_types) > 0)
        entry["source_types"] = source_types + ({ });
    if (query_skill_can_practice(skill_id))
        entry["can_practice"] = 1;

    /* 挂机选技过滤布尔（服务端计算，客户端纯呈现消费）。
     * 协议 type 字段是 basic/special 轴，外功/内功判定必须走 numeric_category 轴。 */
    if (SKILL_BEHAVIOR_D->query_is_outer_skill(skill_id))
        entry["can_idle_practice"] = 1;
    if (SKILL_BEHAVIOR_D->query_is_force_skill(skill_id))
        entry["is_force_skill"] = 1;

    _skill_list_static_entry_cache[skill_id] =
        copy_skill_list_static_entry(entry);
    return copy_skill_list_static_entry(entry);
}

private mapping build_skill_list_entry(object me, string skill_id, mapping skill_data)
{
    mapping action_intro;
    mapping entry;
    mapping *action_intros;
    int level;
    int max_level;

    level = mapp(skill_data) && intp(skill_data["level"]) ? skill_data["level"] : 0;
    max_level = NUMERIC_RULE_D->query_max_skill_level();
    entry = query_skill_list_static_entry(skill_id);
    entry["level"] = level;
    entry["max_level"] = max_level;

    action_intros = query_action_intro_payloads(me, skill_id, 1, skill_data);
    if (arrayp(action_intros) && sizeof(action_intros) > 0) {
        foreach (action_intro in action_intros) {
            string action_type;

            if (!mapp(action_intro) || !action_intro["unlocked"])
                continue;
            action_type = action_intro["type"] || "";
            if (action_type == "perform")
                entry["has_perform"] = 1;
            else if (action_type == "exert")
                entry["has_exert"] = 1;
        }
        entry["action_intros"] = action_intros;
    }

    return entry;
}

private mapping build_life_skill_list_entry(mapping life_info)
{
    mapping entry;

    entry = ([
        "id": life_info["id"],
        "name": life_info["name"],
        "type": life_info["type"],
        "level": life_info["level"],
        "max_level": life_info["max_level"],
    ]);

    if (intp(life_info["exp"]) && life_info["exp"] > 0)
        entry["exp"] = life_info["exp"];

    return entry;
}

private string resolve_life_skill_id(string raw_skill_id)
{
    mapping def;
    string skill_id;

    if (!stringp(raw_skill_id) || raw_skill_id == "")
        return "";

    foreach (skill_id in LIFE_SKILL_D->query_all_life_skills()) {
        def = LIFE_SKILL_D->query_skill_definition(skill_id);
        if (skill_id == raw_skill_id)
            return skill_id;
        if (mapp(def) && stringp(def["name"]) && def["name"] == raw_skill_id)
            return skill_id;
    }

    return "";
}

private mixed *query_skill_source_types(string skill_id)
{
    if (!stringp(skill_id) || skill_id == "")
        return ({ });

    return SKILL_SOURCE_REGISTRY_D->query_skill_source_types(skill_id);
}

private mapping *query_skill_source_entries_payload(string skill_id)
{
    mapping source;
    mapping sources;
    mapping *entries;
    string source_id;
    mixed teach_skills;

    entries = ({ });
    if (!stringp(skill_id) || skill_id == "")
        return entries;

    sources = SKILL_SOURCE_REGISTRY_D->query_all_sources();
    foreach (source_id, source in sources || ([])) {
        if (!mapp(source))
            continue;
        teach_skills = source["teach_skills"];
        if (source["skill_id"] != skill_id
            && (!arrayp(teach_skills) || member_array(skill_id, teach_skills) == -1))
            continue;
        entries += ({ ([
            "id": source_id,
            "source_type": source["source_type"] || "",
            "provider_id": source["provider_id"] || "",
            "manual_id": source["manual_id"] || "",
            "organization_type": source["organization_type"] || "none",
        ]) });
    }
    return entries;
}

private mapping query_skill_source_payload(string skill_id)
{
    mapping payload;
    mapping practice_source;
    mapping teacher_source;
    mapping *source_entries;

    if (!stringp(skill_id) || skill_id == "")
        return ([]);

    teacher_source = SKILL_SOURCE_REGISTRY_D->query_any_teacher_source(skill_id);
    practice_source = SKILL_SOURCE_REGISTRY_D->query_practice_source(skill_id);
    source_entries = query_skill_source_entries_payload(skill_id);
    payload = ([
        "source_types": query_skill_source_types(skill_id),
        "source_count": sizeof(source_entries),
        "sources": source_entries,
        "teacher_source": mapp(teacher_source) ? (teacher_source["id"] || "") : "",
        "practice_source": mapp(practice_source) ? (practice_source["id"] || "") : "",
        "teacher_max_skill": mapp(teacher_source) ? (teacher_source["max_skill"] || 0) : 0,
        "practice_requires_enable": mapp(practice_source) ? (practice_source["requires_enable"] || 0) : 0,
    ]);
    return payload;
}

private mapping query_skill_unlock_entry_payload(object me, string unlock_type, string skill_id)
{
    mapping result;
    mapping rule;
    mapping state;

    if (!objectp(me) || !stringp(unlock_type) || unlock_type == ""
        || !stringp(skill_id) || skill_id == "")
        return ([]);

    rule = SKILL_UNLOCK_REGISTRY_D->query_unlock_rule(unlock_type, skill_id);
    state = SKILL_UNLOCK_D->query_unlock_state(me, unlock_type, skill_id);
    result = SKILL_UNLOCK_D->can_unlock(me, unlock_type, skill_id);
    return ([
        "has_rule": mapp(rule) ? 1 : 0,
        "required_skill_level": mapp(rule) ? (rule["required_skill_level"] || 0) : 0,
        "unlocked": mapp(state) && state["unlocked"] ? 1 : 0,
        "can_unlock": mapp(result) && result["ok"] ? 1 : 0,
        "error": mapp(result) ? (result["error"] || "") : "",
    ]);
}

private mapping query_skill_unlock_payload(object me, string skill_id)
{
    mapping payload;

    if (!objectp(me) || !stringp(skill_id) || skill_id == "")
        return ([]);

    payload = ([
        "perform": query_skill_unlock_available(me, "perform", skill_id),
        "exert": query_skill_unlock_available(me, "exert", skill_id),
        "exert_state": query_skill_unlock_entry_payload(me, "exert", skill_id),
    ]);
    if (mapp(SKILL_UNLOCK_REGISTRY_D->query_unlock_rule("perform", skill_id)))
        payload["perform_state"] = query_skill_unlock_entry_payload(me, "perform", skill_id);
    return payload;
}

private int query_skill_can_practice(string skill_id)
{
    mapping source;

    if (!stringp(skill_id) || skill_id == "")
        return 0;
    source = SKILL_SOURCE_REGISTRY_D->query_practice_source(skill_id);
    return mapp(source) ? 1 : 0;
}

private mapping query_skill_activation_payload(object me, string skill_id)
{
    if (!objectp(me) || !stringp(skill_id) || skill_id == "")
        return ([]);
    return SKILL_BEHAVIOR_D->query_skill_activation_options(me, skill_id);
}

private mapping query_skill_breakthrough_payload(object me, string skill_id)
{
    mapping breakthrough;
    mapping payload;

    if (!objectp(me) || !stringp(skill_id) || skill_id == "")
        return ([]);

    breakthrough = NUMERIC_RULE_D->query_skill_breakthrough_state(me, skill_id);
    if (!mapp(breakthrough) || !breakthrough["ok"])
        return breakthrough;

    payload = ([
        "ok": 1,
        "skill_id": skill_id,
        "kind": breakthrough["kind"],
        "level": breakthrough["level"],
        "tier": breakthrough["tier"],
        "unlocked_tier": breakthrough["unlocked_tier"],
        "interval": breakthrough["interval"],
        "max_tier": breakthrough["max_tier"],
        "release_max_tier": breakthrough["release_max_tier"],
        "next_level": breakthrough["next_level"],
        "selected": breakthrough["selected"],
        "entries": breakthrough["entries"],
        "available": breakthrough["available"],
        "pending_count": breakthrough["pending_count"],
    ]);
    return payload;
}

private string query_skill_primary_command(object me, string skill_id)
{
    mapping attack_profile;

    if (!objectp(me) || !stringp(skill_id) || skill_id == "")
        return "";

    if (query_skill_unlock_available(me, "perform", skill_id))
        return "perform " + skill_id;
    if (query_skill_unlock_available(me, "exert", skill_id))
        return "exert " + skill_id;

    attack_profile = SKILL_BEHAVIOR_D->query_basic_attack_profile(me, skill_id);
    if (mapp(attack_profile) && sizeof(keys(attack_profile)) > 0)
        return "activate " + skill_id;

    return "";
}

private int query_skill_unlock_available(object me, string unlock_type, string skill_id)
{
    mapping action;
    mapping access;
    mapping *actions;

    if (!objectp(me) || !stringp(unlock_type) || unlock_type == "" || !stringp(skill_id) || skill_id == "")
        return 0;

    actions = SKILL_BEHAVIOR_D->query_skill_actions(unlock_type, skill_id);
    foreach (action in actions || ({ })) {
        if (!mapp(action))
            continue;
        access = SKILL_BEHAVIOR_D->query_skill_action_access(me, unlock_type, skill_id, action["id"], action);
        if (mapp(access) && access["ok"])
            return 1;
    }
    return 0;
}

varargs void xk_send_quick_cmds(object me, mixed cmds, int priority,
    mapping response_context)
{
    xk_send_action_group(me, XK_ACT1, "actions", cmds, "", priority,
        response_context);
}

varargs void xk_send_secondary_cmds(object me, mixed cmds, string title,
    int priority, mapping response_context)
{
    xk_send_action_group(me, XK_ACT2, "actions", cmds, title, priority,
        response_context);
}

varargs void xk_send_dynamic_buttons(object me, mixed cmds, string title,
    int priority, mapping response_context)
{
    xk_send_action_group(me, XK_BTNS, "buttons", cmds, title, priority,
        response_context);
}

varargs void xk_send_skill_list(object me, mapping response_context)
{
    mapping payload;

    if (!me || !interactive(me))
        return;

    payload = build_skill_list_payload(me);
    if (mapp(payload)) {
        payload["active_slots"] = query_active_skill_slots_payload(me);
        call_other(XK_PROTOCOL_CORE, "xk_send", me, XK_SKLS, payload,
            response_context);
        return;
    }
}

private mapping build_skill_list_payload(object me)
{
    mapping cache;
    mapping life_skills;
    mapping life_info;
    mapping resource_thresholds;
    mapping skills;
    mapping *skill_list;
    string *skill_names;
    string *life_skill_ids;
    string fast_signature;
    string runtime_signature;
    string worker_state;
    int index;
    int skill_count;

    if (!objectp(me) || !interactive(me))
        return 0;

    cache = query_fast_cached_skill_list_entry(me);
    if (mapp(cache)) {
        worker_state = promote_cached_skill_list_worker_state(
            cache["read_side_worker_state"]);
        /* L-65：主线程缓存命中不是读侧 worker 原生执行；read_side_worker
         * 保持 0，真实执行标识只由原生 worker 结果给出。 */
        return ([
            "skills": cache["skills"],
            "read_side_worker": 0,
            "read_side_worker_state": worker_state,
        ]);
    }

    skills = SKILL_D->query_player_skills(me);
    life_skills = query_visible_life_skills(me);
    resource_thresholds = query_skill_list_resource_thresholds(me, skills);
    runtime_signature = query_skill_list_runtime_signature(
        me, skills, life_skills, resource_thresholds);
    fast_signature = query_skill_list_fast_signature(me, resource_thresholds);
    cache = query_cached_skill_list_entry(me, runtime_signature);
    if (mapp(cache)) {
        cache["resource_thresholds"] = copy(resource_thresholds);
        cache["fast_signature"] = fast_signature;
        me->set_temp("xk/skill_list_cache", cache);
        worker_state = promote_cached_skill_list_worker_state(
            cache["read_side_worker_state"]);
        /* L-65：同 fast cache 分支，缓存命中不发布 worker 执行身份。 */
        return ([
            "skills": cache["skills"],
            "read_side_worker": 0,
            "read_side_worker_state": worker_state,
        ]);
    }

    worker_state = "main_thread";
    if ((!mapp(skills) || sizeof(skills) == 0)
        && (!mapp(life_skills) || sizeof(life_skills) == 0)) {
        me->set_temp("xk/skill_list_cache", ([
            "at": time(),
            "runtime_signature": runtime_signature,
            "resource_thresholds": copy(resource_thresholds),
            "fast_signature": fast_signature,
            "read_side_worker_state": worker_state,
            "skills": ({ }),
        ]));
        return ([
            "skills": ({ }),
            "read_side_worker": 0,
            "read_side_worker_state": worker_state,
        ]);
    }

    skill_names = mapp(skills) ? keys(skills) : ({ });
    life_skill_ids = mapp(life_skills) ? sort_array(keys(life_skills), 1) : ({ });
    skill_list = allocate(sizeof(skill_names) + sizeof(life_skill_ids));
    skill_count = 0;
    if (mapp(skills) && sizeof(skills) > 0) {
        for (index = 0; index < sizeof(skill_names); index++) {
            string skill_id;

            skill_id = skill_names[index];
            skill_list[skill_count++] = build_skill_list_entry(me, skill_id, skills[skill_id]);
        }
    }

    if (mapp(life_skills) && sizeof(life_skills) > 0) {
        foreach (string life_skill_id in life_skill_ids) {
            life_info = life_skills[life_skill_id];
            skill_list[skill_count++] = build_life_skill_list_entry(life_info);
        }
    }

    if (skill_count < sizeof(skill_list))
        skill_list = skill_count > 0 ? skill_list[0..skill_count - 1] : ({ });
    me->set_temp("xk/skill_list_cache", ([
        "at": time(),
        "runtime_signature": runtime_signature,
        "resource_thresholds": copy(resource_thresholds),
        "fast_signature": fast_signature,
        "read_side_worker_state": worker_state,
        "skills": skill_list,
    ]));
    return ([
        "skills": skill_list,
        "read_side_worker": 0,
        "read_side_worker_state": worker_state,
    ]);
}

void xk_prime_skill_list_cache(object me)
{
    if (!objectp(me) || !interactive(me))
        return;

    build_skill_list_payload(me);
}

varargs void xk_send_skill_info(object me, string skill_id,
    mapping response_context)
{
    mapping def;
    mapping breakthrough;
    mapping status;
    mapping skill_data;
    mapping *action_intros;
    string life_skill_id;
    int level;
    int exp;
    int cap;
    string safe_skill_id;
    string player_description;
    if (!me || !interactive(me) || !stringp(skill_id) || skill_id == "")
        return;

    life_skill_id = resolve_life_skill_id(skill_id);
    if (life_skill_id != "") {
        def = LIFE_SKILL_D->query_skill_definition(life_skill_id);
        level = LIFE_SKILL_D->get_skill_level(me, life_skill_id);
        exp = LIFE_SKILL_D->get_skill_exp(me, life_skill_id);
        cap = mapp(def) && intp(def["max_level"]) ? def["max_level"] : 0;
        call_other(XK_PROTOCOL_CORE, "xk_send", me, XK_SKLI, ([
            "id": life_skill_id,
            "name": mapp(def) && stringp(def["name"]) ? def["name"] : life_skill_id,
            "type": mapp(def) && stringp(def["type"]) ? def["type"] : "life",
            "level": level,
            "exp": exp,
            "cap": cap,
            "next_cost": 0,
            /* L-66：进度阈值取生活技能 owner；满级不再发“可继续进度”。 */
            "max_exp": mapp(def) && cap > 0 && level < cap
                ? LIFE_SKILL_D->query_skill_level_exp_threshold() : 0,
            "description": "生活技能随实际采集、制造等行为积累经验。",
            /* L-66：只描述实际可用能力，不写开发过程与未接入能力承诺。 */
            "detail_description": mapp(def)
                ? "可用行为与收益由该生活技能的内容定义提供。"
                : "当前没有该生活技能的内容定义，暂无可执行行为。",
            "damage": "",
            "cost": "",
            "cooldown": 0,
            "requirements": "",
            "command": "",
            "can_practice": 0,
            "activation_options": ([ ]),
        ]), response_context);
        return;
    }

    level = SKILL_D->query_player_skill_level(me, skill_id);
    safe_skill_id = skill_id;
    def = SKILL_D->get_skill_definition(safe_skill_id);
    player_description = mapp(def) && stringp(def["description"])
        ? def["description"] : "";
    status = NUMERIC_RULE_D->query_skill_train_status(me, safe_skill_id);
    skill_data = SKILL_D->get_player_skill_data(me, safe_skill_id);
    breakthrough = query_skill_breakthrough_payload(me, safe_skill_id);
    action_intros = query_action_intro_payloads(me, safe_skill_id);

    call_other(XK_PROTOCOL_CORE, "xk_send", me, XK_SKLI, ([
        "id": safe_skill_id,
        "name": query_skill_display_name(safe_skill_id),
        "type": mapp(status) ? (status["kind"] || "combat") : "combat",
        "category": mapp(status) ? (status["category"] || "") : "",
        "level": level,
        "cap": mapp(status) ? (status["cap"] || 0) : 0,
        "next_cost": mapp(status) ? (status["next_cost"] || 0) : 0,
        "breakthrough_tier": mapp(status) ? (status["breakthrough_tier"] || 0) : 0,
        "next_breakthrough_level": mapp(status) ? (status["next_breakthrough_level"] || 0) : 0,
        "breakthrough": breakthrough,
        "breakthrough_selected": mapp(breakthrough) ? (breakthrough["selected"] || ({ })) : ({ }),
        "breakthrough_choices": mapp(breakthrough) ? (breakthrough["available"] || ({ })) : ({ }),
        "breakthrough_pending_count": mapp(breakthrough) ? (breakthrough["pending_count"] || 0) : 0,
        "breakthrough_entries": mapp(breakthrough) ? (breakthrough["entries"] || ({ })) : ({ }),
        /* L-66：经验余量取技能 owner 的持久记录，阈值取 owner 常量；满级与
         * 未注册技能不发进度（max_exp=0 → 客户端不显示进度行），不再按
         * level 推算 level×10/(level+1)×10。 */
        "exp": mapp(skill_data) ? (skill_data["exp"] || 0) : 0,
        "max_exp": mapp(skill_data)
            && level < (mapp(status) ? (status["cap"] || 0) : 0)
            ? SKILL_D->query_skill_level_exp_threshold() : 0,
        "description": player_description,
        "detail_description": "",
        "damage": "",
        "cost": "",
        "cooldown": 0,
        "requirements": "",
        "source": query_skill_source_payload(safe_skill_id),
        "source_types": query_skill_source_types(safe_skill_id),
        "skill_intro": ([
            "title": query_skill_display_name(safe_skill_id),
            "summary": player_description != "" ? ""
                : "这门武学的主动能力已在下方列出，消耗与冷却均按当前运行状态计算。",
            "type": mapp(status) ? (status["kind"] || "combat") : "combat",
            "category": mapp(status) ? (status["category"] || "") : "",
            "level": level,
            "cap": mapp(status) ? (status["cap"] || 0) : 0,
            "source_types": query_skill_source_types(safe_skill_id),
        ]),
        "action_intros": action_intros,
        "unlocks": query_skill_unlock_payload(me, safe_skill_id),
        "perform_available": query_skill_unlock_available(me, "perform", safe_skill_id),
        "exert_available": query_skill_unlock_available(me, "exert", safe_skill_id),
        "perform_profile": query_action_profile_payload(me, "perform", safe_skill_id),
        "exert_profile": query_action_profile_payload(me, "exert", safe_skill_id),
        "basic_attack": SKILL_BEHAVIOR_D->query_basic_attack_profile(me, safe_skill_id),
        "can_practice": query_skill_can_practice(safe_skill_id),
        "activation_options": query_skill_activation_payload(me, safe_skill_id),
        "command": query_skill_primary_command(me, safe_skill_id),
    ]), response_context);
}

private int query_status_int(object me, string key, int fallback)
{
    mixed value;

    value = me->query(key);
    return intp(value) ? value : fallback;
}

private string query_status_name(object me)
{
    string name;

    name = call_other("/kernel/simul_efun", "get_object_name", me);
    return stringp(name) ? name : "";
}

private int query_positive_status_int(object me, string primary, string fallback, int default_value)
{
    int value;

    value = query_status_int(me, primary, 0);
    if (value > 0)
        return value;
    value = query_status_int(me, fallback, 0);
    if (value > 0)
        return value;
    return default_value;
}

/* STAT 时辰字段格式单源（两个载荷函数共用，防双写遗漏）。 */
private string query_shichen_text()
{
    return TIME_D->query_hour_name() + "时";
}

varargs void xk_send_status_light(object me, mapping response_context)
{
    mapping maxima;
    int hp;
    int max_hp;
    int eff_hp;
    int mp;
    int max_mp;
    int eff_mp;
    int ep;
    int max_ep;
    int neili;
    int max_neili;

    if (!me || !interactive(me))
        return;

    maxima = function_exists("query_vital_maxima_snapshot", me)
        ? me->query_vital_maxima_snapshot() : ([]);
    hp = query_status_int(me, "combat/hp", 0);
    max_hp = mapp(maxima) && intp(maxima["hp"]) ? maxima["hp"] : 0;
    if (max_hp <= 0)
        max_hp = query_status_int(me, "combat/max_hp", 100);
    if (max_hp <= 0)
        max_hp = 100;
    eff_hp = me->query("eff_qi");
    if (!intp(eff_hp) || eff_hp <= 0)
        eff_hp = max_hp;
    if (eff_hp > max_hp)
        eff_hp = max_hp;
    if (eff_hp < hp)
        eff_hp = hp;

    mp = query_status_int(me, "combat/mp", 0);
    max_mp = mapp(maxima) && intp(maxima["mp"]) ? maxima["mp"] : 0;
    if (max_mp <= 0)
        max_mp = query_status_int(me, "combat/max_mp", 100);
    if (max_mp <= 0)
        max_mp = 100;
    eff_mp = me->query("eff_jing");
    if (!intp(eff_mp) || eff_mp <= 0)
        eff_mp = max_mp;
    if (eff_mp > max_mp)
        eff_mp = max_mp;
    if (eff_mp < mp)
        eff_mp = mp;

    ep = query_status_int(me, "combat/ep", 0);
    max_ep = mapp(maxima) && intp(maxima["ep"]) ? maxima["ep"] : 0;
    if (max_ep <= 0)
        max_ep = query_status_int(me, "combat/max_ep", 100);
    if (max_ep <= 0)
        max_ep = 100;

    neili = query_status_int(me, "neili", 0);
    max_neili = query_status_int(me, "max_neili", 0);

    call_other(XK_PROTOCOL_CORE, "xk_send", me, XK_STAT, ([
        "partial": 1,
        "potential": query_player_potential(me),
        "age": query_player_age(me),
        "name": query_status_name(me),
        /* 时辰随高频状态包附载（格式单源 query_shichen_text）。 */
        "shichen": query_shichen_text(),
        "hp": hp,
        "max_hp": max_hp,
        "eff_hp": eff_hp,
        "mp": mp,
        "max_mp": max_mp,
        "eff_mp": eff_mp,
        "ep": ep,
        "max_ep": max_ep,
        "neili": neili,
        "max_neili": max_neili,
        "str": query_positive_status_int(me, "attributes/strength", "str", 10),
        "dex": query_positive_status_int(me, "attributes/dexterity", "dex", 10),
        "con": query_positive_status_int(me, "attributes/constitution", "con", 10),
        "int": query_positive_status_int(me, "attributes/intelligence", "int", 10),
        "timestamp": time(),
    ]), response_context);
}

varargs int xk_send_status(object me, mapping response_context)
{
    string name;
    int hp;
    int max_hp;
    int eff_hp;
    int mp;
    int max_mp;
    int eff_mp;
    int ep;
    int max_ep;
    int neili;
    int max_neili;
    int progression_rank;
    int exp;
    int skill_cap;
    mapping combat_attrs;
    mapping vital_maxima;
    mapping progression;
    int str;
    int dex;
    int con;
    int int_val;

    if (!me || !interactive(me))
        return 0;

    name = query_status_name(me);
    combat_attrs = NUMERIC_RULE_D->query_derived_combat_attributes(me);
    if (function_exists("cache_vital_maxima_snapshot", me))
        call_other(me, "cache_vital_maxima_snapshot", combat_attrs);
    vital_maxima = function_exists("query_vital_maxima_snapshot", me)
        ? me->query_vital_maxima_snapshot() : ([]);
    hp = me->query("combat/hp");
    if (!intp(hp))
        hp = 0;
    max_hp = mapp(vital_maxima) ? vital_maxima["hp"] : combat_attrs["max_hp"];
    if (!intp(max_hp))
        max_hp = 0;
    if (!intp(max_hp) || max_hp <= 0)
        max_hp = me->query("combat/max_hp");
    if (!intp(max_hp) || max_hp <= 0)
        max_hp = 100;
    eff_hp = me->query("eff_qi");
    if (!intp(eff_hp) || eff_hp <= 0)
        eff_hp = max_hp;
    if (eff_hp > max_hp)
        eff_hp = max_hp;
    if (eff_hp < hp)
        eff_hp = hp;

    mp = me->query("combat/mp");
    if (!intp(mp))
        mp = 0;
    max_mp = mapp(vital_maxima) ? vital_maxima["mp"] : combat_attrs["max_mp"];
    if (!intp(max_mp))
        max_mp = 0;
    if (!intp(max_mp) || max_mp <= 0)
        max_mp = me->query("combat/max_mp");
    if (!intp(max_mp) || max_mp <= 0)
        max_mp = 100;
    eff_mp = me->query("eff_jing");
    if (!intp(eff_mp) || eff_mp <= 0)
        eff_mp = max_mp;
    if (eff_mp > max_mp)
        eff_mp = max_mp;
    if (eff_mp < mp)
        eff_mp = mp;

    ep = me->query("combat/ep");
    if (!intp(ep))
        ep = 0;
    max_ep = mapp(vital_maxima) ? vital_maxima["ep"] : combat_attrs["max_ep"];
    if (!intp(max_ep))
        max_ep = 0;
    if (!intp(max_ep) || max_ep <= 0)
        max_ep = me->query("combat/max_ep");
    if (!intp(max_ep) || max_ep <= 0)
        max_ep = 100;

    neili = me->query("neili");
    if (!intp(neili))
        neili = 0;
    max_neili = me->query("max_neili");
    if (!intp(max_neili))
        max_neili = 0;

    exp = me->query("combat_exp");
    if (!intp(exp))
        exp = 0;
    progression = NUMERIC_RULE_D->query_player_progression_snapshot(me);
    progression_rank = mapp(progression) ? progression["progression_rank"] : 1;
    skill_cap = mapp(progression) ? progression["skill_cap"] : 0;

    str = me->query("attributes/strength");
    if (!intp(str) || str <= 0)
        str = me->query("str");
    if (!intp(str) || str <= 0)
        str = 10;

    dex = me->query("attributes/dexterity");
    if (!intp(dex) || dex <= 0)
        dex = me->query("dex");
    if (!intp(dex) || dex <= 0)
        dex = 10;

    con = me->query("attributes/constitution");
    if (!intp(con) || con <= 0)
        con = me->query("con");
    if (!intp(con) || con <= 0)
        con = 10;

    int_val = me->query("attributes/intelligence");
    if (!intp(int_val) || int_val <= 0)
        int_val = me->query("int");
    if (!intp(int_val) || int_val <= 0)
        int_val = 10;

    return call_other(XK_PROTOCOL_CORE, "xk_send", me, XK_STAT, ([
        "name": name,
        /* 全量版同源附载（格式单源 query_shichen_text）。 */
        "shichen": query_shichen_text(),
        "hp": hp,
        "max_hp": max_hp,
        "eff_hp": eff_hp,
        "mp": mp,
        "max_mp": max_mp,
        "eff_mp": eff_mp,
        "ep": ep,
        "max_ep": max_ep,
        "neili": neili,
        "max_neili": max_neili,
        "skill_cap": skill_cap,
        "progression_rank": progression_rank,
        "potential": query_player_potential(me),
        "age": query_player_age(me),
        "exp": exp,
        "str": str,
        "dex": dex,
        "con": con,
        "int": int_val,
        "attack": combat_attrs["attack"] || 0,
        "defense": combat_attrs["defense"] || 0,
        "parry": combat_attrs["parry"] || 0,
        "hit": combat_attrs["hit"] || 0,
        "dodge": combat_attrs["dodge"] || 0,
        "critical": combat_attrs["critical"] || 0,
        "critical_resist": combat_attrs["critical_resist"] || 0,
        "armor_break": combat_attrs["armor_break"] || 0,
        "attack_speed": combat_attrs["attack_speed"] || 0,
        "timestamp": time(),
    ]), response_context);
}

/* 潜能/年龄单源：两个 STAT 载荷函数共用，防止字段双写遗漏。 */
private int query_player_potential(object me)
{
    mapping progression;

    if (!objectp(me))
        return 0;
    progression = NUMERIC_RULE_D->query_player_progression_snapshot(me);
    return (mapp(progression) && intp(progression["potential"])) ? progression["potential"] : 0;
}

private int query_player_age(object me)
{
    if (!objectp(me))
        return 18;
    return 18 + TIME_D->query_day() / 365;
}
