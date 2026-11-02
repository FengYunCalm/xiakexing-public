/**
 * @file    combat_math.c
 * @brief   战斗数值计算辅助子服务



 */

#pragma strict_types
#pragma save_types

#include <system/services.h>
#include <game_constants.h>

#define BP_BASE 10000

nosave int _derived_attr_cache_depth;
nosave mapping _derived_attr_cache;
// B1 派生属性读模型（跨回合层）：快照寄居玩家 temp（vitals.c token-snapshot
// 同款，随对象析构自清），token＝who vital 代＋目标标识＋config 代。失效权
// 威＝who 侧 vital_maxima_generation（属性/技能/buff/装备变更均挂 invalidate）；
// 目标维入键（突破词条 vs-X 条件消费 target）；NPC 无失效链，门槛外一律回落
// 直算（原语义）；token 含 skill config 代（热重载全局维，vitals.c 同款护栏）。
// 掷点与耐久扣减副作用不在本层（U1 边界）。回退开关置 0 即旁
// 路（旧 temp 留存无害，读侧 token 校验兜底）；旁路/NPC 均不计 miss。
nosave int _derived_attr_model_enabled = 1;
// 参与度计数（B1 量化证据）：hit＝token 全匹配复用；miss＝参与下回退直算。
// 旁路与 NPC 门槛外均不计数。Δ 口径由 measure 窗口差分消费，无重置面。
nosave int _derived_attr_model_hits;
nosave int _derived_attr_model_misses;
#define DERIVED_ATTR_MODEL_TEMP "combat/derived_attr_model"

private void consume_weapon_durability(object who, int amount);
private void consume_armor_durability(object who, int amount);
private int query_snapshot_int(mapping snapshot, string key,
    int default_value);

// 层序归位后角色注记：玩家面窗内复用已被外层 temp 模型全遮蔽
// （模型先于深度门被咨询），本窗内缓存的实服务对象收窄为 NPC 面
void begin_derived_attribute_cache()
{
    _derived_attr_cache_depth++;
    if (!mapp(_derived_attr_cache))
        _derived_attr_cache = ([]);
}

void end_derived_attribute_cache()
{
    _derived_attr_cache_depth--;
    if (_derived_attr_cache_depth > 0)
        return;

    _derived_attr_cache_depth = 0;
    _derived_attr_cache = ([]);
}

void clear_derived_attribute_cache()
{
    if (_derived_attr_cache_depth > 0)
        _derived_attr_cache = ([]);
}

private string derived_attribute_cache_key(object who, object target)
{
    if (!objectp(who))
        return "";

    return file_name(who) + "|" + (objectp(target) ? file_name(target) : "");
}

public void set_derived_attr_read_model_enabled(int flag)
{
    // 回退开关：置 0 即旁路读写；各玩家 temp 旧快照由读侧 token 校验兜底
    _derived_attr_model_enabled = flag ? 1 : 0;
}

public int query_derived_attr_read_model_enabled()
{
    return _derived_attr_model_enabled;
}

public mapping query_derived_attr_model_stats()
{
    return (["hits": _derived_attr_model_hits,
             "misses": _derived_attr_model_misses,
             "enabled": _derived_attr_model_enabled]);
}

/** 参与门槛：快照失效链只覆盖玩家面（vital generation 继承自 player.c）；
 * living/NPC 中 debuff 无失效钩子，入模将终身陈旧——NPC 一律回落直算。
 * （strict_types 先声明后用：必须位于 token 之前——r10d 教训） */
private int derived_attr_model_eligible(object who)
{
    return _derived_attr_model_enabled && objectp(who)
        && function_exists("query_xk_vital_maxima_generation", who);
}

private string derived_attr_model_token(object who, object target)
{
    // 目标维：突破词条 vs-X 类条件显式消费 target（query_numeric_effects 链），
    // 同玩家先后打不同目标必须换键——稳定标识用 file_name 即可
    // skill config 代：突破元数据（SKILL_D get_skill_definition breakthroughs）
    // 是派生属性直接输入面，热重载只 bump 全局代不触碰玩家 vital 代——
    // vitals.c 同批值已挂同款护栏，此处必须对齐
    return derived_attr_model_eligible(who)
        ? who->query_xk_vital_maxima_generation() + "|"
          + (objectp(target) ? file_name(target) : "-") + "|cfg"
          + NUMERIC_RULE_D->query_numeric_config_generation()
          + "|sk" + SKILL_D->query_skill_config_generation()
        : "";
}

