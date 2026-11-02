/**
 * @file    skill_behavior_d.c
 * @brief   技能行为定义真源,持有技能类型到行为/动作的映射。
 */

#pragma strict_types
#pragma save_types

#include <framework/message_facade.h>
#include <game_constants.h>
#include <system/services.h>

inherit "/lib/framework/daemon";

nosave mapping _behavior_definitions;
nosave mapping _action_definitions;
nosave mapping _actions_by_skill_type;
nosave mapping _resource_thresholds_by_skill_shape;
nosave int _resource_thresholds_numeric_generation;
nosave int _behavior_config_generation;

#define BP_BASE 10000

private string query_fallback_basic_attack_skill(object player);
private string *query_unarmed_active_skill_candidates(object player);
private mapping query_persisted_active_skill_state(object player);
string query_active_skill_for_slot(object player, string slot);
private mapping build_combat_skill_selection(object player);
private int can_activate_skill_slot(object player, string skill_id, string slot);
private string query_action_effect_name(mapping action, string skill_id);
private int query_player_resource_max(object player, string resource);
private void record_perform_phase(object player, string phase, int wall_started_at,
    int cpu_started_at);

private void record_perform_phase(object player, string phase, int wall_started_at,
    int cpu_started_at)
{
    int cpu_finished_at;

    cpu_finished_at = thread_cpu_time_ns();
    GATEWAY_TELEMETRY_D->record_gateway_combat_phase_timing(
        player, phase, perf_counter_ns() - wall_started_at,
        cpu_started_at >= 0 && cpu_finished_at >= cpu_started_at
            ? cpu_finished_at - cpu_started_at : -1);
}

private void clear_exert_bonus(object player)
{
    if (!objectp(player))
        return;

    player->delete_temp("combat/exert/effects");
    player->delete_temp("combat/exert/skill");
    player->delete_temp("combat/exert/action");
}

private void clear_perform_bonus(object player)
{
    if (!objectp(player))
        return;

    player->delete_temp("combat/perform/effects");
    player->delete_temp("combat/perform/skill");
    player->delete_temp("combat/perform/action");
}

private mapping query_behavior_config()
{
    mapping config;

    config = CONFIG_D->query_full_config("skill_catalog");
    return mapp(config) ? config : ([]);
}

private string action_index_key(string action_type, string skill_id)
{
    return action_type + ":" + skill_id;
}

private int query_action_required_level(mapping action)
{
    mapping unlock;

    if (!mapp(action))
        return 0;
    unlock = mapp(action["unlock"]) ? action["unlock"] : ([ ]);
    return action["required_skill_level"] || unlock["required_skill_level"] || 1;
}

private int compare_skill_action_order(mapping left, mapping right)
{
    string left_id;
    string right_id;
    int level_delta;

    level_delta = query_action_required_level(left)
        - query_action_required_level(right);
    if (level_delta != 0)
        return level_delta;
    left_id = mapp(left) && stringp(left["id"]) ? left["id"] : "";
    right_id = mapp(right) && stringp(right["id"]) ? right["id"] : "";
    return strcmp(left_id, right_id);
}

private void rebuild_action_index()
{
    mapping action;
    string action_key;
    string action_type;
    string index_key;
    string skill_id;

    _actions_by_skill_type = ([ ]);
    if (!mapp(_action_definitions))
        return;

    foreach (action_key, action in _action_definitions) {
        if (!mapp(action))
            continue;
        action_type = action["action_type"] || "";
        skill_id = action["skill_id"] || "";
        if (action_type == "" || skill_id == "")
            continue;

        index_key = action_index_key(action_type, skill_id);
        if (!arrayp(_actions_by_skill_type[index_key]))
            _actions_by_skill_type[index_key] = ({ });
        _actions_by_skill_type[index_key] += ({ action });
    }
    foreach (index_key in keys(_actions_by_skill_type))
        _actions_by_skill_type[index_key] = sort_array(
            _actions_by_skill_type[index_key], (: compare_skill_action_order :));
}

private void add_shared_resource_threshold(mapping thresholds, string resource,
    int amount)
{
    int *values;

    if (!mapp(thresholds) || !stringp(resource) || resource == "" || amount <= 0)
        return;
    values = arrayp(thresholds[resource]) ? thresholds[resource] : ({ });
    if (member_array(amount, values) == -1)
        thresholds[resource] = sort_array(values + ({ amount }), 1);
}

private mapping copy_shared_resource_thresholds(mapping thresholds)
{
    mapping result;
    mixed values;
    string resource;

    result = ([]);
    foreach (resource, values in thresholds || ([]))
        result[resource] = arrayp(values) ? values + ({ }) : ({ });
    return result;
}

private void load_behavior_config()
{
    mapping config;

    config = query_behavior_config();
    _behavior_definitions = mapp(config["behaviors"]) ? copy(config["behaviors"]) : ([]);
    _action_definitions = mapp(config["actions"]) ? copy(config["actions"]) : ([]);
    rebuild_action_index();
    _resource_thresholds_by_skill_shape = ([ ]);
    _behavior_config_generation++;
}

void on_config_reloaded(string key, string config_name)
{
    if (key != "skill_catalog" && config_name != "skill_catalog")
        return;

    load_behavior_config();
}

void on_config_reload_rollback(string key, string config_name)
{
    if (key != "skill_catalog" && config_name != "skill_catalog")
        return;

    load_behavior_config();
    if (_behavior_config_generation > 1)
        _behavior_config_generation -= 2;
    if (_behavior_config_generation < 1)
        _behavior_config_generation = 1;
}

void create()
{
    ::create();
    if (!mapp(_behavior_definitions))
        _behavior_definitions = ([]);
    if (!mapp(_action_definitions))
        _action_definitions = ([]);
    if (!mapp(_actions_by_skill_type))
        _actions_by_skill_type = ([]);
    if (!mapp(_resource_thresholds_by_skill_shape))
        _resource_thresholds_by_skill_shape = ([]);
    load_behavior_config();
    CONFIG_D->compat_register_config_callback("skill_catalog", "on_config_reloaded", this_object());
}

private int has_basic_attack_profile(string skill_id)
{
    mapping behavior;
    mapping profile;

    if (!stringp(skill_id) || skill_id == "")
        return 0;

    behavior = _behavior_definitions[skill_id];
    if (!mapp(behavior))
        return 0;

    profile = behavior["basic_attack"];
    return mapp(profile) && sizeof(keys(profile)) > 0;
}

private string query_activation_category_name(string category)
{
    mapping definition;
    string name;

    if (!stringp(category) || category == "")
        return "当前武器招式";
    definition = NUMERIC_RULE_D->query_skill_definition(category);
    name = mapp(definition) && stringp(definition["name"])
        ? definition["name"] : category;
    if (strsrch(name, "基本") == 0 && strlen(name) > strlen("基本"))
        name = name[strlen("基本")..];
    return name;
}

private string query_equipped_weapon_skill_type(object player)
{
    object weapon;
    mapping effects;
    string skill_type;

    if (!objectp(player))
        return "";
    weapon = player->query_temp("weapon");
    if (!objectp(weapon))
        return "";
    skill_type = function_exists("query_skill_type", weapon)
        ? weapon->query_skill_type() : weapon->query("skill_type");
    if (stringp(skill_type) && skill_type != "")
        return skill_type;
    effects = weapon->query("effects");
    return mapp(effects) && stringp(effects["skill"]) ? effects["skill"] : "";
}

private int is_unarmed_skill_category(string category)
{
    return member_array(category, ({
        "cuff", "palm", "finger", "claw", "leg"
    })) != -1;
}

private int skill_matches_equipped_weapon(object player, string skill_id)
{
    string category;
    string weapon_type;

    if (!objectp(player) || !stringp(skill_id) || skill_id == "")
        return 0;
    category = NUMERIC_RULE_D->query_skill_category(skill_id);
    weapon_type = query_equipped_weapon_skill_type(player);
    if (weapon_type == "")
        return is_unarmed_skill_category(category);
    return weapon_type == category;
}

private string *query_unarmed_active_skill_candidates(object player)
{
    mapping state;
    mapping outer;
    mixed raw_skill_id;
    string category;
    string skill_id;
    string *result;

    result = ({ });
    if (!objectp(player))
        return result;

    state = query_persisted_active_skill_state(player);
    outer = mapp(state["outer"]) ? state["outer"] : ([ ]);
    foreach (category, raw_skill_id in outer) {
        skill_id = stringp(raw_skill_id) ? raw_skill_id : "";
        if (skill_id == "")
            continue;
        category = NUMERIC_RULE_D->query_skill_category(skill_id) || category;
        if (!is_unarmed_skill_category(category)
            || SKILL_D->query_player_skill_level(player, skill_id) <= 0
            || !has_basic_attack_profile(skill_id)
            || !skill_matches_equipped_weapon(player, skill_id))
            continue;
        if (member_array(skill_id, result) == -1)
            result += ({ skill_id });
    }

    return result;
}

