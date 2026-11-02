/**
 * @file    combat_npc_skills.c
 * @brief   配置化 NPC 特技执行器
 *
 * 所有对象访问与效果提交都在战斗主线程完成。标准伤害仍经 COMBAT_DAMAGE_D
 * 的纯快照/proposal 链计算；本服务只按配置缩放并提交到目标对象。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>

inherit "/lib/framework/daemon";

#define BP_BASE 10000
#define COMBAT_MESSAGING_HELPER "/daemons/services/gameplay/combat/combat_messaging"
#define PENDING_PATH "combat/npc_skill_pending"

void clear_pending_npc_combat_skill(object actor);

private int participants_are_valid(object attacker, object defender);
private int execute_skill(object attacker, object defender, mapping skill,
    int nonlethal);

void clear_pending_npc_combat_skill(object actor)
{
    if (objectp(actor))
        actor->delete_temp(PENDING_PATH);
}

/*
 * 特化 AI 专用：按技能 ID 立即执行一个 NPC 技能（不走通用触发器）。
 * 供声明 query_npc_ai_override 的特化 NPC 调用。
 */
int execute_npc_skill_direct(object attacker, object defender, string skill_id,
    int cooldown_seconds)
{
    mapping raw;
    mapping skill;

    if (!objectp(attacker) || !objectp(defender)
        || !stringp(skill_id) || skill_id == "")
        return 0;
    if (!participants_are_valid(attacker, defender))
        return 0;
    raw = ([ "id": skill_id, "chance_bp": 10000,
        "cooldown_seconds": cooldown_seconds > 0 ? cooldown_seconds : 2 ]);
    skill = NUMERIC_RULE_D->normalize_npc_combat_skill_definition(skill_id, raw);
    if (!mapp(skill) || !skill["enabled"])
        return 0;
    if (!execute_skill(attacker, defender, skill, 0))
        return 0;
    NUMERIC_RULE_D->record_npc_combat_skill_trigger(attacker, ([ "selected": skill ]));
    return 1;
}

private string query_actor_name(object actor)
{
    string name;

    if (!objectp(actor))
        return "对手";
    name = actor->query_name() || actor->query("name") || "对手";
    return stringp(name) && name != "" ? name : "对手";
}

private int participants_are_valid(object attacker, object defender)
{
    return objectp(attacker) && objectp(defender)
        && environment(attacker) == environment(defender)
        && (attacker->query("combat/hp") || 0) > 0
        && (defender->query("combat/hp") || 0) > 0;
}

private int is_explicit_encounter_target(object attacker, object candidate)
{
    object fight_target;

    if (!objectp(attacker) || !objectp(candidate))
        return 0;
    if (function_exists("query_fight_object", candidate)) {
        fight_target = call_other(candidate, "query_fight_object");
        if (fight_target == attacker)
            return 1;
    }
    if (function_exists("is_enemy", candidate)
        && call_other(candidate, "is_enemy", attacker))
        return 1;
    return function_exists("is_enemy", attacker)
        && call_other(attacker, "is_enemy", candidate);
}

