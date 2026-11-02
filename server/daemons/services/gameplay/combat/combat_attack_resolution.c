/**
 * @file    combat_attack_resolution.c
 * @brief   战斗单次攻击结算子服务



 */

#pragma strict_types
#pragma save_types

#include <system/ansi.h>
#include <system/services.h>
#include <system/xk_protocol.h>

private string query_attack_action_display_name(mapping attack_profile)
{
    string style;

    if (!mapp(attack_profile))
        return "招式";
    if (stringp(attack_profile["action_name"]) && attack_profile["action_name"] != "")
        return attack_profile["action_name"];
    style = attack_profile["attack_style"] || "";
    switch (style) {
    case "accurate":
        return "精准一击";
    case "nimble":
        return "灵巧一击";
    case "steady":
        return "沉稳一击";
    case "plain":
        return "平实一击";
    }
    return "招式";
}

private int apply_parry(object combat_service, object attacker, object defender, int damage)
{
    string attacker_name;
    string defender_name;
    int attack;
    int parry;
    int parry_rate;
    int reduction;

    if (defender->query("training_dummy"))
        return damage;
    parry = combat_service->query_parry(defender, attacker);
    attack = combat_service->query_attack(attacker, defender);
    parry_rate = 5;

    if (parry > 0)
        parry_rate += (parry * 50) / (parry + attack);

    if (parry_rate > 70)
        parry_rate = 70;

    if (random(100) >= parry_rate)
        return damage;

    reduction = 50 + (parry * 30) / 100;
    if (reduction > 80)
        reduction = 80;

    damage = damage * (100 - reduction) / 100;
    if (damage < 1)
        damage = 1;

    attacker_name = attacker->query_name() || attacker->query("name") || "对手";
    defender_name = defender->query_name() || defender->query("name") || "对手";

    combat_service->combat_packet(defender, XK_CBRD, ([
        "event": "parry",
        "source": attacker_name,
        "target": defender_name,
        "damage": damage,
        "message": sprintf("你顺势卸开了%s的大半攻势。", attacker_name),
        "result_label": "招架"
    ]));
    combat_service->combat_packet(attacker, XK_CBRD, ([
        "event": "parry",
        "source": attacker_name,
        "target": defender_name,
        "damage": damage,
        "message": sprintf("%s架开了你的来势，劲力被卸去不少。", defender_name),
        "result_label": "招架"
    ]));

    return damage;
}

private int query_pending_perform_damage_bonus(object attacker, object defender)
{
    mapping action;
    mapping effects;

    if (!objectp(attacker))
        return 0;

    effects = attacker->query_temp("combat/perform/effects");
    if (mapp(effects))
        return effects["damage_bonus"] || 0;

    action = attacker->query_temp("combat/perform/action");
    if (mapp(action)) {
        effects = NUMERIC_RULE_D->query_skill_action_effects(action, attacker, defender);
        return effects["damage_bonus"] || 0;
    }
    return 0;
}

/* perform 伤害倍率（damage_factor_bp，对齐数值指南 perform 倍率档）——
 * 无 perform 或未配置时返回 10000（1.0 倍，行为不变）。 */
private int query_pending_perform_damage_factor(object attacker, object defender)
{
    mapping action;
    mapping effects;
    int factor_bp;

    if (!objectp(attacker))
        return 10000;

    effects = attacker->query_temp("combat/perform/effects");
    if (mapp(effects)) {
        factor_bp = intp(effects["damage_factor_bp"]) ? effects["damage_factor_bp"] : 10000;
        return factor_bp;
    }

    action = attacker->query_temp("combat/perform/action");
    if (mapp(action)) {
        effects = NUMERIC_RULE_D->query_skill_action_effects(action, attacker, defender);
        factor_bp = intp(effects["damage_factor_bp"]) ? effects["damage_factor_bp"] : 10000;
        return factor_bp;
    }
    return 10000;
}

private string query_pending_action_effect_name(mapping action, string skill_id)
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
    return "招式状态";
}