int is_active_skill_for_slot(object player, string slot, string skill_id)
{
    string category;

    if (!objectp(player) || !stringp(slot) || slot == ""
        || !stringp(skill_id) || skill_id == "")
        return 0;
    if (slot == "outer") {
        category = NUMERIC_RULE_D->query_skill_category(skill_id);
        if (is_unarmed_skill_category(category))
            return member_array(skill_id,
                query_unarmed_active_skill_candidates(player)) != -1;
    }
    return query_active_skill_for_slot(player, slot) == skill_id;
}

private mapping query_persisted_active_skill_state(object player)
{
    mixed state;

    if (!objectp(player))
        return ([]);
    state = player->query("combat/active_skills");
    return mapp(state) ? restore_variable(save_variable(state)) : ([]);
}

string query_active_skill_for_slot(object player, string slot)
{
    mapping state;
    mapping outer;
    string weapon_type;
    string skill_id;
    string category;

    if (!objectp(player) || !stringp(slot) || slot == "")
        return "";
    state = query_persisted_active_skill_state(player);
    if (slot == "outer") {
        weapon_type = query_equipped_weapon_skill_type(player);
        outer = mapp(state["outer"]) ? state["outer"] : ([]);
        skill_id = weapon_type != "" && stringp(outer[weapon_type])
            ? outer[weapon_type] : "";
        if (skill_id == "" && weapon_type == "") {
            foreach (category in ({ "cuff", "palm", "finger", "claw", "leg" })) {
                if (stringp(outer[category]) && outer[category] != "") {
                    skill_id = outer[category];
                    break;
                }
            }
        }
        return skill_id;
    }
    return stringp(state[slot]) ? state[slot] : "";
}

private mapping build_combat_skill_selection(object player)
{
    mapping result;
    string *candidates;
    string active_skill;
    string weapon_type;
    int active_count;

    active_skill = "";
    result = ([
        "skill_id": "",
        "weapon_type": "",
        "unarmed": 0,
        "active_unarmed_count": 0,
    ]);
    if (!objectp(player))
        return result;

    weapon_type = query_equipped_weapon_skill_type(player);
    result["weapon_type"] = weapon_type;
    if (weapon_type == "") {
        candidates = query_unarmed_active_skill_candidates(player);
        active_count = sizeof(candidates);
        if (active_count > 0)
            active_skill = candidates[random(active_count)];
        if (active_skill == "")
            active_skill = query_active_skill_for_slot(player, "outer");
        if (active_skill == ""
            || SKILL_D->query_player_skill_level(player, active_skill) <= 0
            || !has_basic_attack_profile(active_skill)
            || !skill_matches_equipped_weapon(player, active_skill))
            active_skill = query_fallback_basic_attack_skill(player);
        result["unarmed"] = 1;
        result["active_unarmed_count"] = active_count;
    } else {
        active_skill = query_active_skill_for_slot(player, "outer");
        if (active_skill == ""
            || SKILL_D->query_player_skill_level(player, active_skill) <= 0
            || !has_basic_attack_profile(active_skill)
            || !skill_matches_equipped_weapon(player, active_skill))
            active_skill = query_fallback_basic_attack_skill(player);
    }
    result["skill_id"] = active_skill;
    return result;
}

void begin_combat_attack(object player)
{
    if (objectp(player))
        player->delete_temp("combat/active_skill_selection");
}

string query_active_combat_skill(object player)
{
    mapping cached;
    mapping selection;
    string weapon_type;

    if (!objectp(player))
        return "";

    weapon_type = query_equipped_weapon_skill_type(player);
    cached = player->query_temp("combat/active_skill_selection");
    if (mapp(cached) && cached["weapon_type"] == weapon_type
        && stringp(cached["skill_id"]) && cached["skill_id"] != "")
        return cached["skill_id"];

    selection = build_combat_skill_selection(player);
    player->set_temp("combat/active_skill_selection", selection);
    return selection["skill_id"] || "";
}

int query_unarmed_active_skill_count(object player)
{
    mapping selection;

    if (!objectp(player))
        return 0;
    query_active_combat_skill(player);
    selection = player->query_temp("combat/active_skill_selection");
    if (!mapp(selection) || !selection["unarmed"])
        return 0;
    return selection["active_unarmed_count"] || 0;
}

int apply_unarmed_damage_penalty(object player, int damage)
{
    int active_count;

    if (!objectp(player) || damage <= 0)
        return damage;
    active_count = query_unarmed_active_skill_count(player);
    if (active_count <= 1)
        return damage;
    damage /= active_count;
    return damage < 1 ? 1 : damage;
}

private string query_fallback_basic_attack_skill(object player)
{
    string weapon_type;

    if (!objectp(player))
        return "cuff";
    weapon_type = query_equipped_weapon_skill_type(player);
    if (weapon_type != ""
        && SKILL_D->query_player_skill_level(player, weapon_type) > 0
        && has_basic_attack_profile(weapon_type))
        return weapon_type;
    if (weapon_type != "")
        return weapon_type;
    return "cuff";
}

mapping query_active_skill_slots(object player)
{
    mapping state;
    mapping result;

    if (!objectp(player))
        return ([]);
    state = query_persisted_active_skill_state(player);
    result = ([
        "outer": query_active_skill_for_slot(player, "outer"),
        "parry": query_active_skill_for_slot(player, "parry"),
        "force": query_active_skill_for_slot(player, "force"),
        "dodge": query_active_skill_for_slot(player, "dodge"),
    ]);
    result["outer_by_category"] = mapp(state["outer"])
        ? restore_variable(save_variable(state["outer"])) : ([]);
    return result;
}

private int is_outer_skill_category(string category)
{
    return member_array(category, ({
        "blade", "claw", "club", "cuff", "finger", "leg", "palm", "spear",
        "sword", "throwing"
    })) != -1;
}

/* 外功/内功判定公开包装（单点权威，三消费：dazuo 门控、技能列表布尔、
 * 客户端选技过滤）。走 numeric_category 轴——基本内功 def.type="basic" 但
 * numeric_category="force"，按 def["type"] 判会错杀新手唯一内功。 */
int query_is_outer_skill(string skill_id)
{
    if (!stringp(skill_id) || skill_id == "")
        return 0;
    return is_outer_skill_category(NUMERIC_RULE_D->query_skill_category(skill_id));
}

int query_is_force_skill(string skill_id)
{
    if (!stringp(skill_id) || skill_id == "")
        return 0;
    return NUMERIC_RULE_D->query_skill_category(skill_id) == "force";
}

private int can_activate_skill_slot(object player, string skill_id, string slot)
{
    string category;

    if (!objectp(player) || !stringp(skill_id) || skill_id == "" || !stringp(slot) || slot == "")
        return 0;
    if (SKILL_D->query_player_skill_level(player, skill_id) <= 0)
        return 0;

    category = NUMERIC_RULE_D->query_skill_category(skill_id);
    if (slot == "outer")
        return is_outer_skill_category(category) && has_basic_attack_profile(skill_id);
    if (slot == "parry")
        return is_outer_skill_category(category);
    if (slot == "force")
        return category == "force";
    if (slot == "dodge")
        return category == "dodge";
    return 0;
}

mapping query_skill_activation_options(object player, string skill_id)
{
    mapping result;
    string category;

    result = ([ ]);
    category = NUMERIC_RULE_D->query_skill_category(skill_id);
    if (can_activate_skill_slot(player, skill_id, "outer"))
        result["outer"] = ([
            "slot": "outer",
            "label": "激发" + query_activation_category_name(category),
            "command": "activate outer " + skill_id,
        ]);
    if (can_activate_skill_slot(player, skill_id, "parry"))
        result["parry"] = ([
            "slot": "parry",
            "label": "激发招架",
            "command": "activate parry " + skill_id,
        ]);
    if (can_activate_skill_slot(player, skill_id, "force"))
        result["force"] = ([
            "slot": "force",
            "label": "激发内功",
            "command": "activate force " + skill_id,
        ]);
    if (can_activate_skill_slot(player, skill_id, "dodge"))
        result["dodge"] = ([
            "slot": "dodge",
            "label": "激发轻功",
            "command": "activate dodge " + skill_id,
        ]);
    return result;
}

