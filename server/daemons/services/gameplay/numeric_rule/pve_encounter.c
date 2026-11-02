/**
 * @file    numeric_rule/pve_encounter.c
 * @brief   PVE 遭遇与 TTK 预算
 *
 * @par Description:
 * 含 {query_pve_encounter_type_config, query_pve_budget_tier, estimate_budget_damage, query_pve_ttk_budget}。
 *
 * 本模块是 pve_encounter 域纯规则片段，由 `numeric_rule_d.c` 继承；
 * 不持有 daemon 状态（nosave 全部留在 facade），
 * 配置经 facade 的 query_section/query_section_int 读取。
 */

#pragma strict_types
#pragma save_types

#include <system/services.h>
#include <game/skill.h>
#include <game_constants.h>
#include "defs.h"
mapping query_combat_formula(string formula_name);
varargs mapping query_derived_combat_attributes(object player, object target);
int query_mapping_int(mapping source, string key, int default_value);
int query_max_skill_level();
int query_player_exp_skill_cap(object player);
mapping query_pve_budget_framework();
int scale_budget_value(int value, int factor_bp);
mapping query_pve_ttk_budget(object player, string encounter_type);
mapping query_pve_encounter_type_config(string encounter_type)
{
    mapping framework;
    mapping types;
    mapping type_config;

    if (!stringp(encounter_type) || encounter_type == "")
        return ([]);

    framework = query_pve_budget_framework();
    types = mapp(framework["encounter_types"]) ? framework["encounter_types"] : ([]);
    type_config = types[encounter_type];
    return mapp(type_config) ? copy(type_config) : ([]);
}
mapping query_pve_budget_tier(int skill_cap)
{
    mapping framework;
    mapping tier;
    mapping fallback;
    mixed *tiers;
    int max_cap;
    int min_cap;

    framework = query_pve_budget_framework();
    tiers = framework["tiers"];
    if (!arrayp(tiers) || sizeof(tiers) == 0)
        return ([]);

    foreach (tier in tiers) {
        if (!mapp(tier))
            continue;
        min_cap = tier["min_skill_cap"] || 0;
        max_cap = tier["max_skill_cap"] || query_max_skill_level();
        if (skill_cap >= min_cap && skill_cap <= max_cap)
            return copy(tier);
        if (skill_cap >= min_cap)
            fallback = tier;
    }

    return mapp(fallback) ? copy(fallback) : copy(tiers[0]);
}
private int estimate_budget_damage(int attack, int defense, mapping damage_formula)
{
    int base;
    int damage;
    int min_damage;
    int random_average_bp;
    int random_max_bp;
    int random_min_bp;
    int reduction_bp;
    int skill_factor;

    base = query_mapping_int(damage_formula, "base", 1);
    skill_factor = query_mapping_int(damage_formula, "skill_factor_bp", BP_BASE);
    random_min_bp = query_mapping_int(damage_formula, "random_min_bp", 9500);
    random_max_bp = query_mapping_int(damage_formula, "random_max_bp", 10500);
    random_average_bp = (random_min_bp + random_max_bp) / 2;
    min_damage = query_mapping_int(damage_formula, "min", 1);
    reduction_bp = defense * BP_BASE / (defense + attack * 16 / 10 + 8000);
    if (reduction_bp < 500)
        reduction_bp = 500;
    if (reduction_bp > 7500)
        reduction_bp = 7500;

    damage = base + attack * skill_factor / BP_BASE;
    damage = damage * random_average_bp / BP_BASE;
    damage = damage * (BP_BASE - reduction_bp) / BP_BASE;
    return damage < min_damage ? min_damage : damage;
}
mapping query_pve_ttk_budget(object player, string encounter_type)
{
    mapping attrs;
    mapping defaults;
    mapping damage_formula;
    mapping framework;
    mapping recommended_enemy;
    mapping tier;
    mapping type_config;
    int enemy_attack;
    int enemy_damage_factor;
    int enemy_defense;
    int expected_damage;
    int hit_rate_bp;
    int player_round_damage;
    int skill_cap;
    int survival_rounds;
    int target_ttk;

    if (!objectp(player))
        return ([ "ok": 0, "error": "invalid_player" ]);

    framework = query_pve_budget_framework();
    defaults = mapp(framework["defaults"]) ? framework["defaults"] : ([]);
    if (!stringp(encounter_type) || encounter_type == "")
        encounter_type = defaults["encounter_type"] || "normal";
    type_config = query_pve_encounter_type_config(encounter_type);
    if (!mapp(type_config) || sizeof(keys(type_config)) == 0)
        return ([ "ok": 0, "error": "unknown_encounter_type", "encounter_type": encounter_type ]);

    skill_cap = query_player_exp_skill_cap(player);
    tier = query_pve_budget_tier(skill_cap);
    attrs = query_derived_combat_attributes(player);
    damage_formula = query_combat_formula("damage");
    hit_rate_bp = query_mapping_int(tier, "expected_hit_rate_bp",
        query_mapping_int(defaults, "expected_hit_rate_bp", 7500));
    if (hit_rate_bp <= 0)
        hit_rate_bp = 7500;
    if (hit_rate_bp > BP_BASE)
        hit_rate_bp = BP_BASE;

    target_ttk = query_mapping_int(tier, "target_ttk_rounds",
        query_mapping_int(defaults, "target_ttk_rounds", 8));
    target_ttk = scale_budget_value(target_ttk,
        query_mapping_int(type_config, "ttk_factor_bp", BP_BASE));
    if (target_ttk < 1)
        target_ttk = 1;
    survival_rounds = query_mapping_int(tier, "player_survival_rounds",
        query_mapping_int(defaults, "player_survival_rounds", 12));
    survival_rounds = scale_budget_value(survival_rounds,
        query_mapping_int(type_config, "survival_factor_bp", BP_BASE));
    if (survival_rounds < 1)
        survival_rounds = 1;

    enemy_defense = scale_budget_value(attrs["attack"] || 0,
        query_mapping_int(tier, "enemy_defense_factor_bp",
        query_mapping_int(defaults, "enemy_defense_factor_bp", 4500)));
    player_round_damage = estimate_budget_damage(attrs["attack"] || 0, enemy_defense, damage_formula);
    expected_damage = player_round_damage * hit_rate_bp / BP_BASE;
    if (expected_damage < query_mapping_int(defaults, "min_expected_round_damage", 1))
        expected_damage = query_mapping_int(defaults, "min_expected_round_damage", 1);

    enemy_damage_factor = query_mapping_int(type_config, "enemy_damage_factor_bp",
        query_mapping_int(defaults, "enemy_damage_factor_bp", BP_BASE));
    enemy_attack = ((attrs["max_hp"] || 100) / survival_rounds)
        * BP_BASE / (BP_BASE - 500);
    enemy_attack = scale_budget_value(enemy_attack, enemy_damage_factor);
    if (enemy_attack < 1)
        enemy_attack = 1;

    recommended_enemy = ([
        "max_hp": expected_damage * target_ttk,
        "attack": enemy_attack,
        "defense": enemy_defense,
        "expected_hit_rate_bp": hit_rate_bp,
        "expected_player_round_damage": expected_damage,
    ]);

    return ([
        "ok": 1,
        "encounter_type": encounter_type,
        "skill_cap": skill_cap,
        "tier_id": tier["id"] || "",
        "target_ttk_rounds": target_ttk,
        "player_survival_rounds": survival_rounds,
        "player_attrs": attrs,
        "recommended_enemy": recommended_enemy,
        "budget_only": 1,
    ]);
}