/** 跨回合层读：玩家 temp 快照 token 全匹配才复用（防御副本）。 */
private mapping query_derived_attr_model(object who, object target)
{
    mixed snapshot;

    if (!derived_attr_model_eligible(who))
        return 0;
    snapshot = who->query_temp(DERIVED_ATTR_MODEL_TEMP);
    if (!arrayp(snapshot) || sizeof(snapshot) != 2 || !mapp(snapshot[1]))
        return 0;
    if (snapshot[0] != derived_attr_model_token(who, target))
        return 0;
    _derived_attr_model_hits++;
    return copy(snapshot[1]);
}

private void store_derived_attr_model(object who, object target,
    mapping attrs)
{
    if (!derived_attr_model_eligible(who) || !mapp(attrs))
        return;
    who->set_temp(DERIVED_ATTR_MODEL_TEMP,
        ({ derived_attr_model_token(who, target), copy(attrs) }));
}

private mapping query_derived_attributes(object who, object target)
{
    mapping attrs;
    string key;

    if (!objectp(who))
        return ([]);
    // B1 层序归位：模型咨询最外置、先于深度门——战斗主路径全程活在
    // begin/end 深度窗内，模型若留在门后则跨回合复用永不发生（r10 系列
    // Δhits 构造性零的根因）。token 含玩家代＋目标＋双全局代，有效性
    // 严格强于无代的 depth 键；命中先于 depth 缓存返回属既定语义。
    attrs = query_derived_attr_model(who, target);
    if (mapp(attrs))
        return attrs;
    // miss 单次计数：eligible 已含 enabled——NPC 门槛外的查询不入账，
    // 否则 PvE 攻防对称双调会把 defender=NPC 的非参与查询灌满 misses
    if (derived_attr_model_eligible(who))
        _derived_attr_model_misses++;
    if (_derived_attr_cache_depth <= 0) {
        attrs = NUMERIC_RULE_D->query_derived_combat_attributes(who, target);
        store_derived_attr_model(who, target, attrs);
        return mapp(attrs) ? copy(attrs) : ([]);
    }
    // 深度窗内：回合内缓存照旧（NPC 等门槛外对象仍享窗内复用）；
    // 窗内首算同步回填 temp（双回填，miss 已在上方单计）
    if (!mapp(_derived_attr_cache))
        _derived_attr_cache = ([]);
    key = derived_attribute_cache_key(who, target);
    attrs = _derived_attr_cache[key];
    if (!mapp(attrs)) {
        attrs = NUMERIC_RULE_D->query_derived_combat_attributes(who, target);
        _derived_attr_cache[key] = mapp(attrs) ? copy(attrs) : ([]);
        store_derived_attr_model(who, target, attrs);
    }
    attrs = copy(attrs);

    return attrs;
}

private int fold_snapshot_hash(int hash, int value)
{
    hash = (hash * 33 + value) % 2147483647;
    if (hash < 0)
        hash = -hash;
    return hash;
}

int query_damage_snapshot_hash(mapping snapshot)
{
    string *fields;
    string field;
    int hash;

    if (!mapp(snapshot))
        return 0;
    fields = ({
        "attack", "defense", "armor_break", "critical", "critical_resist",
        "armor_break_defense_factor_bp", "armor_break_flat", "armor_break_cap_bp",
        "reduction_attack_factor_bp", "reduction_flat", "reduction_min_bp", "reduction_max_bp",
        "damage_base", "damage_skill_factor_bp", "damage_random_min_bp", "damage_random_max_bp",
        "damage_min", "variance_roll_bp", "critical_base_bp", "critical_swing_bp",
        "critical_flat", "critical_min", "critical_max", "critical_roll", "critical_damage_factor_bp",
    });
    hash = 5381;
    foreach (field in fields)
        hash = fold_snapshot_hash(hash, query_snapshot_int(snapshot, field, 0));
    return hash > 0 ? hash : 1;
}