private int commit_active_skill_selection(object player, string slot, string skill_id)
{
    mapping state;
    mapping outer;
    mapping changes;
    mapping response;
    string category;

    if (!objectp(player) || !stringp(slot) || slot == ""
        || !stringp(skill_id) || skill_id == "")
        return 0;
    state = query_persisted_active_skill_state(player);
    if (slot == "outer") {
        category = NUMERIC_RULE_D->query_skill_category(skill_id);
        if (!stringp(category) || category == "")
            return 0;
        outer = mapp(state["outer"]) ? state["outer"] : ([]);
        outer[category] = skill_id;
        state["outer"] = outer;
    } else {
        state[slot] = skill_id;
    }
    changes = ([ "combat/active_skills": state ]);
    response = PLAYER_STATE_D->commit_state_fields(player, changes,
        PLAYER_STATE_D->build_state_idempotency_key(player,
            "skill/active_selection", changes));
    if (!mapp(response) || !response["ok"])
        return 0;
    player->delete_temp("combat/active_skill_selection");
    return 1;
}

mapping activate_active_skill(object player, string slot, string skill_id)
{
    mapping cost;
    string category;

    if (!objectp(player) || !stringp(slot) || slot == "" || !stringp(skill_id) || skill_id == "")
        return ([ "ok": 0, "error": "invalid_arguments" ]);
    if (slot != "outer" && slot != "parry" && slot != "force" && slot != "dodge")
        return ([ "ok": 0, "error": "unsupported_slot" ]);
    if (SKILL_D->query_player_skill_level(player, skill_id) <= 0)
        return ([ "ok": 0, "error": "skill_not_learned" ]);
    if (!can_activate_skill_slot(player, skill_id, slot))
        return ([ "ok": 0, "error": slot == "outer" ? "missing_basic_attack_profile" : "unsupported_slot" ]);
    if (!commit_active_skill_selection(player, slot, skill_id))
        return ([ "ok": 0, "error": "active_skill_persist_failed" ]);

    cost = NUMERIC_RULE_D->query_switch_cost(slot);
    if ((cost["busy"] || 0) > 0)
        player->add_busy(cost["busy"] || 0);
    if (slot == "outer" && SKILL_BREAKTHROUGH_D->is_public_skill(skill_id))
        QUEST_D->record_action_progress(player,
            "onboarding_outer_skill_activated", skill_id, "success");
    if (slot == "force" && skill_id == "force")
        QUEST_D->record_action_progress(player,
            "onboarding_basic_force_activated", skill_id, "success");
    category = NUMERIC_RULE_D->query_skill_category(skill_id);

    return ([
        "ok": 1,
        "slot": slot,
        "category": category,
        "category_name": slot == "outer"
            ? query_activation_category_name(category) : "",
        "skill_id": skill_id,
        "busy": cost["busy"] || 0,
        "persisted": 1,
    ]);
}

mapping switch_active_skill(object player, string slot, string skill_id, string mode)
{
    mapping cost;
    int busy;
    int debate_turns;

    if (!objectp(player) || !stringp(slot) || slot == "" || !stringp(skill_id) || skill_id == "")
        return ([ "ok": 0, "error": "invalid_arguments" ]);
    if (SKILL_D->query_player_skill_level(player, skill_id) <= 0)
        return ([ "ok": 0, "error": "skill_not_learned" ]);
    if (!can_activate_skill_slot(player, skill_id, slot))
        return ([ "ok": 0, "error": slot == "outer"
            ? "missing_basic_attack_profile" : "unsupported_slot" ]);

    cost = NUMERIC_RULE_D->query_switch_cost(slot);
    busy = cost["busy"] || 0;
    debate_turns = cost["debate_turns"] || 0;
    if (!commit_active_skill_selection(player, slot, skill_id))
        return ([ "ok": 0, "error": "active_skill_persist_failed" ]);
    if (mode == "debate")
        player->set_temp("combat/debate/skip_turns", debate_turns);
    else if (busy > 0)
        player->add_busy(busy);

    return ([
        "ok": 1,
        "slot": slot,
        "skill_id": skill_id,
        "busy": busy,
        "debate_turns": debate_turns,
    ]);
}

mapping query_basic_attack_profile(object player, string skill_id)
{
    mapping behavior;
    mapping profile;

    if (!objectp(player) || !stringp(skill_id) || skill_id == "")
        return ([]);

    behavior = _behavior_definitions[skill_id];
    if (!mapp(behavior))
        return ([]);

    profile = behavior["basic_attack"];
    return mapp(profile) ? copy(profile) : ([]);
}

private object resolve_skill_action_target(object player, mapping action, object target)
{
    mapping combat;
    string combat_id;
    string target_policy;

    if (objectp(target))
        return target;
    if (!objectp(player) || !mapp(action))
        return 0;

    target_policy = action["target_policy"] || "";
    if (target_policy == "self")
        return player;
    if (target_policy == "combat_target") {
        if (function_exists("query_fight_object", player))
            target = player->query_fight_object();
        if (!objectp(target))
            target = player->query_temp("fight_ob");
        if (!objectp(target)) {
            combat_id = COMBAT_D->query_combat_id(player);
            combat = stringp(combat_id) && combat_id != "" ? COMBAT_D->query_combat(combat_id) : 0;
            if (mapp(combat))
                target = combat["attacker"] == player ? combat["defender"] : combat["attacker"];
        }
    }
    return target;
}

private int is_valid_combat_target(object player, object target)
{
    string player_combat_id;
    string target_combat_id;

    if (!objectp(player) || !objectp(target) || target == player)
        return 0;
    if ((target->query("combat/hp") || 0) <= 0)
        return 0;
    player_combat_id = COMBAT_D->query_combat_id(player);
    target_combat_id = COMBAT_D->query_combat_id(target);
    return stringp(player_combat_id) && player_combat_id != ""
        && player_combat_id == target_combat_id;
}

private string query_action_required_slot(mapping action_def)
{
    mapping behavior;
    string slot;

    if (!mapp(action_def))
        return "";
    slot = action_def["required_active_slot"] || "";
    behavior = mapp(action_def["behavior"]) ? action_def["behavior"] : ([]);
    if (slot == "")
        slot = behavior["required_active_slot"] || "";
    return slot;
}

private string query_action_required_weapon_type(mapping action_def)
{
    mapping behavior;
    string weapon_type;

    if (!mapp(action_def))
        return "";
    weapon_type = action_def["required_weapon_type"] || "";
    behavior = mapp(action_def["behavior"]) ? action_def["behavior"] : ([]);
    if (weapon_type == "")
        weapon_type = behavior["required_weapon_type"] || "";
    return weapon_type;
}

private mapping validate_skill_action_activation(object player, string skill_id,
    mapping action_def)
{
    string required_slot;
    string required_weapon_type;
    string equipped_weapon_type;

    required_weapon_type = query_action_required_weapon_type(action_def);
    equipped_weapon_type = query_equipped_weapon_skill_type(player);
    if (required_weapon_type != "" && equipped_weapon_type != required_weapon_type) {
        return ([ "ok": 0, "error": "weapon_type_mismatch",
            "required_weapon_type": required_weapon_type,
            "required_weapon_name": query_activation_category_name(required_weapon_type),
            "equipped_weapon_type": equipped_weapon_type ]);
    }
    required_slot = query_action_required_slot(action_def);
    if (required_slot != ""
        && !is_active_skill_for_slot(player, required_slot, skill_id)) {
        return ([ "ok": 0, "error": "skill_not_activated",
            "required_active_slot": required_slot, "skill_id": skill_id ]);
    }
    return ([ "ok": 1 ]);
}

private mixed *normalize_status_requirements(mixed raw_requirements)
{
    mapping requirement;
    mixed *requirements;
    mixed raw_requirement;
    mixed value;
    string status_id;

    requirements = ({ });
    if (stringp(raw_requirements) && raw_requirements != "")
        return ({ ([ "id": raw_requirements, "stacks": 1 ]) });
    if (mapp(raw_requirements)) {
        if (stringp(raw_requirements["id"]) && raw_requirements["id"] != "")
            return ({ copy(raw_requirements) });
        foreach (status_id, value in raw_requirements) {
            if (!stringp(status_id) || status_id == "")
                continue;
            requirement = mapp(value) ? copy(value) : ([ ]);
            requirement["id"] = status_id;
            if (!intp(requirement["stacks"]))
                requirement["stacks"] = intp(value) ? value : 1;
            requirements += ({ requirement });
        }
        return requirements;
    }
    foreach (raw_requirement in raw_requirements || ({ })) {
        if (stringp(raw_requirement) && raw_requirement != "")
            requirements += ({ ([ "id": raw_requirement, "stacks": 1 ]) });
        else if (mapp(raw_requirement) && stringp(raw_requirement["id"])
            && raw_requirement["id"] != "")
            requirements += ({ copy(raw_requirement) });
    }
    return requirements;
}