private mixed *merge_pending_effects_into_explicit_status_effects(mixed *raw_effects, mapping effects)
{
    mapping effect;
    mixed *merged;
    mixed raw_effect;
    string type;
    int bonus;
    int merged_effects;

    merged = ({ });
    foreach (raw_effect in raw_effects || ({ })) {
        if (!mapp(raw_effect))
            continue;
        effect = copy(raw_effect);
        effect["suppress_apply_lifecycle_message"] = 1;
        if (!merged_effects && mapp(effects)) {
            type = effect["type"] || "";
            bonus = effects["debuff_bonus"] || 0;
            if (bonus > 0) {
                if (!mapp(effect["stat_effects"]))
                    effect["stat_effects"] = ([ ]);
                effect["stat_effects"]["hit"] = (effect["stat_effects"]["hit"] || 0) - bonus;
            }
            bonus = (effects["dot_bonus"] || 0) + (effects["poison_bonus"] || 0);
            if (bonus > 0 && type == "dot")
                effect["damage"] = (effect["damage"] || 0) + bonus;
            if ((effects["control_bonus"] || 0) > 0 && type == "control")
                effect["action_block"] = 1;
            merged_effects = 1;
        }
        merged += ({ effect });
    }
    return merged;
}

private mapping build_perform_bonus_status_effect(string skill_id, mapping effects, mapping action)
{
    mapping effect;
    int control_bonus;
    int debuff_bonus;
    int dot_bonus;
    string type;

    if (!mapp(effects))
        return 0;
    control_bonus = effects["control_bonus"] || 0;
    debuff_bonus = effects["debuff_bonus"] || 0;
    dot_bonus = (effects["dot_bonus"] || 0) + (effects["poison_bonus"] || 0);
    if (control_bonus <= 0 && debuff_bonus <= 0 && dot_bonus <= 0)
        return 0;
    if (!stringp(skill_id) || skill_id == "")
        skill_id = "unknown";
    type = control_bonus > 0 ? "control" : (dot_bonus > 0 ? "dot" : "debuff");

    effect = ([
        "id": sprintf("perform_%s_target_effects", skill_id),
        "name": query_pending_action_effect_name(action, skill_id),
        "type": type,
        "duration_rounds": type == "control" ? (control_bonus >= 5 ? 2 : 1) : 2,
        "tick_interval_rounds": 1,
        "stack_rule": "refresh",
        "max_stacks": 1,
        "suppress_apply_lifecycle_message": 1,
    ]);

    if (control_bonus > 0)
        effect["action_block"] = 1;
    if (dot_bonus > 0)
        effect["damage"] = dot_bonus;
    if (debuff_bonus > 0)
        effect["stat_effects"] = ([ "hit": -debuff_bonus ]);

    return effect;
}

private mapping build_perform_bonus_self_status_effect(string skill_id, string key, int bonus, mapping action)
{
    mapping effect;

    if (!stringp(key) || key == "" || bonus == 0)
        return 0;
    if (key != "dodge" && key != "attack_speed" && key != "parry"
        && key != "defense" && key != "critical_resist")
        return 0;
    if (!stringp(skill_id) || skill_id == "")
        skill_id = "unknown";

    effect = ([
        "id": sprintf("perform_%s_self_%s", skill_id, key),
        "name": query_pending_action_effect_name(action, skill_id),
        "type": bonus > 0 ? "buff" : "debuff",
        "duration_rounds": 2,
        "tick_interval_rounds": 1,
        "stack_rule": "refresh",
        "max_stacks": 1,
        "suppress_apply_lifecycle_message": 1,
        "stat_effects": ([ key: bonus ]),
    ]);
    return effect;
}

private mixed *query_pending_perform_bonus_status_effects(object attacker)
{
    mapping action;
    mapping effect;
    mapping effects;
    mixed *status_effects;
    string skill_id;

    status_effects = ({ });
    if (!objectp(attacker))
        return status_effects;

    effects = attacker->query_temp("combat/perform/effects");
    if (!mapp(effects))
        return status_effects;

    skill_id = attacker->query_temp("combat/perform/skill") || "";
    action = attacker->query_temp("combat/perform/action");
    if (skill_id == "" && mapp(action) && stringp(action["skill_id"]))
        skill_id = action["skill_id"];

    effect = build_perform_bonus_status_effect(skill_id, effects, action);
    if (mapp(effect))
        status_effects += ({ effect });

    return status_effects;
}