/* L-174：一次命中对每个实装防具槽各磨损一次；预算必须按实装槽数计入
 * armor_loss_per_hit × slots，才能与实际磨损和修理成本同口径。 */
private int count_equipped_armor_slots(object who)
{
    object armor;
    string *slots;
    string slot;
    int count;

    if (!objectp(who) || !function_exists("query_temp", who))
        return 0;
    slots = EQUIPMENT_ARMOR_SLOTS;
    foreach (slot in slots) {
        armor = who->query_temp("armor/" + slot);
        if (!objectp(armor) || !function_exists("consume_durability", armor))
            continue;
        count++;
    }
    return count;
}

void consume_equipment_durability(object attacker, object defender)
{
    mapping report;
    int armor_slots;

    armor_slots = count_equipped_armor_slots(defender);
    report = NUMERIC_RULE_D->query_equipment_durability_budget_report(([
        "action_type": "combat_hit",
        "armor_slots": armor_slots,
    ]));
    if (mapp(report) && report["block_apply"])
        return;

    consume_weapon_durability(attacker,
        mapp(report) ? (report["weapon_loss_per_hit"] || 0) : 1);
    consume_armor_durability(defender,
        mapp(report) ? (report["armor_loss_per_hit"] || 0) : 1);
}

private void consume_armor_durability(object who, int amount)
{
    object armor;
    string *slots;
    string slot;

    if (!objectp(who))
        return;
    if (amount <= 0)
        return;

    slots = EQUIPMENT_ARMOR_SLOTS;
    foreach (slot in slots) {
        armor = who->query_temp("armor/" + slot);
        if (!objectp(armor) || !function_exists("consume_durability", armor))
            continue;

        armor->consume_durability(amount);
    }
}

private void consume_weapon_durability(object who, int amount)
{
    object weapon;
    int before_durability;
    int after_durability;

    if (!objectp(who))
        return;
    if (amount <= 0)
        return;

    weapon = who->query_temp("weapon");
    if (!objectp(weapon) || !function_exists("consume_durability", weapon))
        return;

    before_durability = function_exists("query_durability", weapon)
        ? weapon->query_durability() : 0;
    weapon->consume_durability(amount);
    after_durability = function_exists("query_durability", weapon)
        ? weapon->query_durability() : before_durability;
    if (after_durability < before_durability
        && ITEM_D->is_training_weapon(weapon))
        QUEST_D->record_action_progress(who,
            "training_weapon_worn", weapon, "success");
}

private int clamp_formula_value(int value, int min_value, int max_value)
{
    if (max_value >= min_value && value > max_value)
        return max_value;
    if (value < min_value)
        return min_value;
    return value;
}

private int query_snapshot_int(mapping snapshot, string key, int default_value)
{
    mixed value;

    value = mapp(snapshot) ? snapshot[key] : 0;
    if (undefinedp(value) || !intp(value))
        return default_value;
    return value;
}

private int query_weapon_attack_bonus(object who)
{
    object weapon;

    if (!objectp(who))
        return 0;

    weapon = who->query_temp("weapon");
    if (!objectp(weapon))
        return 0;

    if (function_exists("damage_bonus", weapon))
        return weapon->damage_bonus(who, 0);
    if (function_exists("query_damage", weapon))
        return weapon->query_damage();

    return 0;
}

private int query_formula_int(mapping formula, string key, int default_value)
{
    mixed value;

    value = mapp(formula) ? formula[key] : 0;
    if (undefinedp(value) || !intp(value))
        return default_value;
    return value;
}

private int query_weapon_stat_bonus(object who, string stat)
{
    object weapon;
    mapping effects;

    if (!objectp(who) || !stringp(stat) || stat == "")
        return 0;

    weapon = who->query_temp("weapon");
    if (!objectp(weapon))
        return 0;
    effects = weapon->query("effects");
    return mapp(effects) && intp(effects[stat]) ? effects[stat] : 0;
}

private int query_armor_defense_bonus(object who)
{
    object armor;
    string *slots;
    string slot;
    int total;

    if (!objectp(who))
        return 0;

    slots = EQUIPMENT_ARMOR_SLOTS;
    total = 0;

    foreach (slot in slots) {
        armor = who->query_temp("armor/" + slot);
        if (!objectp(armor))
            continue;

        if (function_exists("query_defense", armor))
            total += armor->query_defense();
    }

    return total;
}