private mapping validate_status_requirements(object owner, mixed raw_requirements,
    string owner_name)
{
    mapping requirement;
    mixed *requirements;
    string status_id;
    int stacks;

    requirements = normalize_status_requirements(raw_requirements);
    foreach (requirement in requirements) {
        status_id = requirement["id"] || "";
        stacks = requirement["stacks"] || 1;
        if (status_id == "" || stacks <= 0)
            continue;
        if (COMBAT_EFFECT_D->query_combat_status_stacks(owner, status_id) < stacks) {
            return ([ "ok": 0, "error": "missing_required_status",
                "status_owner": owner_name, "status_id": status_id,
                "required_stacks": stacks ]);
        }
    }
    return ([ "ok": 1 ]);
}

private mapping validate_skill_action_status_requirements(object player,
    object target, mapping action_def)
{
    mapping result;

    if (!mapp(action_def))
        return ([ "ok": 1 ]);
    result = validate_status_requirements(player,
        action_def["requires_self_status"], "self");
    if (!result["ok"])
        return result;
    if (arrayp(normalize_status_requirements(action_def["requires_target_status"]))) {
        result = validate_status_requirements(target,
            action_def["requires_target_status"], "target");
        if (!result["ok"])
            return result;
    }
    return ([ "ok": 1 ]);
}

private mixed *apply_status_requirement_deltas(mixed raw_requirements,
    mapping deltas)
{
    mapping requirement;
    mixed *result;
    mixed *requirements;
    string status_id;
    int stacks;

    requirements = normalize_status_requirements(raw_requirements);
    if (!mapp(deltas) || sizeof(deltas) == 0)
        return requirements;

    result = ({ });
    foreach (requirement in requirements) {
        if (!mapp(requirement))
            continue;
        requirement = copy(requirement);
        status_id = requirement["id"] || "";
        stacks = requirement["stacks"] || 1;
        if (stringp(status_id) && intp(deltas[status_id]))
            stacks += deltas[status_id];
        if (stacks <= 0)
            continue;
        requirement["stacks"] = stacks;
        result += ({ requirement });
    }
    return result;
}

private mapping apply_skill_action_status_requirement_modifiers(mapping action_def,
    mapping modifiers)
{
    mapping result;

    result = mapp(action_def) ? copy(action_def) : ([ ]);
    if (!mapp(modifiers) || sizeof(modifiers) == 0)
        return result;
    result["requires_self_status"] = apply_status_requirement_deltas(
        action_def["requires_self_status"], modifiers["requires_self_status"]);
    result["requires_target_status"] = apply_status_requirement_deltas(
        action_def["requires_target_status"], modifiers["requires_target_status"]);
    result["consume_self_status"] = apply_status_requirement_deltas(
        action_def["consume_self_status"], modifiers["consume_self_status"]);
    result["consume_target_status"] = apply_status_requirement_deltas(
        action_def["consume_target_status"], modifiers["consume_target_status"]);
    return result;
}

private mapping build_status_consumption(mapping action_def)
{
    return ([
        "self": normalize_status_requirements(mapp(action_def)
            ? action_def["consume_self_status"] : 0),
        "target": normalize_status_requirements(mapp(action_def)
            ? action_def["consume_target_status"] : 0),
    ]);
}

private mixed *append_breakthrough_action_status_effects(mixed *raw_effects,
    mapping projection, string owner)
{
    mapping effect;
    mapping effects;
    mixed raw_effect;
    mixed *result;
    mixed *additions;

    result = arrayp(raw_effects) ? raw_effects + ({ }) : ({ });
    effects = mapp(projection) && mapp(projection["action_status_effects"])
        ? projection["action_status_effects"] : ([ ]);
    additions = arrayp(effects[owner]) ? effects[owner] : ({ });
    foreach (raw_effect in additions) {
        if (!mapp(raw_effect))
            continue;
        effect = copy(raw_effect);
        result += ({ effect });
    }
    return result;
}

private void consume_status_requirements(object owner, mixed *requirements)
{
    mapping requirement;
    string status_id;
    int stacks;

    if (!objectp(owner))
        return;
    foreach (requirement in requirements || ({ })) {
        status_id = mapp(requirement) ? (requirement["id"] || "") : "";
        stacks = mapp(requirement) ? (requirement["stacks"] || 1) : 0;
        if (status_id != "" && stacks > 0)
            COMBAT_EFFECT_D->consume_combat_status_stacks(owner, status_id, stacks);
    }
}

void consume_completed_action_statuses(object player, object target, mapping action)
{
    mapping consumption;

    if (!objectp(player) || !mapp(action))
        return;
    consumption = mapp(action["status_consumption"])
        ? action["status_consumption"] : build_status_consumption(action);
    consume_status_requirements(player, consumption["self"]);
    consume_status_requirements(target, consumption["target"]);
}

private mapping prepare_skill_action_runtime(object player, string action_type, string skill_id, mapping action)
{
    mapping budget;
    mapping commit;

    budget = NUMERIC_RULE_D->query_action_budget_state(player, action_type, skill_id, action);
    if (!mapp(budget) || !budget["ok"])
        return budget;
    if (mapp(action) && stringp(action["skill_id"]) && action["skill_id"] != "")
        budget["skill_id"] = action["skill_id"];
    if (mapp(action))
        budget["action_name"] = query_action_effect_name(action, budget["skill_id"] || skill_id);

    commit = NUMERIC_RULE_D->commit_action_budget_event(player, budget);
    if (!mapp(commit) || !commit["ok"])
        return commit;
    budget["commit"] = commit;
    return budget;
}

private void apply_recovery_effects(object player, mapping effects)
{
    int amount;

    if (!objectp(player) || !mapp(effects))
        return;

    amount = effects["recover_hp"] || 0;
    if (amount > 0 && function_exists("heal_hp", player))
        player->heal_hp(amount);
    amount = effects["recover_mp"] || 0;
    if (amount > 0 && function_exists("heal_mp", player))
        player->heal_mp(amount);
    amount = effects["recover_ep"] || 0;
    if (amount > 0 && function_exists("heal_ep", player))
        player->heal_ep(amount);
    if ((effects["recover_hp"] || 0) > 0 || (effects["recover_mp"] || 0) > 0 || (effects["recover_ep"] || 0) > 0)
        xk_send_status_light(player);
}

private mapping normalize_status_effect(mapping raw_effect)
{
    mapping effect;

    if (!mapp(raw_effect) || !stringp(raw_effect["id"]) || raw_effect["id"] == "")
        return 0;

    effect = copy(raw_effect);
    effect["suppress_apply_lifecycle_message"] = 1;
    if (!stringp(effect["type"]) || effect["type"] == "")
        effect["type"] = "buff";
    if (!intp(effect["duration_rounds"]) || effect["duration_rounds"] <= 0)
        effect["duration_rounds"] = 1;
    if (!intp(effect["tick_interval_rounds"]) || effect["tick_interval_rounds"] <= 0)
        effect["tick_interval_rounds"] = 1;
    if (!stringp(effect["stack_rule"]) || effect["stack_rule"] == "")
        effect["stack_rule"] = "refresh";
    if (!intp(effect["max_stacks"]) || effect["max_stacks"] <= 0)
        effect["max_stacks"] = 1;
    if (!mapp(effect["stat_effects"]))
        effect["stat_effects"] = ([]);
    return effect;
}

private mixed *prepare_status_effects(mixed *raw_effects)
{
    mixed *effects;
    mixed raw_effect;
    mapping effect;

    effects = ({ });
    foreach (raw_effect in raw_effects || ({ })) {
        effect = normalize_status_effect(raw_effect);
        if (mapp(effect))
            effects += ({ effect });
    }
    return effects;
}

private int query_status_modifier_limit(string key, int fallback)
{
    mapping framework;
    mapping limits;
    int value;

    framework = NUMERIC_RULE_D->query_combat_status_effect_framework();
    limits = mapp(framework["limits"]) ? framework["limits"] : ([]);
    value = limits[key];
    return intp(value) && value > 0 ? value : fallback;
}