private int query_pending_perform_evasion_bonus(mapping effects)
{
    int bonus;

    if (!mapp(effects))
        return 0;

    bonus = 0;
    bonus += effects["dodge"] || 0;
    bonus += effects["dodge_bonus"] || 0;
    bonus += effects["move_speed"] || 0;
    bonus += effects["cleanse_bonus"] || 0;
    bonus += effects["lock_break_bonus"] || 0;
    bonus -= effects["detection_pressure"] || 0;
    bonus -= effects["negative_movement"] || 0;
    return bonus;
}

private int query_pending_perform_parry_bonus(mapping effects)
{
    if (!mapp(effects))
        return 0;
    return (effects["parry_bonus"] || 0) + (effects["counter_bonus"] || 0);
}

private int query_pending_perform_defense_bonus(mapping effects)
{
    if (!mapp(effects))
        return 0;
    return (effects["defense"] || 0) + (effects["guard_bonus"] || 0) + (effects["shield_bonus"] || 0);
}

private int query_pending_perform_critical_resist_bonus(mapping effects)
{
    if (!mapp(effects))
        return 0;
    return effects["critical_resist"] || 0;
}

private mixed *query_pending_perform_self_status_effects(object attacker)
{
    mapping action;
    mapping effect;
    mapping effects;
    mixed *status_effects;
    string skill_id;

    status_effects = ({ });
    if (!objectp(attacker))
        return status_effects;

    effects = attacker->query_temp("combat/perform/effects");
    if (!mapp(effects))
        return status_effects;

    skill_id = attacker->query_temp("combat/perform/skill") || "";
    action = attacker->query_temp("combat/perform/action");
    if (skill_id == "" && mapp(action) && stringp(action["skill_id"]))
        skill_id = action["skill_id"];

    effect = build_perform_bonus_self_status_effect(skill_id, "dodge", query_pending_perform_evasion_bonus(effects), action);
    if (mapp(effect))
        status_effects += ({ effect });
    effect = build_perform_bonus_self_status_effect(skill_id, "parry", query_pending_perform_parry_bonus(effects), action);
    if (mapp(effect))
        status_effects += ({ effect });
    effect = build_perform_bonus_self_status_effect(skill_id, "defense", query_pending_perform_defense_bonus(effects), action);
    if (mapp(effect))
        status_effects += ({ effect });
    effect = build_perform_bonus_self_status_effect(skill_id, "critical_resist", query_pending_perform_critical_resist_bonus(effects), action);
    if (mapp(effect))
        status_effects += ({ effect });
    effect = build_perform_bonus_self_status_effect(skill_id, "attack_speed", effects["attack_speed"] || 0, action);
    if (mapp(effect))
        status_effects += ({ effect });

    return status_effects;
}

private void apply_pending_perform_status_effects(object attacker, object defender)
{
    mapping action;
    mapping effect;
    mapping effects;
    mixed *self_status_effects;
    mixed *target_status_effects;

    if (!objectp(attacker) || !objectp(defender))
        return;

    action = attacker->query_temp("combat/perform/action");
    effects = attacker->query_temp("combat/perform/effects");
    target_status_effects = ({ });
    if (mapp(action) && arrayp(action["status_effects"]) && sizeof(action["status_effects"]) > 0
        && (action["target_policy"] || "") != "self")
        target_status_effects += merge_pending_effects_into_explicit_status_effects(action["status_effects"], effects);
    else
        target_status_effects += query_pending_perform_bonus_status_effects(attacker);
    if (sizeof(target_status_effects) > 0)
        COMBAT_EFFECT_D->apply_combat_status_effects(attacker, defender, target_status_effects);

    self_status_effects = query_pending_perform_self_status_effects(attacker);
    if (mapp(action) && arrayp(action["self_status_effects"])) {
        foreach (effect in action["self_status_effects"]) {
            if (mapp(effect))
                self_status_effects += ({ copy(effect) });
        }
    }
    if (sizeof(self_status_effects) > 0)
        COMBAT_EFFECT_D->apply_combat_status_effects(attacker, attacker, self_status_effects);
}