private int query_pending_perform_hit_bonus(object attacker, object defender)
{
    mapping action;
    mapping effects;

    if (!objectp(attacker))
        return 0;

    effects = attacker->query_temp("combat/perform/effects");
    if (mapp(effects))
        return effects["hit_bonus"] || 0;

    action = attacker->query_temp("combat/perform/action");
    if (!mapp(action))
        return 0;

    effects = NUMERIC_RULE_D->query_skill_action_effects(action, attacker, defender);
    return mapp(effects) ? (effects["hit_bonus"] || 0) : 0;
}

private int query_pending_perform_armor_break_bonus(object attacker, object defender)
{
    mapping action;
    mapping effects;

    if (!objectp(attacker))
        return 0;

    effects = attacker->query_temp("combat/perform/effects");
    if (mapp(effects))
        return effects["armor_break"] || 0;

    action = attacker->query_temp("combat/perform/action");
    if (!mapp(action))
        return 0;

    effects = NUMERIC_RULE_D->query_skill_action_effects(action, attacker, defender);
    return mapp(effects) ? (effects["armor_break"] || 0) : 0;
}

private int query_pending_perform_critical_bonus(object attacker, object defender)
{
    mapping action;
    mapping effects;

    if (!objectp(attacker))
        return 0;

    effects = attacker->query_temp("combat/perform/effects");
    if (mapp(effects))
        return effects["critical_bonus"] || 0;

    action = attacker->query_temp("combat/perform/action");
    if (!mapp(action))
        return 0;

    effects = NUMERIC_RULE_D->query_skill_action_effects(action, attacker, defender);
    return mapp(effects) ? (effects["critical_bonus"] || 0) : 0;
}

private void clear_exert_effects_if_empty(object who, mapping effects)
{
    if (!objectp(who) || !mapp(effects))
        return;
    if ((effects["parry_bonus"] || 0) > 0 || (effects["shield_bonus"] || 0) > 0)
        return;

    who->delete_temp("combat/exert/effects");
    who->delete_temp("combat/exert/skill");
    who->delete_temp("combat/exert/action");
}

varargs int query_attack(object who, object target)
{
    mapping attrs;
    int attack;

    if (!objectp(who))
        return 100;

    attrs = query_derived_attributes(who, target);
    attack = attrs["attack"] || 0;
    if (!intp(attack) || attack <= 0)
        attack = 100 + (who->query("attributes/strength") || 10) * 20;

    attack += query_weapon_attack_bonus(who);
    attack += query_weapon_stat_bonus(who, "attack");

    return attack < 1 ? 1 : attack;
}

varargs int query_defense(object who, object target)
{
    mapping attrs;
    mapping exert_effects;
    int defense;
    int shield_bonus;

    if (!objectp(who))
        return 50;

    attrs = query_derived_attributes(who, target);
    defense = attrs["defense"] || 0;
    if (!intp(defense) || defense < 0)
        defense = 50 + (who->query("attributes/constitution") || 10) * 18;

    defense += query_armor_defense_bonus(who);
    exert_effects = who->query_temp("combat/exert/effects");
    shield_bonus = mapp(exert_effects) ? (exert_effects["shield_bonus"] || 0) : 0;
    defense += shield_bonus;
    if (shield_bonus > 0 && mapp(exert_effects)) {
        map_delete(exert_effects, "shield_bonus");
        clear_exert_effects_if_empty(who, exert_effects);
    }

    if (who->is_busy())
        defense = defense / 3;

    return defense < 0 ? 0 : defense;
}

varargs int query_parry(object who, object target)
{
    mapping attrs;
    mapping exert_effects;
    int parry;
    int parry_bonus;

    if (!objectp(who))
        return 0;

    attrs = query_derived_attributes(who, target);
    parry = attrs["parry"] || 0;
    if (!intp(parry) || parry < 0)
        parry = who->query("attributes/constitution") || 0;

    {
        mapping active_skills;
        string parry_skill;

        active_skills = SKILL_BEHAVIOR_D->query_active_skill_slots(who);
        parry_skill = active_skills["parry"] || "";
        if (parry_skill != "")
            parry += SKILL_D->query_player_skill_level(who, parry_skill);
    }

    exert_effects = who->query_temp("combat/exert/effects");
    parry_bonus = mapp(exert_effects) ? (exert_effects["parry_bonus"] || 0) : 0;
    parry += parry_bonus;
    if (parry_bonus > 0 && mapp(exert_effects)) {
        map_delete(exert_effects, "parry_bonus");
        clear_exert_effects_if_empty(who, exert_effects);
    }
    if (who->is_busy())
        parry = parry / 3;

    return parry < 0 ? 0 : parry;
}