private mixed *apply_status_modifiers_to_raw_effects(mixed *raw_effects,
    mapping status_modifiers)
{
    mapping effect;
    mapping modifier;
    mapping stat_effects;
    mixed *result;
    mixed raw_effect;
    string effect_id;
    string key;
    int value;

    result = ({ });
    foreach (raw_effect in raw_effects || ({ })) {
        if (!mapp(raw_effect))
            continue;
        effect = copy(raw_effect);
        effect_id = effect["id"] || "";
        modifier = mapp(status_modifiers) && effect_id != ""
            ? status_modifiers[effect_id] : 0;
        if (mapp(modifier)) {
            effect["duration_rounds"] = (effect["duration_rounds"] || 1)
                + (modifier["duration_rounds_delta"] || 0);
            if (effect["duration_rounds"] < 1)
                effect["duration_rounds"] = 1;
            if (effect["duration_rounds"] > query_status_modifier_limit(
                "max_duration_rounds", 6))
                effect["duration_rounds"] = query_status_modifier_limit(
                    "max_duration_rounds", 6);
            effect["max_stacks"] = (effect["max_stacks"] || 1)
                + (modifier["max_stacks_delta"] || 0);
            if (effect["max_stacks"] < 1)
                effect["max_stacks"] = 1;
            if (effect["max_stacks"] > query_status_modifier_limit(
                "max_stacks", 3))
                effect["max_stacks"] = query_status_modifier_limit("max_stacks", 3);
            if (mapp(modifier["stat_effects"])) {
                stat_effects = mapp(effect["stat_effects"])
                    ? copy(effect["stat_effects"]) : ([]);
                foreach (key, value in modifier["stat_effects"])
                    if (stringp(key) && intp(value))
                        stat_effects[key] = (stat_effects[key] || 0) + value;
                effect["stat_effects"] = stat_effects;
            }
        }
        result += ({ effect });
    }
    return result;
}

private mapping query_non_recovery_status_effects(mapping effects)
{
    mapping stat_effects;
    string key;
    int value;

    if (!mapp(effects))
        return ([ ]);
    stat_effects = ([ ]);
    foreach (key, value in effects) {
        if (!stringp(key) || !intp(value) || value == 0)
            continue;
        if (key == "recover_hp" || key == "recover_mp" || key == "recover_ep")
            continue;
        stat_effects[key] = value;
    }
    return stat_effects;
}

private varargs mixed *prepare_status_effects_with_action_effects(mixed *raw_effects,
    mapping effects, mapping status_modifiers)
{
    mixed *prepared;
    mapping stat_effects;
    mapping effect;
    string key;
    int value;

    prepared = prepare_status_effects(apply_status_modifiers_to_raw_effects(
        raw_effects, status_modifiers));
    if (sizeof(prepared) == 0)
        return prepared;
    stat_effects = query_non_recovery_status_effects(effects);
    if (sizeof(stat_effects) == 0)
        return prepared;

    effect = prepared[0];
    if (!mapp(effect))
        return prepared;
    if (!mapp(effect["stat_effects"]))
        effect["stat_effects"] = ([ ]);
    foreach (key, value in stat_effects)
        effect["stat_effects"][key] = (effect["stat_effects"][key] || 0) + value;
    prepared[0] = effect;
    return prepared;
}

private string query_action_effect_name(mapping action, string skill_id)
{
    mapping skill_def;
    string description;
    int colon;

    if (mapp(action) && stringp(action["description"]) && action["description"] != "") {
        description = action["description"];
        colon = strsrch(description, "：");
        if (colon <= 0)
            colon = strsrch(description, ":");
        if (colon > 0)
            return description[0..colon - 1];
    }

    skill_def = SKILL_D->get_skill_definition(skill_id);
    if (mapp(skill_def) && stringp(skill_def["name"]) && skill_def["name"] != "")
        return skill_def["name"];
    return "技能状态";
}

private mapping build_self_action_status_effect(string action_type, string skill_id, mapping action, mapping effects)
{
    mapping stat_effects;

    stat_effects = query_non_recovery_status_effects(effects);
    if (sizeof(stat_effects) == 0)
        return 0;
    return ([
        "id": sprintf("%s_%s_self_effects", action_type, skill_id),
        "name": query_action_effect_name(action, skill_id),
        "type": "buff",
        "duration_rounds": 2,
        "tick_interval_rounds": 1,
        "stack_rule": "refresh",
        "max_stacks": 1,
        "suppress_apply_lifecycle_message": 1,
        "stat_effects": stat_effects,
    ]);
}

private void apply_self_action_status_effect(object player, string action_type, string skill_id, mapping action, mapping effects)
{
    mapping effect;

    effect = build_self_action_status_effect(action_type, skill_id, action, effects);
    if (mapp(effect))
        COMBAT_EFFECT_D->apply_combat_status_effect(player, player, effect);
}

private mixed *build_skill_action_status_payload(string action_type, string skill_id,
    mapping action, mapping effects, string owner)
{
    mapping effect;
    mixed *status_effects;

    if (arrayp(action["status_effects"]) && sizeof(action["status_effects"]) > 0)
        status_effects = prepare_status_effects_with_action_effects(action["status_effects"], effects);
    else if (owner == "self") {
        effect = build_self_action_status_effect(action_type, skill_id, action, effects);
        status_effects = mapp(effect) ? ({ effect }) : ({ });
    } else {
        status_effects = ({ });
    }
    return COMBAT_EFFECT_D->build_combat_status_instances(status_effects, owner);
}

private int perform_should_resolve_damage_now(mapping action)
{
    return mapp(action) && (action["target_policy"] || "") == "combat_target";
}

private void finalize_immediate_perform_combat(object player, object target, int defeated)
{
    mapping combat;
    string combat_id;

    if (!objectp(player) || !objectp(target))
        return;
    combat_id = COMBAT_D->query_combat_id(player);
    if (!stringp(combat_id) || combat_id == "" || combat_id != COMBAT_D->query_combat_id(target))
        return;
    combat = COMBAT_D->query_combat(combat_id);
    if (!mapp(combat))
        return;
    if ((target->query("combat/hp") || 0) <= 0 || defeated) {
        COMBAT_D->end_combat(combat_id,
            combat["attacker"] == player ? COMBAT_RESULT_WIN : COMBAT_RESULT_LOSE);
    } else {
        COMBAT_D->schedule_next_round(combat_id);
    }
}

mapping resolve_pending_perform_action(object player, object target)
{
    mapping combat;
    string combat_id;
    int defeated;
    int nonlethal;

    if (!objectp(player) || !objectp(target))
        return ([ "ok": 0, "error": "invalid_arguments" ]);
    combat_id = COMBAT_D->query_combat_id(player);
    combat = stringp(combat_id) && combat_id != "" ? COMBAT_D->query_combat(combat_id) : 0;
    nonlethal = mapp(combat) && !combat["kill_mode"] ? 1 : 0;
    defeated = COMBAT_D->do_single_attack(player, target, nonlethal);
    finalize_immediate_perform_combat(player, target, defeated);
    return ([ "ok": 1, "defeated": defeated ]);
}

private mapping merge_action_effects(mapping base_effects, mapping extra_effects)
{
    mapping merged;
    string key;
    int value;

    merged = mapp(base_effects) ? copy(base_effects) : ([ ]);
    if (!mapp(extra_effects))
        return merged;

    foreach (key, value in extra_effects) {
        if (!stringp(key) || !intp(value))
            continue;
        merged[key] = (merged[key] || 0) + value;
    }
    return merged;
}

private int apply_action_delta(int value, mapping modifiers, string key, int min_value)
{
    int delta;

    if (!mapp(modifiers) || !intp(modifiers[key]))
        return value;
    delta = modifiers[key];
    value += delta;
    return value < min_value ? min_value : value;
}

private mapping apply_action_cost_modifiers(mapping costs, mapping modifiers)
{
    mapping result;
    string resource;
    int amount;
    int base_amount;
    int minimum_amount;
    int percent;

    result = mapp(costs) ? copy(costs) : ([ ]);
    if (!mapp(modifiers))
        return result;
    foreach (resource, amount in result || ([ ])) {
        if (!stringp(resource) || !intp(amount))
            continue;
        base_amount = amount;
        percent = modifiers[resource + "_cost_percent_delta"] || 0;
        amount = amount * (100 + percent) / 100;
        amount += modifiers[resource + "_cost_delta"] || 0;
        minimum_amount = base_amount > 0 ? (base_amount + 9) / 10 : 0;
        if (amount < minimum_amount)
            amount = minimum_amount;
        result[resource] = amount < 0 ? 0 : amount;
    }
    return result;
}