private int apply_scaled_damage(object attacker, object defender, mapping skill,
    int nonlethal)
{
    mapping damage_report;
    mapping protection;
    string skill_id;
    string skill_name;
    int base_damage;
    int factor_bp;
    int final_damage;
    int defender_hp;

    if (!participants_are_valid(attacker, defender) || !mapp(skill))
        return 0;

    base_damage = COMBAT_DAMAGE_D->calculate_damage(attacker, defender);
    factor_bp = intp(skill["damage_factor_bp"]) ? skill["damage_factor_bp"] : BP_BASE;
    final_damage = base_damage * factor_bp / BP_BASE;
    if (intp(skill["damage"]) && skill["damage"] > 0)
        final_damage = skill["damage"] + random((skill["variance"] || 0) + 1);
    if (final_damage < 1)
        final_damage = 1;

    skill_id = skill["id"] || "npc_skill";
    skill_name = skill["name"] || skill_id;
    protection = NUMERIC_RULE_D->apply_combat_pvp_damage_protection(
        attacker, defender, final_damage, ([
            "damage_source": "npc_skill",
            "skill_id": skill_id,
        ]));
    if (mapp(protection) && intp(protection["applied_damage"]))
        final_damage = protection["applied_damage"];
    /* L-135：切磋（非致命）下 NPC 特技与普通攻击同一钳制，不得击倒玩家。 */
    if (nonlethal) {
        defender_hp = defender->query("combat/hp") || 0;
        if (defender_hp <= final_damage)
            final_damage = defender_hp > 1 ? defender_hp - 1 : 0;
    }
    if (final_damage <= 0)
        return 0;

    defender->receive_damage("combat/hp", final_damage, attacker);
    damage_report = mapp(protection) ? copy(protection) : ([ ]);
    damage_report += ([
        "event": "npc_skill",
        "damage_source": "npc_skill",
        "skill_id": skill_id,
        "skill_name": skill_name,
        "message": sprintf("%s使出「%s」，令%s受到%d点伤害。",
            query_actor_name(attacker), skill_name, query_actor_name(defender), final_damage),
        "result_label": skill_name,
        "applied_damage": final_damage,
    ]);
    attacker->set_temp("combat/last_pvp_damage_protection", protection);
    defender->set_temp("combat/last_received_pvp_damage_protection", protection);
    call_other(COMBAT_MESSAGING_HELPER, "send_damage_packets",
        attacker, defender, final_damage,
        defender->query("combat/hp") || 0, damage_report);
    return 1;
}

private object *query_area_targets(object attacker, object primary, int max_targets)
{
    object candidate;
    object room;
    object *targets;

    targets = ({ });
    if (!participants_are_valid(attacker, primary))
        return targets;
    targets += ({ primary });
    if (max_targets <= 1)
        return targets;

    room = environment(attacker);
    foreach (candidate in all_inventory(room)) {
        if (sizeof(targets) >= max_targets)
            break;
        if (!objectp(candidate) || candidate == attacker || candidate == primary)
            continue;
        if (!living(candidate) || (candidate->query("combat/hp") || 0) <= 0)
            continue;
        if (!function_exists("is_player", candidate) || !call_other(candidate, "is_player"))
            continue;
        if (!is_explicit_encounter_target(attacker, candidate))
            continue;
        targets += ({ candidate });
    }
    return targets;
}

private int execute_area_damage(object attacker, object defender, mapping skill,
    int nonlethal)
{
    object target;
    int applied;
    int max_targets;

    max_targets = skill["max_targets"] || 1;
    /* L-135：区域技对同房间其他玩家同样使用 nonlethal 钳制。 */
    foreach (target in query_area_targets(attacker, defender, max_targets))
        applied += apply_scaled_damage(attacker, target, skill, nonlethal);
    return applied > 0;
}

private int execute_status_apply(object attacker, object defender, mapping skill)
{
    mapping result;

    result = COMBAT_EFFECT_D->apply_combat_status_effects(
        attacker, defender, skill["status_effects"]);
    return mapp(result) && (result["applied"] || 0) > 0;
}

private int execute_resource_pressure(object attacker, object defender, mapping skill)
{
    mapping resource_damage;
    string resource;
    int applied;
    int before;
    int damage;

    if (!participants_are_valid(attacker, defender))
        return 0;
    resource_damage = skill["resource_damage"];
    foreach (resource, damage in resource_damage || ([ ])) {
        if (member_array(resource, ({ "mp", "ep" })) == -1 || damage <= 0)
            continue;
        before = defender->query("combat/" + resource) || 0;
        defender->set("combat/" + resource, before > damage ? before - damage : 0);
        if ((defender->query("combat/" + resource) || 0) < before)
            applied = 1;
    }
    if (applied) {
        msg_to_room(environment(attacker),
            sprintf("%s使出「%s」，逼得%s气息与体力一滞。\n",
                query_actor_name(attacker), skill["name"] || skill["id"],
                query_actor_name(defender)), ({ }));
        send_xk_status(defender);
    }
    return applied;
}