varargs int query_hit(object who, object target)
{
    mapping attrs;

    if (!objectp(who))
        return 0;
    attrs = query_derived_attributes(who, target);
    return (attrs["hit"] || 0) + query_weapon_stat_bonus(who, "hit");
}

varargs int query_dodge(object who, object target)
{
    mapping attrs;

    if (!objectp(who))
        return 0;
    attrs = query_derived_attributes(who, target);
    return (attrs["dodge"] || 0) + query_weapon_stat_bonus(who, "dodge");
}

varargs int query_critical(object who, object target)
{
    mapping attrs;

    if (!objectp(who))
        return 0;
    attrs = query_derived_attributes(who, target);
    return (attrs["critical"] || 0) + query_weapon_stat_bonus(who, "critical");
}

varargs int query_critical_resist(object who, object target)
{
    mapping attrs;

    if (!objectp(who))
        return 0;
    attrs = query_derived_attributes(who, target);
    return (attrs["critical_resist"] || 0) + query_weapon_stat_bonus(who, "critical_resist");
}

varargs int query_armor_break(object who, object target)
{
    mapping attrs;

    if (!objectp(who))
        return 0;
    attrs = query_derived_attributes(who, target);
    return (attrs["armor_break"] || 0) + query_weapon_stat_bonus(who, "armor_break");
}

int calculate_hit_rate(object attacker, object defender)
{
    mapping attack_profile;
    mapping attacker_attrs;
    mapping defender_attrs;
    mapping formula;
    string active_skill;
    int denominator;
    int difference;
    int rate;

    formula = NUMERIC_RULE_D->query_combat_formula("hit");
    attacker_attrs = query_derived_attributes(attacker, defender);
    defender_attrs = query_derived_attributes(defender, attacker);
    difference = (attacker_attrs["hit"] || 0) - (defender_attrs["dodge"] || 0);
    denominator = (attacker_attrs["hit"] || 0) + (defender_attrs["dodge"] || 0)
        + query_formula_int(formula, "flat", 8000);
    if (denominator < 1)
        denominator = 1;
    rate = (query_formula_int(formula, "base_bp", 5000)
        + query_formula_int(formula, "swing_bp", 4000) * difference / denominator) / 100;
    active_skill = SKILL_BEHAVIOR_D->query_active_combat_skill(attacker);
    attack_profile = SKILL_BEHAVIOR_D->query_basic_attack_profile(attacker, active_skill);
    rate += attack_profile["hit_rate_bonus"] || 0;
    rate += query_pending_perform_hit_bonus(attacker, defender);
    if (attack_profile["attack_style"] == "accurate")
        rate += query_formula_int(formula, "accurate_style_bonus", 3);
    rate = clamp_formula_value(rate,
        query_formula_int(formula, "min", 20),
        query_formula_int(formula, "max", 95));

    return rate;
}

private int calculate_critical_rate(mapping attacker_attrs, mapping defender_attrs)
{
    mapping formula;
    int denominator;
    int difference;
    int rate;

    formula = NUMERIC_RULE_D->query_combat_formula("critical");
    difference = (attacker_attrs["critical"] || 0) - (defender_attrs["critical_resist"] || 0);
    denominator = (attacker_attrs["critical"] || 0) + (defender_attrs["critical_resist"] || 0)
        + query_formula_int(formula, "flat", 6000);
    if (denominator < 1)
        denominator = 1;
    rate = (query_formula_int(formula, "base_bp", 500)
        + query_formula_int(formula, "swing_bp", 3500) * difference / denominator) / 100;
    return clamp_formula_value(rate,
        query_formula_int(formula, "min", 0),
        query_formula_int(formula, "max", 50));
}