private mapping apply_action_runtime_modifiers(mapping action, mapping modifiers)
{
    mapping effects;
    mapping result;
    int base_damage_bonus;
    int base_hit_bonus;

    result = mapp(action) ? copy(action) : ([ ]);
    result["busy"] = apply_action_delta(result["busy"] || 0, modifiers, "busy_delta", 0);
    result["cooldown"] = apply_action_delta(result["cooldown"] || 0, modifiers, "cooldown_delta", 0);
    result["fatigue_per_use_bp"] = apply_action_delta(result["fatigue_per_use_bp"] || 0, modifiers, "fatigue_per_use_bp_delta", 0);
    result["resource_costs"] = apply_action_cost_modifiers(result["resource_costs"], modifiers);

    /* 词条 perform 强化：伤害/命中增量当次生效；next_attack_* 保留原名
     * 供战斗结算读取（普通攻击临时应用）。 */
    if (!mapp(modifiers))
        return result;
    effects = mapp(result["effects"]) ? copy(result["effects"]) : ([ ]);
    base_damage_bonus = intp(effects["damage_bonus"]) ? effects["damage_bonus"] : 0;
    base_hit_bonus = intp(effects["hit_bonus"]) ? effects["hit_bonus"] : 0;
    base_damage_bonus += intp(modifiers["damage_bonus_delta"]) ? modifiers["damage_bonus_delta"] : 0;
    base_hit_bonus += intp(modifiers["hit_bonus_delta"]) ? modifiers["hit_bonus_delta"] : 0;
    if (intp(modifiers["next_attack_damage_bonus"]) && modifiers["next_attack_damage_bonus"] > 0)
        effects["next_attack_damage_bonus"] = modifiers["next_attack_damage_bonus"];
    if (intp(modifiers["next_action_damage_bonus"]) && modifiers["next_action_damage_bonus"] > 0)
        effects["next_attack_damage_bonus"] = modifiers["next_action_damage_bonus"];
    if (intp(modifiers["next_attack_damage_factor_bp"]) && modifiers["next_attack_damage_factor_bp"] > 0)
        effects["next_attack_damage_factor_bp"] = modifiers["next_attack_damage_factor_bp"];
    if (intp(modifiers["next_attack_hit_bonus"]) && modifiers["next_attack_hit_bonus"] > 0)
        effects["next_attack_hit_bonus"] = modifiers["next_attack_hit_bonus"];
    if (intp(modifiers["next_attack_armor_break_bonus"]) && modifiers["next_attack_armor_break_bonus"] > 0)
        effects["next_attack_armor_break_bonus"] = modifiers["next_attack_armor_break_bonus"];
    effects["damage_bonus"] = base_damage_bonus;
    effects["hit_bonus"] = base_hit_bonus;
    result["effects"] = effects;
    return result;
}

private int recover_resource_by_bp(object player, string resource, int bp)
{
    int current;
    int max_value;
    int amount;

    if (!objectp(player) || !stringp(resource) || resource == "" || bp <= 0)
        return 0;

    current = player->query("combat/" + resource) || 0;
    max_value = query_player_resource_max(player, resource);
    if (max_value <= 0 || current >= max_value)
        return 0;

    amount = max_value * bp / BP_BASE;
    if (amount < 1)
        amount = 1;

    player->receive_healing(resource, amount);
    return (player->query("combat/" + resource) || 0) > current;
}

private int query_player_resource_max(object player, string resource)
{
    if (!objectp(player) || !stringp(resource) || resource == "")
        return 0;

    switch (resource) {
        case "hp":
            if (function_exists("query_max_hp", player))
                return player->query_max_hp();
            break;
        case "mp":
            if (function_exists("query_max_mp", player))
                return player->query_max_mp();
            break;
        case "ep":
            if (function_exists("query_max_ep", player))
                return player->query_max_ep();
            break;
    }

    return player->query("combat/max_" + resource) || 0;
}

void update_player_skill_passives(object player)
{
    mapping behavior;
    mapping passive;
    mapping recovery;
    int changed;

    if (!objectp(player) || SKILL_D->query_player_skill_level(player, "force") <= 0)
        return;
    if (COMBAT_D->is_in_combat(player) || objectp(player->query_temp("fight_ob")))
        return;

    behavior = _behavior_definitions["force"];
    passive = mapp(behavior) ? behavior["passive"] : 0;
    recovery = mapp(passive) ? passive["out_of_combat_recovery"] : 0;
    if (!mapp(recovery))
        return;

    changed += recover_resource_by_bp(player, "hp", recovery["hp_bp"] || 0);
    changed += recover_resource_by_bp(player, "mp", recovery["mp_bp"] || 0);
    changed += recover_resource_by_bp(player, "ep", recovery["ep_bp"] || 0);
    if (changed > 0)
        xk_send_status_light(player);
}

mapping *query_skill_actions(string action_type, string skill_id)
{
    mapping *result;
    mapping *indexed_actions;
    int index;

    result = ({ });
    if (!stringp(action_type) || action_type == "" || !stringp(skill_id) || skill_id == "")
        return result;
    if (!mapp(_actions_by_skill_type))
        rebuild_action_index();

    indexed_actions = _actions_by_skill_type[action_index_key(action_type, skill_id)];
    if (!arrayp(indexed_actions) || sizeof(indexed_actions) == 0)
        return result;

    result = allocate(sizeof(indexed_actions));
    for (index = 0; index < sizeof(indexed_actions); index++)
        result[index] = copy(indexed_actions[index]);
    return result;
}

mapping query_shared_skill_resource_thresholds(string *skill_ids)
{
    mapping action;
    mapping behavior;
    mapping budget_spec;
    mapping resource_costs;
    mapping skill_set;
    mapping thresholds;
    mixed amount;
    string action_type;
    string resource;
    string shape_signature;
    string skill_id;
    int numeric_generation;

    numeric_generation = NUMERIC_RULE_D->query_numeric_config_generation();
    if (_resource_thresholds_numeric_generation != numeric_generation) {
        _resource_thresholds_by_skill_shape = ([ ]);
        _resource_thresholds_numeric_generation = numeric_generation;
    }
    skill_set = ([ ]);
    foreach (skill_id in skill_ids || ({ }))
        if (stringp(skill_id) && skill_id != "")
            skill_set[skill_id] = 1;
    shape_signature = implode(sort_array(keys(skill_set), 1), "|");
    if (mapp(_resource_thresholds_by_skill_shape[shape_signature]))
        return copy_shared_resource_thresholds(
            _resource_thresholds_by_skill_shape[shape_signature]);

    thresholds = ([ ]);
    foreach (skill_id in keys(skill_set)) {
        foreach (action_type in ({ "perform", "exert" })) {
            foreach (action in _actions_by_skill_type[
                action_index_key(action_type, skill_id)] || ({ })) {
                if (!mapp(action))
                    continue;
                behavior = mapp(action["behavior"]) ? action["behavior"] : ([ ]);
                budget_spec = NUMERIC_RULE_D->query_action_budget_spec(
                    action_type, action["id"] || skill_id, behavior);
                resource_costs = mapp(budget_spec["resource_costs"])
                    ? budget_spec["resource_costs"]
                    : ([ ]);
                foreach (resource, amount in resource_costs)
                    if (intp(amount) && amount > 0)
                        add_shared_resource_threshold(thresholds, resource, amount);
            }
        }
    }
    _resource_thresholds_by_skill_shape[shape_signature] =
        copy_shared_resource_thresholds(thresholds);
    return copy_shared_resource_thresholds(thresholds);
}

int query_behavior_config_generation()
{
    return _behavior_config_generation > 0 ? _behavior_config_generation : 1;
}

private mapping query_skill_action_definition(string action_type, string skill_id, string action_id)
{
    mapping action;
    mapping *actions;

    actions = query_skill_actions(action_type, skill_id);
    if (sizeof(actions) == 0)
        return 0;
    if (!stringp(action_id) || action_id == "")
        return actions[0];
    foreach (action in actions)
        if (mapp(action) && action["id"] == action_id)
            return action;
    return 0;
}

private int player_has_breakthrough_entry(object player, string skill_id, string entry_id)
{
    mapping entry;
    mapping *entries;

    if (!objectp(player) || !stringp(skill_id) || skill_id == ""
        || !stringp(entry_id) || entry_id == "")
        return 0;

    entries = NUMERIC_RULE_D->query_skill_breakthrough_entries(player, skill_id);
    foreach (entry in entries || ({ }))
        if (mapp(entry) && entry["id"] == entry_id)
            return 1;
    return 0;
}

varargs mapping query_skill_action_access(object player, string action_type,
    string skill_id, string action_id, mapping action_def, mixed known_skill_data)
{
    mapping activation;
    mapping unlock;
    string required_entry;
    int current_level;
    int required_level;

    if (!objectp(player) || !stringp(action_type) || action_type == ""
        || !stringp(skill_id) || skill_id == "")
        return ([ "ok": 0, "error": "invalid_arguments" ]);

    if (!mapp(action_def))
        action_def = query_skill_action_definition(action_type, skill_id, action_id);
    if (!mapp(action_def))
        return ([ "ok": 0, "error": "missing_action" ]);

    unlock = mapp(action_def["unlock"]) ? action_def["unlock"] : ([]);
    required_level = action_def["required_skill_level"] || unlock["required_skill_level"] || 1;
    current_level = mapp(known_skill_data) && intp(known_skill_data["level"])
        ? known_skill_data["level"]
        : SKILL_D->query_player_skill_level(player, skill_id);
    if (current_level < required_level)
        return ([ "ok": 0, "error": "skill_level_too_low", "required_skill_level": required_level ]);

    required_entry = action_def["required_breakthrough_entry"] || "";
    if (required_entry != "" && !player_has_breakthrough_entry(player, skill_id, required_entry))
        return ([ "ok": 0, "error": "breakthrough_entry_required", "required_breakthrough_entry": required_entry ]);

    activation = validate_skill_action_activation(player, skill_id, action_def);
    if (!mapp(activation) || !activation["ok"])
        return activation;

    return ([ "ok": 1, "action": action_def ]);
}