private int execute_skill(object attacker, object defender, mapping skill,
    int nonlethal)
{
    string type;

    if (!mapp(skill))
        return 0;
    type = skill["type"] || "direct_damage";
    switch (type) {
    case "direct_damage":
        return apply_scaled_damage(attacker, defender, skill, nonlethal);
    case "area_damage":
        return execute_area_damage(attacker, defender, skill, nonlethal);
    case "status_apply":
        return execute_status_apply(attacker, defender, skill);
    case "resource_pressure":
        return execute_resource_pressure(attacker, defender, skill);
    }
    return 0;
}

private int prepare_windup(object attacker, object defender, mapping trigger)
{
    mapping skill;
    mixed *prepare_effects;

    skill = trigger["selected"];
    if (!mapp(skill))
        return 0;
    prepare_effects = skill["prepare_status_effects"];
    if (arrayp(prepare_effects) && sizeof(prepare_effects) > 0)
        COMBAT_EFFECT_D->apply_combat_status_effects(attacker, attacker, prepare_effects);
    attacker->set_temp(PENDING_PATH, ([
        "target": defender,
        "skill": copy(skill),
        "prepared_at": time(),
    ]));
    msg_to_room(environment(attacker),
        sprintf("%s%s。\n", query_actor_name(attacker),
            skill["telegraph"] || "蓄势待发"), ({ }));
    NUMERIC_RULE_D->record_npc_combat_skill_trigger(attacker, trigger);
    return 1;
}

private int execute_pending_skill(object attacker, object defender, int nonlethal)
{
    mapping pending;
    mapping skill;
    object target;

    pending = attacker->query_temp(PENDING_PATH);
    if (!mapp(pending))
        return -1;
    target = pending["target"];
    skill = pending["skill"];
    clear_pending_npc_combat_skill(attacker);
    if (target != defender || !participants_are_valid(attacker, target))
        return 0;
    return execute_skill(attacker, target, skill, nonlethal);
}

int maybe_trigger_npc_combat_skill(object attacker, object defender, int nonlethal)
{
    mapping skill;
    mapping trigger;
    int pending_result;

    if (!participants_are_valid(attacker, defender)) {
        clear_pending_npc_combat_skill(attacker);
        return 0;
    }
    if (!function_exists("is_npc", attacker) || !call_other(attacker, "is_npc"))
        return 0;

    /*
     * 特化 AI 层：NPC 可声明 query_npc_ai_override() 返回特化决策。
     * 返回 ([ "ok":1, "trigger":1 ]) = 特化已接管本回合；
     * 返回 ([ "ok":1, "trigger":0 ]) = 特化不接管，回退通用引擎；
     * 无声明 = 走通用引擎（默认所有 NPC）。
     */
    if (function_exists("query_npc_ai_override", attacker)) {
        mapping override;

        override = call_other(attacker, "query_npc_ai_override", defender);
        if (mapp(override) && override["ok"]) {
            if (override["trigger"])
                return 1;
        }
    }

    pending_result = execute_pending_skill(attacker, defender, nonlethal);
    if (pending_result >= 0)
        return pending_result;

    trigger = NUMERIC_RULE_D->query_npc_combat_skill_trigger(attacker, defender);
    if (!mapp(trigger) || !trigger["trigger"])
        return 0;
    skill = trigger["selected"];
    if (!mapp(skill))
        return 0;
    if ((skill["windup_rounds"] || 0) > 0)
        return prepare_windup(attacker, defender, trigger);
    if (!execute_skill(attacker, defender, skill, nonlethal))
        return 0;
    NUMERIC_RULE_D->record_npc_combat_skill_trigger(attacker, trigger);
    return 1;
}