private mapping normalize_damage_worker_snapshot(mapping snapshot)
{
    mapping defaults;
    string field;

    defaults = ([
        "attack": 1,
        "defense": 0,
        "armor_break": 0,
        "critical": 0,
        "critical_resist": 0,
        "armor_break_defense_factor_bp": 9000,
        "armor_break_flat": 5000,
        "armor_break_cap_bp": 8000,
        "reduction_attack_factor_bp": 16000,
        "reduction_flat": 8000,
        "reduction_min_bp": 500,
        "reduction_max_bp": 7500,
        "damage_base": 1,
        "damage_skill_factor_bp": BP_BASE,
        "damage_random_min_bp": 9500,
        "damage_random_max_bp": 10500,
        "damage_min": 1,
        "variance_roll_bp": 0,
        "critical_base_bp": 500,
        "critical_swing_bp": 3500,
        "critical_flat": 6000,
        "critical_min": 0,
        "critical_max": 50,
        "critical_roll": 100,
        "critical_damage_factor_bp": 15000,
    ]);
    foreach (field in keys(defaults))
        snapshot[field] = query_snapshot_int(snapshot, field, defaults[field]);
    return snapshot;
}

mapping query_damage_worker_snapshot(object attacker, object defender)
{
    mapping attacker_attrs;
    mapping defender_attrs;
    mapping break_formula;
    mapping critical_formula;
    mapping damage_formula;
    mapping reduction_formula;
    string context;
    int armor_break_cap;
    int armor_break;
    int variance_bp;

    break_formula = NUMERIC_RULE_D->query_combat_formula("armor_break");
    critical_formula = NUMERIC_RULE_D->query_combat_formula("critical");
    damage_formula = NUMERIC_RULE_D->query_combat_formula("damage");
    reduction_formula = NUMERIC_RULE_D->query_combat_formula("damage_reduction");
    attacker_attrs = query_derived_attributes(attacker, defender);
    defender_attrs = query_derived_attributes(defender, attacker);
    attacker_attrs["critical"] = query_snapshot_int(attacker_attrs, "critical", 0)
        + query_pending_perform_critical_bonus(attacker, defender);
    armor_break = query_snapshot_int(attacker_attrs, "armor_break", 0)
        + query_pending_perform_armor_break_bonus(attacker, defender);
    context = NUMERIC_RULE_D->query_numeric_context(attacker, defender);
    armor_break_cap = context == "pvp"
        ? query_formula_int(break_formula, "max_pvp_bp", 6500)
        : query_formula_int(break_formula, "max_pve_bp", 8000);
    variance_bp = query_formula_int(damage_formula, "random_max_bp", 10500)
        - query_formula_int(damage_formula, "random_min_bp", 9500);

    return normalize_damage_worker_snapshot(([
        "attack": query_attack(attacker, defender),
        "defense": query_defense(defender, attacker),
        "armor_break": armor_break,
        "critical": query_snapshot_int(attacker_attrs, "critical", 0),
        "critical_resist": query_snapshot_int(defender_attrs, "critical_resist", 0),
        "armor_break_defense_factor_bp": query_formula_int(break_formula, "defense_factor_bp", 9000),
        "armor_break_flat": query_formula_int(break_formula, "flat", 5000),
        "armor_break_cap_bp": armor_break_cap,
        "reduction_attack_factor_bp": query_formula_int(reduction_formula, "attack_factor_bp", 16000),
        "reduction_flat": query_formula_int(reduction_formula, "flat", 8000),
        "reduction_min_bp": query_formula_int(reduction_formula, "min_bp", 500),
        "reduction_max_bp": query_formula_int(reduction_formula, "max_bp", 7500),
        "damage_base": query_formula_int(damage_formula, "base", 1),
        "damage_skill_factor_bp": query_formula_int(damage_formula, "skill_factor_bp", BP_BASE),
        "damage_random_min_bp": query_formula_int(damage_formula, "random_min_bp", 9500),
        "damage_random_max_bp": query_formula_int(damage_formula, "random_max_bp", 10500),
        "damage_min": query_formula_int(damage_formula, "min", 1),
        "variance_roll_bp": variance_bp > 0 ? random(variance_bp + 1) : 0,
        "critical_base_bp": query_formula_int(critical_formula, "base_bp", 500),
        "critical_swing_bp": query_formula_int(critical_formula, "swing_bp", 3500),
        "critical_flat": query_formula_int(critical_formula, "flat", 6000),
        "critical_min": query_formula_int(critical_formula, "min", 0),
        "critical_max": query_formula_int(critical_formula, "max", 50),
        "critical_roll": random(100),
        "critical_damage_factor_bp": query_formula_int(critical_formula, "damage_factor_bp", 15000),
    ]));
}