mapping query_skill_action_profile(string action_type, string skill_id)
{
    mapping behavior;
    mapping raw_action;

    if (!stringp(action_type) || action_type == "" || !stringp(skill_id) || skill_id == "")
        return ([]);

    behavior = _behavior_definitions[skill_id];
    raw_action = mapp(behavior) ? behavior[action_type] : 0;
    if (!mapp(raw_action))
        return ([]);
    return NUMERIC_RULE_D->normalize_skill_action_definition(action_type, skill_id, raw_action);
}

varargs mapping perform_action(object player, string skill_id, object target, string action_id)
{
    mapping action_def;
    mapping action_runtime_def;
    mapping breakthrough_projection;
    mapping effects;
    mapping perform;
    mapping runtime;
    string runtime_action_id;
    int phase_started_at;
    int phase_cpu_started_at;

    if (!objectp(player) || !stringp(skill_id) || skill_id == "")
        return ([ "ok": 0, "error": "invalid_arguments" ]);

    phase_started_at = perf_counter_ns();
    phase_cpu_started_at = thread_cpu_time_ns();
    action_def = query_skill_action_definition("perform", skill_id, action_id);
    runtime = query_skill_action_access(player, "perform", skill_id, action_id, action_def);
    record_perform_phase(player, "perform_definition_access", phase_started_at,
        phase_cpu_started_at);
    if (!mapp(runtime) || !runtime["ok"])
        return runtime;
    perform = mapp(action_def) ? action_def["behavior"] : query_skill_action_profile("perform", skill_id);
    if (!mapp(perform))
        return ([ "ok": 0, "error": "missing_perform_behavior" ]);
    runtime_action_id = mapp(action_def) && stringp(action_def["id"]) && action_def["id"] != "" ? action_def["id"] : skill_id;

    phase_started_at = perf_counter_ns();
    phase_cpu_started_at = thread_cpu_time_ns();
    target = resolve_skill_action_target(player, perform, target);
    if ((perform["target_policy"] || "") == "combat_target"
        && !is_valid_combat_target(player, target))
        return ([ "ok": 0, "error": "missing_combat_target" ]);
    breakthrough_projection = NUMERIC_RULE_D->query_skill_breakthrough_action_projection(
        player, skill_id, "perform", target, runtime_action_id,
        query_action_required_slot(action_def));
    action_runtime_def = apply_skill_action_status_requirement_modifiers(action_def,
        breakthrough_projection["status_requirement_modifiers"]);
    runtime = validate_skill_action_status_requirements(player, target,
        action_runtime_def);
    if (!mapp(runtime) || !runtime["ok"])
        return runtime;
    perform = apply_action_runtime_modifiers(perform,
        breakthrough_projection["action_modifiers"]);
    perform["skill_id"] = skill_id;
    if ((perform["target_policy"] || "") == "self") {
        perform["status_effects"] = append_breakthrough_action_status_effects(
            perform["status_effects"], breakthrough_projection, "self");
    } else {
        perform["status_effects"] = append_breakthrough_action_status_effects(
            perform["status_effects"], breakthrough_projection, "target");
        perform["self_status_effects"] = append_breakthrough_action_status_effects(
            perform["self_status_effects"], breakthrough_projection, "self");
    }
    perform["status_effects"] = apply_status_modifiers_to_raw_effects(
        perform["status_effects"], breakthrough_projection["status_modifiers"]);
    perform["self_status_effects"] = apply_status_modifiers_to_raw_effects(
        perform["self_status_effects"], breakthrough_projection["status_modifiers"]);
    perform["status_consumption"] = build_status_consumption(action_runtime_def);
    record_perform_phase(player, "perform_target_modifiers", phase_started_at,
        phase_cpu_started_at);

    phase_started_at = perf_counter_ns();
    phase_cpu_started_at = thread_cpu_time_ns();
    runtime = prepare_skill_action_runtime(player, "perform", runtime_action_id, perform);
    record_perform_phase(player, "perform_budget", phase_started_at,
        phase_cpu_started_at);
    if (!mapp(runtime) || !runtime["ok"])
        return runtime;

    phase_started_at = perf_counter_ns();
    phase_cpu_started_at = thread_cpu_time_ns();
    effects = merge_action_effects(
        NUMERIC_RULE_D->query_skill_action_effects(perform, player, target),
        breakthrough_projection["action_effects"]);
    apply_recovery_effects(player, effects);
    /* 词条 next_attack_*：perform 结算后转存玩家 temp，供下一次普通攻击应用 */
    if (intp(effects["next_attack_damage_factor_bp"])
        || intp(effects["next_attack_damage_bonus"])
        || intp(effects["next_attack_hit_bonus"])
        || intp(effects["next_attack_armor_break_bonus"])) {
        player->set_temp("combat/next_attack/damage_factor_bp",
            intp(effects["next_attack_damage_factor_bp"]) ? effects["next_attack_damage_factor_bp"] : 0);
        player->set_temp("combat/next_attack/damage_bonus",
            intp(effects["next_attack_damage_bonus"]) ? effects["next_attack_damage_bonus"] : 0);
        player->set_temp("combat/next_attack/hit_bonus",
            intp(effects["next_attack_hit_bonus"]) ? effects["next_attack_hit_bonus"] : 0);
        player->set_temp("combat/next_attack/armor_break_bonus",
            intp(effects["next_attack_armor_break_bonus"]) ? effects["next_attack_armor_break_bonus"] : 0);
    }
    record_perform_phase(player, "perform_effects", phase_started_at,
        phase_cpu_started_at);
    if ((perform["target_policy"] || "") == "self") {
        if (sizeof(perform["status_effects"] || ({ })) > 0) {
            COMBAT_EFFECT_D->apply_combat_status_effects(player, player,
                prepare_status_effects_with_action_effects(perform["status_effects"], effects));
        } else {
            apply_self_action_status_effect(player, "perform", skill_id, perform, effects);
        }
        player->delete_temp("combat/perform/effects");
        player->delete_temp("combat/perform/skill");
        player->delete_temp("combat/perform/action");
        consume_completed_action_statuses(player, player, perform);
        player->add_busy(runtime["busy"] || 0);
        return ([
            "ok": 1,
            "skill_id": skill_id,
            "action_id": runtime_action_id,
            "action_name": query_action_effect_name(perform, skill_id),
            "busy": runtime["busy"] || 0,
            "cooldown": runtime["cooldown"] || 0,
            "cooldown_remaining": runtime["cooldown_remaining"] || 0,
            "interval_remaining": runtime["interval_remaining"] || 0,
            "resource_costs": copy(runtime["resource_costs"] || ([ ])),
            "fatigue_bp": runtime["fatigue_bp"] || 0,
            "fatigue_factor_bp": runtime["fatigue_factor_bp"] || 10000,
            "target_policy": perform["target_policy"] || "",
            "pvp_factor_bp": perform["pvp_factor_bp"] || 10000,
            "pve_factor_bp": perform["pve_factor_bp"] || 10000,
            "effects": effects,
            "status_effects": perform["status_effects"] || ({ }),
            "statuses": build_skill_action_status_payload("perform", skill_id, perform, effects, "self"),
            "message": perform["message"] || "",
        ]);
    }

    phase_started_at = perf_counter_ns();
    phase_cpu_started_at = thread_cpu_time_ns();
    player->set_temp("combat/perform/effects", copy(effects));
    player->set_temp("combat/perform/skill", skill_id);
    perform["action_type"] = "perform";
    perform["skill_id"] = skill_id;
    perform["resource_costs"] = copy(runtime["resource_costs"] || ([ ]));
    perform["effects"] = copy(effects || ([ ]));
    player->set_temp("combat/perform/action", perform);
    player->add_busy(runtime["busy"] || 0);
    record_perform_phase(player, "perform_state_build", phase_started_at,
        phase_cpu_started_at);
    return ([
        "ok": 1,
        "skill_id": skill_id,
        "action_id": runtime_action_id,
        "action_name": query_action_effect_name(perform, skill_id),
        "busy": runtime["busy"] || 0,
        "cooldown": runtime["cooldown"] || 0,
        "cooldown_remaining": runtime["cooldown_remaining"] || 0,
        "interval_remaining": runtime["interval_remaining"] || 0,
        "resource_costs": copy(runtime["resource_costs"] || ([ ])),
        "fatigue_bp": runtime["fatigue_bp"] || 0,
        "fatigue_factor_bp": runtime["fatigue_factor_bp"] || 10000,
        "target_policy": perform["target_policy"] || "",
        "pvp_factor_bp": perform["pvp_factor_bp"] || 10000,
        "pve_factor_bp": perform["pve_factor_bp"] || 10000,
        "effects": effects,
        "status_effects": perform["status_effects"] || ({ }),
        "statuses": build_skill_action_status_payload("perform", skill_id, perform, effects,
            (perform["target_policy"] || "") == "self" ? "self" : "target"),
        "message": perform["message"] || "",
        "resolve_damage_now": perform_should_resolve_damage_now(perform),
        "target": target,
    ]);
}