private void clear_pending_perform_action(object attacker)
{
    if (!objectp(attacker))
        return;

    attacker->delete_temp("combat/perform/effects");
    attacker->delete_temp("combat/perform/skill");
    attacker->delete_temp("combat/perform/action");
}

/* The onboarding target teaches a specific, already validated perform. Keep
 * this exception here so normal combat hit chances never inherit it. */
private int is_valid_onboarding_sparring_perform(object attacker, object defender,
    mapping pending_action)
{
    string skill_id;

    if (!objectp(attacker) || !objectp(defender) || !mapp(pending_action))
        return 0;
    if (!defender->query("onboarding_sparring_target")
        || (pending_action["action_type"] || "") != "perform")
        return 0;
    skill_id = stringp(pending_action["skill_id"])
        ? pending_action["skill_id"] : "";
    if (!SKILL_BREAKTHROUGH_D->is_public_skill(skill_id))
        return 0;
    return SKILL_BEHAVIOR_D->is_active_skill_for_slot(attacker, "outer", skill_id);
}

int do_single_attack(object combat_service, object attacker, object defender, int nonlethal)
{
    mapping attack_profile;
    mapping attack_budget;
    mapping damage_protection;
    mapping pending_action;
    mapping resource_budget;
    mapping skill_def;
    string active_skill;
    string action_name;
    string damage_context;
    string damage_source;
    string skill_name;
    int damage;
    int defender_hp;
    int final_damage;
    int next_attack_armor_break_bonus;
    int next_attack_damage_bonus;
    int next_attack_factor_bp;
    int next_attack_hit_bonus;
    int defeated;
    int hit_rate;
    int parry_reduction_bp;
    int post_parry_damage;
    int force_sparring_hit;
    int sparring_complete;

    if (!objectp(combat_service) || !objectp(attacker) || !objectp(defender))
        return 0;

    if (attacker->query("no_attack")) {
        pending_action = attacker->query_temp("combat/perform/action");
        if (mapp(pending_action))
            clear_pending_perform_action(attacker);
        return 0;
    }

    resource_budget = NUMERIC_RULE_D->apply_combat_resource_budget(attacker, ([
        "action_type": "basic_attack",
    ]));
    if (mapp(resource_budget) && resource_budget["action_blocked"]) {
        combat_service->combat_msg(attacker, YEL "你气力不足，攻势一滞。" NOR "\n");
        return 0;
    }

    pending_action = attacker->query_temp("combat/perform/action");
    force_sparring_hit = is_valid_onboarding_sparring_perform(attacker, defender,
        pending_action);
    /* 词条 next_attack_*：普通攻击（无 perform）时应用并清除 */
    if (!mapp(pending_action)) {
        next_attack_factor_bp = intp(attacker->query_temp("combat/next_attack/damage_factor_bp"))
            ? attacker->query_temp("combat/next_attack/damage_factor_bp") : 0;
        next_attack_damage_bonus = intp(attacker->query_temp("combat/next_attack/damage_bonus"))
            ? attacker->query_temp("combat/next_attack/damage_bonus") : 0;
        next_attack_hit_bonus = intp(attacker->query_temp("combat/next_attack/hit_bonus"))
            ? attacker->query_temp("combat/next_attack/hit_bonus") : 0;
        next_attack_armor_break_bonus = intp(attacker->query_temp("combat/next_attack/armor_break_bonus"))
            ? attacker->query_temp("combat/next_attack/armor_break_bonus") : 0;
        if (next_attack_factor_bp > 0 || next_attack_damage_bonus > 0
            || next_attack_hit_bonus > 0 || next_attack_armor_break_bonus > 0) {
            attacker->delete_temp("combat/next_attack/damage_factor_bp");
            attacker->delete_temp("combat/next_attack/damage_bonus");
            attacker->delete_temp("combat/next_attack/hit_bonus");
            attacker->delete_temp("combat/next_attack/armor_break_bonus");
        }
    }
    SKILL_BEHAVIOR_D->begin_combat_attack(attacker);
    hit_rate = combat_service->calculate_hit_rate(attacker, defender);
    if (next_attack_hit_bonus > 0)
        hit_rate += next_attack_hit_bonus;
    if (defender->query("training_dummy"))
        hit_rate = 100;
    if (!force_sparring_hit && random(100) >= hit_rate) {
        attack_budget = NUMERIC_RULE_D->query_combat_attack_budget_report(([
            "hit_rate": hit_rate,
            "damage": 0,
            "final_damage": 0,
        ]));
        attacker->set_temp("combat/last_attack_budget", attack_budget);
        combat_service->combat_packet(attacker, XK_CBRD, ([
            "event": "dodge",
            "opponent": defender->query_name(),
            "message": sprintf("你这一击擦身而过，被%s避开了。", defender->query_name()),
            "result_label": "落空"
        ]));
        combat_service->combat_packet(defender, XK_CBRD, ([
            "event": "dodge",
            "opponent": attacker->query_name(),
            "message": sprintf("你身形一侧，避开了%s的这一击。", attacker->query_name()),
            "result_label": "闪避"
        ]));
        if (mapp(pending_action))
            clear_pending_perform_action(attacker);
        return 0;
    }

    damage = combat_service->calculate_damage(attacker, defender);
    /* perform 倍率（1.0 默认；快攻低倍率/爆发高倍率拉开武学指纹） */
    damage = damage * query_pending_perform_damage_factor(attacker, defender) / 10000;
    /* 词条 next_attack_*：普通攻击临时倍率/伤害/破甲 */
    if (!mapp(attacker->query_temp("combat/perform/action"))) {
        if (next_attack_factor_bp > 0)
            damage = damage * next_attack_factor_bp / 10000;
        damage += next_attack_damage_bonus;
        damage += next_attack_armor_break_bonus * 3 / 2;
    }
    active_skill = SKILL_BEHAVIOR_D->query_active_combat_skill(attacker);
    attack_profile = SKILL_BEHAVIOR_D->query_basic_attack_profile(attacker, active_skill);
    skill_def = SKILL_D->get_skill_definition(active_skill);
    skill_name = mapp(skill_def) && stringp(skill_def["name"]) && skill_def["name"] != ""
        ? skill_def["name"]
        : "招式";
    action_name = query_attack_action_display_name(attack_profile);
    pending_action = attacker->query_temp("combat/perform/action");
    if (mapp(pending_action)) {
        skill_def = SKILL_D->get_skill_definition(pending_action["skill_id"] || active_skill);
        if (mapp(skill_def) && stringp(skill_def["name"]) && skill_def["name"] != "")
            skill_name = skill_def["name"];
        action_name = query_pending_action_effect_name(pending_action, pending_action["skill_id"] || active_skill);
    }
    damage += attack_profile["damage_bonus"] || 0;
    damage += query_pending_perform_damage_bonus(attacker, defender);
    damage = SKILL_BEHAVIOR_D->apply_unarmed_damage_penalty(attacker, damage);
    combat_service->clear_derived_attribute_cache();
    post_parry_damage = apply_parry(combat_service, attacker, defender, damage);
    final_damage = post_parry_damage;
    damage_source = mapp(pending_action)
        ? (pending_action["action_type"] || "perform") : "basic_attack";
    damage_context = NUMERIC_RULE_D->query_numeric_context(attacker, defender);
    damage_protection = NUMERIC_RULE_D->apply_combat_pvp_damage_protection(
        attacker, defender, final_damage, ([
            "damage_source": damage_source,
            "damage_context": damage_context,
            "action": pending_action,
            "resource_costs": mapp(pending_action) && mapp(pending_action["resource_costs"])
                ? pending_action["resource_costs"]
                : ([ ]),
        ]));
    if (mapp(damage_protection) && intp(damage_protection["applied_damage"]))
        final_damage = damage_protection["applied_damage"];
    if (mapp(damage_protection)) {
        damage_protection["event"] = damage_source;
        damage_protection["skill_id"] = mapp(pending_action) && stringp(pending_action["skill_id"])
            ? pending_action["skill_id"] : active_skill;
        damage_protection["skill_name"] = skill_name;
        damage_protection["action_name"] = action_name;
        damage_protection["attack_style"] = attack_profile["attack_style"] || "";
        damage_protection["result_label"] = damage_source == "exert" ? "运功命中"
            : (damage_source == "perform" ? "招式命中" : "命中");
        if (mapp(pending_action)) {
            damage_protection["resource_costs"] = mapp(pending_action["resource_costs"])
                ? copy(pending_action["resource_costs"])
                : ([ ]);
            damage_protection["effects"] = mapp(pending_action["effects"])
                ? copy(pending_action["effects"])
                : ([ ]);
            damage_protection["status_effects"] = arrayp(pending_action["status_effects"])
                ? pending_action["status_effects"]
                : ({ });
            if (stringp(pending_action["message"]) && pending_action["message"] != "")
                damage_protection["message"] = pending_action["message"];
        } else if (stringp(attack_profile["message"]) && attack_profile["message"] != "") {
            damage_protection["message"] = attack_profile["message"];
        }
    }
    defender_hp = defender->query("combat/hp") || 0;
    defeated = defender_hp <= final_damage;
    if (nonlethal && defeated)
        final_damage = defender_hp > 1 ? defender_hp - 1 : 0;

    parry_reduction_bp = damage > 0 && post_parry_damage < damage
        ? (damage - post_parry_damage) * 10000 / damage : 0;
    /* 总伤害归属追踪（掉落 pick_rule=total_damage 用） */
    if (final_damage > 0 && objectp(defender))
        defender->add_temp("combat/damage_by/" + file_name(attacker), final_damage);
    attack_budget = NUMERIC_RULE_D->query_combat_attack_budget_report(([
        "hit_rate": hit_rate,
        "damage": damage,
        "final_damage": final_damage,
        "parry_reduction_bp": parry_reduction_bp,
    ]));
    attacker->set_temp("combat/last_attack_budget", attack_budget);
    defender->set_temp("combat/last_received_attack_budget", attack_budget);
    attacker->set_temp("combat/last_pvp_damage_protection", damage_protection);
    defender->set_temp("combat/last_received_pvp_damage_protection", damage_protection);
    if (mapp(damage_protection))
        damage_protection["applied_damage"] = final_damage;

    if (final_damage > 0) {
        defender->receive_damage("combat/hp", final_damage, attacker);
        if (!objectp(defender)) {
            clear_pending_perform_action(attacker);
            return defeated;
        }
        /* 多人战斗仇恨：实际伤害 1:1 记入攻击者仇恨 */
        combat_service->record_combat_threat(attacker, defender, final_damage);
    }
    /* The sparring lesson is about deliberately using a learned action. A
     * regular combat round may still wear the training weapon, but must not
     * end the lesson before the player can perform the selected technique. */
    if (final_damage > 0 && is_valid_onboarding_sparring_perform(attacker, defender,
        pending_action)) {
        QUEST_D->record_action_progress(attacker,
            "onboarding_sparring_hit", defender, "success");
        sparring_complete = 1;
    }
    combat_service->consume_equipment_durability(attacker, defender);

    if (final_damage > 0) {
        combat_service->send_damage_packets(attacker, defender, final_damage,
            defender->query("combat/hp") || 0, damage_protection);
    }
    apply_pending_perform_status_effects(attacker, defender);
    if (mapp(pending_action)) {
        SKILL_BEHAVIOR_D->consume_completed_action_statuses(attacker, defender,
            pending_action);
        clear_pending_perform_action(attacker);
    }
    if (sparring_complete) {
        combat_service->combat_msg(attacker,
            "演武木人受力后微微后仰，文昌武师示意这一式已足够。\n");
        combat_service->stop_fight(attacker);
    }
    // 奖励与房间刷新可能销毁败方；先保存结果，上层不得再查询该对象。
    defeated = (nonlethal && defeated) || (defender->query("combat/hp") || 0) <= 0;
    if (defeated && !nonlethal)
        combat_service->handle_npc_defeat_effects(defender, attacker);

    return defeated;
}