int calculate_damage_from_snapshot(mapping snapshot)
{
    int armor_break_bp;
    int critical_rate;
    int damage;
    int denominator;
    int effective_defense;
    int reduction_bp;
    int variance_bp;

    if (!mapp(snapshot))
        return 1;
    denominator = query_snapshot_int(snapshot, "armor_break", 0)
        + query_snapshot_int(snapshot, "defense", 0)
        * query_snapshot_int(snapshot, "armor_break_defense_factor_bp", 9000) / BP_BASE
        + query_snapshot_int(snapshot, "armor_break_flat", 5000);
    if (denominator < 1)
        denominator = 1;
    armor_break_bp = query_snapshot_int(snapshot, "armor_break", 0) * BP_BASE / denominator;
    armor_break_bp = clamp_formula_value(armor_break_bp, 0, query_snapshot_int(snapshot, "armor_break_cap_bp", 8000));
    effective_defense = query_snapshot_int(snapshot, "defense", 0) * (BP_BASE - armor_break_bp) / BP_BASE;
    denominator = effective_defense
        + query_snapshot_int(snapshot, "attack", 1)
        * query_snapshot_int(snapshot, "reduction_attack_factor_bp", 16000) / BP_BASE
        + query_snapshot_int(snapshot, "reduction_flat", 8000);
    if (denominator < 1)
        denominator = 1;
    reduction_bp = effective_defense * BP_BASE / denominator;
    reduction_bp = clamp_formula_value(reduction_bp,
        query_snapshot_int(snapshot, "reduction_min_bp", 500),
        query_snapshot_int(snapshot, "reduction_max_bp", 7500));
    damage = query_snapshot_int(snapshot, "damage_base", 1)
        + query_snapshot_int(snapshot, "attack", 1)
        * query_snapshot_int(snapshot, "damage_skill_factor_bp", BP_BASE) / BP_BASE;
    variance_bp = query_snapshot_int(snapshot, "damage_random_max_bp", 10500)
        - query_snapshot_int(snapshot, "damage_random_min_bp", 9500);
    if (variance_bp > 0)
        damage = damage * (query_snapshot_int(snapshot, "damage_random_min_bp", 9500)
            + clamp_formula_value(query_snapshot_int(snapshot, "variance_roll_bp", 0), 0, variance_bp)) / BP_BASE;
    damage = damage * (BP_BASE - reduction_bp) / BP_BASE;

    denominator = query_snapshot_int(snapshot, "critical", 0)
        + query_snapshot_int(snapshot, "critical_resist", 0)
        + query_snapshot_int(snapshot, "critical_flat", 6000);
    if (denominator < 1)
        denominator = 1;
    critical_rate = (query_snapshot_int(snapshot, "critical_base_bp", 500)
        + query_snapshot_int(snapshot, "critical_swing_bp", 3500)
        * (query_snapshot_int(snapshot, "critical", 0)
        - query_snapshot_int(snapshot, "critical_resist", 0)) / denominator) / 100;
    critical_rate = clamp_formula_value(critical_rate,
        query_snapshot_int(snapshot, "critical_min", 0),
        query_snapshot_int(snapshot, "critical_max", 50));
    if (critical_rate > query_snapshot_int(snapshot, "critical_roll", 100))
        damage = damage * query_snapshot_int(snapshot, "critical_damage_factor_bp", 15000) / BP_BASE;
    if (damage < query_snapshot_int(snapshot, "damage_min", 1))
        damage = query_snapshot_int(snapshot, "damage_min", 1);
    return damage;
}

int calculate_damage(object attacker, object defender)
{
    return calculate_damage_from_snapshot(query_damage_worker_snapshot(attacker, defender));
}