mapping resolve_debate_action(object player, string action)
{
    mapping attack_profile;
    mapping resolution;
    string active_skill;
    int exert_bonus;
    int perform_bonus;

    if (!objectp(player) || !stringp(action) || action == "")
        return ([ "ok": 0, "error": "invalid_arguments" ]);

    active_skill = query_active_combat_skill(player);
    attack_profile = query_basic_attack_profile(player, active_skill);
    perform_bonus = mapp(player->query_temp("combat/perform/action")) ? 1 : 0;
    exert_bonus = mapp(player->query_temp("combat/exert/effects")) ? 1 : 0;

    resolution = ([
        "ok": 1,
        "skill_id": active_skill,
        "attack_style": attack_profile["attack_style"] || "",
        "impact": 0,
        "pressure_gain": 0,
        "stability_gain": 0,
    ]);

    switch (action) {
        case "attack":
            resolution["impact"] = (attack_profile["damage_bonus"] || 0) + perform_bonus;
            break;
        case "defend":
            resolution["stability_gain"] = 2 + (exert_bonus > 0 ? 1 : 0);
            break;
        case "focus":
            resolution["pressure_gain"] = 1 + ((attack_profile["hit_rate_bonus"] || 0) >= 5 ? 1 : 0);
            break;
        case "probe":
            resolution["impact"] = 1;
            resolution["pressure_gain"] = (attack_profile["hit_rate_bonus"] || 0) >= 5 ? 1 : 0;
            break;
        default:
            return ([ "ok": 0, "error": "invalid_action" ]);
    }

    if (perform_bonus > 0)
        clear_perform_bonus(player);
    if (exert_bonus > 0)
        clear_exert_bonus(player);

    return resolution;
}

varargs mapping exert_function(object player, string skill_id, object target, string action_id)
{
    mapping action_def;
    mapping action_runtime_def;
    mapping breakthrough_projection;
    mapping effects;
    mapping exert;
    mapping runtime;
    string runtime_action_id;

    if (!objectp(player) || !stringp(skill_id) || skill_id == "")
        return ([ "ok": 0, "error": "invalid_arguments" ]);

    action_def = query_skill_action_definition("exert", skill_id, action_id);
    runtime = query_skill_action_access(player, "exert", skill_id, action_id, action_def);
    if (!mapp(runtime) || !runtime["ok"])
        return runtime;
    exert = mapp(action_def) ? action_def["behavior"] : query_skill_action_profile("exert", skill_id);
    if (!mapp(exert))
        return ([ "ok": 0, "error": "missing_exert_behavior" ]);
    runtime_action_id = mapp(action_def) && stringp(action_def["id"]) && action_def["id"] != "" ? action_def["id"] : skill_id;

    target = resolve_skill_action_target(player, exert, target);
    if ((exert["target_policy"] || "") == "combat_target"
        && !is_valid_combat_target(player, target))
        return ([ "ok": 0, "error": "missing_combat_target" ]);
    breakthrough_projection = NUMERIC_RULE_D->query_skill_breakthrough_action_projection(
        player, skill_id, "resource", target, runtime_action_id,
        query_action_required_slot(action_def));
    action_runtime_def = apply_skill_action_status_requirement_modifiers(action_def,
        breakthrough_projection["status_requirement_modifiers"]);
    runtime = validate_skill_action_status_requirements(player, target,
        action_runtime_def);
    if (!mapp(runtime) || !runtime["ok"])
        return runtime;
    exert = apply_action_runtime_modifiers(exert,
        breakthrough_projection["action_modifiers"]);
    exert["skill_id"] = skill_id;
    if ((exert["target_policy"] || "") == "self") {
        exert["status_effects"] = append_breakthrough_action_status_effects(
            exert["status_effects"], breakthrough_projection, "self");
    } else {
        exert["status_effects"] = append_breakthrough_action_status_effects(
            exert["status_effects"], breakthrough_projection, "target");
        exert["self_status_effects"] = append_breakthrough_action_status_effects(
            exert["self_status_effects"], breakthrough_projection, "self");
    }
    exert["status_effects"] = apply_status_modifiers_to_raw_effects(
        exert["status_effects"], breakthrough_projection["status_modifiers"]);
    exert["self_status_effects"] = apply_status_modifiers_to_raw_effects(
        exert["self_status_effects"], breakthrough_projection["status_modifiers"]);
    exert["status_consumption"] = build_status_consumption(action_runtime_def);
    runtime = prepare_skill_action_runtime(player, "exert", runtime_action_id, exert);
    if (!mapp(runtime) || !runtime["ok"])
        return runtime;

    effects = merge_action_effects(
        NUMERIC_RULE_D->query_skill_action_effects(exert, player, target),
        breakthrough_projection["action_effects"]);
    apply_recovery_effects(player, effects);
    if ((exert["target_policy"] || "") == "combat_target") {
        exert["skill_id"] = skill_id;
        exert["action_type"] = "exert";
        exert["resource_costs"] = copy(runtime["resource_costs"] || ([ ]));
        exert["effects"] = copy(effects || ([ ]));
        player->set_temp("combat/perform/effects", copy(effects));
        player->set_temp("combat/perform/skill", skill_id);
        player->set_temp("combat/perform/action", exert);
        player->add_busy(runtime["busy"] || 0);
        return ([
            "ok": 1,
            "skill_id": skill_id,
            "action_id": runtime_action_id,
            "action_name": query_action_effect_name(exert, skill_id),
            "busy": runtime["busy"] || 0,
            "cooldown": runtime["cooldown"] || 0,
            "cooldown_remaining": runtime["cooldown_remaining"] || 0,
            "interval_remaining": runtime["interval_remaining"] || 0,
            "resource_costs": copy(runtime["resource_costs"] || ([ ])),
            "fatigue_bp": runtime["fatigue_bp"] || 0,
            "fatigue_factor_bp": runtime["fatigue_factor_bp"] || 10000,
            "target_policy": exert["target_policy"] || "",
            "pvp_factor_bp": exert["pvp_factor_bp"] || 10000,
            "pve_factor_bp": exert["pve_factor_bp"] || 10000,
            "effects": effects,
            "status_effects": exert["status_effects"] || ({ }),
            "statuses": build_skill_action_status_payload("exert", skill_id, exert, effects, "target"),
            "message": exert["message"] || "",
            "resolve_damage_now": 1,
            "target": target,
        ]);
    }
    consume_completed_action_statuses(player, player, exert);
    if (sizeof(exert["status_effects"] || ({ })) > 0) {
        COMBAT_EFFECT_D->apply_combat_status_effects(player, player,
            prepare_status_effects_with_action_effects(exert["status_effects"], effects));
    } else {
        apply_self_action_status_effect(player, "exert", skill_id, exert, effects);
    }

    player->set_temp("combat/exert/skill", skill_id);
    player->set_temp("combat/exert/action", exert);
    player->set_temp("combat/exert/effects", effects);
    player->add_busy(runtime["busy"] || 0);
    if (skill_id == "force" && runtime_action_id == "basic_force_exert")
        QUEST_D->record_action_progress(player,
            "onboarding_basic_force_exerted", skill_id, "success");
    return ([
        "ok": 1,
        "skill_id": skill_id,
        "action_id": runtime_action_id,
        "action_name": query_action_effect_name(exert, skill_id),
        "busy": runtime["busy"] || 0,
        "cooldown": runtime["cooldown"] || 0,
        "cooldown_remaining": runtime["cooldown_remaining"] || 0,
        "interval_remaining": runtime["interval_remaining"] || 0,
        "resource_costs": copy(runtime["resource_costs"] || ([ ])),
        "fatigue_bp": runtime["fatigue_bp"] || 0,
        "fatigue_factor_bp": runtime["fatigue_factor_bp"] || 10000,
        "target_policy": exert["target_policy"] || "",
        "pvp_factor_bp": exert["pvp_factor_bp"] || 10000,
        "pve_factor_bp": exert["pve_factor_bp"] || 10000,
        "effects": effects,
        "status_effects": exert["status_effects"] || ({ }),
        "statuses": build_skill_action_status_payload("exert", skill_id, exert, effects, "self"),
        "message": exert["message"] || "",
    ]);
}
